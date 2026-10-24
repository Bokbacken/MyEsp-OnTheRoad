#include "game_live.h"

#include "freertos/FreeRTOS.h"

namespace GameLive {

static_assert(sizeof(Snapshot) < 2048, "GameLive::Snapshot grew unexpectedly");

// A plain struct copy (~1 kB) under a spinlock: short, and safe from any task without lazy init.
static Snapshot s_snap = {};
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static Cmd s_q[CMD_QUEUE];
static int s_qHead = 0, s_qLen = 0;

void publish(const YatzyGame &g, bool active, bool rolling, bool resultPhase, uint8_t rollMask)
{
    portENTER_CRITICAL(&s_mux);
    s_snap.gameType = GameType::YATZY;
    s_snap.game = g;
    s_snap.active = active;
    s_snap.rolling = rolling;
    s_snap.rollMask = rolling ? rollMask : 0;
    s_snap.resultPhase = resultPhase;
    s_snap.version++;
    portEXIT_CRITICAL(&s_mux);
}

void publish(const PigGame &g, bool active, bool rolling, uint8_t rollMask)
{
    portENTER_CRITICAL(&s_mux);
    s_snap.gameType = GameType::PIG;
    s_snap.pig = g;
    s_snap.active = active;
    s_snap.rolling = rolling;
    s_snap.rollMask = rolling ? rollMask : 0;
    s_snap.resultPhase = false;
    s_snap.version++;
    portEXIT_CRITICAL(&s_mux);
}

void publish(const FarkleGame &g, bool active, bool rolling, uint8_t rollMask)
{
    portENTER_CRITICAL(&s_mux);
    s_snap.gameType = GameType::FARKLE;
    s_snap.farkle = g;
    s_snap.active = active;
    s_snap.rolling = rolling;
    s_snap.rollMask = rolling ? rollMask : 0;
    s_snap.resultPhase = false;
    s_snap.version++;
    portEXIT_CRITICAL(&s_mux);
}

void publish(const SccGame &g, bool active, bool rolling, bool resultPhase, uint8_t rollMask)
{
    portENTER_CRITICAL(&s_mux);
    s_snap.gameType = GameType::SCC;
    s_snap.scc = g;
    s_snap.active = active;
    s_snap.rolling = rolling;
    s_snap.rollMask = rolling ? rollMask : 0;
    s_snap.resultPhase = resultPhase;
    s_snap.version++;
    portEXIT_CRITICAL(&s_mux);
}

bool started(const Snapshot &s)
{
    switch (s.gameType) {
    case GameType::PIG: return s.pig.started();
    case GameType::FARKLE: return s.farkle.started();
    case GameType::SCC: return s.scc.started();
    default: return s.game.started();
    }
}

bool finished(const Snapshot &s)
{
    switch (s.gameType) {
    case GameType::PIG: return s.pig.finished();
    case GameType::FARKLE: return s.farkle.finished();
    case GameType::SCC: return s.scc.finished();
    default: return s.game.finished();
    }
}

int playerCount(const Snapshot &s)
{
    switch (s.gameType) {
    case GameType::PIG: return s.pig.playerCount();
    case GameType::FARKLE: return s.farkle.playerCount();
    case GameType::SCC: return s.scc.playerCount();
    default: return s.game.playerCount();
    }
}

int currentPlayer(const Snapshot &s)
{
    switch (s.gameType) {
    case GameType::PIG: return s.pig.currentPlayer();
    case GameType::FARKLE: return s.farkle.currentPlayer();
    case GameType::SCC: return s.scc.currentPlayer();
    default: return s.game.currentPlayer();
    }
}

int diceCount(const Snapshot &s)
{
    switch (s.gameType) {
    case GameType::PIG: return s.pig.numDice();
    case GameType::FARKLE: return FARKLE_DICE;
    case GameType::SCC: return SCC_DICE;
    default: return s.game.diceCount();
    }
}

const char *playerName(const Snapshot &s, int i)
{
    if (i < 0 || i >= playerCount(s)) return "";
    switch (s.gameType) {
    case GameType::PIG: return s.pig.player(i).name;
    case GameType::FARKLE: return s.farkle.player(i).name;
    case GameType::SCC: return s.scc.player(i).name;
    default: return s.game.player(i).name;
    }
}

void setActive(bool active)
{
    portENTER_CRITICAL(&s_mux);
    if (s_snap.active != active) {
        s_snap.active = active;
        s_snap.rolling = false;
        s_snap.rollMask = 0;
        s_snap.shaking = false;
        if (!active) s_qLen = 0;   // Game left: web clients drop to the lobby (see app_web)
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

void setSeats(const bool *web, const uint16_t *pids, int n)
{
    portENTER_CRITICAL(&s_mux);
    if (++s_snap.session == 0) s_snap.session = 1;
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

uint32_t session()
{
    portENTER_CRITICAL(&s_mux);
    uint32_t c = s_snap.session;
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
