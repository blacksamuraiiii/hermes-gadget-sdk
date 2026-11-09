"""Turn the PPM frames written by tools/dump_emoji.cpp into assets/emoji/*.gif.

    g++ -std=c++17 -I firmware/core/include tools/dump_emoji.cpp \\
        build-emoji/core/libhg_core.a -o /tmp/dump_emoji
    /tmp/dump_emoji                  # 20 looks x 20 frames -> /tmp/emoji-frames/*.ppm
    python tools/make_emoji_gifs.py  # -> assets/emoji/<look>.gif
"""
from __future__ import annotations

import glob
import os
import pathlib

from PIL import Image

LOOKS = ["neutral", "happy", "sad", "thinking", "sleeping", "listening", "speaking", "silly",
         "laughing", "loving", "angry", "shocked", "embarrassed", "winking", "excited", "bored",
         "cool", "hi", "connecting", "disconnected"]


def main() -> None:
    out_dir = pathlib.Path(__file__).resolve().parents[1] / "assets" / "emoji"
    out_dir.mkdir(parents=True, exist_ok=True)
    for name in LOOKS:
        frames = sorted(glob.glob(f"/tmp/emoji-frames/{name}-*.ppm"))
        if not frames:
            print(f"skip {name}: no frames under /tmp/emoji-frames/")
            continue
        imgs = [Image.open(f).convert("P", palette=Image.ADAPTIVE, colors=16) for f in frames]
        out = out_dir / f"{name}.gif"
        imgs[0].save(out, save_all=True, append_images=imgs[1:], duration=100, loop=0, disposal=2)
        print(os.path.getsize(out), out)


if __name__ == "__main__":
    main()
