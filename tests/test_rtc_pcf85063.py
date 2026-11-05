#!/usr/bin/env python3
"""Host regression test for the PCF85063 UTC register encoding."""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
BINARY = ROOT / "tests" / ".test_rtc_pcf85063"

try:
    subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{ROOT / 'components/my_rtc/include'}",
            str(ROOT / "tests/test_rtc_pcf85063.cpp"),
            "-o",
            str(BINARY),
        ],
        check=True,
    )
    subprocess.run([str(BINARY)], check=True)
finally:
    BINARY.unlink(missing_ok=True)
