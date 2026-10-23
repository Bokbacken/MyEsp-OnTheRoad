// dice_cup.h - pure gesture and physics maths for the dice cup ("bägaren"), no LVGL/ESP
// dependencies so it can be unit tested on the host (tests/test_cup.cpp).
//
// Coordinates are screen pixels (x right, y DOWN), times in milliseconds. In this frame a
// positive cross product of two consecutive motion vectors means a CLOCKWISE turn on screen.
//
// The gesture only shapes the throw animation; the dice values always come from the game
// engine (YatzyGame::roll()).
#pragma once

#include <stdint.h>

namespace DiceCup {

// --- Thresholds (also used by the UI and documented in README/ROADMAP) ------------------
constexpr float SEG_PX = 6.0f;            // Finger path is split into segments of >= 6 px
constexpr float REVERSAL_DEG = 120.0f;    // Turn between segments > 120 deg = direction reversal
constexpr float SPIN_MAX_DEG = 100.0f;    // Turns below 100 deg count towards rotation
constexpr float SPIN_MIN_RAD = 3.14159265f;   // >= half a turn in one direction = "spinning"
constexpr uint32_t VEL_WINDOW_MS = 80;    // Release velocity is measured over the last 80 ms
constexpr float THROW_VY = 500.0f;        // Upward speed (px/s) that counts as a throw swipe
constexpr float THROW_MAX_SIDE = 1.2f;    // |vx| <= 1.2 * |vy|  (within ~50 deg of straight up)
constexpr float EXIT_VY = 150.0f;         // Finger left the cup upwards with >= 150 px/s
constexpr float EXIT_MARGIN_PX = 8.0f;    // ... and is at least 8 px above the cup rim
constexpr uint32_t TAP_MAX_MS = 350;      // Tap: short press ...
constexpr float TAP_MAX_PX = 10.0f;       // ... that stays within 10 px of the press point
constexpr float POWER_FULL_V = 2000.0f;   // Release speed giving full throw power
constexpr float SHAKE_FULL_PX = 1600.0f;  // Shake path length giving full shake energy
constexpr uint32_t THROW_MIN_MS = 600, THROW_MAX_MS = 1400;   // Throw animation length range

// --- Shake / swipe tracking -------------------------------------------------------------
class ShakeTracker {
public:
    static constexpr int RING = 12;

    void begin(float x, float y, uint32_t ms);
    void move(float x, float y, uint32_t ms);   // Every touch sample while pressed (also when still)

    float energy() const { return m_energy; }            // Sum of |delta| in px
    int reversals() const { return m_reversals; }        // Back-and-forth direction changes
    float spin() const { return m_spin; }                // Signed accumulated turn (rad), + = clockwise
    int spinDir() const;                                 // +1 clockwise, -1 counter-clockwise, 0 none
    float angularVelocity() const { return m_angVel; }   // Smoothed signed turn rate (rad/s)
    void velocity(float &vx, float &vy) const;           // Over the last VEL_WINDOW_MS (px/s)
    uint32_t duration() const { return m_lastMs - m_startMs; }
    float maxDistance() const { return m_maxDist; }      // Max distance from the press point
    float lastX() const { return m_lastX; }
    float lastY() const { return m_lastY; }
    bool active() const { return m_active; }
    void end() { m_active = false; }

private:
    struct Sample { float x, y; uint32_t ms; };
    Sample m_ring[RING] = {};
    int m_head = 0, m_count = 0;
    bool m_active = false;
    float m_startX = 0, m_startY = 0, m_lastX = 0, m_lastY = 0;
    uint32_t m_startMs = 0, m_lastMs = 0, m_segStartMs = 0;
    float m_segX = 0, m_segY = 0;          // Segment being accumulated
    float m_prevX = 0, m_prevY = 0;        // Previous finished segment (unit vector)
    bool m_prevValid = false;
    float m_energy = 0, m_spin = 0, m_angVel = 0, m_maxDist = 0;
    int m_reversals = 0;
};

// --- Release decision --------------------------------------------------------------------
enum class Release : uint8_t { NONE, TAP, THROW };

struct ReleaseInfo {
    Release kind;
    float vx, vy;     // Release velocity (px/s)
    float speed;
};

// releaseY: finger y at release; cupTopY: y of the cup rim top edge (screen coordinates).
ReleaseInfo decideRelease(const ShakeTracker &t, float releaseY, float cupTopY);

// --- Gesture -> throw animation parameters ----------------------------------------------
struct ThrowParams {
    float angle;          // Launch direction, rad: 0 = straight up, + = towards the right
    float power;          // 0..1 from the swipe speed
    float shake;          // 0..1 from the shake energy / reversals
    int spinDir;          // +1 clockwise, -1 counter-clockwise (die rotation direction)
    float turns;          // Die rotations during the flight (1..3)
    float curve;          // Lateral bulge of the path in px (signed, follows the spin direction)
    float spread;         // 0.25..1: angle / timing scatter between the dice
    int hops;             // Bounces on the felt (2..4)
    float hopPx;          // Bounce height in px
    float reachPx;        // Distance of the Bezier control point along the launch direction
    uint32_t durationMs;  // THROW_MIN_MS..THROW_MAX_MS
};

// Standard throw (tap on the cup, or a gesture-less roll). fallbackDir = +1 / -1.
ThrowParams defaultThrow(uint32_t baseMs, int fallbackDir);
// Throw shaped by the gesture. t may be null (= no shake information).
ThrowParams mapThrow(const ReleaseInfo &r, const ShakeTracker *t, uint32_t baseMs, int fallbackDir);

// --- Dice rattling inside the cup ---------------------------------------------------------
struct Body {
    float x, y;       // Centre relative to the cup centre (px)
    float vx, vy;     // px/s
    float ang;        // Rotation (deg)
    float av;         // Angular velocity (deg/s)
};

struct CupParams {
    float wallR = 42.0f;          // Max distance of a die centre from the cup centre
    float dieR = 14.0f;           // Collision radius between dice
    float wallBounce = 0.6f;      // Restitution against the wall
    float dieBounce = 0.5f;       // Restitution die <-> die
    float friction = 2.5f;        // Velocity damping per second
    float maxSpeed = 900.0f;      // px/s clamp
    float impactSpeed = 120.0f;   // Normal speed that counts as an audible/visible impact
};

struct StepResult {
    int wallHits;
    int dieHits;
    float maxImpact;      // Largest normal impact speed this step (px/s)
    uint32_t hitMask;     // Bit i: die i had an impact >= impactSpeed (new random face)
};

// Advances n bodies by dt seconds. (dvx, dvy) is a velocity change applied to every die this
// step - the inertia of the dice when the cup accelerates (pass minus the cup's velocity change).
StepResult step(Body *b, int n, float dt, float dvx, float dvy, const CupParams &p);

// Places n bodies on a ring inside the cup (deterministic start layout, no overlap for n <= 6).
void layout(Body *b, int n, const CupParams &p, float phase);

}  // namespace DiceCup
