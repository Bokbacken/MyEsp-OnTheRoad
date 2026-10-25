// otr_joystick.h - pure joystick logic for the adventure (no LVGL, host-tested by
// tests/test_joystick.cpp).
//
// Geometry: a round pad with outer radius R and a central stop zone of radius S. Inside S
// nothing moves. Between S and R the ring is split into N equally wide speed zones (1..4);
// zone 1 (inner) steps every slowMs, zone N (outer) every fastMs, linearly interpolated
// between them. Touches outside R count as the outer zone.
//
// Direction is always 4-way: the axis with the largest deflection wins ("dominant axis").
// An exact diagonal prefers the vertical axis (up/down before left/right). Sectors, with 0
// degrees straight up: up = 0 +-30, diagonal up-right = 45 +-15, right = 90 +-30, and so on
// (v0.10.0, was 22.5 degrees). Less than 30 degrees off the dominant axis is a straight
// direction, 30..45 degrees is "diagonal". Optional mode ALTERNATE: on a diagonal the steps
// alternate between the two axes, giving a staircase walk. The engine never moves diagonally.
//
// Timing: a press outside the stop zone gives one step at once (a tap = one step). Holding
// longer than holdMs starts auto-repeat at the zone's step interval. Dragging updates
// direction/zone live; dragging back into the stop zone pauses; leaving it again counts as a
// new press. Release stops (a tap step that could not run yet because the hero was still
// animating is kept for a short while so quick taps are never lost).
#pragma once

#include <math.h>
#include <stdint.h>

#include "otr_world.h"

namespace Otr {

struct JoyConfig {
    uint8_t zones = 2;        // 1..4 speed zones
    uint16_t slowMs = 300;    // Step interval of the innermost zone (v0.10.0: was 150)
    uint16_t fastMs = 150;    // Step interval of the outermost zone (v0.10.0: was 75)
    uint16_t holdMs = 300;    // Hold time before auto-repeat (100..600)
    uint8_t diag = 0;         // 0 = dominant axis, 1 = alternate axes on diagonals
};

struct JoySample {
    bool moving = false;      // false = inside the stop zone
    Dir dir = Dir::UP;        // Dominant direction
    Dir alt = Dir::UP;        // Second axis (== dir unless clearly diagonal)
    uint8_t zone = 0;         // 0 = stop, 1..zones
    uint16_t intervalMs = 0;  // Step interval for this zone
};

// tan(30 degrees) * 1000: the boundary between a straight and a diagonal stick.
constexpr long JOY_DIAG_TAN_1000 = 577;

// Step interval of zone z (1..zones).
inline uint16_t joyZoneMs(const JoyConfig &c, int z)
{
    int n = c.zones < 1 ? 1 : (c.zones > 4 ? 4 : c.zones);
    if (z < 1) z = 1;
    if (z > n) z = n;
    if (n == 1) return c.slowMs;
    return (uint16_t)(c.slowMs + ((int)c.fastMs - (int)c.slowMs) * (z - 1) / (n - 1));
}

// dx/dy: finger position relative to the pad centre in screen pixels (y grows downwards).
inline JoySample joySample(int dx, int dy, int radius, int stopR, const JoyConfig &c)
{
    JoySample s;
    long d2 = (long)dx * dx + (long)dy * dy;
    if (radius <= stopR) radius = stopR + 1;
    if (d2 < (long)stopR * stopR) return s;
    int n = c.zones < 1 ? 1 : (c.zones > 4 ? 4 : c.zones);
    float t = (sqrtf((float)d2) - (float)stopR) / (float)(radius - stopR);
    int z = 1 + (int)(t * (float)n);
    if (z > n) z = n;
    s.moving = true;
    s.zone = (uint8_t)z;
    s.intervalMs = joyZoneMs(c, z);
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    Dir h = dx < 0 ? Dir::LEFT : Dir::RIGHT;
    Dir v = dy < 0 ? Dir::UP : Dir::DOWN;
    s.dir = ax > ay ? h : v;
    // Diagonal sector: at least 30 degrees off the dominant axis, minor >= tan(30) ~ 0.577 * major
    long major = ax > ay ? ax : ay, minor = ax > ay ? ay : ax;
    s.alt = (minor * 1000 >= major * JOY_DIAG_TAN_1000) ? (ax > ay ? v : h) : s.dir;
    return s;
}

class JoyController {
public:
    void configure(const JoyConfig &c, int radius, int stopR)
    {
        cfg_ = c;
        radius_ = radius;
        stopR_ = stopR;
    }
    const JoyConfig &config() const { return cfg_; }

    void press(int dx, int dy, uint32_t now)
    {
        down_ = true;
        cur_ = joySample(dx, dy, radius_, stopR_, cfg_);
        if (cur_.moving) engage(now);
    }
    void drag(int dx, int dy, uint32_t now)
    {
        if (!down_) return press(dx, dy, now);
        bool was = cur_.moving;
        cur_ = joySample(dx, dy, radius_, stopR_, cfg_);
        if (cur_.moving && !was) engage(now);
    }
    void release(uint32_t now)
    {
        (void)now;
        down_ = false;
        cur_ = JoySample();
    }
    void cancel()
    {
        down_ = false;
        cur_ = JoySample();
        tapPending_ = false;
    }

    bool held() const { return down_; }
    const JoySample &sample() const { return cur_; }
    // Step interval currently in use (0 when idle) - the UI fits the walk animation into it.
    uint16_t intervalMs() const { return cur_.moving ? cur_.intervalMs : 0; }

    // Call every UI tick. canStep = the hero is idle and may take a step. Returns true and the
    // direction when a step should be taken now.
    bool poll(uint32_t now, bool canStep, Dir &out)
    {
        if (tapPending_) {
            if ((uint32_t)(now - tapAt_) > TAP_KEEP_MS && !down_) {
                tapPending_ = false;
            } else if (canStep) {
                tapPending_ = false;
                out = pick(tapDir_, tapAlt_);
                lastStep_ = now;
                return true;
            }
            return false;
        }
        if (!down_ || !cur_.moving || !canStep) return false;
        if ((uint32_t)(now - engagedAt_) < cfg_.holdMs) return false;
        if ((uint32_t)(now - lastStep_) < cur_.intervalMs) return false;
        out = pick(cur_.dir, cur_.alt);
        lastStep_ = now;
        return true;
    }

    static constexpr uint32_t TAP_KEEP_MS = 400;

private:
    void engage(uint32_t now)
    {
        engagedAt_ = now;
        tapPending_ = true;
        tapAt_ = now;
        tapDir_ = cur_.dir;
        tapAlt_ = cur_.alt;
        flip_ = false;
    }
    Dir pick(Dir d, Dir alt)
    {
        if (cfg_.diag != 1 || d == alt) return d;
        flip_ = !flip_;
        return flip_ ? d : alt;
    }

    JoyConfig cfg_;
    int radius_ = 56, stopR_ = 16;
    bool down_ = false, tapPending_ = false, flip_ = false;
    JoySample cur_;
    Dir tapDir_ = Dir::UP, tapAlt_ = Dir::UP;
    uint32_t engagedAt_ = 0, tapAt_ = 0, lastStep_ = 0;
};

}  // namespace Otr
