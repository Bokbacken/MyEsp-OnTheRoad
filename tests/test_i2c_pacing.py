#!/usr/bin/env python3
"""Exercise the actual shared-bus pacing implementation with concurrent callers."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "components/otr_i2c/otr_i2c.cpp").read_text()
source = re.sub(r"^#include .*?$", "", source, flags=re.M)
code = (ROOT / "tests/test_i2c_pacing.cpp").read_text().replace("// PACING_UNDER_TEST", source)

with tempfile.TemporaryDirectory(prefix="otr-i2c-pacing-") as directory:
    for profile in ("S3_35B", "P4_43", "ES3C35P"):
        binary = Path(directory) / profile
        subprocess.run(
            ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=undefined", "-pthread", f"-DCONFIG_OTR_HW_PROFILE_{profile}=1",
             f"-I{ROOT / 'tests/host_stubs'}", f"-I{ROOT / 'components/otr_i2c/include'}",
             "-x", "c++", "-", "-o", str(binary)],
            input=code, text=True, check=True,
        )
        subprocess.run([str(binary)], check=True)

for relative in ("main/board_power.cpp", "main/board_touch_axs15231b.cpp",
                 "main/board_audio.cpp", "components/my_rtc/my_rtc_pcf85063.cpp"):
    text = (ROOT / relative).read_text()
    assert not re.search(r"\bi2c_master_(?:transmit|receive|transmit_receive|probe)\s*\(", text), relative
    assert "OtrI2c::" in text, relative
