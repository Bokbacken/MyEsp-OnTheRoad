// adv_loader.cpp - firmware-safe .adv parser. See adv_loader.h. Mirrors tools/adv_parse.cpp.
#include "adv_loader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>   // read the .adv into PSRAM, not the (scarce) internal heap
#endif

namespace Adv {

namespace {

using Otr::Story::Choice;
using Otr::Story::Named;
using Otr::Story::Node;
using Otr::Story::Npc;

void setErr(char *err, size_t cap, int line, const char *msg)
{
    if (err && cap) snprintf(err, cap, "line %d: %s", line, msg);
}

// Trims leading/trailing space/tab/CR in place; returns the trimmed start (NUL-terminated).
char *trimIn(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
    return s;
}

// "key: value" -> key / val (both trimmed, line mutated). false if there is no ':'.
bool splitKV(char *line, char *&key, char *&val)
{
    char *colon = strchr(line, ':');
    if (!colon) return false;
    *colon = 0;
    key = trimIn(line);
    val = trimIn(colon + 1);
    return true;
}

// Cuts a trailing " #..." inline comment and trims, in place.
void stripCommentIn(char *s)
{
    for (char *p = s; *p; p++) {
        if (p[0] == ' ' && p[1] == '#') { *p = 0; break; }
    }
    // re-trim trailing ws
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = 0;
}

// Splits "<sv> | <en>" (space-pipe-space) in place. Missing '|' -> en = "".
void splitBi(char *s, char *&sv, char *&en)
{
    char *bar = strstr(s, " | ");
    if (!bar) {
        sv = trimIn(s);
        en = s + strlen(s);   // empty
        return;
    }
    *bar = 0;
    sv = trimIn(s);
    en = trimIn(bar + 3);
}

bool maskFromTokens(const char *s, uint32_t &mask)
{
    mask = 0;
    char buf[32];
    size_t bi = 0;
    bool valid = true;
    auto flush = [&]() {
        if (!bi) return;
        buf[bi] = 0;
        for (size_t i = 0; i < bi; i++)
            if (buf[i] >= 'A' && buf[i] <= 'Z') buf[i] = (char)(buf[i] - 'A' + 'a');
        if (!strcmp(buf, "energy")) mask |= CARRY_ENERGY;
        else if (!strcmp(buf, "maxenergy") || !strcmp(buf, "max_energy") || !strcmp(buf, "maxhp")) mask |= CARRY_MAXENERGY;
        else if (!strcmp(buf, "items") || !strcmp(buf, "inventory")) mask |= CARRY_ITEMS;
        else if (!strcmp(buf, "lives") || !strcmp(buf, "life")) mask |= CARRY_LIVES;
        else if (!strcmp(buf, "suit")) mask |= CARRY_SUIT;
        else if (!strcmp(buf, "keys") || !strcmp(buf, "key")) mask |= CARRY_KEYS;
        else if (!strcmp(buf, "gems") || !strcmp(buf, "gem")) mask |= CARRY_GEMS;
        else if (!strcmp(buf, "boat")) mask |= CARRY_BOAT;
        else valid = false;
        bi = 0;
    };
    for (const char *p = s; *p; p++) {
        char c = *p;
        if (c == ',' || c == ' ' || c == '\t') flush();
        else if (bi < sizeof(buf) - 1) buf[bi++] = c;
        else valid = false;
    }
    flush();
    return valid;
}

Otr::Obj objFromName(const char *name)
{
    char s[16];
    size_t i = 0;
    for (; name[i] && i < sizeof(s) - 1; i++) s[i] = (name[i] >= 'A' && name[i] <= 'Z') ? (char)(name[i] - 'A' + 'a') : name[i];
    s[i] = 0;
    if (!strcmp(s, "key")) return Otr::Obj::KEY;
    if (!strcmp(s, "gem")) return Otr::Obj::GEM;
    if (!strcmp(s, "boat")) return Otr::Obj::BOAT;
    if (!strcmp(s, "potion")) return Otr::Obj::POTION;
    if (!strcmp(s, "heart")) return Otr::Obj::HEART;
    if (!strcmp(s, "suit")) return Otr::Obj::SUIT;
    if (!strcmp(s, "life")) return Otr::Obj::LIFE;
    if (!strcmp(s, "bomb3")) return Otr::Obj::BOMB3;
    if (!strcmp(s, "bomb4")) return Otr::Obj::BOMB4;
    if (!strcmp(s, "flashlight") || !strcmp(s, "flash")) return Otr::Obj::FLASH;   // v0.18.0
    if (!strcmp(s, "pogo") || !strcmp(s, "pogostick")) return Otr::Obj::POGO;      // v0.18.0
    if (!strcmp(s, "skates")) return Otr::Obj::SKATES;
    if (!strcmp(s, "suction")) return Otr::Obj::SUCTION;
    return Otr::Obj::NONE;
}

bool isBuiltinGlyph(char c)
{
    if (c >= '1' && c <= '9') return true;
    switch (c) {
    case '.': case 'S': case ',': case '_': case '#': case '%': case 'T': case '~':
    case '^': case 'o': case '=': case 'Q': case 'X': case 'R': case 'r': case 'c':
    case 'C': case 'k': case 'K': case 'D': case 'B': case '*': case '+': case '$':
    case 'b': case 'p': case 'h': case 'u': case 'l': case 'a': case 'z': case 'm':
    case 'f': case 'j':   // v0.18.0: flashlight / pogo-stick pickups (level 1)
        return true;
    default:
        return false;
    }
}

struct LegendDef {
    char glyph;
    uint8_t encoded;
    uint8_t plateGroup;
};

constexpr int MAX_LEGEND = 128;

bool groupFromText(const char *text, uint8_t &group)
{
    if (!text || text[0] == 0 || text[1] != 0) return false;
    const char c = text[0];
    if (c == '0') group = 0;
    else if (c >= '1' && c <= '9') group = (uint8_t)(c - '0');
    else if (c >= 'A' && c <= 'Z') group = (uint8_t)(c - 'A' + 10);
    else if (c >= 'a' && c <= 'z') group = (uint8_t)(c - 'a' + 10);
    else return false;
    return true;
}

bool sameWord(const char *a, const char *b)
{
    while (*a && *b) {
        const char ac = (*a >= 'A' && *a <= 'Z') ? (char)(*a - 'A' + 'a') : *a;
        const char bc = (*b >= 'A' && *b <= 'Z') ? (char)(*b - 'A' + 'a') : *b;
        if (ac != bc) return false;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

bool positiveSteps(const char *text, uint16_t &steps)
{
    unsigned n = 0;
    if (!text || !*text) return false;
    for (; *text; text++) {
        if (*text < '0' || *text > '9') return false;
        const unsigned digit = (unsigned)(*text - '0');
        if (n > (65535u - digit) / 10u) return false;
        n = n * 10u + digit;
    }
    if (!n) return false;
    steps = (uint16_t)n;
    return true;
}

bool parseLegendLine(char *line, LegendDef *defs, int &count, int lineNo, char *err, size_t errCap)
{
    stripCommentIn(line);
    char *equals = strchr(line + (line[0] == '=' ? 1 : 0), '=');
    if (!equals) { setErr(err, errCap, lineNo, "legend needs 'glyph = kind [argument]'"); return false; }
    *equals = 0;
    char *glyphText = trimIn(line);
    char *value = trimIn(equals + 1);
    if (strlen(glyphText) != 1) { setErr(err, errCap, lineNo, "legend glyph must be one character"); return false; }
    const char glyph = glyphText[0];
    if (glyph == 'S' || glyph == ' ' || glyph == '\t' || glyph == '\r' || glyph == '\n' || glyph == '@') {
        setErr(err, errCap, lineNo, "reserved legend glyph");
        return false;
    }
    for (int i = 0; i < count; i++)
        if (defs[i].glyph == glyph) { setErr(err, errCap, lineNo, "duplicate legend glyph"); return false; }
    if (count >= MAX_LEGEND) { setErr(err, errCap, lineNo, "too many legend glyphs"); return false; }

    char *kind = strtok(value, " \t");
    char *arg = strtok(nullptr, " \t");
    char *detail = strtok(nullptr, " \t");
    if (!kind || strtok(nullptr, " \t") || (detail && !sameWord(kind, "plate"))) {
        setErr(err, errCap, lineNo, "legend needs 'glyph = kind [argument]'");
        return false;
    }
    uint8_t encoded;
    uint8_t plateGroup = 0;
    if (detail) {
        uint8_t group;
        if (!arg || !groupFromText(arg, group)) {
            setErr(err, errCap, lineNo, "invalid colored plate group"); return false;
        }
        plateGroup = group + 1;
        arg = detail;
    }
    const char *colors[] = {"red", "green", "blue", "yellow", "purple", "cyan"};
    int plateColor = 0;
    for (; plateColor < 6; ++plateColor)
        if (arg && sameWord(arg, colors[plateColor])) break;
    if (detail && plateColor == 6) {
        setErr(err, errCap, lineNo, "invalid plate color"); return false;
    }
    if (sameWord(kind, "plate") && plateColor < 6) {
        encoded = (uint8_t)(Otr::COLORED_PLATE_GLYPH + plateColor);
    } else if (sameWord(kind, "plate") || sameWord(kind, "gate") || sameWord(kind, "lever")) {
        uint8_t group = 0;
        if ((sameWord(kind, "lever") && !arg) || (arg && !groupFromText(arg, group))) {
            setErr(err, errCap, lineNo, "invalid plate/gate/lever group");
            return false;
        }
        encoded = (uint8_t)((sameWord(kind, "plate") ? Otr::GROUPED_PLATE_GLYPH :
                            sameWord(kind, "gate") ? Otr::GROUPED_GATE_GLYPH : Otr::GROUPED_LEVER_GLYPH) + group);
    } else if (sameWord(kind, "ice")) {
        if (arg) { setErr(err, errCap, lineNo, "ice takes no argument"); return false; }
        encoded = Otr::ICE_GLYPH;
    } else if (sameWord(kind, "belt")) {
        const char *dirs[] = {"up", "right", "down", "left"};
        int d = 0;
        for (; d < 4; d++) if (arg && sameWord(arg, dirs[d])) break;
        if (d == 4) { setErr(err, errCap, lineNo, "invalid belt direction"); return false; }
        encoded = (uint8_t)(Otr::BELT_UP_GLYPH + d);
    } else if (sameWord(kind, "skates") || sameWord(kind, "suction") || sameWord(kind, "poison") ||
               sameWord(kind, "swamp") || sameWord(kind, "sharp_rocks")) {
        if (arg) { setErr(err, errCap, lineNo, "terrain/equipment takes no argument"); return false; }
        encoded = sameWord(kind, "suction") ? Otr::SUCTION_GLYPH :
                  sameWord(kind, "skates") ? Otr::SKATES_GLYPH :
                  sameWord(kind, "poison") ? Otr::POISON_GLYPH :
                  sameWord(kind, "swamp") ? Otr::SWAMP_GLYPH : Otr::SHARP_ROCKS_GLYPH;
    } else if (sameWord(kind, "key") || sameWord(kind, "door") || sameWord(kind, "box")) {
        const char *colors[] = {"red", "green", "blue", "yellow", "purple", "cyan"};
        int c = 0;
        for (; c < 6; c++) if (arg && sameWord(arg, colors[c])) break;
        if (c == 6) { setErr(err, errCap, lineNo, "invalid key/door/box color"); return false; }
        encoded = (uint8_t)((sameWord(kind, "key") ? Otr::COLORED_KEY_GLYPH :
                            sameWord(kind, "box") ? Otr::BOX_GLYPH : Otr::COLORED_DOOR_GLYPH) + c);
    } else {
        setErr(err, errCap, lineNo, "unknown legend kind");
        return false;
    }
    defs[count++] = {glyph, encoded, plateGroup};
    return true;
}

enum Sec { NONE, META, ABOUT, RULES, MAP, TREASURES, ENEMIES, RESOURCES, DARK, FLAGS, COUNTERS, ITEMS, SHARED_ITEMS, QUESTS, NPC, NODE, LEGEND,
           PLATEGROUP, GATEGROUP, TIMEDGATE, ANDGATE };

// Maps a "@tag" (without any arg) to a section. Returns false for an unknown tag.
bool secFromTag(const char *tag, Sec &sec)
{
    if (!strcmp(tag, "@meta")) sec = META;
    else if (!strcmp(tag, "@about")) sec = ABOUT;
    else if (!strcmp(tag, "@rules")) sec = RULES;
    else if (!strcmp(tag, "@map")) sec = MAP;
    else if (!strcmp(tag, "@treasures")) sec = TREASURES;
    else if (!strcmp(tag, "@enemies")) sec = ENEMIES;
    else if (!strcmp(tag, "@resources")) sec = RESOURCES;
    else if (!strcmp(tag, "@dark")) sec = DARK;
    else if (!strcmp(tag, "@flags")) sec = FLAGS;
    else if (!strcmp(tag, "@counters")) sec = COUNTERS;
    else if (!strcmp(tag, "@items")) sec = ITEMS;
    else if (!strcmp(tag, "@shareditems")) sec = SHARED_ITEMS;
    else if (!strcmp(tag, "@quests")) sec = QUESTS;
    else if (!strcmp(tag, "@legend")) sec = LEGEND;
    else if (!strcmp(tag, "@plategroup")) sec = PLATEGROUP;
    else if (!strcmp(tag, "@gategroup")) sec = GATEGROUP;
    else if (!strcmp(tag, "@timedgate")) sec = TIMEDGATE;
    else if (!strcmp(tag, "@andgate")) sec = ANDGATE;
    else if (!strcmp(tag, "@group")) sec = ANDGATE;
    else if (!strcmp(tag, "@npc")) sec = NPC;
    else if (!strcmp(tag, "@node")) sec = NODE;
    else return false;
    return true;
}

bool parseCoordinate(char *token, int &x, int &y)
{
    char *comma = strchr(token, ',');
    if (!comma || strchr(comma + 1, ',')) return false;
    *comma = 0;
    auto number = [](const char *s, int &value) {
        if (!*s) return false;
        value = 0;
        for (; *s; s++) {
            if (*s < '0' || *s > '9') return false;
            const int digit = *s - '0';
            if (value > (Otr::MAX_W - digit) / 10) return false;
            value = value * 10 + digit;
        }
        return true;
    };
    return number(token, x) && number(comma + 1, y);
}

bool byteNumber(const char *text, uint8_t &value)
{
    unsigned n = 0;
    if (!text || !*text) return false;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return false;
        const unsigned digit = (unsigned)(*text - '0');
        if (n > (255u - digit) / 10u) return false;
        n = n * 10u + digit;
    }
    value = (uint8_t)n;
    return true;
}

bool parseEnemy(char *line, Otr::EnemyDef &enemy, int lineNo, char *err, size_t errCap)
{
    stripCommentIn(line);
    constexpr int TOKEN_CAP = Otr::MAX_ROUTE + 10;
    char *tokens[TOKEN_CAP];
    int count = 0;
    for (char *p = strtok(line, " \t"); p; p = strtok(nullptr, " \t")) {
        if (count == TOKEN_CAP) { setErr(err, errCap, lineNo, "too many enemy route points/tokens"); return false; }
        tokens[count++] = p;
    }
    auto bad = [&](const char *message) { setErr(err, errCap, lineNo, message); return false; };
    if (count < 3) return bad("enemy needs 'patrol X Y mode [area ...] route ...' or 'chaser X Y [area ...]'");
    enemy = Otr::EnemyDef{};
    if (sameWord(tokens[0], "patrol")) enemy.kind = Otr::EnemyKind::PATROL;
    else if (sameWord(tokens[0], "chaser")) enemy.kind = Otr::EnemyKind::CHASER;
    else return bad("unknown enemy kind");
    if (!byteNumber(tokens[1], enemy.x) || !byteNumber(tokens[2], enemy.y))
        return bad("invalid enemy start coordinates");
    int i = 3;
    if (enemy.kind == Otr::EnemyKind::PATROL) {
        if (i == count || (!sameWord(tokens[i], "loop") && !sameWord(tokens[i], "pingpong")))
            return bad("patrol mode must be loop or pingpong");
        enemy.pingPong = sameWord(tokens[i++], "pingpong");
    }
    if (i < count && sameWord(tokens[i], "area")) {
        if (count - i < 5 ||
            !byteNumber(tokens[i + 1], enemy.x0) || !byteNumber(tokens[i + 2], enemy.y0) ||
            !byteNumber(tokens[i + 3], enemy.x1) || !byteNumber(tokens[i + 4], enemy.y1))
            return bad("enemy area needs four coordinates");
        enemy.bounded = true;
        i += 5;
    }
    if (enemy.kind == Otr::EnemyKind::CHASER)
        return i == count ? true : bad("unexpected chaser arguments");
    if (i == count || !sameWord(tokens[i++], "route") || i == count)
        return bad("patrol needs route X,Y ...");
    for (; i < count; ++i) {
        int x, y;
        if (enemy.routeCount == Otr::MAX_ROUTE) return bad("too many enemy route points");
        if (!parseCoordinate(tokens[i], x, y)) return bad("invalid enemy route coordinate");
        enemy.route[enemy.routeCount++] = {(uint8_t)x, (uint8_t)y};
    }
    return true;
}

// Mirrors the engine's initial enemy terrain: gates start closed and hazards/mines block.
bool enemyWalkable(uint8_t glyph)
{
    if ((glyph >= Otr::GROUPED_PLATE_GLYPH && glyph < Otr::GROUPED_GATE_GLYPH) ||
        (glyph >= Otr::ICE_GLYPH && glyph < Otr::COLORED_DOOR_GLYPH) ||
        (glyph >= Otr::COLORED_PLATE_GLYPH && glyph < Otr::COLORED_PLATE_GLYPH + 6) ||
        glyph == Otr::SKATES_GLYPH || glyph == Otr::SUCTION_GLYPH) return true;
    switch (glyph) {
    case '.': case 'S': case ',': case '_': case 'o': case 'Q': case 'X':
    case 'k': case 'K': case 'B': case '*': case '+': case '$': case 'p':
    case 'h': case 'u': case 'l': case 'a': case 'z': case 'f': case 'j':
        return true;
    default:
        return false;
    }
}

bool enemyRouteTerrain(uint8_t glyph)
{
    if (enemyWalkable(glyph) || (glyph >= '1' && glyph <= '9') ||
        (glyph >= Otr::BOX_GLYPH && glyph < Otr::BOX_GLYPH + 6) ||
        (glyph >= Otr::COLORED_DOOR_GLYPH && glyph < Otr::COLORED_DOOR_GLYPH + 6) ||
        (glyph >= Otr::GROUPED_GATE_GLYPH && glyph < Otr::GROUPED_LEVER_GLYPH)) return true;
    switch (glyph) {
    case 'R': case 'r': case 'c': case 'C': case 'D': case 'b': case '=':
        return true;
    default:
        return false;
    }
}

bool validateEnemies(const Loaded &out, const int *lines, char *err, size_t errCap)
{
    const int width = (int)strlen(out.rows[0]);
    auto inside = [&](int x, int y) { return x < width && y < out.rowCount; };
    for (int i = 0; i < out.enemyCount; ++i) {
        const Otr::EnemyDef &e = out.enemies[i];
        auto bad = [&](const char *message) { setErr(err, errCap, lines[i], message); return false; };
        if (!inside(e.x, e.y)) return bad("enemy start coordinates outside map");
        if (out.rows[e.y][e.x] == 'S') return bad("enemy start overlaps player");
        if (!enemyWalkable((uint8_t)out.rows[e.y][e.x]))
            return bad("enemy start is not walkable (or overlaps NPC)");
        for (int j = 0; j < i; ++j)
            if (e.x == out.enemies[j].x && e.y == out.enemies[j].y)
                return bad("enemy starts overlap");
        if (e.bounded && (e.x0 > e.x1 || e.y0 > e.y1 ||
                         !inside(e.x0, e.y0) || !inside(e.x1, e.y1)))
            return bad("invalid enemy area bounds");
        auto inArea = [&](int x, int y) {
            return !e.bounded || (x >= e.x0 && x <= e.x1 && y >= e.y0 && y <= e.y1);
        };
        if (!inArea(e.x, e.y)) return bad("enemy start outside area");
        for (int j = 0; j < e.routeCount; ++j) {
            const Otr::EnemyPoint &p = e.route[j];
            if (!inside(p.x, p.y)) return bad("enemy route coordinates outside map");
            if (!inArea(p.x, p.y)) return bad("enemy route outside area");
            if (!enemyRouteTerrain((uint8_t)out.rows[p.y][p.x]))
                return bad("enemy route is not walkable");
        }
        auto segment = [&](int x, int y, int tx, int ty) {
            if (x != tx && y != ty) return bad("patrol route segments must be axis-aligned");
            const int dx = (tx > x) - (tx < x), dy = (ty > y) - (ty < y);
            while (x != tx || y != ty) {
                x += dx;
                y += dy;
                if (!enemyRouteTerrain((uint8_t)out.rows[y][x]))
                    return bad("patrol route crosses impassable terrain");
            }
            return true;
        };
        if (e.routeCount) {
            if (!segment(e.x, e.y, e.route[0].x, e.route[0].y)) return false;
            for (int j = 1; j < e.routeCount; ++j)
                if (!segment(e.route[j - 1].x, e.route[j - 1].y, e.route[j].x, e.route[j].y))
                    return false;
            if (!e.pingPong && !segment(e.route[e.routeCount - 1].x, e.route[e.routeCount - 1].y,
                                       e.route[0].x, e.route[0].y)) return false;
        }
    }
    return true;
}

bool applyGroupDirective(char *line, bool plates, Loaded &out, int lineNo, char *err, size_t errCap)
{
    stripCommentIn(line);
    char *colon = strchr(line, ':');
    if (!colon) { setErr(err, errCap, lineNo, "plate/gate group needs 'group: x,y ...'"); return false; }
    *colon = 0;
    uint8_t group;
    if (!groupFromText(trimIn(line), group)) { setErr(err, errCap, lineNo, "invalid plate/gate group"); return false; }
    char *coords = trimIn(colon + 1);
    char *token = strtok(coords, " \t");
    if (!token) { setErr(err, errCap, lineNo, "plate/gate group needs coordinates"); return false; }
    const int width = out.rowCount ? (int)strlen(out.rows[0]) : 0;
    for (; token; token = strtok(nullptr, " \t")) {
        int x, y;
        if (!parseCoordinate(token, x, y) || x >= width || y >= out.rowCount) {
            setErr(err, errCap, lineNo, "invalid plate/gate coordinate");
            return false;
        }
        const uint8_t glyph = (uint8_t)out.rows[y][x];
        const bool isColoredPlate = glyph >= Otr::COLORED_PLATE_GLYPH && glyph < Otr::COLORED_PLATE_GLYPH + 6;
        const bool isPlate = isColoredPlate || (glyph >= Otr::GROUPED_PLATE_GLYPH && glyph < Otr::GROUPED_GATE_GLYPH)
                                 ? true
                                 : glyph == 'o';
        const bool isGate = glyph >= Otr::GROUPED_GATE_GLYPH &&
                                    glyph < Otr::GROUPED_GATE_GLYPH + Otr::PLATE_GROUPS
                                ? true
                                : glyph == '=';
        if ((plates && !isPlate) || (!plates && !isGate)) {
            setErr(err, errCap, lineNo, plates ? "@plategroup cell is not a plate" : "@gategroup cell is not a gate");
            return false;
        }
        if (isColoredPlate && plates) out.plateGroups[y][x] = group + 1;
        else out.rows[y][x] = (char)((plates ? Otr::GROUPED_PLATE_GLYPH : Otr::GROUPED_GATE_GLYPH) + group);
    }
    return true;
}

bool applyGroupDirectives(const char *text, size_t len, Loaded &out, char *err, size_t errCap)
{
    char line[LINE_CAP];
    size_t pos = 0;
    int lineNo = 0;
    while (pos < len) {
        const size_t start = pos;
        while (pos < len && text[pos] != '\n') pos++;
        size_t n = pos - start;
        if (pos < len) pos++;
        lineNo++;
        if (n >= LINE_CAP) continue;  // The main parser reports this with the same line number.
        memcpy(line, text + start, n);
        line[n] = 0;
        char *t = trimIn(line);
        if (t[0] != '@') continue;
        char *space = strpbrk(t, " \t");
        char *arg = nullptr;
        if (space) { *space = 0; arg = trimIn(space + 1); }
        if (!strcmp(t, "@plategroup")) {
            if (!applyGroupDirective(arg ? arg : t + strlen(t), true, out, lineNo, err, errCap)) return false;
        } else if (!strcmp(t, "@gategroup")) {
            if (!applyGroupDirective(arg ? arg : t + strlen(t), false, out, lineNo, err, errCap)) return false;
        }
    }
    return true;
}

// Copies at most cap-1 bytes of s into dst and NUL-terminates.
void copyStr(char *dst, size_t cap, const char *s)
{
    size_t i = 0;
    for (; s[i] && i < cap - 1; i++) dst[i] = s[i];
    dst[i] = 0;
}

// v0.18.0: parses "x0,y0 x1,y1" (exactly 4 integers; commas/spaces/tabs) into a DarkRect.
// Returns false on the wrong token count. Values are clipped to the map bounds in World::load().
bool parseDarkTokens(const char *s, Otr::DarkRect &out)
{
    char buf[LINE_CAP];
    copyStr(buf, sizeof(buf), s);
    char *tok[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    int nt = 0;
    for (char *p = strtok(buf, " ,\t"); p && nt < 5; p = strtok(nullptr, " ,\t")) tok[nt++] = p;
    if (nt != 4) return false;
    uint8_t values[4];
    for (int i = 0; i < 4; ++i)
        if (!byteNumber(tok[i], values[i])) return false;
    out = {values[0], values[1], values[2], values[3]};
    return true;
}

}  // namespace

const char *Loaded::intern(const char *s, size_t len)
{
    if (arenaUsed + len + 1 > (size_t)ARENA_CAP) return nullptr;
    char *dst = arena + arenaUsed;
    memcpy(dst, s, len);
    dst[len] = 0;
    arenaUsed += len + 1;
    return dst;
}

void Loaded::reset()
{
    // Reset backing in place rather than putting a Loaded-sized temporary on the device stack.
    memset(static_cast<void *>(this), 0, sizeof(*this));
}

static bool parseSingleMap(const char *text, size_t len, int requestedMap, Loaded &out, char *err, size_t errCap)
{
    out.reset();
    Otr::Rules rules = {8, 20, (int8_t)Otr::LIVES_INFINITE, 3, 4, 3, 1, 2, 1, false, 0};

    Sec sec = NONE;
    long curNpc = -1, curNode = -1;
    int nodeChoices = 0;
    bool oom = false;
    LegendDef legend[MAX_LEGEND];
    int legendCount = 0;
    int rowLines[MAX_ROWS] = {};
    int treasureLines[MAX_TREASURES] = {};
    int enemyLines[Otr::MAX_ENEMIES] = {};
    int timedLines[Otr::PLATE_GROUPS] = {};
    int andLines[Otr::PLATE_GROUPS] = {};
    uint16_t timedSteps[Otr::PLATE_GROUPS] = {};
    int par = 0;

    // intern helper: sets oom on overflow (checked at the end).
    auto IN = [&](const char *s) -> const char * {
        const char *p = out.intern(s, strlen(s));
        if (!p) oom = true;
        return p;
    };

    char line[LINE_CAP];
    size_t pos = 0;
    int lineNo = 0;

    while (pos < len) {
        size_t start = pos;
        while (pos < len && text[pos] != '\n') pos++;
        size_t rawN = pos - start;
        if (pos < len) pos++;   // skip '\n'
        lineNo++;
        if (rawN >= LINE_CAP) { setErr(err, errCap, lineNo, "line too long"); return false; }
        memcpy(line, text + start, rawN);
        if (rawN && line[rawN - 1] == '\r') rawN--;
        line[rawN] = 0;

        // Inside @map, only a new section or a blank line ends/skips a row; '#' is map data.
        if (sec == MAP) {
            char tmp[LINE_CAP];
            memcpy(tmp, line, rawN + 1);
            char *t = trimIn(tmp);
            if (t[0] == '@') {
                sec = NONE;   // fall through to section handling below
            } else if (t[0] == 0) {
                continue;
            } else {
                if (out.rowCount >= MAX_ROWS) { setErr(err, errCap, lineNo, "too many map rows"); return false; }
                size_t w = strlen(line);
                if (w > (size_t)Otr::MAX_W) { setErr(err, errCap, lineNo, "map row too wide"); return false; }
                memcpy(out.rows[out.rowCount], line, w + 1);
                rowLines[out.rowCount] = lineNo;
                out.rowCount++;
                continue;
            }
        }

        char *t = trimIn(line);
        if (t[0] == 0 || (t[0] == '#' && !(sec == LEGEND && strchr(t, '=')))) continue;

        if (t[0] == '@') {
            // "@tag" or "@tag arg".
            char *arg = nullptr;
            char *space = strpbrk(t, " \t");
            if (space) { *space = 0; arg = trimIn(space + 1); }
            Sec ns;
            if (!secFromTag(t, ns)) { setErr(err, errCap, lineNo, "unknown section"); return false; }
            if (arg && strstr(arg, "map=") && ns != NPC) {
                setErr(err, errCap, lineNo, "unexpected map selector after preprocessing"); return false;
            }
            if (ns == MAP && ((arg && arg[0] && arg[0] != '#') || out.rowCount)) {
                setErr(err, errCap, lineNo, "unexpected map declaration after preprocessing"); return false;
            }
            if (ns == ENEMIES && arg && arg[0] && arg[0] != '#') {
                setErr(err, errCap, lineNo, "@enemies takes no arguments"); return false;
            }
            if (ns == ANDGATE) {
                uint8_t group;
                if (arg) stripCommentIn(arg);
                if (!strcmp(t, "@group")) {
                    char *groupText, *mode;
                    if (!arg || !splitKV(arg, groupText, mode) || !sameWord(mode, "and")) {
                        setErr(err, errCap, lineNo, "@group needs 'group: and'"); return false;
                    }
                    arg = groupText;
                }
                if (!arg || !groupFromText(arg, group)) {
                    setErr(err, errCap, lineNo, "@andgate needs one group"); return false;
                }
                if (andLines[group] || timedLines[group]) {
                    setErr(err, errCap, lineNo, "duplicate/conflicting gate control"); return false;
                }
                andLines[group] = lineNo;
                sec = NONE;
                continue;
            }
            if (ns == TIMEDGATE) {
                char *groupText, *stepsText;
                uint8_t group;
                uint16_t steps;
                if (arg) stripCommentIn(arg);
                if (!arg || !splitKV(arg, groupText, stepsText) ||
                    !groupFromText(groupText, group) || !positiveSteps(stepsText, steps)) {
                    setErr(err, errCap, lineNo, "@timedgate needs 'group: steps' (1..65535)");
                    return false;
                }
                if (timedLines[group] || andLines[group]) { setErr(err, errCap, lineNo, "duplicate/conflicting timed gate group"); return false; }
                timedLines[group] = lineNo;
                timedSteps[group] = steps;
                sec = NONE;
                continue;
            }
            if (ns == PLATEGROUP || ns == GATEGROUP) {
                sec = NONE;
                continue;
            }
            if (ns == DARK && arg && arg[0]) {
                // v0.18.0: inline rect on the header ("@dark x0,y0 x1,y1"). More rects may follow as
                // body lines; exceeding MAX_DARK is a structural error.
                Otr::DarkRect dr{};
                if (!parseDarkTokens(arg, dr)) { setErr(err, errCap, lineNo, "@dark needs 'x0,y0 x1,y1'"); return false; }
                if (out.darkCount >= Otr::MAX_DARK) { setErr(err, errCap, lineNo, "too many dark rectangles"); return false; }
                out.darks[out.darkCount++] = dr;
            }
            if (ns == NPC) {
                uint8_t npcMap = 1;
                if (arg) {
                    char *selector = strstr(arg, " map=");
                    if (selector) {
                        char *end = selector + 5;
                        unsigned value = 0;
                        if (!*end) { setErr(err, errCap, lineNo, "invalid NPC map selector"); return false; }
                        for (; *end && *end != ' ' && *end != '\t'; ++end) {
                            if (*end < '0' || *end > '9' || value > (255u - (unsigned)(*end - '0')) / 10u) {
                                setErr(err, errCap, lineNo, "invalid NPC map selector"); return false;
                            }
                            value = value * 10u + (unsigned)(*end - '0');
                        }
                        if (!value) { setErr(err, errCap, lineNo, "invalid NPC map selector"); return false; }
                        npcMap = (uint8_t)value;
                        *selector = 0;
                        arg = trimIn(arg);
                    }
                }
                if (out.npcCount >= MAX_NPCS) { setErr(err, errCap, lineNo, "too many npcs"); return false; }
                Npc &n = out.npcs[out.npcCount];
                n = Npc{};
                n.id = IN(arg ? arg : "");
                n.mapId = npcMap;
                out.npcCount++;
                curNpc = out.npcCount - 1;
            } else if (ns == NODE) {
                if (out.nodeCount >= MAX_NODES) { setErr(err, errCap, lineNo, "too many nodes"); return false; }
                Node &n = out.nodes[out.nodeCount];
                n = Node{};
                n.id = IN(arg ? arg : "");
                out.nodeCount++;
                curNode = out.nodeCount - 1;
                nodeChoices = 0;
            }
            sec = ns;
            continue;
        }

        switch (sec) {
            case META: {
                char *k, *v;
                if (!splitKV(t, k, v)) break;
                stripCommentIn(v);
                if (!strcmp(k, "id")) copyStr(out.id, sizeof(out.id), v);
                else if (!strcmp(k, "world")) out.world = atoi(v);
                else if (!strcmp(k, "map_id")) out.mapId = atoi(v);
                else if (!strcmp(k, "name.sv")) { if (!out.titleSv[0]) copyStr(out.titleSv, sizeof(out.titleSv), v); }
                else if (!strcmp(k, "name.en")) { if (!out.titleEn[0]) copyStr(out.titleEn, sizeof(out.titleEn), v); }
                else if (!strcmp(k, "desc.sv")) { if (!out.descSv[0]) copyStr(out.descSv, sizeof(out.descSv), v); }
                else if (!strcmp(k, "desc.en")) { if (!out.descEn[0]) copyStr(out.descEn, sizeof(out.descEn), v); }
                break;
            }
            case ABOUT: {
                char *k, *v;
                if (!splitKV(t, k, v)) break;
                stripCommentIn(v);
                if (!strcmp(k, "title.sv")) copyStr(out.titleSv, sizeof(out.titleSv), v);
                else if (!strcmp(k, "title.en")) copyStr(out.titleEn, sizeof(out.titleEn), v);
                else if (!strcmp(k, "desc.sv") || !strcmp(k, "description.sv")) copyStr(out.descSv, sizeof(out.descSv), v);
                else if (!strcmp(k, "desc.en") || !strcmp(k, "description.en")) copyStr(out.descEn, sizeof(out.descEn), v);
                else if (!strcmp(k, "author")) copyStr(out.author, sizeof(out.author), v);
                else if (!strcmp(k, "cover") || !strcmp(k, "cover.image")) copyStr(out.cover, sizeof(out.cover), v);
                break;
            }
            case RULES: {
                char *k, *v;
                if (!splitKV(t, k, v)) {
                    if (!strncmp(t, "par", 3) && (t[3] == 0 || t[3] == ' ' || t[3] == '\t')) {
                        setErr(err, errCap, lineNo, "par needs 'par: steps' (1..65535)");
                        return false;
                    }
                    break;
                }
                stripCommentIn(v);
                if (requestedMap != 1 &&
                    (!strcmp(k, "start_energy") || !strcmp(k, "energy_cap") || !strcmp(k, "lives") ||
                     !strcmp(k, "carry") || !strcmp(k, "reset"))) {
                    setErr(err, errCap, lineNo, "energy, lives, carry, and reset rules are main-map-only"); return false;
                }
                if (!strcmp(k, "par")) {
                    uint16_t steps;
                    if (!positiveSteps(v, steps)) { setErr(err, errCap, lineNo, "par must be 1..65535"); return false; }
                    par = steps;
                    break;
                }
                const int n = atoi(v);
                if (!strcmp(k, "start_energy")) rules.startEnergy = (uint8_t)n;
                else if (!strcmp(k, "energy_cap")) rules.energyCap = (uint8_t)n;
                else if (!strcmp(k, "lives")) rules.lives = !strcmp(v, "infinite") ? (int8_t)Otr::LIVES_INFINITE : (int8_t)n;
                else if (!strcmp(k, "lava_damage")) rules.lavaDamage = (uint8_t)n;
                else if (!strcmp(k, "mine_damage")) rules.mineDamage = (uint8_t)n;
                else if (!strcmp(k, "suit_base_steps")) rules.suitBaseSteps = (uint8_t)n;
                else if (!strcmp(k, "suit_per_extra_max")) rules.suitPerExtraMax = (uint8_t)n;
                else if (!strcmp(k, "potion_default")) rules.potionDefault = (uint8_t)n;
                else if (!strcmp(k, "heart_default")) rules.heartDefault = (uint8_t)n;
                else if (!strcmp(k, "fog")) {
                    if (!strcmp(v, "true") || !strcmp(v, "1")) rules.fog = true;
                    else if (!strcmp(v, "false") || !strcmp(v, "0")) rules.fog = false;
                    else { setErr(err, errCap, lineNo, "fog must be true or false"); return false; }
                }
                // True minesweeper activator: mine_level 1..12. Alias minesweeper: true/yes/1 == 1.
                // Implies fog (also forced on in World::load). Must stay in sync with tools/adv_parse.cpp.
                else if (!strcmp(k, "mine_level")) {
                    if (n < 0 || n > Otr::MINE_LEVELS) { setErr(err, errCap, lineNo, "mine_level must be 0..12"); return false; }
                    rules.mineLevel = (uint8_t)n;
                    if (rules.mineLevel) rules.fog = true;
                } else if (!strcmp(k, "minesweeper")) {
                    if (!strcmp(v, "true") || !strcmp(v, "yes") || !strcmp(v, "1")) { rules.mineLevel = 1; rules.fog = true; }
                    else if (strcmp(v, "false") && strcmp(v, "no") && strcmp(v, "0")) {
                        setErr(err, errCap, lineNo, "minesweeper must be true or false"); return false;
                    }
                } else if (!strcmp(k, "carry")) {
                    if (!maskFromTokens(v, out.carryMask)) { setErr(err, errCap, lineNo, "unknown carry token"); return false; }
                } else if (!strcmp(k, "reset")) {
                    if (!maskFromTokens(v, out.resetMask)) { setErr(err, errCap, lineNo, "unknown reset token"); return false; }
                }
                else { setErr(err, errCap, lineNo, "unknown @rules key"); return false; }
                break;
            }
            case FLAGS: {
                stripCommentIn(t);
                if (!t[0]) break;
                if (out.flagCount >= MAX_FLAGS) { setErr(err, errCap, lineNo, "too many flags"); return false; }
                out.flags[out.flagCount++] = IN(t);
                break;
            }
            case COUNTERS: {
                stripCommentIn(t);
                if (!t[0]) break;
                if (out.counterCount >= MAX_COUNTERS) { setErr(err, errCap, lineNo, "too many counters"); return false; }
                out.counters[out.counterCount++] = IN(t);
                break;
            }
            case ITEMS:
            case SHARED_ITEMS:
            case QUESTS: {
                // id : sv : en
                char *c1 = strchr(t, ':');
                if (!c1) break;
                *c1 = 0;
                char *idp = trimIn(t);
                char *rest = c1 + 1;
                char *c2 = strchr(rest, ':');
                char *capText = nullptr;
                char *sv, *en;
                if (!c2) { sv = trimIn(rest); en = rest + strlen(rest); }
                else {
                    *c2 = 0;
                    sv = trimIn(rest);
                    char *enText = c2 + 1;
                    char *c3 = strchr(enText, ':');
                    if (c3) {
                        *c3 = 0;
                        capText = trimIn(c3 + 1);
                    }
                    en = trimIn(enText);
                }
                const bool isItems = (sec == ITEMS || sec == SHARED_ITEMS);
                int &cnt = isItems ? out.itemCount : out.questCount;
                const int cap = isItems ? MAX_ITEMS : MAX_QUESTS;
                if (sec == SHARED_ITEMS) {
                    char *third = c2 ? trimIn(c2 + 1) : nullptr;
                    char *fourth = third ? strchr(third, ':') : nullptr;
                    if (fourth) { *fourth = 0; capText = trimIn(fourth + 1); }
                    uint8_t itemCap = 255;
                    if (capText && (!byteNumber(capText, itemCap) || itemCap == 0)) {
                        setErr(err, errCap, lineNo, "item cap must be 1..255"); return false;
                    }
                    for (int i = 0; i < out.itemCount; ++i) {
                        if (!strcmp(out.items[i].id, idp)) {
                            if (strcmp(out.items[i].name.s[0] ? out.items[i].name.s[0] : "", sv) ||
                                strcmp(out.items[i].name.s[1] ? out.items[i].name.s[1] : "", en) ||
                                out.items[i].cap != itemCap) {
                                setErr(err, errCap, lineNo, "resource id has conflicting name or cap"); return false;
                            }
                            break;
                        }
                    }
                    bool found = false;
                    for (int i = 0; i < out.itemCount; ++i)
                        if (!strcmp(out.items[i].id, idp)) { found = true; break; }
                    if (found) break;
                }
                if (cnt >= cap) { setErr(err, errCap, lineNo, isItems ? "too many items" : "too many quests"); return false; }
                Named &nm = (isItems ? out.items : out.quests)[cnt];
                nm = Named{};
                nm.id = IN(idp);
                nm.name.s[0] = IN(sv);
                nm.name.s[1] = en[0] ? IN(en) : nullptr;
                if (isItems && capText) {
                    uint8_t itemCap;
                    if (!byteNumber(capText, itemCap) || itemCap == 0) {
                        setErr(err, errCap, lineNo, "item cap must be 1..255"); return false;
                    }
                    nm.cap = itemCap;
                }
                cnt++;
                break;
            }
            case TREASURES: {
                stripCommentIn(t);
                // "x y item [amount]"
                char buf[LINE_CAP];
                copyStr(buf, sizeof(buf), t);
                char *tok[5] = {};
                int nt = 0;
                for (char *p = strtok(buf, " ,\t"); p && nt < 5; p = strtok(nullptr, " ,\t")) tok[nt++] = p;
                if (nt < 3 || nt > 4) { setErr(err, errCap, lineNo, "treasure needs 'x y item [amount]'"); return false; }
                uint8_t x, y, amount = 1;
                if (!byteNumber(tok[0], x) || !byteNumber(tok[1], y) ||
                    (nt == 4 && !byteNumber(tok[3], amount))) {
                    setErr(err, errCap, lineNo, "invalid treasure coordinates/amount"); return false;
                }
                const Otr::Obj item = objFromName(tok[2]);
                if (item == Otr::Obj::NONE) { setErr(err, errCap, lineNo, "unknown treasure item"); return false; }
                if (out.treasureCount >= MAX_TREASURES) { setErr(err, errCap, lineNo, "too many treasures"); return false; }
                treasureLines[out.treasureCount] = lineNo;
                Otr::Treasure &tr = out.treasures[out.treasureCount++];
                tr = Otr::Treasure{};
                tr.x = x;
                tr.y = y;
                tr.item = item;
                tr.amount = amount;
                break;
            }
            case ENEMIES:
                if (out.enemyCount == Otr::MAX_ENEMIES) {
                    setErr(err, errCap, lineNo, "too many enemies"); return false;
                }
                if (!parseEnemy(t, out.enemies[out.enemyCount], lineNo, err, errCap)) return false;
                enemyLines[out.enemyCount++] = lineNo;
                break;
            case DARK: {
                // One dark rectangle per body line: "x0,y0 x1,y1". Exceeding MAX_DARK is an error.
                stripCommentIn(t);
                if (!t[0]) break;
                Otr::DarkRect dr{};
                if (!parseDarkTokens(t, dr)) { setErr(err, errCap, lineNo, "@dark rect needs 'x0,y0 x1,y1'"); return false; }
                if (out.darkCount >= Otr::MAX_DARK) { setErr(err, errCap, lineNo, "too many dark rectangles"); return false; }
                out.darks[out.darkCount++] = dr;
                break;
            }
            case LEGEND:
                if (!parseLegendLine(t, legend, legendCount, lineNo, err, errCap)) return false;
                break;
            case RESOURCES: {
                // "glyph : id : name.sv : name.en : max : image"
                stripCommentIn(t);
                char *f[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
                int nf = 0;
                char *seg = t;
                while (nf < 6) {
                    char *colon = strchr(seg, ':');
                    if (colon) *colon = 0;
                    f[nf++] = trimIn(seg);
                    if (!colon) break;
                    seg = colon + 1;
                }
                if (nf < 2 || strlen(f[0]) != 1 || !f[1][0]) {
                    setErr(err, errCap, lineNo, "resource needs 'glyph : id : name.sv : name.en : max : image'");
                    return false;
                }
                const char g = f[0][0];
                if ((unsigned char)g <= 32 || (unsigned char)g >= 127 || g == '@' || isBuiltinGlyph(g)) {
                    setErr(err, errCap, lineNo, "resource glyph clashes with a built-in glyph / syntax"); return false;
                }
                if (strlen(f[1]) >= 40) { setErr(err, errCap, lineNo, "resource id too long"); return false; }
                for (int i = 0; i < out.resourceCount; i++)
                    if (out.resources[i].glyph == g) { setErr(err, errCap, lineNo, "duplicate resource glyph"); return false; }
                    else if (!strcmp(out.resources[i].id, f[1])) {
                        setErr(err, errCap, lineNo, "duplicate resource id on this map"); return false;
                    }
                if (out.resourceCount >= MAX_RESOURCES) { setErr(err, errCap, lineNo, "too many resources"); return false; }
                Resource &r = out.resources[out.resourceCount++];
                r = Resource{};
                r.glyph = g;
                r.id = IN(f[1]);
                const char *nsv = nf >= 3 && f[2][0] ? f[2] : f[1];
                const char *nen = nf >= 4 && f[3][0] ? f[3] : nullptr;
                r.name.s[0] = IN(nsv);
                r.name.s[1] = nen ? IN(nen) : nullptr;
                int cap = nf >= 5 && f[4][0] ? atoi(f[4]) : 1;
                if (cap < 1) cap = 1;
                if (cap > 255) cap = 255;
                r.cap = (uint8_t)cap;
                const char *image = nf >= 6 ? f[5] : "";
                if (strlen(image) >= sizeof(r.image) || strstr(image, "..") ||
                    strpbrk(image, "/\\:") || (image[0] && (strlen(image) < 5 ||
                    strcasecmp(image + strlen(image) - 4, ".png")))) {
                    setErr(err, errCap, lineNo, "resource image must be a PNG filename (max 39 bytes)"); return false;
                }
                copyStr(r.image, sizeof(r.image), image);
                // The resource IS a story item (so has:/give/take validate).
                int itemIndex = -1;
                for (int i = 0; i < out.itemCount; ++i)
                    if (!strcmp(out.items[i].id, f[1])) { itemIndex = i; break; }
                if (itemIndex < 0) {
                    if (out.itemCount >= MAX_ITEMS) { setErr(err, errCap, lineNo, "too many items (resource)"); return false; }
                    itemIndex = out.itemCount++;
                    Named &nm = out.items[itemIndex];
                    nm = Named{};
                    nm.id = IN(f[1]);
                    nm.name.s[0] = IN(nsv);
                    nm.name.s[1] = nen ? IN(nen) : nullptr;
                    nm.cap = r.cap;
                } else {
                    const Named &nm = out.items[itemIndex];
                    const char *oldEn = nm.name.s[1] ? nm.name.s[1] : "";
                    if (strcmp(nm.name.s[0] ? nm.name.s[0] : "", nsv) ||
                        strcmp(oldEn, nen ? nen : "") || nm.cap != r.cap) {
                        setErr(err, errCap, lineNo, "resource id has conflicting name, cap, or image");
                        return false;
                    }
                }
                break;
            }
            case NPC: {
                if (curNpc < 0) break;
                Npc &n = out.npcs[curNpc];
                char *k, *v;
                if (!splitKV(t, k, v)) break;
                stripCommentIn(v);
                if (!strcmp(k, "name")) {
                    char *sv, *en;
                    splitBi(v, sv, en);
                    n.name.s[0] = IN(sv);
                    n.name.s[1] = en[0] ? IN(en) : nullptr;
                } else if (!strcmp(k, "look")) {
                    n.look = (uint8_t)atoi(v);
                } else if (!strcmp(k, "num")) {
                    n.num = (uint8_t)atoi(v);
                }
                break;
            }
            case NODE: {
                if (curNode < 0) break;
                Node &n = out.nodes[curNode];
                // splitKV but keep the raw value (do NOT strip comments from dialogue text).
                char *colon = strchr(t, ':');
                if (!colon) break;
                *colon = 0;
                char *k = trimIn(t);
                char *v = trimIn(colon + 1);
                if (!strcmp(k, "cond")) n.cond = IN(v);
                else if (!strcmp(k, "speaker")) n.speaker = IN(v);
                else if (!strcmp(k, "ref")) n.ref = IN(v);
                else if (!strcmp(k, "quote")) n.quote = IN(v);
                else if (!strcmp(k, "effects")) n.effects = IN(v);
                else if (!strcmp(k, "text.sv")) n.text.s[0] = IN(v);
                else if (!strcmp(k, "text.en")) n.text.s[1] = IN(v);
                else if (!strcmp(k, "choice")) {
                    if (nodeChoices >= Otr::Story::MAX_CHOICES) { setErr(err, errCap, lineNo, "too many choices"); return false; }
                    // <sv> | <en> => <target> [?? cond] [!! effects] [~~ hintsv | hinten]
                    char *arrow = strstr(v, " => ");
                    if (!arrow) { setErr(err, errCap, lineNo, "choice missing '=>'"); return false; }
                    *arrow = 0;
                    char *texts = trimIn(v);
                    char *rest = arrow + 4;
                    // Locate optional markers in `rest` (they are space-delimited 2-char markers).
                    char *pc = strstr(rest, " ?? ");
                    char *pe = strstr(rest, " !! ");
                    char *ph = strstr(rest, " ~~ ");
                    // target = rest up to the first marker.
                    char *endTarget = rest + strlen(rest);
                    for (char *m : {pc, pe, ph})
                        if (m && m < endTarget) endTarget = m;
                    // Terminate the target at the first marker and keep it terminated: the
                    // segments start at marker+4 (past this NUL), so they are unaffected.
                    *endTarget = 0;
                    char *target = trimIn(rest);

                    auto segment = [&](char *markerStart) -> char * {
                        char *s = markerStart + 4;
                        char *stop = s + strlen(s);
                        for (char *m : {pc, pe, ph})
                            if (m && m >= s && m < stop) stop = m;
                        *stop = 0;
                        return trimIn(s);
                    };

                    Choice &c = n.choices[nodeChoices];
                    c = Choice{};
                    char *sv, *en;
                    splitBi(texts, sv, en);
                    c.text.s[0] = IN(sv);
                    c.text.s[1] = en[0] ? IN(en) : nullptr;
                    c.next = IN(target);
                    // Extract segments carefully: compute all before mutating, since segment()
                    // writes NULs. Process in file order of markers to avoid clobbering.
                    // Hint first is fine because segment() only truncates at a later marker.
                    if (ph) {
                        char *hint = segment(ph);
                        char *hsv, *hen;
                        splitBi(hint, hsv, hen);
                        c.hint.s[0] = IN(hsv);
                        c.hint.s[1] = hen[0] ? IN(hen) : nullptr;
                    }
                    if (pe) c.effects = IN(segment(pe));
                    if (pc) c.cond = IN(segment(pc));
                    nodeChoices++;
                }
                break;
            }
            default:
                break;
        }
    }

    if (oom) { setErr(err, errCap, lineNo, "out of string space"); return false; }
    for (int i = 0; i < out.itemCount; ++i)
        for (int j = 0; j < i; ++j)
            if (!strcmp(out.items[i].id, out.items[j].id)) {
                setErr(err, errCap, 0, "duplicate item / resource id"); return false;
            }

    // --- Apply grouped plate/gate authoring before translating resources to grass. ---
    if (out.rowCount == 0) { setErr(err, errCap, 0, "no @map rows"); return false; }
    const size_t w = strlen(out.rows[0]);
    for (int i = 0; i < out.rowCount; i++) {
        if (strlen(out.rows[i]) != w) { setErr(err, errCap, 0, "map rows not equal width"); return false; }
    }
    for (int y = 0; y < out.rowCount; y++)
        for (size_t x = 0; x < w; x++)
            for (int i = 0; i < legendCount; i++)
                if (out.rows[y][x] == legend[i].glyph) {
                    out.rows[y][x] = (char)legend[i].encoded;
                    out.plateGroups[y][x] = legend[i].plateGroup;
                    break;
                }
    if (!applyGroupDirectives(text, len, out, err, errCap)) return false;

    for (int i = 0; i < out.treasureCount; i++) {
        const Otr::Treasure &tr = out.treasures[i];
        if (tr.x >= w || tr.y >= out.rowCount) {
            setErr(err, errCap, treasureLines[i], "treasure coordinates outside map"); return false;
        }
        const uint8_t c = (uint8_t)out.rows[tr.y][tr.x];
        if (c == 'o' || c == '=' ||
            (c >= Otr::COLORED_PLATE_GLYPH && c < Otr::COLORED_PLATE_GLYPH + 6) ||
            (c >= Otr::GROUPED_PLATE_GLYPH && c < Otr::GROUPED_LEVER_GLYPH + Otr::PLATE_GROUPS)) {
            setErr(err, errCap, treasureLines[i], "treasure cannot share a plate, gate or lever cell");
            return false;
        }
    }

    // Validate final groups, including coordinate overrides. Keep provenance per row/group,
    // rather than adding a source-line array for every map cell.
    uint16_t plateCounts[Otr::PLATE_GROUPS] = {};
    uint16_t leverCounts[Otr::PLATE_GROUPS] = {};
    bool gates[Otr::PLATE_GROUPS] = {};
    int leverLines[Otr::PLATE_GROUPS] = {};
    for (int y = 0; y < out.rowCount; y++) {
        for (size_t x = 0; x < w; x++) {
            const uint8_t c = (uint8_t)out.rows[y][x];
            if (c == 'o') plateCounts[0]++;
            else if (c >= Otr::COLORED_PLATE_GLYPH && c < Otr::COLORED_PLATE_GLYPH + 6)
                plateCounts[out.plateGroups[y][x] ? out.plateGroups[y][x] - 1 : 0]++;
            else if (c == '=') gates[0] = true;
            else if (c >= Otr::GROUPED_PLATE_GLYPH && c < Otr::GROUPED_PLATE_GLYPH + Otr::PLATE_GROUPS)
                plateCounts[c - Otr::GROUPED_PLATE_GLYPH]++;
            else if (c >= Otr::GROUPED_GATE_GLYPH && c < Otr::GROUPED_GATE_GLYPH + Otr::PLATE_GROUPS)
                gates[c - Otr::GROUPED_GATE_GLYPH] = true;
            else if (c >= Otr::GROUPED_LEVER_GLYPH && c < Otr::GROUPED_LEVER_GLYPH + Otr::PLATE_GROUPS) {
                const int group = c - Otr::GROUPED_LEVER_GLYPH;
                leverCounts[group]++;
                leverLines[group] = rowLines[y];
            }
        }
    }
    for (int g = 0; g < Otr::PLATE_GROUPS; g++) {
        if (leverCounts[g] && plateCounts[g] && !andLines[g]) {
            setErr(err, errCap, leverLines[g], "group cannot mix levers and plates");
            return false;
        }
        if (timedLines[g] && (!gates[g] || plateCounts[g] + leverCounts[g] != 1)) {
            setErr(err, errCap, timedLines[g], "timed gate needs a gate and exactly one lever or plate");
            return false;
        }
        if (leverCounts[g] || timedLines[g] || andLines[g])
            out.controls[out.controlCount++] = {(uint8_t)g, timedSteps[g], andLines[g] != 0};
    }

    // --- Build the MapDef rows (translate resource glyphs to grass, record placements) ---
    for (int i = 0; i < out.rowCount; i++) {
        for (size_t x = 0; x < w; x++) {
            char ch = out.rows[i][x];
            for (int r = 0; r < out.resourceCount; r++) {
                if (ch == out.resources[r].glyph) {
                    if (out.placementCount >= MAX_RES_PLACE) { setErr(err, errCap, 0, "too many resource placements"); return false; }
                    ResourcePlace &pl = out.placements[out.placementCount++];
                    pl.glyph = ch;
                    pl.x = (uint8_t)x;
                    pl.y = (uint8_t)i;
                    int itemIndex = -1;
                    for (int k = 0; k < out.itemCount; ++k)
                        if (!strcmp(out.items[k].id, out.resources[r].id)) itemIndex = k;
                    if (itemIndex < 0) { setErr(err, errCap, 0, "missing resource item"); return false; }
                    out.pickups[out.placementCount - 1] = {(uint8_t)x, (uint8_t)i, (uint8_t)itemIndex};
                    out.rows[i][x] = '.';
                    break;
                }
            }
        }
        out.rowPtrs[i] = out.rows[i];
    }
    if (!validateEnemies(out, enemyLines, err, errCap)) return false;
    out.map.id = (uint8_t)out.mapId;
    out.map.rows = out.rowPtrs;
    out.map.h = out.rowCount;
    out.map.rules = rules;
    out.map.treasures = out.treasureCount ? out.treasures : nullptr;
    out.map.treasureCount = out.treasureCount;
    out.map.darks = out.darkCount ? out.darks : nullptr;   // v0.18.0
    out.map.darkCount = out.darkCount;
    out.map.controls = out.controlCount ? out.controls : nullptr;
    out.map.controlCount = out.controlCount;
    out.map.par = par;
    out.map.plateGroups = out.plateGroups;
    out.map.enemies = out.enemyCount ? out.enemies : nullptr;
    out.map.enemyCount = out.enemyCount;
    out.map.resources = out.placementCount ? out.pickups : nullptr;
    out.map.resourceCount = out.placementCount;

    // --- Build the Script ---
    out.script.npcs = out.npcs;
    out.script.npcCount = out.npcCount;
    out.script.nodes = out.nodes;
    out.script.nodeCount = out.nodeCount;
    out.script.flags = out.flags;
    out.script.flagCount = out.flagCount;
    out.script.counters = out.counters;
    out.script.counterCount = out.counterCount;
    out.script.items = out.items;
    out.script.itemCount = out.itemCount;
    out.script.quests = out.quests;
    out.script.questCount = out.questCount;
    return true;
}

namespace {

bool positiveId(const char *s, int &id)
{
    if (!s || !*s) return false;
    unsigned n = 0;
    for (; *s && *s != ' ' && *s != '\t' && *s != '\r'; ++s) {
        if (*s < '0' || *s > '9' || n > (255u - (unsigned)(*s - '0')) / 10u) return false;
        n = n * 10u + (unsigned)(*s - '0');
    }
    if (*s || n == 0) return false;
    id = (int)n;
    return true;
}

bool sameId(const int *ids, int count, int id)
{
    for (int i = 0; i < count; ++i) if (ids[i] == id) return true;
    return false;
}

bool mapScopedTag(const char *tag)
{
    Sec sec;
    if (!secFromTag(tag, sec)) return false;
    return sec == MAP || sec == RULES || sec == TREASURES || sec == ENEMIES ||
           sec == RESOURCES || sec == DARK || sec == LEGEND || sec == NPC ||
           sec == PLATEGROUP || sec == GATEGROUP || sec == TIMEDGATE || sec == ANDGATE;
}

bool rowScopedSection(Sec sec)
{
    return sec == RULES || sec == TREASURES || sec == ENEMIES || sec == RESOURCES ||
           sec == DARK;
}

bool isDestinationTag(const char *tag) { return !strcmp(tag, "@destinations"); }
bool isPortalTag(const char *tag) { return !strcmp(tag, "@portals"); }

bool scanMapIds(const char *text, size_t len, int *ids, int &count, char *err, size_t errCap)
{
    count = 0;
    bool main = false;
    size_t pos = 0;
    int lineNo = 0;
    while (pos < len) {
        const size_t start = pos;
        while (pos < len && text[pos] != '\n') ++pos;
        const size_t n = pos - start;
        if (pos < len) ++pos;
        ++lineNo;
        if (n >= LINE_CAP) { setErr(err, errCap, lineNo, "line too long"); return false; }
        char line[LINE_CAP];
        memcpy(line, text + start, n);
        line[n] = 0;
        char *t = trimIn(line);
        if (strncmp(t, "@map", 4) || (t[4] && t[4] != ' ' && t[4] != '\t')) continue;
        char *arg = trimIn(t + 4);
        if (!*arg || arg[0] == '#') {
            if (main) { setErr(err, errCap, lineNo, "duplicate @map (main map)"); return false; }
            main = true;
            if (sameId(ids, count, 1)) { setErr(err, errCap, lineNo, "duplicate map id 1"); return false; }
            if (count == MAX_INTERNAL_MAPS) { setErr(err, errCap, lineNo, "too many internal maps (maximum 4)"); return false; }
            ids[count++] = 1;
            continue;
        }
        char *end = arg;
        while (*end && *end != ' ' && *end != '\t' && *end != '#') ++end;
        char saved = *end;
        *end = 0;
        int id;
        const bool ok = positiveId(arg, id);
        *end = saved;
        const char *tail = end;
        while (*tail == ' ' || *tail == '\t') ++tail;
        if (!ok || (*tail && *tail != '#')) {
            setErr(err, errCap, lineNo, "@map needs a positive id from 1 to 255"); return false;
        }
        if (id == 1) { setErr(err, errCap, lineNo, "main map id 1 uses @map without a number"); return false; }
        if (sameId(ids, count, id)) { setErr(err, errCap, lineNo, "duplicate map id"); return false; }
        if (count == MAX_INTERNAL_MAPS) { setErr(err, errCap, lineNo, "too many internal maps (maximum 4)"); return false; }
        ids[count++] = id;
    }
    if (!main) { setErr(err, errCap, 0, "missing main @map"); return false; }
    for (int i = 0; i < count; ++i) {
        if (ids[i] == 1) {
            const int first = ids[0];
            ids[0] = 1;
            ids[i] = first;
            break;
        }
    }
    return true;
}

bool appendText(char *out, size_t cap, size_t &used, const char *s, size_t n)
{
    if (n > cap - used) return false;
    memcpy(out + used, s, n);
    used += n;
    return true;
}

bool appendLine(char *out, size_t cap, size_t &used, const char *s)
{
    const size_t n = strlen(s);
    return appendText(out, cap, used, s, n) && appendText(out, cap, used, "\n", 1);
}

bool getMapSelector(char *s, int defaultMap, int &mapId, bool &hasSelector, char *err, size_t errCap, int lineNo)
{
    mapId = defaultMap;
    hasSelector = false;
    char *p = strstr(s, "map=");
    if (!p) return true;
    if (hasSelector || (p != s && p[-1] != ' ' && p[-1] != '\t')) {
        setErr(err, errCap, lineNo, "invalid map selector"); return false;
    }
    char *value = p + 4;
    unsigned id = 0;
    char *end = value;
    while (*end >= '0' && *end <= '9') {
        const unsigned digit = (unsigned)(*end - '0');
        if (id > (255u - digit) / 10u) { setErr(err, errCap, lineNo, "map selector out of range"); return false; }
        id = id * 10u + digit;
        ++end;
    }
    if (!id || (*end && *end != ' ' && *end != '\t' && *end != ':' && *end != '\r')) {
        setErr(err, errCap, lineNo, "map selector must be map=N (1..255)"); return false;
    }
    mapId = (int)id;
    hasSelector = true;
    char *remove = p;
    while (remove > s && (remove[-1] == ' ' || remove[-1] == '\t')) --remove;
    memmove(remove, end, strlen(end) + 1);
    if (strstr(s, "map=")) { setErr(err, errCap, lineNo, "duplicate map selector"); return false; }
    return true;
}

bool preprocessMap(const char *text, size_t len, int selectedMap, const int *ids, int mapCount,
                   char *out, size_t cap, size_t &outLen, char *err, size_t errCap)
{
    enum Special { ORDINARY, DESTS, PORTAL };
    Sec sec = NONE;
    Special special = ORDINARY;
    int currentMap = 1, sectionMap = 1;
    bool sectionSelected = true, npcSection = false;
    outLen = 0;
    size_t pos = 0;
    int lineNo = 0;
    while (pos < len) {
        const size_t start = pos;
        while (pos < len && text[pos] != '\n') ++pos;
        size_t n = pos - start;
        if (pos < len) ++pos;
        ++lineNo;
        if (n >= LINE_CAP) { setErr(err, errCap, lineNo, "line too long"); return false; }
        char line[LINE_CAP];
        memcpy(line, text + start, n);
        line[n] = 0;
        char tmp[LINE_CAP];
        copyStr(tmp, sizeof(tmp), line);
        char *trimmed = trimIn(tmp);

        if (trimmed[0] == '@') {
            char *space = strpbrk(trimmed, " \t");
            char *arg = nullptr;
            if (space) { *space = 0; arg = trimIn(space + 1); }
            const char *tag = trimmed;
            if (!strcmp(tag, "@shareditems")) {
                setErr(err, errCap, lineNo, "reserved internal loader directive");
                return false;
            }
            if (!strcmp(tag, "@map")) {
                int id = 1;
                if (arg && arg[0] && arg[0] != '#') {
                    char idText[8];
                    copyStr(idText, sizeof(idText), arg);
                    char *tail = strpbrk(idText, " \t#");
                    if (tail) {
                        const char *rest = tail;
                        while (*rest == ' ' || *rest == '\t') ++rest;
                        if (*rest && *rest != '#') {
                            setErr(err, errCap, lineNo, "@map takes one id"); return false;
                        }
                        *tail = 0;
                    }
                    if (!positiveId(idText, id)) { setErr(err, errCap, lineNo, "@map needs a positive id from 1 to 255"); return false; }
                }
                if (!sameId(ids, mapCount, id)) { setErr(err, errCap, lineNo, "map declaration was not validated"); return false; }
                currentMap = id;
                sec = MAP;
                special = ORDINARY;
                sectionMap = id;
                sectionSelected = (id == selectedMap);
                npcSection = false;
                if (sectionSelected && !appendLine(out, cap, outLen, "@map")) {
                    setErr(err, errCap, lineNo, "map preprocessing capacity exceeded"); return false;
                }
                continue;
            }

            if (isDestinationTag(tag) || isPortalTag(tag)) {
                special = isDestinationTag(tag) ? DESTS : PORTAL;
                sec = NONE;
                sectionSelected = false;
                continue;
            }
            Sec newSec;
            const bool known = secFromTag(tag, newSec);
            int headerMap = 1;
            bool headerHasMap = false;
            char headerArg[LINE_CAP] = {};
            if (arg) copyStr(headerArg, sizeof(headerArg), arg);
            if (known && strstr(headerArg, "map=")) {
                if (!mapScopedTag(tag)) {
                    setErr(err, errCap, lineNo, "map selector is not valid for this section"); return false;
                }
                if (!getMapSelector(headerArg, 1, headerMap, headerHasMap, err, errCap, lineNo)) return false;
                if (!sameId(ids, mapCount, headerMap)) { setErr(err, errCap, lineNo, "map selector references undeclared map"); return false; }
            } else if (known && mapScopedTag(tag)) {
                headerMap = 1;
            }
            if (!known) {
                // Let the production parser report unknown tags, including their line number.
                if (special == DESTS || special == PORTAL) continue;
                if (!appendLine(out, cap, outLen, line)) {
                    setErr(err, errCap, lineNo, "map preprocessing capacity exceeded"); return false;
                }
                sec = NONE;
                special = ORDINARY;
                sectionSelected = true;
                continue;
            }
            special = ORDINARY;
            sec = newSec;
            npcSection = (newSec == NPC);
            sectionMap = headerMap;
            sectionSelected = !mapScopedTag(tag) || headerMap == selectedMap || npcSection ||
                              rowScopedSection(newSec);
            if (!sectionSelected) continue;

            char normalized[LINE_CAP];
            size_t normalizedLen = 0;
            char mapText[3];
            int mapDigits = 0;
            int mapValue = headerMap;
            do { mapText[mapDigits++] = (char)('0' + mapValue % 10); mapValue /= 10; } while (mapValue);
            for (int i = 0; i < mapDigits / 2; ++i) {
                const char t = mapText[i];
                mapText[i] = mapText[mapDigits - 1 - i];
                mapText[mapDigits - 1 - i] = t;
            }
            const bool normalizedOk = appendText(normalized, sizeof(normalized) - 1, normalizedLen, tag, strlen(tag)) &&
                (!(arg && arg[0]) || (appendText(normalized, sizeof(normalized) - 1, normalizedLen, " ", 1) &&
                                       appendText(normalized, sizeof(normalized) - 1, normalizedLen,
                                                  headerArg, strlen(headerArg)))) &&
                (!npcSection || (appendText(normalized, sizeof(normalized) - 1, normalizedLen, " map=", 5) &&
                                 appendText(normalized, sizeof(normalized) - 1, normalizedLen,
                                            mapText, (size_t)mapDigits)));
            if (!normalizedOk) { setErr(err, errCap, lineNo, "section header too long"); return false; }
            normalized[normalizedLen] = 0;
            if (newSec == RESOURCES && headerMap != selectedMap) {
                if (!appendLine(out, cap, outLen, "@shareditems")) {
                    setErr(err, errCap, lineNo, "map preprocessing capacity exceeded"); return false;
                }
            } else if (!appendLine(out, cap, outLen, normalized)) {
                setErr(err, errCap, lineNo, "map preprocessing capacity exceeded"); return false;
            }
            continue;
        }

        if (special != ORDINARY) continue;
        if (sec == MAP) {
            if (currentMap == selectedMap &&
                !appendLine(out, cap, outLen, line)) {
                setErr(err, errCap, lineNo, "map preprocessing capacity exceeded"); return false;
            }
            continue;
        }
        if (!sectionSelected) continue;
        char *body = trimIn(line);
        int bodyMap = sectionMap;
        if (body[0] == 0 || body[0] == '#') {
            if (!appendLine(out, cap, outLen, line)) {
                setErr(err, errCap, lineNo, "map preprocessing capacity exceeded"); return false;
            }
            continue;
        }
        if (strstr(body, "map=")) {
            if (!mapScopedTag(sec == NPC ? "@npc" : sec == RULES ? "@rules" :
                              sec == TREASURES ? "@treasures" : sec == ENEMIES ? "@enemies" :
                              sec == RESOURCES ? "@resources" : sec == DARK ? "@dark" :
                              sec == LEGEND ? "@legend" : sec == PLATEGROUP ? "@plategroup" :
                              sec == GATEGROUP ? "@gategroup" : sec == TIMEDGATE ? "@timedgate" :
                              sec == ANDGATE ? "@andgate" : "")) {
                if (npcSection) { setErr(err, errCap, lineNo, "map selector belongs on @npc"); return false; }
                int rowMap;
                bool hasMap;
                if (!getMapSelector(body, 1, rowMap, hasMap, err, errCap, lineNo)) return false;
                if (!hasMap || !sameId(ids, mapCount, rowMap)) {
                    setErr(err, errCap, lineNo, "map selector references undeclared map"); return false;
                }
                bodyMap = rowMap;
                if (rowMap != selectedMap && sec != RESOURCES) continue;
            } else {
                setErr(err, errCap, lineNo, "map selector is not valid in this section"); return false;
            }
        }
        if (mapScopedTag(sec == NPC ? "@npc" : sec == RULES ? "@rules" :
                         sec == TREASURES ? "@treasures" : sec == ENEMIES ? "@enemies" :
                         sec == RESOURCES ? "@resources" : sec == DARK ? "@dark" :
                         sec == LEGEND ? "@legend" : sec == PLATEGROUP ? "@plategroup" :
                         sec == GATEGROUP ? "@gategroup" : sec == TIMEDGATE ? "@timedgate" :
                         sec == ANDGATE ? "@andgate" : "") &&
            sec != NPC && bodyMap != selectedMap) {
            if (sec != RESOURCES) continue;
        }
        if (sec == RESOURCES && bodyMap != selectedMap) {
            char resource[LINE_CAP];
            copyStr(resource, sizeof(resource), body);
            stripCommentIn(resource);
            char *field[6] = {};
            int count = 0;
            char *part = resource;
            while (count < 6) {
                char *colon = strchr(part, ':');
                if (colon) *colon = 0;
                field[count++] = trimIn(part);
                if (!colon) break;
                part = colon + 1;
            }
            if (count < 2 || !field[1][0]) {
                setErr(err, errCap, lineNo, "resource needs 'glyph : id : name.sv : name.en : max : image'"); return false;
            }
            const char *sv = count >= 3 && field[2][0] ? field[2] : field[1];
            const char *en = count >= 4 ? field[3] : "";
            const char *capText = count >= 5 && field[4][0] ? field[4] : "1";
            char shared[LINE_CAP];
            snprintf(shared, sizeof(shared), "%s : %s : %s : %s", field[1], sv, en, capText);
            if (!appendLine(out, cap, outLen, shared)) {
                setErr(err, errCap, lineNo, "map preprocessing capacity exceeded"); return false;
            }
            continue;
        }
        if (!appendLine(out, cap, outLen, body)) {
            setErr(err, errCap, lineNo, "map preprocessing capacity exceeded"); return false;
        }
    }
    return true;
}

bool validEntityName(const char *name)
{
    if (!name || !*name || strlen(name) >= DESTINATION_NAME_CAP) return false;
    for (const char *p = name; *p; ++p)
        if (!( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
               (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return false;
    return true;
}

uint32_t hashText(const char *s)
{
    uint32_t hash = 2166136261u;
    for (; *s; ++s) hash = (hash ^ (uint8_t)*s) * 16777619u;
    return hash;
}

bool validateGlobalResources(const char *text, size_t len, const int *mapIds, int mapCount,
                             char *err, size_t errCap)
{
    struct Identity {
        char id[40];
        uint32_t nameSv, nameEn, image;
        uint8_t cap;
    } identities[MAX_ITEMS] = {};
    int identityCount = 0, lineNo = 0, sectionMap = 1;
    bool resources = false;
    size_t pos = 0;
    while (pos < len) {
        const size_t start = pos;
        while (pos < len && text[pos] != '\n') ++pos;
        const size_t n = pos - start;
        if (pos < len) ++pos;
        ++lineNo;
        if (n >= LINE_CAP) { setErr(err, errCap, lineNo, "line too long"); return false; }
        char line[LINE_CAP];
        memcpy(line, text + start, n);
        line[n] = 0;
        char *t = trimIn(line);
        if (!t[0]) continue;
        if (t[0] == '@') {
            char *space = strpbrk(t, " \t");
            char *arg = nullptr;
            if (space) { *space = 0; arg = trimIn(space + 1); }
            resources = !strcmp(t, "@resources");
            sectionMap = 1;
            if (resources && arg && strstr(arg, "map=")) {
                bool found;
                if (!getMapSelector(arg, 1, sectionMap, found, err, errCap, lineNo) ||
                    !found || !sameId(mapIds, mapCount, sectionMap)) {
                    setErr(err, errCap, lineNo, "resource references undeclared map"); return false;
                }
            }
            continue;
        }
        if (!resources) continue;
        int map = sectionMap;
        if (!strncmp(t, "map=", 4)) {
            char *colon = strchr(t, ':');
            if (!colon) { setErr(err, errCap, lineNo, "resource row needs 'map=N:'"); return false; }
            *colon = 0;
            if (!positiveId(t + 4, map) || !sameId(mapIds, mapCount, map)) {
                setErr(err, errCap, lineNo, "resource references undeclared map"); return false;
            }
            t = trimIn(colon + 1);
        }
        stripCommentIn(t);
        if (!t[0]) continue;
        char *f[6] = {};
        int nf = 0;
        char *part = t;
        while (nf < 6) {
            char *colon = strchr(part, ':');
            if (colon) *colon = 0;
            f[nf++] = trimIn(part);
            if (!colon) break;
            part = colon + 1;
        }
        if (nf < 2 || strlen(f[0]) != 1 || !f[1][0] || strlen(f[1]) >= sizeof(identities[0].id)) {
            setErr(err, errCap, lineNo, "resource needs 'glyph : id : name.sv : name.en : max : image'"); return false;
        }
        const char *sv = nf >= 3 && f[2][0] ? f[2] : f[1];
        const char *en = nf >= 4 ? f[3] : "";
        unsigned cap = nf >= 5 && f[4][0] ? 0u : 1u;
        if (nf >= 5 && f[4][0]) {
            for (const char *p = f[4]; *p; ++p) {
                if (*p < '0' || *p > '9' || cap > (255u - (unsigned)(*p - '0')) / 10u) {
                    setErr(err, errCap, lineNo, "resource cap must be 1..255"); return false;
                }
                cap = cap * 10u + (unsigned)(*p - '0');
            }
            if (!cap) { setErr(err, errCap, lineNo, "resource cap must be 1..255"); return false; }
        }
        const char *image = nf >= 6 ? f[5] : "";
        const size_t imageLen = strlen(image);
        if (imageLen >= 40 || strstr(image, "..") || strpbrk(image, "/\\:") ||
            (image[0] && (imageLen < 5 || strcasecmp(image + imageLen - 4, ".png")))) {
            setErr(err, errCap, lineNo, "resource image must be a PNG filename (max 39 bytes)"); return false;
        }
        const uint32_t svHash = hashText(sv), enHash = hashText(en), imageHash = hashText(image);
        int found = -1;
        for (int i = 0; i < identityCount; ++i)
            if (!strcmp(identities[i].id, f[1])) { found = i; break; }
        if (found >= 0) {
            const Identity &identity = identities[found];
            if (identity.nameSv != svHash || identity.nameEn != enHash ||
                identity.image != imageHash || identity.cap != cap) {
                setErr(err, errCap, lineNo, "resource id has conflicting name, cap, or image"); return false;
            }
        } else {
            if (identityCount >= MAX_ITEMS) { setErr(err, errCap, lineNo, "too many global resource ids"); return false; }
            Identity &identity = identities[identityCount++];
            copyStr(identity.id, sizeof(identity.id), f[1]);
            identity.nameSv = svHash;
            identity.nameEn = enHash;
            identity.image = imageHash;
            identity.cap = (uint8_t)cap;
        }
        (void)map;
    }
    return true;
}

bool parseNamedLines(const char *text, size_t len, Loaded &out, const int *mapIds, int mapCount,
                     char *err, size_t errCap)
{
    size_t pos = 0;
    int lineNo = 0, section = 0, sectionMap = 1;
    while (pos < len) {
        const size_t start = pos;
        while (pos < len && text[pos] != '\n') ++pos;
        size_t n = pos - start;
        if (pos < len) ++pos;
        ++lineNo;
        if (n >= LINE_CAP) { setErr(err, errCap, lineNo, "line too long"); return false; }
        char line[LINE_CAP];
        memcpy(line, text + start, n);
        line[n] = 0;
        char *t = trimIn(line);
        if (!t[0]) continue;
        if (t[0] == '@') {
            char *space = strpbrk(t, " \t");
            char *arg = nullptr;
            if (space) { *space = 0; arg = trimIn(space + 1); }
            if (!strcmp(t, "@destinations")) section = 1;
            else if (!strcmp(t, "@portals")) section = 2;
            else { section = 0; continue; }
            sectionMap = 1;
            if (arg && strstr(arg, "map=")) {
                bool found;
                if (!getMapSelector(arg, 1, sectionMap, found, err, errCap, lineNo) ||
                    !found || !sameId(mapIds, mapCount, sectionMap)) {
                    setErr(err, errCap, lineNo, "invalid or undeclared map selector"); return false;
                }
            } else if (arg && arg[0]) {
                setErr(err, errCap, lineNo, "destination/portal section takes only map=N"); return false;
            }
            continue;
        }
        if (!section) continue;
        int map = sectionMap;
        if (!strncmp(t, "map=", 4)) {
            char *colon = strchr(t, ':');
            if (!colon) { setErr(err, errCap, lineNo, "map row needs 'map=N:'"); return false; }
            *colon = 0;
            if (!positiveId(t + 4, map) || !sameId(mapIds, mapCount, map)) {
                setErr(err, errCap, lineNo, "destination/portal references undeclared map"); return false;
            }
            t = trimIn(colon + 1);
        }
        stripCommentIn(t);
        if (!t[0]) continue;
        if (section == 1) {
            char *name = strtok(t, " \t");
            char *coordinate = strtok(nullptr, " \t");
            if (!validEntityName(name) || !coordinate || strtok(nullptr, " \t")) {
                setErr(err, errCap, lineNo, "destination needs 'name x,y'"); return false;
            }
            int x, y;
            if (!parseCoordinate(coordinate, x, y) || out.destinationCount >= MAX_DESTINATIONS) {
                setErr(err, errCap, lineNo, out.destinationCount >= MAX_DESTINATIONS ? "too many destinations (maximum 32)" : "invalid destination coordinate");
                return false;
            }
            for (int i = 0; i < out.destinationCount; ++i)
                if (!strcmp(out.destinations[i].name, name)) {
                    setErr(err, errCap, lineNo, "duplicate destination name"); return false;
                }
            Destination &d = out.destinations[out.destinationCount++];
            copyStr(d.name, sizeof(d.name), name);
            d.mapId = (uint8_t)map;
            d.x = (uint8_t)x;
            d.y = (uint8_t)y;
        } else {
            char *arrow = strstr(t, "->");
            if (!arrow) { setErr(err, errCap, lineNo, "portal needs 'x,y -> destination'"); return false; }
            *arrow = 0;
            char *source = trimIn(t);
            char *destination = trimIn(arrow + 2);
            if (!validEntityName(destination) || strtok(destination, " \t") == nullptr ||
                strtok(nullptr, " \t")) {
                setErr(err, errCap, lineNo, "portal needs one destination name"); return false;
            }
            int x, y;
            if (!parseCoordinate(source, x, y) || out.portalCount >= MAX_PORTALS) {
                setErr(err, errCap, lineNo, out.portalCount >= MAX_PORTALS ? "too many portals (maximum 32)" : "invalid portal source coordinate");
                return false;
            }
            for (int i = 0; i < out.portalCount; ++i)
                if (out.portals[i].mapId == map && out.portals[i].x == x && out.portals[i].y == y) {
                    setErr(err, errCap, lineNo, "duplicate portal source"); return false;
                }
            Portal &p = out.portals[out.portalCount++];
            p.mapId = (uint8_t)map;
            p.x = (uint8_t)x;
            p.y = (uint8_t)y;
            copyStr(p.destination, sizeof(p.destination), destination);
        }
    }
    for (int i = 0; i < out.portalCount; ++i) {
        bool found = false;
        for (int j = 0; j < out.destinationCount; ++j)
            if (!strcmp(out.portals[i].destination, out.destinations[j].name)) found = true;
        if (!found) { setErr(err, errCap, 0, "portal references missing destination"); return false; }
    }
    return true;
}

bool validateTravelEffects(const Loaded &out, char *err, size_t errCap)
{
    auto check = [&](const char *effects) {
        if (!effects) return true;
        const char *p = effects;
        while (*p) {
            while (*p == ' ' || *p == ';') ++p;
            const char *end = strchr(p, ';');
            const size_t n = end ? (size_t)(end - p) : strlen(p);
            if (n >= LINE_CAP) return false;
            char line[LINE_CAP];
            memcpy(line, p, n);
            line[n] = 0;
            char *word = strtok(line, " \t");
            char *verb = strtok(nullptr, " \t");
            char *name = strtok(nullptr, " \t");
            char *extra = strtok(nullptr, " \t");
            if (word && verb && name && !strcmp(word, "world") && !strcmp(verb, "travel")) {
                if (extra) return false;
                bool found = false;
                for (int i = 0; i < out.destinationCount; ++i)
                    if (!strcmp(name, out.destinations[i].name)) found = true;
                if (!found) return false;
            }
            p = end ? end + 1 : p + n;
        }
        return true;
    };
    for (int i = 0; i < out.nodeCount; ++i) {
        if (!check(out.nodes[i].effects)) { setErr(err, errCap, 0, "story travel references unknown destination"); return false; }
        for (int j = 0; j < Otr::Story::MAX_CHOICES; ++j)
            if (!check(out.nodes[i].choices[j].effects)) {
                setErr(err, errCap, 0, "story travel references unknown destination"); return false;
            }
    }
    return true;
}

} // namespace

bool parseMap(const char *text, size_t len, int internalMapId, Loaded &out, char *err, size_t errCap)
{
    if (!text || internalMapId < 1 || internalMapId > 255) {
        if (err && errCap) snprintf(err, errCap, "invalid map input or id");
        return false;
    }
    if (len > MAX_FILE) {
        if (err && errCap) snprintf(err, errCap, "file too large (maximum 64 KiB)");
        return false;
    }
    int ids[MAX_INTERNAL_MAPS] = {};
    int mapCount = 0;
    if (!scanMapIds(text, len, ids, mapCount, err, errCap)) return false;
    if (!sameId(ids, mapCount, internalMapId)) {
        if (err && errCap) snprintf(err, errCap, "requested internal map %d is not declared", internalMapId);
        return false;
    }
    void *memory = nullptr;
#if defined(ESP_PLATFORM)
    memory = heap_caps_malloc(MAX_FILE * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    memory = malloc(MAX_FILE * 2);
#endif
    if (!memory) {
        if (err && errCap) snprintf(err, errCap, "out of memory preprocessing maps");
        return false;
    }
    size_t preparedLen = 0;
    const bool prepared = preprocessMap(text, len, internalMapId, ids, mapCount,
                                        static_cast<char *>(memory), MAX_FILE * 2, preparedLen, err, errCap);
    bool ok = false;
    if (prepared) {
        ok = parseSingleMap(static_cast<const char *>(memory), preparedLen, internalMapId, out, err, errCap);
        if (ok) {
            out.internalMapId = internalMapId;
            out.internalMapCount = mapCount;
            memcpy(out.internalMapIds, ids, sizeof(ids));
            if (mapCount > 1) out.map.id = (uint8_t)internalMapId;
            if (!parseNamedLines(text, len, out, ids, mapCount, err, errCap) ||
                !validateGlobalResources(text, len, ids, mapCount, err, errCap) ||
                !validateTravelEffects(out, err, errCap)) ok = false;
        }
    }
    free(memory);
    return ok;
}

bool parse(const char *text, size_t len, Loaded &out, char *err, size_t errCap)
{
    return parseMap(text, len, 1, out, err, errCap);
}

bool parseFile(const char *path, Loaded &out, char *err, size_t errCap)
{
    FILE *f = fopen(path, "rb");
    if (!f) { if (err && errCap) snprintf(err, errCap, "cannot open %s", path); return false; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || (size_t)sz > MAX_FILE) { fclose(f); if (err && errCap) snprintf(err, errCap, "file too large"); return false; }
    char *buf = nullptr;
#if defined(ESP_PLATFORM)
    buf = (char *)heap_caps_malloc((size_t)sz + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (char *)heap_caps_malloc((size_t)sz + 1, MALLOC_CAP_8BIT);   // fallback: internal
#else
    buf = (char *)malloc((size_t)sz + 1);
#endif
    if (!buf) { fclose(f); if (err && errCap) snprintf(err, errCap, "out of memory"); return false; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = 0;
    const bool ok = parse(buf, n, out, err, errCap);
    free(buf);   // free() handles heap_caps_malloc pointers on ESP-IDF
    return ok;
}

bool parseMapFile(const char *path, int internalMapId, Loaded &out, char *err, size_t errCap)
{
    FILE *f = fopen(path, "rb");
    if (!f) { if (err && errCap) snprintf(err, errCap, "cannot open %s", path); return false; }
    fseek(f, 0, SEEK_END);
    const long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || (size_t)sz > MAX_FILE) { fclose(f); if (err && errCap) snprintf(err, errCap, "file too large"); return false; }
    char *buf = nullptr;
#if defined(ESP_PLATFORM)
    buf = (char *)heap_caps_malloc((size_t)sz + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (char *)heap_caps_malloc((size_t)sz + 1, MALLOC_CAP_8BIT);
#else
    buf = (char *)malloc((size_t)sz + 1);
#endif
    if (!buf) { fclose(f); if (err && errCap) snprintf(err, errCap, "out of memory"); return false; }
    const size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = 0;
    const bool ok = parseMap(buf, n, internalMapId, out, err, errCap);
    free(buf);
    return ok;
}

bool parseHeaderFile(const char *path, Header &out, char *err, size_t errCap)
{
    memset(&out, 0, sizeof(out));
    FILE *f = fopen(path, "rb");
    if (!f) { if (err && errCap) snprintf(err, errCap, "cannot open %s", path); return false; }
    char line[LINE_CAP];
    Sec sec = NONE;
    bool sawMap = false;
    while (fgets(line, sizeof(line), f)) {
        char *t = trimIn(line);
        if (t[0] == 0 || t[0] == '#') continue;
        if (t[0] == '@') {
            char *space = strchr(t, ' ');
            if (space) *space = 0;
            Sec ns;
            if (!secFromTag(t, ns)) { sec = NONE; continue; }
            sec = ns;
            if (sec == MAP) { sawMap = true; }
            continue;
        }
        // Once we have passed @map and are in a body section well past the header, stop early if
        // both title fields are known (headers always precede the map in practice).
        if (sawMap && out.titleSv[0] && sec != META && sec != ABOUT) break;
        if (sec == META) {
            char *k, *v;
            if (!splitKV(t, k, v)) continue;
            stripCommentIn(v);
            if (!strcmp(k, "id")) copyStr(out.id, sizeof(out.id), v);
            else if (!strcmp(k, "world")) out.world = atoi(v);
            else if (!strcmp(k, "map_id")) out.mapId = atoi(v);
            else if (!strcmp(k, "name.sv")) { if (!out.titleSv[0]) copyStr(out.titleSv, sizeof(out.titleSv), v); }
            else if (!strcmp(k, "name.en")) { if (!out.titleEn[0]) copyStr(out.titleEn, sizeof(out.titleEn), v); }
            else if (!strcmp(k, "desc.sv")) { if (!out.descSv[0]) copyStr(out.descSv, sizeof(out.descSv), v); }
            else if (!strcmp(k, "desc.en")) { if (!out.descEn[0]) copyStr(out.descEn, sizeof(out.descEn), v); }
        } else if (sec == ABOUT) {
            char *k, *v;
            if (!splitKV(t, k, v)) continue;
            stripCommentIn(v);
            if (!strcmp(k, "title.sv")) copyStr(out.titleSv, sizeof(out.titleSv), v);
            else if (!strcmp(k, "title.en")) copyStr(out.titleEn, sizeof(out.titleEn), v);
            else if (!strcmp(k, "desc.sv") || !strcmp(k, "description.sv")) copyStr(out.descSv, sizeof(out.descSv), v);
            else if (!strcmp(k, "desc.en") || !strcmp(k, "description.en")) copyStr(out.descEn, sizeof(out.descEn), v);
            else if (!strcmp(k, "author")) copyStr(out.author, sizeof(out.author), v);
            else if (!strcmp(k, "cover") || !strcmp(k, "cover.image")) copyStr(out.cover, sizeof(out.cover), v);
        }
    }
    fclose(f);
    return true;
}

}  // namespace Adv
