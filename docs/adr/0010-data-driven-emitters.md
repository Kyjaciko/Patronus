# ADR 0010: Data-driven emitters behind one facade, no per-effect classes

Status: Proposed
Date: 2026-09-18

## Context

Two target effects exist as previs-approved data (`arcane_bolt.json` and
`force_vortex.json`, 13 emitters each) and more will follow. The runtime
has to play all of them without duplicating simulation or rendering code,
keep CPU and GPU cost flat as the emitter count grows, and let an artist
change the look without a rebuild. Every file must also be explainable in
an interview.

Alternatives considered:

- **A class per effect** (`ArcaneBolt`, `ForceVortex`) deriving from an
  effect base with virtual `Update` and `Draw`. Familiar from gameplay
  code. Each effect re-implements spawn and force logic, tuning needs a
  rebuild, and each effect issues its own dispatches and draws, so cost
  grows with the effect count.
- **A class per emitter type** (`SphereEmitter`, `RingEmitter`,
  `VortexEmitter`). The tornado arms need ring spawn *and* vortex force
  *and* curl, so the combinations multiply.
- **One parameter struct per emitter, shared kernels, enum switches.**
  Every behaviour is a field; zero disables it. The previs already works
  this way (`EmitterSpec`), which is why both spells needed no code in it.
- **Rings as particles.** Rejected in the previs: a ring that lives for
  the whole spell, rotates, expands and deforms is one mesh driven by a
  vertex shader, not hundreds of particles holding formation.

## Decision

One `VfxSystem` is the only class the application calls (`Initialize`,
`LoadSpell`, `Play`, `Update`, `RecordSimulate`, `RecordDraw`,
`DrawDebugUi`, `Shutdown`). Effects are data: `SpellAsset` parses the
JSON into `SpellDesc`, `SpellInstance` plays the timeline and writes one
`EmitterParams` row per emitter per frame, `VfxResources` holds one
allocation per buffer type sub-ranged per emitter, and `VfxPipeline` runs
the same three kernels and two shaders for every row. Behaviour variation
is `uint` enums and zeroed strengths, switched on values that are uniform
per dispatch or per draw. Ring geometry is a second plain struct
(`RingParams`) and a second shader, not a subclass. No inheritance, no
virtual dispatch, no per-effect code. Layout and per-field meaning live in
the headers under `src/renderer/vfx/`; `VfxTypes.h` is the source of truth
for the HLSL twin.

Frame contract with the app: `Update` after the fence wait,
`RecordSimulate` before the opaque pass, `RecordDraw` after it with the HDR
target and the read-only depth bound. All simulation runs in emitter space;
`follow` decides whether that space moves with the anchor every frame (the
vertex shader adds the anchor) or is frozen at spawn.

## Consequences

- Adding a spell is adding a JSON file and a curve spec. The previs and
  the runtime read the same file, so the previs stays the CPU twin and is
  the first place to debug a look.
- Per-frame cost is fixed: kickoff, two indirect compute launches, one
  `ExecuteIndirect` for all particle emitters, one draw for all rings.
- `EmitterParams` grows with every feature and must stay a multiple of 16
  bytes with a mirrored HLSL twin; sizes are guarded by `static_assert`,
  field order by review.
- One uber kernel and one uber pixel shader branch on uniform values.
  Register pressure is the cost; if the simulate kernel needs more than
  64 VGPRs, split it by spawn shape or force set, still driven by the same
  rows.
- A JSON parser (or a bake step) is needed on the C++ side; that is a
  dependency decision taken separately.
- First version limits, on purpose: one playback per loaded spell, no
  sorting, no sub-emitters yet (they plug in as an event buffer between
  simulate and the next frame's spawn without changing the layout).
