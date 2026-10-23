// game_live.h - thread-safe copy of the running Yatzy game for the web live view (/game).
//
// The UI (LVGL task) publishes the game after every change; the web server (httpd task) reads
// a consistent snapshot. A version counter lets the page skip unchanged polls.
#pragma once

#include <stdint.h>

#include "game_yatzy.h"

namespace GameLive {

struct Snapshot {
    uint32_t version;
    bool active;          // A game screen is open (false after "Avsluta" / before the first game)
    bool rolling;         // Dice animation running (dice values are already final)
    bool resultPhase;     // Console shows the score table (choosing a box)
    YatzyGame game;
};

void publish(const YatzyGame &g, bool active, bool rolling, bool resultPhase);
void setActive(bool active);
uint32_t version();
void get(Snapshot &out);

}  // namespace GameLive
