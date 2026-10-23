// dice_cup.cpp - see dice_cup.h. Pure maths, host-testable.
#include "dice_cup.h"

#include <math.h>

namespace DiceCup {

static constexpr float PI_F = 3.14159265f;
static constexpr float DEG = PI_F / 180.0f;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// ----------------------------------------------------------------------------------------
// ShakeTracker
// ----------------------------------------------------------------------------------------
void ShakeTracker::begin(float x, float y, uint32_t ms)
{
    *this = ShakeTracker();
    m_active = true;
    m_startX = m_lastX = x;
    m_startY = m_lastY = y;
    m_startMs = m_lastMs = m_segStartMs = ms;
    m_ring[0] = {x, y, ms};
    m_head = 1;
    m_count = 1;
}

void ShakeTracker::move(float x, float y, uint32_t ms)
{
    if (!m_active) begin(x, y, ms);
    const float dx = x - m_lastX, dy = y - m_lastY;
    const float d = sqrtf(dx * dx + dy * dy);
    m_ring[m_head] = {x, y, ms};
    m_head = (m_head + 1) % RING;
    if (m_count < RING) m_count++;
    m_lastX = x;
    m_lastY = y;
    m_lastMs = ms;
    const float fx = x - m_startX, fy = y - m_startY;
    const float from = sqrtf(fx * fx + fy * fy);
    if (from > m_maxDist) m_maxDist = from;
    if (d <= 0.0f) {
        m_angVel *= 0.9f;   // Finger resting: the turn rate fades
        return;
    }
    m_energy += d;
    m_segX += dx;
    m_segY += dy;
    const float segLen = sqrtf(m_segX * m_segX + m_segY * m_segY);
    if (segLen < SEG_PX) return;
    const float ux = m_segX / segLen, uy = m_segY / segLen;
    if (m_prevValid) {
        const float dot = m_prevX * ux + m_prevY * uy;
        const float cross = m_prevX * uy - m_prevY * ux;   // > 0: clockwise on screen (y down)
        const float ang = atan2f(cross, dot);
        const float segMs = (float)(ms - m_segStartMs < 1 ? 1 : ms - m_segStartMs);
        if (fabsf(ang) > REVERSAL_DEG * DEG) {
            m_reversals++;
            m_angVel *= 0.5f;
        } else if (fabsf(ang) < SPIN_MAX_DEG * DEG) {
            m_spin += ang;
            m_angVel = 0.6f * m_angVel + 0.4f * (ang * 1000.0f / segMs);
        }
    }
    m_prevX = ux;
    m_prevY = uy;
    m_prevValid = true;
    m_segX = m_segY = 0.0f;
    m_segStartMs = ms;
}

int ShakeTracker::spinDir() const
{
    if (m_spin >= SPIN_MIN_RAD) return 1;
    if (m_spin <= -SPIN_MIN_RAD) return -1;
    return 0;
}

void ShakeTracker::velocity(float &vx, float &vy) const
{
    vx = vy = 0.0f;
    if (m_count < 2) return;
    const int newest = (m_head - 1 + RING) % RING;
    const Sample &n = m_ring[newest];
    int pick = -1;
    for (int k = 1; k < m_count; k++) {
        const int idx = (newest - k + RING) % RING;
        if (pick >= 0 && n.ms - m_ring[idx].ms > VEL_WINDOW_MS) break;
        pick = idx;
    }
    if (pick < 0) return;
    const Sample &o = m_ring[pick];
    const uint32_t dt = n.ms - o.ms;
    if (dt == 0) return;
    vx = (n.x - o.x) * 1000.0f / (float)dt;
    vy = (n.y - o.y) * 1000.0f / (float)dt;
}

// ----------------------------------------------------------------------------------------
// Release / throw mapping
// ----------------------------------------------------------------------------------------
ReleaseInfo decideRelease(const ShakeTracker &t, float releaseY, float cupTopY)
{
    ReleaseInfo r{};
    t.velocity(r.vx, r.vy);
    r.speed = sqrtf(r.vx * r.vx + r.vy * r.vy);
    if (t.duration() <= TAP_MAX_MS && t.maxDistance() <= TAP_MAX_PX) {
        r.kind = Release::TAP;
    } else if (r.vy <= -THROW_VY && fabsf(r.vx) <= THROW_MAX_SIDE * -r.vy) {
        r.kind = Release::THROW;
    } else if (releaseY < cupTopY - EXIT_MARGIN_PX && r.vy <= -EXIT_VY) {
        r.kind = Release::THROW;   // Dragged out of the cup onto the felt
    } else {
        r.kind = Release::NONE;
    }
    return r;
}

static void finish(ThrowParams &p, uint32_t baseMs)
{
    if (baseMs < 300) baseMs = 300;
    p.turns = 1.0f + 1.6f * p.shake + 0.4f * p.power;
    p.spread = 0.25f + 0.75f * p.shake;
    p.hops = 2 + (int)(p.power * 2.99f);
    p.hopPx = 6.0f + 12.0f * p.power;
    p.reachPx = 90.0f + 150.0f * p.power;
    float ms = (float)baseMs * (0.8f + 0.55f * p.power + 0.15f * p.shake);
    p.durationMs = (uint32_t)clampf(ms, (float)THROW_MIN_MS, (float)THROW_MAX_MS);
}

ThrowParams defaultThrow(uint32_t baseMs, int fallbackDir)
{
    ThrowParams p{};
    p.angle = 0.0f;
    p.power = 0.35f;
    p.shake = 0.3f;
    p.spinDir = fallbackDir < 0 ? -1 : 1;
    p.curve = 0.0f;
    finish(p, baseMs);
    return p;
}

ThrowParams mapThrow(const ReleaseInfo &r, const ShakeTracker *t, uint32_t baseMs, int fallbackDir)
{
    if (r.kind != Release::THROW) return defaultThrow(baseMs, fallbackDir);
    ThrowParams p{};
    p.power = clampf((r.speed - EXIT_VY) / (POWER_FULL_V - EXIT_VY), 0.0f, 1.0f);
    p.angle = clampf(atan2f(r.vx, -r.vy), -0.6f, 0.6f);
    int dir = fallbackDir < 0 ? -1 : 1;
    if (r.vx > 40.0f) dir = 1;          // Sideways component of the swipe sets the roll direction
    else if (r.vx < -40.0f) dir = -1;
    p.curve = 0.0f;
    if (t) {
        p.shake = clampf(t->energy() / SHAKE_FULL_PX + 0.04f * (float)t->reversals(), 0.0f, 1.0f);
        if (t->spinDir() != 0) {         // Spinning the cup wins: rotation + curved path
            dir = t->spinDir();
            p.curve = (float)dir * (12.0f + 24.0f * clampf(fabsf(t->angularVelocity()) / 12.0f, 0.0f, 1.0f));
        }
    }
    p.spinDir = dir;
    finish(p, baseMs);
    return p;
}

// ----------------------------------------------------------------------------------------
// Rattling dice
// ----------------------------------------------------------------------------------------
static bool keepInside(Body &b, const CupParams &p, float &impact)
{
    impact = 0.0f;
    const float d = sqrtf(b.x * b.x + b.y * b.y);
    if (d <= p.wallR) return false;
    const float nx = b.x / d, ny = b.y / d;
    b.x = nx * p.wallR;
    b.y = ny * p.wallR;
    const float vn = b.vx * nx + b.vy * ny;
    if (vn <= 0.0f) return false;
    const float vt = -b.vx * ny + b.vy * nx;   // Tangential speed -> die starts to rotate
    b.vx -= (1.0f + p.wallBounce) * vn * nx;
    b.vy -= (1.0f + p.wallBounce) * vn * ny;
    b.av += vt * 1.5f;
    impact = vn;
    return true;
}

StepResult step(Body *b, int n, float dt, float dvx, float dvy, const CupParams &p)
{
    StepResult r{};
    if (dt <= 0.0f || n <= 0) return r;
    if (dt > 0.05f) dt = 0.05f;   // Long frame: avoid tunnelling
    float damp = 1.0f - p.friction * dt;
    if (damp < 0.0f) damp = 0.0f;
    for (int i = 0; i < n; i++) {
        Body &d = b[i];
        d.vx = (d.vx + dvx) * damp;
        d.vy = (d.vy + dvy) * damp;
        d.av *= damp;
        const float s = sqrtf(d.vx * d.vx + d.vy * d.vy);
        if (s > p.maxSpeed) {
            d.vx *= p.maxSpeed / s;
            d.vy *= p.maxSpeed / s;
        }
        d.av = clampf(d.av, -1440.0f, 1440.0f);
        d.x += d.vx * dt;
        d.y += d.vy * dt;
        d.ang = fmodf(d.ang + d.av * dt, 360.0f);
        float imp;
        if (keepInside(d, p, imp) && imp >= p.impactSpeed) {
            r.wallHits++;
            r.hitMask |= 1u << i;
            if (imp > r.maxImpact) r.maxImpact = imp;
        }
    }
    // Die <-> die: equal masses, push apart and exchange the normal velocity
    const float minD = 2.0f * p.dieR;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            float dx = b[j].x - b[i].x, dy = b[j].y - b[i].y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist >= minD) continue;
            float nx = 1.0f, ny = 0.0f;
            if (dist > 1e-3f) {
                nx = dx / dist;
                ny = dy / dist;
            }
            const float push = (minD - dist) * 0.5f;
            b[i].x -= nx * push;
            b[i].y -= ny * push;
            b[j].x += nx * push;
            b[j].y += ny * push;
            const float vn = (b[j].vx - b[i].vx) * nx + (b[j].vy - b[i].vy) * ny;
            if (vn >= 0.0f) continue;   // Already separating
            const float J = -(1.0f + p.dieBounce) * vn * 0.5f;
            b[i].vx -= J * nx;
            b[i].vy -= J * ny;
            b[j].vx += J * nx;
            b[j].vy += J * ny;
            if (-vn >= p.impactSpeed) {
                r.dieHits++;
                r.hitMask |= (1u << i) | (1u << j);
                if (-vn > r.maxImpact) r.maxImpact = -vn;
            }
        }
    }
    // The separation may have pushed a die through the wall: clamp again (no extra impact),
    // and keep the speed bounded after the impulses
    for (int i = 0; i < n; i++) {
        float imp;
        keepInside(b[i], p, imp);
        const float s = sqrtf(b[i].vx * b[i].vx + b[i].vy * b[i].vy);
        if (s > p.maxSpeed) {
            b[i].vx *= p.maxSpeed / s;
            b[i].vy *= p.maxSpeed / s;
        }
    }
    return r;
}

void layout(Body *b, int n, const CupParams &p, float phase)
{
    float ring = 0.0f;
    if (n > 1) ring = clampf(p.dieR / sinf(PI_F / (float)n) + 0.5f, 0.0f, p.wallR);
    for (int i = 0; i < n; i++) {
        const float a = phase + (float)i * 2.0f * PI_F / (float)(n > 0 ? n : 1);
        b[i] = Body{ring * cosf(a), ring * sinf(a), 0.0f, 0.0f, (float)(i * 37 % 360), 0.0f};
    }
}

}  // namespace DiceCup
