#!/usr/bin/env python3
"""Host checks for Arcade touch steering and the first test course."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "main/ui_arcade.cpp").read_text()
assert "Touch::contacts()" in source and "Touch::setMultiTouch(true)" in source
assert "ArcadeTouch::clampToField" in source
assert "FIRE_INTERVAL_MS" in source and "BULLET_SPEED" in source
assert "ArcadeTouch::circleHitsGate" in source
assert "Display::setLandscape(true)" in source

test = r'''
#include <cassert>
#include "arcade_touch.h"
int main() {
    using ArcadeTouch::circleHitsGate;
    using ArcadeTouch::clampToField;

    assert(ArcadeTouch::throttle(0, 201) == 1);
    assert(ArcadeTouch::throttle(200, 201) == -1);
    assert(ArcadeTouch::throttle(100, 201) == 0);
    assert(ArcadeTouch::throttle(90, 201) == 0);
    assert(ArcadeTouch::throttle(110, 201) == 0);
    assert(ArcadeTouch::throttle(60, 201) > 0);
    assert(ArcadeTouch::throttle(140, 201) < 0);
    ArcadeTouch::Motion ship{150, 100, 0, 0};
    ArcadeTouch::moveShip(ship, 14, 1, 320, 200, 14, 33);
    assert(ship.y < 100 && ship.y > 95 && ship.vy < 0);
    assert(ship.x > 150 && ship.x < 155 && ship.vx > 0);
    for (int i = 0; i < 100; ++i)
        ArcadeTouch::moveShip(ship, 14, 1, 320, 200, 14, 33);
    assert(ship.y >= 14 && ship.y < 15 && ship.x == 305);
    for (int i = 0; i < 100; ++i)
        ArcadeTouch::moveShip(ship, 185, -1, 320, 200, 14, 33);
    assert(ship.y <= 185 && ship.y > 184 && ship.x == 14);
    ship = {150, 100, 120, 0};
    ArcadeTouch::moveShip(ship, 100, 0, 320, 200, 14, 33);
    assert(ship.vx > 0 && ship.vx < 120); // Brake rather than stop instantly.
    for (int i = 0; i < 20; ++i)
        ArcadeTouch::moveShip(ship, 100, 0, 320, 200, 14, 33);
    assert(ship.vx == 0);
    const float stoppedX = ship.x;
    ArcadeTouch::moveShip(ship, 100, 0, 320, 200, 14, 33);
    assert(ship.x == stoppedX);

    auto p = clampToField(-10, -20, 480, 240, 14);
    assert(p.x == 14 && p.y == 14);
    p = clampToField(1000, 1000, 480, 240, 14);
    assert(p.x == 465 && p.y == 225);
    p = clampToField(20, 80, 480, 240, 14);
    assert(p.x == 20 && p.y == 80);

    // A ship centered in the opening clears the gate; the blocked sections collide.
    assert(!circleHitsGate(110, 120, 10, 100, 20, 240, 120, 80));
    assert(circleHitsGate(110, 40, 10, 100, 20, 240, 120, 80));
    assert(circleHitsGate(110, 200, 10, 100, 20, 240, 120, 80));
    // A circle just outside a gate can still clip its edge.
    assert(circleHitsGate(90, 40, 11, 100, 20, 240, 120, 80));
    assert(!circleHitsGate(80, 120, 10, 100, 20, 240, 120, 80));
}
'''
with tempfile.TemporaryDirectory(prefix="otr-arcade-touch-") as tmp:
    work = Path(tmp)
    test_cpp = work / "test.cpp"
    binary = work / "test"
    test_cpp.write_text(test)
    subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    f"-I{ROOT / 'main'}", str(test_cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Arcade touch steering and gate collision geometry passed")
