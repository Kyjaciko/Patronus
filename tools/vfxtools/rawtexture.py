"""Raw texture writer: <stem>.bin + <stem>.json.

The renderer has no image loader. Baked textures are written as tightly
packed raw texel data in the exact DXGI layout the app uploads with
UpdateSubresources (x fastest, then y, then z), plus a JSON sidecar that
describes the layout so nothing about width/height/format/world bounds has
to be hard-coded twice.

Supported formats and the numpy dtype/shape each expects:

    DXGI_FORMAT_R16G16B16A16_FLOAT   float16, shape [..., 4]
    DXGI_FORMAT_R8G8B8A8_UNORM       uint8,   shape [..., 4]
    DXGI_FORMAT_R8_UNORM             uint8,   shape [...]     (or [..., 1])

Array shape is [depth, height, width, channels] for 3D and
[height, width, channels] for 2D, i.e. C-order with x fastest.
"""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np

_FORMATS = {
  "DXGI_FORMAT_R16G16B16A16_FLOAT": (np.float16, 4),
  "DXGI_FORMAT_R8G8B8A8_UNORM": (np.uint8, 4),
  "DXGI_FORMAT_R8_UNORM": (np.uint8, 1),
}


def bytes_per_texel(dxgi_format: str) -> int:
  dtype, channels = _FORMATS[dxgi_format]
  return np.dtype(dtype).itemsize * channels


def write_raw_texture(
  stem: Path,
  texels: np.ndarray,
  dxgi_format: str,
  extra_metadata: dict | None = None,
) -> dict:
  """Write <stem>.bin and <stem>.json. Returns the metadata written."""
  if dxgi_format not in _FORMATS:
    raise ValueError(f"unsupported format {dxgi_format}")
  dtype, channels = _FORMATS[dxgi_format]

  data = np.asarray(texels)
  if channels == 1 and data.ndim in (2, 3) and data.shape[-1] != 1:
    data = data[..., np.newaxis]
  if data.shape[-1] != channels:
    raise ValueError(
      f"{dxgi_format} needs {channels} channels, got shape {data.shape}"
    )
  if data.ndim == 3:
    depth, height, width = 1, data.shape[0], data.shape[1]
  elif data.ndim == 4:
    depth, height, width = data.shape[0], data.shape[1], data.shape[2]
  else:
    raise ValueError(f"expected [h, w, c] or [d, h, w, c], got {data.shape}")

  data = np.ascontiguousarray(data.astype(dtype))

  texel_size = bytes_per_texel(dxgi_format)
  row_pitch = width * texel_size
  slice_pitch = row_pitch * height
  expected_size = slice_pitch * depth

  stem = Path(stem)
  bin_path = stem.with_suffix(".bin")
  json_path = stem.with_suffix(".json")

  data.tofile(bin_path)
  actual_size = bin_path.stat().st_size
  if actual_size != expected_size:
    raise RuntimeError(
      f"{bin_path}: wrote {actual_size} bytes, expected {expected_size}"
    )

  metadata = {
    "format": dxgi_format,
    "width": width,
    "height": height,
    "depth": depth,
    "bytes_per_texel": texel_size,
    "row_pitch": row_pitch,
    "slice_pitch": slice_pitch,
    "byte_size": expected_size,
    "array_order": "[z, y, x, channel]" if depth > 1 else "[y, x, channel]",
    "bin": bin_path.name,
  }
  if extra_metadata:
    metadata.update(extra_metadata)

  with json_path.open("w", encoding="utf-8") as file:
    json.dump(metadata, file, indent=2)

  return metadata


def read_raw_texture(stem: Path) -> tuple[np.ndarray, dict]:
  """Read <stem>.bin + <stem>.json written by write_raw_texture.

  Returns (texels, metadata). Texels are [d, h, w, c] for volumes and
  [h, w, c] for 2D textures, in the format's native dtype (float16 or
  uint8); callers convert as needed."""
  stem = Path(stem)
  json_path = stem.with_suffix(".json")
  with json_path.open("r", encoding="utf-8") as file:
    metadata = json.load(file)
  dxgi_format = metadata["format"]
  if dxgi_format not in _FORMATS:
    raise ValueError(f"{json_path}: unsupported format {dxgi_format}")
  dtype, channels = _FORMATS[dxgi_format]

  bin_path = stem.parent / metadata.get("bin", stem.with_suffix(".bin").name)
  data = np.fromfile(bin_path, dtype=dtype)
  width, height, depth = metadata["width"], metadata["height"], metadata["depth"]
  expected = width * height * depth * channels
  if data.size != expected:
    raise ValueError(f"{bin_path}: {data.size} elements, expected {expected}")
  if depth > 1:
    return data.reshape(depth, height, width, channels), metadata
  return data.reshape(height, width, channels), metadata


def write_constexpr_header(
  path: Path,
  namespace: str,
  values: dict[str, int | float | str | list],
) -> None:
  """Write a tiny C++ header of constexpr values from a flat dict.

  Generated data, not hand-written code: the app can include it to get the
  world bounds / dimensions of a baked asset without duplicating numbers.
  Lists become std::array-free brace-initialised float arrays.
  """
  lines = [
    "// GENERATED FILE - do not edit. Produced by tools/vfxtools; re-run the",
    "// bake script that made the matching .bin/.json instead.",
    "#pragma once",
    "",
    f"namespace {namespace} {{",
    "",
  ]
  for name, value in values.items():
    if isinstance(value, bool):
      lines.append(f"inline constexpr bool {name} = {'true' if value else 'false'};")
    elif isinstance(value, int):
      lines.append(f"inline constexpr int {name} = {value};")
    elif isinstance(value, float):
      lines.append(f"inline constexpr float {name} = {value!r}f;")
    elif isinstance(value, str):
      escaped = value.replace("\\", "\\\\").replace('"', '\\"')
      lines.append(f'inline constexpr const char* {name} = "{escaped}";')
    elif isinstance(value, (list, tuple)):
      items = ", ".join(f"{float(v)!r}f" for v in value)
      lines.append(f"inline constexpr float {name}[{len(value)}] = {{ {items} }};")
    else:
      raise TypeError(f"{name}: unsupported value type {type(value)}")
  lines += ["", f"}}  // namespace {namespace}", ""]

  Path(path).write_text("\n".join(lines), encoding="utf-8")
