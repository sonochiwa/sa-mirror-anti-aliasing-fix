# Mirror Anti-Aliasing Fix

`MirrorAntiAliasingFix.asi` is a standalone GTA San Andreas plugin that
supersamples real-time mirror reflections.

The renderer draws planar mirrors through `CMirrors::BeforeMainRender` into a
separate single-sample camera texture. Driver anti-aliasing profiles applied
to the back buffer do not reach that off-screen target, so geometry and
textures in a reflection stay jagged while the same objects in the main scene
are smooth.

The plugin redirects only the reflected-scene pass into a plain render target
several times the size of the mirror texture, together with a matching depth
surface, and reduces the result back into the game's own mirror texture. The
mirror dimensions, the camera projection and the composition pass that
follows are untouched. Supersampling is used rather than multisampling because
an off-screen RenderWare camera pass into a multisampled target loses the
depth comparison at every sample count, while the same pass into a
single-sample target reproduces the original image exactly; it also smooths
texture detail inside the reflection, which multisampling would not.

Only planar mirrors in interiors and the 8-Track screens are affected. Vehicle
environment maps, water and wet-road reflections are out of scope.

## Features

- `2x`, `4x` or `8x` supersampling of the reflected scene.
- Reduction back into the mirror texture through a chain of exact 2:1 steps,
  which makes the bilinear filter in `StretchRect` behave as a box filter.
- Automatic fallback to a lower factor when a surface cannot be allocated.
- A matching depth/stencil surface at the rendering resolution.
- Verifies the executable and the two call sites before writing and refuses
  to patch any other executable.
- Creates the default INI when it is missing.

## Requirements

- GTA San Andreas 1.0 US (Compact or Hoodlum executable).
- An ASI loader, such as Silent's ASI Loader or Ultimate ASI Loader.
- A Direct3D 9 device able to allocate a render target and depth surface of
  `supersample` times the mirror's dimensions in the game's active colour and
  depth formats.

Other executables are unsupported: the hook and RenderWare bindings are
address-specific, and the plugin does nothing when the image base or the call
sites do not match.

## Installation

1. Extract `MirrorAntiAliasingFix.asi` and `MirrorAntiAliasingFix.ini` into
   the GTA San Andreas directory or its `scripts` directory.
2. Start the game.

Remove any older copy of `MirrorReflectionFix.asi` and its INI first.

## Configuration

```ini
# Mirror Anti-Aliasing Fix v1.1.1
# Created by sonochiwa
# Source code: https://github.com/sonochiwa/sa-mirror-anti-aliasing-fix

[antiAliasing]
supersample=4
```

| Setting | Default | Meaning |
| --- | ---: | --- |
| `[antiAliasing]` | | |
| `supersample` | `4` | Rendering resolution multiplier for the reflected scene, normalised to `1`, `2`, `4` or `8`. `1` disables the anti-aliasing while keeping the rest of the path. |

Settings are read once when the plugin loads. A mirror pass draws the whole
reflected scene, so each step of `supersample` costs four times the pixels of
the previous one.

## Building

Visual Studio 2022 (v143), `Release|Win32`. Open `MirrorAntiAliasingFix.sln`
or run:

```powershell
msbuild MirrorAntiAliasingFix.sln /t:Rebuild /p:Configuration=Release /p:Platform=Win32
```

The plugin is written to `build\MirrorAntiAliasingFix.asi` next to a copy of
the INI.

## Repository Layout

```text
MirrorAntiAliasingFix.sln
README.md
CHANGELOG.md
LICENSE
.github\workflows\release.yml   Tagged release build, checksum and attestation
Config\
  MirrorAntiAliasingFix.ini     Canonical configuration, embedded as RCDATA
src\
  MirrorAntiAliasingFix.cpp     DllMain and the initialization thread
  MirrorAntiAliasingFix.rc      Version resource and the embedded INI
  MirrorAntiAliasingFix.vcxproj
  addresses.h                   Game addresses and function signatures
  config.cpp / config.h         INI creation and loading
  patch.cpp / patch.h           Safe reads, protected writes, CALL rewriting
  supersample.cpp / supersample.h   The mirror pass hooks and surfaces
  resource.h
  version.h
```

## How It Works

The plugin checks the image base and that the four code addresses it depends
on are executable, then finds the `RsCameraBeginUpdate` and
`RwCameraEndUpdate` calls inside `CMirrors::BeforeMainRender` and rewrites
their displacements to its own hooks.

After RenderWare binds its mirror texture, the begin hook captures the bound
render target and depth surface and substitutes a plain pair of the same
formats at `supersample` times their dimensions. `SetRenderTarget` resets the
viewport to the whole surface, so the reflected scene is rasterised on a
denser grid; the camera and its projection are not touched.

The end hook restores the original surfaces and reduces the rendered image
into the mirror texture with `IDirect3DDevice9::StretchRect` through a chain
of exact 2:1 calls. `StretchRect` filters bilinearly and reads only a 2x2
neighbourhood, so a single `4x` or `8x` reduction would discard most of the
rendered image; at 2:1 the bilinear tap lands in the centre of each 2x2 block
and averages all four texels.

## Release Integrity

Tagged releases are built by GitHub Actions from the tagged commit. Each
release carries `MirrorAntiAliasingFix-vX.Y.Z.zip`, its SHA-256 in
`MirrorAntiAliasingFix-vX.Y.Z.zip.sha256` and a signed build-provenance
attestation, which proves that the archive was produced by this repository's
workflow from that revision. It does not prove the code is bug-free.

```text
gh attestation verify MirrorAntiAliasingFix-vX.Y.Z.zip -R sonochiwa/sa-mirror-anti-aliasing-fix
```

## License

MIT. See [LICENSE](LICENSE).
