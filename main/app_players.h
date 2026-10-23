// app_players.h - persistent player registry (player_db.h) on the FFat partition.
//
// File /ffat/players.bin = header (magic, version, size) + PlayerDb, written atomically
// (MyFat::writeAtomic: temp file + rename). Same format as the Arduino 0.3.x builds.
// MyFat must be mounted before begin(). Thread-safe (LVGL task and web server use it).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "player_db.h"

namespace Players {

bool begin();                 // Loads the registry. False if the filesystem is missing.
bool ok();                    // Filesystem available
void snapshot(PlayerDb &out); // Copy for listing (sort with out.sortedIndices)
int count();
// Returns the new id (> 0) or a negative PlayerErr. All changes are saved immediately.
int add(const char *name);
int rename(uint16_t id, const char *name);   // PlayerErr as int
int remove(uint16_t id);                     // PlayerErr as int
bool name(uint16_t id, char *out, size_t cap);
int findByName(const char *name);            // id or 0

}  // namespace Players
