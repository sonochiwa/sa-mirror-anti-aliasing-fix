#include "supersample.h"

#include "addresses.h"
#include "patch.h"

#include <windows.h>
#include <d3d9.h>

namespace {

constexpr int kMaxDownsampleStages = 3;

IDirect3DDevice9* g_surfaceDevice = nullptr;
IDirect3DSurface9* g_largeColor = nullptr;
IDirect3DSurface9* g_largeDepth = nullptr;
IDirect3DSurface9* g_downsampleChain[kMaxDownsampleStages] = {};
int g_downsampleCount = 0;
IDirect3DSurface9* g_mirrorColor = nullptr;
IDirect3DSurface9* g_mirrorDepth = nullptr;
D3DSURFACE_DESC g_colorDesc = {};
D3DFORMAT g_depthFormat = D3DFMT_UNKNOWN;
int g_requestedFactor = 4;
int g_activeFactor = 0;
bool g_passActive = false;

IDirect3DDevice9* GetDevice() {
    return static_cast<IDirect3DDevice9*>(reinterpret_cast<GetD3DDeviceFn>(kRwD3D9GetCurrentDevice)());
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

void ReleaseLargeTargets() {
    ReleaseDownsampleChain();
    if (g_largeDepth) {
        g_largeDepth->Release();
        g_largeDepth = nullptr;
    }
    if (g_largeColor) {
        g_largeColor->Release();
        g_largeColor = nullptr;
    }
    g_surfaceDevice = nullptr;
    g_colorDesc = {};
    g_depthFormat = D3DFMT_UNKNOWN;
    g_activeFactor = 0;
}

void ReleaseMirrorSurfaces() {
    if (g_mirrorDepth) {
        g_mirrorDepth->Release();
        g_mirrorDepth = nullptr;
    }
    if (g_mirrorColor) {
        g_mirrorColor->Release();
        g_mirrorColor = nullptr;
    }
    g_passActive = false;
}

bool HasStencil(D3DFORMAT format) {
    return format == D3DFMT_D15S1 || format == D3DFMT_D24S8 || format == D3DFMT_D24X4S4;
}

// Plain single-sample surfaces at a multiple of the mirror size. Rendering an
// off-screen camera pass into a multisampled target loses the depth
// comparison at every sample count, while the same pass into a single-sample
// target reproduces the original image exactly, so supersampling does the
// anti-aliasing and also smooths texture detail inside the reflection.
bool CreateLargeTargets(IDirect3DDevice9* device, const D3DSURFACE_DESC& color,
                        const D3DSURFACE_DESC& depth) {
    ReleaseLargeTargets();

    int factor = g_requestedFactor;
    while (factor >= 1) {
        const UINT width = color.Width * static_cast<UINT>(factor);
        const UINT height = color.Height * static_cast<UINT>(factor);
        IDirect3DSurface9* colorSurface = nullptr;
        IDirect3DSurface9* depthSurface = nullptr;

        HRESULT colorResult = device->CreateRenderTarget(width, height, color.Format, D3DMULTISAMPLE_NONE,
                                                         0, FALSE, &colorSurface, nullptr);
        HRESULT depthResult = E_FAIL;
        if (SUCCEEDED(colorResult)) {
            depthResult = device->CreateDepthStencilSurface(width, height, depth.Format, D3DMULTISAMPLE_NONE,
                                                            0, FALSE, &depthSurface, nullptr);
        }

        if (SUCCEEDED(colorResult) && SUCCEEDED(depthResult)) {
            g_surfaceDevice = device;
            g_largeColor = colorSurface;
            g_largeDepth = depthSurface;
            g_colorDesc = color;
            g_depthFormat = depth.Format;
            g_activeFactor = factor;

            // StretchRect filters bilinearly and reads only a 2x2 neighbourhood,
            // so a single 4x or 8x reduction would discard most of the image.
            // Halving repeatedly keeps every step at 2:1, where the bilinear tap
            // averages all four texels of each block.
            for (int step = factor / 2; step >= 2; step /= 2) {
                IDirect3DSurface9* stage = nullptr;
                if (FAILED(device->CreateRenderTarget(color.Width * static_cast<UINT>(step),
                                                      color.Height * static_cast<UINT>(step), color.Format,
                                                      D3DMULTISAMPLE_NONE, 0, FALSE, &stage, nullptr))) {
                    ReleaseDownsampleChain();
                    break;
                }
                g_downsampleChain[g_downsampleCount++] = stage;
            }
            return true;
        }

        if (depthSurface) {
            depthSurface->Release();
        }
        if (colorSurface) {
            colorSurface->Release();
        }
        factor /= 2;
    }

    return false;
}

bool EnsureLargeTargets(IDirect3DDevice9* device, const D3DSURFACE_DESC& color,
                        const D3DSURFACE_DESC& depth) {
    if (g_largeColor && g_largeDepth && g_surfaceDevice == device && g_activeFactor == g_requestedFactor &&
        g_colorDesc.Width == color.Width && g_colorDesc.Height == color.Height &&
        g_colorDesc.Format == color.Format && g_depthFormat == depth.Format) {
        return true;
    }
    return CreateLargeTargets(device, color, depth);
}

bool ActivateLargeTargets(IDirect3DDevice9* device) {
    HRESULT result = device->SetDepthStencilSurface(nullptr);
    if (SUCCEEDED(result)) {
        result = device->SetRenderTarget(0, g_largeColor);
    }
    if (SUCCEEDED(result)) {
        result = device->SetDepthStencilSurface(g_largeDepth);
    }
    if (FAILED(result)) {
        device->SetDepthStencilSurface(nullptr);
        device->SetRenderTarget(0, g_mirrorColor);
        device->SetDepthStencilSurface(g_mirrorDepth);
        ReleaseLargeTargets();
        return false;
    }

    DWORD clearFlags = D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER;
    if (HasStencil(g_depthFormat)) {
        clearFlags |= D3DCLEAR_STENCIL;
    }
    result = device->Clear(0, nullptr, clearFlags, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0);
    return SUCCEEDED(result);
}

int __cdecl RsCameraBeginUpdateHook(void* camera) {
    const int result = reinterpret_cast<RsCameraBeginUpdateFn>(kRsCameraBeginUpdate)(camera);
    if (!result) {
        return result;
    }

    IDirect3DDevice9* device = GetDevice();
    if (!device) {
        return result;
    }

    ReleaseMirrorSurfaces();
    if (FAILED(device->GetRenderTarget(0, &g_mirrorColor)) || !g_mirrorColor ||
        FAILED(device->GetDepthStencilSurface(&g_mirrorDepth)) || !g_mirrorDepth) {
        ReleaseMirrorSurfaces();
        return result;
    }

    D3DSURFACE_DESC color = {};
    D3DSURFACE_DESC depth = {};
    if (FAILED(g_mirrorColor->GetDesc(&color)) || FAILED(g_mirrorDepth->GetDesc(&depth))) {
        ReleaseMirrorSurfaces();
        return result;
    }

    if (!EnsureLargeTargets(device, color, depth) || !ActivateLargeTargets(device)) {
        ReleaseMirrorSurfaces();
        return result;
    }

    g_passActive = true;
    return result;
}

void* __cdecl RwCameraEndUpdateHook(void* camera) {
    void* result = reinterpret_cast<RwCameraEndUpdateFn>(kRwCameraEndUpdate)(camera);
    if (!g_passActive) {
        return result;
    }

    IDirect3DDevice9* device = GetDevice();
    if (device && g_mirrorColor && g_mirrorDepth && g_largeColor) {
        device->SetDepthStencilSurface(nullptr);
        const HRESULT targetResult = device->SetRenderTarget(0, g_mirrorColor);
        const HRESULT depthResult = device->SetDepthStencilSurface(g_mirrorDepth);
        if (SUCCEEDED(targetResult) && SUCCEEDED(depthResult)) {
            const D3DTEXTUREFILTERTYPE filter = g_activeFactor > 1 ? D3DTEXF_LINEAR : D3DTEXF_NONE;
            IDirect3DSurface9* stageSource = g_largeColor;
            HRESULT reduction = S_OK;
            for (int i = 0; i < g_downsampleCount && SUCCEEDED(reduction); ++i) {
                reduction = device->StretchRect(stageSource, nullptr, g_downsampleChain[i], nullptr,
                                                D3DTEXF_LINEAR);
                stageSource = g_downsampleChain[i];
            }
            if (SUCCEEDED(reduction)) {
                device->StretchRect(stageSource, nullptr, g_mirrorColor, nullptr, filter);
            }
        }
    }

    ReleaseMirrorSurfaces();
    return result;
}

} // namespace

bool InstallSupersampling(int factor) {
    g_requestedFactor = factor;

    if (reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) != kImageBase ||
        !IsExecutableAddress(kBeforeMainRender) || !IsExecutableAddress(kRsCameraBeginUpdate) ||
        !IsExecutableAddress(kRwCameraEndUpdate) || !IsExecutableAddress(kRwD3D9GetCurrentDevice)) {
        return false;
    }

    const uintptr_t beginCall = FindRelativeCall(kBeforeMainRender, kBeforeMainRenderScanSize, kRsCameraBeginUpdate);
    const uintptr_t endCall = FindRelativeCall(kBeforeMainRender, kBeforeMainRenderScanSize, kRwCameraEndUpdate);
    if (!beginCall || !endCall) {
        return false;
    }

    // The end hook does nothing until a begin hook has activated a pass, so
    // it goes in first: a failure between the two leaves the game as it was.
    return RedirectRelativeCall(endCall, reinterpret_cast<uintptr_t>(&RwCameraEndUpdateHook)) &&
           RedirectRelativeCall(beginCall, reinterpret_cast<uintptr_t>(&RsCameraBeginUpdateHook));
}
