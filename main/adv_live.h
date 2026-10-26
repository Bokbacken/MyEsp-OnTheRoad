// adv_live.h - thread-safe snapshot of the running adventure for the web live view (/adventure).
//
// Separate from GameLive on purpose: the map (up to 64 x 64 cells, three layers) is far too big
// for the GameLive union. The UI (LVGL task) calls publish() whenever something visible changed;
// the web server (httpd task) asks for JSON with writeJson(). One std::mutex guards the snapshot,
// which lives in PSRAM on the device (~15 kB, allocated on the first publish, never freed).
//
// Traffic: the map layers are copied only when World::revision() (or the world) changes and are
// sent only to a client whose cached map revision ("mr") differs. Everything else (player, stats,
// people, carried items, the current dialogue line with its answers) is small and sent with every
// new version.
//
// JSON (see writeJson(); field names are part of the web contract, docs/COMPONENTS.md):
//   {"active":false,"version":N}                                 no adventure running
//   {"active":true,"version":N,"world":"Byn","world_id":3,"map_rev":R,
//    "map":{"w":32,"h":22,"fog":false,"terrain":[rows],"obj":[rows],"hint":[rows]},   only if mr != R
//    "player":{"x":..,"y":..,"dir":"up|right|down|left","sailing":false},
//    "energy":..,"energy_max":..,"lives":-1|n,"lives_used":..,"keys":..,"gems":..,"gems_total":..,
//    "suit":..,"boat":false,"gates_open":false,"game_over":false,"exit":false,
//    "checkpoint":[x,y],"steps":..,
//    "npcs":[{"num":1,"x":..,"y":..,"look":0,"name":"..","talking":false}],
//    "items":[{"name":"Bulle","count":2}],
//    "dialog":null | {"speaker":"..","narrator":false,"look":0,"text":"..","ref":"Läs 1Mo 37:15",
//                     "choices":[{"text":"..","locked":false,"hint":""}]}}
//   Map rows: one character per cell.
//     terrain  'a' + Otr::Tile (a grass, b sand, c floor, d rubble, e wall, f cracked, g tree,
//              h water, i bridge, j lava, k basalt, l plate, m gate, n checkpoint, o exit,
//              p crater), '?' = unrevealed fog
//     obj      '.' none, 'A' + Otr::Obj for visible objects (B rock, C crate, D key, E door, F boat,
//              G gem, H bush, I potion, J heart, K suit, L life, M mine), '!' = flagged fog cell.
//              People are not in the map (they move): see "npcs".
//     hint     fog worlds only ('hint' is [] otherwise): '0'..'8' mines around a revealed cell.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "otr_story.h"
#include "otr_world.h"

namespace AdvLive {

constexpr int NAME_CAP = 48;
constexpr int TEXT_CAP = 4 * Otr::Story::MAX_TEXT_CHARS + 1;   // UTF-8: up to 4 bytes per character
constexpr int CHOICE_CAP = 4 * Otr::Story::MAX_CHOICE_CHARS + 1;
constexpr int HINT_CAP = 160;
constexpr int REF_CAP = 96;
constexpr int MAX_NPCS = 9;        // Map digits 1..9
constexpr int MAX_ITEMS = 16;      // Carried story items listed (first ones)
constexpr int MAX_CHOICES = Otr::Story::MAX_CHOICES + 1;   // + the device's own "Okej" / "Gå vidare"
constexpr size_t JSON_CAP = 28 * 1024;   // Worst case: 64 x 64 map with hints + a long dialogue

struct NpcInfo {
    uint8_t num, x, y, look;
    bool talking;
    char name[NAME_CAP];
};

struct ItemInfo {
    char name[NAME_CAP];
    uint8_t count;
};

struct ChoiceInfo {
    char text[CHOICE_CAP];
    char hint[HINT_CAP];
    bool locked;
};

struct DialogInfo {
    bool active;
    bool narrator;
    uint8_t look;
    char speaker[NAME_CAP];
    char text[TEXT_CAP];
    char ref[REF_CAP];
    uint8_t choiceCount;
    ChoiceInfo choices[MAX_CHOICES];
};

struct Snapshot {
    uint32_t version;
    bool active;
    // Map (copied when the revision / world changes)
    uint32_t mapRev;          // Bumped by AdvLive whenever the copied map changed (never 0 while active)
    uint32_t worldRev;        // World::revision() the map was copied at
    uint8_t worldId;
    uint8_t w, h;
    bool fog;
    char world[NAME_CAP];
    char terrain[Otr::MAX_H][Otr::MAX_W];
    char obj[Otr::MAX_H][Otr::MAX_W];
    char hint[Otr::MAX_H][Otr::MAX_W];
    // Per update
    uint8_t px, py, facing;
    bool sailing, boat, gatesOpen, gameOver, exitReached;
    int16_t energy, energyMax, lives, livesUsed, keys, gems, gemsTotal, suit;
    uint8_t cpX, cpY;
    uint32_t steps;
    uint8_t mapId, mineLevel, completed, bomb3, bomb4, lang;
    int safeLeft, bombSize, bombX, bombY;
    uint8_t npcCount;
    NpcInfo npcs[MAX_NPCS];
    uint8_t itemCount;
    ItemInfo items[MAX_ITEMS];
    DialogInfo dialog;
};

// Everything publish() reads. Pointers may be null where noted.
struct Source {
    const Otr::World *world;                    // Required
    const char *worldName;                      // Display name (current language)
    const Otr::Story::Script *story;            // nullptr = world without people / story
    const Otr::Story::StoryState *storyState;   // With story
    const Otr::Story::Dialog *dialog;           // nullptr or inactive = no conversation
    int lang;                                   // 0 = Swedish, 1 = English (Story::Text index)
    const char *refFormat;                      // "Läs %s" (the reference line on the device)
    const char *okText;                         // Shown when a line has no answers ("Okej")
    const char *byeText;                        // Added when every answer is locked ("Gå vidare")
    int talkNum;                                // Person being talked to (map number), 0 = none
    int worldId = 0, completed = 0, bombSize = 0, bombX = -1, bombY = -1;
};

// Pure: fills a snapshot from the source (no lock, no version). The map layers are rebuilt only
// when withMap is true. Used by publish() and by the host tests.
void fillMap(Snapshot &s, const Source &src);
void fillState(Snapshot &s, const Source &src);

// Pure: writes the JSON for snapshot s into buf. clientMapRev != s.mapRev adds the map. Returns
// the length (excluding the terminating 0) or 0 if cap was too small.
size_t toJson(const Snapshot &s, uint32_t clientMapRev, char *buf, size_t cap);

// Thread-safe API (UI task publishes, web task reads)
void publish(const Source &src);   // Bumps the version; copies the map only when it changed
void invalidateMap();              // World reloaded / restored: resend the map even if the revision matches
void setInactive();                // Leaving the adventure (bumps the version once)
bool active();
uint32_t version();
size_t writeJson(uint32_t clientMapRev, char *buf, size_t cap);   // Locked toJson() of the snapshot
// {"active":bool,"version":N,"world":".."} only (the /game page checks for a running adventure).
size_t writeBriefJson(char *buf, size_t cap);

}  // namespace AdvLive
