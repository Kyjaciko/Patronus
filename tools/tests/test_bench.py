"""Tests for tools/bench/bench_report.py."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

TOOLS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR / "bench"))

import bench_report  # noqa: E402

EXAMPLE = TOOLS_DIR / "bench" / "example.csv"


def write_csv(path: Path, rows: list[tuple[int, str, float]], metadata: dict | None = None) -> None:
  lines = [f"# {k}={v}" for k, v in (metadata or {}).items()]
  lines.append("frame,zone,ms")
  lines += [f"{f},{z},{ms}" for f, z, ms in rows]
  path.write_text("\n".join(lines) + "\n", encoding="utf-8")


class LoadTests(unittest.TestCase):
  def test_example_loads_with_metadata(self):
    run = bench_report.load_run(EXAMPLE)
    self.assertEqual(run.metadata["synthetic"], "true")
    self.assertEqual(set(run.zones), {"sim", "draw", "frame"})
    self.assertEqual(run.zones["sim"].size, 20)

  def test_warmup_drops_frames(self):
    run = bench_report.load_run(EXAMPLE, warmup=5)
    self.assertEqual(run.zones["sim"].size, 15)
    self.assertEqual(int(run.frames["sim"][0]), 5)

  def test_bad_header_rejected(self):
    with tempfile.TemporaryDirectory() as tmp:
      path = Path(tmp) / "bad.csv"
      path.write_text("frame,name,ms\n0,a,1\n", encoding="utf-8")
      with self.assertRaises(ValueError):
        bench_report.load_run(path)


class StatsTests(unittest.TestCase):
  def test_percentiles_on_known_data(self):
    ms = np.arange(1, 101, dtype=np.float64)  # 1..100
    stats = bench_report.zone_stats(ms)
    self.assertEqual(stats["count"], 100)
    self.assertAlmostEqual(stats["mean"], 50.5)
    self.assertAlmostEqual(stats["p50"], 50.5)
    self.assertAlmostEqual(stats["p95"], 95.05)
    self.assertAlmostEqual(stats["min"], 1.0)
    self.assertAlmostEqual(stats["max"], 100.0)

  def test_markdown_table_has_all_zones(self):
    table = bench_report.markdown_table(bench_report.load_run(EXAMPLE))
    for zone in ("sim", "draw", "frame"):
      self.assertIn(f"| {zone} |", table)

  def test_compare_delta_sign(self):
    with tempfile.TemporaryDirectory() as tmp:
      base = Path(tmp) / "base.csv"
      new = Path(tmp) / "new.csv"
      write_csv(base, [(i, "draw", 2.0) for i in range(10)])
      write_csv(new, [(i, "draw", 1.0) for i in range(10)] + [(i, "extra", 0.5) for i in range(10)])
      table = bench_report.compare_table(bench_report.load_run(new), bench_report.load_run(base))
      self.assertIn("-50.0%", table)
      self.assertIn("| extra |", table)


class PlotTests(unittest.TestCase):
  def test_plot_writes_png(self):
    with tempfile.TemporaryDirectory() as tmp:
      out = Path(tmp) / "plot.png"
      bench_report.plot(bench_report.load_run(EXAMPLE), out)
      self.assertGreater(out.stat().st_size, 1000)


if __name__ == "__main__":
  unittest.main()
