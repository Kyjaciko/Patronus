# ADR 0011: GPU timing by timestamp queries, read back two frames late

Status: Accepted
Date: 2026-09-19

## Context

Every performance claim this project makes needs a number produced on the
GPU, not a frame rate read off a window title. The measurement itself must
not change what it measures: the moment the CPU waits for the GPU to hand
back a timing value, the pipelining that the renderer is built around
(ADR-0002) disappears and every number afterwards is wrong.

Alternatives considered:

- **CPU timing around `ExecuteCommandLists`.** Measures submission, not
  execution. Submission returns immediately, so the number is meaningless.
- **CPU timing with a fence wait per pass.** Correct, and it serialises the
  CPU and GPU completely. The act of measuring removes the overlap being
  measured.
- **Tracy's D3D12 zones only.** Excellent for a live timeline, but the
  results live in a running profiler session rather than a file that can be
  aggregated, diffed between runs and pasted into a README.
- **Timestamp queries resolved into a readback buffer, read after the
  fence wait that the frame loop already performs.** No added
  synchronisation at all: the wait is one the renderer does anyway.

## Decision

One `ID3D12QueryHeap` of type `TIMESTAMP` with `kFramesInFlight *
kSlotsPerFrame` slots, and one readback buffer of the same capacity that
stays in `COPY_DEST` for its whole life. Six slots per frame bracket three
zones: `frame` (the whole command list), `sim` (the dispatch) and `render`
(the particle draw).

Frame N writes its six timestamps with `EndQuery` and ends its command list
with a single `ResolveQueryData` into region `N % kFramesInFlight` of the
readback buffer. In `BeginFrame`, immediately after the per-slot fence wait
that the frame loop already performs, the CPU maps that same region. The
wait proves frame N-2 has completed, and frame N-2 is the last frame that
wrote this region, so the data is ready and no new synchronisation was
introduced. The values are therefore attributed to frame `counter -
kFramesInFlight`, and the first `kFramesInFlight` frames are skipped
because their regions have never been resolved.

Ticks become milliseconds through `GetTimestampFrequency`, captured once
after the command queue is created. The samples go to
`patronus::profiling::FrameTimingLog`, which writes the long-format CSV
that `tools/bench/bench_report.py` aggregates. Zone naming, percentiles and
plotting live in that tool, not in the renderer.

## Consequences

- Measurement costs one resolve per frame and adds no CPU/GPU
  synchronisation, so the renderer under measurement is the renderer that
  ships.
- Every reported number is two frames old. Irrelevant for aggregation,
  which is the only consumer; a live on-screen readout would show the same
  two-frame lag.
- The readback buffer is written by the GPU and read by the CPU without a
  barrier, which is legal only because the fence wait orders them. That
  reasoning is the whole decision, so it is written here and repeated in a
  comment at the read site.
- Timestamps bracket positions in the command stream, not isolated work.
  Without a barrier between them, adjacent GPU work can overlap a
  boundary, so a zone is a good relative measure across runs and a weak
  absolute measure of one draw in isolation. A/B comparisons of the same
  zone are the intended use.
- Zone names are strings in the CSV, so adding a zone costs one `EndQuery`
  pair and two constants. The schema never changes.
- Results are per machine and per driver. Runs live in `benchmarks/runs/`,
  which is gitignored; only aggregated tables are committed.
