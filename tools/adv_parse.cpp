// adv_parse.cpp - implementation of the .adv importer (host only). See adv_parse.h.
#include "adv_parse.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {

using Otr::Story::Choice;
using Otr::Story::Named;
using Otr::Story::Node;
using Otr::Story::Npc;
using Otr::Story::Text;

std::string trim(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}

// Splits "<sv> | <en>" into its two halves (trimmed). Missing '|' -> en empty.
void splitBi(const std::string &s, std::string &sv, std::string &en)
{
    const size_t p = s.find(" | ");
    if (p == std::string::npos) {
        sv = trim(s);
        en.clear();
    } else {
        sv = trim(s.substr(0, p));
        en = trim(s.substr(p + 3));
    }
}

// Removes a trailing " # ..." inline comment (space-hash) and trims. Used for
// short config/token values; dialogue text lines are left untouched.
std::string stripComment(const std::string &s)
{
    const size_t p = s.find(" #");
    return trim(p == std::string::npos ? s : s.substr(0, p));
}

// "key: value" -> key / value. Returns false if there is no ':'.
bool splitKV(const std::string &line, std::string &key, std::string &val)
{
    const size_t p = line.find(':');
    if (p == std::string::npos) return false;
    key = trim(line.substr(0, p));
    val = trim(line.substr(p + 1));
    return true;
}

std::string toLower(std::string s)
{
    for (char &c : s)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return s;
}

// Splits "a, b c" / "a,b,c" on commas AND spaces into trimmed, lower-cased
// tokens (for @rules carry/reset lists). Empty tokens are dropped.
void splitTokens(const std::string &s, std::vector<std::string> &out)
{
    std::string cur;
    for (char c : s) {
        if (c == ',' || c == ' ' || c == '\t') {
            if (!cur.empty()) { out.push_back(toLower(cur)); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(toLower(cur));
}

// Parses "x0,y0 x1,y1" (exactly 4 integers; commas and/or spaces) into a DarkRect
// (see @dark). Returns false on the wrong count. Values are clipped to the map
// bounds later, in World::load().
bool parseDarkTokens(const std::string &s, Otr::DarkRect &out)
{
    std::vector<std::string> tok;
    splitTokens(s, tok);
    if (tok.size() != 4) return false;
    out.x0 = (uint8_t)atoi(tok[0].c_str());
    out.y0 = (uint8_t)atoi(tok[1].c_str());
    out.x1 = (uint8_t)atoi(tok[2].c_str());
    out.y1 = (uint8_t)atoi(tok[3].c_str());
    return true;
}

// Treasure / resource item name -> engine Obj. Only pickable items are allowed.
// Returns Obj::NONE for an unknown name.
Otr::Obj objFromName(const std::string &n)
{
    const std::string s = toLower(n);
    if (s == "key") return Otr::Obj::KEY;
    if (s == "gem") return Otr::Obj::GEM;
    if (s == "boat") return Otr::Obj::BOAT;
    if (s == "potion") return Otr::Obj::POTION;
    if (s == "heart") return Otr::Obj::HEART;
    if (s == "suit") return Otr::Obj::SUIT;
    if (s == "life") return Otr::Obj::LIFE;
    if (s == "bomb3") return Otr::Obj::BOMB3;
    if (s == "bomb4") return Otr::Obj::BOMB4;
    if (s == "flashlight" || s == "flash") return Otr::Obj::FLASH;   // v0.18.0
    if (s == "pogo" || s == "pogostick") return Otr::Obj::POGO;      // v0.18.0
    return Otr::Obj::NONE;
}

// True for a glyph the engine's decode() already understands (terrain, object or
// a person digit) - such glyphs may NOT be reused by @resources.
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

}  // namespace

bool parseAdvFile(const std::string &path, AdvDoc &doc, std::string &err)
{
    std::ifstream in(path);
    if (!in) {
        err = "cannot open " + path;
        return false;
    }

    Otr::Rules rules = {8, 20, Otr::LIVES_INFINITE, 3, 4, 3, 1, 2, 1, false, 0};
    std::vector<std::string> rawRows;

    enum Sec { NONE, META, ABOUT, RULES, MAP, TREASURES, RESOURCES, DARK, FLAGS, COUNTERS, ITEMS, QUESTS, NPC, NODE, LEGEND };
    Sec sec = NONE;

    // The npc / node currently being filled (indices into doc vectors, -1 = none).
    long curNpc = -1, curNode = -1;
    int nodeChoices = 0;

    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        lineNo++;
        // Inside @map every line is a map row until the next '@' section.
        if (sec == MAP) {
            const std::string t = trim(line);
            if (!t.empty() && t[0] == '@') {
                sec = NONE;  // fall through to section handling below
            } else if (t.empty() || t[0] == '#') {
                continue;  // blank / comment between @map and rows is skipped
            } else {
                rawRows.push_back(t);
                continue;
            }
        }

        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;

        if (t[0] == '@') {
            // New section header. Form: "@tag" or "@tag arg".
            std::string tag = t, arg;
            const size_t sp = t.find(' ');
            if (sp != std::string::npos) {
                tag = t.substr(0, sp);
                arg = trim(t.substr(sp + 1));
            }
            if (tag == "@meta") sec = META;
            else if (tag == "@about") sec = ABOUT;
            else if (tag == "@rules") sec = RULES;
            else if (tag == "@map") sec = MAP;
            else if (tag == "@treasures") sec = TREASURES;
            else if (tag == "@resources") sec = RESOURCES;
            else if (tag == "@dark") {
                // v0.18.0: "@dark x0,y0 x1,y1" (inline rect on the header) and/or one rect per
                // following body line. Over MAX_DARK rects are silently dropped (clipped later).
                sec = DARK;
                if (!arg.empty()) {
                    Otr::DarkRect dr{};
                    if (!parseDarkTokens(arg, dr)) {
                        err = "line " + std::to_string(lineNo) + ": @dark needs 'x0,y0 x1,y1'";
                        return false;
                    }
                    if ((int)doc.darks.size() < Otr::MAX_DARK) doc.darks.push_back(dr);
                }
            }
            else if (tag == "@flags") sec = FLAGS;
            else if (tag == "@counters") sec = COUNTERS;
            else if (tag == "@items") sec = ITEMS;
            else if (tag == "@quests") sec = QUESTS;
            else if (tag == "@legend") sec = LEGEND;
            else if (tag == "@npc") {
                sec = NPC;
                Npc n{};
                n.id = doc.intern(arg);
                n.look = 0;
                n.num = 0;
                doc.npcs.push_back(n);
                curNpc = (long)doc.npcs.size() - 1;
            } else if (tag == "@node") {
                sec = NODE;
                Node n{};
                n.id = doc.intern(arg);
                doc.nodes.push_back(n);
                curNode = (long)doc.nodes.size() - 1;
                nodeChoices = 0;
            } else {
                err = "line " + std::to_string(lineNo) + ": unknown section " + tag;
                return false;
            }
            continue;
        }

        // Body lines of the current section.
        switch (sec) {
            case META: {
                std::string k, v;
                if (!splitKV(t, k, v)) break;
                v = stripComment(v);
                if (k == "id") doc.id = v;
                else if (k == "world") doc.world = atoi(v.c_str());
                else if (k == "map_id") doc.mapId = atoi(v.c_str());
                // Legacy labels: @meta name.*/desc.* feed the menu @about fields
                // unless an explicit @about section overrides them later.
                else if (k == "name.sv") { if (doc.titleSv.empty()) doc.titleSv = v; }
                else if (k == "name.en") { if (doc.titleEn.empty()) doc.titleEn = v; }
                else if (k == "desc.sv") { if (doc.descSv.empty()) doc.descSv = v; }
                else if (k == "desc.en") { if (doc.descEn.empty()) doc.descEn = v; }
                break;
            }
            case ABOUT: {
                // What the SD adventure menu shows (A1). An explicit @about always
                // wins over the legacy @meta name.*/desc.* fallback.
                std::string k, v;
                if (!splitKV(t, k, v)) break;
                v = stripComment(v);
                if (k == "title.sv") doc.titleSv = v;
                else if (k == "title.en") doc.titleEn = v;
                else if (k == "desc.sv" || k == "description.sv") doc.descSv = v;
                else if (k == "desc.en" || k == "description.en") doc.descEn = v;
                else if (k == "author") doc.author = v;
                else if (k == "cover" || k == "cover.image") doc.cover = v;
                break;
            }
            case RULES: {
                std::string k, v;
                if (!splitKV(t, k, v)) break;
                v = stripComment(v);
                const int n = atoi(v.c_str());
                if (k == "start_energy") rules.startEnergy = (uint8_t)n;
                else if (k == "energy_cap") rules.energyCap = (uint8_t)n;
                else if (k == "lives") rules.lives = (v == "infinite") ? Otr::LIVES_INFINITE : (int8_t)n;
                else if (k == "lava_damage") rules.lavaDamage = (uint8_t)n;
                else if (k == "mine_damage") rules.mineDamage = (uint8_t)n;
                else if (k == "suit_base_steps") rules.suitBaseSteps = (uint8_t)n;
                else if (k == "suit_per_extra_max") rules.suitPerExtraMax = (uint8_t)n;
                else if (k == "potion_default") rules.potionDefault = (uint8_t)n;
                else if (k == "heart_default") rules.heartDefault = (uint8_t)n;
                else if (k == "fog") rules.fog = (v == "true" || v == "1");
                // True minesweeper activator: mine_level 1..12 (HUD / suit / bomb parity with the
                // built-in campaign). Alias minesweeper: true/yes/1 == mine_level 1. Implies fog.
                else if (k == "mine_level") {
                    if (n < 0 || n > Otr::MINE_LEVELS) {
                        err = "line " + std::to_string(lineNo) + ": mine_level must be 0.." +
                              std::to_string(Otr::MINE_LEVELS);
                        return false;
                    }
                    rules.mineLevel = (uint8_t)n;
                    if (rules.mineLevel) rules.fog = true;
                } else if (k == "minesweeper") {
                    if (v == "true" || v == "yes" || v == "1") { rules.mineLevel = 1; rules.fog = true; }
                }
                // A5: carry/reset policy between levels of the same adventure.
                else if (k == "carry") splitTokens(v, doc.carry);
                else if (k == "reset") splitTokens(v, doc.reset);
                break;
            }
            case FLAGS:
                doc.flags.push_back(doc.intern(stripComment(t)));
                break;
            case COUNTERS:
                doc.counters.push_back(doc.intern(stripComment(t)));
                break;
            case ITEMS:
            case QUESTS: {
                // id : sv : en
                const size_t p1 = t.find(':');
                if (p1 == std::string::npos) break;
                const size_t p2 = t.find(':', p1 + 1);
                Named nm{};
                nm.id = doc.intern(trim(t.substr(0, p1)));
                std::string sv, en;
                if (p2 == std::string::npos) {
                    sv = trim(t.substr(p1 + 1));
                } else {
                    sv = trim(t.substr(p1 + 1, p2 - p1 - 1));
                    en = trim(t.substr(p2 + 1));
                }
                nm.name.s[0] = doc.intern(sv);
                nm.name.s[1] = en.empty() ? nullptr : doc.intern(en);
                (sec == ITEMS ? doc.items : doc.quests).push_back(nm);
                break;
            }
            case TREASURES: {
                // "x y item [amount]" - a hidden item placed under a covering
                // object (or visible if nothing covers the cell).
                std::vector<std::string> tok;
                splitTokens(stripComment(t), tok);
                if (tok.size() < 3) {
                    err = "line " + std::to_string(lineNo) + ": treasure needs 'x y item [amount]'";
                    return false;
                }
                const Otr::Obj item = objFromName(tok[2]);
                if (item == Otr::Obj::NONE) {
                    err = "line " + std::to_string(lineNo) + ": unknown treasure item '" + tok[2] + "'";
                    return false;
                }
                Otr::Treasure tr{};
                tr.x = (uint8_t)atoi(tok[0].c_str());
                tr.y = (uint8_t)atoi(tok[1].c_str());
                tr.item = item;
                tr.amount = (uint8_t)(tok.size() >= 4 ? atoi(tok[3].c_str()) : 1);
                doc.treasures.push_back(tr);
                break;
            }
            case DARK: {
                // One dark rectangle per body line: "x0,y0 x1,y1". Over MAX_DARK rects are
                // silently dropped (consistent with the inline @dark form).
                Otr::DarkRect dr{};
                if (!parseDarkTokens(stripComment(t), dr)) {
                    err = "line " + std::to_string(lineNo) + ": @dark rect needs 'x0,y0 x1,y1'";
                    return false;
                }
                if ((int)doc.darks.size() < Otr::MAX_DARK) doc.darks.push_back(dr);
                break;
            }
            case RESOURCES: {
                // "glyph : id : name.sv : name.en : max : image" - binds a free map
                // glyph to a collectible that reuses the story item system (A4).
                std::vector<std::string> f;
                {
                    std::string cur;
                    const std::string s = stripComment(t);
                    for (char c : s) {
                        if (c == ':') { f.push_back(trim(cur)); cur.clear(); }
                        else cur.push_back(c);
                    }
                    f.push_back(trim(cur));
                }
                if (f.size() < 2 || f[0].size() != 1 || f[1].empty()) {
                    err = "line " + std::to_string(lineNo) +
                          ": resource needs 'glyph : id : name.sv : name.en : max : image'";
                    return false;
                }
                AdvResource r{};
                r.glyph = f[0][0];
                if (isBuiltinGlyph(r.glyph)) {
                    err = "line " + std::to_string(lineNo) + ": resource glyph '" +
                          std::string(1, r.glyph) + "' clashes with a built-in terrain/object/person glyph";
                    return false;
                }
                for (const AdvResource &o : doc.resources)
                    if (o.glyph == r.glyph) {
                        err = "line " + std::to_string(lineNo) + ": duplicate resource glyph '" +
                              std::string(1, r.glyph) + "'";
                        return false;
                    }
                r.id = f[1];
                r.nameSv = f.size() >= 3 ? f[2] : f[1];
                r.nameEn = f.size() >= 4 ? f[3] : "";
                r.cap = f.size() >= 5 && !f[4].empty() ? atoi(f[4].c_str()) : 1;
                if (r.cap < 1) r.cap = 1;
                if (r.cap > 255) r.cap = 255;
                r.image = f.size() >= 6 ? f[5] : "";
                doc.resources.push_back(r);
                // Reuse the item system: the resource IS a story item.
                Named nm{};
                nm.id = doc.intern(r.id);
                nm.name.s[0] = doc.intern(r.nameSv);
                nm.name.s[1] = r.nameEn.empty() ? nullptr : doc.intern(r.nameEn);
                doc.items.push_back(nm);
                break;
            }
            case NPC: {
                if (curNpc < 0) break;
                Npc &n = doc.npcs[(size_t)curNpc];
                std::string k, v;
                if (!splitKV(t, k, v)) break;
                v = stripComment(v);
                if (k == "name") {
                    std::string sv, en;
                    splitBi(v, sv, en);
                    n.name.s[0] = doc.intern(sv);
                    n.name.s[1] = en.empty() ? nullptr : doc.intern(en);
                } else if (k == "look") {
                    n.look = (uint8_t)atoi(v.c_str());
                } else if (k == "num") {
                    n.num = (uint8_t)atoi(v.c_str());
                }
                break;
            }
            case NODE: {
                if (curNode < 0) break;
                Node &n = doc.nodes[(size_t)curNode];
                std::string k, v;
                if (!splitKV(t, k, v)) break;
                if (k == "cond") n.cond = doc.intern(v);
                else if (k == "speaker") n.speaker = doc.intern(v);
                else if (k == "ref") n.ref = doc.intern(v);
                else if (k == "quote") n.quote = doc.intern(v);
                else if (k == "effects") n.effects = doc.intern(v);
                else if (k == "text.sv") n.text.s[0] = doc.intern(v);
                else if (k == "text.en") n.text.s[1] = doc.intern(v);
                else if (k == "choice") {
                    if (nodeChoices >= Otr::Story::MAX_CHOICES) {
                        err = "line " + std::to_string(lineNo) + ": more than " +
                              std::to_string(Otr::Story::MAX_CHOICES) + " choices";
                        return false;
                    }
                    // <sv> | <en> => <target> [?? cond] [!! effects] [~~ hintsv | hinten]
                    const size_t arrow = v.find(" => ");
                    if (arrow == std::string::npos) {
                        err = "line " + std::to_string(lineNo) + ": choice missing '=>'";
                        return false;
                    }
                    std::string texts = v.substr(0, arrow);
                    std::string rest = v.substr(arrow + 4);
                    // Locate optional markers in `rest`.
                    const size_t pc = rest.find(" ?? ");
                    const size_t pe = rest.find(" !! ");
                    const size_t ph = rest.find(" ~~ ");
                    size_t endTarget = rest.size();
                    for (size_t m : {pc, pe, ph})
                        if (m != std::string::npos && m < endTarget) endTarget = m;
                    const std::string target = trim(rest.substr(0, endTarget));

                    auto segment = [&](size_t start) -> std::string {
                        // content from `start` (just after a 4-char marker) up to the next marker
                        size_t stop = rest.size();
                        for (size_t m : {pc, pe, ph})
                            if (m != std::string::npos && m >= start && m < stop) stop = m;
                        return trim(rest.substr(start, stop - start));
                    };

                    Choice &c = n.choices[nodeChoices];
                    std::string sv, en;
                    splitBi(trim(texts), sv, en);
                    c.text.s[0] = doc.intern(sv);
                    c.text.s[1] = en.empty() ? nullptr : doc.intern(en);
                    c.next = doc.intern(target);
                    if (pc != std::string::npos) c.cond = doc.intern(segment(pc + 4));
                    if (pe != std::string::npos) c.effects = doc.intern(segment(pe + 4));
                    if (ph != std::string::npos) {
                        std::string hsv, hen;
                        splitBi(segment(ph + 4), hsv, hen);
                        c.hint.s[0] = doc.intern(hsv);
                        c.hint.s[1] = hen.empty() ? nullptr : doc.intern(hen);
                    }
                    nodeChoices++;
                }
                break;
            }
            default:
                break;  // META already handled; LEGEND ignored; NONE skips
        }
    }

    // --- Build the MapDef rows ---------------------------------------------------------------
    if (rawRows.empty()) {
        err = "no @map rows";
        return false;
    }
    const size_t w = rawRows[0].size();
    for (size_t i = 0; i < rawRows.size(); i++) {
        if (rawRows[i].size() != w) {
            err = "map row " + std::to_string(i) + " width " + std::to_string(rawRows[i].size()) +
                  " != " + std::to_string(w);
            return false;
        }
        // Resource glyphs are not understood by the engine's decode(); record each
        // placement and translate the cell to grass so World::load() accepts the
        // map. The device overlays and picks up resources from doc.resources.
        std::string row = rawRows[i];
        for (size_t x = 0; x < row.size(); x++) {
            for (const AdvResource &r : doc.resources) {
                if (row[x] == r.glyph) {
                    AdvResourcePlacement pl{};
                    pl.glyph = r.glyph;
                    pl.x = (uint8_t)x;
                    pl.y = (uint8_t)i;
                    doc.resourcePlacements.push_back(pl);
                    row[x] = '.';
                    break;
                }
            }
        }
        doc.mapRows.push_back(doc.intern(row));
    }
    doc.map.id = (uint8_t)doc.mapId;
    doc.map.rows = doc.mapRows.data();
    doc.map.h = (int)doc.mapRows.size();
    doc.map.rules = rules;
    doc.map.treasures = doc.treasures.empty() ? nullptr : doc.treasures.data();
    doc.map.treasureCount = (int)doc.treasures.size();
    doc.map.darks = doc.darks.empty() ? nullptr : doc.darks.data();   // v0.18.0
    doc.map.darkCount = (int)doc.darks.size();

    // --- Build the Script --------------------------------------------------------------------
    doc.script.npcs = doc.npcs.data();
    doc.script.npcCount = (int)doc.npcs.size();
    doc.script.nodes = doc.nodes.data();
    doc.script.nodeCount = (int)doc.nodes.size();
    doc.script.flags = doc.flags.data();
    doc.script.flagCount = (int)doc.flags.size();
    doc.script.counters = doc.counters.data();
    doc.script.counterCount = (int)doc.counters.size();
    doc.script.items = doc.items.data();
    doc.script.itemCount = (int)doc.items.size();
    doc.script.quests = doc.quests.data();
    doc.script.questCount = (int)doc.quests.size();
    return true;
}
