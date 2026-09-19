"""Tileable gradient (Perlin) and cellular (Worley) noise in pure numpy.

Why tileable: the renderer samples the curl volume with a WRAP sampler and
the 2D noise with wrapping UVs. A field baked over a plain world range is
not periodic, so wrapping produces a discontinuity at the tile boundary (a
"wall" in the velocity field). Generating the noise on a periodic lattice
makes every tile edge exact, so wrap sampling is seamless by construction.

All functions are vectorised: a 64^3 volume with 4 octaves is well under a
second. Period arguments are in lattice cells; the noise repeats every
`period` cells and the caller maps that onto the texture size.
"""

from __future__ import annotations

import numpy as np


def _fade(t: np.ndarray) -> np.ndarray:
  # Perlin's quintic: zero first and second derivative at 0 and 1, so the
  # curl (a derivative of the field) is continuous across cell edges.
  return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def _unit_vectors(rng: np.random.Generator, shape: tuple[int, ...], dims: int) -> np.ndarray:
  vectors = rng.normal(size=(*shape, dims)).astype(np.float32)
  norms = np.linalg.norm(vectors, axis=-1, keepdims=True)
  norms[norms < 1e-8] = 1.0
  return vectors / norms


def perlin3d_periodic(
  size: tuple[int, int, int],
  period: tuple[int, int, int],
  seed: int,
) -> np.ndarray:
  """Periodic 3D Perlin noise.

  size:   samples per axis as (nz, ny, nx)
  period: lattice cells per axis as (pz, py, px); the pattern repeats every
          `size` samples because the lattice wraps modulo `period`.
  Returns float32 array [nz, ny, nx] in roughly [-1, 1].
  """
  nz, ny, nx = size
  pz, py, px = period
  rng = np.random.default_rng(seed)
  gradients = _unit_vectors(rng, (pz, py, px), 3)

  # Sample positions in lattice units. Using n (not n - 1) cells across the
  # texture is what makes texel 0 and texel n coincide, i.e. tile.
  z = (np.arange(nz, dtype=np.float32) * (pz / nz))[:, None, None]
  y = (np.arange(ny, dtype=np.float32) * (py / ny))[None, :, None]
  x = (np.arange(nx, dtype=np.float32) * (px / nx))[None, None, :]

  z0 = np.floor(z).astype(np.int64)
  y0 = np.floor(y).astype(np.int64)
  x0 = np.floor(x).astype(np.int64)
  fz = z - z0
  fy = y - y0
  fx = x - x0

  z0, y0, x0 = np.broadcast_arrays(z0, y0, x0)
  fz, fy, fx = np.broadcast_arrays(fz, fy, fx)

  def corner(dz: int, dy: int, dx: int) -> np.ndarray:
    g = gradients[(z0 + dz) % pz, (y0 + dy) % py, (x0 + dx) % px]
    return g[..., 0] * (fx - dx) + g[..., 1] * (fy - dy) + g[..., 2] * (fz - dz)

  uz, uy, ux = _fade(fz), _fade(fy), _fade(fx)

  def lerp(a, b, t):
    return a + (b - a) * t

  c000, c001 = corner(0, 0, 0), corner(0, 0, 1)
  c010, c011 = corner(0, 1, 0), corner(0, 1, 1)
  c100, c101 = corner(1, 0, 0), corner(1, 0, 1)
  c110, c111 = corner(1, 1, 0), corner(1, 1, 1)

  x00 = lerp(c000, c001, ux)
  x01 = lerp(c010, c011, ux)
  x10 = lerp(c100, c101, ux)
  x11 = lerp(c110, c111, ux)
  y0v = lerp(x00, x01, uy)
  y1v = lerp(x10, x11, uy)
  return lerp(y0v, y1v, uz).astype(np.float32)


def perlin2d_periodic(
  size: tuple[int, int],
  period: tuple[int, int],
  seed: int,
) -> np.ndarray:
  """Periodic 2D Perlin noise. size = (ny, nx), period = (py, px)."""
  ny, nx = size
  py, px = period
  rng = np.random.default_rng(seed)
  gradients = _unit_vectors(rng, (py, px), 2)

  y = (np.arange(ny, dtype=np.float32) * (py / ny))[:, None]
  x = (np.arange(nx, dtype=np.float32) * (px / nx))[None, :]
  y0 = np.floor(y).astype(np.int64)
  x0 = np.floor(x).astype(np.int64)
  fy = y - y0
  fx = x - x0
  y0, x0 = np.broadcast_arrays(y0, x0)
  fy, fx = np.broadcast_arrays(fy, fx)

  def corner(dy: int, dx: int) -> np.ndarray:
    g = gradients[(y0 + dy) % py, (x0 + dx) % px]
    return g[..., 0] * (fx - dx) + g[..., 1] * (fy - dy)

  uy, ux = _fade(fy), _fade(fx)
  top = corner(0, 0) + (corner(0, 1) - corner(0, 0)) * ux
  bottom = corner(1, 0) + (corner(1, 1) - corner(1, 0)) * ux
  return (top + (bottom - top) * uy).astype(np.float32)


def fbm(
  base: callable,
  size: tuple[int, ...],
  period: tuple[int, ...],
  seed: int,
  octaves: int = 4,
  lacunarity: int = 2,
  gain: float = 0.5,
  billow: bool = False,
) -> np.ndarray:
  """Fractal sum of `base` noise. Period doubles per octave (integer
  lacunarity keeps every octave exactly periodic on the same tile).
  billow=True sums |noise| instead, which gives the puffy cloud look."""
  total = np.zeros(size, dtype=np.float32)
  amplitude = 1.0
  amplitude_sum = 0.0
  current_period = tuple(period)
  for octave in range(octaves):
    layer = base(size, current_period, seed + 1013 * octave)
    if billow:
      layer = np.abs(layer) * 2.0 - 1.0
    total += amplitude * layer
    amplitude_sum += amplitude
    amplitude *= gain
    current_period = tuple(p * lacunarity for p in current_period)
  return total / amplitude_sum


def worley2d_periodic(
  size: tuple[int, int],
  period: tuple[int, int],
  seed: int,
  points_per_cell: int = 1,
) -> np.ndarray:
  """Periodic 2D Worley (cellular) noise: distance to the nearest feature
  point, normalised so the typical range is [0, 1]. size = (ny, nx),
  period = (py, px) cells; one or more feature points per cell."""
  ny, nx = size
  py, px = period
  rng = np.random.default_rng(seed)
  feature_points = rng.random((py, px, points_per_cell, 2), dtype=np.float32)

  y = (np.arange(ny, dtype=np.float32) * (py / ny))[:, None]
  x = (np.arange(nx, dtype=np.float32) * (px / nx))[None, :]
  cy = np.floor(y).astype(np.int64)
  cx = np.floor(x).astype(np.int64)
  cy, cx = np.broadcast_arrays(cy, cx)
  y, x = np.broadcast_arrays(y, x)

  best = np.full((ny, nx), np.inf, dtype=np.float32)
  for dy in (-1, 0, 1):
    for dx in (-1, 0, 1):
      ncy = cy + dy
      ncx = cx + dx
      points = feature_points[ncy % py, ncx % px]  # [ny, nx, k, 2]
      # Feature point position in lattice units, in the *unwrapped*
      # neighbour cell so distances across the tile edge are correct.
      point_x = ncx[..., None] + points[..., 0]
      point_y = ncy[..., None] + points[..., 1]
      distance = np.sqrt((point_x - x[..., None]) ** 2 + (point_y - y[..., None]) ** 2)
      best = np.minimum(best, distance.min(axis=-1))
  return np.clip(best, 0.0, 1.0).astype(np.float32)


def curl_periodic(
  psi_x: np.ndarray,
  psi_y: np.ndarray,
  psi_z: np.ndarray,
  spacing: tuple[float, float, float],
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
  """Curl of a periodic vector potential via periodic central differences.

  Arrays are [z, y, x]; spacing is (dz, dy, dx) in world units per sample.
  np.roll makes the stencil wrap, so the derivative at the tile edge uses
  the texel on the other side instead of a one-sided difference. That is
  what keeps the resulting velocity field exactly periodic.

    Vx = dPsiz/dy - dPsiy/dz
    Vy = dPsix/dz - dPsiz/dx
    Vz = dPsiy/dx - dPsix/dy
  """
  dz, dy, dx = spacing

  def d_dx(f):
    return (np.roll(f, -1, axis=2) - np.roll(f, 1, axis=2)) / (2.0 * dx)

  def d_dy(f):
    return (np.roll(f, -1, axis=1) - np.roll(f, 1, axis=1)) / (2.0 * dy)

  def d_dz(f):
    return (np.roll(f, -1, axis=0) - np.roll(f, 1, axis=0)) / (2.0 * dz)

  vx = d_dy(psi_z) - d_dz(psi_y)
  vy = d_dz(psi_x) - d_dx(psi_z)
  vz = d_dx(psi_y) - d_dy(psi_x)
  return vx.astype(np.float32), vy.astype(np.float32), vz.astype(np.float32)


def divergence_periodic(
  vx: np.ndarray,
  vy: np.ndarray,
  vz: np.ndarray,
  spacing: tuple[float, float, float],
) -> np.ndarray:
  """Periodic central-difference divergence, for checking curl output.
  With the same stencil as curl_periodic the result is zero to float
  round-off: that is the divergence-free guarantee, verified numerically."""
  dz, dy, dx = spacing
  ddx = (np.roll(vx, -1, axis=2) - np.roll(vx, 1, axis=2)) / (2.0 * dx)
  ddy = (np.roll(vy, -1, axis=1) - np.roll(vy, 1, axis=1)) / (2.0 * dy)
  ddz = (np.roll(vz, -1, axis=0) - np.roll(vz, 1, axis=0)) / (2.0 * dz)
  return (ddx + ddy + ddz).astype(np.float32)


def normalise_to_percentile(
  components: list[np.ndarray],
  percentile: float = 99.0,
  target: float = 1.0,
) -> tuple[list[np.ndarray], float]:
  """Scale a vector field so its `percentile` magnitude equals `target`.
  A few extreme voxels would otherwise dominate the simulation's tuning."""
  magnitude = np.sqrt(sum(c.astype(np.float32) ** 2 for c in components))
  reference = float(np.percentile(magnitude, percentile))
  if reference <= 1e-8:
    raise RuntimeError("field has almost zero magnitude")
  scale = target / reference
  return [c * scale for c in components], scale
