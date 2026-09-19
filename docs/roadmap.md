# Roadmap: from the curl-noise orb to a magic-spell VFX

Condensed from the 2026-09-17 architecture review. Milestones are ordered
so every step has a visible result and a number to publish. Graphics code
(D3D12, HLSL, particle system, renderer) is written by hand; tooling,
build, benchmark plumbing and docs are scaffolded with Claude (CLAUDE.md).

## Target frame

```
CPU: advance spell timeline -> emitter params + spawn counts into this frame's ring slot
GPU (direct queue):
  Kickoff  [CS]            reset counters, write spawn + sim dispatch args
  Spawn    [CS, indirect]  pop dead -> init -> push alive
  Simulate [CS, indirect]  integrate, age, curl, forces; dead -> dead list,
                           alive -> next list + render record (cull folded in)
  Opaque   [gfx]           ground, occluders -> HDR RT + depth (write)
  Particles[ExecuteIndirect, one call, one command per emitter]
                           VS pulls render records; PS: shape SDF x noise erosion
                           x curve atlas, soft fade, premultiplied output
  Post                     distortion (optional), bloom, tonemap -> sRGB swapchain
```

Decisions behind this are recorded in ADR-0003 (HDR target), ADR-0004
(reverse-Z and depth SRV), ADR-0005 (lists, counters, barriers, indirect),
ADR-0006 (premultiplied blend), ADR-0007 (work graphs and mesh shaders) and
ADR-0010 (data-driven emitters behind one `VfxSystem` facade). The class
layout exists as an empty skeleton in `src/renderer/vfx/` (compiled as
`patronus_vfx`, format-checked); M2 to M4 fill its bodies and write the
matching `shaders/vfx/` kernels.

## Milestones

| | Objective | Visible result | Measure |
|---|---|---|---|
| M0 | Correct, measurable baseline | none | sim/draw ms at 10k/100k/1M; `DrawInstanced(4,N)` vs `DrawIndexedInstanced(6N,1)` |
| M1 | Make it glow: HDR RT, premultiplied blend, bloom, tonemap, depth + ground + occluders, soft particles, radial SDF sprite | the largest visual jump | GPU ms vs billboard size (fill-rate curve), bloom cost, PIX overdraw |
| M2 | GPU-driven core: dead/alive lists, kickoff/spawn/simulate, indirect dispatch + draw, emitter params, wave-aggregated atomics | continuous emission, several emitters | draw cost vs alive count; naive vs wave atomics |
| M3 | Materials and motion: curve atlas, noise erosion, UV distortion, shape set, velocity stretch, rotation, tileable emitter-local animated curl | sparks read as sparks, wisps dissolve | PS cost per shape |
| M4 | Assemble the spell: timeline, nine layers, ribbon trail, impact ring, flash, distortion, sub-emitter events | the hero shot | per-layer GPU cost table |
| M5 | Performance depth (optional): mesh-shader draw, half-res particles, work-graph sub-emitter experiment, sort for smoke | same image, faster | before/after for each |
| M6 | Packaging: video, README numbers, ADRs final | | |

### M0 checklist (hand-written side)

Done, 2026-09-19 — see
[devlog: M0](devlog/2026-09-19-m0-measurable-baseline.md):

- [x] Ring-buffer the camera constant buffer per frame in flight (one
  256-byte slot each; write after the fence wait, every frame, not only
  when the camera moved). Same rule for every future CPU-written per-frame
  resource.
- [x] Clamp `deltaTime` (0.1 s).
- [x] Release the particle and curl-noise upload buffers after the initial
  `WaitForGpu()`, with both copies recorded into that same command list.
- [x] Runtime vsync toggle on V, as a key *event* so a held key toggles
  once; benchmark runs need it off.
- [x] D3D12 timestamp query heap, per-frame readback, CSV writer in the
  schema of `tools/bench/README.md`. Method in
  [ADR-0011](adr/0011-gpu-timestamp-measurement.md).
- [x] ImGui, with its own CMake module and a reserved descriptor slot.

Open:

- [ ] Load the curl volume from `assets/noise/curl_noise_64_rgba16f.bin`
  via `GetAssetFullPath` (deployed by CMake) and sample it in
  emitter-local space with a WRAP sampler; read dimensions from the
  generated `curl_noise_64_rgba16f.h` or the `.json`. The app still loads
  the superseded non-periodic bake, which has a seam under WRAP. Then
  delete the old bake and `tools/curl noise generator/`.
- [x] Capture a baseline with a fixed camera and publish the table. Done,
  in the README.
- [ ] Settle `DrawInstanced(4, N)` against `DrawIndexedInstanced(6N, 1)`.
  The first A/B is inconclusive: the `sim` control zone, which the draw
  call cannot affect, varied 19% between the two runs while `render`
  varied 2%. Rerun alternating both paths inside one process, with the
  billboards shrunk so the draw is input-assembler bound rather than
  fill bound, which is the regime where the difference would appear.
  Worth adding as a third variant: `DrawInstanced(6N, 1)` with a
  six-entry corner lookup, which needs no index buffer at all.

Parked deliberately (not blocking M1):

- FPS limiter in `HPTimer` and the four commented-out CPU placement modes
  are left as comments for now.
- Tracy D3D12 zones: skipped in favour of the Visual Studio profiler and
  PIX.
- ImGui sliders for the simulation constants: deferred to M2, where the
  emitter parameters give them something to tune.

### Tooling already in place

- `tools/bake_curl_noise.py`: tileable curl volume, scalar noise in alpha,
  divergence verified, metadata JSON + constexpr header.
- `tools/bake_noise2d.py`: tileable RGBA8 noise pack for erosion,
  distortion, cells and density.
- `tools/bake_curves.py`: curve/gradient atlas from JSON
  (`assets/curves/arcane_bolt.json` is the starting spec for the nine
  layers).
- `tools/bench/bench_report.py`: percentiles, A/B tables, plots from the
  benchmark CSV.
- `tools/previs_spell.py` + `assets/emitters/*.json`: CPU previs of a
  spell. The emitter JSON is the source for the GPU `EmitterParams` rows;
  the previs report gives the pool sizes. `arcane_bolt` is the orb
  projectile from Part 5; `force_vortex` is the alternative: a horizontal
  tornado (ring/helix spawn + vortex force with explicit centripetal term,
  both of which the simulate kernel must reproduce).
- `cmake/CompileShaders.cmake`: SM 6.6 shader rules; `cmake/DeployAssets.cmake`
  copies `assets/` next to the executable.

## The nine layers (M4)

| Layer | Primitive | Blend | Motion | Life |
|---|---|---|---|---|
| Core | 4 to 8 large rotating billboards, noise-eroded radial SDF, intensity 8 to 20 | additive | attached to projectile | 0.5 s staggered |
| Energy shell | 3k/s on a sphere surface, band force, fine curl | additive | emitter-local curl + damping | 0.4 to 0.8 s |
| Curl wisps | 200/s, large soft, strong erosion, rotation | mostly alpha | large-scale animated curl, slight inward pull | 1.0 to 1.5 s |
| Sparks | bursts, velocity-stretched streaks, white-hot to red | additive | radial 8 to 15 u/s, gravity, drag | 0.3 to 0.8 s |
| Trail | ribbon, 48 segments, width tapers | additive | ring buffer of head positions | 0.6 s tail |
| Motes | 60/s tiny, twinkling | additive | slow rise, gentle curl | 2 to 3 s |
| Charge | 1.5k/s from radius 2.5 r inward, grows toward centre | additive | attractor with inner radius 0 | 0.5 to 0.8 s |
| Impact | 3k radial burst + expanding ring quad + 2-frame flash + distortion pulse | additive | radial 4 to 12 u/s, high drag | 0.3 to 0.8 s |
| Dissipation | embers (gravity, flicker) + smoke puffs (alpha, soft) + lingering motes | mixed | slow | 1.5 to 3 s |

Timeline: charge 0.0 to 0.8 s, launch, flight 0.8 to 1.8 s, impact at
1.8 s, dissipation to 4.0 s. All layers share the same kernels and shaders;
they differ only in emitter parameters and curve-atlas rows.

## The force vortex (alternative M4 target)

`assets/emitters/force_vortex.json` is the second spell, previs-approved
on 2026-09-18. It adds three requirements the orb does not have:

- **Ring/helix spawn** with coherent arms, a phase rate and a pitch
  (spawn kernel).
- **Vortex force**: target tangential speed relaxed at a rate, explicit
  centripetal term v^2/r, band pull toward the axis with a cone, axial
  push (simulate kernel). See the docstring of `tools/vfxtools/previs.py`
  for the exact formulas.
- **Ring geometry**: one wavy full ring plus two quarter-arcs that rotate,
  expand and travel with the spell, and two fast line-drawn impact rings.
  These are ring meshes (or ribbons) with a vertex shader doing the
  rotation, expansion and noise displacement, drawn with the same
  premultiplied PSO. Plan them together with the trail ribbon in M4.

| Layer | Primitive | Motion |
|---|---|---|
| dust | alpha particles, ring spawn around the path | vortex pull, slow spin |
| motes, wind | additive particles, ring spawn | vortex spin, wind pushed forward, streaks |
| arms | 3 helical arms, 1500/s | vortex spin 10 u/s, band 0.85 r, cone |
| main ring + arcs | ring geometry, created once | rotate, expand 0.35 to 0.45 u/s, radial wave |
| corebeam | bright spinning discs on the axis | ring spawn, fast phase |
| impact | flash, two line-drawn rings (expand 11 to 14 u/s, move 5 to 8 u/s), debris streaks, plume, residue | one-shot |

Timeline: charge 0.0 to 0.5 s, travel 0.5 to 1.5 s, impact at 1.5 s,
dissipation to 3.5 s.
