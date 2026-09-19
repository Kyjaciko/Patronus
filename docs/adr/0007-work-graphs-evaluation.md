# ADR 0007: Work graphs are an experiment for sub-emitters, not the base architecture

Status: Proposed
Date: 2026-09-17

## Context

D3D12 work graphs (Agility SDK 1.613, 2024; `D3D12_WORK_GRAPHS_TIER_1_0`)
let shader nodes emit records that launch other nodes with GPU-side
scheduling. Their advantage is for irregular workloads with unknown
fan-out, where the classic pattern needs a chain of "write arguments,
barrier, indirect dispatch" round trips or conservative over-allocation.

The particle frame (ADR-0005) is a fixed three-stage pipeline with known
upper bounds: kickoff, spawn, simulate. Indirect dispatch already handles
it with no CPU involvement, and the barrier bubbles between kernels are
microseconds. Rebuilding it as a work graph would add complexity, restrict
hardware support (RDNA 3 / Ampere or newer), and produce no measurable win.

Where fan-out *is* dynamic: sub-emitters. A dying particle emits an event
that spawns a burst of sparks; an impact emits one event that fans out to
several child emitters. In the classic design this is an event buffer
written by simulate and consumed by the child's spawn one frame later,
which costs a frame of latency and a fixed-size event buffer.

Mesh shaders are the non-experimental feature with a clearer payoff for
this project: they replace the input assembler for the particle draw,
remove the 4-vertex instancing inefficiency, allow per-particle culling
in the mesh shader, and are guaranteed by the feature level 12_2 the app
already requires.

## Decision

Build the base pipeline on indirect dispatch and `ExecuteIndirect`. Add a
mesh-shader draw path as the first advanced feature and benchmark it
against the indexed-quad draw. Implement sub-emitters with an event buffer
first, then, as a scoped experiment behind a feature check, a work-graph
version of the same sub-emitter chain. Publish the comparison (latency,
GPU time, code size, hardware support) with numbers from
`tools/bench/`.

## Consequences

- The project can say "evaluated work graphs against the established
  technique and here is the data", which is a stronger statement than
  "uses work graphs".
- The experiment is optional and can be dropped without touching the base
  pipeline.
- Hardware without work-graph support runs the event-buffer path; the app
  must check `D3D12_FEATURE_D3D12_OPTIONS21` and fall back cleanly.
- Two implementations of the same feature to keep correct while the
  experiment lives. Keep its scope to one chain (impact burst) to bound
  that cost.
