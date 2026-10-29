#!/usr/bin/env python3
"""Host-test the pure Karaoke lyric-line layout policy."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = r'''
#include <cassert>
#include "karaoke_layout.h"

int main() {
    using KaraokeLayout::visibleLyricLines;
    assert(visibleLyricLines(480, 120, 24, 4) == 4);
    assert(visibleLyricLines(480, 300, 48, 4) == 4);
    assert(visibleLyricLines(800, 132, 28, 4) == 4);
    assert(visibleLyricLines(800, 292, 28, 4) == 9);
    assert(visibleLyricLines(800, 292, 48, 4) == 5);
    assert(visibleLyricLines(800, 0, 28, 4) == 1);
    assert(visibleLyricLines(800, 320, 0, 4) == 1);
    assert(visibleLyricLines(800, 2000, 10, 4) == 32);
}
'''

with tempfile.TemporaryDirectory(prefix="otr-karaoke-line-layout-") as directory:
    source = pathlib.Path(directory) / "test.cpp"
    binary = pathlib.Path(directory) / "test"
    source.write_text(SOURCE)
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        f"-I{ROOT / 'main'}", str(source), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
print("Karaoke lyric layout: S3 fixed count, P4 height/font sizing and edge cases passed")
