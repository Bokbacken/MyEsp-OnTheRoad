// Host tests for the production adventure geometry and its existing joystick engine.
#include "check.h"
#include "adv_game_geometry.h"
#include "otr_joystick.h"
#include <cmath>
#include <initializer_list>
#include <vector>

namespace G = AdvUi::Game;

static void testMap(G::Rect canvas)
{
    for (int ts : {16, 32}) {
        for (G::Point cam : {G::Point{0, 0}, G::Point{37, 91}, G::Point{1024, 1536}}) {
            // Corners, edges and adjacent pixels, including non-tile-aligned camera offsets.
            for (int x : {0, 1, 15, 16, 31, 32, 159, canvas.w - 2, canvas.w - 1}) {
                for (int y : {0, 1, 15, 16, 31, 32, 159, canvas.h - 2, canvas.h - 1}) {
                    float fx = -1, fy = -1;
                    CHECK(G::worldPoint(canvas, {canvas.x + x, canvas.y + y}, cam.x, cam.y, ts, fx, fy));
                    CHECK(fx * ts - cam.x == x && fy * ts - cam.y == y);
                    CHECK(int(std::floor(fx)) == (x + cam.x) / ts);
                    CHECK(int(std::floor(fy)) == (y + cam.y) / ts);
                }
            }
            for (G::Point p : {G::Point{-1, 0}, G::Point{0, -1}, G::Point{canvas.w, 0},
                              G::Point{0, canvas.h}, G::Point{canvas.w - 1, canvas.h},
                              G::Point{canvas.w, canvas.h - 1},
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

static void testJoystick(G::Rect pad)
{
    const auto j = G::joyGeometry(pad);
    const G::Point centre = {pad.x + j.cx, pad.y + j.cy};
    CHECK(G::JOY_R == 74 && G::JOY_STOP_R == 23 && G::JOY_KNOB_R == 20);
    CHECK(pad.contains(centre));
    CHECK(pad.contains({pad.x, pad.y}) && pad.contains({pad.x + pad.w - 1, pad.y + pad.h - 1}));
    CHECK(!pad.contains({pad.x - 1, pad.y}) && !pad.contains({pad.x + pad.w, pad.y}));
    Otr::JoyConfig config;
    for (G::Point local : {G::Point{0, 0}, G::Point{22, 0}, G::Point{23, 0},
                          G::Point{24, 0}, G::Point{48, 0}, G::Point{49, 0},
                          G::Point{73, 0}, G::Point{74, 0}, G::Point{75, 0},
                          G::Point{-80, -80}, G::Point{79, 79}, G::Point{200, -200}}) {
        const auto delta = G::joyDelta(pad, {centre.x + local.x, centre.y + local.y});
        CHECK(delta.x == local.x && delta.y == local.y);
        const auto actual = Otr::joySample(delta.x, delta.y, j.radius, j.stopRadius, config);
        const auto baseline = Otr::joySample(local.x, local.y, j.radius, j.stopRadius, config);
        CHECK(actual.moving == baseline.moving && actual.zone == baseline.zone);
        CHECK(actual.dir == baseline.dir && actual.alt == baseline.alt);
        CHECK(actual.intervalMs == baseline.intervalMs);
    }
    CHECK(!Otr::joySample(j.stopRadius - 1, 0, j.radius, j.stopRadius, config).moving);
    CHECK(Otr::joySample(j.stopRadius, 0, j.radius, j.stopRadius, config).zone == 1);
    const int boundary = j.stopRadius + (j.radius - j.stopRadius + 1) / 2;
    CHECK(Otr::joySample(boundary - 1, 0, j.radius, j.stopRadius, config).zone == 1);
    CHECK(Otr::joySample(boundary, 0, j.radius, j.stopRadius, config).zone == 2);
    CHECK(Otr::joySample(j.radius, 0, j.radius, j.stopRadius, config).zone == 2);
}

static void testBlit(G::Rect view)
{
    constexpr uint16_t key = 0xF81F, untouched = 0xEEEE;
    for (int ts : {16, 32}) {
        std::vector<uint16_t> sprite(size_t(ts) * ts);
        for (size_t i = 0; i < sprite.size(); ++i) sprite[i] = i % 3 ? uint16_t(i) : key;
        // Include row padding, leading/trailing guards and sprites fully/partly outside every edge.
        for (int stride : {view.w, view.w + 3}) {
            const size_t pixels = G::frameBytes(size_t(stride) * 2, view.h) / 2;
            for (int sx : {-ts - 1, -ts, -1, 0, view.w - ts, view.w - 1, view.w, view.w + 1}) {
                for (int sy : {-ts - 1, -ts, -1, 0, view.h - ts, view.h - 1, view.h, view.h + 1}) {
                    for (bool keyed : {false, true}) {
                        std::vector<uint16_t> actual(pixels + 2, untouched), expected = actual;
                        G::blitSprite(actual.data() + 1, view.w, view.h, stride, sprite.data(), ts, sx, sy, keyed, key);
                        // Reference copies individual source pixels, including transparency.
                        for (int y = 0; y < ts; ++y) {
                            for (int x = 0; x < ts; ++x) {
                                const int dx = sx + x, dy = sy + y;
                                const auto value = sprite[y * ts + x];
                                if (dx >= 0 && dx < view.w && dy >= 0 && dy < view.h && (!keyed || value != key))
                                    expected[1 + size_t(dy) * stride + dx] = value;
                            }
                        }
                        CHECK(actual == expected);  // Guards and row padding must remain untouched.
                    }
                }
            }
        }
    }
}

int main()
{
    auto l = G::layout(320, 480);
    CHECK(l.view.x == 0 && l.view.y == 0 && l.view.w == 320 && l.view.h == 320);
    CHECK(l.joystick.x == 80 && l.joystick.y == 320);
    CHECK(G::frameBytes(l.view.w * 2, l.view.h) == 204800);
    l = G::layout(480, 800);  // Logical profile dimensions, not a physical P4 assertion.
    CHECK(l.view.x == 0 && l.view.y == 0 && l.view.w == 480 && l.view.h == 560);
    CHECK(l.controls.h == 240 && l.joystick.w == 240 && l.joystick.x == 120 && l.joystick.y == 560);
    const auto joy = G::joyGeometry(l.joystick);
    CHECK(joy.cx == 120 && joy.cy == 120 && joy.radius == 111 && joy.stopRadius == 34 && joy.knobRadius == 30);
    CHECK(l.controlPx(36) == 54 && l.controlPx(48) == 72);
    CHECK(G::frameBytes(l.view.w * 2, l.view.h) == 537600);
    l = G::layout(481, 801);
    CHECK(l.view.w == 481 && l.view.h == 561 && l.controls.h == 240 && l.joystick.x == 120);
    l = G::layout(240, 320);
    CHECK(l.width == 320 && l.height == 480);  // No new small-screen layout.
    CHECK(G::frameBytes(0, 640) == 0 && G::frameBytes(960, 0) == 0);
    CHECK(G::frameBytes(960, -1) == 0 && G::frameBytes(SIZE_MAX, 640) == 0);
    for (G::Point size : {G::Point{320, 480}, G::Point{480, 800}, G::Point{481, 801}, G::Point{600, 1024}}) {
        const auto layout = G::layout(size.x, size.y);
        CHECK(layout.width == size.x && layout.height == size.y);
        CHECK(layout.view.h + layout.controls.h == size.y && layout.controls.h == size.x / 2);
        CHECK(layout.controls.y == layout.view.y + layout.view.h && layout.controls.w == layout.view.w);
        CHECK(layout.joystick.y == layout.controls.y && layout.joystick.h == layout.controls.h);
        CHECK(layout.joystick.w == layout.controls.h);
        CHECK(layout.controlPx(160) == layout.controls.h);
        CHECK(layout.joystick.x >= 80 && layout.width - layout.joystick.x - layout.joystick.w >= 80);
        testMap(layout.view);
        testJoystick(layout.joystick);
        testBlit(layout.view);
        for (int ts : {16, 32}) {
            for (int extent : {layout.view.w, layout.view.h}) {
                // Small worlds (including maps that fit only at 16 px), exact fit, larger worlds.
                for (int world : {3 * ts, 20 * ts, extent, 64 * ts}) {
                    const int limit = world > extent ? world - extent : 0;
                    CHECK(G::cameraOffset(-1, world, extent) == 0);
                    CHECK(G::cameraOffset(0, world, extent) == 0);
                    CHECK(G::cameraOffset(world, world, extent) == limit);
                    CHECK(G::cameraOffset(limit + 1, world, extent) == limit);
                    CHECK(G::cameraOffset(limit / 2, world, extent) == limit / 2);
                }
            }
        }
    }
    // Actual canvas/pad positions remain authoritative after an additional parent translation.
    testMap({103, 207, 480, 560});
    testJoystick({263, 847, 160, 160});
    CHECK_DONE();
}
