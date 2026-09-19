# Mirror Anti-Aliasing Fix

`MirrorAntiAliasingFix.asi` is a GTA San Andreas plugin that supersamples
real-time mirror reflections.

The game draws mirrors into a separate texture that driver anti-aliasing
never reaches, so a reflection stays jagged while the room around it is
smooth. The plugin renders the reflected scene at a multiple of the mirror's
resolution and scales it back down. Only planar mirrors in interiors and the
8-Track screens are affected; vehicle, water and wet-road reflections are
out of scope.

## Features

- `2x`, `4x` or `8x` supersampling of the reflected scene.
- Falls back to a lower factor when video memory runs out.
- Verifies the bytes it replaces before writing and refuses to patch any
  other executable.
- Creates the default INI when it is missing.

## Requirements

- GTA San Andreas 1.0 US (Compact or Hoodlum executable).
- An ASI loader, such as Silent's ASI Loader or Ultimate ASI Loader.
- A Direct3D 9 device with enough video memory for a render target
  `supersample` times the mirror's dimensions.

Other executables are left untouched.

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

Settings are read once when the game starts. Each step of `supersample`
costs four times the pixels of the previous one.

## Release Integrity

Releases are built by GitHub Actions from the tagged commit and carry a
SHA-256 file and a build-provenance attestation:

```text
gh attestation verify MirrorAntiAliasingFix-vX.Y.Z.zip -R sonochiwa/sa-mirror-anti-aliasing-fix
```

## License

MIT. See [LICENSE](LICENSE).
