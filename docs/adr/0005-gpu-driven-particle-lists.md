# ADR 0005: GPU-driven particle lifetime with explicit counters and indirect execution

Status: Proposed
Date: 2026-09-17

## Context

Today the CPU initialises a fixed pool once and one compute kernel
integrates every slot every frame. There is no emission, no death, and
draw cost is proportional to the pool size rather than to the number of
live particles. A spell needs emitters with rates, bursts and lifetimes,
and the CPU should never need the live count.

Alternatives considered:

- Fixed pool, respawn in place (each thread checks `age >= life` and
  re-initialises itself). No lists, no atomics, no indirect calls. Sim and
  draw cost scale with pool size; per-frame spawn counts are approximate.
  Visually equivalent for one spell. Kept as the baseline to benchmark
  against.
- `AppendStructuredBuffer` / `ConsumeStructuredBuffer`. D3D11-era. In D3D12
  the hidden counter lives in a separate buffer at a 4096-byte-aligned
  offset, cannot be read by shaders except through the append intrinsics,
  needs a copy or UAV clear to reset, and needs another copy to become an
  indirect argument. Rejected.
- Explicit counters in a `RWByteAddressBuffer` with `InterlockedAdd`,
  alive/dead index lists, and `ExecuteIndirect`. The pattern used by
  shipping engines (Frostbite, Wicked Engine, Unity VFX Graph) and the one
  described in Gareth Thomas' GDC 2014 talk.
- Work graphs as the base architecture. Evaluated separately in ADR-0007;
  not chosen for the base pipeline.

## Decision

Per emitter: a particle pool, a persistent dead list, two alive lists
(ping-ponged by rebinding, never copied), a compact render buffer written
by the simulate kernel, a counters buffer that stays in `UNORDERED_ACCESS`
all frame, and a separate indirect-arguments buffer that transitions
`UNORDERED_ACCESS <-> INDIRECT_ARGUMENT` around `ExecuteIndirect`.

Frame order on the direct queue: kickoff (one thread per emitter: reset
counters, write spawn arguments, write an upper-bound sim dispatch count of
`alivePrev + spawnCount`), UAV barrier, spawn (indirect), UAV barrier,
simulate (indirect; dead particles push to the dead list, live ones push
to the next alive list and write a render record; the frustum test is
folded in), UAV barrier, argument transition, opaque pass, one
`ExecuteIndirect` with one command per emitter for the particle draw.

Counter increments use wave aggregation (`WaveActiveCountBits`,
`WavePrefixCountBits`, one `InterlockedAdd` per wave) rather than one
atomic per particle.

Every dispatch-to-dispatch dependency on the same UAV gets a
`D3D12_RESOURCE_BARRIER_TYPE_UAV` barrier. The debug layer does not flag
their absence; the failure is vendor-specific flicker.

Nothing is ever read back to the CPU to drive the frame. A delayed
readback of the counters may feed the ImGui debug display only.

## Consequences

- Draw and simulate cost become proportional to live particles. This is
  the measurable claim the benchmark harness (`tools/bench/`) exists to
  record, against the fixed-pool baseline.
- Three dispatches plus one `ExecuteIndirect` per frame regardless of
  emitter count. CPU cost per frame stays constant.
- Spawn counts are exact per frame; bursts are a spawn count on one frame.
- Sub-emitters (spawn on death, spawn on impact) fit as an event buffer
  written by simulate and consumed by the child's spawn one frame later.
- The pool per emitter is fixed-size; when the dead list is empty, spawn
  fails silently for that frame. Pool sizes are an authoring parameter and
  a debug counter should show high-water marks.
- More state to reason about than the fixed-pool model. The ping-pong
  binding swap and the barrier set are documented here precisely so that
  reasoning is written down once.
