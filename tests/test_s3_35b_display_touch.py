#!/usr/bin/env python3
"""Compile/run the native S3 3.5B drivers against bounded host hardware mocks."""
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"
TEMPLATE = (TESTS / "test_s3_35b_display_touch.cpp").read_text()


def main():
    cases = {
        "display": (
            "board_display_s3_35b.cpp",
            ["normal", "missing-power", "failed-power", "memory-frame", "memory-dma", "semaphore",
             "spi", "io", "init-command", "dma-timeout", "lvgl",
             "runtime-timeout", "runtime-queue", "column-error", "bounds", "sleep-errors"],
        ),
        "touch": (
            "board_touch_axs15231b.cpp",
            ["normal", "idle-first", "missing-power", "failed-power", "missing-lvgl", "add-device", "bad-first-report", "pointer"],
        ),
    }
    count = 0
    for kind, (source, scenarios) in cases.items():
        body = re.sub(r"^#include .*?$", "", (ROOT / "main" / source).read_text(), flags=re.M)
        code = TEMPLATE.replace("// DRIVER_UNDER_TEST", body)
        binary = TESTS / f".s3_35b_{kind}_host"
        try:
            subprocess.run(
                ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=undefined", f"-DTEST_{kind.upper()}",
                 "-I", str(TESTS), "-x", "c++", "-", "-o", str(binary)],
                input=code, text=True, check=True, cwd=ROOT,
            )
            for scenario in scenarios:
                subprocess.run([str(binary), scenario], check=True, cwd=ROOT)
                count += 1
        finally:
            binary.unlink(missing_ok=True)
    print(f"S3 3.5B display/touch: {count} host scenarios passed")


if __name__ == "__main__":
    main()
