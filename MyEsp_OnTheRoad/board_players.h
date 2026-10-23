// board_players.h - persistent player registry (player_db.h) on the FFat partition.
//
// File /players.bin = header (magic, version, size) + PlayerDb. Written to /players.tmp and
// renamed, so a power cut never leaves a half-written file. FFat is mounted (and formatted on
// first use) by begin(); the same filesystem will hold the game history.
// Thread-safe: every call takes a mutex (LVGL task and web server use it).
#pragma once

#include "player_db.h"

namespace Players {

bool begin();                 // Mounts FFat and loads the registry. False if FFat failed.
bool ok();                    // Filesystem available
void snapshot(PlayerDb &out); // Copy for listing (sort with out.sortedIndices)
int count();
// Returns the new id (> 0) or a negative PlayerErr. All changes are saved immediately.
int add(const char *name);
int rename(uint16_t id, const char *name);   // PlayerErr as int
int remove(uint16_t id);                     // PlayerErr as int
bool name(uint16_t id, char *out, size_t cap);
int findByName(const char *name);            // id or 0
uint32_t fsTotalBytes();
uint32_t fsUsedBytes();

}  // namespace Players
