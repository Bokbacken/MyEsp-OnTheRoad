// Fixed-pixel adventure geometry: translate the whole game, never scale the canvas.
#pragma once
#include <stdint.h>

namespace AdvUi {
namespace Game {

struct Point { int32_t x, y; };
struct Rect {
    int32_t x, y, w, h;
    bool contains(Point p) const {
        return p.x >= x && p.y >= y && p.x - x < w && p.y - y < h;
    }
};

constexpr int32_t WIDTH = 320, HEIGHT = 480;
constexpr Rect VIEW = {0, 0, WIDTH, 320};
constexpr Rect CONTROLS = {0, VIEW.y + VIEW.h, WIDTH, HEIGHT - VIEW.h};
constexpr Rect JOYSTICK = {80, CONTROLS.y, 160, CONTROLS.h};
constexpr int32_t JOY_CX = JOYSTICK.w / 2, JOY_CY = JOYSTICK.h / 2;
constexpr int32_t JOY_R = 74, JOY_STOP_R = JOY_R * 17 / 53, JOY_KNOB_R = JOY_R * 15 / 53;

inline Point origin(int32_t screenW, int32_t screenH) {
    return {screenW > WIDTH ? (screenW - WIDTH) / 2 : 0,
            screenH > HEIGHT ? (screenH - HEIGHT) / 2 : 0};
}

// Canvas bounds are in LVGL display coordinates; camera offsets are pixels, output is tiles.
inline bool worldPoint(Rect canvas, Point touch, int32_t camX, int32_t camY, int tileSize,
                       float &x, float &y) {
    if (!canvas.contains(touch) || (tileSize != 16 && tileSize != 32)) return false;
    x = float(touch.x - canvas.x + camX) / tileSize;
    y = float(touch.y - canvas.y + camY) / tileSize;
    return true;
}

// Do not clip drags: the full pad rectangle and points beyond the ring use the outer zone.
inline Point joyDelta(Rect area, Point touch) {
    return {touch.x - area.x - JOY_CX, touch.y - area.y - JOY_CY};
}

}  // namespace Game
}  // namespace AdvUi
