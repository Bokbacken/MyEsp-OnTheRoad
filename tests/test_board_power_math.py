#!/usr/bin/env python3
"""Host regression test for AXP2101 voltage and fuel-gauge decoding."""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
BINARY = ROOT / "tests" / ".test_board_power_math"

try:
    subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{ROOT / 'main'}",
            str(ROOT / "tests/test_board_power_math.cpp"),
            "-o",
            str(BINARY),
        ],
        check=True,
    )
    subprocess.run([str(BINARY)], check=True)
finally:
    BINARY.unlink(missing_ok=True)
