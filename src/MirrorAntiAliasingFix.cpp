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
constexpr uintptr_t kDefinedState = 0x00734650;

using RsCameraBeginUpdateFn = int(__cdecl*)(void*);
using RwCameraEndUpdateFn = void*(__cdecl*)(void*);
using GetD3DDeviceFn = void*(__cdecl*)();

IDirect3DDevice9* g_surfaceDevice = nullptr;
IDirect3DSurface9* g_msaaColor = nullptr;
IDirect3DSurface9* g_msaaDepth = nullptr;
IDirect3DSurface9* g_resolveColor = nullptr;
IDirect3DSurface9* g_resolveDepth = nullptr;
D3DSURFACE_DESC g_colorDesc = {};
D3DFORMAT g_depthFormat = D3DFMT_UNKNOWN;
D3DMULTISAMPLE_TYPE g_activeSamples = D3DMULTISAMPLE_NONE;
int g_requestedSamples = 8;
bool g_resolveActive = false;
bool g_installed = false;
bool g_haveOldMultisampleState = false;
DWORD g_oldMultisampleState = FALSE;

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
    wchar_t iniPath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(module, iniPath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;

    wchar_t* slash = wcsrchr(iniPath, L'\\');
    if (!slash)
        return;
    wcscpy_s(slash + 1, static_cast<size_t>(MAX_PATH - (slash + 1 - iniPath)),
             L"MirrorAntiAliasingFix.ini");

    const int configured = GetPrivateProfileIntW(L"antiAliasing", L"sampleCount",
                                                   8, iniPath);
    if (configured >= 16)
        g_requestedSamples = 16;
    else if (configured >= 8)
        g_requestedSamples = 8;
    else if (configured >= 4)
        g_requestedSamples = 4;
    else
        g_requestedSamples = 2;
}

IDirect3DDevice9* GetDevice() {
    return static_cast<IDirect3DDevice9*>(
        reinterpret_cast<GetD3DDeviceFn>(kRwD3D9GetCurrentDevice)());
}

void ReleaseResolveTargets() {
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
    g_activeSamples = D3DMULTISAMPLE_NONE;
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
    g_haveOldMultisampleState = false;
}

bool HasStencil(D3DFORMAT format) {
    return format == D3DFMT_D15S1 || format == D3DFMT_D24S8 ||
           format == D3DFMT_D24X4S4;
}

bool CreateResolveTargets(IDirect3DDevice9* device,
                          const D3DSURFACE_DESC& color,
                          const D3DSURFACE_DESC& depth) {
    ReleaseResolveTargets();

    int samples = g_requestedSamples;
    while (samples >= 2) {
        auto type = static_cast<D3DMULTISAMPLE_TYPE>(samples);
        IDirect3DSurface9* colorSurface = nullptr;
        IDirect3DSurface9* depthSurface = nullptr;

        HRESULT colorResult = device->CreateRenderTarget(
            color.Width, color.Height, color.Format, type, 0, FALSE,
            &colorSurface, nullptr);
        HRESULT depthResult = E_FAIL;
        if (SUCCEEDED(colorResult)) {
            depthResult = device->CreateDepthStencilSurface(
                color.Width, color.Height, depth.Format, type, 0, TRUE,
                &depthSurface, nullptr);
        }

        if (SUCCEEDED(colorResult) && SUCCEEDED(depthResult)) {
            g_surfaceDevice = device;
            g_msaaColor = colorSurface;
            g_msaaDepth = depthSurface;
            g_colorDesc = color;
            g_depthFormat = depth.Format;
            g_activeSamples = type;
            return true;
        }

        if (depthSurface)
            depthSurface->Release();
        if (colorSurface)
            colorSurface->Release();
        samples /= 2;
    }

    return false;
}

bool EnsureResolveTargets(IDirect3DDevice9* device,
                          const D3DSURFACE_DESC& color,
                          const D3DSURFACE_DESC& depth) {
    if (g_msaaColor && g_msaaDepth && g_surfaceDevice == device &&
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
    g_haveOldMultisampleState = SUCCEEDED(device->GetRenderState(
        D3DRS_MULTISAMPLEANTIALIAS, &g_oldMultisampleState));
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

void __cdecl DefinedStateForMirrorHook() {
    reinterpret_cast<void(__cdecl*)()>(kDefinedState)();
    if (g_resolveActive) {
        if (IDirect3DDevice9* device = GetDevice())
            device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS, TRUE);
    }
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
        HRESULT resolveResult = E_FAIL;
        if (SUCCEEDED(targetResult) && SUCCEEDED(depthResult)) {
            resolveResult = device->StretchRect(g_msaaColor, nullptr,
                                                g_resolveColor, nullptr,
                                                D3DTEXF_NONE);
        }
        if (g_haveOldMultisampleState) {
            device->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,
                                   g_oldMultisampleState);
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
        !IsExecutableAddress(kRwD3D9GetCurrentDevice) ||
        !IsExecutableAddress(kDefinedState))
        return false;

    const uintptr_t beginCall = FindRelativeCall(
        kBeforeMainRender, kBeforeMainRenderScanSize, kRsCameraBeginUpdate);
    const uintptr_t endCall = FindRelativeCall(
        kBeforeMainRender, kBeforeMainRenderScanSize, kRwCameraEndUpdate);
    const uintptr_t definedStateCall = FindRelativeCall(
        kBeforeMainRender, kBeforeMainRenderScanSize, kDefinedState);
    if (!beginCall || !endCall || !definedStateCall)
        return false;

    // The end hook is harmless without the begin hook, so install it first.
    if (!RedirectRelativeCall(endCall,
                              reinterpret_cast<uintptr_t>(&RwCameraEndUpdateHook)) ||
        !RedirectRelativeCall(definedStateCall,
                              reinterpret_cast<uintptr_t>(&DefinedStateForMirrorHook)) ||
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
