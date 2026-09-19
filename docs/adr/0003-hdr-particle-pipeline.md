# ADR 0003: HDR intermediate target for the particle pipeline

Status: Proposed
Date: 2026-09-17

## Context

Particles currently render straight into the `R8G8B8A8_UNORM` swapchain
with blending disabled. The spell effect needs additive and alpha-blended
layers with emissive intensities well above 1.0 (a core at 10 to 20x, sparks
at 10x fading to 1x) and a bloom pass fed by those values. Additive blending
into an 8-bit target clips to white after a handful of overlapping quads;
bloom on an LDR image has nothing to bloom. Every reference the project is
measured against (Hogwarts Legacy and similar) composites VFX in a
floating-point target and tonemaps afterwards.

Alternatives considered:

- Keep LDR and fake intensity with colour ramps. Cheap, but the look caps
  early and bloom stays flat. Rejected.
- `R16G16B16A16_FLOAT` intermediate. Keeps alpha for later compositing;
  8 bytes per pixel.
- `R11G11B10_FLOAT` intermediate. 4 bytes per pixel, no alpha, no sign.
  Blending only needs *source* alpha (from the pixel shader), so the missing
  destination alpha does not matter for premultiplied compositing.

## Decision

Render opaque geometry and all particle layers into an `R11G11B10_FLOAT`
colour target with a `D32_FLOAT` depth buffer (ADR-0004), run bloom on that
target, and tonemap into the swapchain through an `R8G8B8A8_UNORM_SRGB`
render target view (the flip-model swapchain buffer stays `UNORM`; only
the RTV is sRGB). Switch to `R16G16B16A16_FLOAT` only if a later pass needs
destination alpha.

## Consequences

- Bandwidth: one extra full-resolution 4-byte RT read and write per frame
  for the tonemap pass, plus the bloom chain (about 1.4x the base
  resolution in pixels across all mip levels). Trivial next to the
  read-modify-write traffic of additive particles themselves.
- Every particle shader now outputs linear HDR radiance. Colour curves in
  the curve atlas (`tools/bake_curves.py`) are authored in that space, so
  values above 1.0 are normal and intended.
- The tonemap operator becomes a visible artistic decision (ACES fitted or
  a simple filmic curve). It needs an ImGui toggle so the difference can be
  shown, not just described.
- Debug UI (ImGui) renders after tonemap, into the swapchain, so it is not
  tonemapped.
