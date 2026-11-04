#!/usr/bin/env python3
"""Exercise the S3 PMIC driver and link its unsupported-board implementations."""
import pathlib
import subprocess
import tempfile

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

with tempfile.TemporaryDirectory(prefix="otr-board-power-") as directory:
    work = pathlib.Path(directory)
    test = work / "test.cpp"
    test.write_text('''
#include <cassert>
#include "board_power.h"
int main() {
    assert(!BoardPower::begin());
    assert(!BoardPower::resetPanel());
    assert(BoardPower::bus() == nullptr);
    const auto status = BoardPower::status();
    assert(!status.supported && !status.initialized);
    assert(!status.powerButtonPressed && !status.powerOffRequested);
    assert(!BoardPower::powerOff());
    BoardPower::cancelPowerOff();
}
''')
    for profile in ["P4_43", "ES3C35P"]:
        binary = work / profile
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            f"-DCONFIG_OTR_HW_PROFILE_{profile}=1",
            f"-I{ROOT / 'tests/host_stubs'}", f"-I{ROOT / 'main'}",
            str(test), str(ROOT / "main/board_power.cpp"), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
