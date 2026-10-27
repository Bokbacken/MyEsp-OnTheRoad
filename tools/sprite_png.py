#!/usr/bin/env python3
"""sprite_png.py - pack the raw RGBA dumps from tools/sprite_export into PNGs.

tools/sprite_export writes one <name>.rgba file per sprite (32x32, RGBA8888, the
transparent KEY colour already written as alpha 0) plus a manifest.txt listing
"<name> <w> <h>". This script turns every .rgba in the input dir into a
transparent <name>.png in the output dir, so the result is a ready-to-copy set
of the engine's STANDARD 32x32 sprites (a faithful export of what the device
draws procedurally, NOT hand-drawn).

Usage:
    python3 tools/sprite_png.py <raw_dir> <png_out_dir>

Requires Pillow (pip install pillow).
"""
import os
import sys

from PIL import Image


def pack(raw_dir, out_dir):
    manifest = os.path.join(raw_dir, "manifest.txt")
    if not os.path.isfile(manifest):
        sys.exit(f"no manifest.txt in {raw_dir} - run tools/sprite_export first")
    os.makedirs(out_dir, exist_ok=True)
    n = 0
    with open(manifest) as f:
        for line in f:
            parts = line.split()
            if len(parts) != 3:
                continue
            name, w, h = parts[0], int(parts[1]), int(parts[2])
            raw = os.path.join(raw_dir, name + ".rgba")
            if not os.path.isfile(raw):
                print(f"  skip {name}: missing {raw}", file=sys.stderr)
                continue
            with open(raw, "rb") as rf:
                data = rf.read()
            if len(data) != w * h * 4:
                print(f"  skip {name}: size {len(data)} != {w*h*4}", file=sys.stderr)
                continue
            img = Image.frombytes("RGBA", (w, h), data)
            img.save(os.path.join(out_dir, name + ".png"))
            n += 1
    print(f"wrote {n} PNGs to {out_dir}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: sprite_png.py <raw_dir> <png_out_dir>")
    pack(sys.argv[1], sys.argv[2])
