# Mirror Anti-Aliasing Fix

A GTA San Andreas ASI plugin that multisamples real-time mirror reflections.

The original PC renderer draws planar mirrors into a separate single-sample
camera texture. Driver anti-aliasing profiles applied to the main backbuffer do
not automatically affect this off-screen target, so geometry and textures in a
reflection can remain jagged while the same objects in the main scene are
smooth.

Mirror Anti-Aliasing Fix redirects only the reflected-scene pass into matching
multisampled color and depth surfaces. After the scene is rendered, Direct3D 9
resolves the multisampled color surface into the game's original mirror
texture. Mirror dimensions, camera projection and the later composition pass
remain unchanged.

The plugin changes only planar mirrors used in interiors and the 8-Track
screens. Vehicle environment maps, water and wet-road reflections are out of
scope.

## Features

- True `2x`, `4x`, `8x` or `16x` MSAA for the reflected scene.
- Automatic fallback to a lower sample count when allocation fails.
- Hardware resolve into the original RenderWare camera texture.
- Matching multisampled depth/stencil surface.
- No resizing or post-processing of the mirror texture.

## Requirements

- Grand Theft Auto: San Andreas PC, Hoodlum/US 1.0 executable.
- An ASI loader.
- A Direct3D 9 graphics device supporting at least 2x multisampled off-screen
  render targets in the game's active color and depth formats.

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
# Mirror Anti-Aliasing Fix v1.0.0
# Created by sonochiwa
# Source code: https://github.com/sonochiwa/sa-mirror-anti-aliasing-fix

[antiAliasing]
sampleCount=8
```

| Setting | Default | Meaning |
| --- | ---: | --- |
| `sampleCount` | `8` | Requested mirror MSAA mode. Values are normalized to `2`, `4`, `8` or `16`; unsupported modes fall back by halves. |

Settings are read once when the plugin loads. NVIDIA Inspector may further
override the requested multisample mode according to the active driver profile.

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
its normal mirror texture, the begin hook captures it and substitutes a
multisampled render-target/depth pair of identical dimensions and formats. The
end hook restores the original surfaces and resolves the multisampled color
surface into the mirror texture with `IDirect3DDevice9::StretchRect`.

## Release Integrity

Tagged archives are built by GitHub Actions from the tagged source revision.
Each release includes a SHA-256 checksum and a signed GitHub build-provenance
attestation. Verify an archive with:

```powershell
gh attestation verify MirrorAntiAliasingFix-v1.0.0.zip -R sonochiwa/sa-mirror-anti-aliasing-fix
```

This verifies archive provenance and integrity; it is not a guarantee that the
software is bug-free or safe for every mod configuration.

## License

[MIT](LICENSE)
