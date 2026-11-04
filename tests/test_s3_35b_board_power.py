#!/usr/bin/env python3
"""Compile and exercise board_power.cpp against a deterministic I2C/FreeRTOS fake."""
import pathlib
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
BINARY = ROOT / "tests" / ".test_s3_35b_board_power"

try:
    subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DCONFIG_OTR_HW_PROFILE_S3_35B=1",
            f"-I{ROOT / 'tests/host_stubs'}",
            f"-I{ROOT / 'main'}",
            str(ROOT / "tests/test_s3_35b_board_power.cpp"),
            "-o",
            str(BINARY),
        ],
        check=True,
    )
    subprocess.run([str(BINARY)], check=True)
finally:
    BINARY.unlink(missing_ok=True)
