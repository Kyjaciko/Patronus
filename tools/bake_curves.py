#!/usr/bin/env python
"""Bake per-emitter curves and gradients into one RGBA16F "curve atlas".

Why a texture: the simulation evaluates "size over life", "alpha over life"
and "colour over life" once per particle per frame. Baking each curve into
one texture row and sampling it with normalised age (u in [0, 1], linear
filter, CLAMP) is one texture fetch, needs no per-emitter key arrays in
constant buffers, and is exactly how Niagara and Unity's VFX Graph do it.
RGBA16F keeps emissive colours above 1.0 for the HDR pipeline.

Input JSON:

  {
    "width": 256,
    "curves": [
      { "name": "core.color", "keys": [[0.0, [8, 4, 20, 0]], [0.2, [12, 6, 30, 1]], [1.0, [2, 1, 6, 0]]] },
      { "name": "core.size",  "keys": [[0.0, 0.2], [0.3, 1.0], [1.0, 0.0]], "interp": "smooth" }
    ]
  }

* A key is [t, value] with t in [0, 1]; value is a scalar (written to R,
  with G/B/A = 0) or a 4-vector (RGBA).
* interp: "linear" (default) or "smooth" (smoothstep between keys).
* Every curve is one row, in input order. The output .json maps each name
  to its row index and the v coordinate of the row centre.

Sampling convention: texel i covers u in [i/width, (i+1)/width]; the value
stored at texel i is the curve evaluated at the texel centre (i + 0.5)/width.
With a linear sampler and u = age/lifetime, the shader gets the interpolated
curve back. Use CLAMP so age 0 and 1 hit the end keys exactly.

Output: <stem>.bin (R16G16B16A16_FLOAT, [row, x, rgba]), <stem>.json,
optional <stem>.png preview (tonemapped for display, HDR values compress).

Usage (from the repo root):

  python tools/bake_curves.py assets/curves/arcane_bolt.json --preview
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from vfxtools.rawtexture import write_raw_texture  # noqa: E402


def evaluate_curve(keys: list, u: np.ndarray, interp: str = "linear") -> np.ndarray:
  """Evaluate a keyed curve at positions u (any shape). Returns [..., 4]."""
  if not keys:
    raise ValueError("curve has no keys")
  times = np.array([float(k[0]) for k in keys], dtype=np.float32)
  if np.any(np.diff(times) < 0):
    raise ValueError("keys must be sorted by t")
  values = []
  for _, value in keys:
    v = np.atleast_1d(np.asarray(value, dtype=np.float32))
    if v.size == 1:
      v = np.array([v[0], 0.0, 0.0, 0.0], dtype=np.float32)
    elif v.size == 3:
      v = np.array([v[0], v[1], v[2], 1.0], dtype=np.float32)
    elif v.size != 4:
      raise ValueError(f"key value must be scalar, RGB or RGBA, got {value}")
    values.append(v)
  values = np.stack(values)  # [k, 4]

  u = np.asarray(u, dtype=np.float32)
  out = np.empty((*u.shape, 4), dtype=np.float32)
  if len(keys) == 1:
    out[...] = values[0]
    return out

  # Index of the segment each u falls into, clamped to the key range.
  segment = np.clip(np.searchsorted(times, u, side="right") - 1, 0, len(keys) - 2)
  t0 = times[segment]
  t1 = times[segment + 1]
  span = np.where(t1 > t0, t1 - t0, 1.0)
  f = np.clip((u - t0) / span, 0.0, 1.0)
  if interp == "smooth":
    f = f * f * (3.0 - 2.0 * f)
  elif interp != "linear":
    raise ValueError(f"unknown interp {interp!r}")
  out[...] = values[segment] + (values[segment + 1] - values[segment]) * f[..., None]
  return out


def bake(spec: dict) -> tuple[np.ndarray, dict]:
  width = int(spec.get("width", 256))
  curves = spec["curves"]
  if not curves:
    raise ValueError("no curves")
  u = (np.arange(width, dtype=np.float32) + 0.5) / width

  texels = np.zeros((len(curves), width, 4), dtype=np.float16)
  rows = {}
  for row, curve in enumerate(curves):
    name = curve["name"]
    if name in rows:
      raise ValueError(f"duplicate curve name {name!r}")
    texels[row] = evaluate_curve(curve["keys"], u, curve.get("interp", "linear"))
    rows[name] = {"row": row, "v": (row + 0.5) / len(curves)}

  metadata = {
    "rows": rows,
    "row_count": len(curves),
    "sampling": "u = normalised age with a linear CLAMP sampler; v = rows[name].v",
  }
  return texels, metadata


def write_preview(texels: np.ndarray, rows: dict, path: Path) -> None:
  import matplotlib

  matplotlib.use("Agg")
  import matplotlib.pyplot as plt

  values = texels.astype(np.float32)
  names = [name for name, _ in sorted(rows.items(), key=lambda item: item[1]["row"])]
  count, width = values.shape[0], values.shape[1]

  # Reinhard for display so emissive rows above 1.0 still show a gradient.
  rgb = values[..., :3]
  display = rgb / (1.0 + rgb)
  # Scalar rows (G = B = 0) render as greyscale of R, normalised per row.
  scalar_rows = np.all(values[..., 1:3] == 0.0, axis=(1, 2))
  for row in np.flatnonzero(scalar_rows):
    r = values[row, :, 0]
    peak = max(float(np.abs(r).max()), 1e-8)
    display[row] = (r / peak)[:, None]
  display = np.clip(display, 0.0, 1.0)

  fig, ax = plt.subplots(figsize=(10, 0.45 * count + 1.2), constrained_layout=True)
  ax.imshow(display, aspect="auto", interpolation="nearest", extent=[0, 1, count, 0])
  ax.set_yticks(np.arange(count) + 0.5)
  ax.set_yticklabels(names, fontsize=8)
  ax.set_xlabel("normalised age (u)")
  ax.set_title(f"Curve atlas: {count} rows x {width} texels (display tonemapped)")
  fig.savefig(path, dpi=110)
  plt.close(fig)


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("spec", type=Path, help="curve spec .json")
  parser.add_argument("--out", type=Path, default=None,
                      help="output stem; defaults to the spec path with '_rgba16f' appended")
  parser.add_argument("--preview", action="store_true")
  args = parser.parse_args()

  spec = json.loads(args.spec.read_text(encoding="utf-8"))
  texels, metadata = bake(spec)
  out = args.out or args.spec.with_name(args.spec.stem + "_rgba16f")
  out.parent.mkdir(parents=True, exist_ok=True)
  metadata["source"] = args.spec.name
  written = write_raw_texture(out, texels, "DXGI_FORMAT_R16G16B16A16_FLOAT", metadata)
  if args.preview:
    write_preview(texels, metadata["rows"], out.with_suffix(".png"))
  print(f"wrote {out}.bin ({written['byte_size']:,} bytes, {metadata['row_count']} rows) + .json"
        f"{' + .png' if args.preview else ''}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
