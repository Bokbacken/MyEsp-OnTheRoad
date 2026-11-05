#!/usr/bin/env python3
"""Exercise the actual RTC backends with deterministic wall/monotonic clocks and I2C."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def implementation(relative):
    return re.sub(r"^#(?:include .*|pragma once)$", "", (ROOT / relative).read_text(), flags=re.M)


code = (ROOT / "tests/test_rtc_sync.cpp").read_text()
code = code.replace("// SYNC_UNDER_TEST", implementation("components/my_rtc/include/my_rtc_sync.h"))
for profile, source, board in (
    ("DS3231", "my_rtc.cpp", "P4_43"),
    ("DS3231", "my_rtc.cpp", "ES3C35P"),
    ("PCF85063", "my_rtc_pcf85063.cpp", "S3_35B"),
    ("INTERNAL", "my_rtc_internal.cpp", "P4_43"),
):
    source_code = code.replace("// BACKEND_UNDER_TEST", implementation(f"components/my_rtc/{source}"))
    for hz in (100, 1000):
        binary = ROOT / "tests" / f".test_rtc_sync_{profile}_{board}_{hz}"
        try:
            subprocess.run(
                ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                 "-fsanitize=undefined", f"-DTEST_{profile}", f"-DconfigTICK_RATE_HZ={hz}",
                 f"-DCONFIG_OTR_HW_PROFILE_{board}=1",
                 f"-I{ROOT / 'tests/host_stubs'}",
                 f"-I{ROOT / 'components/my_rtc/include'}",
                 "-x", "c++", "-", "-o", str(binary)],
                input=source_code, text=True, check=True,
            )
            subprocess.run([str(binary)], check=True)
        finally:
            binary.unlink(missing_ok=True)
