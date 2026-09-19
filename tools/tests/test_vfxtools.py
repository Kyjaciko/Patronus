"""Tests for the bake tools. Run from the repo root:

  python -m unittest discover -s tools/tests -t tools
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

TOOLS_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS_DIR))

import bake_curl_noise  # noqa: E402
import bake_curves  # noqa: E402
import bake_noise2d  # noqa: E402
from vfxtools import noise as vn  # noqa: E402
from vfxtools.rawtexture import write_raw_texture  # noqa: E402


def seam_ratio(field: np.ndarray, axis: int) -> float:
  """Std of the wrap-around difference divided by std of interior
  differences along `axis`. ~1 when the field tiles, large when it doesn't."""
  interior = np.diff(field, axis=axis)
  wrap = np.take(field, 0, axis=axis) - np.take(field, -1, axis=axis)
  return float(wrap.std() / max(interior.std(), 1e-8))


class PerlinTests(unittest.TestCase):
  def test_3d_tiles_on_every_axis(self):
    field = vn.perlin3d_periodic((32, 32, 32), (4, 4, 4), seed=3)
    for axis in range(3):
      self.assertLess(seam_ratio(field, axis), 1.5, f"seam on axis {axis}")

  def test_3d_non_periodic_reference_has_a_seam(self):
    # Sanity check of the seam metric itself: a shifted field must fail it.
    field = vn.perlin3d_periodic((32, 32, 32), (4, 4, 4), seed=3)
    broken = np.concatenate([field[:, :, 8:], field[:, :, :8] * 3.0], axis=2)
    self.assertGreater(seam_ratio(broken, 2), 1.5)

  def test_2d_tiles(self):
    field = vn.perlin2d_periodic((64, 64), (4, 4), seed=1)
    self.assertLess(seam_ratio(field, 0), 1.5)
    self.assertLess(seam_ratio(field, 1), 1.5)

  def test_range_and_mean(self):
    field = vn.fbm(vn.perlin3d_periodic, (32, 32, 32), (4, 4, 4), seed=5, octaves=3)
    self.assertGreaterEqual(field.min(), -1.0)
    self.assertLessEqual(field.max(), 1.0)
    self.assertLess(abs(field.mean()), 0.1)


class WorleyTests(unittest.TestCase):
  def test_tiles_and_range(self):
    field = vn.worley2d_periodic((64, 64), (8, 8), seed=2)
    self.assertLess(seam_ratio(field, 0), 1.5)
    self.assertLess(seam_ratio(field, 1), 1.5)
    self.assertGreaterEqual(field.min(), 0.0)
    self.assertLessEqual(field.max(), 1.0)


class CurlTests(unittest.TestCase):
  def test_curl_is_divergence_free(self):
    shape, lattice, spacing = (32, 32, 32), (4, 4, 4), (0.5, 0.5, 0.5)
    psi = [vn.fbm(vn.perlin3d_periodic, shape, lattice, seed=10 + i, octaves=2) for i in range(3)]
    vx, vy, vz = vn.curl_periodic(*psi, spacing)
    div = vn.divergence_periodic(vx, vy, vz, spacing)
    magnitude = np.sqrt(vx * vx + vy * vy + vz * vz).max()
    self.assertLess(np.abs(div).max() / magnitude, 1e-4)

  def test_curl_field_tiles(self):
    shape, lattice, spacing = (32, 32, 32), (4, 4, 4), (0.5, 0.5, 0.5)
    psi = [vn.fbm(vn.perlin3d_periodic, shape, lattice, seed=20 + i, octaves=2) for i in range(3)]
    for component in vn.curl_periodic(*psi, spacing):
      for axis in range(3):
        self.assertLess(seam_ratio(component, axis), 1.5)

  def test_normalise_hits_percentile(self):
    field = [np.random.default_rng(0).normal(size=(16, 16, 16)).astype(np.float32) for _ in range(3)]
    (a, b, c), _ = vn.normalise_to_percentile(field, 99.0, 2.0)
    magnitude = np.sqrt(a * a + b * b + c * c)
    self.assertAlmostEqual(float(np.percentile(magnitude, 99.0)), 2.0, places=4)


class BakeCurlNoiseTests(unittest.TestCase):
  def test_bake_shape_and_metadata(self):
    texels, meta = bake_curl_noise.bake(size=16, period=2, octaves=2, tile_world_size=8.0, seed=1)
    self.assertEqual(texels.shape, (16, 16, 16, 4))
    self.assertEqual(texels.dtype, np.float16)
    self.assertTrue(meta["tileable"])
    self.assertLess(meta["max_abs_divergence_over_max_magnitude"], 1e-3)
    alpha = texels[..., 3].astype(np.float32)
    self.assertGreaterEqual(alpha.min(), 0.0)
    self.assertLessEqual(alpha.max(), 1.0)


class BakeNoise2dTests(unittest.TestCase):
  def test_bake_shape_and_full_range(self):
    texels, meta = bake_noise2d.bake(size=64, period=2, seed=3)
    self.assertEqual(texels.shape, (64, 64, 4))
    self.assertEqual(texels.dtype, np.uint8)
    for channel in range(4):
      self.assertLessEqual(int(texels[..., channel].min()), 8)
      self.assertGreaterEqual(int(texels[..., channel].max()), 247)
    self.assertTrue(meta["tileable"])


class BakeCurvesTests(unittest.TestCase):
  def test_linear_curve_reproduces_keys(self):
    u = np.array([0.0, 0.25, 0.5, 1.0], dtype=np.float32)
    values = bake_curves.evaluate_curve([[0.0, 0.0], [0.5, 1.0], [1.0, 0.0]], u)
    np.testing.assert_allclose(values[:, 0], [0.0, 0.5, 1.0, 0.0], atol=1e-6)
    np.testing.assert_allclose(values[:, 1:], 0.0)

  def test_rgba_and_rgb_keys(self):
    values = bake_curves.evaluate_curve([[0.0, [1, 2, 3]], [1.0, [2, 4, 6, 0]]], np.array([0.5]))
    np.testing.assert_allclose(values[0], [1.5, 3.0, 4.5, 0.5], atol=1e-6)

  def test_smooth_interp_is_monotone_and_hits_ends(self):
    u = np.linspace(0.0, 1.0, 33, dtype=np.float32)
    values = bake_curves.evaluate_curve([[0.0, 0.0], [1.0, 1.0]], u, interp="smooth")[:, 0]
    self.assertAlmostEqual(float(values[0]), 0.0)
    self.assertAlmostEqual(float(values[-1]), 1.0)
    self.assertTrue(np.all(np.diff(values) >= 0.0))
    self.assertAlmostEqual(float(values[16]), 0.5, places=5)

  def test_outside_key_range_clamps(self):
    values = bake_curves.evaluate_curve([[0.2, 5.0], [0.8, 7.0]], np.array([0.0, 1.0]))
    np.testing.assert_allclose(values[:, 0], [5.0, 7.0])

  def test_bake_rows_in_order(self):
    spec = {"width": 8, "curves": [
      {"name": "a", "keys": [[0, 1.0]]},
      {"name": "b", "keys": [[0, [1, 2, 3, 4]]]},
    ]}
    texels, meta = bake_curves.bake(spec)
    self.assertEqual(texels.shape, (2, 8, 4))
    self.assertEqual(meta["rows"]["a"]["row"], 0)
    self.assertEqual(meta["rows"]["b"]["row"], 1)
    self.assertAlmostEqual(meta["rows"]["b"]["v"], 0.75)
    np.testing.assert_allclose(texels[1, 3].astype(np.float32), [1, 2, 3, 4])

  def test_example_spec_bakes(self):
    spec_path = TOOLS_DIR.parent / "assets" / "curves" / "arcane_bolt.json"
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    texels, meta = bake_curves.bake(spec)
    self.assertEqual(texels.shape[1], spec["width"])
    self.assertEqual(meta["row_count"], len(spec["curves"]))


class RawTextureTests(unittest.TestCase):
  def test_3d_rgba16f_layout(self):
    with tempfile.TemporaryDirectory() as tmp:
      stem = Path(tmp) / "vol"
      texels = np.zeros((4, 3, 2, 4), dtype=np.float16)  # depth 4, height 3, width 2
      meta = write_raw_texture(stem, texels, "DXGI_FORMAT_R16G16B16A16_FLOAT", {"extra": 1})
      self.assertEqual((meta["width"], meta["height"], meta["depth"]), (2, 3, 4))
      self.assertEqual(meta["row_pitch"], 2 * 8)
      self.assertEqual(meta["slice_pitch"], 2 * 8 * 3)
      self.assertEqual(meta["byte_size"], 2 * 8 * 3 * 4)
      self.assertEqual(stem.with_suffix(".bin").stat().st_size, meta["byte_size"])
      on_disk = json.loads(stem.with_suffix(".json").read_text(encoding="utf-8"))
      self.assertEqual(on_disk["extra"], 1)
      self.assertEqual(on_disk["array_order"], "[z, y, x, channel]")

  def test_2d_r8_accepts_2d_array(self):
    with tempfile.TemporaryDirectory() as tmp:
      stem = Path(tmp) / "tex"
      meta = write_raw_texture(stem, np.zeros((5, 7), dtype=np.uint8), "DXGI_FORMAT_R8_UNORM")
      self.assertEqual((meta["width"], meta["height"], meta["depth"]), (7, 5, 1))
      self.assertEqual(meta["byte_size"], 35)

  def test_wrong_channel_count_rejected(self):
    with tempfile.TemporaryDirectory() as tmp:
      with self.assertRaises(ValueError):
        write_raw_texture(Path(tmp) / "x", np.zeros((2, 2, 3), dtype=np.uint8), "DXGI_FORMAT_R8G8B8A8_UNORM")


if __name__ == "__main__":
  unittest.main()
