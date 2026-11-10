"""Karaoke overload guard: timing hints must not stop playback below 100% average load."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
test = r'''
#include <cassert>
#include <initializer_list>
#include "kara_overload.h"
int main() {
    using KaraOverload::shouldStop;
    assert(!shouldStop(0));
    assert(!shouldStop(9999));
    assert(shouldStop(10000));
    assert(shouldStop(10001));
    // v0.26.2 S3, 48 voices: same-tick write counts +3, +5, +7 are not underruns.
    for (uint32_t avg : {8023u, 9128u, 8762u})
        assert(!shouldStop(avg));
    // v0.23.18 watchdog log: keep the >=100% average guard, not the timing heuristic.
    assert(!shouldStop(8729));
    assert(shouldStop(11193));
}
'''
with tempfile.TemporaryDirectory(prefix="otr-kara-overload-") as tmp:
    path = Path(tmp)
    (path / "test.cpp").write_text(test)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", f"-I{ROOT / 'main'}",
                    str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("karaoke overload guard: average-load threshold and owner S3 regressions passed")
