#!/usr/bin/env python3
"""Verify the board descriptor at the ESP-IDF application metadata offset."""
import argparse
from pathlib import Path
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("image", type=Path)
parser.add_argument("board", choices=("ES3C35P", "P4_43", "S3_35B"))
args = parser.parse_args()
image = args.image.read_bytes()
offset = 24 + 8 + 256  # esp_image_header_t, first segment header, esp_app_desc_t.
expected = struct.pack("<I12s", 0x4252544F, args.board.encode("ascii"))
if not image or image[0] != 0xE9 or image[offset:offset + len(expected)] != expected:
    raise SystemExit(f"{args.image}: missing or incorrect {args.board} board descriptor")
print(f"{args.image}: verified {args.board} board descriptor at {offset}")
