// Host tests for the production adventure geometry and its existing joystick engine.
#include "check.h"
#include "adv_game_geometry.h"
#include "otr_joystick.h"
#include <cmath>
#include <initializer_list>

namespace G = AdvUi::Game;

static void testMap(G::Point origin)
{
    const G::Rect canvas = {origin.x + G::VIEW.x, origin.y + G::VIEW.y, G::VIEW.w, G::VIEW.h};
    for (int ts : {16, 32}) {
        for (G::Point cam : {G::Point{0, 0}, G::Point{37, 91}, G::Point{1024, 1536}}) {
            // Corners, edges and adjacent pixels, including non-tile-aligned camera offsets.
            for (int x : {0, 1, 15, 16, 31, 32, 159, 318, 319}) {
                for (int y : {0, 1, 15, 16, 31, 32, 159, 318, 319}) {
                    float fx = -1, fy = -1;
                    CHECK(G::worldPoint(canvas, {canvas.x + x, canvas.y + y}, cam.x, cam.y, ts, fx, fy));
                    CHECK(fx * ts - cam.x == x && fy * ts - cam.y == y);
                    CHECK(int(std::floor(fx)) == (x + cam.x) / ts);
                    CHECK(int(std::floor(fy)) == (y + cam.y) / ts);
                }
            }
            for (G::Point p : {G::Point{-1, 0}, G::Point{0, -1}, G::Point{320, 0},
                              G::Point{0, 320}, G::Point{319, 320}, G::Point{320, 319},
                              G::Point{-100, -100}}) {
                float fx = -123, fy = -456;
                CHECK(!G::worldPoint(canvas, {canvas.x + p.x, canvas.y + p.y}, cam.x, cam.y, ts, fx, fy));
                CHECK(fx == -123 && fy == -456);  // Rejected input never supplies a world cell.
            }
        }
    }
    float fx, fy;
    CHECK(G::worldPoint(canvas, {canvas.x, canvas.y}, 37, 91, 16, fx, fy));
    CHECK(fx == 2.3125f && fy == 5.6875f);
    CHECK(G::worldPoint(canvas, {canvas.x + 319, canvas.y + 319}, 37, 91, 32, fx, fy));
    CHECK(fx == 11.125f && fy == 12.8125f);
    CHECK(!G::worldPoint(canvas, {canvas.x, canvas.y}, 0, 0, 0, fx, fy));
}

static void testJoystick(G::Point origin)
{
    const G::Rect pad = {origin.x + G::JOYSTICK.x, origin.y + G::JOYSTICK.y,
                         G::JOYSTICK.w, G::JOYSTICK.h};
    const G::Point centre = {pad.x + G::JOY_CX, pad.y + G::JOY_CY};
    CHECK(G::JOY_R == 74 && G::JOY_STOP_R == 23 && G::JOY_KNOB_R == 20);
    CHECK(pad.contains(centre));
    CHECK(pad.contains({pad.x, pad.y}) && pad.contains({pad.x + 159, pad.y + 159}));
    CHECK(!pad.contains({pad.x - 1, pad.y}) && !pad.contains({pad.x + 160, pad.y}));
    Otr::JoyConfig config;
    for (G::Point local : {G::Point{0, 0}, G::Point{22, 0}, G::Point{23, 0},
                          G::Point{24, 0}, G::Point{48, 0}, G::Point{49, 0},
                          G::Point{73, 0}, G::Point{74, 0}, G::Point{75, 0},
                          G::Point{-80, -80}, G::Point{79, 79}, G::Point{200, -200}}) {
        const auto delta = G::joyDelta(pad, {centre.x + local.x, centre.y + local.y});
        CHECK(delta.x == local.x && delta.y == local.y);
        const auto actual = Otr::joySample(delta.x, delta.y, G::JOY_R, G::JOY_STOP_R, config);
        const auto baseline = Otr::joySample(local.x, local.y, 74, 23, config);
        CHECK(actual.moving == baseline.moving && actual.zone == baseline.zone);
        CHECK(actual.dir == baseline.dir && actual.alt == baseline.alt);
        CHECK(actual.intervalMs == baseline.intervalMs);
    }
    CHECK(!Otr::joySample(22, 0, G::JOY_R, G::JOY_STOP_R, config).moving);
    CHECK(Otr::joySample(23, 0, G::JOY_R, G::JOY_STOP_R, config).zone == 1);
    CHECK(Otr::joySample(48, 0, G::JOY_R, G::JOY_STOP_R, config).zone == 1);
    CHECK(Otr::joySample(49, 0, G::JOY_R, G::JOY_STOP_R, config).zone == 2);
    CHECK(Otr::joySample(74, 0, G::JOY_R, G::JOY_STOP_R, config).zone == 2);
}

int main()
{
    auto p = G::origin(320, 480);
    CHECK(p.x == 0 && p.y == 0);
    p = G::origin(480, 800);  // Logical profile dimensions, not a physical P4 assertion.
    CHECK(p.x == 80 && p.y == 160);
    p = G::origin(481, 801);
    CHECK(p.x == 80 && p.y == 160);
    p = G::origin(240, 320);
    CHECK(p.x == 0 && p.y == 0);  // No new small-screen/landscape layout.
    for (G::Point size : {G::Point{320, 480}, G::Point{480, 800}, G::Point{600, 1024}}) {
        const auto origin = G::origin(size.x, size.y);
        testMap(origin);
        testJoystick(origin);
    }
    // The transform also uses the actual canvas/pad position, not just the planned root.
    testMap({103, 207});
    testJoystick({103, 207});
    CHECK(G::VIEW.w * G::VIEW.h * 2 == 204800);
    CHECK(G::CONTROLS.y == G::VIEW.y + G::VIEW.h);
    CHECK(G::JOYSTICK.y == G::CONTROLS.y && G::JOYSTICK.h == G::CONTROLS.h);
    CHECK_DONE();
}
