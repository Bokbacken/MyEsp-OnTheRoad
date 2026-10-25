// Host test for components/otr_engine/include/otr_joystick.h
#include "check.h"
#include "otr_joystick.h"

using namespace Otr;

static const int R = 54, S = 16;

static void testSample()
{
    JoyConfig c;   // 2 zones, 300 / 150 ms
    JoySample s = joySample(0, 0, R, S, c);
    CHECK(!s.moving && s.zone == 0 && s.intervalMs == 0);
    s = joySample(10, 10, R, S, c);   // |d| = 14.1 < 16
    CHECK(!s.moving);
    s = joySample(0, -20, R, S, c);
    CHECK(s.moving && s.dir == Dir::UP && s.zone == 1 && s.intervalMs == 300);
    s = joySample(0, 50, R, S, c);
    CHECK(s.moving && s.dir == Dir::DOWN && s.zone == 2 && s.intervalMs == 150);
    s = joySample(200, 0, R, S, c);   // Outside the pad = outer zone
    CHECK(s.dir == Dir::RIGHT && s.zone == 2);
    s = joySample(-30, 0, R, S, c);
    CHECK(s.dir == Dir::LEFT && s.zone == 1);
    // Dominant axis
    s = joySample(30, -20, R, S, c);
    CHECK(s.dir == Dir::RIGHT && s.alt == Dir::UP);
    s = joySample(-22, 35, R, S, c);  // 32 degrees off vertical
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

// Finger at `deg` degrees (0 = straight up, clockwise), far out (outer zone).
static JoySample at(double deg, int diag = 0)
{
    JoyConfig c;
    c.diag = (uint8_t)diag;
    const double r = deg * 3.14159265358979 / 180.0;
    return joySample((int)lround(1000.0 * sin(r)), (int)lround(-1000.0 * cos(r)), R, S, c);
}

// v0.10.0 sectors: up = 0 +-30, diagonal = 45 +-15, right = 90 +-30 ...
static void testSectors()
{
    JoySample s = at(0);
    CHECK(s.dir == Dir::UP && s.alt == Dir::UP);
    s = at(29);                                        // Still straight up
    CHECK(s.dir == Dir::UP && s.alt == Dir::UP);
    s = at(31);                                        // Diagonal up-right, vertical dominant
    CHECK(s.dir == Dir::UP && s.alt == Dir::RIGHT);
    s = at(44);
    CHECK(s.dir == Dir::UP && s.alt == Dir::RIGHT);
    s = at(46);
    CHECK(s.dir == Dir::RIGHT && s.alt == Dir::UP);
    s = at(59);                                        // Still diagonal
    CHECK(s.dir == Dir::RIGHT && s.alt == Dir::UP);
    s = at(61);                                        // Straight right
    CHECK(s.dir == Dir::RIGHT && s.alt == Dir::RIGHT);
    s = at(90);
    CHECK(s.dir == Dir::RIGHT && s.alt == Dir::RIGHT);
    s = at(119);
    CHECK(s.dir == Dir::RIGHT && s.alt == Dir::RIGHT);
    s = at(121);
    CHECK(s.dir == Dir::RIGHT && s.alt == Dir::DOWN);
    s = at(149);
    CHECK(s.dir == Dir::DOWN && s.alt == Dir::RIGHT);
    s = at(151);
    CHECK(s.dir == Dir::DOWN && s.alt == Dir::DOWN);
    s = at(209);
    CHECK(s.dir == Dir::DOWN && s.alt == Dir::DOWN);
    s = at(211);
    CHECK(s.dir == Dir::DOWN && s.alt == Dir::LEFT);
    s = at(241);
    CHECK(s.dir == Dir::LEFT && s.alt == Dir::LEFT);
    s = at(299);
    CHECK(s.dir == Dir::LEFT && s.alt == Dir::LEFT);
    s = at(301);
    CHECK(s.dir == Dir::LEFT && s.alt == Dir::UP);
    s = at(329);
    CHECK(s.dir == Dir::UP && s.alt == Dir::LEFT);
    s = at(331);
    CHECK(s.dir == Dir::UP && s.alt == Dir::UP);
    // Exact boundary in integers: 577/1000 is diagonal, 576/1000 is straight
    JoyConfig c;
    CHECK(joySample(577, -1000, R, S, c).alt == Dir::RIGHT);
    CHECK(joySample(576, -1000, R, S, c).alt == Dir::UP);
    // Alternate mode: 25 degrees off vertical (diagonal before v0.10.0) walks straight now
    JoyController j;
    c.diag = 1;
    j.configure(c, R, S);
    j.press((int)lround(50 * sin(25 * 3.14159265358979 / 180)), -(int)lround(50 * cos(25 * 3.14159265358979 / 180)), 0);
    int other = 0;
    for (uint32_t t = 0; t < 2000; t += 5) {
        Dir d;
        if (j.poll(t, true, d) && d != Dir::UP) other++;
    }
    CHECK(other == 0);
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
    // Hold in the outer zone for 1 s: 1 tap step + repeats every 150 ms after 300 ms hold
    j.press(0, -50, 6000);
    int n = countSteps(j, 6000, 7000, true, &d);
    CHECK(d == Dir::UP);
    CHECK(n >= 5 && n <= 6);     // 1 + (700 / 150 = 4.7 -> 4 or 5)
    j.release(7000);
    CHECK(countSteps(j, 7000, 8000) == 0);
    // Hold in the inner zone: slower
    j.press(0, 20, 9000);
    n = countSteps(j, 9000, 10000);
    CHECK(n >= 3 && n <= 4);     // 1 + 700 / 300
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
    testSectors();
    testTapAndHold();
    testBusy();
    testAlternate();
    CHECK_DONE();
}
