# Changelog

## 1.1.2

- Added `README.txt` to the release archive.

## 1.1.1

- Added creation of the INI when it is missing.
- Added version information to the plugin file.
- Removed `README.txt` from the release archive.

## 1.1.0

- Changed the anti-aliasing from multisampling to supersampling, which also
  smooths texture detail inside the reflection.
- Replaced `sampleCount` with `supersample`, `4` by default.

## 1.0.0

- `2x`, `4x`, `8x` or `16x` MSAA for reflected scenes, with fallback when
  the driver rejects a mode.
- Mirror dimensions, camera projection and composition are unchanged.
