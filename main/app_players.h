// app_players.h - persistent player registry (player_db.h) on the FFat partition.
//
// File /ffat/players.bin = header (magic, version, size) + PlayerDb, written atomically
// (checked temp write + backup/rename; failed loads are read-only). Version 2 (v0.5.6) adds a per-player PIN; version-1
// files (Arduino 0.3.x .. v0.5.5) are decoded on load; rewritten only on a committed change.
// MyFat must be mounted before begin(). Thread-safe (LVGL task and web server use it).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "player_db.h"

namespace Players {

bool begin();                 // False on unreadable/recovery-only storage; keeps last good memory.
bool ok();                    // Registry safely loaded and writable
void snapshot(PlayerDb &out); // Copy for listing (sort with out.sortedIndices)
int count();
// Returns the new id (> 0) or a negative PlayerErr. All changes are saved immediately.
int add(const char *name, const char *pin = nullptr);   // pin: nullptr/"" or 4 digits
int setPin(uint16_t id, const char *pin);    // "" clears; PlayerErr as int
bool hasPin(uint16_t id);
bool checkPin(uint16_t id, const char *pin); // False when the player has no PIN
int rename(uint16_t id, const char *name);   // PlayerErr as int
int remove(uint16_t id);                     // PlayerErr as int
bool name(uint16_t id, char *out, size_t cap);
int findByName(const char *name);            // id or 0

}  // namespace Players
