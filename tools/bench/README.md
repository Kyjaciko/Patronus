# Benchmark harness

Turns per-frame GPU/CPU timings into the numbers that go in the README:
percentiles per zone, A/B deltas, and a plot. The renderer writes the CSV,
this directory turns it into a report.

The writer is `patronus::profiling::FrameTimingLog` (`src/profiling/`);
the D3D12 timestamp queries that feed it, and the two-frame-late readback
that keeps measurement free of synchronisation, are described in
ADR-0011. Zones emitted today: `frame` (whole command list), `sim`
(compute dispatch), `render` (particle draw).

## CSV schema

```
# key=value metadata lines, any number, before the header
# gpu=NVIDIA GeForce RTX 4070
# config=RelWithDebInfo
# resolution=1920x1080
# particles=1000000
# note=DrawInstanced(4,N) baseline
frame,zone,ms
0,sim,0.412
0,draw,1.903
1,sim,0.409
1,draw,1.887
```

- `frame`: integer frame index, monotonically increasing.
- `zone`: free-form name. Keep them stable across runs so `--compare` can
  match them (`sim`, `spawn`, `draw`, `bloom`, `frame`, ...).
- `ms`: duration in milliseconds as a float. GPU zones come from timestamp
  pairs divided by `GetTimestampFrequency()`; CPU zones (if you log them)
  use the same column, just a different zone name (`cpu.record`).
- Metadata is optional and echoed into the report header. `note=` is the
  one to fill in: what was different about this run.

Long format (one row per zone per frame) rather than one column per zone so
the writer never needs to know the zone set up front, and adding a zone
does not change the schema.

Runs are per-machine measurements, not source: write them to
`benchmarks/runs/` (gitignored) and commit only the report tables or plots.

## Usage

```powershell
# Percentile table (markdown) for one run, skipping the first 60 frames
python tools/bench/bench_report.py benchmarks/runs/indexed.csv --warmup 60

# A/B against a baseline: p50/p95 for both plus delta %
python tools/bench/bench_report.py benchmarks/runs/indexed.csv --compare benchmarks/runs/instanced.csv

# Time-series plot per zone
python tools/bench/bench_report.py benchmarks/runs/indexed.csv --plot docs/media/bench_indexed.png

# Machine-readable
python tools/bench/bench_report.py benchmarks/runs/indexed.csv --json
```

`example.csv` is synthetic data for exercising the script and CI. It is
not a measurement of anything.

## Measurement hygiene (what the report cannot fix)

- **Disable the CPU frame-rate limiter and vsync for GPU timings**; otherwise
  `frame` measures the display, not the renderer.
- Warm up: the first frames include PSO compilation and driver upload. Use
  `--warmup`.
- **One scenario per file.** Fixed camera, deterministic seed, nothing
  touched during the capture. Flying the camera around mid-run produced a
  file whose `render` p50 varied by 8x between segments while its
  percentile table looked perfectly ordinary; see
  `docs/devlog/2026-09-19-m0-measurable-baseline.md`. Always run `--plot`
  once on a new capture: a percentile table cannot show you that a run is
  really three runs, and a time series shows it immediately.
- **Warm up past the scene, not just the driver.** `--warmup` has to cover
  whatever the simulation does before it reaches steady state, which can
  be thousands of frames, not the 60 or so that cover PSO compilation and
  the first uploads. The plot shows where it settles.
- **Keep a control zone in every A/B.** Always time at least one zone the
  change under test cannot possibly affect. Comparing the two draw-call
  variants, `render` differed by 2% while `sim`, which is identical
  compute work in both builds, differed by 19% — so the runs sat at
  different clock states and the 2% meant nothing. A control zone is the
  only thing in the file that tells you whether the rest of it is
  resolvable. If the control moves more than the signal, the answer is
  "rerun", not "ship the number".
- **Interleave A and B in one process** when the effect you are chasing is
  small. Separate runs pick up separate thermal and clock states, and that
  difference is easily larger than the thing being measured.
- Report p50 and p95, not the mean. Spikes matter for frame pacing and the
  mean hides them.
- Record `gpu=`, `config=`, `resolution=`, `particles=` every time. A
  number without its conditions is not a result.
