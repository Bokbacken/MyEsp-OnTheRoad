#include "otr_world.h"

#include <math.h>
#include <stdlib.h>
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
    case 'Q': out = {Tile::CHECKPOINT, Obj::NONE}; return true;
    case 'X': out = {Tile::EXIT, Obj::NONE}; return true;
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
    case 'b': out = {Tile::GRASS, Obj::BUSH}; return true;
    case 'p': out = {Tile::GRASS, Obj::POTION}; return true;
    case 'h': out = {Tile::GRASS, Obj::HEART}; return true;
    case 'u': out = {Tile::GRASS, Obj::SUIT}; return true;
    case 'l': out = {Tile::GRASS, Obj::LIFE}; return true;
    case 'm': out = {Tile::GRASS, Obj::MINE}; return true;
    default: return false;
    }
}

// Ground that both the player and rocks can stand on.
bool isGround(Tile t)
{
    switch (t) {
    case Tile::GRASS: case Tile::SAND: case Tile::FLOOR: case Tile::RUBBLE:
    case Tile::BRIDGE: case Tile::BASALT: case Tile::PLATE: case Tile::CHECKPOINT:
    case Tile::EXIT: case Tile::CRATER:
        return true;
    default:
        return false;
    }
}

bool blocksWalk(Obj o) { return o == Obj::ROCK || o == Obj::CRATE || o == Obj::DOOR || o == Obj::BUSH; }
bool isItem(Obj o)
{
    return o == Obj::KEY || o == Obj::GEM || o == Obj::BOAT || o == Obj::POTION || o == Obj::HEART ||
           o == Obj::SUIT || o == Obj::LIFE;
}
bool alwaysVisible(Tile t) { return t == Tile::WALL || t == Tile::TREE || t == Tile::CRACKED; }

bool rulesValid(const Rules &r)
{
    return r.startEnergy >= 1 && r.energyCap >= r.startEnergy &&
           (r.lives == LIVES_INFINITE || (r.lives >= 1 && r.lives <= MAX_LIVES));
}

uint32_t crc32(const uint8_t *p, size_t n, uint32_t crc = 0)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

}  // namespace

// ---- Loading --------------------------------------------------------------------------------

bool World::load(const char *const *rows, int h, const Rules &rules)
{
    memset(this, 0, sizeof(*this));
    facing_ = Dir::DOWN;
    rules_ = rules;
    if (!rulesValid(rules)) return false;
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
    cpX_ = startX_;
    cpY_ = startY_;
    energy_ = maxEnergy_ = rules.startEnergy;
    livesLeft_ = rules.lives;
    uint32_t ev = 0;
    updateGates(ev);
    if (rules_.fog) {
        for (int y = 0; y < h_; y++)
            for (int x = 0; x < w_; x++)
                if (alwaysVisible(tile(x, y))) flags_[y][x] |= F_REVEALED;
        reveal(px_, py_, ev);
    }
    rev_ = 1;
    return true;
}

bool World::load(const MapDef &def)
{
    if (!load(def.rows, def.h, def.rules)) return false;
    mapId_ = def.id;
    for (int i = 0; i < def.treasureCount && def.treasures; i++) {
        const Treasure &t = def.treasures[i];
        if (!inside(t.x, t.y) || !isItem(t.item) || hid_[t.y][t.x]) return false;
        hid_[t.y][t.x] = (uint8_t)t.item;
        hidVal_[t.y][t.x] = t.amount;
        if (t.item == Obj::GEM) gemsTotal_++;
        uint32_t ev = 0;
        uncover(t.x, t.y, ev);   // Nothing covers it: visible from the start
    }
    rev_ = 1;
    return true;
}

// ---- Queries --------------------------------------------------------------------------------

Obj World::obj(int x, int y) const
{
    const Obj o = rawObj(x, y);
    return o == Obj::MINE ? Obj::NONE : o;   // Mines are never visible
}

int World::mineHint(int x, int y) const
{
    int n = 0;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if ((dx || dy) && rawObj(x + dx, y + dy) == Obj::MINE) n++;
    return n;
}

bool World::toggleFlag(int x, int y)
{
    if (!rules_.fog || !inside(x, y) || revealed(x, y) || (x == px_ && y == py_)) return false;
    flags_[y][x] ^= F_FLAG;
    rev_++;
    return true;
}

int World::suitChargesAtPickup() const
{
    const int extra = maxEnergy_ > rules_.startEnergy ? maxEnergy_ - rules_.startEnergy : 0;
    return rules_.suitBaseSteps + rules_.suitPerExtraMax * extra;
}

bool World::playerCanEnter(int x, int y) const
{
    if (!inside(x, y)) return false;
    if (blocksWalk(obj(x, y))) return false;
    const Tile t = tile(x, y);
    if (isGround(t) || t == Tile::LAVA) return true;   // Lava: allowed, but it burns
    if (t == Tile::WATER) return boat_;
    if (t == Tile::GATE) return gateOpen_;
    return false;
}

bool World::rockCanEnter(int x, int y) const
{
    if (!inside(x, y) || rawObj(x, y) != Obj::NONE) return false;
    const Tile t = tile(x, y);
    if (t == Tile::CHECKPOINT || t == Tile::EXIT) return false;
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

// ---- Mutations ------------------------------------------------------------------------------

void World::uncover(int x, int y, uint32_t &ev)
{
    if (!hid_[y][x] || obj_[y][x] != (uint8_t)Obj::NONE || tile(x, y) == Tile::CRACKED) return;
    obj_[y][x] = hid_[y][x];
    val_[y][x] = hidVal_[y][x];
    hid_[y][x] = 0;
    hidVal_[y][x] = 0;
    rev_++;
    ev |= EV_TREASURE;
}

// Reveals (x, y); zero cells reveal their neighbours (classic flood fill, flags stay hidden).
// Done by sweeping instead of a queue so no large stack / static buffer is needed: each sweep
// expands all zero cells, a sweep that reveals nothing ends it (60 x 60: a few sweeps).
void World::reveal(int x, int y, uint32_t &ev)
{
    if (!rules_.fog || !inside(x, y) || (flags_[y][x] & F_REVEALED)) return;
    flags_[y][x] = (uint8_t)((flags_[y][x] | F_REVEALED) & ~F_FLAG);
    ev |= EV_REVEALED;
    rev_++;
    if (rawObj(x, y) == Obj::MINE || mineHint(x, y)) return;
    for (bool grew = true; grew;) {
        grew = false;
        for (int cy = 0; cy < h_; cy++)
            for (int cx = 0; cx < w_; cx++) {
                if (!(flags_[cy][cx] & F_REVEALED) || alwaysVisible(tile(cx, cy)) || rawObj(cx, cy) == Obj::MINE ||
                    mineHint(cx, cy))
                    continue;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        const int nx = cx + dx, ny = cy + dy;
                        if (!inside(nx, ny) || (flags_[ny][nx] & (F_REVEALED | F_FLAG))) continue;
                        if (rawObj(nx, ny) == Obj::MINE) continue;
                        flags_[ny][nx] |= F_REVEALED;
                        grew = true;
                    }
            }
    }
}

void World::respawn()
{
    px_ = cpX_;
    py_ = cpY_;
    facing_ = Dir::DOWN;
    energy_ = maxEnergy_;
    rev_++;
}

void World::hurt(int amount, uint32_t &ev)
{
    ev |= EV_HURT;
    energy_ -= amount;
    if (energy_ > 0) return;
    energy_ = 0;
    livesUsed_++;
    ev |= EV_DIED;
    if (livesLeft_ != LIVES_INFINITE && --livesLeft_ <= 0) {
        livesLeft_ = 0;
        gameOver_ = true;
        ev |= EV_GAME_OVER;
        rev_++;
        return;
    }
    respawn();
}

void World::revive()
{
    if (!gameOver_) return;
    gameOver_ = false;
    livesLeft_ = 1;
    respawn();
}

uint32_t World::pickup(uint32_t ev)
{
    const Obj o = obj(px_, py_);
    if (!isItem(o)) return ev;
    const int amount = val_[py_][px_];
    switch (o) {
    case Obj::KEY: keys_++; ev |= EV_KEY; break;
    case Obj::GEM:
        gems_++;
        ev |= EV_GEM;
        if (gems_ == gemsTotal_) ev |= EV_ALL_GEMS;
        break;
    case Obj::BOAT: boat_ = true; ev |= EV_BOAT; break;
    case Obj::POTION: {
        const int a = amount ? amount : rules_.potionDefault;
        energy_ = energy_ + a > maxEnergy_ ? maxEnergy_ : energy_ + a;
        ev |= EV_POTION;
        break;
    }
    case Obj::HEART: {
        const int a = amount ? amount : rules_.heartDefault;
        const int old = maxEnergy_;
        maxEnergy_ = maxEnergy_ + a > rules_.energyCap ? rules_.energyCap : maxEnergy_ + a;
        const int gained = maxEnergy_ - old;
        energy_ += gained;
        if (suit_ > 0) suit_ += gained * rules_.suitPerExtraMax;   // Worn suit lasts longer too
        ev |= EV_HEART;
        break;
    }
    case Obj::SUIT: {
        const int c = suitChargesAtPickup();
        if (c > suit_) suit_ = c;
        ev |= EV_SUIT;
        break;
    }
    case Obj::LIFE:
        if (livesLeft_ == LIVES_INFINITE) energy_ = maxEnergy_;
        else if (livesLeft_ < MAX_LIVES) livesLeft_++;
        ev |= EV_LIFE;
        break;
    default: break;
    }
    obj_[py_][px_] = (uint8_t)Obj::NONE;
    val_[py_][px_] = 0;
    rev_++;
    return ev;
}

uint32_t World::move(Dir d)
{
    if (gameOver_) return EV_NONE;
    facing_ = d;
    const int tx = px_ + dirX(d), ty = py_ + dirY(d);
    if (!inside(tx, ty)) return EV_BLOCKED;
    uint32_t ev = 0;

    switch (obj(tx, ty)) {
    case Obj::DOOR:
        if (keys_ <= 0) return EV_BLOCKED | EV_NEED_KEY;
        keys_--;
        set(tx, ty, Obj::NONE);
        uncover(tx, ty, ev);
        return ev | EV_DOOR;
    case Obj::CRATE:
    case Obj::BUSH:
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
        uncover(tx, ty, ev);   // Something under the rock's old place?
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

    if (rawObj(px_, py_) == Obj::MINE) {   // Boom: the mine is gone, a crater remains
        obj_[py_][px_] = (uint8_t)Obj::NONE;
        terrain_[py_][px_] = (uint8_t)Tile::CRATER;
        if (rules_.fog) flags_[py_][px_] = F_REVEALED;
        rev_++;
        ev |= EV_MINE;
        hurt(rules_.mineDamage, ev);
        if (ev & EV_DIED) return ev;
    }
    if (rules_.fog) reveal(px_, py_, ev);

    if (tile(px_, py_) == Tile::LAVA) {
        if (suit_ > 0) {
            suit_--;
            ev |= EV_SUIT_USED;
        } else {
            ev |= EV_BURNED;
            hurt(rules_.lavaDamage, ev);
            if (ev & EV_DIED) return ev;
        }
    }
    if (tile(px_, py_) == Tile::WATER) ev |= EV_SAILING;
    if (tile(px_, py_) == Tile::CHECKPOINT && (cpX_ != px_ || cpY_ != py_)) {
        cpX_ = px_;
        cpY_ = py_;
        ev |= EV_CHECKPOINT;
    }
    if (tile(px_, py_) == Tile::EXIT) {
        exitReached_ = true;
        ev |= EV_EXIT;
    }
    return pickup(ev);
}

uint32_t World::hit()
{
    if (gameOver_) return EV_NONE;
    const int tx = px_ + dirX(facing_), ty = py_ + dirY(facing_);
    if (!inside(tx, ty)) return EV_NONE;
    const Obj o = obj(tx, ty);
    uint32_t ev = 0;
    if (o == Obj::CRATE || o == Obj::BUSH) {
        set(tx, ty, Obj::NONE);
        uncover(tx, ty, ev);
        return ev | EV_HIT | EV_BROKE;
    }
    if (o == Obj::ROCK || o == Obj::DOOR) return EV_HIT;
    const Tile t = tile(tx, ty);
    if (t == Tile::CRACKED) {
        if (++dmg_[ty][tx] >= CRACK_HITS) {
            dmg_[ty][tx] = 0;
            set(tx, ty, Tile::RUBBLE);
            uncover(tx, ty, ev);
            return ev | EV_HIT | EV_BROKE;
        }
        rev_++;
        return EV_HIT;
    }
    if (t == Tile::WALL || t == Tile::TREE || (t == Tile::GATE && !gateOpen_)) return EV_HIT;
    return EV_NONE;   // Swing at nothing
}

// ---- Save / restore -------------------------------------------------------------------------
// Header (16 B): magic u32, version u16, map id u8, w u8, h u8, reserved u8, body length u32,
//                reserved u16.
// Body: player block (PLAYER_BYTES) + per cell 7 bytes (terrain, obj, val, hid, hidVal, flags,
//       dmg) for w * h cells, row major. Trailer: CRC32 of header + body.

namespace {
constexpr size_t HEADER_BYTES = 16, PLAYER_BYTES = 32, CELL_BYTES = 7;

struct Writer {
    uint8_t *p;
    void u8(int v) { *p++ = (uint8_t)v; }
    void u16(int v) { u8(v & 0xFF); u8((v >> 8) & 0xFF); }
    void u32(uint32_t v) { u16((int)(v & 0xFFFF)); u16((int)(v >> 16)); }
};
struct Reader {
    const uint8_t *p;
    int u8() { return *p++; }
    int s8() { return (int8_t)*p++; }
    int u16() { int v = u8(); return v | (u8() << 8); }
    uint32_t u32() { uint32_t v = (uint32_t)u16(); return v | ((uint32_t)u16() << 16); }
};
}  // namespace

size_t World::saveSize() const { return HEADER_BYTES + PLAYER_BYTES + CELL_BYTES * (size_t)(w_ * h_) + 4; }

size_t World::save(uint8_t *buf, size_t cap) const
{
    const size_t n = saveSize();
    if (!buf || cap < n || !w_) return 0;
    memset(buf, 0, n);
    Writer o{buf};
    o.u32(SAVE_MAGIC);
    o.u16(SAVE_VERSION);
    o.u8(mapId_);
    o.u8(w_);
    o.u8(h_);
    o.u8(0);
    o.u32((uint32_t)(n - HEADER_BYTES - 4));
    o.u16(0);
    // Player block (32 bytes, unused tail stays 0)
    uint8_t *pb = o.p;
    o.u8(px_); o.u8(py_); o.u8(cpX_); o.u8(cpY_); o.u8((int)facing_);
    o.u16(keys_); o.u16(gems_); o.u16(gemsTotal_);
    o.u32((uint32_t)steps_);
    o.u8(energy_); o.u8(maxEnergy_); o.u8((uint8_t)(int8_t)livesLeft_); o.u16(livesUsed_); o.u16(suit_);
    o.u8((boat_ ? 1 : 0) | (gateOpen_ ? 2 : 0) | (gameOver_ ? 4 : 0) | (exitReached_ ? 8 : 0));
    o.p = pb + PLAYER_BYTES;
    for (int y = 0; y < h_; y++)
        for (int x = 0; x < w_; x++) {
            o.u8(terrain_[y][x]); o.u8(obj_[y][x]); o.u8(val_[y][x]); o.u8(hid_[y][x]);
            o.u8(hidVal_[y][x]); o.u8(flags_[y][x]); o.u8(dmg_[y][x]);
        }
    o.u32(crc32(buf, n - 4));
    return n;
}

bool World::restore(const uint8_t *buf, size_t n)
{
    if (!buf || !w_ || n != saveSize()) return false;
    Reader r{buf};
    if (r.u32() != SAVE_MAGIC || r.u16() != SAVE_VERSION || r.u8() != mapId_ || r.u8() != w_ || r.u8() != h_)
        return false;
    r.u8();
    if (r.u32() != n - HEADER_BYTES - 4) return false;
    r.u16();
    Reader tail{buf + n - 4};
    if (tail.u32() != crc32(buf, n - 4)) return false;
    // Validate everything before touching the world
    const uint8_t *pb = r.p;
    const int px = r.u8(), py = r.u8(), cpx = r.u8(), cpy = r.u8(), facing = r.u8();
    const int keys = r.u16(), gems = r.u16(), gemsTotal = r.u16();
    const uint32_t steps = r.u32();
    const int energy = r.u8(), maxE = r.u8(), lives = r.s8(), livesUsed = r.u16(), suit = r.u16();
    const int bits = r.u8();
    if (!inside(px, py) || !inside(cpx, cpy) || facing > 3 || gems > gemsTotal || gemsTotal != gemsTotal_ ||
        maxE < 1 || maxE > rules_.energyCap || energy > maxE || steps > 0x7FFFFFFF)
        return false;
    if (rules_.lives == LIVES_INFINITE ? lives != LIVES_INFINITE : (lives < 0 || lives > MAX_LIVES)) return false;
    if (!(bits & 4) && energy < 1) return false;
    r.p = pb + PLAYER_BYTES;
    const uint8_t *cells = r.p;
    for (int i = 0; i < w_ * h_; i++) {
        const uint8_t *c = cells + i * CELL_BYTES;
        if (c[0] >= (uint8_t)Tile::COUNT || c[1] >= (uint8_t)Obj::COUNT || c[3] >= (uint8_t)Obj::COUNT || c[5] > 3)
            return false;
    }
    // Apply
    px_ = px; py_ = py; cpX_ = cpx; cpY_ = cpy; facing_ = (Dir)facing;
    keys_ = keys; gems_ = gems; steps_ = (int)steps;
    energy_ = energy; maxEnergy_ = maxE; livesLeft_ = lives; livesUsed_ = livesUsed; suit_ = suit;
    boat_ = bits & 1; gameOver_ = (bits & 4) != 0; exitReached_ = (bits & 8) != 0;
    plates_ = 0;
    for (int y = 0; y < h_; y++)
        for (int x = 0; x < w_; x++) {
            const uint8_t *c = cells + (y * w_ + x) * CELL_BYTES;
            terrain_[y][x] = c[0]; obj_[y][x] = c[1]; val_[y][x] = c[2]; hid_[y][x] = c[3];
            hidVal_[y][x] = c[4]; flags_[y][x] = c[5]; dmg_[y][x] = c[6];
            if (c[0] == (uint8_t)Tile::PLATE) plates_++;
        }
    gateOpen_ = (bits & 2) != 0;
    uint32_t ev = 0;
    updateGates(ev);
    rev_ += 2;
    return true;
}

// ---- Planner --------------------------------------------------------------------------------

bool Planner::targetable(const World &w, int x, int y)
{
    if (!w.inside(x, y) || w.flagged(x, y) || blocksWalk(w.obj(x, y))) return false;
    const Tile t = w.tile(x, y);
    if (t == Tile::GATE) return w.gatesOpen();
    return isGround(t) || t == Tile::LAVA || t == Tile::WATER;
}

bool Planner::pickTarget(const World &w, float fx, float fy, int &tx, int &ty)
{
    const int cx = (int)floorf(fx), cy = (int)floorf(fy);
    if (targetable(w, cx, cy)) {
        tx = cx;
        ty = cy;
        return true;
    }
    const float ux = fx - cx, uy = fy - cy;   // 0..1 inside the tapped cell
    float best = SNAP + 1e-4f;
    bool found = false;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy) continue;
            const float ddx = dx < 0 ? ux : dx > 0 ? 1.f - ux : 0.f;
            const float ddy = dy < 0 ? uy : dy > 0 ? 1.f - uy : 0.f;
            const float d = sqrtf(ddx * ddx + ddy * ddy);
            if (d < best && targetable(w, cx + dx, cy + dy)) {
                best = d;
                tx = cx + dx;
                ty = cy + dy;
                found = true;
            }
        }
    return found;
}

Planner::Cat Planner::category(const World &w, int x, int y, WalkMode mode) const
{
    if (!w.inside(x, y) || w.flagged(x, y) || blocksWalk(w.obj(x, y))) return C_BLOCK;
    const Tile t = w.tile(x, y);
    Cat c;
    if (isGround(t)) c = C_FREE;
    else if (t == Tile::LAVA) c = C_LAVA;
    else if (t == Tile::WATER) c = w.hasBoat() ? C_FREE : C_BOAT;
    else if (t == Tile::GATE) c = w.gatesOpen() ? C_FREE : C_BLOCK;
    else c = C_BLOCK;
    if (c == C_BLOCK) return c;
    if (mode == WalkMode::ASSIST && !w.revealed(x, y)) return C_UNKNOWN;
    if (c == C_LAVA && mode == WalkMode::ASSIST && w.suitCharges() <= 0) return C_DEADLY;
    return c;
}

void Planner::heapPush(uint32_t key)
{
    if (heapLen_ >= HEAP) return;   // Cannot happen: <= 4 relaxations per settled cell
    int i = heapLen_++;
    while (i > 0) {
        const int p = (i - 1) / 2;
        if (heap_[p] <= key) break;
        heap_[i] = heap_[p];
        i = p;
    }
    heap_[i] = key;
}

uint32_t Planner::heapPop()
{
    const uint32_t top = heap_[0];
    const uint32_t last = heap_[--heapLen_];
    int i = 0;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= heapLen_) break;
        if (c + 1 < heapLen_ && heap_[c + 1] < heap_[c]) c++;
        if (heap_[c] >= last) break;
        heap_[i] = heap_[c];
        i = c;
    }
    if (heapLen_) heap_[i] = last;
    return top;
}

// A* with a Manhattan heuristic (every step costs >= STEP_COST, so it is admissible and consistent).
// Soft cells (lava without suit in Assist, water without boat, unknown fog in Assist) are only
// expanded when allowSoft is set, with a penalty so the path uses as few of them as possible.
//
// Costs are scaled by STEP_COST (4) so lava can carry a small tie-break penalty in Pro mode:
// before v0.10.0 a Pro lava step cost exactly as much as a ground step, so among several
// equally short paths the search order decided - next to a basalt causeway the hero often cut
// through the lava beside it (HW test: "ended up in the lava when crossing the bridge"). Now
// Pro still takes the shortest way, but prefers the one with the fewest lava cells (a detour is
// at least 2 steps = 8, so only paths crossing 8+ lava cells are ever traded for a detour).
bool Planner::search(const World &w, int tx, int ty, WalkMode mode, Path &out)
{
    const bool allowSoft = out.len < 0;   // Internal flag set by plan()
    out.len = 0;
    for (int i = 0; i < N; i++) cost_[i] = 0xFFFFFFFFu;
    memset(closed_, 0, sizeof(closed_));
    heapLen_ = 0;
    const int start = w.playerY() * MAX_W + w.playerX(), goal = ty * MAX_W + tx;
    cost_[start] = 0;
    prev_[start] = -1;
    heapPush((uint32_t)(STEP_COST * (abs(tx - w.playerX()) + abs(ty - w.playerY()))) << 12 | (uint32_t)start);
    bool found = false;
    while (heapLen_) {
        const int cell = (int)(heapPop() & 0xFFF);
        if (closed_[cell]) continue;
        closed_[cell] = 1;
        if (cell == goal) {
            found = true;
            break;
        }
        const int cx = cell % MAX_W, cy = cell / MAX_W;
        for (int d = 0; d < 4; d++) {
            const int nx = cx + dirX((Dir)d), ny = cy + dirY((Dir)d);
            if (!w.inside(nx, ny)) continue;
            const int n = ny * MAX_W + nx;
            if (closed_[n]) continue;
            const Cat c = category(w, nx, ny, mode);
            if (c == C_BLOCK) continue;
            const bool soft = c == C_BOAT || c == C_UNKNOWN || c == C_DEADLY;
            if (soft && !allowSoft) continue;
            uint32_t step = STEP_COST;
            if (c == C_LAVA) step += mode == WalkMode::ASSIST ? 8 * STEP_COST : 1;   // Suit charges are precious
            if (soft) step += 64 * STEP_COST;
            const uint32_t g = cost_[cell] + step;
            if (g >= cost_[n]) continue;
            cost_[n] = g;
            prev_[n] = (int16_t)cell;
            const uint32_t f = g + (uint32_t)(STEP_COST * (abs(tx - nx) + abs(ty - ny)));
            if (f < (1u << 20)) heapPush(f << 12 | (uint32_t)n);
        }
    }
    if (!found) return false;
    int n = 0;
    for (int c = goal; c != start; c = prev_[c]) n++;
    out.len = n;
    for (int c = goal, i = n - 1; c != start; c = prev_[c], i--) {
        const int p = prev_[c];
        const int dx = c % MAX_W - p % MAX_W, dy = c / MAX_W - p / MAX_W;
        out.dirs[i] = (uint8_t)(dx > 0 ? Dir::RIGHT : dx < 0 ? Dir::LEFT : dy > 0 ? Dir::DOWN : Dir::UP);
    }
    return true;
}

PlanResult Planner::plan(const World &w, int tx, int ty, WalkMode mode, Path &out)
{
    out.goalX = tx;
    out.goalY = ty;
    out.len = 0;
    if (!targetable(w, tx, ty)) return out.result = PlanResult::INVALID;
    if (tx == w.playerX() && ty == w.playerY()) return out.result = PlanResult::HERE;
    out.result = PlanResult::OK;
    if (!search(w, tx, ty, mode, out)) {
        out.len = -1;   // Second pass: soft cells allowed
        if (!search(w, tx, ty, mode, out)) {
            out.len = 0;
            return out.result = PlanResult::UNREACHABLE;
        }
    }
    // Walk the path: truncate before the first soft cell; Assist also checks the suit charges and
    // stops before a lava stretch the suit cannot carry it through (the last safe cell).
    int x = w.playerX(), y = w.playerY(), charges = w.suitCharges(), lavaRun = -1;
    bool usedSuit = false;
    for (int i = 0; i < out.len; i++) {
        x += dirX((Dir)out.dirs[i]);
        y += dirY((Dir)out.dirs[i]);
        const Cat c = category(w, x, y, mode);
        if (c == C_BOAT || c == C_UNKNOWN || c == C_DEADLY) {
            const int cut = (c == C_DEADLY && lavaRun >= 0) ? lavaRun : i;
            out.len = cut;
            return out.result = c == C_BOAT ? PlanResult::NEED_BOAT : c == C_UNKNOWN ? PlanResult::UNKNOWN
                                                                                   : PlanResult::DANGER;
        }
        if (c == C_LAVA) {
            if (lavaRun < 0) lavaRun = i;
            if (mode == WalkMode::ASSIST) {
                if (charges <= 0) {
                    out.len = lavaRun;
                    return out.result = PlanResult::DANGER;
                }
                charges--;
                usedSuit = true;
            }
        } else {
            lavaRun = -1;
        }
    }
    return out.result = usedSuit ? PlanResult::SUIT : PlanResult::OK;
}

}  // namespace Otr
