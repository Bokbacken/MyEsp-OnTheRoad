"""Karaoke overload guard: stop only on sustained render overload (owner S3 logs v0.23.18 and v0.26.1)."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
test = r'''
#include <cassert>
#include "kara_overload.h"
int main() {
    using KaraOverload::Guard;
    // Owner log periods: (avg load in 1/100 %, new underruns)
    Guard g;
    assert(!g.shouldStop(3962, 0));
    assert(!g.shouldStop(7811, 1));   // Brief stutter: keep playing
    assert(!g.shouldStop(7230, 1));
    assert(!g.shouldStop(4374, 0));   // Max 372 % but low average: a single spike
    assert(g.shouldStop(10000, 0));
    assert(!g.shouldStop(9999, 4));
    assert(g.shouldStop(9999, 15));   // One very late period is enough
    // v0.23.18 watchdog log: 5 late blocks, then 11 at 112 %
    Guard w;
    assert(!w.shouldStop(8729, 5));
    assert(w.shouldStop(11193, 11));
    // v0.26.1 S3 log, 48 voices, no audible dropout: single strikes are separated by quiet periods
    Guard s;
    assert(!s.shouldStop(7584, 2));
    assert(!s.shouldStop(8887, 3));
    assert(!s.shouldStop(8594, 4));
    assert(!s.shouldStop(8957, 7));
    assert(!s.shouldStop(6000, 1));   // A calm period clears the strike
    assert(!s.shouldStop(8957, 7));
    assert(s.shouldStop(8957, 6));    // Two late periods in a row
    // Paused/reset clears strikes
    Guard r;
    assert(!r.shouldStop(8000, 5));
    r.reset();
    assert(!r.shouldStop(8000, 5));
}
'''
with tempfile.TemporaryDirectory(prefix="otr-kara-overload-") as tmp:
    path = Path(tmp)
    (path / "test.cpp").write_text(test)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", f"-I{ROOT / 'main'}",
                    str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("karaoke overload guard: owner S3 log thresholds passed")
