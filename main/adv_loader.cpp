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

uint32_t maskFromTokens(const char *s)
{
    uint32_t m = 0;
    char buf[32];
    size_t bi = 0;
    auto flush = [&]() {
        if (!bi) return;
        buf[bi] = 0;
        for (size_t i = 0; i < bi; i++)
            if (buf[i] >= 'A' && buf[i] <= 'Z') buf[i] = (char)(buf[i] - 'A' + 'a');
        if (!strcmp(buf, "energy")) m |= CARRY_ENERGY;
        else if (!strcmp(buf, "maxenergy") || !strcmp(buf, "max_energy") || !strcmp(buf, "maxhp")) m |= CARRY_MAXENERGY;
        else if (!strcmp(buf, "items") || !strcmp(buf, "inventory")) m |= CARRY_ITEMS;
        else if (!strcmp(buf, "lives") || !strcmp(buf, "life")) m |= CARRY_LIVES;
        else if (!strcmp(buf, "suit")) m |= CARRY_SUIT;
        else if (!strcmp(buf, "keys") || !strcmp(buf, "key")) m |= CARRY_KEYS;
        else if (!strcmp(buf, "gems") || !strcmp(buf, "gem")) m |= CARRY_GEMS;
        else if (!strcmp(buf, "boat")) m |= CARRY_BOAT;
        bi = 0;
    };
    for (const char *p = s; *p; p++) {
        char c = *p;
        if (c == ',' || c == ' ' || c == '\t') flush();
        else if (bi < sizeof(buf) - 1) buf[bi++] = c;
    }
    flush();
    return m;
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
    Otr::Tile tile;
    uint8_t group;
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

bool parseLegendLine(char *line, LegendDef *defs, int &count, int lineNo, char *err, size_t errCap)
{
    stripCommentIn(line);
    char *equals = strchr(line, '=');
    if (!equals) { setErr(err, errCap, lineNo, "legend needs 'glyph = plate|gate group'"); return false; }
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
    char *groupText = strtok(nullptr, " \t");
    if (!kind || strtok(nullptr, " \t")) {
        setErr(err, errCap, lineNo, "legend needs 'glyph = plate|gate group'");
        return false;
    }
    Otr::Tile tile;
    if (sameWord(kind, "plate")) tile = Otr::Tile::PLATE;
    else if (sameWord(kind, "gate")) tile = Otr::Tile::GATE;
    else { setErr(err, errCap, lineNo, "legend supports only plate and gate"); return false; }
    uint8_t group = 0;
    if (groupText && !groupFromText(groupText, group)) {
        setErr(err, errCap, lineNo, "invalid plate/gate group");
        return false;
    }
    defs[count++] = {glyph, tile, group};
    return true;
}

enum Sec { NONE, META, ABOUT, RULES, MAP, TREASURES, RESOURCES, DARK, FLAGS, COUNTERS, ITEMS, QUESTS, NPC, NODE, LEGEND,
           PLATEGROUP, GATEGROUP };

// Maps a "@tag" (without any arg) to a section. Returns false for an unknown tag.
bool secFromTag(const char *tag, Sec &sec)
{
    if (!strcmp(tag, "@meta")) sec = META;
    else if (!strcmp(tag, "@about")) sec = ABOUT;
    else if (!strcmp(tag, "@rules")) sec = RULES;
    else if (!strcmp(tag, "@map")) sec = MAP;
    else if (!strcmp(tag, "@treasures")) sec = TREASURES;
    else if (!strcmp(tag, "@resources")) sec = RESOURCES;
    else if (!strcmp(tag, "@dark")) sec = DARK;
    else if (!strcmp(tag, "@flags")) sec = FLAGS;
    else if (!strcmp(tag, "@counters")) sec = COUNTERS;
    else if (!strcmp(tag, "@items")) sec = ITEMS;
    else if (!strcmp(tag, "@quests")) sec = QUESTS;
    else if (!strcmp(tag, "@legend")) sec = LEGEND;
    else if (!strcmp(tag, "@plategroup")) sec = PLATEGROUP;
    else if (!strcmp(tag, "@gategroup")) sec = GATEGROUP;
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
        const bool isPlate = glyph >= Otr::GROUPED_PLATE_GLYPH && glyph < Otr::GROUPED_GATE_GLYPH
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
        out.rows[y][x] = (char)((plates ? Otr::GROUPED_PLATE_GLYPH : Otr::GROUPED_GATE_GLYPH) + group);
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
    out.x0 = (uint8_t)atoi(tok[0]);
    out.y0 = (uint8_t)atoi(tok[1]);
    out.x1 = (uint8_t)atoi(tok[2]);
    out.y1 = (uint8_t)atoi(tok[3]);
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
    memset(this, 0, sizeof(*this));
}

bool parse(const char *text, size_t len, Loaded &out, char *err, size_t errCap)
{
    out.reset();
    Otr::Rules rules = {8, 20, (int8_t)Otr::LIVES_INFINITE, 3, 4, 3, 1, 2, 1, false, 0};

    Sec sec = NONE;
    long curNpc = -1, curNode = -1;
    int nodeChoices = 0;
    bool oom = false;
    LegendDef legend[MAX_LEGEND];
    int legendCount = 0;

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
            if (ns == PLATEGROUP || ns == GATEGROUP) {
                sec = NONE;
                continue;
            }
            if (ns == DARK && arg && arg[0]) {
                // v0.18.0: inline rect on the header ("@dark x0,y0 x1,y1"). More rects may follow as
                // body lines. Over MAX_DARK rects are silently dropped (clipped later in World::load).
                Otr::DarkRect dr{};
                if (!parseDarkTokens(arg, dr)) { setErr(err, errCap, lineNo, "@dark needs 'x0,y0 x1,y1'"); return false; }
                if (out.darkCount < Otr::MAX_DARK) out.darks[out.darkCount++] = dr;
            }
            if (ns == NPC) {
                if (out.npcCount >= MAX_NPCS) { setErr(err, errCap, lineNo, "too many npcs"); return false; }
                Npc &n = out.npcs[out.npcCount];
                n = Npc{};
                n.id = IN(arg ? arg : "");
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
                if (!splitKV(t, k, v)) break;
                stripCommentIn(v);
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
                else if (!strcmp(k, "fog")) rules.fog = (!strcmp(v, "true") || !strcmp(v, "1"));
                // True minesweeper activator: mine_level 1..12. Alias minesweeper: true/yes/1 == 1.
                // Implies fog (also forced on in World::load). Must stay in sync with tools/adv_parse.cpp.
                else if (!strcmp(k, "mine_level")) {
                    if (n < 0 || n > Otr::MINE_LEVELS) { setErr(err, errCap, lineNo, "mine_level must be 0..12"); return false; }
                    rules.mineLevel = (uint8_t)n;
                    if (rules.mineLevel) rules.fog = true;
                } else if (!strcmp(k, "minesweeper")) {
                    if (!strcmp(v, "true") || !strcmp(v, "yes") || !strcmp(v, "1")) { rules.mineLevel = 1; rules.fog = true; }
                }
                else if (!strcmp(k, "carry")) out.carryMask = maskFromTokens(v);
                else if (!strcmp(k, "reset")) out.resetMask = maskFromTokens(v);
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
            case QUESTS: {
                // id : sv : en
                char *c1 = strchr(t, ':');
                if (!c1) break;
                *c1 = 0;
                char *idp = trimIn(t);
                char *rest = c1 + 1;
                char *c2 = strchr(rest, ':');
                char *sv, *en;
                if (!c2) { sv = trimIn(rest); en = rest + strlen(rest); }
                else { *c2 = 0; sv = trimIn(rest); en = trimIn(c2 + 1); }
                const bool isItems = (sec == ITEMS);
                int &cnt = isItems ? out.itemCount : out.questCount;
                const int cap = isItems ? MAX_ITEMS : MAX_QUESTS;
                if (cnt >= cap) { setErr(err, errCap, lineNo, isItems ? "too many items" : "too many quests"); return false; }
                Named &nm = (isItems ? out.items : out.quests)[cnt];
                nm = Named{};
                nm.id = IN(idp);
                nm.name.s[0] = IN(sv);
                nm.name.s[1] = en[0] ? IN(en) : nullptr;
                cnt++;
                break;
            }
            case TREASURES: {
                stripCommentIn(t);
                // "x y item [amount]"
                char buf[LINE_CAP];
                copyStr(buf, sizeof(buf), t);
                char *tok[4] = {nullptr, nullptr, nullptr, nullptr};
                int nt = 0;
                for (char *p = strtok(buf, " ,\t"); p && nt < 4; p = strtok(nullptr, " ,\t")) tok[nt++] = p;
                if (nt < 3) { setErr(err, errCap, lineNo, "treasure needs 'x y item [amount]'"); return false; }
                const Otr::Obj item = objFromName(tok[2]);
                if (item == Otr::Obj::NONE) { setErr(err, errCap, lineNo, "unknown treasure item"); return false; }
                if (out.treasureCount >= MAX_TREASURES) { setErr(err, errCap, lineNo, "too many treasures"); return false; }
                Otr::Treasure &tr = out.treasures[out.treasureCount++];
                tr = Otr::Treasure{};
                tr.x = (uint8_t)atoi(tok[0]);
                tr.y = (uint8_t)atoi(tok[1]);
                tr.item = item;
                tr.amount = (uint8_t)(nt >= 4 ? atoi(tok[3]) : 1);
                break;
            }
            case DARK: {
                // One dark rectangle per body line: "x0,y0 x1,y1". Over MAX_DARK rects are silently
                // dropped (consistent with the inline @dark form).
                stripCommentIn(t);
                if (!t[0]) break;
                Otr::DarkRect dr{};
                if (!parseDarkTokens(t, dr)) { setErr(err, errCap, lineNo, "@dark rect needs 'x0,y0 x1,y1'"); return false; }
                if (out.darkCount < Otr::MAX_DARK) out.darks[out.darkCount++] = dr;
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
                if (isBuiltinGlyph(g)) { setErr(err, errCap, lineNo, "resource glyph clashes with a built-in glyph"); return false; }
                for (int i = 0; i < out.resourceCount; i++)
                    if (out.resources[i].glyph == g) { setErr(err, errCap, lineNo, "duplicate resource glyph"); return false; }
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
                copyStr(r.image, sizeof(r.image), nf >= 6 ? f[5] : "");
                // The resource IS a story item (so has:/give/take validate).
                if (out.itemCount >= MAX_ITEMS) { setErr(err, errCap, lineNo, "too many items (resource)"); return false; }
                Named &nm = out.items[out.itemCount++];
                nm = Named{};
                nm.id = IN(f[1]);
                nm.name.s[0] = IN(nsv);
                nm.name.s[1] = nen ? IN(nen) : nullptr;
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
                    out.rows[y][x] = (char)((legend[i].tile == Otr::Tile::PLATE
                                                  ? Otr::GROUPED_PLATE_GLYPH
                                                  : Otr::GROUPED_GATE_GLYPH) +
                                             legend[i].group);
                    break;
                }
    if (!applyGroupDirectives(text, len, out, err, errCap)) return false;

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
                    out.rows[i][x] = '.';
                    break;
                }
            }
        }
        out.rowPtrs[i] = out.rows[i];
    }
    out.map.id = (uint8_t)out.mapId;
    out.map.rows = out.rowPtrs;
    out.map.h = out.rowCount;
    out.map.rules = rules;
    out.map.treasures = out.treasureCount ? out.treasures : nullptr;
    out.map.treasureCount = out.treasureCount;
    out.map.darks = out.darkCount ? out.darks : nullptr;   // v0.18.0
    out.map.darkCount = out.darkCount;

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
