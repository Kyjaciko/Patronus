# 2026-09-17/18: Architecture review, tooling, and a CPU spell previs

*Two days of planning and tooling with Claude before the next graphics
milestone. No renderer or HLSL code changed; everything here is build
plumbing, offline bakers, tests, docs, and a Python previsualisation of
the target effect. Same disclosure as earlier entries.*

## The review

A full review of the particle system against the goal (a layered,
Hogwarts-Legacy-quality spell) is condensed into `docs/roadmap.md` and
ADR-0003 to ADR-0007. The conclusion that reordered the plan: the gap
between the current orb and an AAA spell is mostly the rendering pipeline
around the particles (HDR target, premultiplied blending, bloom, depth and
soft particles, curves and noise erosion) and layered authoring, not the
GPU-driven plumbing. So the milestones build the look first (M1), the
lists and indirect execution second (M2), and measure both.

Defects found in the current code are listed with line references in the
roadmap's M0 checklist: a CPU/GPU race on the single camera constant
buffer, unclamped delta time, upload buffers never released, a possible
overrun in the sphere placement, a curl-field seam under the WRAP sampler,
and vertex/pixel shaders compiled as SM 5.1 instead of 6.6.

## Tooling that landed

- `cmake/CompileShaders.cmake`: one `patronus_add_shader()` rule, SM 6.6
  for every stage, embedded debug info in Debug builds. Shaders moved to
  `shaders/`.
- `cmake/DeployAssets.cmake`: `assets/` is copied next to the executable so
  nothing is loaded relative to the working directory.
- `tools/bake_curl_noise.py`: tileable curl volume (periodic lattice noise,
  wrapping curl stencil, divergence verified to float precision), scalar
  noise in alpha, metadata JSON and a constexpr header.
- `tools/bake_noise2d.py`: tileable RGBA8 noise pack for erosion,
  distortion, cells and density.
- `tools/bake_curves.py`: per-emitter size/alpha/colour curves from JSON to
  one RGBA16F atlas row per curve.
- `tools/bench/`: CSV schema for timestamp results, percentiles, A/B
  tables, plots.
- 54 unit tests under `tools/tests`, run in CI on a Linux runner.

## The previs, and what it caught

`tools/previs_spell.py` runs the emitter data (`assets/emitters/*.json`)
through a CPU twin of the planned GPU pipeline: pools with free-slot
reuse, the kickoff spawn accumulator, the same force order as the compute
shader, the curve atlas sampled by age, the curl volume sampled with WRAP,
and the premultiplied compositing from ADR-0006, splatted into an HDR
buffer with a fake bloom and an ACES tonemap. It renders to a window, PNG
frames or a GIF, and prints per-emitter max-alive and dropped-spawn
counts, which are the GPU pool sizes.

Two spells exist: `arcane_bolt` (the orb projectile from the roadmap) and
`force_vortex` (a horizontal violet tornado in the Force-spell language of
the reference game: three helical arms, a wavy full ring with two orbiting
quarter-arcs, dust dragged in, wind streaks, then a flash, two fast
line-drawn impact rings, debris and a plume).

Three things it caught before any HLSL existed:

1. **The first spec saturated to white.** A 600/s trail at intensity 4 and
   a core at intensity 12 stack past any tonemapper. An additive HDR
   renderer would do exactly the same; the fix was in the data.
2. **Spin as an acceleration spirals outward.** A particle orbiting at
   10 u/s at radius 1 needs 100 u/s^2 of centripetal acceleration, far
   more than any sane band pull. The vortex force now relaxes toward a
   target tangential speed and applies v^2/r explicitly. The simulate
   kernel must do the same or the tornado is several times too wide.
3. **Rings are geometry, not particles.** A rotating, expanding, noise-
   deformed ring that lives for the whole spell is a ring mesh or ribbon
   driven by a vertex shader. The previs has a kinematic `ring` kind for
   it; the renderer will need that primitive alongside the particle pools.

One previs bug was found the same way: summing overlapping alpha
particles instead of converging like over-blending turned smoke into a
white blob. Fixed by normalising a layer's alpha share by coverage.

## Documentation

ADR-0008 (fail-fast error handling) and ADR-0009 (feature level 12_2 and
debug-layer policy) record decisions that had lived only in code. ADR-0002
lost its stale "not yet handled" list. The 2026-08-07 entry finally says
what the crash was (Agility SDK export 618 vs deployed 619), and the
2026-09-12 entry's claim about structured-buffer packing was corrected.
