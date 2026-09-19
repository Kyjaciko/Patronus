"""Tests for the spell previs: the logic checks that matter for the GPU port."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

import numpy as np

TOOLS_DIR = Path(__file__).resolve().parents[1]
REPO = TOOLS_DIR.parent
sys.path.insert(0, str(TOOLS_DIR))

from vfxtools import previs as pv  # noqa: E402

SPEC = REPO / "assets" / "emitters" / "arcane_bolt.json"


def emitter(**overrides) -> pv.Emitter:
  d = {"name": "t", "pool": 512, "life": [1.0, 1.0]}
  d.update(overrides)
  return pv.Emitter(pv.EmitterSpec.from_dict(d), seed=3)


ORIGIN = np.zeros(3, np.float32)
STILL = np.zeros(3, np.float32)


class SamplingTests(unittest.TestCase):
  def test_volume_wrap_is_periodic(self):
    vol = np.random.default_rng(0).random((8, 8, 8, 4)).astype(np.float16)
    uvw = np.random.default_rng(1).random((50, 3)).astype(np.float32) * 3 - 1
    a = pv.sample_volume_wrap(vol, uvw)
    b = pv.sample_volume_wrap(vol, uvw + np.array([2.0, -1.0, 5.0], np.float32))
    np.testing.assert_allclose(a, b, atol=1e-5)

  def test_volume_texel_centre_convention(self):
    vol = np.zeros((4, 4, 4, 1), np.float32)
    vol[1, 2, 3, 0] = 1.0
    centre = np.array([[(3 + 0.5) / 4, (2 + 0.5) / 4, (1 + 0.5) / 4]], np.float32)
    self.assertAlmostEqual(float(pv.sample_volume_wrap(vol, centre)[0, 0]), 1.0, places=5)

  def test_curve_clamps_and_hits_ends(self):
    atlas = np.zeros((1, 8, 4), np.float32)
    atlas[0, :, 0] = np.arange(8)
    v = pv.sample_curve(atlas, 0, np.array([-1.0, 0.0, 1.0, 2.0]))[:, 0]
    np.testing.assert_allclose(v, [0.0, 0.0, 7.0, 7.0])
    mid = pv.sample_curve(atlas, 0, np.array([0.5]))[0, 0]
    self.assertAlmostEqual(float(mid), 3.5, places=5)


class TimelineTests(unittest.TestCase):
  def test_phases(self):
    tl = pv.Timeline({"start": [0, 0, 0], "target": [10, 0, 0], "charge_end": 1.0, "impact": 2.0})
    np.testing.assert_allclose(tl.projectile(0.5), [0, 0, 0])
    np.testing.assert_allclose(tl.projectile(1.5), [5, 0, 0])
    np.testing.assert_allclose(tl.projectile(3.0), [10, 0, 0])
    np.testing.assert_allclose(tl.anchor("impact", 0.0), [10, 0, 0])


class EmissionTests(unittest.TestCase):
  def test_rate_accumulator_is_exact_over_a_second(self):
    e = emitter(active=[0.0, 1.0], rate=100.0, life=[10.0, 10.0])
    t = 0.0
    for _ in range(60):
      e.step(t, pv.DT, ORIGIN, STILL, None)
      t += pv.DT
    self.assertIn(e.spawned, (99, 100))

  def test_burst_fires_once(self):
    e = emitter(bursts=[[0.5, 37]], life=[10.0, 10.0])
    t = 0.0
    for _ in range(120):
      e.step(t, pv.DT, ORIGIN, STILL, None)
      t += pv.DT
    self.assertEqual(e.spawned, 37)

  def test_particles_die_after_life(self):
    e = emitter(bursts=[[0.0, 50]], life=[0.5, 0.5])
    t = 0.0
    for _ in range(45):  # 0.75 s
      e.step(t, pv.DT, ORIGIN, STILL, None)
      t += pv.DT
    self.assertEqual(e.alive_count, 0)
    self.assertEqual(e.max_alive, 50)

  def test_pool_exhaustion_is_counted_not_crashed(self):
    e = emitter(pool=16, bursts=[[0.0, 40]], life=[1.0, 1.0])
    e.step(0.0, pv.DT, ORIGIN, STILL, None)
    self.assertEqual(e.alive_count, 16)
    self.assertEqual(e.dropped, 24)


class ForceTests(unittest.TestCase):
  def test_gravity_pulls_velocity_down(self):
    e = emitter(bursts=[[0.0, 20]], forces={"gravity": -9.8}, life=[5, 5])
    e.step(0.0, pv.DT, ORIGIN, STILL, None)
    vy0 = e.vel[e.alive][:, 1].mean()
    for i in range(1, 30):
      e.step(i * pv.DT, pv.DT, ORIGIN, STILL, None)
    self.assertLess(e.vel[e.alive][:, 1].mean(), vy0 - 4.0)

  def test_attractor_with_zero_radius_pulls_inward(self):
    e = emitter(bursts=[[0.0, 50]], spawn={"radius": 2.0, "surface": True},
                forces={"attract": {"strength": 20, "radius": 0, "band": 0}}, life=[5, 5])
    e.step(0.0, pv.DT, ORIGIN, STILL, None)
    d0 = np.linalg.norm(e.pos[e.alive], axis=1).mean()
    for i in range(1, 30):
      e.step(i * pv.DT, pv.DT, ORIGIN, STILL, None)
    self.assertLess(np.linalg.norm(e.pos[e.alive], axis=1).mean(), d0 * 0.8)

  def test_band_attractor_keeps_particles_near_the_band(self):
    e = emitter(bursts=[[0.0, 200]], spawn={"radius": 3.0},
                forces={"attract": {"strength": 40, "radius": 1.0, "band": 0.5}, "drag": 5.0}, life=[5, 5])
    for i in range(120):
      e.step(i * pv.DT, pv.DT, ORIGIN, STILL, None)
    dist = np.linalg.norm(e.pos[e.alive], axis=1)
    self.assertGreater(float((np.abs(dist - 1.0) < 0.5).mean()), 0.9)

  def test_follow_moves_particles_with_anchor(self):
    e = emitter(bursts=[[0.0, 10]], follow=True, life=[5, 5])
    e.step(0.0, pv.DT, ORIGIN, STILL, None)
    before = e.pos[e.alive].copy()
    e.step(pv.DT, pv.DT, np.array([1.0, 0.0, 0.0], np.float32), STILL, None)
    np.testing.assert_allclose(e.pos[e.alive] - before, [[1.0, 0.0, 0.0]] * 10, atol=1e-6)

  def test_curl_changes_velocity_and_is_finite(self):
    vol = (np.random.default_rng(2).random((8, 8, 8, 4)).astype(np.float32) - 0.5).astype(np.float16)
    e = emitter(bursts=[[0.0, 30]], spawn={"radius": 1.0}, forces={"curl": 5.0, "curl_scale": 2.0}, life=[5, 5])
    e.step(0.0, pv.DT, ORIGIN, STILL, vol)
    e.step(pv.DT, pv.DT, ORIGIN, STILL, vol)
    v = e.vel[e.alive]
    self.assertTrue(np.isfinite(v).all())
    self.assertGreater(np.abs(v).max(), 0.0)


class VortexTests(unittest.TestCase):
  def test_ring_spawn_lies_on_ring_around_axis(self):
    e = emitter(bursts=[[0.0, 200]], life=[5, 5],
                spawn={"shape": "ring", "radius": 2.0, "axis": [1, 0, 0], "surface": True, "arms": 3, "jitter": 0.0})
    e.step(0.0, pv.DT, ORIGIN, STILL, None)
    p = e.pos[e.alive]
    np.testing.assert_allclose(p[:, 0], 0.0, atol=1e-5)          # no axial offset
    np.testing.assert_allclose(np.hypot(p[:, 1], p[:, 2]), 2.0, atol=1e-4)
    angles = np.mod(np.arctan2(p[:, 2], p[:, 1]), 2 * np.pi)
    self.assertLessEqual(len(np.unique(np.round(angles, 3))), 3)  # exactly the 3 arm angles

  def test_vortex_spin_builds_angular_momentum_and_pull_holds_radius(self):
    e = emitter(bursts=[[0.0, 300]], life=[5, 5], spawn={"shape": "ring", "radius": 3.0, "axis": [0, 1, 0], "jitter": 3.2},
                forces={"vortex": {"axis": [0, 1, 0], "spin": 8, "pull": 40, "radius": 1.0, "band": 0.3}, "drag": 0.5})
    for i in range(150):
      e.step(i * pv.DT, pv.DT, ORIGIN, STILL, None)
    p, v = e.pos[e.alive], e.vel[e.alive]
    radial = np.hypot(p[:, 0], p[:, 2])
    self.assertGreater(float((np.abs(radial - 1.0) < 0.4).mean()), 0.9)
    # angular momentum about +y: (p x v).y, right-hand rule -> positive
    angular = p[:, 2] * v[:, 0] - p[:, 0] * v[:, 2]
    self.assertGreater(float((angular > 0).mean()), 0.95)

  def test_vortex_axial_pushes_along_axis(self):
    e = emitter(bursts=[[0.0, 50]], life=[5, 5], spawn={"shape": "ring", "radius": 1.0, "axis": [1, 0, 0], "jitter": 3.2},
                forces={"vortex": {"axis": [1, 0, 0], "axial": 10}, "drag": 1.0})
    for i in range(60):
      e.step(i * pv.DT, pv.DT, ORIGIN, STILL, None)
    self.assertGreater(float(e.pos[e.alive][:, 0].mean()), 2.0)


class RingKindTests(unittest.TestCase):
  def ring(self, **ring):
    d = {"name": "r", "kind": "ring", "bursts": [[0.0, 1]], "life": [2.0, 2.0],
         "ring": dict({"axis": [1, 0, 0], "points": 90, "radius": 1.0}, **ring)}
    return pv.Emitter(pv.EmitterSpec.from_dict(d), seed=1)

  def test_full_ring_created_once_on_circle(self):
    e = self.ring()
    e.step(0.0, pv.DT, ORIGIN, STILL, None)
    self.assertEqual(e.alive_count, 90)
    p = e.pos[e.alive]
    np.testing.assert_allclose(p[:, 0], 0.0, atol=1e-6)
    np.testing.assert_allclose(np.hypot(p[:, 1], p[:, 2]), 1.0, atol=1e-5)
    e.step(pv.DT, pv.DT, ORIGIN, STILL, None)
    self.assertEqual(e.spawned, 90)  # not re-created

  def test_expand_rotate_and_move(self):
    e = self.ring(expand=2.0, omega=1.0, axial_velocity=3.0)
    for i in range(61):
      e.step(i * pv.DT, pv.DT, ORIGIN, STILL, None)
    p = e.pos[e.alive]
    age = 60 * pv.DT  # positions are written with the age at the start of the last step
    np.testing.assert_allclose(np.hypot(p[:, 1], p[:, 2]), 1.0 + 2.0 * age, atol=1e-3)
    np.testing.assert_allclose(p[:, 0], 3.0 * age, atol=1e-3)
    u, v = pv._basis(np.array([1.0, 0.0, 0.0], np.float32))
    first = e.pos[0]
    angle = float(np.arctan2(first @ v, first @ u)) % (2 * np.pi)
    self.assertAlmostEqual(angle, age % (2 * np.pi), places=3)

  def test_two_quarter_arcs_cover_two_opposite_spans(self):
    e = self.ring(arc=90, count=2, points=30)
    e.step(0.0, pv.DT, ORIGIN, STILL, None)
    p = e.pos[e.alive]
    u, v = pv._basis(np.array([1.0, 0.0, 0.0], np.float32))
    angles = np.degrees(np.mod(np.arctan2(p @ v, p @ u), 2 * np.pi))
    in_first = (angles >= -1e-3) & (angles <= 90 + 1e-3)
    in_second = (angles >= 180 - 1e-3) & (angles <= 270 + 1e-3)
    self.assertEqual(int(in_first.sum()), 30)
    self.assertEqual(int(in_second.sum()), 30)

  def test_wave_deforms_but_stays_bounded(self):
    e = self.ring(wave={"amplitude": 0.2, "frequency": 3, "speed": 0.0})
    e.step(0.0, pv.DT, ORIGIN, STILL, None)
    r = np.hypot(e.pos[e.alive][:, 1], e.pos[e.alive][:, 2])
    self.assertGreater(float(r.std()), 0.02)
    self.assertTrue(np.all((r >= 0.8 - 1e-4) & (r <= 1.2 + 1e-4)))

  def test_ring_dies_after_life(self):
    e = self.ring()
    for i in range(130):
      e.step(i * pv.DT, pv.DT, ORIGIN, STILL, None)
    self.assertEqual(e.alive_count, 0)


class RenderTests(unittest.TestCase):
  def test_premultiplied_layer_formula(self):
    r = pv.Renderer(64, 64, eye=(0, 0, 10), look_at=(0, 0, 0), fov_deg=40, bloom=0.0)
    hdr = np.full((64, 64, 3), 0.5, np.float32)
    rec = {
      "pos": np.array([[0.0, 0.0, 0.0]], np.float32), "vel": np.zeros((1, 3), np.float32),
      "rgb": np.array([[2.0, 0.0, 0.0]], np.float32), "alpha": np.array([1.0], np.float32),
      "size": np.array([0.5], np.float32), "shape": "disc", "additive": 0.0, "stretch": 0.0,
    }
    r.draw_layer(hdr, rec)
    centre = hdr[32, 32]
    # Alpha blend with a = 1 at the kernel peak replaces the destination.
    np.testing.assert_allclose(centre, [2.0, 0.0, 0.0], atol=0.05)
    self.assertAlmostEqual(float(hdr[0, 0, 0]), 0.5)  # untouched far away

    hdr2 = np.full((64, 64, 3), 0.5, np.float32)
    rec["additive"] = 1.0
    r.draw_layer(hdr2, rec)
    np.testing.assert_allclose(hdr2[32, 32], [2.5, 0.5, 0.5], atol=0.05)

  def test_overlapping_alpha_particles_converge_instead_of_summing(self):
    r = pv.Renderer(64, 64, eye=(0, 0, 10), look_at=(0, 0, 0), fov_deg=40, bloom=0.0)
    hdr = np.zeros((64, 64, 3), np.float32)
    n = 30
    rec = {
      "pos": np.zeros((n, 3), np.float32), "vel": np.zeros((n, 3), np.float32),
      "rgb": np.tile(np.array([[0.5, 0.5, 0.5]], np.float32), (n, 1)), "alpha": np.full(n, 0.3, np.float32),
      "size": np.full(n, 0.5, np.float32), "shape": "disc", "additive": 0.0, "stretch": 0.0,
    }
    r.draw_layer(hdr, rec)
    self.assertLessEqual(float(hdr[32, 32].max()), 0.5 + 1e-4)
    self.assertGreater(float(hdr[32, 32].max()), 0.4)

  def test_finish_returns_uint8_in_range(self):
    r = pv.Renderer(32, 32, eye=(0, 0, 10), look_at=(0, 0, 0))
    hdr = np.random.default_rng(0).random((32, 32, 3)).astype(np.float32) * 5
    out = r.finish(hdr)
    self.assertEqual(out.dtype, np.uint8)
    self.assertEqual(out.shape, (32, 32, 3))


class SpellTests(unittest.TestCase):
  def test_real_spec_runs_and_renders(self):
    p = pv.Previs(SPEC, 160, 96, seed=1)
    p.run(until=0.5)
    charge = p.render()
    self.assertGreater(int(charge.max()), 40)
    self.assertGreater(p.alive_total, 100)
    p.run(until=1.85)  # just after impact
    impact = p.render()
    self.assertGreater(int(impact.max()), 150)
    report = p.report()
    for name in ("core", "shell", "sparks", "impact_burst"):
      self.assertIn(f"| {name} |", report)

  def test_real_spec_pools_are_large_enough(self):
    p = pv.Previs(SPEC, 160, 96, seed=2)
    p.run()
    for e in p.emitters:
      self.assertEqual(e.dropped, 0, f"{e.spec.name} dropped {e.dropped} spawns; pool {e.spec.pool} too small")

  def test_force_vortex_spec_runs_without_dropping(self):
    p = pv.Previs(REPO / "assets" / "emitters" / "force_vortex.json", 160, 96, seed=4)
    p.run(until=1.0)
    self.assertGreater(int(p.render().max()), 60)
    p.run()
    for e in p.emitters:
      self.assertEqual(e.dropped, 0, f"{e.spec.name} dropped {e.dropped}; pool {e.spec.pool} too small")

  def test_everything_is_dead_at_the_end(self):
    p = pv.Previs(SPEC, 160, 96, seed=3)
    p.run(until=p.timeline.end + 3.5)
    self.assertEqual(p.alive_total, 0)


if __name__ == "__main__":
  unittest.main()
