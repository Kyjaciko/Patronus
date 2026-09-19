#!/usr/bin/env python
"""Summarise a benchmark CSV (see README.md for the schema).

  python tools/bench/bench_report.py run.csv [--warmup N] [--compare base.csv]
                                             [--plot out.png] [--json]

Prints a markdown table of per-zone statistics (paste into the README), or
an A/B table against a baseline, or JSON. --plot writes a per-zone time
series so spikes and warm-up are visible, which a percentile hides.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

STATS = ("count", "mean", "p50", "p95", "p99", "min", "max")


@dataclass
class Run:
  path: Path
  metadata: dict[str, str] = field(default_factory=dict)
  zones: dict[str, np.ndarray] = field(default_factory=dict)  # zone -> ms per frame (in frame order)
  frames: dict[str, np.ndarray] = field(default_factory=dict)  # zone -> frame indices


def load_run(path: Path, warmup: int = 0) -> Run:
  run = Run(path=path)
  samples: dict[str, list[tuple[int, float]]] = {}
  with path.open("r", encoding="utf-8", newline="") as file:
    header_seen = False
    reader = csv.reader(line for line in file if line.strip())
    for row in reader:
      if row[0].startswith("#"):
        text = ",".join(row)[1:].strip()
        if "=" in text:
          key, value = text.split("=", 1)
          run.metadata[key.strip()] = value.strip()
        continue
      if not header_seen:
        expected = ["frame", "zone", "ms"]
        if [c.strip().lower() for c in row] != expected:
          raise ValueError(f"{path}: expected header {expected}, got {row}")
        header_seen = True
        continue
      if len(row) != 3:
        raise ValueError(f"{path}: bad row {row}")
      frame = int(row[0])
      if frame < warmup:
        continue
      samples.setdefault(row[1].strip(), []).append((frame, float(row[2])))

  if not samples:
    raise ValueError(f"{path}: no samples after warmup={warmup}")
  for zone, pairs in samples.items():
    pairs.sort()
    run.frames[zone] = np.array([p[0] for p in pairs], dtype=np.int64)
    run.zones[zone] = np.array([p[1] for p in pairs], dtype=np.float64)
  return run


def zone_stats(ms: np.ndarray) -> dict[str, float]:
  return {
    "count": int(ms.size),
    "mean": float(ms.mean()),
    "p50": float(np.percentile(ms, 50)),
    "p95": float(np.percentile(ms, 95)),
    "p99": float(np.percentile(ms, 99)),
    "min": float(ms.min()),
    "max": float(ms.max()),
  }


def summarise(run: Run) -> dict[str, dict[str, float]]:
  return {zone: zone_stats(ms) for zone, ms in run.zones.items()}


def _fmt(value: float) -> str:
  return f"{value:.3f}"


def markdown_table(run: Run) -> str:
  lines = []
  if run.metadata:
    lines.append("  ".join(f"`{k}={v}`" for k, v in run.metadata.items()))
    lines.append("")
  lines.append("| zone | frames | mean ms | p50 ms | p95 ms | p99 ms | min | max |")
  lines.append("|---|---:|---:|---:|---:|---:|---:|---:|")
  for zone, s in summarise(run).items():
    lines.append(
      f"| {zone} | {s['count']} | {_fmt(s['mean'])} | {_fmt(s['p50'])} | {_fmt(s['p95'])} "
      f"| {_fmt(s['p99'])} | {_fmt(s['min'])} | {_fmt(s['max'])} |"
    )
  return "\n".join(lines)


def compare_table(run: Run, base: Run) -> str:
  """A/B table: delta is (run - base) / base, negative = faster."""
  a, b = summarise(run), summarise(base)
  lines = [
    f"Baseline: `{base.path.name}`  Candidate: `{run.path.name}`",
    "",
    "| zone | base p50 | new p50 | delta p50 | base p95 | new p95 | delta p95 |",
    "|---|---:|---:|---:|---:|---:|---:|",
  ]
  for zone in a:
    if zone not in b:
      lines.append(f"| {zone} | - | {_fmt(a[zone]['p50'])} | n/a | - | {_fmt(a[zone]['p95'])} | n/a |")
      continue

    def delta(key: str) -> str:
      old, new = b[zone][key], a[zone][key]
      if old <= 0:
        return "n/a"
      return f"{(new - old) / old * 100:+.1f}%"

    lines.append(
      f"| {zone} | {_fmt(b[zone]['p50'])} | {_fmt(a[zone]['p50'])} | {delta('p50')} "
      f"| {_fmt(b[zone]['p95'])} | {_fmt(a[zone]['p95'])} | {delta('p95')} |"
    )
  missing = [z for z in b if z not in a]
  if missing:
    lines.append("")
    lines.append("Zones only in baseline: " + ", ".join(missing))
  return "\n".join(lines)


# Fixed categorical order (never cycled); zones get colours in first-seen order.
_SERIES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#4a3aa7", "#e34948"]
_INK, _MUTED, _GRID, _SURFACE = "#0b0b0b", "#898781", "#e1e0d9", "#fcfcfb"


def plot(run: Run, path: Path) -> None:
  import matplotlib

  matplotlib.use("Agg")
  import matplotlib.pyplot as plt

  zones = list(run.zones)
  if len(zones) > len(_SERIES):
    # Beyond the palette, keep the largest zones and fold the rest.
    zones = sorted(zones, key=lambda z: -float(np.median(run.zones[z])))[: len(_SERIES)]

  fig, ax = plt.subplots(figsize=(11, 4.5), constrained_layout=True)
  fig.patch.set_facecolor(_SURFACE)
  ax.set_facecolor(_SURFACE)
  for colour, zone in zip(_SERIES, zones):
    ax.plot(run.frames[zone], run.zones[zone], color=colour, linewidth=1.5, label=zone)
    p50 = float(np.percentile(run.zones[zone], 50))
    ax.annotate(f"{zone} p50 {p50:.2f} ms", xy=(run.frames[zone][-1], run.zones[zone][-1]),
                xytext=(6, 0), textcoords="offset points", fontsize=8, color=_INK, va="center")

  ax.set_xlabel("frame", color=_MUTED)
  ax.set_ylabel("ms", color=_MUTED)
  ax.tick_params(colors=_MUTED, labelsize=8)
  ax.grid(True, axis="y", color=_GRID, linewidth=0.8)
  for side in ("top", "right"):
    ax.spines[side].set_visible(False)
  for side in ("left", "bottom"):
    ax.spines[side].set_color(_GRID)
  ax.set_ylim(bottom=0)
  if len(zones) > 1:
    ax.legend(frameon=False, fontsize=8, loc="upper right")
  note = run.metadata.get("note", "")
  title = f"{run.path.name}" + (f": {note}" if note else "")
  ax.set_title(title, color=_INK, fontsize=10, loc="left")
  fig.savefig(path, dpi=120)
  plt.close(fig)


def main(argv: list[str] | None = None) -> int:
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("csv", type=Path)
  parser.add_argument("--warmup", type=int, default=0, help="drop frames with index < N")
  parser.add_argument("--compare", type=Path, default=None, help="baseline CSV for an A/B table")
  parser.add_argument("--plot", type=Path, default=None, help="write a per-zone time-series PNG")
  parser.add_argument("--json", action="store_true", help="print JSON instead of markdown")
  args = parser.parse_args(argv)

  run = load_run(args.csv, args.warmup)
  if args.json:
    print(json.dumps({"metadata": run.metadata, "zones": summarise(run)}, indent=2))
  elif args.compare:
    print(compare_table(run, load_run(args.compare, args.warmup)))
  else:
    print(markdown_table(run))

  if args.plot:
    plot(run, args.plot)
    print(f"\nplot written to {args.plot}", file=sys.stderr)
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
