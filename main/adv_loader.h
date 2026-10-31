// adv_loader.h - Firmware-safe on-device parser for the ASCII adventure format (.adv).
//
// This is the device-side twin of the host reference parser (tools/adv_parse.*), extended with
// S2 lever/timer, motion, colored-key, resource and multimap syntax (not yet in the reference
// parser). It builds an Otr::MapDef (+ rows) and an Otr::Story::Script in bounded storage. The
// parser uses a bounded PSRAM scratch allocation to preprocess map selectors; it avoids STL
// containers. Adv::Loaded is caller-owned (88 kB on host) and MUST be allocated in PSRAM on the
// ESP32-S3 (heap_caps_malloc). On the host it compiles as plain C++ for unit testing.
//
// Usage (device):
//   auto *L = (Adv::Loaded*)heap_caps_malloc(sizeof(Adv::Loaded), MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
//   char err[96];
//   if (Adv::parseFile("/sdcard/otr/adventure/josef/01_lager.adv", *L, err, sizeof err)) {
//       // L->map   -> Otr::MapDef (load with World::load)
//       // L->script-> Otr::Story::Script (check with Script::validate)
//   }
//   heap_caps_free(L);
//
// Loaded owns all backing storage; MapDef/Script point into it, so keep Loaded alive while used.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "otr_story.h"
#include "otr_world.h"

namespace Adv {

// Fixed capacities. Kept at or below the engine's own limits where they exist, generous where the
// engine has none. A file that exceeds any of these fails to parse with a clear error rather than
// overflowing a buffer.
constexpr int MAX_ROWS = Otr::MAX_H;                 // 128 map rows
constexpr int ROW_CAP = Otr::MAX_W + 1;              // one row + NUL
constexpr int MAX_NPCS = 16;
constexpr int MAX_NODES = 128;
constexpr int MAX_FLAGS = 64;
constexpr int MAX_COUNTERS = Otr::Story::MAX_COUNTERS;   // 16
constexpr int MAX_ITEMS = Otr::Story::MAX_ITEMS;         // 32
constexpr int MAX_QUESTS = Otr::Story::MAX_QUESTS;       // 16
constexpr int MAX_TREASURES = 48;
constexpr int MAX_RESOURCES = 16;
constexpr int MAX_RES_PLACE = Otr::MAX_RESOURCE_PICKUPS;
constexpr int MAX_INTERNAL_MAPS = 4;
constexpr int MAX_DESTINATIONS = 32;
constexpr int MAX_PORTALS = 32;
constexpr int DESTINATION_NAME_CAP = 32;
static_assert(MAX_ITEMS <= Otr::MAX_RESOURCE_ITEMS, "resource indices must fit the engine inventory");
constexpr int ARENA_CAP = 28 * 1024;                 // interned strings (dialogue is text-heavy)
constexpr int LINE_CAP = 1200;                       // longest source line we accept

// carry/reset tokens recognised by @rules. Unknown tokens are parse errors. The session applies
// carry to the next level and resets the rest (see adv_sd_session).
enum CarryBit : uint32_t {
    CARRY_ENERGY    = 1u << 0,   // current energy
    CARRY_MAXENERGY = 1u << 1,   // permanent max-energy (hearts)
    CARRY_ITEMS     = 1u << 2,   // story items / resources (inventory)
    CARRY_LIVES     = 1u << 3,   // lives used / left
    CARRY_SUIT      = 1u << 4,   // protective suit charges
    CARRY_KEYS      = 1u << 5,
    CARRY_GEMS      = 1u << 6,
    CARRY_BOAT      = 1u << 7,
};

// A collectible bound to a free map glyph by @resources (reuses the story item system). The
// Inventory lives in StoryState; World handles pickup through the SD session's collector.
struct Resource {
    char glyph;                 // free map glyph (not a built-in terrain/object/person)
    const char *id;             // story item id (also added to Loaded.items)
    Otr::Story::Text name;      // sv / en display name
    uint8_t cap;                // max you can hold (1..255)
    char image[40];             // 32x32 PNG filename in the adventure's bitmaps/ dir, or ""
};

// A placed resource on the map (glyph + cell). The map cell itself is translated to grass so
// World::load() accepts it; map.resources then installs a real resource object here.
struct ResourcePlace { char glyph; uint8_t x, y; };
struct Destination {
    char name[DESTINATION_NAME_CAP];
    uint8_t mapId, x, y;
};
struct Portal {
    uint8_t mapId, x, y;
    char destination[DESTINATION_NAME_CAP];
};

// One fully-parsed .adv map. Big; allocate in PSRAM.
struct Loaded {
    // @meta
    char id[40];
    int world;
    int mapId;
    int internalMapId;
    int internalMapCount;
    int internalMapIds[MAX_INTERNAL_MAPS];

    // @about (what the SD menu shows). Falls back to legacy @meta name.*/desc.*.
    char titleSv[80], titleEn[80];
    char descSv[200], descEn[200];
    char author[64];
    char cover[40];

    // @rules carry/reset policy (bitmask of CarryBit).
    uint32_t carryMask, resetMask;

    // Built engine structures (point into the backing storage below).
    Otr::MapDef map;
    Otr::Story::Script script;

    // @treasures
    Otr::Treasure treasures[MAX_TREASURES];
    int treasureCount;

    // @enemies: patrol X Y loop|pingpong [area X0 Y0 X1 Y1] route X,Y ...
    //           chaser X Y [area X0 Y0 X1 Y1]
    Otr::EnemyDef enemies[Otr::MAX_ENEMIES];
    int enemyCount;

    // @dark (v0.18.0): inclusive dark-area rectangles. map.darks points at this array.
    Otr::DarkRect darks[Otr::MAX_DARK];
    int darkCount;

    // Lever/timed gate configuration; map.controls points into this fixed backing.
    Otr::GroupControl controls[Otr::PLATE_GROUPS];
    int controlCount;
    uint8_t plateGroups[Otr::MAX_H][Otr::MAX_W];

    // @resources
    Resource resources[MAX_RESOURCES];
    int resourceCount;
    ResourcePlace placements[MAX_RES_PLACE];
    Otr::ResourcePickup pickups[MAX_RES_PLACE];
    int placementCount;

    Destination destinations[MAX_DESTINATIONS];
    int destinationCount;
    Portal portals[MAX_PORTALS];
    int portalCount;

    // ---- backing storage (stable addresses) ----
    char arena[ARENA_CAP];
    size_t arenaUsed;
    char rows[MAX_ROWS][ROW_CAP];
    const char *rowPtrs[MAX_ROWS];
    int rowCount;
    Otr::Story::Npc npcs[MAX_NPCS];
    int npcCount;
    Otr::Story::Node nodes[MAX_NODES];
    int nodeCount;
    const char *flags[MAX_FLAGS];
    int flagCount;
    const char *counters[MAX_COUNTERS];
    int counterCount;
    Otr::Story::Named items[MAX_ITEMS];
    int itemCount;
    Otr::Story::Named quests[MAX_QUESTS];
    int questCount;

    // Copies [s, s+len) into the arena as a NUL-terminated string; returns a stable pointer, or
    // nullptr if the arena is full (the parse then fails with "out of string space").
    const char *intern(const char *s, size_t len);
    void reset();
    bool carries(uint32_t bit) const { return (carryMask & bit) != 0; }
    bool resets(uint32_t bit) const { return (resetMask & bit) != 0; }
};

// Parses a .adv text buffer (NUL not required; len bytes) into `out`. Returns false and fills
// `err` on any structural error. Does NOT run World::load() / Script::validate() - the caller
// does that to confirm engine-level validity.
bool parse(const char *text, size_t len, Loaded &out, char *err, size_t errCap);
bool parseMap(const char *text, size_t len, int internalMapId, Loaded &out, char *err, size_t errCap);

// Convenience: reads the whole file at `path` into a bounded PSRAM temp buffer and parses it.
// Uses stdio (works on ESP-IDF VFS and the host). Temporary buffers are freed before return.
// Returns false (with err) if the file cannot be opened or is larger than MAX_FILE.
constexpr size_t MAX_FILE = 64 * 1024;
bool parseFile(const char *path, Loaded &out, char *err, size_t errCap);
bool parseMapFile(const char *path, int internalMapId, Loaded &out, char *err, size_t errCap);

// Parses ONLY the @about / @meta header of a file (title, description, author, world, map_id) for
// the menu catalog - cheap, no map/story building, stops at the first map/section body. `out` is a
// small struct so it can live on the stack. Returns false (with err) on open failure.
struct Header {
    char id[40];
    int world, mapId;
    char titleSv[80], titleEn[80];
    char descSv[200], descEn[200];
    char author[64];
    char cover[40];
};
bool parseHeaderFile(const char *path, Header &out, char *err, size_t errCap);

}  // namespace Adv
