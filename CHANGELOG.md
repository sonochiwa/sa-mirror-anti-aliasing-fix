# Changelog

## 1.1.0

- Changed the anti-aliasing from multisampling to supersampling. The reflected
  scene is now rendered into a plain render target several times the size of the
  mirror texture and reduced back into it. Rendering an off-screen RenderWare
  camera pass into a multisampled target was measured, in the sister project for
  SA-MP preview textdraws, to lose the depth comparison at every sample count,
  while the same swap into a single sample target reproduced the original image
  exactly.
- Added reduction through a chain of exact 2:1 steps, so the bilinear filter in
  `StretchRect` acts as a box filter instead of discarding most of the rendered
  image.
- Replaced `sampleCount` with `supersample`, defaulting to `4`. A mirror pass
  draws the whole reflected scene, so each step costs four times the pixels.
- Removed the `DefinedState` hook, which existed only to re-assert the
  multisample render state.

## 1.0.0

- Added true configurable `2x`, `4x`, `8x` or `16x` MSAA for reflected scenes.
- Added automatic fallback when the graphics driver rejects a requested mode.
- Added an explicit hardware resolve into the game's original mirror texture.
- Preserved the original mirror dimensions, camera projection and composition
  path.
