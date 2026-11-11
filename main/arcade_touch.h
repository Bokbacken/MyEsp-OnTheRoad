#pragma once

#include <stdint.h>
#include <cmath>

namespace ArcadeTouch {

struct Point {
    int32_t x;
    int32_t y;
};

struct Motion {
    float x = 0, y = 0;
    float vx = 0, vy = 0;
};

inline float approach(float value, float target, float delta)
{
    return value < target ? std::fmin(value + delta, target) : std::fmax(value - delta, target);
}

// Signed horizontal speed, with a central 20% stop zone and full speed at the ends.
inline float throttle(int32_t y, int32_t height)
{
    if (height <= 1) return 0;
    const float offset = 1.0f - 2.0f * y / (height - 1);
    if (std::fabs(offset) <= 0.2f) return 0;
    const float speed = std::fmin(1.0f, (std::fabs(offset) - 0.2f) / 0.8f);
    return offset > 0 ? speed : -speed;
}

inline void moveShip(Motion &ship, float targetY, float speed, int32_t width,
                     int32_t height, int32_t margin, uint32_t elapsedMs)
{
    const float dt = elapsedMs / 1000.0f;
    const float maxX = width - margin - 1, maxY = height - margin - 1;
    targetY = std::fmax(float(margin), std::fmin(maxY, targetY));
    ship.vx = approach(ship.vx, speed * width * 0.65f, width * 2.2f * dt);
    const float error = targetY - ship.y;
    const float desiredY = std::fmax(-height * 1.4f, std::fmin(height * 1.4f, error * 6.0f));
    ship.vy = approach(ship.vy, desiredY, height * 5.0f * dt);
    const float nextY = ship.y + ship.vy * dt;
    if (error != 0 && (targetY - nextY) * error <= 0) {
        ship.y = targetY;
        ship.vy = 0;
    } else {
        ship.y = nextY;
    }
    ship.x += ship.vx * dt;
    if (ship.x < margin) { ship.x = float(margin); ship.vx = 0; }
    if (ship.x > maxX) { ship.x = maxX; ship.vx = 0; }
    if (ship.y < margin) { ship.y = float(margin); ship.vy = 0; }
    if (ship.y > maxY) { ship.y = maxY; ship.vy = 0; }
}

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
