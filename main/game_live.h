// game_live.h - thread-safe copy of the running Yatzy game for the web live view (/game).
//
// The UI (LVGL task) publishes the game after every change; the web server (httpd task) reads
// a consistent snapshot (v0.6.0: Yatzy, Pig, Farkle or Ship/Captain/Crew). A version counter lets the page skip unchanged polls.
//
// v0.5.2 web play: the snapshot also carries which seats are played from a phone (/play) and how
// many WebSocket clients are bound to each seat. v0.5.6: the shared game code is gone (players
// join with their personal PIN); an internal session counter, bumped for every new game, makes
// old WebSocket bindings invalid. Commands from the web
// (hold / roll / choose) go through a small lock-protected ring buffer that the LVGL task drains.
#pragma once

#include <stdint.h>

#include "game_base.h"
#include "game_farkle.h"
#include "game_pig.h"
#include "game_scc.h"
#include "game_yatzy.h"

namespace GameLive {

struct Snapshot {
    uint32_t version;
    bool active;          // A game screen is open (false after "Avsluta" / before the first game)
    bool rolling;         // Dice animation running. v0.7.1: the snapshot then holds the state from
                          // BEFORE the roll - the new values are not revealed until the dice stop.
    uint8_t rollMask;     // While rolling: bit i = die i is in the air
    bool resultPhase;     // Console shows the score table (choosing a box)
    bool shaking;         // Player is shaking the dice cup on the console
    uint32_t session;                      // Bumped by setSeats(); 0 = no game yet (internal)
    bool web[YATZY_MAX_PLAYERS];           // Seat is played from the web page (/play)
    uint16_t pid[YATZY_MAX_PLAYERS];       // Registry id per seat (0 = guest)
    uint8_t conn[YATZY_MAX_PLAYERS];       // WebSocket clients bound to the seat
    GameType gameType;                     // v0.6.0: which game below is valid
    YatzyGame game;                        // Yatzy (kept as a named member: existing code uses it)
    union {                                // Only one of the other games runs at a time
        PigGame pig;
        FarkleGame farkle;
        SccGame scc;
    };
};

// Game-independent views of the snapshot (dispatch on gameType).
bool started(const Snapshot &s);
bool finished(const Snapshot &s);
bool running();
int playerCount(const Snapshot &s);
int currentPlayer(const Snapshot &s);
int diceCount(const Snapshot &s);
const char *playerName(const Snapshot &s, int i);

enum class CmdType : uint8_t { HOLD, ROLL, CHOOSE, BANK, END_TURN };

struct Cmd {
    CmdType type;
    int8_t arg;      // HOLD: die index, CHOOSE: YatzyCategory, others: unused
    int8_t player;   // Seat of the sending client (checked again on the UI side)
};

// While rolling, pass the game as it was before the roll plus the mask of dice in the air.
void publish(const YatzyGame &g, bool active, bool rolling, bool resultPhase, uint8_t rollMask = 0);
void publish(const PigGame &g, bool active, bool rolling, uint8_t rollMask = 0);
void publish(const FarkleGame &g, bool active, bool rolling, uint8_t rollMask = 0);
void publish(const SccGame &g, bool active, bool rolling, bool resultPhase, uint8_t rollMask = 0);
void setActive(bool active);
void setShaking(bool shaking);   // Bumps the version only on change
uint32_t version();
void get(Snapshot &out);

// Seats of a new game: per-seat web flag / registry id. Starts a new session (old web bindings
// become invalid) and clears the command queue.
void setSeats(const bool *web, const uint16_t *pids, int n);
void setConnections(const uint8_t *conn, int n);   // From the web server; bumps version on change
uint32_t session();
bool isWeb(int seat);
int connections(int seat);
// v0.7.2: a browser gave a seat back ("leave"). Clears the seat's web flag in the snapshot (no new
// session, bindings stay valid) and records it for the UI, which polls takeReleased() and clears
// its own per-seat web flag so the device continues locally. Also used by the device button.
void releaseWeb(int seat);
uint32_t takeReleased();   // Bit i = seat i released since the last call (clears the mask)

constexpr int CMD_QUEUE = 16;
bool pushCmd(const Cmd &c);   // false when full
bool popCmd(Cmd &out);        // false when empty
void clearCmds();

}  // namespace GameLive
