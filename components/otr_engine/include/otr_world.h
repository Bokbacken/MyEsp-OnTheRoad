// otr_world.h - OnTheRoad Engine: tile world logic (pure C++, host tested, no LVGL / ESP-IDF).
//
// 2D adventure engine (see docs/ENGINE_2D_BRIEF.md). Strictly grid based:
//   terrain  one Tile per cell (grass, sand, floor, wall, cracked wall, water, lava, ...)
//   object   at most one Obj per cell on top of the terrain (rock, crate, bush, key, door, items)
//   hidden   v0.8.0: a treasure layer - an item under a crate / bush / cracked wall / rock that
//            appears when the covering thing goes away (data driven, see MapDef::treasures)
//   fog      v0.8.0: minesweeper worlds hide the ground until it is revealed; revealed cells show
//            the number of hidden mines around them, zeros flood-reveal, cells can be flagged
//   player   a cell position + facing direction + energy / lives / suit / inventory.
//
// Rules:
//   move      Walks one cell. Rocks are pushed if the cell behind them is free; a rock pushed into
//             water becomes a bridge, into lava it cools to basalt. Doors open with a key (the move
//             that opens the door does not walk through it). Items are picked up by walking onto
//             them. Water needs the boat. Lava costs Rules::lavaDamage energy per step unless the
//             protective suit has charges left. Mines cost Rules::mineDamage and explode.
//   energy    0 energy = one life lost, respawn at the last checkpoint (or start) with full energy.
//             Finite lives: 0 lives left = game over. Infinite lives: only "lives used" is counted.
//   hit       Hits the facing cell: crates and bushes break at once, cracked walls after CRACK_HITS.
//   plates    When every pressure plate carries a rock all gates open (and close again otherwise).
//
// Maps are ASCII rows (see otr_maps.h for the legend). The world is trivially constructible and
// ~29 kB, so the UI allocates it in PSRAM. Planner (A*) is separate and also ~70 kB (PSRAM).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Otr {

constexpr int MAX_W = 80;   // v0.17.0: raised from 64 for larger SD-authored adventures (map cells live in PSRAM)
constexpr int MAX_H = 80;
constexpr int CRACK_HITS = 2;
constexpr int LIVES_INFINITE = -1;
constexpr int MAX_LIVES = 9;

enum class Tile : uint8_t {
    GRASS, SAND, FLOOR, RUBBLE, WALL, CRACKED, TREE, WATER, BRIDGE, LAVA, BASALT, PLATE, GATE,
    CHECKPOINT, EXIT, CRATER,
    COUNT
};

enum class Obj : uint8_t {
    NONE, ROCK, CRATE, KEY, DOOR, BOAT, GEM,
    BUSH, POTION, HEART, SUIT, LIFE, MINE,
    NPC,      // v0.11.0: a person (objAmount = map number 1..9, see otr_story.h); blocks the cell
    BOMB3, BOMB4, // Campaign consumables, appended to preserve old object ids
    COUNT
};

enum class Dir : uint8_t { UP, RIGHT, DOWN, LEFT };
inline int dirX(Dir d) { return d == Dir::RIGHT ? 1 : d == Dir::LEFT ? -1 : 0; }
inline int dirY(Dir d) { return d == Dir::DOWN ? 1 : d == Dir::UP ? -1 : 0; }

// Bit flags returned by move() / hit() so the UI can animate and give feedback.
enum Event : uint32_t {
    EV_NONE = 0,
    EV_MOVED = 1u << 0,
    EV_BLOCKED = 1u << 1,
    EV_PUSHED = 1u << 2,
    EV_HIT = 1u << 3,       // Something solid was hit
    EV_BROKE = 1u << 4,     // ... and it broke
    EV_KEY = 1u << 5,
    EV_GEM = 1u << 6,
    EV_BOAT = 1u << 7,
    EV_DOOR = 1u << 8,      // A door was unlocked (one key used)
    EV_NEED_KEY = 1u << 9,
    EV_SPLASH = 1u << 10,   // Rock pushed into water -> bridge
    EV_SIZZLE = 1u << 11,   // Rock pushed into lava -> basalt
    EV_BURNED = 1u << 12,   // Lava step cost energy (or the life)
    EV_GATE_OPEN = 1u << 13,
    EV_GATE_CLOSE = 1u << 14,
    EV_ALL_GEMS = 1u << 15,
    EV_SAILING = 1u << 16,  // The player is now on water (in the boat)
    // v0.8.0
    EV_TREASURE = 1u << 17,   // A hidden item appeared
    EV_POTION = 1u << 18,
    EV_HEART = 1u << 19,
    EV_SUIT = 1u << 20,
    EV_LIFE = 1u << 21,
    EV_HURT = 1u << 22,       // Energy lost
    EV_SUIT_USED = 1u << 23,  // A lava step was absorbed by the suit
    EV_DIED = 1u << 24,       // Energy 0: a life lost, player respawned
    EV_GAME_OVER = 1u << 25,  // No lives left (finite lives)
    EV_MINE = 1u << 26,       // Stepped on a mine
    EV_CHECKPOINT = 1u << 27, // New respawn point
    EV_EXIT = 1u << 28,       // Reached the exit
    EV_REVEALED = 1u << 29,   // Fog cells were revealed
    // v0.11.0
    EV_TALK = 1u << 30,       // Walked into / hit a person: start a conversation (talkNpc())
};

// Per world / game configuration (data driven, part of MapDef). Plain aggregate.
struct Rules {
    uint8_t startEnergy;      // Energy at the start (and the first max)
    uint8_t energyCap;        // Max energy can grow up to this (hearts)
    int8_t lives;             // LIVES_INFINITE or 1..MAX_LIVES
    uint8_t lavaDamage;       // Energy per lava step without suit charge (>= energy = deadly)
    uint8_t mineDamage;       // Energy per mine
    uint8_t suitBaseSteps;    // Suit charges at pickup ...
    uint8_t suitPerExtraMax;  // ... + this per permanent extra energy unit (max - start)
    uint8_t potionDefault;    // Energy restored by a potion without an explicit amount
    uint8_t heartDefault;     // Max energy added by a heart without an explicit amount
    bool fog;                 // Minesweeper world: ground hidden until revealed
    uint8_t mineLevel;        // 0 = ordinary map. 1..12 = true minesweeper (tap-reveal, flag, bomb,
                              // win when every non-mine cell is revealed). Implies fog (forced on in
                              // World::load). SD .adv sets this via @rules "mine_level: N".
};

// v0.7.0 behaviour: lava sends you straight back to the start, nothing else costs energy.
constexpr Rules CLASSIC_RULES = {8, 20, LIVES_INFINITE, 255, 4, 3, 1, 2, 1, false, 0};

// A hidden item: appears at (x, y) when the cell's covering object / cracked wall is gone.
struct Treasure {
    uint8_t x, y;
    Obj item;
    uint8_t amount;   // Potion: energy, heart: max units, 0 = Rules default
};

struct MapDef {
    uint8_t id;                     // Stable id (save files)
    const char *const *rows;
    int h;
    Rules rules;
    const Treasure *treasures;
    int treasureCount;
};

struct MineLevel { uint8_t w, h, mines; };
constexpr int MINE_LEVELS = 12;
extern const MineLevel MINE_LEVEL[MINE_LEVELS];
struct BombArea { int x0, y0, x1, y1; }; // Inclusive, clipped; anchor is second row/column
class Planner;

class World {
public:
    // rows: h strings of equal length (1..MAX_W), exactly one 'S'. false on any error.
    bool load(const char *const *rows, int h, const Rules &rules = CLASSIC_RULES);
    bool load(const MapDef &def);   // Rows + rules + treasures

    int width() const { return w_; }
    int height() const { return h_; }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }
    Tile tile(int x, int y) const { return inside(x, y) ? (Tile)terrain_[y][x] : Tile::WALL; }
    // Visible object (hidden mines are reported as NONE until they explode / game over).
    Obj obj(int x, int y) const;
    Obj rawObj(int x, int y) const { return inside(x, y) ? (Obj)obj_[y][x] : Obj::NONE; }
    int objAmount(int x, int y) const { return inside(x, y) ? val_[y][x] : 0; }
    Obj hidden(int x, int y) const { return inside(x, y) ? (Obj)hid_[y][x] : Obj::NONE; }
    int damage(int x, int y) const { return inside(x, y) ? dmg_[y][x] : 0; }
    const Rules &rules() const { return rules_; }
    uint8_t mapId() const { return mapId_; }

    // Fog (minesweeper)
    bool fog() const { return rules_.fog; }
    bool revealed(int x, int y) const { return !rules_.fog || (inside(x, y) && (flags_[y][x] & F_REVEALED)); }
    bool flagged(int x, int y) const { return inside(x, y) && (flags_[y][x] & F_FLAG); }
    int mineHint(int x, int y) const;   // Mines in the 8 neighbours
    bool toggleFlag(int x, int y);      // Only on unrevealed fog cells; true if changed

    int playerX() const { return px_; }
    int playerY() const { return py_; }
    Dir facing() const { return facing_; }
    bool sailing() const { return tile(px_, py_) == Tile::WATER; }
    int keys() const { return keys_; }
    int gems() const { return gems_; }
    int gemsTotal() const { return gemsTotal_; }
    bool hasBoat() const { return boat_; }
    bool gatesOpen() const { return gateOpen_; }
    int steps() const { return steps_; }
    uint32_t revision() const { return rev_; }   // Bumped on every terrain / object / fog change

    int energy() const { return energy_; }
    int maxEnergy() const { return maxEnergy_; }
    int livesLeft() const { return livesLeft_; }   // LIVES_INFINITE when infinite
    int livesUsed() const { return livesUsed_; }
    int suitCharges() const { return suit_; }
    int suitChargesAtPickup() const;              // Formula below, for the current max energy
    bool gameOver() const { return gameOver_; }
    bool exitReached() const { return exitReached_; }
    int checkpointX() const { return cpX_; }
    int checkpointY() const { return cpY_; }

    uint32_t move(Dir d);   // Turns to d, then walks / pushes / opens
    uint32_t hit();         // Hits the facing cell
    void revive();          // After game over ("continue"): one life, full energy, at the checkpoint

    bool playerCanEnter(int x, int y) const;     // Terrain + object check (no side effects)

    // People (v0.11.0). A person stands on a cell like a rock that cannot be pushed; walking into
    // it or hitting it returns EV_TALK and remembers its number in talkNpc().
    int npcAt(int x, int y) const { return rawObj(x, y) == Obj::NPC ? val_[y][x] : 0; }
    int facingNpc() const { return npcAt(px_ + dirX(facing_), py_ + dirY(facing_)); }
    int talkNpc() const { return talkNpc_; }    // Person of the last EV_TALK
    bool findNpc(int num, int &x, int &y) const;
    // Moves person num to (x, y): free ground (no object, not the player). False = unchanged.
    bool moveNpc(int num, int x, int y);
    bool removeNpc(int num);
    // Story effects on the world
    bool clearObj(int x, int y);                 // Opens a door / removes a rock, crate or bush
    void addKeys(int n);

    // v0.17.0: granular setters used by SD adventures to carry selected player resources from the
    // previous map into a freshly loaded next map (see adv_sd_session). No map/terrain side
    // effects; values are clamped to valid ranges. maxEnergy is kept >= energy.
    void setEnergy(int e) { energy_ = e < 0 ? 0 : (e > maxEnergy_ ? maxEnergy_ : e); rev_++; }
    void setMaxEnergy(int m) { if (m < 1) m = 1; maxEnergy_ = m; if (energy_ > m) energy_ = m; rev_++; }
    void setSuitCharges(int c) { suit_ = c < 0 ? 0 : c; rev_++; }
    void setKeys(int k) { keys_ = k < 0 ? 0 : k; rev_++; }
    void setBoat(bool b) { boat_ = b; rev_++; }
    void setLives(int left, int used) { livesLeft_ = left; livesUsed_ = used < 0 ? 0 : used; rev_++; }

    // Campaign and map travel, no rendering / storage dependencies.
    bool loadMines(int level);
    int mineLevel() const { return mineLevel_; } // 0 = legacy/non-campaign world
    int bombs(int size) const { return size == 3 ? bomb3_ : size == 4 ? bomb4_ : 0; }
    int safeLeft() const;
    BombArea bombArea(int size, int x, int y) const;
    bool placeBomb(int size, int x, int y); // Only confirmation calls this; no mutation on failure
    bool spawnAt(int x, int y); // Safe, empty, non-portal ground only
    void carryFrom(const World &from); // Global inventory, not map-local gems/checkpoint

    // Save / restore of the complete mutable state. Format (little endian, see otr_world.cpp):
    // header {magic "OTRS", version, map id, w, h, body size} + body + CRC32. load() of the map
    // must have been done first; restore() validates everything and leaves the world untouched
    // (freshly loaded) on any error.
    static constexpr uint32_t SAVE_MAGIC = 0x5352544F;   // "OTRS"
    static constexpr uint16_t SAVE_VERSION = 2;
    size_t saveSize() const;
    size_t save(uint8_t *buf, size_t cap) const;   // Bytes written, 0 if cap too small
    bool restore(const uint8_t *buf, size_t n);

private:
    friend class Planner;
    enum : uint8_t { F_REVEALED = 1, F_FLAG = 2 };
    bool rockCanEnter(int x, int y) const;
    void updateGates(uint32_t &ev);
    void set(int x, int y, Tile t) { terrain_[y][x] = (uint8_t)t; rev_++; }
    void set(int x, int y, Obj o) { obj_[y][x] = (uint8_t)o; rev_++; }
    void uncover(int x, int y, uint32_t &ev);   // Hidden treasure appears if the cell is free
    void reveal(int x, int y, uint32_t &ev);    // Fog flood fill from (x, y)
    void hurt(int amount, uint32_t &ev);
    void respawn();
    uint32_t pickup(uint32_t ev);

    uint8_t terrain_[MAX_H][MAX_W];
    uint8_t obj_[MAX_H][MAX_W];
    uint8_t val_[MAX_H][MAX_W];     // Item amount for POTION / HEART (0 = default)
    uint8_t hid_[MAX_H][MAX_W];     // Hidden treasure item
    uint8_t hidVal_[MAX_H][MAX_W];
    uint8_t flags_[MAX_H][MAX_W];   // F_REVEALED | F_FLAG
    uint8_t dmg_[MAX_H][MAX_W];
    Rules rules_;
    int w_, h_;
    int px_, py_, startX_, startY_, cpX_, cpY_;
    Dir facing_;
    int keys_, gems_, gemsTotal_, plates_, steps_;
    int energy_, maxEnergy_, livesLeft_, livesUsed_, suit_;
    bool boat_, gateOpen_, gameOver_, exitReached_;
    uint8_t mapId_;
    uint8_t talkNpc_;
    uint8_t mineLevel_, bomb3_, bomb4_;
    void checkMineWin();
    uint32_t rev_;
};

// ---- Tap-to-walk pathfinding (v0.8.0) --------------------------------------------------------
enum class WalkMode : uint8_t { PRO, ASSIST };

enum class PlanResult : uint8_t {
    OK,            // Path to the goal
    HERE,          // Goal is the player's cell
    INVALID,       // Not a place you can walk to (tree, wall, crate, ...)
    UNREACHABLE,   // No way there
    DANGER,        // Assist: the way needs deadly lava - path ends on the last safe cell
    NEED_BOAT,     // Water without boat - path ends at the shore
    UNKNOWN,       // Assist, fog world: the way crosses unrevealed ground - path ends before it
    SUIT,          // Assist: OK, but lava is crossed with the suit's charges
};

constexpr int MAX_PATH = MAX_W * MAX_H;

struct Path {
    PlanResult result;
    int goalX, goalY;      // Requested (snapped) goal
    int len;               // Steps to walk (possibly truncated, see result)
    uint8_t dirs[MAX_PATH];
};

class Planner {
public:
    // Picks the target cell for a touch at world position (fx, fy) in tile units (fractional).
    // A clear tap on a passable cell selects it. A tap within SNAP of an edge / corner of a
    // passable neighbour snaps to that neighbour (nearest first). Otherwise false (obstacle).
    static constexpr float SNAP = 1.f / 3.f;
    static bool pickTarget(const World &w, float fx, float fy, int &tx, int &ty);
    static bool targetable(const World &w, int x, int y);

    // A* from the player to (tx, ty). Bounded: every cell is settled at most once.
    PlanResult plan(const World &w, int tx, int ty, WalkMode mode, Path &out);

private:
    enum Cat : uint8_t { C_BLOCK, C_FREE, C_LAVA, C_BOAT, C_UNKNOWN, C_DEADLY };
    Cat category(const World &w, int x, int y, WalkMode mode) const;
    bool search(const World &w, int tx, int ty, WalkMode mode, Path &out);
    void heapPush(uint64_t key);   // v0.17.0: 64-bit key packs f<<16 | cell (cell up to 80*80-1 = 6399 > 12-bit)
    uint64_t heapPop();

    static constexpr uint32_t STEP_COST = 4;   // Cost of one ground step (see search())
    static constexpr int N = MAX_W * MAX_H;
    static constexpr int HEAP = 4 * N;
    uint32_t cost_[N];
    int16_t prev_[N];
    uint8_t closed_[N];
    uint64_t heap_[HEAP];
    int heapLen_;
};

}  // namespace Otr
