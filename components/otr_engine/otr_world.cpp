#include "otr_world.h"

#include <string.h>

namespace Otr {

namespace {

struct Cell {
    Tile t;
    Obj o;
};

// Map legend (keep in sync with otr_maps.h). Returns false for unknown characters.
bool decode(char c, Cell &out)
{
    switch (c) {
    case '.': out = {Tile::GRASS, Obj::NONE}; return true;
    case 'S': out = {Tile::GRASS, Obj::NONE}; return true;
    case ',': out = {Tile::SAND, Obj::NONE}; return true;
    case '_': out = {Tile::FLOOR, Obj::NONE}; return true;
    case '#': out = {Tile::WALL, Obj::NONE}; return true;
    case '%': out = {Tile::CRACKED, Obj::NONE}; return true;
    case 'T': out = {Tile::TREE, Obj::NONE}; return true;
    case '~': out = {Tile::WATER, Obj::NONE}; return true;
    case '^': out = {Tile::LAVA, Obj::NONE}; return true;
    case 'o': out = {Tile::PLATE, Obj::NONE}; return true;
    case '=': out = {Tile::GATE, Obj::NONE}; return true;
    case 'R': out = {Tile::GRASS, Obj::ROCK}; return true;
    case 'r': out = {Tile::FLOOR, Obj::ROCK}; return true;
    case 'c': out = {Tile::GRASS, Obj::CRATE}; return true;
    case 'C': out = {Tile::FLOOR, Obj::CRATE}; return true;
    case 'k': out = {Tile::GRASS, Obj::KEY}; return true;
    case 'K': out = {Tile::FLOOR, Obj::KEY}; return true;
    case 'D': out = {Tile::FLOOR, Obj::DOOR}; return true;
    case 'B': out = {Tile::SAND, Obj::BOAT}; return true;
    case '*': out = {Tile::GRASS, Obj::GEM}; return true;
    case '+': out = {Tile::FLOOR, Obj::GEM}; return true;
    case '$': out = {Tile::SAND, Obj::GEM}; return true;
    default: return false;
    }
}

// Ground that both the player and rocks can stand on.
bool isGround(Tile t)
{
    switch (t) {
    case Tile::GRASS: case Tile::SAND: case Tile::FLOOR: case Tile::RUBBLE:
    case Tile::BRIDGE: case Tile::BASALT: case Tile::PLATE:
        return true;
    default:
        return false;
    }
}

}  // namespace

bool World::load(const char *const *rows, int h)
{
    memset(this, 0, sizeof(*this));
    facing_ = Dir::DOWN;
    if (!rows || h <= 0 || h > MAX_H || !rows[0]) return false;
    const size_t w = strlen(rows[0]);
    if (w == 0 || w > (size_t)MAX_W) return false;
    int starts = 0;
    for (int y = 0; y < h; y++) {
        if (!rows[y] || strlen(rows[y]) != w) return false;
        for (int x = 0; x < (int)w; x++) {
            Cell c;
            if (!decode(rows[y][x], c)) return false;
            terrain_[y][x] = (uint8_t)c.t;
            obj_[y][x] = (uint8_t)c.o;
            if (c.o == Obj::GEM) gemsTotal_++;
            if (c.t == Tile::PLATE) plates_++;
            if (rows[y][x] == 'S') {
                startX_ = px_ = x;
                startY_ = py_ = y;
                starts++;
            }
        }
    }
    if (starts != 1) return false;
    w_ = (int)w;
    h_ = h;
    uint32_t ev = 0;
    updateGates(ev);
    rev_ = 1;
    return true;
}

bool World::playerCanEnter(int x, int y) const
{
    if (!inside(x, y)) return false;
    const Obj o = obj(x, y);
    if (o == Obj::ROCK || o == Obj::CRATE || o == Obj::DOOR) return false;
    const Tile t = tile(x, y);
    if (isGround(t) || t == Tile::LAVA) return true;   // Lava: allowed, but it burns
    if (t == Tile::WATER) return boat_;
    if (t == Tile::GATE) return gateOpen_;
    return false;
}

bool World::rockCanEnter(int x, int y) const
{
    if (!inside(x, y) || obj(x, y) != Obj::NONE) return false;
    const Tile t = tile(x, y);
    return isGround(t) || t == Tile::WATER || t == Tile::LAVA;   // Never onto gates
}

void World::updateGates(uint32_t &ev)
{
    bool open = plates_ > 0;
    for (int y = 0; y < h_ && open; y++)
        for (int x = 0; x < w_; x++)
            if (terrain_[y][x] == (uint8_t)Tile::PLATE && obj_[y][x] != (uint8_t)Obj::ROCK) {
                open = false;
                break;
            }
    if (open != gateOpen_) {
        gateOpen_ = open;
        rev_++;
        ev |= open ? EV_GATE_OPEN : EV_GATE_CLOSE;
    }
}

uint32_t World::move(Dir d)
{
    facing_ = d;
    const int tx = px_ + dirX(d), ty = py_ + dirY(d);
    if (!inside(tx, ty)) return EV_BLOCKED;
    uint32_t ev = 0;

    switch (obj(tx, ty)) {
    case Obj::DOOR:
        if (keys_ <= 0) return EV_BLOCKED | EV_NEED_KEY;
        keys_--;
        set(tx, ty, Obj::NONE);
        return EV_DOOR;
    case Obj::CRATE:
        return EV_BLOCKED;
    case Obj::ROCK: {
        const int bx = tx + dirX(d), by = ty + dirY(d);
        if (!rockCanEnter(bx, by)) return EV_BLOCKED;
        set(tx, ty, Obj::NONE);
        const Tile under = tile(bx, by);
        if (under == Tile::WATER) {
            set(bx, by, Tile::BRIDGE);
            ev |= EV_SPLASH;
        } else if (under == Tile::LAVA) {
            set(bx, by, Tile::BASALT);
            ev |= EV_SIZZLE;
        } else {
            set(bx, by, Obj::ROCK);
        }
        ev |= EV_PUSHED;
        updateGates(ev);
        break;
    }
    default:
        break;
    }

    if (!playerCanEnter(tx, ty)) return ev | EV_BLOCKED;
    px_ = tx;
    py_ = ty;
    steps_++;
    ev |= EV_MOVED;

    if (tile(px_, py_) == Tile::LAVA) {   // Friendly: no lives, just back to the start
        px_ = startX_;
        py_ = startY_;
        facing_ = Dir::DOWN;
        return ev | EV_BURNED;
    }
    if (tile(px_, py_) == Tile::WATER) ev |= EV_SAILING;

    switch (obj(px_, py_)) {
    case Obj::KEY:
        keys_++;
        set(px_, py_, Obj::NONE);
        ev |= EV_KEY;
        break;
    case Obj::GEM:
        gems_++;
        set(px_, py_, Obj::NONE);
        ev |= EV_GEM;
        if (gems_ == gemsTotal_) ev |= EV_ALL_GEMS;
        break;
    case Obj::BOAT:
        boat_ = true;
        set(px_, py_, Obj::NONE);
        ev |= EV_BOAT;
        break;
    default:
        break;
    }
    return ev;
}

uint32_t World::hit()
{
    const int tx = px_ + dirX(facing_), ty = py_ + dirY(facing_);
    if (!inside(tx, ty)) return EV_NONE;
    const Obj o = obj(tx, ty);
    if (o == Obj::CRATE) {
        set(tx, ty, Obj::NONE);
        return EV_HIT | EV_BROKE;
    }
    if (o == Obj::ROCK || o == Obj::DOOR) return EV_HIT;
    const Tile t = tile(tx, ty);
    if (t == Tile::CRACKED) {
        if (++dmg_[ty][tx] >= CRACK_HITS) {
            dmg_[ty][tx] = 0;
            set(tx, ty, Tile::RUBBLE);
            return EV_HIT | EV_BROKE;
        }
        rev_++;
        return EV_HIT;
    }
    if (t == Tile::WALL || t == Tile::TREE || (t == Tile::GATE && !gateOpen_)) return EV_HIT;
    return EV_NONE;   // Swing at nothing
}

}  // namespace Otr
