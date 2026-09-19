#!/usr/bin/env python
"""Bake a tileable 3D curl-noise volume for the particle simulation.

Replaces "tools/curl noise generator/curlnoise.py". Differences that matter:

* Tileable. Noise is generated on a periodic lattice and the curl uses a
  wrapping stencil, so the volume repeats seamlessly. The shader can sample
  it with a WRAP sampler in emitter-local space at any tile scale; the old
  world-fixed +/-10 box and its wrap seam are gone.
* The alpha channel carries a scalar fractal noise in [0, 1] instead of 0.
  It costs nothing (the texel is fetched anyway) and gives the sim a free
  per-position modulation source for size, alpha, or spawn density.
* Divergence is checked numerically and written to the metadata, so the
  "curl is divergence-free" claim is verified, not asserted.
* No third-party noise dependency (pure numpy).

Output: <stem>.bin (R16G16B16A16_FLOAT, [z, y, x, rgba]), <stem>.json
(layout + sampling metadata), optional <stem>.h (constexpr values) and
<stem>.png (mid-slice preview).

Usage (from the repo root):

  python tools/bake_curl_noise.py --out assets/noise/curl_noise_64_rgba16f --preview --header
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from vfxtools import noise as vn  # noqa: E402
from vfxtools.rawtexture import write_constexpr_header, write_raw_texture  # noqa: E402


def bake(
  size: int = 64,
  period: int = 4,
  octaves: int = 3,
  tile_world_size: float = 20.0,
  seed: int = 12345,
  percentile: float = 99.0,
) -> tuple[np.ndarray, dict]:
  """Return (texels [size, size, size, 4] float16, metadata dict)."""
  shape = (size, size, size)
  lattice = (period, period, period)
  spacing = (tile_world_size / size,) * 3

  # Three independent potentials; the curl of the combined vector potential
  # is the velocity field. Different seeds keep them uncorrelated.
  psi_x = vn.fbm(vn.perlin3d_periodic, shape, lattice, seed + 0, octaves)
  psi_y = vn.fbm(vn.perlin3d_periodic, shape, lattice, seed + 1, octaves)
  psi_z = vn.fbm(vn.perlin3d_periodic, shape, lattice, seed + 2, octaves)

  vx, vy, vz = vn.curl_periodic(psi_x, psi_y, psi_z, spacing)
  (vx, vy, vz), scale = vn.normalise_to_percentile([vx, vy, vz], percentile, 1.0)

  magnitude = np.sqrt(vx * vx + vy * vy + vz * vz)
  divergence = vn.divergence_periodic(vx, vy, vz, spacing)
  max_div_relative = float(np.abs(divergence).max() / max(float(magnitude.max()), 1e-8))

  # Scalar modulation noise in [0, 1] for the alpha channel.
  scalar = vn.fbm(vn.perlin3d_periodic, shape, lattice, seed + 3, octaves)
  low, high = np.percentile(scalar, [1.0, 99.0])
  scalar = np.clip((scalar - low) / max(high - low, 1e-8), 0.0, 1.0)

  texels = np.empty((size, size, size, 4), dtype=np.float16)
  texels[..., 0] = vx
  texels[..., 1] = vy
  texels[..., 2] = vz
  texels[..., 3] = scalar

  metadata = {
    "channels": {"R": "Vx", "G": "Vy", "B": "Vz", "A": "scalar fbm in [0,1]"},
    "tileable": True,
    "tile_world_size": tile_world_size,
    "sampling": (
      "uvw = (position - tileOrigin) / tile_world_size with a WRAP sampler; "
      "any origin/scale works because the volume is periodic"
    ),
    "grid_spacing": list(spacing),
    "lattice_period": period,
    "octaves": octaves,
    "seed": seed,
    "normalization_percentile": percentile,
    "normalization_scale": scale,
    "magnitude_mean": float(magnitude.mean()),
    "magnitude_max": float(magnitude.max()),
    "max_abs_divergence_over_max_magnitude": max_div_relative,
  }
  return texels, metadata


def write_preview(texels: np.ndarray, path: Path, tile_world_size: float) -> None:
  import matplotlib

  matplotlib.use("Agg")
  import matplotlib.pyplot as plt

  size = texels.shape[0]
  mid = size // 2
  v = texels.astype(np.float32)
  magnitude = np.sqrt(np.sum(v[..., :3] ** 2, axis=-1))
  vmax = float(np.percentile(magnitude, 99.0))
  step = max(1, size // 20)
  coords = (np.arange(size) + 0.5) * (tile_world_size / size)

  fig, axes = plt.subplots(1, 4, figsize=(20, 5), constrained_layout=True)
  slices = [
    ("XY slice, Z mid", magnitude[mid], v[mid, ..., 0], v[mid, ..., 1]),
    ("XZ slice, Y mid", magnitude[:, mid], v[:, mid, :, 0], v[:, mid, :, 2]),
    ("YZ slice, X mid", magnitude[:, :, mid], v[:, :, mid, 1], v[:, :, mid, 2]),
  ]
  for ax, (title, mag, u, w) in zip(axes[:3], slices):
    ax.imshow(np.clip(mag / max(vmax, 1e-8), 0, 1), origin="lower", cmap="magma",
              extent=[0, tile_world_size, 0, tile_world_size], interpolation="nearest")
    xx, yy = np.meshgrid(coords[::step], coords[::step])
    ax.quiver(xx, yy, u[::step, ::step], w[::step, ::step], color="white",
              angles="xy", scale_units="xy", scale=0.75, width=0.003)
    ax.set_title(title)
    ax.set_aspect("equal")
  axes[3].imshow(v[mid, ..., 3], origin="lower", cmap="gray", vmin=0, vmax=1,
                 extent=[0, tile_world_size, 0, tile_world_size], interpolation="nearest")
  axes[3].set_title("alpha: scalar noise, Z mid")
  axes[3].set_aspect("equal")
  fig.suptitle(f"Tileable curl noise {size}^3 (tile = {tile_world_size} world units)")
  fig.savefig(path, dpi=110)
  plt.close(fig)


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("--out", type=Path, default=Path("assets/noise/curl_noise_64_rgba16f"),
                      help="output stem (no extension)")
  parser.add_argument("--size", type=int, default=64, help="texels per axis")
  parser.add_argument("--period", type=int, default=4, help="lattice cells per tile (lower = larger curls)")
  parser.add_argument("--octaves", type=int, default=3)
  parser.add_argument("--tile-size", type=float, default=20.0, help="world units one tile spans (metadata only)")
  parser.add_argument("--seed", type=int, default=12345)
  parser.add_argument("--preview", action="store_true", help="also write <stem>.png")
  parser.add_argument("--header", action="store_true", help="also write <stem>.h with constexpr metadata")
  args = parser.parse_args()

  texels, metadata = bake(args.size, args.period, args.octaves, args.tile_size, args.seed)
  args.out.parent.mkdir(parents=True, exist_ok=True)
  written = write_raw_texture(args.out, texels, "DXGI_FORMAT_R16G16B16A16_FLOAT", metadata)

  if args.header:
    write_constexpr_header(
      args.out.with_suffix(".h"),
      "curl_noise",
      {
        "kSize": args.size,
        "kTileWorldSize": float(args.tile_size),
        "kBytesPerTexel": written["bytes_per_texel"],
        "kByteSize": written["byte_size"],
        "kBinFile": written["bin"],
      },
    )
  if args.preview:
    write_preview(texels, args.out.with_suffix(".png"), args.tile_size)

  print(f"wrote {args.out}.bin ({written['byte_size']:,} bytes) + .json"
        f"{' + .h' if args.header else ''}{' + .png' if args.preview else ''}")
  print(f"  magnitude mean/max     : {metadata['magnitude_mean']:.4f} / {metadata['magnitude_max']:.4f}")
  print(f"  max |div| / max |v|    : {metadata['max_abs_divergence_over_max_magnitude']:.2e}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
