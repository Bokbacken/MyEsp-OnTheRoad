#pragma once

#include <stdint.h>

namespace ArcadeTouch {

struct Point {
    int32_t x;
    int32_t y;
};

inline Point clampToField(int32_t x, int32_t y, int32_t width, int32_t height, int32_t margin)
{
    const int32_t maxX = width > margin * 2 ? width - margin - 1 : 0;
    const int32_t maxY = height > margin * 2 ? height - margin - 1 : 0;
    if (x < margin) x = margin;
    if (x > maxX) x = maxX;
    if (y < margin) y = margin;
    if (y > maxY) y = maxY;
    return {x, y};
}

inline bool circleHitsGate(int32_t x, int32_t y, int32_t radius,
                           int32_t gateX, int32_t gateWidth, int32_t fieldHeight,
                           int32_t gapCenter, int32_t gapHeight)
{
    const int32_t gapTop = gapCenter - gapHeight / 2;
    const int32_t gapBottom = gapTop + gapHeight;
    const int32_t nearestX = x < gateX ? gateX : (x > gateX + gateWidth ? gateX + gateWidth : x);
    if (nearestX == x) return y - radius < gapTop || y + radius > gapBottom;
    const int32_t nearestYTop = y < 0 ? 0 : (y > gapTop ? gapTop : y);
    const int32_t nearestYBottom = y < gapBottom ? gapBottom : (y > fieldHeight ? fieldHeight : y);
    const int32_t dx = x - nearestX;
    const int32_t dyTop = y - nearestYTop;
    const int32_t dyBottom = y - nearestYBottom;
    return dx * dx + dyTop * dyTop <= radius * radius ||
           dx * dx + dyBottom * dyBottom <= radius * radius;
}

}  // namespace ArcadeTouch
