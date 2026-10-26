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

}  // namespace

bool parseAdvFile(const std::string &path, AdvDoc &doc, std::string &err)
{
    std::ifstream in(path);
    if (!in) {
        err = "cannot open " + path;
        return false;
    }

    Otr::Rules rules = {8, 20, Otr::LIVES_INFINITE, 3, 4, 3, 1, 2, 1, false};
    std::vector<std::string> rawRows;

    enum Sec { NONE, META, RULES, MAP, FLAGS, COUNTERS, ITEMS, QUESTS, NPC, NODE, LEGEND };
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
            else if (tag == "@rules") sec = RULES;
            else if (tag == "@map") sec = MAP;
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
                // name.*/desc.* are human labels (held in lang on-device); ignored here.
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
        doc.mapRows.push_back(doc.intern(rawRows[i]));
    }
    doc.map.id = (uint8_t)doc.mapId;
    doc.map.rows = doc.mapRows.data();
    doc.map.h = (int)doc.mapRows.size();
    doc.map.rules = rules;
    doc.map.treasures = nullptr;
    doc.map.treasureCount = 0;

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
