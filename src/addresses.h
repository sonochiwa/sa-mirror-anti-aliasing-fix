#pragma once

#include <cstddef>
#include <cstdint>

// GTA San Andreas 1.0 US, default image base.
constexpr uintptr_t kImageBase = 0x00400000;

// CMirrors::BeforeMainRender. Renders the mirror camera; the two RenderWare
// calls below are found inside its first 0x180 bytes and redirected.
constexpr uintptr_t kBeforeMainRender = 0x00727140;
constexpr size_t kBeforeMainRenderScanSize = 0x180;

// RsCameraBeginUpdate: makes the camera's raster the render target.
constexpr uintptr_t kRsCameraBeginUpdate = 0x00619450;
// RwCameraEndUpdate: finishes the camera pass.
constexpr uintptr_t kRwCameraEndUpdate = 0x007EE180;
// RwD3D9GetCurrentD3DDevice.
constexpr uintptr_t kRwD3D9GetCurrentDevice = 0x007F9D50;

using RsCameraBeginUpdateFn = int(__cdecl*)(void*);
using RwCameraEndUpdateFn = void*(__cdecl*)(void*);
using GetD3DDeviceFn = void*(__cdecl*)();
