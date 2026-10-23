#include "game_live.h"

#include "freertos/FreeRTOS.h"

namespace GameLive {

// A plain struct copy (~0.5 kB) under a spinlock: short, and safe from any task without lazy init.
static Snapshot s_snap = {};
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

void publish(const YatzyGame &g, bool active, bool rolling, bool resultPhase)
{
    portENTER_CRITICAL(&s_mux);
    s_snap.game = g;
    s_snap.active = active;
    s_snap.rolling = rolling;
    s_snap.resultPhase = resultPhase;
    s_snap.version++;
    portEXIT_CRITICAL(&s_mux);
}

void setActive(bool active)
{
    portENTER_CRITICAL(&s_mux);
    if (s_snap.active != active) {
        s_snap.active = active;
        s_snap.rolling = false;
        s_snap.shaking = false;
        s_snap.version++;
    }
    portEXIT_CRITICAL(&s_mux);
}

void setShaking(bool shaking)
{
    portENTER_CRITICAL(&s_mux);
    if (s_snap.shaking != shaking) {
        s_snap.shaking = shaking;
        s_snap.version++;
    }
    portEXIT_CRITICAL(&s_mux);
}

uint32_t version()
{
    portENTER_CRITICAL(&s_mux);
    uint32_t v = s_snap.version;
    portEXIT_CRITICAL(&s_mux);
    return v;
}

void get(Snapshot &out)
{
    portENTER_CRITICAL(&s_mux);
    out = s_snap;
    portEXIT_CRITICAL(&s_mux);
}

}  // namespace GameLive
