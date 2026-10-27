// adv_parse.h - Host-side importer for the ASCII adventure format (.adv).
//
// Reads a .adv text file (see docs/AUTHORING_ADVENTURES.md) into the engine's
// REAL structures: an Otr::MapDef (+ its rows) and an Otr::Story::Script. This
// is a host tool (std::string / std::vector / std::deque); it is NOT compiled
// into the firmware and deliberately avoids any ESP-IDF / LVGL dependency.
//
// Usage:
//   AdvDoc doc;
//   std::string err;
//   if (!parseAdvFile("example.adv", doc, err)) { ... err ... }
//   // doc.map  -> Otr::MapDef (doc.mapRows backs rows; load with World::load)
//   // doc.script -> Otr::Story::Script (validate with Script::validate)
//
// The AdvDoc owns all backing storage; MapDef/Script point into it, so keep the
// AdvDoc alive for as long as you use them.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "otr_story.h"
#include "otr_world.h"

// A collectible bound to a free map glyph by the @resources section (A4). It
// reuses the story item system (an entry is also added to AdvDoc.items so that
// has:/give/take validate), and adds the device-side authoring metadata: which
// map glyph represents it, how many you may hold (cap) and the 32x32 PNG used
// for it. On-device pickup/persistence (reusing StoryState.items) is the
// firmware integration step; the host parser only validates and records it.
struct AdvResource {
    char glyph = 0;        // free map glyph (not a built-in terrain/object/person)
    std::string id;        // story item id (also present in AdvDoc.items)
    std::string nameSv;
    std::string nameEn;
    int cap = 1;           // max you can hold (1..255)
    std::string image;     // 32x32 PNG filename in the adventure's bitmaps/ dir, or empty
};

// A placed resource on the map: its glyph and cell. Filled while reading @map so
// the device can overlay/pick up resources (the engine itself sees plain ground
// at this cell - the glyph is translated to grass before World::load()).
struct AdvResourcePlacement {
    char glyph = 0;
    uint8_t x = 0, y = 0;
};

struct AdvDoc {
    // Meta
    std::string id;
    int world = 0;
    int mapId = 0;

    // @about: what the SD adventure menu shows for this adventure (A1). Read
    // from the @about section, with legacy fallback to @meta name.*/desc.*.
    std::string titleSv, titleEn;
    std::string descSv, descEn;
    std::string author;
    std::string cover;     // optional cover image filename (bitmaps/ dir)

    // carry/reset policy declared in @rules (A5): which player state carries over
    // to the next level of the same adventure, and which resets. Lower-cased
    // tokens, e.g. carry={"energy","items"}, reset={"suit"}. Applied by the
    // session at a level transition (firmware).
    std::vector<std::string> carry;
    std::vector<std::string> reset;

    // Built structures (point into the storage below)
    Otr::MapDef map{};
    Otr::Story::Script script{};

    // @treasures (A3): hidden items placed under a covering object (or visible if
    // nothing covers them). map.treasures points at this vector.
    std::vector<Otr::Treasure> treasures;

    // @resources (A4): glyph-bound collectibles and their placements on the map.
    std::vector<AdvResource> resources;
    std::vector<AdvResourcePlacement> resourcePlacements;

    // Backing storage - stable addresses (deque never moves elements; vectors
    // are filled completely before map/script point at them).
    std::deque<std::string> arena;                  // every C-string lives here
    std::vector<const char *> mapRows;              // MapDef.rows
    std::vector<Otr::Story::Npc> npcs;
    std::vector<Otr::Story::Node> nodes;
    std::vector<const char *> flags;
    std::vector<const char *> counters;
    std::vector<Otr::Story::Named> items;
    std::vector<Otr::Story::Named> quests;

    const char *intern(const std::string &s)
    {
        arena.push_back(s);
        return arena.back().c_str();
    }
};

// Parses the file at `path` into `doc`. Returns false and fills `err` on a
// structural parse error (missing S, unequal map rows, malformed line, ...).
// It does NOT run World::load() or Script::validate() - the caller does that to
// check engine-level validity.
bool parseAdvFile(const std::string &path, AdvDoc &doc, std::string &err);
