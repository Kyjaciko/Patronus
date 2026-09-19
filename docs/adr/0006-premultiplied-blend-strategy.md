# ADR 0006: One premultiplied-alpha blend state for all particle layers

Status: Proposed
Date: 2026-09-17

## Context

The spell needs additive layers (core, sparks, shell), alpha-blended layers
(smoke puffs, wisps) and layers that fade from one to the other over a
particle's life. The obvious implementation is one pipeline state object
per blend mode and one draw per mode. That multiplies PSOs, forces draws
to be grouped by blend mode rather than by emitter, and cannot express a
per-particle mix.

Premultiplied alpha (`SrcBlend = ONE`, `DestBlend = INV_SRC_ALPHA`,
`BlendOp = ADD`) covers both: with output `(rgb * a, a)` it is standard
alpha blending; with output `(rgb, 0)` it is pure additive; anything in
between is a continuous mix. The blend mode becomes a shader parameter.

## Decision

All particle draws use one graphics PSO with premultiplied-alpha blending,
depth test on, depth write off. The pixel shader computes an "additive
factor" per emitter (optionally per particle over life) and outputs
`float4(rgb * lerp(a, 1, additive), a * (1 - additive))`.

## Consequences

- One PSO and one `ExecuteIndirect` cover every emitter, which is what
  ADR-0005 assumes.
- Draw order is by emitter, which is also the order that makes sense
  visually (additive core layers last, over alpha layers). Alpha-blended
  layers still need back-to-front order among themselves for strict
  correctness; a per-emitter sort is an opt-in for the smoke layer and is
  skipped for additive layers, where order does not matter.
- Colour curves in the curve atlas are authored as straight (not
  premultiplied) radiance plus alpha; the shader premultiplies. Authoring
  stays intuitive.
- The HDR target (ADR-0003) does not store destination alpha. Nothing in
  this scheme reads it.
