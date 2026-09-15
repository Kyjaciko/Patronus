from __future__ import annotations

import json
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from opensimplex import OpenSimplex

# ============================================================
# Configuration
# ============================================================

GRID_SIZE = 64

# World-space region represented by the vector field.
FIELD_MIN = np.array(
  [-10.0, -10.0, -10.0],
  dtype=np.float32
)

FIELD_MAX = np.array(
  [10.0, 10.0, 10.0],
  dtype=np.float32
)

# Noise frequency.
#
# Higher values:
#   -> more rapidly changing noise
#   -> more tightly packed curls
#
# Lower values:
#   -> smoother, larger curls
NOISE_SCALE = 0.85

# Three independent noise fields.
SEED_X = 12345
SEED_Y = 67890
SEED_Z = 13579

# Final scale of the generated vector field.
# This is not needed for the curl calculation itself.
FIELD_STRENGTH = 1.0

# Save paths.
OUTPUT_BIN  = Path("curl_noise_64x64x64_rgba16f.bin")
OUTPUT_JSON = Path("curl_noise_64x64x64_rgba16f.json")

# Visualization
SLICE_ARROW_STEP = 3

# Fraction of the field used for displaying arrows.
# Lower values make arrows smaller.
ARROW_SCALE = 0.75

# ============================================================
# Validation
# ============================================================

assert GRID_SIZE >= 3
assert FIELD_MAX.shape == (3,)
assert FIELD_MIN.shape == (3,)

field_size = FIELD_MAX - FIELD_MIN

assert np.all(field_size > 0.0)

# Distance between two neighbouring grid points.
GRID_SPACING = field_size / (GRID_SIZE - 1)

print("=" * 70)
print("3D CURL VECTOR FIELD GENERATOR")
print("=" * 70)

print(f"Grid size       : {GRID_SIZE} x {GRID_SIZE} x {GRID_SIZE}")
print(f"Field min       : {FIELD_MIN}")
print(f"Field max       : {FIELD_MAX}")
print(f"Grid spacing    : {GRID_SPACING}")
print(f"Noise scale     : {NOISE_SCALE}")
print(f"Seeds           : {SEED_X}, {SEED_Y}, {SEED_Z}")
print()

# For a discrete grid, using the actual grid spacing as epsilon
# is the mathematically natural choice for central finite differences.
EPSILON = float(np.mean(GRID_SPACING))

print(f"Finite difference epsilon: {EPSILON}")
print()


# ============================================================
# Generate world-space coordinates
# ============================================================

x = np.linspace(
  FIELD_MIN[0],
  FIELD_MAX[0],
  GRID_SIZE,
  dtype=np.float32
)

y = np.linspace(
  FIELD_MIN[1],
  FIELD_MAX[1],
  GRID_SIZE,
  dtype=np.float32
)

z = np.linspace(
  FIELD_MIN[2],
  FIELD_MAX[2],
  GRID_SIZE,
  dtype=np.float32
)


# ============================================================
# Generate independent OpenSimplex fields
# ============================================================
#
# We use [Z, Y, X] memory order because this matches the
# intuitive D3D12 texture layout:
#
#   texture[z][y][x]
#
# The final binary will therefore have X as the fastest-moving
# dimension, followed by Y, followed by Z.
# ============================================================

def generate_noise_field(
  seed: int,
  x_values: np.ndarray,
  y_values: np.ndarray,
  z_values: np.ndarray,
  scale: float,
) -> np.ndarray:
  """
  Generate one 3D OpenSimplex noise field.

  Returns:
    ndarray with shape [Z, Y, X]
  """

  generator = OpenSimplex(seed)

  size_x = len(x_values)
  size_y = len(y_values)
  size_z = len(z_values)

  field = np.empty(
      (size_z, size_y, size_x),
      dtype=np.float32
  )

  # OpenSimplex's scalar API is used here deliberately.
  # 64^3 = 262,144 samples per field.
  #
  # We generate three fields, so this is ~786k noise samples,
  # which is perfectly reasonable for an offline tool.

  for iz, world_z in enumerate(z_values):
    nz = float(world_z) * scale

    for iy, world_y in enumerate(y_values):
      ny = float(world_y) * scale

      for ix, world_x in enumerate(x_values):
        nx = float(world_x) * scale

        field[iz, iy, ix] = generator.noise3(
          nx,
          ny,
          nz
        )

  return field


print("Generating Ψx ...")
psi_x = generate_noise_field(
  SEED_X,
  x,
  y,
  z,
  NOISE_SCALE
)

print("Generating Ψy ...")
psi_y = generate_noise_field(
  SEED_Y,
  x,
  y,
  z,
  NOISE_SCALE
)

print("Generating Ψz ...")
psi_z = generate_noise_field(
  SEED_Z,
  x,
  y,
  z,
  NOISE_SCALE
)

print("Noise generation complete.")
print()


# ============================================================
# Finite differences
# ============================================================

def derivative_x(field: np.ndarray, dx: float) -> np.ndarray:
  """
  d(field) / dx

  field layout:
    [Z, Y, X]
  """

  result = np.empty_like(field)

  # Interior: central difference
  result[:, :, 1:-1] = (
    field[:, :, 2:] -
    field[:, :, :-2]
  ) / (2.0 * dx)

  # X = 0: forward difference
  result[:, :, 0] = (
    field[:, :, 1] -
    field[:, :, 0]
  ) / dx

  # X = last: backward difference
  result[:, :, -1] = (
    field[:, :, -1] -
    field[:, :, -2]
  ) / dx

  return result


def derivative_y(field: np.ndarray, dy: float) -> np.ndarray:
  """
  d(field) / dy

  field layout:
    [Z, Y, X]
  """

  result = np.empty_like(field)

  # Interior
  result[:, 1:-1, :] = (
    field[:, 2:, :] -
    field[:, :-2, :]
  ) / (2.0 * dy)

  # Y = 0
  result[:, 0, :] = (
    field[:, 1, :] -
    field[:, 0, :]
  ) / dy

  # Y = last
  result[:, -1, :] = (
    field[:, -1, :] -
    field[:, -2, :]
  ) / dy

  return result


def derivative_z(field: np.ndarray, dz: float) -> np.ndarray:
  """
  d(field) / dz

  field layout:
    [Z, Y, X]
  """

  result = np.empty_like(field)

  # Interior
  result[1:-1, :, :] = (
    field[2:, :, :] -
    field[:-2, :, :]
  ) / (2.0 * dz)

  # Z = 0
  result[0, :, :] = (
    field[1, :, :] -
    field[0, :, :]
  ) / dz

  # Z = last
  result[-1, :, :] = (
    field[-1, :, :] -
    field[-2, :, :]
  ) / dz

  return result


dx = float(GRID_SPACING[0])
dy = float(GRID_SPACING[1])
dz = float(GRID_SPACING[2])

print("Calculating finite differences ...")

# d_psix_dx = derivative_x(psi_x, dx)
# d_psix_dy = derivative_y(psi_x, dy)
# d_psix_dz = derivative_z(psi_x, dz)

# d_psiy_dx = derivative_x(psi_y, dx)
# d_psiy_dy = derivative_y(psi_y, dy)
# d_psiy_dz = derivative_z(psi_y, dz)

# d_psiz_dx = derivative_x(psi_z, dx)
# d_psiz_dy = derivative_y(psi_z, dy)
# d_psiz_dz = derivative_z(psi_z, dz)

d_psix_dz, d_psix_dy, d_psix_dx = np.gradient(psi_x, dz, dy, dx)
d_psiy_dz, d_psiy_dy, d_psiy_dx = np.gradient(psi_y, dz, dy, dx)
d_psiz_dz, d_psiz_dy, d_psiz_dx = np.gradient(psi_z, dz, dy, dx)

# ============================================================
# Curl
# ============================================================
#
# curl(Ψ) =
#
#   Vx = dΨz/dy - dΨy/dz
#   Vy = dΨx/dz - dΨz/dx
#   Vz = dΨy/dx - dΨx/dy
#
# ============================================================

print("Calculating curl ...")

vx = d_psiz_dy - d_psiy_dz
vy = d_psix_dz - d_psiz_dx
vz = d_psiy_dx - d_psix_dy


# ============================================================
# Convert to float32 before normalization
# ============================================================

vx = vx.astype(np.float32)
vy = vy.astype(np.float32)
vz = vz.astype(np.float32)


# ============================================================
# Vector magnitude
# ============================================================

magnitude = np.sqrt(
  vx * vx +
  vy * vy +
  vz * vz
).astype(np.float32)


print()
print("Raw curl statistics:")
print(f"  Vx min/max : {vx.min(): .6f} / {vx.max(): .6f}")
print(f"  Vy min/max : {vy.min(): .6f} / {vy.max(): .6f}")
print(f"  Vz min/max : {vz.min(): .6f} / {vz.max(): .6f}")
print(f"  Magnitude  : {magnitude.min(): .6f} / {magnitude.max(): .6f}")
print(f"  Mean mag.  : {magnitude.mean(): .6f}")
print()


# ============================================================
# Normalize field to a useful range
# ============================================================
#
# Instead of using the absolute mathematical curl magnitude
# directly, scale the field based on a high percentile.
#
# This prevents a handful of extreme voxels from dominating
# the complete simulation.
# ============================================================

percentile_99 = float(
  np.percentile(magnitude, 99.0)
)

if percentile_99 <= 1e-8:
  raise RuntimeError(
    "Generated curl field has almost zero magnitude."
  )

normalization_scale = (
  FIELD_STRENGTH / percentile_99
)

vx *= normalization_scale
vy *= normalization_scale
vz *= normalization_scale

magnitude = np.sqrt(
  vx * vx +
  vy * vy +
  vz * vz
).astype(np.float32)

print("Normalized curl statistics:")
print(f"  Scale      : {normalization_scale:.6f}")
print(f"  Magnitude  : {magnitude.min(): .6f} / {magnitude.max(): .6f}")
print(f"  Mean mag.  : {magnitude.mean(): .6f}")
print()


# ============================================================
# Build RGBA float16 texture data
# ============================================================
#
# D3D12 format:
#
#   DXGI_FORMAT_R16G16B16A16_FLOAT
#
# Therefore:
#
#   R = Vx
#   G = Vy
#   B = Vz
#   A = 0
#
# Shape:
#
#   [Z, Y, X, RGBA]
#
# Since NumPy uses C-order, X changes fastest.
# This is exactly the convenient layout for a tightly-packed
# texture upload source.
# ============================================================

vector_field = np.empty(
  (GRID_SIZE, GRID_SIZE, GRID_SIZE, 4),
  dtype=np.float16
)

vector_field[..., 0] = vx.astype(np.float16)
vector_field[..., 1] = vy.astype(np.float16)
vector_field[..., 2] = vz.astype(np.float16)
vector_field[..., 3] = np.float16(0.0)


# ============================================================
# Save binary file
# ============================================================

print(f"Writing binary texture: {OUTPUT_BIN}")

vector_field.tofile(OUTPUT_BIN)

expected_size = (
  GRID_SIZE *
  GRID_SIZE *
  GRID_SIZE *
  4 *
  np.dtype(np.float16).itemsize
)

actual_size = OUTPUT_BIN.stat().st_size

print(f"Expected binary size : {expected_size:,} bytes")
print(f"Actual binary size   : {actual_size:,} bytes")

if actual_size != expected_size:
  raise RuntimeError(
    "Binary file size does not match expected R16G16B16A16_FLOAT size."
  )

print()


# ============================================================
# Save metadata
# ============================================================

metadata = {
  "format": "DXGI_FORMAT_R16G16B16A16_FLOAT",
  "width": GRID_SIZE,
  "height": GRID_SIZE,
  "depth": GRID_SIZE,
  "channels": {
    "R": "Vx",
    "G": "Vy",
    "B": "Vz",
    "A": 0.0
  },
  "array_order": "[z, y, x, channel]",
  "world_space_min": FIELD_MIN.tolist(),
  "world_space_max": FIELD_MAX.tolist(),
  "grid_spacing": GRID_SPACING.tolist(),
  "epsilon": EPSILON,
  "noise_scale": NOISE_SCALE,
  "seeds": {
    "x": SEED_X,
    "y": SEED_Y,
    "z": SEED_Z
  },
  "field_strength": FIELD_STRENGTH,
  "normalization_percentile": 99.0,
  "normalization_scale": normalization_scale
}

with OUTPUT_JSON.open("w", encoding="utf-8") as file:
  json.dump(
    metadata,
    file,
    indent=4
  )

print(f"Metadata written     : {OUTPUT_JSON}")
print()


# ============================================================
# Visualization
# ============================================================

# Middle slices.
mid_x = GRID_SIZE // 2
mid_y = GRID_SIZE // 2
mid_z = GRID_SIZE // 2


def normalize_color_data(data: np.ndarray) -> np.ndarray:
  """
  Robustly normalize magnitude for display.
  """

  vmax = float(np.percentile(data, 99.0))

  if vmax <= 1e-8:
    return np.zeros_like(data)

  return np.clip(
    data / vmax,
    0.0,
    1.0
  )


fig, axes = plt.subplots(
  1,
  3,
  figsize=(18, 6),
  constrained_layout=True
)


# ============================================================
# XY slice at Z = middle
# ============================================================

ax = axes[0]

mag_xy = magnitude[mid_z, :, :]
vx_xy = vx[mid_z, :, :]
vy_xy = vy[mid_z, :, :]

im = ax.imshow(
  normalize_color_data(mag_xy),
  origin="lower",
  extent=[
    FIELD_MIN[0],
    FIELD_MAX[0],
    FIELD_MIN[1],
    FIELD_MAX[1]
  ],
  cmap="magma",
  interpolation="nearest"
)

yy, xx = np.meshgrid(
  y[::SLICE_ARROW_STEP],
  x[::SLICE_ARROW_STEP],
  indexing="ij"
)

u = vx_xy[::SLICE_ARROW_STEP, ::SLICE_ARROW_STEP]
v = vy_xy[::SLICE_ARROW_STEP, ::SLICE_ARROW_STEP]

ax.quiver(
  xx,
  yy,
  u,
  v,
  color="white",
  angles="xy",
  scale_units="xy",
  scale=ARROW_SCALE,
  width=0.003
)

ax.set_title(
  f"XY slice | Z = {z[mid_z]:.2f}"
)

ax.set_xlabel("X")
ax.set_ylabel("Y")
ax.set_aspect("equal")


# ============================================================
# XZ slice at Y = middle
# ============================================================

ax = axes[1]

mag_xz = magnitude[:, mid_y, :]
vx_xz = vx[:, mid_y, :]
vz_xz = vz[:, mid_y, :]

ax.imshow(
  normalize_color_data(mag_xz),
  origin="lower",
  extent=[
    FIELD_MIN[0],
    FIELD_MAX[0],
    FIELD_MIN[2],
    FIELD_MAX[2]
  ],
  cmap="magma",
  interpolation="nearest"
)

zz, xx = np.meshgrid(
  z[::SLICE_ARROW_STEP],
  x[::SLICE_ARROW_STEP],
  indexing="ij"
)

u = vx_xz[::SLICE_ARROW_STEP, ::SLICE_ARROW_STEP]
v = vz_xz[::SLICE_ARROW_STEP, ::SLICE_ARROW_STEP]

ax.quiver(
  xx,
  zz,
  u,
  v,
  color="white",
  angles="xy",
  scale_units="xy",
  scale=ARROW_SCALE,
  width=0.003
)

ax.set_title(
  f"XZ slice | Y = {y[mid_y]:.2f}"
)

ax.set_xlabel("X")
ax.set_ylabel("Z")
ax.set_aspect("equal")


# ============================================================
# YZ slice at X = middle
# ============================================================

ax = axes[2]

mag_yz = magnitude[:, :, mid_x]
vy_yz = vy[:, :, mid_x]
vz_yz = vz[:, :, mid_x]

ax.imshow(
  normalize_color_data(mag_yz),
  origin="lower",
  extent=[
    FIELD_MIN[1],
    FIELD_MAX[1],
    FIELD_MIN[2],
    FIELD_MAX[2]
  ],
  cmap="magma",
  interpolation="nearest"
)

zz, yy = np.meshgrid(
  z[::SLICE_ARROW_STEP],
  y[::SLICE_ARROW_STEP],
  indexing="ij"
)

u = vy_yz[::SLICE_ARROW_STEP, ::SLICE_ARROW_STEP]
v = vz_yz[::SLICE_ARROW_STEP, ::SLICE_ARROW_STEP]

ax.quiver(
  yy,
  zz,
  u,
  v,
  color="white",
  angles="xy",
  scale_units="xy",
  scale=ARROW_SCALE,
  width=0.003
)

ax.set_title(
  f"YZ slice | X = {x[mid_x]:.2f}"
)

ax.set_xlabel("Y")
ax.set_ylabel("Z")
ax.set_aspect("equal")


fig.suptitle(
  "3D Curl Noise Vector Field",
  fontsize=16
)

plt.show()