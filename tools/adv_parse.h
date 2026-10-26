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

struct AdvDoc {
    // Meta
    std::string id;
    int world = 0;
    int mapId = 0;

    // Built structures (point into the storage below)
    Otr::MapDef map{};
    Otr::Story::Script script{};

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
