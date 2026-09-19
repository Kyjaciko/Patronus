#!/usr/bin/env python
"""Previsualise a spell spec on the CPU: window, PNG frames, or GIF.

Checks that the emitter data, curve atlas, curl volume and timeline compose
into the intended effect before anything is written in HLSL. The sim and
the compositing follow the GPU design (see vfxtools/previs.py for the
mapping and the conventions the shaders must match). Not a performance or
soft-particle reference.

Usage (from the repo root):

  python tools/previs_spell.py                       # live window, loops
  python tools/previs_spell.py --frames out/previs   # PNG per frame
  python tools/previs_spell.py --gif docs/media/previs.gif
  python tools/previs_spell.py --stills 0.5 1.3 1.85 2.5 --frames out/stills

Always prints the emitter report (spawned, max alive, dropped, suggested
pool size) at the end; that table is the input to the GPU pool sizes.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

from vfxtools.previs import DT, Previs  # noqa: E402


def parse_resolution(text: str) -> tuple[int, int]:
  w, h = text.lower().split("x")
  return int(w), int(h)


def save_png(image: np.ndarray, path: Path) -> None:
  import matplotlib

  matplotlib.use("Agg")
  import matplotlib.pyplot as plt

  path.parent.mkdir(parents=True, exist_ok=True)
  plt.imsave(path, image)


def run_frames(previs: Previs, out_dir: Path, fps: float, until: float) -> int:
  steps_per_frame = max(1, int(round((1.0 / fps) / DT)))
  frame = 0
  while previs.t < until - 1e-6:
    for _ in range(steps_per_frame):
      previs.step()
    save_png(previs.render(), out_dir / f"frame_{frame:04d}.png")
    frame += 1
  return frame


def run_stills(previs: Previs, times: list[float], out_dir: Path) -> None:
  for target in sorted(times):
    previs.run(until=target)
    save_png(previs.render(), out_dir / f"still_{target:05.2f}s.png")


def run_gif(previs: Previs, path: Path, fps: float, until: float) -> None:
  import matplotlib

  matplotlib.use("Agg")
  import matplotlib.pyplot as plt
  from matplotlib import animation

  try:
    writer = animation.PillowWriter(fps=int(fps))
  except Exception as error:  # pillow missing
    raise SystemExit(f"GIF output needs Pillow (pip install pillow): {error}")

  steps_per_frame = max(1, int(round((1.0 / fps) / DT)))
  fig = plt.figure(figsize=(previs.renderer.width / 100, previs.renderer.height / 100), dpi=100)
  ax = fig.add_axes([0, 0, 1, 1])
  ax.set_axis_off()
  image = ax.imshow(previs.render())
  path.parent.mkdir(parents=True, exist_ok=True)
  with writer.saving(fig, str(path), dpi=100):
    while previs.t < until - 1e-6:
      for _ in range(steps_per_frame):
        previs.step()
      image.set_data(previs.render())
      writer.grab_frame()
  plt.close(fig)


def run_window(make_previs, fps: float, until: float) -> Previs:
  import matplotlib.pyplot as plt
  from matplotlib import animation

  state = {"previs": make_previs()}
  steps_per_frame = max(1, int(round((1.0 / fps) / DT)))
  fig, ax = plt.subplots(figsize=(9.6, 5.4))
  fig.canvas.manager.set_window_title("Patronus spell previs (space: pause, r: restart)")
  ax.set_axis_off()
  image = ax.imshow(state["previs"].render())
  label = ax.text(8, 16, "", color="white", fontsize=9, family="monospace")
  paused = {"value": False}

  def on_key(event):
    if event.key == " ":
      paused["value"] = not paused["value"]
    elif event.key == "r":
      state["previs"] = make_previs()

  fig.canvas.mpl_connect("key_press_event", on_key)

  def update(_):
    p = state["previs"]
    if not paused["value"]:
      if p.t >= until - 1e-6:
        state["previs"] = p = make_previs()
      for _ in range(steps_per_frame):
        p.step()
      image.set_data(p.render())
      label.set_text(f"t = {p.t:5.2f} s   alive = {p.alive_total:6d}")
    return image, label

  anim = animation.FuncAnimation(fig, update, interval=1, blit=False, cache_frame_data=False)
  plt.show()
  del anim
  return state["previs"]


def main() -> int:
  parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument("--spec", type=Path, default=Path("assets/emitters/arcane_bolt.json"))
  parser.add_argument("--res", type=parse_resolution, default=(640, 360), help="WxH, multiples of 4")
  parser.add_argument("--fps", type=float, default=30.0, help="output frame rate (sim always runs at 60 Hz)")
  parser.add_argument("--until", type=float, default=None, help="stop time in seconds (default: timeline end)")
  parser.add_argument("--seed", type=int, default=1)
  mode = parser.add_mutually_exclusive_group()
  mode.add_argument("--frames", type=Path, help="write PNG frames into this directory (with --stills: the stills' directory)")
  mode.add_argument("--gif", type=Path, help="write an animated GIF (needs Pillow)")
  parser.add_argument("--stills", type=float, nargs="+", metavar="T",
                      help="render stills at these times instead of a sequence; output dir from --frames, default out/previs")
  args = parser.parse_args()
  if args.stills and args.gif:
    parser.error("--stills cannot be combined with --gif")

  width, height = args.res

  def make_previs() -> Previs:
    return Previs(args.spec, width, height, args.seed)

  previs = make_previs()
  until = args.until if args.until is not None else previs.timeline.end

  if args.stills:
    out_dir = args.frames or Path("out/previs")
    run_stills(previs, args.stills, out_dir)
    previs.run(until=until)
    print(f"stills written to {out_dir}")
  elif args.frames:
    count = run_frames(previs, args.frames, args.fps, until)
    print(f"{count} frames written to {args.frames}")
  elif args.gif:
    run_gif(previs, args.gif, args.fps, until)
    print(f"gif written to {args.gif}")
  else:
    previs = run_window(make_previs, args.fps, until)

  print()
  print(previs.report())
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
