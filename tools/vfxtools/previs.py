"""Spell previsualisation: a CPU reference of the particle logic.

Purpose: check that the emitter data (assets/emitters/*.json), the curve
atlas, the curl volume and the timeline compose into the spell described
in docs/roadmap.md, before any of it is written in HLSL. It deliberately
mirrors the GPU design at the level of *logic*, not plumbing:

  GPU (ADR-0005)                          here
  ------------------------------------    ------------------------------------
  per-emitter pool + dead list            fixed arrays per emitter, `alive` mask;
                                          spawn takes free slots, drops when full
  kickoff: spawnCount = rate*dt + carry   same accumulator, same burst rule
  spawn kernel                            Emitter._spawn
  simulate kernel (semi-implicit Euler,   Emitter.step, same force order as
    curl, attractor band, drag, age)        shaders/computeshader.hlsl
  curve atlas sampled by age/life         sample_curve (linear, CLAMP)
  curl volume, WRAP, emitter-local        sample_volume_wrap (trilinear, WRAP)
  premultiplied output (ADR-0006)         Renderer.draw_layer uses the same formula
  HDR target + bloom + tonemap (ADR-0003) float buffer, gaussian bloom, ACES

What it does not model: depth/soft particles (no scene), per-pixel noise
erosion (approximated per particle), exact sprite shapes, sorting, and
anything about performance. Treat its image as a layout check.

Conventions worth knowing because the HLSL must match them:

* Texel centres: texel i of an n-wide texture is at (i + 0.5) / n. The
  samplers here subtract 0.5 before flooring, like D3D linear filtering.
* Curve atlas: u = age / life, CLAMP; row v is metadata["rows"][name]["v"].
* Curl volume: uvw = (pos - anchor) / curl_scale + t * curl_scroll, WRAP.
* Premultiplied output: rgb * lerp(a, 1, additive), a * (1 - additive).
* Vortex force: `spin` is a target tangential speed relaxed at `spin_rate`,
  the centripetal term v_t^2 / r is applied explicitly, and `pull` is the
  band attractor toward the axis. Treating spin as a plain acceleration
  makes the orbit spiral outward (found by the first test run).
* Ring spawn: angle = phase_rate * t + arm * 2pi/arms + pitch * axial +
  jitter; radius = spawn_radius + cone * axial. Three arms with a phase
  rate and a pitch are what read as a tornado body.
* Ring kind (kinematic geometry): point i has a fixed theta_i; each frame
  pos = anchor + axis * (offset + axial_velocity * age)
      + dir(theta_i + omega * age) * (radius + expand * age) * (1 + amp * noise).
  This is vertex-shader logic on a ring mesh or ribbon, not a particle pool.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

from .rawtexture import read_raw_texture

DT = 1.0 / 60.0
KERNEL_RADII = np.array([1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 24.0, 32.0, 48.0, 64.0], dtype=np.float32)
SHAPES = ("disc", "mote", "ring", "streak")


# --------------------------------------------------------------------------
# Sampling helpers (the conventions the HLSL has to reproduce)
# --------------------------------------------------------------------------

def smoothstep(edge0, edge1, x):
  t = np.clip((x - edge0) / np.maximum(edge1 - edge0, 1e-8), 0.0, 1.0)
  return t * t * (3.0 - 2.0 * t)


def sample_volume_wrap(volume: np.ndarray, uvw: np.ndarray) -> np.ndarray:
  """Trilinear sample of a [d, h, w, c] volume with WRAP addressing."""
  depth, height, width, _ = volume.shape
  frac = uvw - np.floor(uvw)
  x = frac[..., 0] * width - 0.5
  y = frac[..., 1] * height - 0.5
  z = frac[..., 2] * depth - 0.5
  x0 = np.floor(x).astype(np.int64)
  y0 = np.floor(y).astype(np.int64)
  z0 = np.floor(z).astype(np.int64)
  fx = (x - x0)[..., None]
  fy = (y - y0)[..., None]
  fz = (z - z0)[..., None]

  def tap(dz, dy, dx):
    return volume[(z0 + dz) % depth, (y0 + dy) % height, (x0 + dx) % width].astype(np.float32)

  c00 = tap(0, 0, 0) * (1 - fx) + tap(0, 0, 1) * fx
  c01 = tap(0, 1, 0) * (1 - fx) + tap(0, 1, 1) * fx
  c10 = tap(1, 0, 0) * (1 - fx) + tap(1, 0, 1) * fx
  c11 = tap(1, 1, 0) * (1 - fx) + tap(1, 1, 1) * fx
  c0 = c00 * (1 - fy) + c01 * fy
  c1 = c10 * (1 - fy) + c11 * fy
  return c0 * (1 - fz) + c1 * fz


def sample_texture2d_wrap(texture: np.ndarray, uv: np.ndarray) -> np.ndarray:
  """Bilinear sample of a [h, w, c] texture with WRAP addressing."""
  height, width, _ = texture.shape
  frac = uv - np.floor(uv)
  x = frac[..., 0] * width - 0.5
  y = frac[..., 1] * height - 0.5
  x0 = np.floor(x).astype(np.int64)
  y0 = np.floor(y).astype(np.int64)
  fx = (x - x0)[..., None]
  fy = (y - y0)[..., None]

  def tap(dy, dx):
    return texture[(y0 + dy) % height, (x0 + dx) % width].astype(np.float32)

  top = tap(0, 0) * (1 - fx) + tap(0, 1) * fx
  bottom = tap(1, 0) * (1 - fx) + tap(1, 1) * fx
  return top * (1 - fy) + bottom * fy


def sample_curve(atlas: np.ndarray, row: int, u: np.ndarray) -> np.ndarray:
  """Linear sample of one atlas row with CLAMP addressing. Returns [..., 4]."""
  _, width, _ = atlas.shape
  x = np.clip(np.asarray(u, dtype=np.float32), 0.0, 1.0) * width - 0.5
  x = np.clip(x, 0.0, width - 1.0)
  x0 = np.floor(x).astype(np.int64)
  x1 = np.minimum(x0 + 1, width - 1)
  f = (x - x0)[..., None]
  line = atlas[row].astype(np.float32)
  return line[x0] * (1 - f) + line[x1] * f


# --------------------------------------------------------------------------
# Spec
# --------------------------------------------------------------------------

def _unit(v) -> np.ndarray:
  a = np.asarray(v, np.float32)
  n = float(np.linalg.norm(a))
  return a / n if n > 1e-8 else np.array([1.0, 0.0, 0.0], np.float32)


def _basis(axis: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
  """Two unit vectors perpendicular to `axis` (Duff et al. 2017)."""
  x, y, z = (float(c) for c in axis)
  sign = math.copysign(1.0, z)
  a = -1.0 / (sign + z)
  b = x * y * a
  u = np.array([1.0 + sign * x * x * a, sign * b, -sign * x], np.float32)
  v = np.array([b, sign + y * y * a, -y], np.float32)
  return u, v


def _pair(value, default):
  if value is None:
    return tuple(default)
  if isinstance(value, (int, float)):
    return (float(value), float(value))
  return (float(value[0]), float(value[1]))


@dataclass
class EmitterSpec:
  """One emitter. This is the CPU twin of the EmitterParams row the D3D12
  side will upload (ADR-0005); keep the field names in sync when that
  struct is written."""
  name: str
  kind: str = "particles"          # particles | ring (kinematic ring/arc geometry, see below)
  attach: str = "projectile"       # projectile | origin | impact
  follow: bool = False             # particles move with the anchor (attached core/shell)
  # kind == "ring": `points` per copy, `count` copies spread evenly, each an
  # `arc` (degrees, 360 = full ring). Created once at the first burst time,
  # then driven kinematically: theta_i + omega*age around spawn_axis, radius
  # + expand*age, radial wave from the noise pack, axial offset + velocity.
  # On the GPU this is a ring mesh / ribbon with a vertex shader, not a
  # particle pool; the previs uses points so it can share the renderer.
  ring_points: int = 120
  ring_arc: float = 360.0
  ring_count: int = 1
  ring_phase: float = 0.0          # degrees, start angle of copy 0
  ring_radius: float = 1.0
  ring_expand: float = 0.0         # radius growth, u/s
  ring_omega: float = 0.0          # rad/s
  ring_wave_amp: float = 0.0       # radial displacement as a fraction of radius
  ring_wave_freq: float = 3.0      # cycles around the ring
  ring_wave_speed: float = 0.5     # noise scroll, cycles/s
  ring_axial_offset: float = 0.0
  ring_axial_velocity: float = 0.0
  active: tuple = (0.0, 0.0)       # [start, end) window for rate emission
  rate: float = 0.0                # particles per second inside the window
  bursts: list = field(default_factory=list)  # [[time, count], ...]
  pool: int = 256
  life: tuple = (1.0, 1.0)
  spawn_shape: str = "sphere"      # sphere | ring (ring: around spawn_axis, coherent arms)
  spawn_radius: float = 0.0
  spawn_surface: bool = False
  spawn_axis: tuple = (1.0, 0.0, 0.0)
  spawn_arms: int = 1              # ring: number of spiral arms
  spawn_phase_rate: float = 0.0    # ring: rad/s the arm angle advances (makes a helix over time)
  spawn_jitter: float = 0.0        # ring: random angle spread (rad)
  spawn_axial_range: tuple = (0.0, 0.0)  # ring: offset along the axis
  spawn_cone: float = 0.0          # ring: radius grows by this per unit of axial offset
  spawn_pitch: float = 0.0         # ring: angle advances by this (rad) per unit of axial offset (helix)
  vel_radial: tuple = (0.0, 0.0)   # along spawn direction, away from anchor
  vel_inward: tuple = (0.0, 0.0)   # along spawn direction, toward anchor
  vel_tangent: tuple = (0.0, 0.0)  # ring: around the axis (right-hand rule)
  vel_direction: tuple = (0.0, 0.0, 0.0)
  vel_spread: float = 0.0          # random isotropic speed
  vel_inherit: float = 0.0         # fraction of anchor velocity
  gravity: float = 0.0
  drag: float = 0.0                # 1/s, vel *= 1 - saturate(drag*dt)
  curl_strength: float = 0.0
  curl_scale: float = 4.0          # world units per volume tile
  curl_scroll: float = 0.0         # tiles per second of uvw drift (animates the field)
  attract_strength: float = 0.0
  attract_radius: float = 0.0      # band centre; 0 = pull to the anchor
  attract_band: float = 0.0
  vortex_axis: tuple = (1.0, 0.0, 0.0)   # vortex: axis through the anchor
  vortex_spin: float = 0.0         # target tangential speed (u/s)
  vortex_spin_rate: float = 8.0    # 1/s, how fast the tangential speed relaxes to spin
  vortex_pull: float = 0.0         # band attractor toward the axis (same rule as attract)
  vortex_radius: float = 0.0
  vortex_band: float = 0.0
  vortex_cone: float = 0.0         # band radius grows by this per unit of axial position
  vortex_axial: float = 0.0        # acceleration along the axis
  size: float = 0.1                # base radius, world units
  color_curve: str | None = None
  size_curve: str | None = None
  additive: float = 1.0            # 1 additive, 0 alpha blend, between = mix (ADR-0006)
  shape: str = "disc"
  erosion: float = 0.0             # 0..1, noise dissolve strength over life
  twinkle: float = 0.0             # Hz, alpha flicker
  stretch: float = 0.0             # seconds of velocity to stretch streaks by
  angvel: tuple = (0.0, 0.0)

  @classmethod
  def from_dict(cls, d: dict) -> "EmitterSpec":
    spawn = d.get("spawn", {})
    vel = d.get("velocity", {})
    forces = d.get("forces", {})
    attract = forces.get("attract", {})
    vortex = forces.get("vortex", {})
    curves = d.get("curves", {})
    ring = d.get("ring", {})
    wave = ring.get("wave", {})
    kind = d.get("kind", "particles")
    pool = int(d.get("pool", 256))
    if kind == "ring":
      pool = int(ring.get("points", 120)) * int(ring.get("count", 1))
      spawn = dict(spawn, axis=ring.get("axis", spawn.get("axis", (1.0, 0.0, 0.0))))
    spec = cls(
      name=d["name"],
      kind=kind,
      ring_points=int(ring.get("points", 120)),
      ring_arc=float(ring.get("arc", 360.0)),
      ring_count=int(ring.get("count", 1)),
      ring_phase=float(ring.get("phase", 0.0)),
      ring_radius=float(ring.get("radius", 1.0)),
      ring_expand=float(ring.get("expand", 0.0)),
      ring_omega=float(ring.get("omega", 0.0)),
      ring_wave_amp=float(wave.get("amplitude", 0.0)),
      ring_wave_freq=float(wave.get("frequency", 3.0)),
      ring_wave_speed=float(wave.get("speed", 0.5)),
      ring_axial_offset=float(ring.get("axial_offset", 0.0)),
      ring_axial_velocity=float(ring.get("axial_velocity", 0.0)),
      attach=d.get("attach", "projectile"),
      follow=bool(d.get("follow", False)),
      active=_pair(d.get("active"), (0.0, 0.0)),
      rate=float(d.get("rate", 0.0)),
      bursts=[[float(t), int(n)] for t, n in d.get("bursts", [])],
      pool=pool,
      life=_pair(d.get("life"), (1.0, 1.0)),
      spawn_shape=spawn.get("shape", "sphere"),
      spawn_radius=float(spawn.get("radius", 0.0)),
      spawn_surface=bool(spawn.get("surface", False)),
      spawn_axis=tuple(float(v) for v in spawn.get("axis", (1.0, 0.0, 0.0))),
      spawn_arms=int(spawn.get("arms", 1)),
      spawn_phase_rate=float(spawn.get("phase_rate", 0.0)),
      spawn_jitter=float(spawn.get("jitter", 0.0)),
      spawn_axial_range=_pair(spawn.get("axial_range"), (0.0, 0.0)),
      spawn_cone=float(spawn.get("cone", 0.0)),
      spawn_pitch=float(spawn.get("pitch", 0.0)),
      vel_radial=_pair(vel.get("radial"), (0.0, 0.0)),
      vel_inward=_pair(vel.get("inward"), (0.0, 0.0)),
      vel_tangent=_pair(vel.get("tangent"), (0.0, 0.0)),
      vel_direction=tuple(float(v) for v in vel.get("direction", (0.0, 0.0, 0.0))),
      vel_spread=float(vel.get("spread", 0.0)),
      vel_inherit=float(vel.get("inherit", 0.0)),
      gravity=float(forces.get("gravity", 0.0)),
      drag=float(forces.get("drag", 0.0)),
      curl_strength=float(forces.get("curl", 0.0)),
      curl_scale=float(forces.get("curl_scale", 4.0)),
      curl_scroll=float(forces.get("curl_scroll", 0.0)),
      attract_strength=float(attract.get("strength", 0.0)),
      attract_radius=float(attract.get("radius", 0.0)),
      attract_band=float(attract.get("band", 0.0)),
      vortex_axis=tuple(float(v) for v in vortex.get("axis", (1.0, 0.0, 0.0))),
      vortex_spin=float(vortex.get("spin", 0.0)),
      vortex_spin_rate=float(vortex.get("spin_rate", 8.0)),
      vortex_pull=float(vortex.get("pull", 0.0)),
      vortex_radius=float(vortex.get("radius", 0.0)),
      vortex_band=float(vortex.get("band", 0.0)),
      vortex_cone=float(vortex.get("cone", 0.0)),
      vortex_axial=float(vortex.get("axial", 0.0)),
      size=float(d.get("size", 0.1)),
      color_curve=curves.get("color"),
      size_curve=curves.get("size"),
      additive=float(d.get("additive", 1.0)),
      shape=d.get("shape", "disc"),
      erosion=float(d.get("erosion", 0.0)),
      twinkle=float(d.get("twinkle", 0.0)),
      stretch=float(d.get("stretch", 0.0)),
      angvel=_pair(d.get("angvel"), (0.0, 0.0)),
    )
    if spec.shape not in SHAPES:
      raise ValueError(f"{spec.name}: unknown shape {spec.shape!r}")
    if spec.spawn_shape not in ("sphere", "ring"):
      raise ValueError(f"{spec.name}: unknown spawn shape {spec.spawn_shape!r}")
    if spec.kind not in ("particles", "ring"):
      raise ValueError(f"{spec.name}: unknown kind {spec.kind!r}")
    if spec.kind == "ring" and not spec.bursts:
      raise ValueError(f"{spec.name}: a ring needs one burst time (when it is created)")
    if spec.attach not in ("projectile", "origin", "impact"):
      raise ValueError(f"{spec.name}: unknown attach {spec.attach!r}")
    return spec


class Timeline:
  """Charge at `start`, fly to `target` between charge_end and impact,
  then stay. The CPU-side spell sequencer will own exactly this."""

  def __init__(self, d: dict):
    self.start = np.array(d.get("start", (0, 0, 0)), dtype=np.float32)
    self.target = np.array(d.get("target", (10, 0, 0)), dtype=np.float32)
    self.charge_end = float(d.get("charge_end", 0.8))
    self.impact = float(d.get("impact", 1.8))
    self.end = float(d.get("end", 4.0))

  def projectile(self, t: float) -> np.ndarray:
    if t <= self.charge_end:
      return self.start.copy()
    if t >= self.impact:
      return self.target.copy()
    f = (t - self.charge_end) / (self.impact - self.charge_end)
    return self.start + (self.target - self.start) * f

  def anchor(self, attach: str, t: float) -> np.ndarray:
    if attach == "projectile":
      return self.projectile(t)
    if attach == "impact":
      return self.target.copy()
    return self.start.copy()


# --------------------------------------------------------------------------
# Emitter simulation
# --------------------------------------------------------------------------

class Emitter:
  def __init__(self, spec: EmitterSpec, seed: int):
    self.spec = spec
    self.rng = np.random.default_rng(seed)
    n = spec.pool
    self.pos = np.zeros((n, 3), np.float32)
    self.vel = np.zeros((n, 3), np.float32)
    self.age = np.zeros(n, np.float32)
    self.life = np.ones(n, np.float32)
    self.seed = np.zeros(n, np.float32)
    self.rot = np.zeros(n, np.float32)
    self.angvel = np.zeros(n, np.float32)
    self.alive = np.zeros(n, bool)
    self.theta = np.zeros(n, np.float32)  # ring kind: parametric angle at creation
    self.noise2d: np.ndarray | None = None  # set by Previs; ring wave samples it
    self.carry = 0.0
    self.fired = [False] * len(spec.bursts)
    self.prev_anchor: np.ndarray | None = None
    # Stats the report uses to size the GPU pools.
    self.spawned = 0
    self.dropped = 0
    self.max_alive = 0

  @property
  def alive_count(self) -> int:
    return int(self.alive.sum())

  def _uniform(self, pair: tuple, n: int) -> np.ndarray:
    return self.rng.uniform(pair[0], pair[1], n).astype(np.float32)

  def _spawn(self, count: int, t: float, anchor: np.ndarray, anchor_vel: np.ndarray) -> None:
    free = np.flatnonzero(~self.alive)
    n = min(count, free.size)
    self.dropped += count - n
    if n == 0:
      return
    idx = free[:n]
    s = self.spec

    if s.spawn_shape == "ring":
      # Points on a ring around spawn_axis. With arms > 1 and a phase rate,
      # successive spawns trace `arms` helical strands: that is what reads
      # as a tornado/vortex body once the vortex force keeps them orbiting.
      axis = _unit(s.spawn_axis)
      u, v = _basis(axis)
      arm = self.rng.integers(0, max(s.spawn_arms, 1), n)
      axial = self._uniform(s.spawn_axial_range, n)
      angle = (s.spawn_phase_rate * t + arm * (2 * math.pi / max(s.spawn_arms, 1))
               + s.spawn_pitch * axial
               + self.rng.normal(size=n) * s.spawn_jitter).astype(np.float32)
      directions = np.cos(angle)[:, None] * u + np.sin(angle)[:, None] * v
      radius = np.full(n, s.spawn_radius, np.float32) + s.spawn_cone * axial
      if not s.spawn_surface:
        radius *= np.sqrt(self.rng.random(n, dtype=np.float32))
      self.pos[idx] = anchor + directions * radius[:, None] + axis * axial[:, None]
      tangent = np.cross(axis, directions)
    else:
      directions = self.rng.normal(size=(n, 3)).astype(np.float32)
      norms = np.linalg.norm(directions, axis=1, keepdims=True)
      norms[norms < 1e-6] = 1.0
      directions /= norms
      if s.spawn_surface:
        radius = np.full(n, s.spawn_radius, np.float32)
      else:
        radius = s.spawn_radius * np.cbrt(self.rng.random(n, dtype=np.float32))
      self.pos[idx] = anchor + directions * radius[:, None]
      tangent = None

    speed = self._uniform(s.vel_radial, n) - self._uniform(s.vel_inward, n)
    velocity = directions * speed[:, None]
    if tangent is not None and (s.vel_tangent[0] != 0.0 or s.vel_tangent[1] != 0.0):
      velocity += tangent * self._uniform(s.vel_tangent, n)[:, None]
    velocity += np.asarray(s.vel_direction, np.float32)
    if s.vel_spread > 0:
      velocity += self.rng.normal(size=(n, 3)).astype(np.float32) * s.vel_spread
    velocity += anchor_vel * s.vel_inherit
    self.vel[idx] = velocity

    self.age[idx] = 0.0
    self.life[idx] = np.maximum(self._uniform(s.life, n), 1e-3)
    self.seed[idx] = self.rng.random(n, dtype=np.float32)
    self.rot[idx] = self.rng.random(n, dtype=np.float32) * (2 * math.pi)
    self.angvel[idx] = self._uniform(s.angvel, n)
    self.alive[idx] = True
    self.spawned += n

  def _create_ring(self) -> None:
    s = self.spec
    full = s.ring_arc >= 360.0 - 1e-3
    arc = math.radians(s.ring_arc)
    thetas = []
    for copy in range(max(s.ring_count, 1)):
      start = math.radians(s.ring_phase) + copy * (2 * math.pi / max(s.ring_count, 1))
      thetas.append(start + np.linspace(0.0, arc, s.ring_points, endpoint=not full, dtype=np.float32))
    theta = np.concatenate(thetas)
    n = theta.size
    idx = np.arange(n)
    self.theta[idx] = theta
    self.age[idx] = 0.0
    self.life[idx] = max(s.life[0], 1e-3)  # one life for the whole ring: it fades as a unit
    self.seed[idx] = (theta / (2 * math.pi)) % 1.0
    self.rot[idx] = 0.0
    self.angvel[idx] = 0.0
    self.alive[idx] = True
    self.spawned += n

  def _step_ring(self, t: float, dt: float, anchor: np.ndarray) -> None:
    s = self.spec
    for i, (burst_time, _) in enumerate(s.bursts):
      if not self.fired[i] and t >= burst_time:
        self.fired[i] = True
        if not self.alive.any():
          self._create_ring()
    alive = self.alive
    if alive.any():
      age = self.age[alive]
      axis = _unit(s.spawn_axis)
      u, v = _basis(axis)
      theta = self.theta[alive] + s.ring_omega * age
      radius = s.ring_radius + s.ring_expand * age
      if s.ring_wave_amp != 0.0:
        phase = theta * (s.ring_wave_freq / (2 * math.pi)) + s.ring_wave_speed * t
        if self.noise2d is not None:
          uv = np.stack([phase, np.full_like(phase, 0.37)], axis=-1)
          noise = sample_texture2d_wrap(self.noise2d, uv)[:, 0] * 2.0 - 1.0
        else:
          noise = np.sin(phase * 2 * math.pi)
        radius = radius * (1.0 + s.ring_wave_amp * noise)
      direction = np.cos(theta)[:, None] * u + np.sin(theta)[:, None] * v
      axial = s.ring_axial_offset + s.ring_axial_velocity * age
      self.pos[alive] = anchor + axis * axial[:, None] + direction * radius[:, None]
      tangent = np.cross(axis, direction)
      self.vel[alive] = tangent * (s.ring_omega * radius)[:, None] + axis * s.ring_axial_velocity
    self.age[alive] += dt
    self.alive &= self.age < self.life
    self.max_alive = max(self.max_alive, self.alive_count)

  def step(self, t: float, dt: float, anchor: np.ndarray, anchor_vel: np.ndarray,
           curl_volume: np.ndarray | None) -> None:
    s = self.spec
    if s.kind == "ring":
      self._step_ring(t, dt, anchor)
      return

    # Emission: the kickoff kernel's spawnCount = floor(rate*dt + carry),
    # plus bursts that fire once when their time is reached.
    count = 0
    if s.rate > 0 and s.active[0] <= t < s.active[1]:
      self.carry += s.rate * dt
      count = int(self.carry)
      self.carry -= count
    for i, (burst_time, burst_count) in enumerate(s.bursts):
      if not self.fired[i] and t >= burst_time:
        self.fired[i] = True
        count += burst_count
    if count > 0:
      self._spawn(count, t, anchor, anchor_vel)

    # Attached emitters: particles ride along with the anchor.
    if s.follow and self.prev_anchor is not None:
      self.pos[self.alive] += anchor - self.prev_anchor
    self.prev_anchor = anchor.copy()

    alive = self.alive
    if alive.any():
      pos = self.pos[alive]
      vel = self.vel[alive]

      if s.curl_strength != 0.0 and curl_volume is not None:
        uvw = (pos - anchor) / s.curl_scale + t * s.curl_scroll
        curl = sample_volume_wrap(curl_volume, uvw)[:, :3]
        vel += curl * (s.curl_strength * dt)

      if s.gravity != 0.0:
        vel[:, 1] += s.gravity * dt

      if s.attract_strength != 0.0:
        # Same band rule as shaders/computeshader.hlsl: outside the band
        # pull in, inside push out, error grows with distance to the band.
        to_center = anchor - pos
        dist = np.linalg.norm(to_center, axis=1)
        direction = np.where(dist[:, None] > 1e-5, to_center / np.maximum(dist, 1e-5)[:, None], 0.0)
        inner = s.attract_radius - 0.5 * s.attract_band
        outer = s.attract_radius + 0.5 * s.attract_band
        error = np.where(dist > outer, dist - outer, dist - inner)
        vel += direction * (error * s.attract_strength * dt)[:, None]

      if s.vortex_spin != 0.0 or s.vortex_pull != 0.0 or s.vortex_axial != 0.0:
        # Vortex around an axis through the anchor.
        #  spin   : target tangential speed (u/s), relaxed toward at spin_rate.
        #  pull   : band attractor toward the axis (radius may grow along the
        #           axis for a cone), same rule as the sphere attractor.
        #  axial  : acceleration along the axis.
        # The centripetal term v_t^2 / r is applied explicitly: a particle
        # orbiting at 10 u/s at r = 1 needs 100 u/s^2 inward, far more than
        # any sane band pull. Without it the orbit spirals outward and the
        # vortex is several times wider than its radius parameter (this is
        # what the first test run showed). The HLSL must do the same.
        axis = _unit(s.vortex_axis)
        rel = pos - anchor
        axial = rel @ axis
        radial_vec = rel - axial[:, None] * axis
        dist = np.linalg.norm(radial_vec, axis=1)
        radial_dir = radial_vec / np.maximum(dist, 1e-5)[:, None]
        tangent = np.cross(axis, radial_dir)
        if s.vortex_spin != 0.0:
          v_t = np.einsum("ij,ij->i", vel, tangent)
          relax = min(s.vortex_spin_rate * dt, 1.0)
          vel += tangent * ((s.vortex_spin - v_t) * relax)[:, None]
          v_t = np.einsum("ij,ij->i", vel, tangent)
          centripetal = v_t * v_t / np.maximum(dist, 0.25 * max(s.vortex_radius, 0.2))
          vel -= radial_dir * (centripetal * dt)[:, None]
        if s.vortex_pull != 0.0:
          centre = s.vortex_radius + s.vortex_cone * axial
          inner = centre - 0.5 * s.vortex_band
          outer = centre + 0.5 * s.vortex_band
          error = np.where(dist > outer, dist - outer, dist - inner)
          vel -= radial_dir * (error * s.vortex_pull * dt)[:, None]
        if s.vortex_axial != 0.0:
          vel += axis * (s.vortex_axial * dt)

      if s.drag != 0.0:
        vel *= 1.0 - min(max(s.drag * dt, 0.0), 1.0)

      pos += vel * dt  # semi-implicit Euler: velocity first, then position
      self.pos[alive] = pos
      self.vel[alive] = vel
      self.rot[alive] += self.angvel[alive] * dt

    self.age[alive] += dt
    self.alive &= self.age < self.life
    self.max_alive = max(self.max_alive, self.alive_count)

  def render_records(self, t: float, atlas: np.ndarray, rows: dict, noise2d: np.ndarray | None) -> dict | None:
    """What the simulate kernel writes into the render buffer, per particle."""
    alive = self.alive
    if not alive.any():
      return None
    s = self.spec
    u = self.age[alive] / self.life[alive]
    seed = self.seed[alive]

    if s.color_curve:
      color = sample_curve(atlas, rows[s.color_curve]["row"], u)
      rgb, alpha = color[:, :3], color[:, 3]
    else:
      rgb = np.ones((u.size, 3), np.float32)
      alpha = np.ones(u.size, np.float32)
    size = np.full(u.size, s.size, np.float32)
    if s.size_curve:
      size *= sample_curve(atlas, rows[s.size_curve]["row"], u)[:, 0]

    if s.erosion > 0 and noise2d is not None:
      # Per-particle stand-in for per-pixel erosion: one noise value per
      # particle drifting slowly, thresholded by age. The real shader does
      # this per pixel with the same smoothstep.
      uv = np.stack([seed * 7.31 + t * 0.02, seed * 3.17], axis=-1)
      noise = sample_texture2d_wrap(noise2d, uv)[:, 0]
      soft = 0.25
      threshold = u * (1.0 + soft) - soft
      eroded = smoothstep(threshold, threshold + soft, noise)
      alpha = alpha * (1.0 - s.erosion + s.erosion * eroded)

    if s.twinkle > 0:
      alpha = alpha * (0.55 + 0.45 * np.sin(2 * math.pi * (s.twinkle * t + seed)))

    return {
      "pos": self.pos[alive],
      "vel": self.vel[alive],
      "rgb": rgb.astype(np.float32),
      "alpha": alpha.astype(np.float32),
      "size": size,
      "shape": s.shape,
      "additive": s.additive,
      "stretch": s.stretch,
    }


# --------------------------------------------------------------------------
# Renderer: splats into an HDR buffer, bloom, tonemap
# --------------------------------------------------------------------------

class Renderer:
  def __init__(self, width: int, height: int, eye, look_at, fov_deg: float = 32.0,
               bloom: float = 0.5, exposure: float = 1.0):
    if width % 4 or height % 4:
      raise ValueError("width and height must be multiples of 4 (quarter-res bloom)")
    self.width, self.height = width, height
    self.eye = np.array(eye, np.float32)
    forward = np.array(look_at, np.float32) - self.eye
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, np.array([0, 1, 0], np.float32))
    right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    self.basis = np.stack([right, up, -forward])  # camera space, RH, looks down -z
    self.focal = (height / 2) / math.tan(math.radians(fov_deg) / 2)
    self.bloom = bloom
    self.exposure = exposure
    self._kernels: dict[tuple, tuple] = {}

  def project(self, pos: np.ndarray):
    cam = (pos - self.eye) @ self.basis.T
    depth = -cam[:, 2]
    safe = np.maximum(depth, 1e-3)
    sx = self.width / 2 + self.focal * cam[:, 0] / safe
    sy = self.height / 2 - self.focal * cam[:, 1] / safe
    return sx, sy, depth

  def _kernel(self, shape: str, radius: float):
    key = (shape, float(radius))
    if key in self._kernels:
      return self._kernels[key]
    r = int(math.ceil(radius))
    ys, xs = np.mgrid[-r:r + 1, -r:r + 1]
    q = np.sqrt(xs * xs + ys * ys) / max(radius, 1.0)
    if shape == "mote":
      w = np.clip(1.0 - q * q, 0.0, 1.0) ** 3
    elif shape == "ring":
      w = np.exp(-((q - 0.8) / 0.15) ** 2) * (q < 1.1)
    else:  # disc, streak
      w = np.clip(1.0 - q * q, 0.0, 1.0) ** 2
    keep = w > 1e-3
    kernel = (xs[keep].astype(np.int64), ys[keep].astype(np.int64), w[keep].astype(np.float32))
    self._kernels[key] = kernel
    return kernel

  def draw_layer(self, hdr: np.ndarray, rec: dict) -> None:
    """Splat one emitter's particles as a layer and composite it with the
    premultiplied formula from ADR-0006. Within a layer, contributions add
    and alpha saturates (no per-particle ordering); layers composite in
    emitter order."""
    sx, sy, depth = self.project(rec["pos"])
    visible = depth > 0.1
    if not visible.any():
      return
    sx, sy, depth = sx[visible], sy[visible], depth[visible]
    rgb, alpha, size = rec["rgb"][visible], rec["alpha"][visible], rec["size"][visible]
    additive = rec["additive"]
    # ADR-0006 premultiplied output: rgb * lerp(a, 1, additive), a * (1 - additive).
    # Kept as two parts here so the alpha share can be normalised by
    # coverage (see below) while the additive share keeps summing.
    src_add = rgb * additive
    src_rgb = rgb * (alpha * (1.0 - additive))[:, None]
    src_a = alpha * (1.0 - additive)
    radius_px = size * self.focal / depth

    # Streaks: several sub-splats along the screen-space velocity.
    if rec["shape"] == "streak" and rec["stretch"] > 0:
      ex, ey, _ = self.project(rec["pos"][visible] + rec["vel"][visible] * rec["stretch"])
      dx, dy = ex - sx, ey - sy
      length = np.hypot(dx, dy)
      k = 6
      spacing = length / (k - 1)
      overlap = np.maximum(1.0, np.minimum(k, 2.0 * np.maximum(radius_px, 1.0) / np.maximum(spacing, 1e-3)))
      f = (np.arange(k, dtype=np.float32) / (k - 1) - 0.5)[None, :]
      sx = (sx[:, None] + dx[:, None] * f).ravel()
      sy = (sy[:, None] + dy[:, None] * f).ravel()
      weight = np.repeat(1.0 / overlap, k).astype(np.float32)
      src_add = np.repeat(src_add, k, axis=0)
      src_rgb = np.repeat(src_rgb, k, axis=0)
      src_a = np.repeat(src_a, k)
      radius_px = np.repeat(radius_px, k)
      src_add *= weight[:, None]
      src_rgb *= weight[:, None]
      src_a *= weight

    # Sub-pixel particles: scale by coverage instead of drawing a full pixel.
    coverage = np.clip(radius_px, 0.0, 1.0) ** 2
    src_add = src_add * coverage[:, None]
    src_rgb = src_rgb * coverage[:, None]
    src_a = src_a * coverage
    radius_px = np.clip(radius_px, KERNEL_RADII[0], KERNEL_RADII[-1])
    bucket = np.searchsorted(KERNEL_RADII, radius_px)
    bucket = np.minimum(bucket, len(KERNEL_RADII) - 1)

    w, h = self.width, self.height
    layer_add = np.zeros((h * w, 3), np.float32)
    layer_rgb = np.zeros((h * w, 3), np.float32)
    layer_a = np.zeros(h * w, np.float32)
    px_all = np.rint(sx).astype(np.int64)
    py_all = np.rint(sy).astype(np.int64)
    shape = "disc" if rec["shape"] == "streak" else rec["shape"]

    for b in np.unique(bucket):
      sel = bucket == b
      kx, ky, kw = self._kernel(shape, float(KERNEL_RADII[b]))
      px = px_all[sel][:, None] + kx[None, :]
      py = py_all[sel][:, None] + ky[None, :]
      valid = (px >= 0) & (px < w) & (py >= 0) & (py < h)
      flat = (py * w + px)[valid]
      weights = np.broadcast_to(kw[None, :], px.shape)[valid]
      if additive > 0.0:
        for c in range(3):
          contrib = (src_add[sel][:, c][:, None] * kw[None, :])[valid]
          layer_add[:, c] += np.bincount(flat, weights=contrib, minlength=h * w)
      if additive < 1.0:
        for c in range(3):
          contrib = (src_rgb[sel][:, c][:, None] * kw[None, :])[valid]
          layer_rgb[:, c] += np.bincount(flat, weights=contrib, minlength=h * w)
        contrib_a = (src_a[sel][:, None] * kw[None, :])[valid]
        layer_a += np.bincount(flat, weights=contrib_a, minlength=h * w)
      del weights

    if additive < 1.0:
      # Order-independent stand-in for per-particle "over": where several
      # alpha particles overlap, coverage saturates at 1 and the colour is
      # their coverage-weighted average (true over-blending converges to
      # the same place). Without this, 30 overlapping puffs at a = 0.3 would
      # sum to 9x their colour and read as white.
      layer_a = layer_a.reshape(h, w, 1)
      layer_rgb = layer_rgb.reshape(h, w, 3)
      layer_rgb = np.where(layer_a > 1.0, layer_rgb / np.maximum(layer_a, 1.0), layer_rgb)
      hdr *= 1.0 - np.minimum(layer_a, 1.0)
      hdr += layer_rgb
    if additive > 0.0:
      hdr += layer_add.reshape(h, w, 3)

  @staticmethod
  def _blur(img: np.ndarray, sigma: float) -> np.ndarray:
    r = max(1, int(3 * sigma))
    k = np.exp(-(np.arange(-r, r + 1, dtype=np.float32) ** 2) / (2 * sigma * sigma))
    k /= k.sum()
    out = np.zeros_like(img)
    padded = np.pad(img, ((r, r), (0, 0), (0, 0)), mode="edge")
    for i, kv in enumerate(k):
      out += kv * padded[i:i + img.shape[0]]
    img2 = np.zeros_like(out)
    padded = np.pad(out, ((0, 0), (r, r), (0, 0)), mode="edge")
    for i, kv in enumerate(k):
      img2 += kv * padded[:, i:i + img.shape[1]]
    return img2

  def finish(self, hdr: np.ndarray) -> np.ndarray:
    """Bloom (quarter res, soft threshold) + ACES tonemap + gamma -> uint8."""
    h, w, _ = hdr.shape
    if self.bloom > 0:
      quarter = hdr.reshape(h // 4, 4, w // 4, 4, 3).mean(axis=(1, 3))
      luminance = quarter @ np.array([0.2126, 0.7152, 0.0722], np.float32)
      source = quarter * smoothstep(0.6, 2.0, luminance)[..., None]
      glow = self._blur(source, 1.5) + 0.6 * self._blur(source, 4.0)
      hdr = hdr + self.bloom * np.repeat(np.repeat(glow, 4, axis=0), 4, axis=1)
    x = np.maximum(hdr * self.exposure, 0.0)
    mapped = (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14)
    mapped = np.clip(mapped, 0.0, 1.0) ** (1.0 / 2.2)
    return (mapped * 255.0 + 0.5).astype(np.uint8)


# --------------------------------------------------------------------------
# The whole spell
# --------------------------------------------------------------------------

class Previs:
  def __init__(self, spec_path: Path, width: int = 640, height: int = 360, seed: int = 1):
    spec_path = Path(spec_path)
    spec = json.loads(spec_path.read_text(encoding="utf-8"))
    base = spec_path.parent
    self.spec = spec
    self.timeline = Timeline(spec.get("timeline", {}))

    atlas, atlas_meta = read_raw_texture(base / spec["curve_atlas"])
    self.atlas = atlas.astype(np.float32)
    self.rows = atlas_meta["rows"]
    self.curl_volume = None
    if spec.get("curl_noise"):
      self.curl_volume, _ = read_raw_texture(base / spec["curl_noise"])
    self.noise2d = None
    if spec.get("noise_pack"):
      noise, _ = read_raw_texture(base / spec["noise_pack"])
      self.noise2d = noise.astype(np.float32) / 255.0

    self.emitters = [Emitter(EmitterSpec.from_dict(d), seed * 1000 + i) for i, d in enumerate(spec["emitters"])]
    for e in self.emitters:
      e.noise2d = self.noise2d
      for curve in (e.spec.color_curve, e.spec.size_curve):
        if curve and curve not in self.rows:
          raise KeyError(f"{e.spec.name}: curve {curve!r} not in atlas rows")

    cam = spec.get("camera", {})
    render = spec.get("render", {})
    self.renderer = Renderer(width, height, cam.get("eye", (7, 2, 19)), cam.get("look_at", (7, 0.2, 0)),
                             cam.get("fov", 32.0), render.get("bloom", 0.5), render.get("exposure", 1.0))
    self.background = np.array(render.get("background", (0.010, 0.012, 0.020)), np.float32)
    self.t = 0.0

  @property
  def alive_total(self) -> int:
    return sum(e.alive_count for e in self.emitters)

  def step(self, dt: float = DT) -> None:
    for e in self.emitters:
      anchor = self.timeline.anchor(e.spec.attach, self.t)
      anchor_next = self.timeline.anchor(e.spec.attach, self.t + dt)
      e.step(self.t, dt, anchor, (anchor_next - anchor) / dt, self.curl_volume)
    self.t += dt

  def render(self) -> np.ndarray:
    r = self.renderer
    hdr = np.empty((r.height, r.width, 3), np.float32)
    hdr[...] = self.background
    for e in self.emitters:  # emitter order is draw order
      rec = e.render_records(self.t, self.atlas, self.rows, self.noise2d)
      if rec is not None:
        r.draw_layer(hdr, rec)
    return r.finish(hdr)

  def run(self, until: float | None = None) -> None:
    until = self.timeline.end if until is None else until
    while self.t < until - 1e-6:
      self.step()

  def report(self) -> str:
    lines = [
      "| emitter | spawned | max alive | pool | dropped | suggested pool |",
      "|---|---:|---:|---:|---:|---:|",
    ]
    for e in self.emitters:
      suggested = int(math.ceil(e.max_alive * 1.25 / 64.0) * 64) if e.max_alive else 64
      lines.append(f"| {e.spec.name} | {e.spawned} | {e.max_alive} | {e.spec.pool} | {e.dropped} | {suggested} |")
    lines.append("")
    lines.append("suggested pool = max alive * 1.25 rounded up to 64; dropped > 0 means the pool was too small.")
    return "\n".join(lines)
