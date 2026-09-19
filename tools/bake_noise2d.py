#!/usr/bin/env python
"""Bake a tileable 2D "noise pack" texture for particle pixel shaders.

One RGBA8 texture, four independent tileable noises, so a single fetch
gives the pixel shader everything it needs for erosion (dissolve), UV
distortion and flicker:

  R  Perlin fBm, 5 octaves, coarse       - erosion threshold, soft shapes
  G  Worley (cellular), inverted         - cell-like energy / cracks
  B  Perlin fBm, 4 octaves, fine, seed+1 - UV distortion, second erosion
  A  billow fBm (|noise|), coarse        - puffy smoke-like density

Each channel is stretched to [0, 1] using its 1st/99th percentile so the
histogram fills the 8-bit range (erosion thresholds then map linearly).

Output: <stem>.bin (R8G8B8A8_UNORM, [y, x, rgba]), <stem>.json, optional
<stem>.png preview.

Usage (from the repo root):

  python tools/bake_noise2d.py --out assets/noise/noise_pack_256_rgba8 --preview
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from vfxtools import noise as vn  # noqa: E402
from vfxtools.rawtexture import write_raw_texture  # noqa: E402


def _stretch(field: np.ndarray) -> np.ndarray:
  low, high = np.percentile(field, [1.0, 99.0])
  return np.clip((field - low) / max(high - low, 1e-8), 0.0, 1.0)


def bake(size: int = 256, period: int = 4, seed: int = 777) -> tuple[np.ndarray, dict]:
  shape = (size, size)
  coarse = (period, period)
  fine = (period * 2, period * 2)

  r = _stretch(vn.fbm(vn.perlin2d_periodic, shape, coarse, seed, octaves=5))
  g = 1.0 - _stretch(vn.worley2d_periodic(shape, fine, seed + 11))
  b = _stretch(vn.fbm(vn.perlin2d_periodic, shape, fine, seed + 1, octaves=4))
  a = _stretch(vn.fbm(vn.perlin2d_periodic, shape, coarse, seed + 2, octaves=4, billow=True))

  texels = np.stack([r, g, b, a], axis=-1)
  texels = np.round(texels * 255.0).astype(np.uint8)

  metadata = {
    "channels": {
      "R": "perlin fbm coarse (erosion)",
      "G": "worley inverted (cells)",
      "B": "perlin fbm fine (distortion)",
      "A": "billow fbm coarse (density)",
    },
    "tileable": True,
    "lattice_period": period,
    "seed": seed,
    "srgb": False,
  }
  return texels, metadata


def write_preview(texels: np.ndarray, path: Path) -> None:
  import matplotlib

  matplotlib.use("Agg")
  import matplotlib.pyplot as plt

  names = ["R perlin", "G worley", "B perlin fine", "A billow"]
  fig, axes = plt.subplots(1, 4, figsize=(16, 4.4), constrained_layout=True)
  for i, (ax, name) in enumerate(zip(axes, names)):
    # Tile 2x2 so a seam would be visible in the preview.
    tile = np.tile(texels[..., i], (2, 2))
    ax.imshow(tile, cmap="gray", vmin=0, vmax=255, interpolation="nearest")
    ax.set_title(f"{name} (tiled 2x2)")
    ax.set_axis_off()
  fig.savefig(path, dpi=110)
  plt.close(fig)


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("--out", type=Path, default=Path("assets/noise/noise_pack_256_rgba8"))
  parser.add_argument("--size", type=int, default=256)
  parser.add_argument("--period", type=int, default=4, help="lattice cells per tile for the coarse channels")
  parser.add_argument("--seed", type=int, default=777)
  parser.add_argument("--preview", action="store_true")
  args = parser.parse_args()

  texels, metadata = bake(args.size, args.period, args.seed)
  args.out.parent.mkdir(parents=True, exist_ok=True)
  written = write_raw_texture(args.out, texels, "DXGI_FORMAT_R8G8B8A8_UNORM", metadata)
  if args.preview:
    write_preview(texels, args.out.with_suffix(".png"))
  print(f"wrote {args.out}.bin ({written['byte_size']:,} bytes) + .json{' + .png' if args.preview else ''}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
