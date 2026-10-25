// Host test for components/otr_engine/include/otr_joystick.h
#include "check.h"
#include "otr_joystick.h"

using namespace Otr;

static const int R = 54, S = 16;

static void testSample()
{
    JoyConfig c;   // 2 zones, 150 / 75 ms
    JoySample s = joySample(0, 0, R, S, c);
    CHECK(!s.moving && s.zone == 0 && s.intervalMs == 0);
    s = joySample(10, 10, R, S, c);   // |d| = 14.1 < 16
    CHECK(!s.moving);
    s = joySample(0, -20, R, S, c);
    CHECK(s.moving && s.dir == Dir::UP && s.zone == 1 && s.intervalMs == 150);
    s = joySample(0, 50, R, S, c);
    CHECK(s.moving && s.dir == Dir::DOWN && s.zone == 2 && s.intervalMs == 75);
    s = joySample(200, 0, R, S, c);   // Outside the pad = outer zone
    CHECK(s.dir == Dir::RIGHT && s.zone == 2);
    s = joySample(-30, 0, R, S, c);
    CHECK(s.dir == Dir::LEFT && s.zone == 1);
    // Dominant axis
    s = joySample(30, -20, R, S, c);
    CHECK(s.dir == Dir::RIGHT && s.alt == Dir::UP);
    s = joySample(-20, 35, R, S, c);
    CHECK(s.dir == Dir::DOWN && s.alt == Dir::LEFT);
    s = joySample(40, -5, R, S, c);   // Nearly straight: no second axis
    CHECK(s.dir == Dir::RIGHT && s.alt == Dir::RIGHT);
    s = joySample(25, -25, R, S, c);  // Exact diagonal prefers vertical
    CHECK(s.dir == Dir::UP && s.alt == Dir::RIGHT);
    // Zone interpolation
    JoyConfig c4;
    c4.zones = 4; c4.slowMs = 200; c4.fastMs = 80;
    CHECK(joyZoneMs(c4, 1) == 200 && joyZoneMs(c4, 2) == 160 && joyZoneMs(c4, 3) == 120 && joyZoneMs(c4, 4) == 80);
    CHECK(joyZoneMs(c4, 0) == 200 && joyZoneMs(c4, 9) == 80);
    s = joySample(0, 17, R, S, c4);
    CHECK(s.zone == 1);
    s = joySample(0, 27, R, S, c4);   // t = 11/38 -> zone 2
    CHECK(s.zone == 2);
    s = joySample(0, 53, R, S, c4);
    CHECK(s.zone == 4);
    JoyConfig c1;
    c1.zones = 1;
    s = joySample(0, 50, R, S, c1);
    CHECK(s.zone == 1 && s.intervalMs == c1.slowMs);
    JoyConfig c0;
    c0.zones = 0;   // Clamped to 1
    CHECK(joySample(0, 50, R, S, c0).zone == 1);
}

static int countSteps(JoyController &j, uint32_t from, uint32_t to, bool canStep = true, Dir *last = nullptr)
{
    int n = 0;
    for (uint32_t t = from; t < to; t += 5) {
        Dir d;
        if (j.poll(t, canStep, d)) { n++; if (last) *last = d; }
    }
    return n;
}

static void testTapAndHold()
{
    JoyController j;
    JoyConfig c;
    j.configure(c, R, S);
    Dir d = Dir::UP;
    // Tap: exactly one step
    j.press(40, 0, 1000);
    CHECK(countSteps(j, 1000, 1100, true, &d) == 1);
    CHECK(d == Dir::RIGHT);
    j.release(1100);
    CHECK(countSteps(j, 1100, 3000) == 0);
    // Tap in stop zone: nothing
    j.press(2, 3, 4000);
    CHECK(countSteps(j, 4000, 5000) == 0);
    j.release(5000);
    // Hold in the outer zone for 1 s: 1 tap step + repeats every 75 ms after 300 ms hold
    j.press(0, -50, 6000);
    int n = countSteps(j, 6000, 7000, true, &d);
    CHECK(d == Dir::UP);
    CHECK(n >= 10 && n <= 11);   // 1 + (700 / 75 = 9.3 -> 9 or 10)
    j.release(7000);
    CHECK(countSteps(j, 7000, 8000) == 0);
    // Hold in the inner zone: slower
    j.press(0, 20, 9000);
    n = countSteps(j, 9000, 10000);
    CHECK(n >= 5 && n <= 6);     // 1 + 700 / 150
    // Drag to stop zone pauses, back out restarts with a tap step
    j.drag(0, 2, 10000);
    CHECK(countSteps(j, 10000, 11000) == 0);
    j.drag(-50, 0, 11000);
    CHECK(countSteps(j, 11000, 11100, true, &d) == 1 && d == Dir::LEFT);
    // Drag to another direction keeps repeating, new direction
    CHECK(countSteps(j, 11100, 11400) >= 1);
    j.drag(0, 50, 11400);
    countSteps(j, 11400, 11600, true, &d);
    CHECK(d == Dir::DOWN);
    j.release(11600);
}

static void testBusy()
{
    JoyController j;
    JoyConfig c;
    j.configure(c, R, S);
    // Tap while the hero is animating: the step runs as soon as it is idle
    j.press(40, 0, 1000);
    j.release(1050);
    CHECK(countSteps(j, 1000, 1200, false) == 0);
    CHECK(countSteps(j, 1200, 1300, true) == 1);
    // ... but not if the hero stayed busy too long
    j.press(40, 0, 2000);
    j.release(2050);
    CHECK(countSteps(j, 2000, 2000 + JoyController::TAP_KEEP_MS + 50, false) == 0);
    CHECK(countSteps(j, 2500, 2600, true) == 0);
    // Busy hero during auto-repeat: no burst of queued steps afterwards
    j.press(0, -50, 3000);
    countSteps(j, 3000, 3010);
    CHECK(countSteps(j, 3010, 4000, false) == 0);
    CHECK(countSteps(j, 4000, 4010, true) == 1);
    j.cancel();
    CHECK(!j.held());
    CHECK(countSteps(j, 4010, 5000) == 0);
}

static void testAlternate()
{
    JoyController j;
    JoyConfig c;
    c.diag = 1;
    j.configure(c, R, S);
    j.press(30, -30, 0);   // Up-right diagonal
    int up = 0, right = 0;
    for (uint32_t t = 0; t < 2000; t += 5) {
        Dir d;
        if (j.poll(t, true, d)) { if (d == Dir::UP) up++; else if (d == Dir::RIGHT) right++; }
    }
    CHECK(up > 5 && right > 5 && (up - right <= 1 && right - up <= 1));
    j.release(2000);
    // Straight stick in alternate mode stays straight
    j.press(0, -50, 3000);
    Dir d;
    int other = 0;
    for (uint32_t t = 3000; t < 4000; t += 5)
        if (j.poll(t, true, d) && d != Dir::UP) other++;
    CHECK(other == 0);
    // Dominant mode (default) never alternates
    JoyController k;
    k.configure(JoyConfig(), R, S);
    k.press(35, -25, 0);
    other = 0;
    for (uint32_t t = 0; t < 1000; t += 5)
        if (k.poll(t, true, d) && d != Dir::RIGHT) other++;
    CHECK(other == 0);
}

int main()
{
    testSample();
    testTapAndHold();
    testBusy();
    testAlternate();
    CHECK_DONE();
}
