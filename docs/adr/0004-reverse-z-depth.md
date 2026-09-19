# ADR 0004: Reverse-Z depth buffer, read as SRV for soft particles

Status: Proposed
Date: 2026-09-17

## Context

There is no depth buffer yet. Soft particles need scene depth in the
particle pixel shader, and the opaque scene (ground plane, occluders) needs
depth testing. Two decisions are bundled here because they are made once
and are expensive to revisit: the depth convention, and how the same depth
resource is bound as both depth-stencil view and shader resource view in
one pass.

Depth precision with a standard `[0, 1]` mapping is concentrated near the
near plane because the floating-point mantissa is densest near zero while
the projection packs most of the scene near 1.0. Reversing the mapping
(near = 1, far = 0) aligns the two distributions and gives near-uniform
precision across the range with `D32_FLOAT`. This is standard practice in
current engines and a common interview question.

For the read-only depth binding, D3D12 allows a resource to be in several
read states at once. A DSV created with `D3D12_DSV_FLAG_READ_ONLY_DEPTH`
may be bound while an SRV of the same resource is read by the pixel
shader, provided the resource is in `DEPTH_READ | PIXEL_SHADER_RESOURCE`.
The alternative is no DSV at all during the particle pass and a manual
depth test (`clip`) in the pixel shader against the SRV.

## Decision

- Depth resource: `R32_TYPELESS`, `ALLOW_DEPTH_STENCIL`; DSV format
  `D32_FLOAT`; SRV format `R32_FLOAT`.
- Reverse-Z: projection built with near and far swapped (or infinite far),
  depth cleared to 0.0, opaque comparison `GREATER`, particle comparison
  `GREATER` with `DepthWriteMask = ZERO`.
- Particle pass binds the read-only DSV and the SRV together; the resource
  transitions `DEPTH_WRITE -> DEPTH_READ | PIXEL_SHADER_RESOURCE` after the
  opaque pass and back before the next frame's opaque pass.
- Soft fade in the pixel shader: `saturate((sceneLinear - particleLinear)
  * invFadeDistance)`, where `particleLinear` is `SV_Position.w` (clip-space
  w equals positive view depth for a perspective projection) and
  `sceneLinear` is the linearised depth-buffer value. Linearisation
  constants come from the frame constant buffer, derived from the
  projection matrix; the exact formula is recorded in the shader header
  once the projection is final.

## Consequences

- Everything that compares depth must use `GREATER`; forgetting this in a
  new PSO produces an empty image, which is at least obvious.
- `D32_FLOAT` leaves no stencil. Nothing planned needs one.
- Depth is loaded, not sampled, in the particle shader (`Load` at
  `SV_Position.xy`); filtering across depth discontinuities would produce
  halos.
- The read-only DSV path saves fill compared with the `clip` path because
  early-Z still rejects fully hidden particle pixels. The `clip` path
  remains the fallback for half-resolution particle rendering, where the
  depth buffer resolution does not match the target.
