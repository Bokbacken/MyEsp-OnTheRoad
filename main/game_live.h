// game_live.h - thread-safe copy of the running Yatzy game for the web live view (/game).
//
// The UI (LVGL task) publishes the game after every change; the web server (httpd task) reads
// a consistent snapshot. A version counter lets the page skip unchanged polls.
//
// v0.5.2 web play: the snapshot also carries which seats are played from a phone (/play) and how
// many WebSocket clients are bound to each seat. v0.5.6: the shared game code is gone (players
// join with their personal PIN); an internal session counter, bumped for every new game, makes
// old WebSocket bindings invalid. Commands from the web
// (hold / roll / choose) go through a small lock-protected ring buffer that the LVGL task drains.
#pragma once

#include <stdint.h>

#include "game_yatzy.h"

namespace GameLive {

struct Snapshot {
    uint32_t version;
    bool active;          // A game screen is open (false after "Avsluta" / before the first game)
    bool rolling;         // Dice animation running (dice values are already final)
    bool resultPhase;     // Console shows the score table (choosing a box)
    bool shaking;         // Player is shaking the dice cup on the console
    uint32_t session;                      // Bumped by setSeats(); 0 = no game yet (internal)
    bool web[YATZY_MAX_PLAYERS];           // Seat is played from the web page (/play)
    uint16_t pid[YATZY_MAX_PLAYERS];       // Registry id per seat (0 = guest)
    uint8_t conn[YATZY_MAX_PLAYERS];       // WebSocket clients bound to the seat
    YatzyGame game;
};

enum class CmdType : uint8_t { HOLD, ROLL, CHOOSE };

struct Cmd {
    CmdType type;
    int8_t arg;      // HOLD: die index, CHOOSE: YatzyCategory, ROLL: unused
    int8_t player;   // Seat of the sending client (checked again on the UI side)
};

void publish(const YatzyGame &g, bool active, bool rolling, bool resultPhase);
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

constexpr int CMD_QUEUE = 16;
bool pushCmd(const Cmd &c);   // false when full
bool popCmd(Cmd &out);        // false when empty
void clearCmds();

}  // namespace GameLive
