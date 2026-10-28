#include "otr_world.h"

#include <math.h>
#include <initializer_list>
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
    case 'a': out = {Tile::GRASS, Obj::BOMB3}; return true;
    case 'z': out = {Tile::GRASS, Obj::BOMB4}; return true;
    case 'f': out = {Tile::GRASS, Obj::FLASH}; return true;   // v0.18.0 flashlight (level 1)
    case 'j': out = {Tile::GRASS, Obj::POGO}; return true;    // v0.18.0 pogo stick (level 1)
    case 'm': out = {Tile::GRASS, Obj::MINE}; return true;
    default:
        if (c >= '1' && c <= '9') { out = {Tile::GRASS, Obj::NPC}; return true; }   // Person (number = digit)
        return false;
    }
}

bool coloredBox(Obj o) { return o >= Obj::BOX_RED && o <= Obj::BOX_CYAN; }
bool coloredPlate(Tile t) { return t >= Tile::PLATE_RED && t <= Tile::PLATE_CYAN; }
bool plate(Tile t) { return t == Tile::PLATE || coloredPlate(t); }
bool plateCovered(Tile t, Obj o)
{
    if (t == Tile::PLATE) return o == Obj::ROCK;
    return coloredPlate(t) && coloredBox(o) &&
           (int)t - (int)Tile::PLATE_RED == (int)o - (int)Obj::BOX_RED;
}

// Ground that both the player and rocks can stand on.
bool isGround(Tile t)
{
    if (coloredPlate(t)) return true;
    switch (t) {
    case Tile::GRASS: case Tile::SAND: case Tile::FLOOR: case Tile::RUBBLE:
    case Tile::BRIDGE: case Tile::BASALT: case Tile::PLATE: case Tile::CHECKPOINT:
    case Tile::EXIT: case Tile::CRATER:
    case Tile::ICE: case Tile::BELT_UP: case Tile::BELT_RIGHT:
    case Tile::BELT_DOWN: case Tile::BELT_LEFT:
    case Tile::POISON: case Tile::SWAMP: case Tile::SHARP_ROCKS:
        return true;
    default:
        return false;
    }
}

bool coloredKey(Obj o) { return o >= Obj::KEY_RED && o <= Obj::KEY_CYAN; }
bool coloredDoor(Obj o) { return o >= Obj::DOOR_RED && o <= Obj::DOOR_CYAN; }
bool forcedTile(Tile t) { return t >= Tile::ICE && t <= Tile::BELT_LEFT; }
bool groupedTile(Tile t) { return plate(t) || t == Tile::GATE || t == Tile::LEVER; }
bool blocksWalk(Obj o) { return o == Obj::ROCK || coloredBox(o) || o == Obj::CRATE || o == Obj::DOOR || o == Obj::BUSH || o == Obj::NPC || coloredDoor(o); }
bool isItem(Obj o)
{
    return o == Obj::KEY || o == Obj::GEM || o == Obj::BOAT || o == Obj::POTION || o == Obj::HEART ||
           o == Obj::SUIT || o == Obj::LIFE || o == Obj::BOMB3 || o == Obj::BOMB4 ||
           o == Obj::FLASH || o == Obj::POGO || o == Obj::SKATES || o == Obj::SUCTION || coloredKey(o);
}
bool alwaysVisible(Tile t) { return t == Tile::WALL || t == Tile::TREE || t == Tile::CRACKED; }

bool rulesValid(const Rules &r)
{
    return r.startEnergy >= 1 && r.energyCap >= r.startEnergy &&
           (r.lives == LIVES_INFINITE || (r.lives >= 1 && r.lives <= MAX_LIVES)) &&
           r.mineLevel <= MINE_LEVELS;
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
    memset(static_cast<void *>(this), 0, sizeof(*this));
    facing_ = Dir::DOWN;
    rules_ = rules;
    if (!rulesValid(rules)) return false;
    if (rules_.mineLevel) rules_.fog = true;   // True minesweeper always needs fog.
    if (!rows || h <= 0 || h > MAX_H || !rows[0]) return false;
    const size_t w = strlen(rows[0]);
    if (w == 0 || w > (size_t)MAX_W) return false;
    int starts = 0;
    for (int y = 0; y < h; y++) {
        if (!rows[y] || strlen(rows[y]) != w) return false;
        for (int x = 0; x < (int)w; x++) {
            Cell c;
            const uint8_t glyph = (uint8_t)rows[y][x];
            if (glyph >= BOX_GLYPH && glyph < BOX_GLYPH + 6) {
                c = {Tile::FLOOR, (Obj)((int)Obj::BOX_RED + glyph - BOX_GLYPH)};
            } else if (glyph >= COLORED_PLATE_GLYPH && glyph < COLORED_PLATE_GLYPH + 6) {
                c = {(Tile)((int)Tile::PLATE_RED + glyph - COLORED_PLATE_GLYPH), Obj::NONE};
            } else if (glyph >= POISON_GLYPH && glyph <= SHARP_ROCKS_GLYPH) {
                c = {(Tile)((int)Tile::POISON + glyph - POISON_GLYPH), Obj::NONE};
            } else if (glyph == SKATES_GLYPH) {
                c = {Tile::FLOOR, Obj::SKATES};
            } else if (glyph == SUCTION_GLYPH) {
                c = {Tile::FLOOR, Obj::SUCTION};
            } else if (glyph >= GROUPED_PLATE_GLYPH && glyph < GROUPED_GATE_GLYPH) {
                c = {Tile::PLATE, Obj::NONE};
                val_[y][x] = glyph - GROUPED_PLATE_GLYPH;
            } else if (glyph >= GROUPED_GATE_GLYPH && glyph < GROUPED_GATE_GLYPH + PLATE_GROUPS) {
                c = {Tile::GATE, Obj::NONE};
                val_[y][x] = glyph - GROUPED_GATE_GLYPH;
            } else if (glyph >= GROUPED_LEVER_GLYPH && glyph < GROUPED_LEVER_GLYPH + PLATE_GROUPS) {
                c = {Tile::LEVER, Obj::NONE};
                val_[y][x] = glyph - GROUPED_LEVER_GLYPH;
            } else if (glyph == ICE_GLYPH) {
                c = {Tile::ICE, Obj::NONE};
            } else if (glyph >= BELT_UP_GLYPH && glyph < BELT_UP_GLYPH + 4) {
                c = {(Tile)((int)Tile::BELT_UP + glyph - BELT_UP_GLYPH), Obj::NONE};
            } else if (glyph >= COLORED_KEY_GLYPH && glyph < COLORED_KEY_GLYPH + 6) {
                c = {Tile::FLOOR, (Obj)((int)Obj::KEY_RED + glyph - COLORED_KEY_GLYPH)};
            } else if (glyph >= COLORED_DOOR_GLYPH && glyph < COLORED_DOOR_GLYPH + 6) {
                c = {Tile::FLOOR, (Obj)((int)Obj::DOOR_RED + glyph - COLORED_DOOR_GLYPH)};
            } else if (!decode(rows[y][x], c)) {
                return false;
            }
            terrain_[y][x] = (uint8_t)c.t;
            obj_[y][x] = (uint8_t)c.o;
            if (c.o == Obj::NPC) {
                const char d = rows[y][x];
                for (int py = 0; py <= y; py++)   // Every person number at most once
                    for (int qx = 0; qx < (py == y ? x : (int)w); qx++)
                        if (rows[py][qx] == d) return false;
                val_[y][x] = (uint8_t)(d - '0');
            }
            if (c.o == Obj::GEM) gemsTotal_++;
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
    mineLevel_ = rules_.mineLevel;   // Data-driven true minesweeper (SD .adv); 0 for ordinary maps.
    if (mineLevel_) {
        // Mirror loadMines(): keep the start strip (the start cell plus the three supply cells to its
        // right) visible even when an adjacent mine stops the flood fill, so S / suit / bombs always
        // show. Never reveal a mine cell.
        for (int x = px_; x < px_ + 4 && x < w_; x++)
            if (isGround(tile(x, py_)) && rawObj(x, py_) != Obj::MINE) flags_[py_][x] |= F_REVEALED;
    }
    rev_ = 1;
    return true;
}

bool World::load(const MapDef &def)
{
    if (!load(def.rows, def.h, def.rules)) return false;
    mapId_ = def.id;
    if (def.controlCount < 0 || def.controlCount > PLATE_GROUPS ||
        (def.controlCount && !def.controls) || def.par < 0) return false;
    bool configured[PLATE_GROUPS] = {};
    for (int i = 0; i < def.controlCount; ++i) {
        const GroupControl &control = def.controls[i];
        if (control.group >= PLATE_GROUPS || configured[control.group] ||
            (control.requireBoth && control.steps)) return false;
        configured[control.group] = true;
        controlSteps_[control.group] = control.steps;
        if (control.requireBoth) andGroups_ |= uint64_t(1) << control.group;
    }
    if (def.plateGroups)
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x)
                if (def.plateGroups[y][x]) {
                    if (!coloredPlate(tile(x, y)) || def.plateGroups[y][x] > PLATE_GROUPS) return false;
                    val_[y][x] = def.plateGroups[y][x] - 1;
                }
    par_ = def.par;
    uint32_t gateEvents = 0;
    updateGates(gateEvents);
    for (int i = 0; i < def.treasureCount && def.treasures; i++) {
        const Treasure &t = def.treasures[i];
        if (!inside(t.x, t.y) || !isItem(t.item) || hid_[t.y][t.x] || groupedTile(tile(t.x, t.y)))
            return false; // Group ids and item amounts share val_; they cannot occupy the same cell.
        // v0.18.5: @treasures takes precedence over a map-glyph PICKUP on the same cell, so a
        // higher pogo / flashlight level (or any treasure) wins over the bare glyph (e.g. j, f).
        // Only item glyphs are cleared — containers (crate, rock, bush, door, cracked wall) still
        // legitimately hide a treasure until they are broken/pushed/opened, so those are untouched.
        if (isItem((Obj)obj_[t.y][t.x])) {
            if (obj_[t.y][t.x] == (uint8_t)Obj::GEM && gemsTotal_ > 0) gemsTotal_--;
            obj_[t.y][t.x] = (uint8_t)Obj::NONE;
            val_[t.y][t.x] = 0;
        }
        hid_[t.y][t.x] = (uint8_t)t.item;
        hidVal_[t.y][t.x] = t.amount;
        if (t.item == Obj::GEM) gemsTotal_++;
        uint32_t ev = 0;
        uncover(t.x, t.y, ev);   // Nothing covers it: visible from the start
    }
    // v0.18.0 dark areas: flag every cell inside each @dark rectangle. Coordinates are clipped to
    // the map bounds; a rectangle that lies fully outside is dropped. Darkness only hides cells
    // (see darkHidden / lit); it never blocks movement.
    for (int i = 0; i < def.darkCount && def.darks; i++) {
        int x0 = def.darks[i].x0, y0 = def.darks[i].y0, x1 = def.darks[i].x1, y1 = def.darks[i].y1;
        if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
        if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
        if (x0 >= w_ || y0 >= h_) continue;          // fully outside
        if (x1 >= w_) x1 = w_ - 1;
        if (y1 >= h_) y1 = h_ - 1;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) flags_[y][x] |= F_DARK;
    }
    if (def.enemyCount < 0 || def.enemyCount > MAX_ENEMIES ||
        (def.enemyCount && !def.enemies)) return false;
    for (int i = 0; i < def.enemyCount; ++i) {
        const EnemyDef &e = def.enemies[i];
        if ((e.kind != EnemyKind::PATROL && e.kind != EnemyKind::CHASER) ||
            e.routeCount > MAX_ROUTE || (e.kind == EnemyKind::PATROL && !e.routeCount) ||
            (e.kind == EnemyKind::CHASER && (e.routeCount || e.pingPong)) ||
            !enemyTerrain(e.x, e.y) || (e.x == px_ && e.y == py_) ||
            enemyAt(e.x, e.y) >= 0) return false;
        if (e.bounded && (e.x0 > e.x1 || e.y0 > e.y1 || !inside(e.x0, e.y0) ||
            !inside(e.x1, e.y1) || e.x < e.x0 || e.x > e.x1 || e.y < e.y0 || e.y > e.y1))
            return false;
        for (int j = 0; j < e.routeCount; ++j) {
            const EnemyPoint &p = e.route[j];
            const EnemyPoint from = j ? e.route[j - 1] : EnemyPoint{e.x, e.y};
            if (!patrolSegment(from, p) || (e.bounded &&
                (p.x < e.x0 || p.x > e.x1 || p.y < e.y0 || p.y > e.y1))) return false;
        }
        if (e.kind == EnemyKind::PATROL && !e.pingPong &&
            !patrolSegment(e.route[e.routeCount - 1], e.route[0])) return false;
        enemyDefs_[i] = e;
        enemies_[i] = {e.x, e.y, 0, 1};
        ++enemyCount_;
    }
    rev_ = 1;
    return true;
}

// ---- Queries --------------------------------------------------------------------------------

int World::enemyAt(int x, int y) const
{
    for (int i = 0; i < enemyCount_; ++i)
        if (enemies_[i].x == x && enemies_[i].y == y) return i;
    return -1;
}

bool World::enemyTerrain(int x, int y) const
{
    if (!inside(x, y) || blocksWalk(rawObj(x, y)) || rawObj(x, y) == Obj::MINE) return false;
    const Tile t = tile(x, y);
    return (isGround(t) && t != Tile::POISON && t != Tile::SWAMP && t != Tile::SHARP_ROCKS) ||
           (t == Tile::GATE && gateOpenAt(x, y));
}

bool World::patrolSegment(EnemyPoint a, EnemyPoint b) const
{
    if (!inside(a.x, a.y) || !inside(b.x, b.y) || (a.x != b.x && a.y != b.y)) return false;
    int x = a.x, y = a.y;
    const int dx = (b.x > a.x) - (b.x < a.x), dy = (b.y > a.y) - (b.y < a.y);
    for (;;) {
        const Tile t = tile(x, y);
        if ((!(isGround(t) || t == Tile::GATE)) ||
            t == Tile::POISON || t == Tile::SWAMP || t == Tile::SHARP_ROCKS ||
            rawObj(x, y) == Obj::MINE) return false;
        if (x == b.x && y == b.y) return true;
        x += dx; y += dy;
    }
}

bool World::enemyCanEnter(int i, int x, int y) const
{
    const EnemyDef &e = enemyDefs_[i];
    const int other = enemyAt(x, y);
    return enemyTerrain(x, y) && (other < 0 || other == i) &&
           (!e.bounded || (x >= e.x0 && x <= e.x1 && y >= e.y0 && y <= e.y1));
}

bool World::enemyStep(int i, int tx, int ty, int &nx, int &ny)
{
    const EnemyState &e = enemies_[i];
    const int start = e.y * w_ + e.x, goal = ty * w_ + tx;
    if (!enemyCanEnter(i, tx, ty) || start == goal) return false;
    for (int j = 0; j < w_ * h_; ++j) enemyPrev_[j] = -1;
    int head = 0, tail = 0;
    enemyQueue_[tail++] = (uint16_t)start;
    enemyPrev_[start] = (int16_t)start;
    while (head < tail && enemyPrev_[goal] < 0) {
        const int c = enemyQueue_[head++];
        for (int d = 0; d < 4; ++d) {
            const int x = c % w_ + dirX((Dir)d), y = c / w_ + dirY((Dir)d);
            if (!enemyCanEnter(i, x, y)) continue;
            const int n = y * w_ + x;
            if (enemyPrev_[n] >= 0) continue;
            enemyPrev_[n] = (int16_t)c;
            enemyQueue_[tail++] = (uint16_t)n;
        }
    }
    if (enemyPrev_[goal] < 0) return false;
    int next = goal;
    while (enemyPrev_[next] != start) next = enemyPrev_[next];
    nx = next % w_; ny = next / w_;
    return true;
}

void World::enemyContact(uint32_t &ev)
{
    if (!enemyContact_ && !(ev & EV_DIED) && enemyAt(px_, py_) >= 0) {
        enemyContact_ = true;
        ++rev_;
        hurt(2, ev);
    }
}

uint32_t World::finishAction(uint32_t ev)
{
    if (gameOver_ || exitReached_ || (ev & EV_DIED)) return ev;
    for (int i = 0; i < enemyCount_; ++i) {
        EnemyState &s = enemies_[i];
        const EnemyDef &e = enemyDefs_[i];
        // A player may enter an enemy cell; that enemy waits until the player leaves.
        if (s.x == px_ && s.y == py_) {
            enemyContact(ev);
            if (ev & EV_DIED) break;
            continue;
        }
        int tx = px_, ty = py_;
        if (e.kind == EnemyKind::PATROL) {
            if (s.x == e.route[s.index].x && s.y == e.route[s.index].y && e.routeCount > 1) {
                int next = s.index + s.direction;
                if (next < 0 || next >= e.routeCount) {
                    if (e.pingPong) { s.direction = -s.direction; next = s.index + s.direction; }
                    else next = 0;
                }
                s.index = (uint8_t)next;
                ++rev_;
            }
            tx = e.route[s.index].x; ty = e.route[s.index].y;
        }
        int nx, ny;
        if (e.kind == EnemyKind::PATROL) {
            nx = s.x + (tx > s.x) - (tx < s.x);
            ny = s.y + (ty > s.y) - (ty < s.y);
            if ((nx == s.x && ny == s.y) || !enemyCanEnter(i, nx, ny)) continue;
        } else if (!enemyStep(i, tx, ty, nx, ny)) continue;
        if (nx == px_ && ny == py_) {
            if (!enemyContact_) { enemyContact_ = true; ++rev_; hurt(2, ev); }
            if (ev & EV_DIED) break;
        } else {
            s.x = (uint8_t)nx; s.y = (uint8_t)ny; ++rev_;
        }
    }
    return ev;
}

int World::keys(KeyColor color) const
{
    const int c = (int)color;
    return c < (int)KeyColor::COUNT ? colorKeys_[c] : 0;
}

void World::setKeys(KeyColor color, int count)
{
    const int c = (int)color;
    if (c >= (int)KeyColor::COUNT) return;
    colorKeys_[c] = (uint8_t)(count < 0 ? 0 : count > 255 ? 255 : count);
    ++rev_;
}

int World::timerRemaining(int group) const
{
    return group >= 0 && group < PLATE_GROUPS ? timers_[group] : 0;
}

bool World::leverOnAt(int x, int y) const
{
    if (!inside(x, y) || tile(x, y) != Tile::LEVER) return false;
    const int group = val_[y][x];
    return group < PLATE_GROUPS && (controlSteps_[group] ? timers_[group] != 0 :
                                   (leverGroups_ & (uint64_t(1) << group)) != 0);
}

uint32_t World::activate()
{
    if (gameOver_ || exitReached_) return EV_NONE;
    enemyContact_ = false;
    const int x = px_ + dirX(facing_), y = py_ + dirY(facing_);
    if (!inside(x, y) || tile(x, y) != Tile::LEVER) return EV_NONE;
    const int group = val_[y][x];
    if (group >= PLATE_GROUPS) return EV_NONE;
    if (controlSteps_[group]) timers_[group] = controlSteps_[group];
    else leverGroups_ ^= uint64_t(1) << group;
    uint32_t ev = EV_LEVER;
    ++rev_;
    updateGates(ev);
    return finishAction(ev);
}

void World::tickTimers(uint32_t &ev)
{
    for (int group = 0; group < PLATE_GROUPS; ++group)
        if (timers_[group]) { --timers_[group]; ++rev_; }
    updateGates(ev);
}

void World::activatePlate(int x, int y, uint32_t &ev)
{
    if (!plate(tile(x, y))) return;
    if (coloredPlate(tile(x, y)) && !plateCovered(tile(x, y), rawObj(x, y))) return;
    const int group = val_[y][x];
    if (group >= PLATE_GROUPS) return;
    if (controlSteps_[group]) {
        timers_[group] = controlSteps_[group];
        ++rev_;
        updateGates(ev);
    }
}

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
    if (gameOver_ || (mineLevel_ && exitReached_) || !rules_.fog || !inside(x, y) || revealed(x, y) || (x == px_ && y == py_)) return false;
    flags_[y][x] ^= F_FLAG;
    rev_++;
    return true;
}

int World::suitChargesAtPickup() const
{
    const int extra = maxEnergy_ > rules_.startEnergy ? maxEnergy_ - rules_.startEnergy : 0;
    return rules_.suitBaseSteps + rules_.suitPerExtraMax * extra;
}

// v0.18.3 flashlight cone. Range R = flash_ (L1->1, L2->2, L3->3 cells ahead). For a cell at
// (fx, fy) = (x-px, y-py): project onto the facing axis (forward) and the perpendicular (lateral).
// Lit iff forward in [1..R]; the leading rows (forward < R) are 3 wide (abs(lateral) <= 1) and the
// last row (forward == R) is a single cell straight ahead (lateral == 0). So L1 = 1 cell; L2 = a
// 3-wide first row + 1 ahead; L3 = two 3-wide rows + 1 ahead. The player's own cell is handled
// separately (always visible). Reveals darkness only: this function is never consulted for fog.
bool World::lit(int x, int y) const
{
    if (flash_ <= 0 || !inside(x, y)) return false;
    const int R = flash_;
    const int rx = dirX(facing_), ry = dirY(facing_);
    const int fx = x - px_, fy = y - py_;
    int forward, lateral;
    if (rx != 0) { forward = fx * rx; lateral = fy; }
    else { forward = fy * ry; lateral = fx; }
    if (forward < 1 || forward > R) return false;
    const int a = lateral < 0 ? -lateral : lateral;
    return forward < R ? a <= 1 : a == 0;
}

bool World::playerCanEnter(int x, int y) const
{
    if (!inside(x, y)) return false;
    if (blocksWalk(obj(x, y))) return false;
    const Tile t = tile(x, y);
    if (isGround(t) || t == Tile::LAVA) return true;   // Lava: allowed, but it burns
    if (t == Tile::WATER) return boat_;
    if (t == Tile::GATE) return gateOpenAt(x, y);
    return false;
}

bool World::rockCanEnter(int x, int y) const
{
    if (!inside(x, y) || rawObj(x, y) != Obj::NONE || enemyAt(x, y) >= 0) return false;
    const Tile t = tile(x, y);
    if (t == Tile::CHECKPOINT || t == Tile::EXIT) return false;
    return isGround(t) || t == Tile::WATER || t == Tile::LAVA;   // Never onto gates
}

// v0.18.0 pogo vault. Cells ahead in direction d at distance k are C(k) = player + k*dir (k >= 1);
// C(1) is the blocking cell that triggered the vault. Count the contiguous run of blocking cells
// (an out-of-bounds cell counts as blocking), capped at pogo_. Land on the first walkable, in-bounds
// cell just beyond the run. Returns false if the run is wider than pogo_ (no gap within reach) or
// the cell beyond is not walkable / out of bounds.
bool World::pogoLand(Dir d, int &lx, int &ly) const
{
    if (pogo_ <= 0) return false;
    const int dx = dirX(d), dy = dirY(d);
    int run = 0;
    for (int k = 1; k <= pogo_; k++) {
        const int cx = px_ + dx * k, cy = py_ + dy * k;
        if (inside(cx, cy) && playerCanEnter(cx, cy)) break;   // gap found: run ended at k-1
        run = k;                                               // blocking (or OOB) cell
    }
    if (run == 0) return false;
    const int landx = px_ + dx * (run + 1), landy = py_ + dy * (run + 1);
    if (!inside(landx, landy) || !playerCanEnter(landx, landy)) return false;   // too wide / blocked / OOB
    lx = landx;
    ly = landy;
    return true;
}

void World::updateGates(uint32_t &ev)
{
    bool hasPlate[PLATE_GROUPS] = {};
    bool allCovered[PLATE_GROUPS];
    for (int group = 0; group < PLATE_GROUPS; group++) allCovered[group] = true;
    for (int y = 0; y < h_; y++)
        for (int x = 0; x < w_; x++)
            if (plate(tile(x, y))) {
                const int group = val_[y][x];
                if (group >= PLATE_GROUPS) continue;
                hasPlate[group] = true;
                if (!plateCovered(tile(x, y), rawObj(x, y))) allCovered[group] = false;
            }
    uint64_t openGroups = leverGroups_;
    for (int group = 0; group < PLATE_GROUPS; group++)
        if (andGroups_ & (uint64_t(1) << group)) {
            if (!hasPlate[group] || !allCovered[group]) openGroups &= ~(uint64_t(1) << group);
        } else if (controlSteps_[group]) {
            if (timers_[group]) openGroups |= uint64_t(1) << group;
            else openGroups &= ~(uint64_t(1) << group);
        } else if (hasPlate[group] && allCovered[group]) openGroups |= uint64_t(1) << group;
    if (openGroups != gateOpenGroups_) {
        const uint64_t opened = openGroups & ~gateOpenGroups_;
        const uint64_t closed = gateOpenGroups_ & ~openGroups;
        gateOpenGroups_ = openGroups;
        rev_++;
        if (opened) ev |= EV_GATE_OPEN;
        if (closed) ev |= EV_GATE_CLOSE;
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
    if (coloredKey(o)) {
        const int c = (int)o - (int)Obj::KEY_RED;
        if (colorKeys_[c] < 255) ++colorKeys_[c];
        ev |= EV_KEY;
    }
    switch (o) {
    case Obj::BOMB3: if (bomb3_ < 99) bomb3_++; ev |= EV_TREASURE; break;
    case Obj::BOMB4: if (bomb4_ < 99) bomb4_++; ev |= EV_TREASURE; break;
    case Obj::KEY: keys_++; ev |= EV_KEY; break;
    case Obj::GEM:
        gems_++;
        ev |= EV_GEM;
        if (gems_ == gemsTotal_) ev |= EV_ALL_GEMS;
        break;
    case Obj::BOAT: boat_ = true; ev |= EV_BOAT; break;
    case Obj::SKATES: skates_ = true; ev |= EV_TREASURE; break;
    case Obj::SUCTION: suction_ = true; ev |= EV_TREASURE; break;
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
    case Obj::FLASH: {   // v0.18.0: level = amount (0 => 1), clamped 1..3; never lowered.
        int lvl = amount ? amount : 1;
        if (lvl > 3) lvl = 3;
        if (lvl > flash_) flash_ = (uint8_t)lvl;
        ev |= EV_TREASURE;   // generic pickup feedback (UI shows a flashlight-specific toast)
        break;
    }
    case Obj::POGO: {
        int lvl = amount ? amount : 1;
        if (lvl > 3) lvl = 3;
        if (lvl > pogo_) pogo_ = (uint8_t)lvl;
        ev |= EV_TREASURE;
        break;
    }
    default: break;
    }
    obj_[py_][px_] = (uint8_t)Obj::NONE;
    val_[py_][px_] = 0;
    rev_++;
    return ev;
}

uint32_t World::move(Dir d, MoveTrace *trace)
{
    if (trace) trace->count = 0;
    if (gameOver_ || exitReached_) return EV_NONE;
    enemyContact_ = false;
    facing_ = d;
    const int tx = px_ + dirX(d), ty = py_ + dirY(d);
    if (!inside(tx, ty) || (mineLevel_ && flagged(tx, ty))) return EV_BLOCKED;
    uint32_t ev = 0;
    const Obj target = obj(tx, ty);
    if (coloredDoor(target)) {
        const int c = (int)target - (int)Obj::DOOR_RED;
        if (!colorKeys_[c]) return EV_BLOCKED | EV_NEED_KEY;
        --colorKeys_[c];
        set(tx, ty, Obj::NONE);
        uncover(tx, ty, ev);
        return finishAction(ev | EV_DOOR);
    }
    int plateX = -1, plateY = -1;

    switch (obj(tx, ty)) {
    case Obj::NPC:
        talkNpc_ = (uint8_t)val_[ty][tx];
        return EV_BLOCKED | EV_TALK;
    case Obj::DOOR:
        if (keys_ <= 0) return EV_BLOCKED | EV_NEED_KEY;
        keys_--;
        set(tx, ty, Obj::NONE);
        uncover(tx, ty, ev);
        return finishAction(ev | EV_DOOR);
    case Obj::CRATE:
    case Obj::BUSH:
        break;   // v0.18.0: fall through to the pogo gate (a pogo stick may vault it)
    case Obj::BOX_RED: case Obj::BOX_GREEN: case Obj::BOX_BLUE:
    case Obj::BOX_YELLOW: case Obj::BOX_PURPLE: case Obj::BOX_CYAN:
    case Obj::ROCK: {
        const int bx = tx + dirX(d), by = ty + dirY(d);
        if (!rockCanEnter(bx, by)) break;   // v0.18.0: not pushable -> fall through to the pogo gate
        if (coloredBox(target) && (tile(bx, by) == Tile::WATER || tile(bx, by) == Tile::LAVA)) break;
        set(tx, ty, Obj::NONE);
        const Tile under = tile(bx, by);
        if (under == Tile::WATER) {
            set(bx, by, Tile::BRIDGE);
            ev |= EV_SPLASH;
        } else if (under == Tile::LAVA) {
            set(bx, by, Tile::BASALT);
            ev |= EV_SIZZLE;
        } else {
            set(bx, by, target);
            plateX = bx; plateY = by;
        }
        ev |= EV_PUSHED;
        uncover(tx, ty, ev);   // Something under the rock's old place?
        updateGates(ev);
        break;
    }
    default:
        break;
    }

    if (!playerCanEnter(tx, ty)) return ev | EV_BLOCKED;   // v0.18.3: pogo vault is now manual (vault())
    ev = landOn(tx, ty, ev, trace);
    if (plateX >= 0) activatePlate(plateX, plateY, ev);
    return finishAction(resolveForced(ev, trace));
}

// v0.18.3: shared landing sequence for a normal step and a manual pogo vault. Moves the player onto
// (destX, destY), then resolves mines, fog, lava, water, checkpoints, the exit and pickups.
uint32_t World::landOn(int destX, int destY, uint32_t ev, MoveTrace *trace)
{
    if (trace && trace->count < 129)
        trace->cells[trace->count++] = {(uint8_t)destX, (uint8_t)destY};
    tickTimers(ev);
    px_ = destX;
    py_ = destY;
    steps_++;
    ev |= EV_MOVED;
    activatePlate(px_, py_, ev);
    enemyContact(ev);
    if (ev & EV_DIED) return ev;

    if (rawObj(px_, py_) == Obj::MINE) {   // Boom: the mine is gone, a crater remains
        obj_[py_][px_] = (uint8_t)Obj::NONE;
        terrain_[py_][px_] = (uint8_t)Tile::CRATER;
        if (rules_.fog) flags_[py_][px_] = F_REVEALED;
        rev_++;
        ev |= EV_MINE;
        if (mineLevel_ && suit_ > 0) { --suit_; ev |= EV_SUIT_USED; }
        else hurt(rules_.mineDamage, ev);
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
    const Tile ground = tile(px_, py_);
    const int hazard = ground == Tile::POISON ? 1 : ground == Tile::SWAMP ? 2 :
                       ground == Tile::SHARP_ROCKS ? 4 : 0;
    if (hazard) {
        const int loss = suit_ > 0 ? hazard / 2 : hazard;
        if (loss) hurt(loss, ev);
        if (ev & EV_DIED) return ev;
    }
    if (tile(px_, py_) == Tile::CHECKPOINT && (cpX_ != px_ || cpY_ != py_)) {
        cpX_ = px_;
        cpY_ = py_;
        ev |= EV_CHECKPOINT;
    }
    if (tile(px_, py_) == Tile::EXIT) {
        exitReached_ = true;
        ev |= EV_EXIT;
    }
    ev = pickup(ev);
    checkMineWin();
    if (mineLevel_ && exitReached_) ev |= EV_EXIT;
    return ev;
}

// v0.18.3: manual pogo vault. The UI swaps the "Slå" action to "Hoppa" while canVault() is true
// (the player owns a pogo stick and faces a jumpable run of obstacles) and calls vault() on the tap,
// so the player must first turn to face the obstacle. Jumps in the current facing direction.
bool World::canVault() const
{
    if (gameOver_ || exitReached_ || pogo_ <= 0) return false;
    int lx, ly;
    return pogoLand(facing_, lx, ly);
}

uint32_t World::vault(MoveTrace *trace)
{
    if (trace) trace->count = 0;
    if (gameOver_ || exitReached_ || pogo_ <= 0) return EV_NONE;
    int lx, ly;
    if (!pogoLand(facing_, lx, ly)) return EV_BLOCKED;
    enemyContact_ = false;
    return finishAction(resolveForced(landOn(lx, ly, 0, trace), trace));
}

bool World::canPull() const
{
    if (!suction_ || gameOver_ || exitReached_) return false;
    const int dx = dirX(facing_), dy = dirY(facing_);
    const Obj o = rawObj(px_ + dx, py_ + dy);
    const int bx = px_ - dx, by = py_ - dy;
    if (!(o == Obj::ROCK || coloredBox(o)) || !rockCanEnter(px_, py_) ||
        tile(px_, py_) == Tile::WATER || tile(px_, py_) == Tile::LAVA ||
        !playerCanEnter(bx, by) || enemyAt(bx, by) >= 0 ||
        (mineLevel_ && flagged(bx, by))) return false;
    if (tile(bx, by) == Tile::GATE) {
        const int group = val_[by][bx];
        bool hasPlate = false, allCovered = true;
        for (int y = 0; y < h_; ++y)
            for (int x = 0; x < w_; ++x)
                if (plate(tile(x, y)) && val_[y][x] == group) {
                    hasPlate = true;
                    const Obj after = x == px_ && y == py_ ? o :
                        x == px_ + dx && y == py_ + dy ? Obj::NONE : rawObj(x, y);
                    if (!plateCovered(tile(x, y), after)) allCovered = false;
                }
        const bool latched = (leverGroups_ & (uint64_t(1) << group)) != 0;
        // Check object-induced closure before committing. Landing still ticks timers
        // afterward, exactly as ordinary movement through an expiring timed gate does.
        const bool open = (andGroups_ & (uint64_t(1) << group)) ?
            latched && hasPlate && allCovered : controlSteps_[group] ?
            timers_[group] != 0 : latched || (hasPlate && allCovered);
        if (!open) return false;
    }
    return true;
}

uint32_t World::pull(MoveTrace *trace)
{
    if (trace) trace->count = 0;
    if (gameOver_ || exitReached_) return EV_NONE;
    if (!canPull()) return EV_BLOCKED;
    enemyContact_ = false;
    const int dx = dirX(facing_), dy = dirY(facing_);
    const int ox = px_, oy = py_, sx = px_ + dx, sy = py_ + dy;
    const Obj o = rawObj(sx, sy);
    uint32_t ev = EV_PUSHED;
    set(sx, sy, Obj::NONE);
    // Amount/group/treasure layers belong to cells, not to the moving rock/box.
    uncover(sx, sy, ev);
    set(ox, oy, o);
    updateGates(ev);
    ev = landOn(ox - dx, oy - dy, ev, trace);
    activatePlate(ox, oy, ev);
    return finishAction(resolveForced(ev, trace, true));
}

bool World::forcedMotion(Dir &direction) const
{
    const Tile t = tile(px_, py_);
    if (t == Tile::ICE && !skates_) { direction = facing_; return true; }
    if (t >= Tile::BELT_UP && t <= Tile::BELT_LEFT) {
        direction = (Dir)((int)t - (int)Tile::BELT_UP);
        return true;
    }
    return false;
}

uint32_t World::resolveForced(uint32_t ev, MoveTrace *trace, bool retreat)
{
    Dir motion = retreat ? (Dir)(((int)facing_ + 2) % 4) : facing_;
    for (int n = 0; n < 128 && !(ev & (EV_DIED | EV_EXIT | EV_MINE | EV_BURNED)); ++n) {
        Dir d;
        if (retreat && tile(px_, py_) == Tile::ICE && !skates_) d = motion;
        else if (!forcedMotion(d)) break;
        const int x = px_ + dirX(d), y = py_ + dirY(d);
        if (!playerCanEnter(x, y) || (mineLevel_ && flagged(x, y))) {
            ev |= EV_BLOCKED;
            break;
        }
        motion = d;
        if (!retreat) facing_ = d;
        ev = landOn(x, y, ev, trace);
    }
    return ev;
}

uint32_t World::hit()
{
    if (gameOver_ || exitReached_) return EV_NONE;
    const int tx = px_ + dirX(facing_), ty = py_ + dirY(facing_);
    if (!inside(tx, ty)) return EV_NONE;
    const Obj o = obj(tx, ty);
    uint32_t ev = 0;
    if (o == Obj::NPC) {   // Nobody gets hit: talk instead
        talkNpc_ = (uint8_t)val_[ty][tx];
        return EV_TALK;
    }
    if (o == Obj::CRATE || o == Obj::BUSH) {
        set(tx, ty, Obj::NONE);
        uncover(tx, ty, ev);
        return ev | EV_HIT | EV_BROKE;
    }
    if (o == Obj::ROCK || coloredBox(o) || o == Obj::DOOR || coloredDoor(o)) return EV_HIT;
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
    if (t == Tile::WALL || t == Tile::TREE || (t == Tile::GATE && !gateOpenAt(tx, ty))) return EV_HIT;
    return EV_NONE;   // Swing at nothing
}

// ---- People / story effects ----------------------------------------------------------------

bool World::findNpc(int num, int &x, int &y) const
{
    for (int cy = 0; cy < h_; cy++)
        for (int cx = 0; cx < w_; cx++)
            if (npcAt(cx, cy) == num && num > 0) {
                x = cx;
                y = cy;
                return true;
            }
    return false;
}

bool World::moveNpc(int num, int x, int y)
{
    int ox, oy;
    if (!findNpc(num, ox, oy)) return false;
    if (ox == x && oy == y) return true;
    if (!inside(x, y) || rawObj(x, y) != Obj::NONE || enemyAt(x, y) >= 0 || (x == px_ && y == py_) ||
        !isGround(tile(x, y)) || groupedTile(tile(x, y))) return false;
    obj_[oy][ox] = (uint8_t)Obj::NONE;
    val_[oy][ox] = 0;
    obj_[y][x] = (uint8_t)Obj::NPC;
    val_[y][x] = (uint8_t)num;
    rev_++;
    uint32_t ev = 0;
    uncover(ox, oy, ev);
    return true;
}

bool World::removeNpc(int num)
{
    int x, y;
    if (!findNpc(num, x, y)) return false;
    obj_[y][x] = (uint8_t)Obj::NONE;
    val_[y][x] = 0;
    rev_++;
    uint32_t ev = 0;
    uncover(x, y, ev);
    return true;
}

bool World::clearObj(int x, int y)
{
    const Obj o = rawObj(x, y);
    if (o != Obj::DOOR && !coloredDoor(o) && o != Obj::ROCK && !coloredBox(o) && o != Obj::CRATE && o != Obj::BUSH) return false;
    set(x, y, Obj::NONE);
    uint32_t ev = 0;
    uncover(x, y, ev);
    updateGates(ev);
    return true;
}

void World::addKeys(int n)
{
    keys_ = keys_ + n < 0 ? 0 : keys_ + n > 99 ? 99 : keys_ + n;
    rev_++;
}

// Deterministic, connected safe lattice: mines only at even/even interior cells. Every safe
// cell is reachable without crossing a mine; start strip and pickups are always safe.
const MineLevel MINE_LEVEL[MINE_LEVELS] = {
    {12,12,6}, {14,12,10}, {14,14,15}, {16,14,21}, {18,16,30}, {20,16,38},
    {22,18,50}, {24,18,62}, {26,20,78}, {28,22,96}, {30,24,116}, {32,26,140}
};
bool World::loadMines(int level)
{
    if (level < 1 || level > MINE_LEVELS) return false;
    const MineLevel &c = MINE_LEVEL[level-1];
    char grid[26][33]; const char *rows[26];
    for (int y=0;y<c.h;y++) {
        rows[y]=grid[y];
        for (int x=0;x<c.w;x++) grid[y][x]=(x==0||y==0||x==c.w-1||y==c.h-1)?'T':'.';
        grid[y][c.w]=0;
    }
    uint16_t candidates[192]; int count=0;
    for (int y=2;y<c.h-1;y+=2) for(int x=2;x<c.w-1;x+=2) candidates[count++]=y*c.w+x;
    uint32_t seed=0x4f545200u+(uint32_t)level;
    for(int i=count-1;i>0;--i) {
        seed=seed*1664525u+1013904223u;
        int j=(int)(seed%(uint32_t)(i+1));
        uint16_t tmp=candidates[i]; candidates[i]=candidates[j]; candidates[j]=tmp;
    }
    for(int i=0;i<c.mines;i++) grid[candidates[i]/c.w][candidates[i]%c.w]='m';
    grid[1][1]='S'; grid[1][2]='u'; grid[1][3]='a'; grid[1][4]='z';
    const Rules rules={1,1,1,1,1,1,0,1,1,true,(uint8_t)level};
    if (!load(rows,c.h,rules)) return false;
    mapId_=2; mineLevel_=(uint8_t)level;
    // Show supplies on the safe starting strip even when a nearby mine stops flood fill.
    for(int x=1;x<=4;x++) flags_[1][x]=F_REVEALED;
    return true;
}
int World::safeLeft() const
{
    int left=0;
    if (!mineLevel_) return 0;
    for(int y=1;y<h_-1;y++) for(int x=1;x<w_-1;x++)
        if(rawObj(x,y)!=Obj::MINE && !revealed(x,y)) ++left;
    return left;
}
void World::checkMineWin()
{
    if (mineLevel_ && !gameOver_ && !safeLeft()) { exitReached_=true; rev_++; }
}
BombArea World::bombArea(int size,int x,int y) const
{
    if ((size!=3&&size!=4)||!inside(x,y)) return {0,0,-1,-1};
    const int x0=x-1,y0=y-1,x1=x+size-2,y1=y+size-2;
    return {x0<0?0:x0,y0<0?0:y0,x1>=w_?w_-1:x1,y1>=h_?h_-1:y1};
}
bool World::placeBomb(int size,int x,int y)
{
    if (!mineLevel_ || gameOver_ || exitReached_ || bombs(size)==0 || !inside(x,y)) return false;
    const BombArea a=bombArea(size,x,y);
    bool useful=false;
    for(int cy=a.y0;cy<=a.y1;cy++) for(int cx=a.x0;cx<=a.x1;cx++)
        if (isGround(tile(cx,cy)) && (!revealed(cx,cy)||rawObj(cx,cy)==Obj::MINE)) useful=true;
    if(!useful) return false;
    if(size==3) --bomb3_; else --bomb4_;
    // Remove all mines first; hints are computed from the new board. Flags inside are cleared.
    // No flood outside the preview, so the actual blast exactly matches the preview.
    for(int cy=a.y0;cy<=a.y1;cy++) for(int cx=a.x0;cx<=a.x1;cx++) {
        if (!isGround(tile(cx,cy))) continue;
        if(rawObj(cx,cy)==Obj::MINE) { obj_[cy][cx]=(uint8_t)Obj::NONE; terrain_[cy][cx]=(uint8_t)Tile::CRATER; }
        flags_[cy][cx]=F_REVEALED;
    }
    ++rev_; checkMineWin(); return true;
}
bool World::spawnAt(int x,int y)
{
    if(!inside(x,y)||!isGround(tile(x,y))||tile(x,y)==Tile::EXIT||rawObj(x,y)!=Obj::NONE||enemyAt(x,y)>=0) return false;
    px_=cpX_=x; py_=cpY_=y; facing_=Dir::DOWN; exitReached_=false; ++rev_; return true;
}
void World::carryFrom(const World &w)
{
    keys_=w.keys_; boat_=w.boat_; suit_=w.suit_; energy_=w.energy_; maxEnergy_=w.maxEnergy_;
    memcpy(colorKeys_, w.colorKeys_, sizeof(colorKeys_));
    skates_ = w.skates_;
    suction_ = w.suction_;
    livesLeft_=w.livesLeft_; livesUsed_=w.livesUsed_; ++rev_;
}

// ---- Save / restore -------------------------------------------------------------------------
// Header (16 B): magic u32, version u16, map id u8, w u8, h u8, reserved u8, body length u32,
//                reserved u16.
// Body: player block (PLAYER_BYTES) + per cell 7 bytes (terrain, obj, val, hid, hidVal, flags,
//       dmg) for w * h cells, row major. v4 appends 6 color counts, a u64 group latch,
//       and 36 u16 remaining timers; v5 adds a boolean skates byte. v6 appends suction,
//       enemy count, definition signature (u32), then {x,y,index,direction} per enemy.
//       Older versions reset suction and enemies to their map-defined initial states.
//       Trailer: CRC32 of header + body.

namespace {
constexpr size_t HEADER_BYTES = 16, PLAYER_BYTES = 32, CELL_BYTES = 7;
constexpr size_t S2_BYTES = (size_t)KeyColor::COUNT + 8 + 2 * PLATE_GROUPS;

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

uint32_t World::enemySignature() const
{
    uint32_t signature = 0;
    for (int i = 0; i < enemyCount_; ++i) {
        const EnemyDef &e = enemyDefs_[i];
        const uint8_t fields[] = {(uint8_t)e.kind, e.x, e.y, (uint8_t)e.bounded,
            e.x0, e.y0, e.x1, e.y1, (uint8_t)e.pingPong, e.routeCount};
        signature = crc32(fields, sizeof(fields), signature);
        for (int j = 0; j < e.routeCount; ++j) {
            const uint8_t point[] = {e.route[j].x, e.route[j].y};
            signature = crc32(point, sizeof(point), signature);
        }
    }
    return signature;
}

size_t World::saveSize() const { return HEADER_BYTES + PLAYER_BYTES + CELL_BYTES * (size_t)(w_ * h_) + S2_BYTES + 1 + 6 + 4 * enemyCount_ + 4; }

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
    o.u8((boat_ ? 1 : 0) | (gatesOpen() ? 2 : 0) | (gameOver_ ? 4 : 0) | (exitReached_ ? 8 : 0));
    o.u8(mineLevel_); o.u8(bomb3_); o.u8(bomb4_);
    o.u8(flash_); o.u8(pogo_);   // v3 (bytes 26,27): persistent ability levels (0..3)
    o.p = pb + PLAYER_BYTES;
    for (int y = 0; y < h_; y++)
        for (int x = 0; x < w_; x++) {
            o.u8(terrain_[y][x]); o.u8(obj_[y][x]); o.u8(val_[y][x]); o.u8(hid_[y][x]);
            o.u8(hidVal_[y][x]); o.u8(flags_[y][x]); o.u8(dmg_[y][x]);
        }
    for (uint8_t count : colorKeys_) o.u8(count);
    o.u32((uint32_t)leverGroups_); o.u32((uint32_t)(leverGroups_ >> 32));
    for (uint16_t timer : timers_) o.u16(timer);
    o.u8(skates_ ? 1 : 0);
    o.u8(suction_ ? 1 : 0);
    o.u8(enemyCount_);
    o.u32(enemySignature());
    for (int i = 0; i < enemyCount_; ++i) {
        o.u8(enemies_[i].x); o.u8(enemies_[i].y);
        o.u8(enemies_[i].index); o.u8(enemies_[i].direction);
    }
    o.u32(crc32(buf, n - 4));
    return n;
}

bool World::restore(const uint8_t *buf, size_t n)
{
    if (!buf || !w_ || n < HEADER_BYTES + PLAYER_BYTES + 4) return false;
    Reader r{buf};
    if (r.u32() != SAVE_MAGIC) return false;
    const int version = r.u16();
    if (version < 1 || version > SAVE_VERSION || r.u8() != mapId_ || r.u8() != w_ || r.u8() != h_)
        return false;
    if (n != saveSize() - (version < 4 ? S2_BYTES : 0) - (version < 5 ? 1 : 0) -
        (version < 6 ? 6 + 4 * enemyCount_ : 0)) return false;
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
    const int level = version >= 2 ? r.u8() : 0;
    const int b3 = version >= 2 ? r.u8() : 0, b4 = version >= 2 ? r.u8() : 0;
    const int flash = version >= 3 ? r.u8() : 0, pogo = version >= 3 ? r.u8() : 0;   // v1/v2: default 0
    if (level != mineLevel_ || b3 > 99 || b4 > 99 || bits > 15 || flash > 3 || pogo > 3) return false;
    if (!inside(px, py) || !inside(cpx, cpy) || facing > 3 || gems > gemsTotal || gemsTotal != gemsTotal_ ||
        maxE < 1 || maxE > rules_.energyCap || energy > maxE || steps > 0x7FFFFFFF)
        return false;
    if (rules_.lives == LIVES_INFINITE ? lives != LIVES_INFINITE : (lives < 0 || lives > MAX_LIVES)) return false;
    if (!(bits & 4) && energy < 1) return false;
    r.p = pb + PLAYER_BYTES;
    const uint8_t *cells = r.p;
    uint8_t colors[(int)KeyColor::COUNT] = {};
    uint16_t timers[PLATE_GROUPS] = {};
    uint64_t latches = 0;
    int skates = 0;
    int suction = 0;
    EnemyState states[MAX_ENEMIES]{};
    for (int i = 0; i < enemyCount_; ++i)
        states[i] = {enemyDefs_[i].x, enemyDefs_[i].y, 0, 1};
    if (version >= 4) {
        Reader ext{cells + size_t(w_) * size_t(h_) * CELL_BYTES};
        for (uint8_t &color : colors) color = (uint8_t)ext.u8();
        latches = ext.u32();
        latches |= uint64_t(ext.u32()) << 32;
        if (latches >> PLATE_GROUPS) return false;
        for (int group = 0; group < PLATE_GROUPS; ++group) {
            timers[group] = (uint16_t)ext.u16();
            if (timers[group] > controlSteps_[group] ||
                (controlSteps_[group] && (latches & (uint64_t(1) << group)))) return false;
        }
        if (version >= 5) skates = ext.u8();
        if (skates > 1) return false;
        if (version >= 6) {
            suction = ext.u8();
            if (suction > 1 || ext.u8() != enemyCount_ || ext.u32() != enemySignature()) return false;
            for (int i = 0; i < enemyCount_; ++i) {
                states[i].x = (uint8_t)ext.u8(); states[i].y = (uint8_t)ext.u8();
                states[i].index = (uint8_t)ext.u8(); states[i].direction = (int8_t)ext.s8();
            }
        }
    }
    bool hasPlate[PLATE_GROUPS] = {};
    uint64_t hasLever = 0;
    bool allCovered[PLATE_GROUPS];
    for (int group = 0; group < PLATE_GROUPS; group++) allCovered[group] = true;
    for (int i = 0; i < w_ * h_; i++) {
        const uint8_t *c = cells + i * CELL_BYTES;
        if (c[0] >= (uint8_t)Tile::COUNT || c[1] >= (uint8_t)Obj::COUNT || c[3] >= (uint8_t)Obj::COUNT || c[5] > 7)
            return false;
        if (version < 4 && (c[0] > (uint8_t)Tile::CRATER || c[1] > (uint8_t)Obj::POGO ||
                            c[3] > (uint8_t)Obj::POGO)) return false;
        if (version < 5 && (c[0] > (uint8_t)Tile::BELT_LEFT || c[1] > (uint8_t)Obj::DOOR_CYAN ||
                            c[3] > (uint8_t)Obj::DOOR_CYAN)) return false;
        if (version < 6 && (c[1] == (uint8_t)Obj::SUCTION || c[3] == (uint8_t)Obj::SUCTION)) return false;
        if (c[1] == (uint8_t)Obj::NPC && (c[2] < 1 || c[2] > 9)) return false;
        if (groupedTile((Tile)c[0]) && c[2] >= PLATE_GROUPS)
            return false;
        if (c[0] == (uint8_t)Tile::LEVER) hasLever |= uint64_t(1) << c[2];
        if (plate((Tile)c[0])) {
            hasPlate[c[2]] = true;
            if (!plateCovered((Tile)c[0], (Obj)c[1])) allCovered[c[2]] = false;
        }
    }
    if (latches & ~hasLever) return false;
    uint64_t savedOpenGroups = latches;
    for (int group = 0; group < PLATE_GROUPS; group++)
        if (andGroups_ & (uint64_t(1) << group)) {
            if (!hasPlate[group] || !allCovered[group]) savedOpenGroups &= ~(uint64_t(1) << group);
        } else if (controlSteps_[group]) {
            if (timers[group]) savedOpenGroups |= uint64_t(1) << group;
        } else if (hasPlate[group] && allCovered[group]) savedOpenGroups |= uint64_t(1) << group;
    // Saved positions must still be walkable in the saved map, never a wall/solid.
    for (int pos : {py * w_ + px, cpy * w_ + cpx}) {
        const uint8_t *c = cells + pos * CELL_BYTES;
        const Tile t = (Tile)c[0];
        if (blocksWalk((Obj)c[1]) || !(isGround(t) || t == Tile::LAVA ||
            (t == Tile::WATER && (bits & 1)) ||
            (t == Tile::GATE && (pos == py * w_ + px ||
                               (savedOpenGroups & (uint64_t(1) << c[2])))))) return false;
    }
    if (level && ((lives == 0) != bool(bits & 4) || suit > 1 || b3 > 1 || b4 > 1)) return false;
    for (int i = 0; i < enemyCount_; ++i) {
        const EnemyState &s = states[i];
        const EnemyDef &e = enemyDefs_[i];
        if (!inside(s.x, s.y) || (s.direction != 1 && s.direction != -1) ||
            (!e.pingPong && s.direction != 1) ||
            (e.kind == EnemyKind::PATROL ? s.index >= e.routeCount : s.index != 0) ||
            (e.bounded && (s.x < e.x0 || s.x > e.x1 || s.y < e.y0 || s.y > e.y1))) return false;
        if (e.kind == EnemyKind::PATROL) {
            const EnemyPoint target = e.route[s.index];
            const auto onSegment = [&s, target](EnemyPoint from) {
                return (from.x == target.x ? s.x == target.x : s.y == target.y) &&
                       s.x >= (from.x < target.x ? from.x : target.x) &&
                       s.x <= (from.x > target.x ? from.x : target.x) &&
                       s.y >= (from.y < target.y ? from.y : target.y) &&
                       s.y <= (from.y > target.y ? from.y : target.y);
            };
            int previous = s.index - s.direction;
            if (previous < 0 && !e.pingPong) previous = e.routeCount - 1;
            const bool initialSegment = s.index == 0 && s.direction == 1 &&
                                        onSegment({e.x, e.y});
            if (!initialSegment && (previous < 0 || previous >= e.routeCount ||
                                    !onSegment(e.route[previous]))) return false;
        }
        const uint8_t *c = cells + (s.y * w_ + s.x) * CELL_BYTES;
        const Tile t = (Tile)c[0];
        // A gate may have closed under an enemy since its previous turn.
        if (blocksWalk((Obj)c[1]) || c[1] == (uint8_t)Obj::MINE ||
            (!(isGround(t) || t == Tile::GATE)) ||
            t == Tile::POISON || t == Tile::SWAMP || t == Tile::SHARP_ROCKS) return false;
        for (int j = 0; j < i; ++j)
            if (states[j].x == s.x && states[j].y == s.y) return false;
    }
    // Apply
    bomb3_ = b3; bomb4_ = b4; flash_ = (uint8_t)flash; pogo_ = (uint8_t)pogo;
    skates_ = skates != 0;
    suction_ = suction != 0;
    memcpy(enemies_, states, sizeof(states));
    enemyContact_ = false;
    px_ = px; py_ = py; cpX_ = cpx; cpY_ = cpy; facing_ = (Dir)facing;
    keys_ = keys; gems_ = gems; steps_ = (int)steps;
    energy_ = energy; maxEnergy_ = maxE; livesLeft_ = lives; livesUsed_ = livesUsed; suit_ = suit;
    boat_ = bits & 1; gameOver_ = (bits & 4) != 0; exitReached_ = (bits & 8) != 0;
    memcpy(colorKeys_, colors, sizeof(colors));
    memcpy(timers_, timers, sizeof(timers));
    leverGroups_ = latches;
    for (int y = 0; y < h_; y++)
        for (int x = 0; x < w_; x++) {
            const uint8_t *c = cells + (y * w_ + x) * CELL_BYTES;
            terrain_[y][x] = c[0]; obj_[y][x] = c[1]; val_[y][x] = c[2]; hid_[y][x] = c[3];
            hidVal_[y][x] = c[4]; flags_[y][x] = c[5]; dmg_[y][x] = c[6];
        }
    gateOpenGroups_ = savedOpenGroups;
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
    if (forcedTile(t) && !(t == Tile::ICE && w.hasSkates())) return false;
    if (t == Tile::GATE) return w.gateOpenAt(x, y);
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
    if (forcedTile(t) && !(t == Tile::ICE && w.hasSkates())) return C_BLOCK;
    Cat c;
    if (isGround(t)) c = C_FREE;
    else if (t == Tile::LAVA) c = C_LAVA;
    else if (t == Tile::WATER) c = w.hasBoat() ? C_FREE : C_BOAT;
    else if (t == Tile::GATE) c = w.gateOpenAt(x, y) ? C_FREE : C_BLOCK;
    else c = C_BLOCK;
    if (c == C_BLOCK) return c;
    if (mode == WalkMode::ASSIST && !w.revealed(x, y)) return C_UNKNOWN;
    if (c == C_LAVA && mode == WalkMode::ASSIST && w.suitCharges() <= 0) return C_DEADLY;
    return c;
}

void Planner::heapPush(uint64_t key)
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

uint64_t Planner::heapPop()
{
    const uint64_t top = heap_[0];
    const uint64_t last = heap_[--heapLen_];
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
    heapPush((uint64_t)(STEP_COST * (abs(tx - w.playerX()) + abs(ty - w.playerY()))) << 16 | (uint64_t)start);
    bool found = false;
    while (heapLen_) {
        const int cell = (int)(heapPop() & 0xFFFF);
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
            if (f < (1u << 20)) heapPush((uint64_t)f << 16 | (uint64_t)n);
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
