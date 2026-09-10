#define NOMINMAX
#include <windows.h>
#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <limits>

namespace {

constexpr uintptr_t kImageBase = 0x00400000;
constexpr uintptr_t kBeforeMainRender = 0x00727140;
constexpr size_t kBeforeMainRenderScanSize = 0x180;
constexpr uintptr_t kRsCameraBeginUpdate = 0x00619450;
constexpr uintptr_t kRwCameraEndUpdate = 0x007EE180;
constexpr uintptr_t kRwD3D9GetCurrentDevice = 0x007F9D50;

using RsCameraBeginUpdateFn = int(__cdecl*)(void*);
using RwCameraEndUpdateFn = void*(__cdecl*)(void*);
using GetD3DDeviceFn = void*(__cdecl*)();

constexpr int kMaxDownsampleStages = 3;

IDirect3DDevice9* g_surfaceDevice = nullptr;
IDirect3DSurface9* g_msaaColor = nullptr;
IDirect3DSurface9* g_msaaDepth = nullptr;
IDirect3DSurface9* g_downsampleChain[kMaxDownsampleStages] = {};
int g_downsampleCount = 0;
IDirect3DSurface9* g_resolveColor = nullptr;
IDirect3DSurface9* g_resolveDepth = nullptr;
D3DSURFACE_DESC g_colorDesc = {};
D3DFORMAT g_depthFormat = D3DFMT_UNKNOWN;
int g_requestedFactor = 4;
int g_activeFactor = 0;
bool g_resolveActive = false;
bool g_installed = false;
wchar_t g_iniPath[MAX_PATH] = {};

template <typename T>
bool SafeRead(uintptr_t address, T& result) {
    __try {
        result = *reinterpret_cast<const T*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SafeCopy(uintptr_t address, void* result, size_t size) {
    __try {
        memcpy(result, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool IsExecutableAddress(uintptr_t address) {
    MEMORY_BASIC_INFORMATION info = {};
    if (!address || VirtualQuery(reinterpret_cast<const void*>(address), &info,
                                 sizeof(info)) != sizeof(info))
        return false;

    constexpr DWORD executablePages = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                      PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return info.State == MEM_COMMIT && (info.Protect & executablePages) != 0;
}

bool WriteMemory(void* destination, const void* source, size_t size) {
    DWORD oldProtect = 0;
    if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(destination, source, size);
    FlushInstructionCache(GetCurrentProcess(), destination, size);

    DWORD unused = 0;
    VirtualProtect(destination, size, oldProtect, &unused);
    return true;
}

void LoadConfiguration(HMODULE module) {
    wchar_t basePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(module, basePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;

    wchar_t* slash = wcsrchr(basePath, L'\\');
    if (!slash)
        return;

    const size_t room = static_cast<size_t>(MAX_PATH - (slash + 1 - basePath));
    wcscpy_s(slash + 1, room, L"MirrorAntiAliasingFix.ini");
    wcscpy_s(g_iniPath, basePath);

    const int configured = GetPrivateProfileIntW(L"antiAliasing", L"supersample",
                                                 4, g_iniPath);
    if (configured >= 8)
        g_requestedFactor = 8;
    else if (configured >= 4)
        g_requestedFactor = 4;
    else if (configured >= 2)
        g_requestedFactor = 2;
    else
        g_requestedFactor = 1;
}

IDirect3DDevice9* GetDevice() {
    return static_cast<IDirect3DDevice9*>(
        reinterpret_cast<GetD3DDeviceFn>(kRwD3D9GetCurrentDevice)());
}

void ReleaseDownsampleChain() {
    for (int i = 0; i < kMaxDownsampleStages; ++i) {
        if (g_downsampleChain[i]) {
            g_downsampleChain[i]->Release();
            g_downsampleChain[i] = nullptr;
        }
    }
    g_downsampleCount = 0;
}

void ReleaseResolveTargets() {
    ReleaseDownsampleChain();
    if (g_msaaDepth) {
        g_msaaDepth->Release();
        g_msaaDepth = nullptr;
    }
    if (g_msaaColor) {
        g_msaaColor->Release();
        g_msaaColor = nullptr;
    }
    g_surfaceDevice = nullptr;
    g_colorDesc = {};
    g_depthFormat = D3DFMT_UNKNOWN;
    g_activeFactor = 0;
}

void ReleaseFrameSurfaces() {
    if (g_resolveDepth) {
        g_resolveDepth->Release();
        g_resolveDepth = nullptr;
    }
    if (g_resolveColor) {
        g_resolveColor->Release();
        g_resolveColor = nullptr;
    }
    g_resolveActive = false;
}

bool HasStencil(D3DFORMAT format) {
    return format == D3DFMT_D15S1 || format == D3DFMT_D24S8 ||
           format == D3DFMT_D24X4S4;
}

bool CreateResolveTargets(IDirect3DDevice9* device,
                          const D3DSURFACE_DESC& color,
                          const D3DSURFACE_DESC& depth) {
    ReleaseResolveTargets();

    // Plain, single sample surfaces at a multiple of the mirror size. The sister
    // project for SA-MP preview textdraws measured that rendering an off-screen
    // camera pass into a multisampled target loses the depth comparison, at
    // every sample count, while the same swap into a single sample target
    // reproduces the original image exactly. Supersampling does the
    // anti-aliasing here for the same reason, and it smooths texture detail
    // inside the reflection as well as its silhouettes.
    int factor = g_requestedFactor;
    while (factor >= 1) {
        const UINT width = color.Width * static_cast<UINT>(factor);
        const UINT height = color.Height * static_cast<UINT>(factor);
        IDirect3DSurface9* colorSurface = nullptr;
        IDirect3DSurface9* depthSurface = nullptr;

        HRESULT colorResult = device->CreateRenderTarget(
            width, height, color.Format, D3DMULTISAMPLE_NONE, 0, FALSE,
            &colorSurface, nullptr);
        HRESULT depthResult = E_FAIL;
        if (SUCCEEDED(colorResult)) {
            depthResult = device->CreateDepthStencilSurface(
                width, height, depth.Format, D3DMULTISAMPLE_NONE, 0, FALSE,
                &depthSurface, nullptr);
        }

        if (SUCCEEDED(colorResult) && SUCCEEDED(depthResult)) {
            g_surfaceDevice = device;
            g_msaaColor = colorSurface;
            g_msaaDepth = depthSurface;
            g_colorDesc = color;
            g_depthFormat = depth.Format;
            g_activeFactor = factor;

            // StretchRect filters bilinearly and therefore reads only a 2x2
            // neighbourhood, so a single 4x or 8x reduction would discard most
            // of the rendered image instead of averaging it. Halving repeatedly
            // keeps every step at exactly 2:1, where the bilinear tap lands in
            // the centre of each 2x2 block and averages all four texels.
            for (int step = factor / 2; step >= 2; step /= 2) {
                IDirect3DSurface9* stage = nullptr;
                if (FAILED(device->CreateRenderTarget(
                        color.Width * static_cast<UINT>(step),
                        color.Height * static_cast<UINT>(step), color.Format,
                        D3DMULTISAMPLE_NONE, 0, FALSE, &stage, nullptr))) {
                    ReleaseDownsampleChain();
                    break;
                }
                g_downsampleChain[g_downsampleCount++] = stage;
            }
            return true;
        }

        if (depthSurface)
            depthSurface->Release();
        if (colorSurface)
            colorSurface->Release();
        factor /= 2;
    }

    return false;
}

bool EnsureResolveTargets(IDirect3DDevice9* device,
                          const D3DSURFACE_DESC& color,
                          const D3DSURFACE_DESC& depth) {
    if (g_msaaColor && g_msaaDepth && g_surfaceDevice == device &&
        g_activeFactor == g_requestedFactor &&
        g_colorDesc.Width == color.Width && g_colorDesc.Height == color.Height &&
        g_colorDesc.Format == color.Format && g_depthFormat == depth.Format)
        return true;
    return CreateResolveTargets(device, color, depth);
}

bool ActivateResolveTargets(IDirect3DDevice9* device) {
    HRESULT result = device->SetDepthStencilSurface(nullptr);
    if (SUCCEEDED(result))
        result = device->SetRenderTarget(0, g_msaaColor);
    if (SUCCEEDED(result))
        result = device->SetDepthStencilSurface(g_msaaDepth);
    if (FAILED(result)) {
        device->SetDepthStencilSurface(nullptr);
        device->SetRenderTarget(0, g_resolveColor);
        device->SetDepthStencilSurface(g_resolveDepth);
        ReleaseResolveTargets();
        return false;
    }

    DWORD clearFlags = D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER;
    if (HasStencil(g_depthFormat))
        clearFlags |= D3DCLEAR_STENCIL;
    result = device->Clear(0, nullptr, clearFlags, D3DCOLOR_ARGB(255, 0, 0, 0),
                           1.0f, 0);
    return SUCCEEDED(result);
}

int __cdecl RsCameraBeginUpdateHook(void* camera) {
    const int result = reinterpret_cast<RsCameraBeginUpdateFn>(
        kRsCameraBeginUpdate)(camera);
    if (!result)
        return result;

    IDirect3DDevice9* device = GetDevice();
    if (!device)
        return result;

    ReleaseFrameSurfaces();
    if (FAILED(device->GetRenderTarget(0, &g_resolveColor)) || !g_resolveColor ||
        FAILED(device->GetDepthStencilSurface(&g_resolveDepth)) || !g_resolveDepth) {
        ReleaseFrameSurfaces();
        return result;
    }

    D3DSURFACE_DESC color = {};
    D3DSURFACE_DESC depth = {};
    if (FAILED(g_resolveColor->GetDesc(&color)) ||
        FAILED(g_resolveDepth->GetDesc(&depth))) {
        ReleaseFrameSurfaces();
        return result;
    }

    if (!EnsureResolveTargets(device, color, depth) ||
        !ActivateResolveTargets(device)) {
        ReleaseFrameSurfaces();
        return result;
    }

    g_resolveActive = true;
    return result;
}

void* __cdecl RwCameraEndUpdateHook(void* camera) {
    void* result = reinterpret_cast<RwCameraEndUpdateFn>(
        kRwCameraEndUpdate)(camera);

    if (!g_resolveActive)
        return result;

    IDirect3DDevice9* device = GetDevice();
    if (device && g_resolveColor && g_resolveDepth && g_msaaColor) {
        device->SetDepthStencilSurface(nullptr);
        HRESULT targetResult = device->SetRenderTarget(0, g_resolveColor);
        HRESULT depthResult = device->SetDepthStencilSurface(g_resolveDepth);
        if (SUCCEEDED(targetResult) && SUCCEEDED(depthResult)) {
            const D3DTEXTUREFILTERTYPE filter =
                g_activeFactor > 1 ? D3DTEXF_LINEAR : D3DTEXF_NONE;
            IDirect3DSurface9* stageSource = g_msaaColor;
            HRESULT reduction = S_OK;
            for (int i = 0; i < g_downsampleCount && SUCCEEDED(reduction); ++i) {
                reduction = device->StretchRect(stageSource, nullptr,
                                                g_downsampleChain[i], nullptr,
                                                D3DTEXF_LINEAR);
                stageSource = g_downsampleChain[i];
            }
            if (SUCCEEDED(reduction)) {
                device->StretchRect(stageSource, nullptr, g_resolveColor,
                                    nullptr, filter);
            }
        }
    }

    ReleaseFrameSurfaces();
    return result;
}

bool RedirectRelativeCall(uintptr_t callAddress, uintptr_t newTarget) {
    uint8_t call[5] = {};
    if (!SafeCopy(callAddress, call, sizeof(call)) || call[0] != 0xE8)
        return false;

    const intptr_t distance = newTarget - (callAddress + sizeof(call));
    if (distance < std::numeric_limits<int32_t>::min() ||
        distance > std::numeric_limits<int32_t>::max())
        return false;

    const int32_t displacement = static_cast<int32_t>(distance);
    memcpy(&call[1], &displacement, sizeof(displacement));
    return WriteMemory(reinterpret_cast<void*>(callAddress), call, sizeof(call));
}

uintptr_t FindRelativeCall(uintptr_t start, size_t size, uintptr_t target) {
    for (size_t offset = 0; offset + 5 <= size; ++offset) {
        uint8_t opcode = 0;
        if (!SafeRead<uint8_t>(start + offset, opcode) || opcode != 0xE8)
            continue;

        int32_t displacement = 0;
        if (!SafeRead<int32_t>(start + offset + 1, displacement))
            continue;
        if (start + offset + 5 + displacement == target)
            return start + offset;
    }
    return 0;
}

bool InstallHooks() {
    if (reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) != kImageBase ||
        !IsExecutableAddress(kBeforeMainRender) ||
        !IsExecutableAddress(kRsCameraBeginUpdate) ||
        !IsExecutableAddress(kRwCameraEndUpdate) ||
        !IsExecutableAddress(kRwD3D9GetCurrentDevice))
        return false;

    const uintptr_t beginCall = FindRelativeCall(
        kBeforeMainRender, kBeforeMainRenderScanSize, kRsCameraBeginUpdate);
    const uintptr_t endCall = FindRelativeCall(
        kBeforeMainRender, kBeforeMainRenderScanSize, kRwCameraEndUpdate);
    if (!beginCall || !endCall)
        return false;

    // The end hook is harmless without the begin hook, so install it first.
    if (!RedirectRelativeCall(endCall,
                              reinterpret_cast<uintptr_t>(&RwCameraEndUpdateHook)) ||
        !RedirectRelativeCall(beginCall,
                              reinterpret_cast<uintptr_t>(&RsCameraBeginUpdateHook)))
        return false;

    g_installed = true;
    return true;
}

DWORD WINAPI Initialize(void* parameter) {
    LoadConfiguration(static_cast<HMODULE>(parameter));
    Sleep(1000);
    InstallHooks();
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        if (HANDLE thread = CreateThread(nullptr, 0, Initialize, instance, 0, nullptr))
            CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH && g_installed) {
        g_installed = false;
    }
    return TRUE;
}
