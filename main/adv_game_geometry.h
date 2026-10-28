// Adventure viewport and fixed-pixel controls; world art is never stretched.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace AdvUi {
namespace Game {

struct Point { int32_t x, y; };
struct Rect {
    int32_t x, y, w, h;
    bool contains(Point p) const {
        return p.x >= x && p.y >= y && p.x - x < w && p.y - y < h;
    }
};

constexpr int32_t JOY_SIZE = 160;
constexpr int32_t JOY_CX = JOY_SIZE / 2, JOY_CY = JOY_SIZE / 2;
constexpr int32_t JOY_R = 74, JOY_STOP_R = JOY_R * 17 / 53, JOY_KNOB_R = JOY_R * 15 / 53;

struct Layout {
    int32_t width, height;
    Rect view, controls, joystick;
};

inline Layout layout(int32_t screenW, int32_t screenH) {
    // Preserve the portrait baseline on unsupported smaller displays.
    const int32_t w = screenW < 320 ? 320 : screenW;
    const int32_t h = screenH < 480 ? 480 : screenH;
    return {w, h, {0, 0, w, h - JOY_SIZE}, {0, h - JOY_SIZE, w, JOY_SIZE},
            {(w - JOY_SIZE) / 2, h - JOY_SIZE, JOY_SIZE, JOY_SIZE}};
}

inline size_t frameBytes(size_t strideBytes, int32_t height) {
    if (!strideBytes || height <= 0 || strideBytes > UINT32_MAX / size_t(height)) return 0;
    return strideBytes * size_t(height);
}

inline int32_t cameraOffset(int32_t desired, int32_t worldPixels, int32_t viewPixels) {
    const int32_t limit = worldPixels > viewPixels ? worldPixels - viewPixels : 0;
    return desired < 0 ? 0 : desired > limit ? limit : desired;
}

// RGB565 canvas stride can include LVGL padding, unlike the tightly packed tile sprites.
inline void blitSprite(uint16_t *frame, int32_t width, int32_t height, size_t stridePixels,
                       const uint16_t *sprite, int tileSize, int sx, int sy, bool keyed, uint16_t key) {
    const int x0 = sx < 0 ? -sx : 0, y0 = sy < 0 ? -sy : 0;
    const int x1 = sx + tileSize > width ? width - sx : tileSize;
    const int y1 = sy + tileSize > height ? height - sy : tileSize;
    if (x0 >= x1 || y0 >= y1) return;
    for (int y = y0; y < y1; ++y) {
        const uint16_t *s = sprite + y * tileSize + x0;
        uint16_t *d = frame + size_t(sy + y) * stridePixels + (sx + x0);
        if (!keyed) {
            memcpy(d, s, size_t(x1 - x0) * sizeof(uint16_t));
        } else {
            for (int x = x0; x < x1; ++x, ++s, ++d)
                if (*s != key) *d = *s;
        }
    }
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
