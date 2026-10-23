// Host test for main/dice_cup.{h,cpp}
// g++ -std=c++17 -Wall -Wextra -Imain tests/test_cup.cpp main/dice_cup.cpp -o test_cup
#include "dice_cup.h"

#include <math.h>
#include <stdio.h>

using namespace DiceCup;

static int g_pass = 0, g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (c) {                                                         \
            g_pass++;                                                    \
        } else {                                                         \
            g_fail++;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);          \
        }                                                                \
    } while (0)

static const float CUP_TOP = 330.0f;

static void testStill() {
    ShakeTracker t;
    t.begin(160, 400, 1000);
    for (int i = 1; i <= 50; i++) t.move(160, 400, 1000 + i * 16);
    CHECK(t.energy() == 0.0f);
    CHECK(t.reversals() == 0);
    CHECK(t.spinDir() == 0);
    float vx, vy;
    t.velocity(vx, vy);
    CHECK(vx == 0.0f && vy == 0.0f);
    ReleaseInfo r = decideRelease(t, 400, CUP_TOP);
    CHECK(r.kind == Release::NONE);   // Held 800 ms -> not a tap, no swipe
}

static void testBackAndForth() {
    ShakeTracker t;
    uint32_t ms = 0;
    t.begin(160, 400, ms);
    float x = 160;
    for (int sweep = 0; sweep < 8; sweep++) {
        float dir = (sweep % 2 == 0) ? 1.0f : -1.0f;
        for (int i = 0; i < 10; i++) {
            x += dir * 6.0f;
            ms += 16;
            t.move(x, 400, ms);
        }
    }
    CHECK(t.energy() > 400.0f);
    CHECK(t.reversals() >= 6);
    CHECK(t.spinDir() == 0);   // Straight line shaking is not rotation
    t.end();
    ReleaseInfo r = decideRelease(t, 400, CUP_TOP);
    CHECK(r.kind == Release::NONE);
}

static ShakeTracker circle(bool clockwiseOnScreen) {
    ShakeTracker t;
    const float cx = 160, cy = 400, R = 30;
    uint32_t ms = 0;
    // y grows downwards: increasing the angle in (cos, sin) runs clockwise on screen.
    float s = clockwiseOnScreen ? 1.0f : -1.0f;
    t.begin(cx + R, cy, ms);
    for (int i = 1; i <= 72; i++) {   // Two full turns, 10 deg steps
        float a = s * i * (float)M_PI / 18.0f;
        ms += 16;
        t.move(cx + R * cosf(a), cy + R * sinf(a), ms);
    }
    return t;
}

static void testCircle() {
    ShakeTracker cw = circle(true);
    CHECK(cw.spin() > SPIN_MIN_RAD);
    CHECK(cw.spinDir() == 1);
    CHECK(cw.angularVelocity() > 0.0f);
    CHECK(cw.reversals() == 0);
    ShakeTracker ccw = circle(false);
    CHECK(ccw.spin() < -SPIN_MIN_RAD);
    CHECK(ccw.spinDir() == -1);
    CHECK(ccw.angularVelocity() < 0.0f);
    // Spin direction carries into the throw parameters
    ReleaseInfo r{Release::THROW, 0.0f, -1200.0f, 1200.0f};
    ThrowParams pc = mapThrow(r, &cw, 800, 1);
    ThrowParams pcc = mapThrow(r, &ccw, 800, 1);
    CHECK(pc.spinDir == 1 && pcc.spinDir == -1);
    CHECK(pc.curve > 0.0f && pcc.curve < 0.0f);
}

static void testSlowRelease() {
    ShakeTracker t;
    uint32_t ms = 0;
    t.begin(160, 420, ms);
    // Slowly drift upwards (~120 px/s) but stay inside the cup
    for (int i = 1; i <= 40; i++) {
        ms += 16;
        t.move(160, 420 - i * 2.0f, ms);
    }
    ReleaseInfo r = decideRelease(t, t.lastY(), CUP_TOP);
    CHECK(r.kind == Release::NONE);
    // Slow sideways movement and release: no throw either
    ShakeTracker s;
    ms = 0;
    s.begin(120, 400, ms);
    for (int i = 1; i <= 30; i++) {
        ms += 16;
        s.move(120 + i * 2.0f, 400, ms);
    }
    CHECK(decideRelease(s, s.lastY(), CUP_TOP).kind == Release::NONE);
}

static void testFastUp() {
    ShakeTracker t;
    uint32_t ms = 0;
    t.begin(160, 430, ms);
    for (int i = 1; i <= 8; i++) {   // 20 px per 16 ms = 1250 px/s upwards
        ms += 16;
        t.move(160 + i * 3.0f, 430 - i * 20.0f, ms);
    }
    ReleaseInfo r = decideRelease(t, t.lastY(), CUP_TOP);
    CHECK(r.kind == Release::THROW);
    CHECK(r.vy < -THROW_VY);
    CHECK(r.vx > 0.0f);
    ThrowParams p = mapThrow(r, &t, 800, -1);
    CHECK(p.durationMs >= THROW_MIN_MS && p.durationMs <= THROW_MAX_MS);
    CHECK(p.angle > 0.0f && p.angle <= 0.61f);   // Leans right, clamped
    CHECK(p.power > 0.4f && p.power <= 1.0f);
    CHECK(p.hops >= 2 && p.hops <= 4);
    CHECK(p.turns >= 1.0f && p.turns <= 3.0f);

    // Fast but mostly sideways is not a throw
    ShakeTracker s;
    ms = 0;
    s.begin(100, 400, ms);
    for (int i = 1; i <= 8; i++) {
        ms += 16;
        s.move(100 + i * 25.0f, 400 - i * 8.0f, ms);
    }
    CHECK(decideRelease(s, s.lastY(), CUP_TOP).kind == Release::NONE);

    // Finger leaves the cup upwards at moderate speed -> throw (exit rule)
    ShakeTracker e;
    ms = 0;
    e.begin(160, 350, ms);
    for (int i = 1; i <= 10; i++) {   // 4 px / 16 ms = 250 px/s
        ms += 16;
        e.move(160, 350 - i * 4.0f, ms);
    }
    CHECK(e.lastY() < CUP_TOP - EXIT_MARGIN_PX);
    CHECK(decideRelease(e, e.lastY(), CUP_TOP).kind == Release::THROW);

    // Harder swipe -> more power and a longer animation than a soft one
    ReleaseInfo soft{Release::THROW, 0, -600, 600}, hard{Release::THROW, 0, -1900, 1900};
    ThrowParams ps = mapThrow(soft, nullptr, 800, 1), ph = mapThrow(hard, nullptr, 800, 1);
    CHECK(ph.power > ps.power);
    CHECK(ph.reachPx > ps.reachPx);
    CHECK(ph.durationMs >= ps.durationMs);
}

static void testTap() {
    ShakeTracker t;
    t.begin(160, 400, 0);
    t.move(162, 401, 40);
    t.move(163, 402, 120);
    ReleaseInfo r = decideRelease(t, 402, CUP_TOP);
    CHECK(r.kind == Release::TAP);
    ThrowParams d = defaultThrow(800, 1);
    CHECK(d.durationMs >= THROW_MIN_MS && d.durationMs <= THROW_MAX_MS);
    CHECK(d.angle == 0.0f);
    CHECK(d.spinDir == 1);
    CHECK(d.hops >= 2 && d.hops <= 4);
    CHECK(defaultThrow(800, -1).spinDir == -1);
    // Long press without movement is not a tap
    ShakeTracker l;
    l.begin(160, 400, 0);
    l.move(160, 400, 600);
    CHECK(decideRelease(l, 400, CUP_TOP).kind == Release::NONE);
}

static void testWalls() {
    CupParams p;
    Body b[6];
    layout(b, 6, p, 0.3f);
    bool inside = true;
    for (int i = 0; i < 6; i++)
        if (sqrtf(b[i].x * b[i].x + b[i].y * b[i].y) > p.wallR + 0.01f) inside = false;
    CHECK(inside);
    // No overlap in the start layout
    bool overlap = false;
    for (int i = 0; i < 6; i++)
        for (int j = i + 1; j < 6; j++) {
            float dx = b[i].x - b[j].x, dy = b[i].y - b[j].y;
            if (sqrtf(dx * dx + dy * dy) < p.dieR * 1.5f) overlap = true;
        }
    CHECK(!overlap);

    // Violent shaking for 5 s: every die must stay within the wall and speeds bounded
    uint32_t seed = 12345;
    int hits = 0;
    float maxImpact = 0;
    inside = true;
    bool finite = true;
    for (int f = 0; f < 300; f++) {
        seed = seed * 1103515245u + 12345u;
        float dvx = (float)((int)(seed >> 16) % 801 - 400);
        seed = seed * 1103515245u + 12345u;
        float dvy = (float)((int)(seed >> 16) % 801 - 400);
        StepResult r = step(b, 6, 0.016f, dvx, dvy, p);
        hits += r.wallHits + r.dieHits;
        if (r.maxImpact > maxImpact) maxImpact = r.maxImpact;
        for (int i = 0; i < 6; i++) {
            if (sqrtf(b[i].x * b[i].x + b[i].y * b[i].y) > p.wallR + 0.01f) inside = false;
            float v = sqrtf(b[i].vx * b[i].vx + b[i].vy * b[i].vy);
            if (!(v <= p.maxSpeed + 0.01f) || !isfinite(b[i].x) || !isfinite(b[i].y)) finite = false;
        }
    }
    CHECK(inside);
    CHECK(finite);
    CHECK(hits > 0);
    CHECK(maxImpact > p.impactSpeed);

    // Huge dt is clamped -> still inside
    step(b, 6, 2.0f, 5000, -5000, p);
    inside = true;
    for (int i = 0; i < 6; i++)
        if (sqrtf(b[i].x * b[i].x + b[i].y * b[i].y) > p.wallR + 0.01f) inside = false;
    CHECK(inside);

    // At rest without input -> friction brings the dice (almost) to a stop
    for (int f = 0; f < 400; f++) step(b, 6, 0.016f, 0, 0, p);
    float vmax = 0;
    for (int i = 0; i < 6; i++) {
        float v = sqrtf(b[i].vx * b[i].vx + b[i].vy * b[i].vy);
        if (v > vmax) vmax = v;
    }
    CHECK(vmax < 20.0f);
    // Single die works too
    Body one[1];
    layout(one, 1, p, 0);
    StepResult r1 = step(one, 1, 0.016f, 300, 0, p);
    CHECK(r1.dieHits == 0);
}

int main() {
    testStill();
    testBackAndForth();
    testCircle();
    testSlowRelease();
    testFastUp();
    testTap();
    testWalls();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
