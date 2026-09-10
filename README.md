# Mirror Anti-Aliasing Fix

A GTA San Andreas ASI plugin that supersamples real-time mirror reflections.

The original PC renderer draws planar mirrors into a separate single-sample
camera texture. Driver anti-aliasing profiles applied to the main backbuffer do
not automatically affect this off-screen target, so geometry and textures in a
reflection can remain jagged while the same objects in the main scene are
smooth.

Mirror Anti-Aliasing Fix redirects only the reflected-scene pass into a plain
render target several times the size of the mirror texture, together with a
matching depth surface. After the scene is rendered the image is reduced back
into the game's original mirror texture. Mirror dimensions, camera projection
and the later composition pass remain unchanged.

The anti-aliasing is done by supersampling rather than multisampling. That is a
measured decision: in the sister project for SA-MP preview textdraws, rendering
an off-screen RenderWare camera pass into a multisampled target lost the depth
comparison at every sample count, while the same swap into a single sample
target reproduced the original image exactly. Supersampling also smooths texture
detail inside the reflection, which multisampling would not have done.

The plugin changes only planar mirrors used in interiors and the 8-Track
screens. Vehicle environment maps, water and wet-road reflections are out of
scope.

## Features

- `2x`, `4x` or `8x` supersampling of the reflected scene.
- Reduction back into the mirror texture through a chain of exact 2:1 steps,
  which is what makes the bilinear filter in `StretchRect` behave as a box
  filter.
- Automatic fallback to a lower factor when a surface cannot be allocated.
- Matching depth/stencil surface at the rendering resolution.
- No resizing or post-processing of the mirror texture itself.
- Optional diagnostic log and mirror dumps.

## Requirements

- Grand Theft Auto: San Andreas PC, Hoodlum/US 1.0 executable.
- An ASI loader.
- A Direct3D 9 graphics device able to allocate a render target and depth
  surface of `supersample` times the mirror's dimensions in the game's active
  color and depth formats.

Other game executables are not supported because the hook and RenderWare
bindings are address-specific. SilentPatch remains recommended for its other
game fixes. Runtime coexistence still needs validation with the user's complete
mod and driver-profile setup.

## Installation

1. Install an ASI loader in the GTA San Andreas directory.
2. Remove every older copy of `MirrorReflectionFix.asi`,
   `MirrorAntiAliasingFix.asi` and their INI files.
3. Copy `MirrorAntiAliasingFix.asi` and `MirrorAntiAliasingFix.ini` next to
   `gta_sa.exe`.
4. Fully restart the game and visit an interior containing a real-time mirror.

## Configuration

The default `MirrorAntiAliasingFix.ini` is:

```ini
# Mirror Anti-Aliasing Fix v1.1.0
# Created by sonochiwa
# Source code: https://github.com/sonochiwa/sa-mirror-anti-aliasing-fix

[general]
isEnabled=1
logging=0
dumpPreviews=0

[antiAliasing]
supersample=2
```

| Setting | Default | Meaning |
| --- | ---: | --- |
| `isEnabled` | `1` | Master switch. `0` leaves the game's own mirror rendering untouched. |
| `logging` | `0` | Writes `MirrorAntiAliasingFix.log` next to the plugin. The file is recreated on every start. |
| `dumpPreviews` | `0` | Diagnostic mode. Writes the first mirror of the session to a 32-bit TGA next to the plugin, from the plugin's own path or the game's own, depending on `isEnabled`. |
| `supersample` | `2` | Rendering resolution multiplier for the reflected scene. Values are normalized to `1`, `2`, `4` or `8`; `1` disables anti-aliasing while keeping the rest of the path. |

Settings are read once when the plugin loads.

A mirror pass draws the whole reflected scene, so each step of `supersample`
costs four times the pixels of the previous one. `2` is the default for that
reason; `4` and above are worth measuring against the frame rate before keeping
them.

## Building

Use Visual Studio 2022 with the v143 C++ toolset. Build
`MirrorAntiAliasingFix.sln` as `Release|Win32`:

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" `
  MirrorAntiAliasingFix.sln /t:Rebuild /p:Configuration=Release /p:Platform=Win32 /m
```

The plugin and canonical INI are written to `build\`. Release builds use the
static C/C++ runtime and require no vendored SDK or runtime shader compiler.

## Repository Layout

```text
MirrorAntiAliasingFix.sln
Config/
  MirrorAntiAliasingFix.ini
src/
  MirrorAntiAliasingFix.cpp
  MirrorAntiAliasingFix.vcxproj
.github/workflows/
  release.yml
packaging/
  README.txt
```

## How It Works

The plugin validates the fixed US 1.0 code locations and intercepts the begin
and end update calls inside `CMirrors::BeforeMainRender`. After RenderWare binds
its normal mirror texture, the begin hook captures it and substitutes a plain
render-target/depth pair of the same formats at `supersample` times its
dimensions. `SetRenderTarget` resets the viewport to the whole surface, so the
reflected scene is simply rasterized on a denser grid; the camera and its
projection are not touched.

The end hook restores the original surfaces and reduces the rendered image into
the mirror texture with `IDirect3DDevice9::StretchRect`. The reduction is a chain
of calls, each exactly 2:1. This matters: `StretchRect` filters bilinearly and
therefore reads only a 2x2 neighbourhood, so a single `4x` or `8x` reduction
would discard most of the rendered image rather than average it. At 2:1 the
bilinear tap lands in the centre of each 2x2 block and averages all four texels.

## Release Integrity

Tagged archives are built by GitHub Actions from the tagged source revision.
Each release includes a SHA-256 checksum and a signed GitHub build-provenance
attestation. Verify an archive with:

```powershell
gh attestation verify MirrorAntiAliasingFix-v1.1.0.zip -R sonochiwa/sa-mirror-anti-aliasing-fix
```

This verifies archive provenance and integrity; it is not a guarantee that the
software is bug-free or safe for every mod configuration.

## License

[MIT](LICENSE)
