#include "game_live.h"

#include "freertos/FreeRTOS.h"

namespace GameLive {

// A plain struct copy (~0.5 kB) under a spinlock: short, and safe from any task without lazy init.
static Snapshot s_snap = {};
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static Cmd s_q[CMD_QUEUE];
static int s_qHead = 0, s_qLen = 0;

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
        if (!active) {   // Game left: the code dies, web clients drop to the lobby
            s_snap.code = 0;
            s_qLen = 0;
        }
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

void setSeats(uint16_t code, const bool *web, const uint16_t *pids, int n)
{
    portENTER_CRITICAL(&s_mux);
    s_snap.code = code;
    for (int i = 0; i < YATZY_MAX_PLAYERS; i++) {
        s_snap.web[i] = i < n && web && web[i];
        s_snap.pid[i] = i < n && pids ? pids[i] : 0;
    }
    s_qLen = 0;
    s_snap.version++;
    portEXIT_CRITICAL(&s_mux);
}

void setConnections(const uint8_t *conn, int n)
{
    portENTER_CRITICAL(&s_mux);
    bool changed = false;
    for (int i = 0; i < YATZY_MAX_PLAYERS; i++) {
        uint8_t c = i < n && conn ? conn[i] : 0;
        if (s_snap.conn[i] != c) {
            s_snap.conn[i] = c;
            changed = true;
        }
    }
    if (changed) s_snap.version++;
    portEXIT_CRITICAL(&s_mux);
}

uint16_t code()
{
    portENTER_CRITICAL(&s_mux);
    uint16_t c = s_snap.code;
    portEXIT_CRITICAL(&s_mux);
    return c;
}

bool isWeb(int seat)
{
    if (seat < 0 || seat >= YATZY_MAX_PLAYERS) return false;
    portENTER_CRITICAL(&s_mux);
    bool w = s_snap.web[seat];
    portEXIT_CRITICAL(&s_mux);
    return w;
}

int connections(int seat)
{
    if (seat < 0 || seat >= YATZY_MAX_PLAYERS) return 0;
    portENTER_CRITICAL(&s_mux);
    int c = s_snap.conn[seat];
    portEXIT_CRITICAL(&s_mux);
    return c;
}

bool pushCmd(const Cmd &c)
{
    portENTER_CRITICAL(&s_mux);
    bool ok = s_qLen < CMD_QUEUE;
    if (ok) {
        s_q[(s_qHead + s_qLen) % CMD_QUEUE] = c;
        s_qLen++;
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

bool popCmd(Cmd &out)
{
    portENTER_CRITICAL(&s_mux);
    bool ok = s_qLen > 0;
    if (ok) {
        out = s_q[s_qHead];
        s_qHead = (s_qHead + 1) % CMD_QUEUE;
        s_qLen--;
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

void clearCmds()
{
    portENTER_CRITICAL(&s_mux);
    s_qLen = 0;
    portEXIT_CRITICAL(&s_mux);
}

}  // namespace GameLive
