// otr_world.h - OnTheRoad Engine: tile world logic (pure C++, host tested, no LVGL / ESP-IDF).
//
// First step of the 2D adventure engine (see docs/ENGINE_2D_BRIEF.md). Strictly grid based:
//   terrain  one Tile per cell (grass, sand, floor, wall, cracked wall, water, lava, ...)
//   object   at most one Obj per cell on top of the terrain (rock, crate, key, door, boat, gem)
//   player   a cell position + facing direction; the UI animates between cells.
//
// Rules of the test world:
//   move      Walks one cell. Rocks are pushed if the cell behind them is free; a rock pushed into
//             water becomes a bridge, into lava it cools to basalt. Doors open with a key (the move
//             that opens the door does not walk through it). Keys, gems and the boat are picked up
//             by walking onto them. Water needs the boat. Lava sends the player back to the start.
//   hit       Hits the facing cell: crates break at once, cracked walls after CRACK_HITS hits.
//   plates    When every pressure plate carries a rock all gates open (and close again otherwise).
//
// Maps are ASCII rows (see otr_maps.h for the legend). The world is trivially constructible and
// ~12 kB, so the UI allocates it in PSRAM.
#pragma once

#include <stdint.h>

namespace Otr {

constexpr int MAX_W = 64;
constexpr int MAX_H = 64;
constexpr int CRACK_HITS = 2;

enum class Tile : uint8_t {
    GRASS, SAND, FLOOR, RUBBLE, WALL, CRACKED, TREE, WATER, BRIDGE, LAVA, BASALT, PLATE, GATE,
    COUNT
};

enum class Obj : uint8_t { NONE, ROCK, CRATE, KEY, DOOR, BOAT, GEM, COUNT };

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
    EV_BURNED = 1u << 12,   // Walked into lava -> back to start
    EV_GATE_OPEN = 1u << 13,
    EV_GATE_CLOSE = 1u << 14,
    EV_ALL_GEMS = 1u << 15,
    EV_SAILING = 1u << 16,  // The player is now on water (in the boat)
};

class World {
public:
    // rows: h strings of equal length (1..MAX_W), exactly one 'S'. false on any error.
    bool load(const char *const *rows, int h);

    int width() const { return w_; }
    int height() const { return h_; }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w_ && y < h_; }
    Tile tile(int x, int y) const { return inside(x, y) ? (Tile)terrain_[y][x] : Tile::WALL; }
    Obj obj(int x, int y) const { return inside(x, y) ? (Obj)obj_[y][x] : Obj::NONE; }
    int damage(int x, int y) const { return inside(x, y) ? dmg_[y][x] : 0; }

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
    uint32_t revision() const { return rev_; }   // Bumped on every terrain / object change

    uint32_t move(Dir d);   // Turns to d, then walks / pushes / opens
    uint32_t hit();         // Hits the facing cell

    bool playerCanEnter(int x, int y) const;     // Terrain + object check (no side effects)

private:
    bool rockCanEnter(int x, int y) const;
    void updateGates(uint32_t &ev);
    void set(int x, int y, Tile t) { terrain_[y][x] = (uint8_t)t; rev_++; }
    void set(int x, int y, Obj o) { obj_[y][x] = (uint8_t)o; rev_++; }

    uint8_t terrain_[MAX_H][MAX_W];
    uint8_t obj_[MAX_H][MAX_W];
    uint8_t dmg_[MAX_H][MAX_W];
    int w_, h_;
    int px_, py_, startX_, startY_;
    Dir facing_;
    int keys_, gems_, gemsTotal_, plates_, steps_;
    bool boat_, gateOpen_;
    uint32_t rev_;
};

}  // namespace Otr
