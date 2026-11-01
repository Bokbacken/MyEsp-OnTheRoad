"""Karaoke overload guard: stop only on sustained render overload (owner S3 log, v0.23.18)."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
test = r'''
#include <cassert>
#include "kara_overload.h"
int main() {
    using KaraOverload::shouldStop;
    // Owner log periods: (avg load in 1/100 %, new underruns)
    assert(!shouldStop(3962, 0));
    assert(!shouldStop(7811, 1));   // Brief stutter: keep playing
    assert(!shouldStop(7230, 1));
    assert(!shouldStop(4374, 0));   // Max 372 % but low average: a single spike
    assert(shouldStop(8729, 5));    // First watchdog period
    assert(shouldStop(11193, 11));
    assert(shouldStop(10000, 0));
    assert(!shouldStop(9999, 4));
}
'''
with tempfile.TemporaryDirectory(prefix="otr-kara-overload-") as tmp:
    path = Path(tmp)
    (path / "test.cpp").write_text(test)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", f"-I{ROOT / 'main'}",
                    str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("karaoke overload guard: owner S3 log thresholds passed")
