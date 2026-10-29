#include "otr_story.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace Otr {
namespace Story {

namespace {

// A (pointer, length) view into a constant manuscript string.
struct Span {
    const char *p;
    size_t n;
    bool eq(const char *s) const { return strlen(s) == n && memcmp(p, s, n) == 0; }
    bool starts(const char *s) const { return strlen(s) <= n && memcmp(p, s, strlen(s)) == 0; }
};

bool isSpace(char c) { return c == ' ' || c == '\t'; }
bool isName(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.'; }

Span trim(Span s)
{
    while (s.n && isSpace(*s.p)) { s.p++; s.n--; }
    while (s.n && isSpace(s.p[s.n - 1])) s.n--;
    return s;
}

// Splits off the part before `sep` (or everything). Returns false when s is exhausted.
bool nextPart(Span &rest, char sep, Span &part)
{
    if (!rest.p || !rest.n) return false;
    const char *e = (const char *)memchr(rest.p, sep, rest.n);
    const size_t len = e ? (size_t)(e - rest.p) : rest.n;
    part = trim(Span{rest.p, len});
    rest = e ? Span{e + 1, rest.n - len - 1} : Span{rest.p + rest.n, 0};
    if (!e && !part.n) return false;
    return true;
}

bool validName(Span s)
{
    if (!s.n) return false;
    for (size_t i = 0; i < s.n; i++)
        if (!isName(s.p[i])) return false;
    return true;
}

bool parseInt(Span s, int &out)
{
    s = trim(s);
    if (!s.n) return false;
    size_t i = 0;
    bool neg = false;
    if (s.p[0] == '-' || s.p[0] == '+') { neg = s.p[0] == '-'; i = 1; }
    if (i >= s.n) return false;
    long v = 0;
    for (; i < s.n; i++) {
        if (s.p[i] < '0' || s.p[i] > '9') return false;
        v = v * 10 + (s.p[i] - '0');
        if (v > 1000000) return false;
    }
    out = (int)(neg ? -v : v);
    return true;
}

bool parseQuest(Span s, Quest &q)
{
    if (s.eq("none")) q = Quest::NONE;
    else if (s.eq("active")) q = Quest::ACTIVE;
    else if (s.eq("done")) q = Quest::DONE;
    else return false;
    return true;
}

int16_t clampCounter(long v)
{
    return (int16_t)(v > COUNTER_LIMIT ? COUNTER_LIMIT : v < -COUNTER_LIMIT ? -COUNTER_LIMIT : v);
}

// Error collector for validate().
struct Errors {
    char *buf;
    size_t cap, len;
    int count;
    void add(const char *fmt, ...)
    {
        count++;
        if (!buf || len + 1 >= cap) return;
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(buf + len, cap - len, fmt, ap);
        va_end(ap);
        if (n < 0) return;
        len += (size_t)n < cap - len ? (size_t)n : cap - len - 1;
        if (len + 1 < cap) {
            buf[len++] = '\n';
            buf[len] = 0;
        }
    }
};

// One condition term. st == nullptr: syntax / name check only (result true).
bool evalTerm(const Script &sc, const StoryState *st, Span t, bool &result)
{
    bool neg = false;
    if (t.n && t.p[0] == '!') { neg = true; t = trim(Span{t.p + 1, t.n - 1}); }
    bool v = true;
    if (t.starts("has:")) {
        const Span name{t.p + 4, t.n - 4};
        const int id = sc.itemId(name.p, name.n);
        if (id < 0) return false;
        if (st) v = st->items[id] > 0;
    } else if (t.starts("quest:")) {
        const Span body{t.p + 6, t.n - 6};
        const char *eq = (const char *)memchr(body.p, '=', body.n);
        if (!eq) return false;
        const int id = sc.questId(body.p, (size_t)(eq - body.p));
        Quest q;
        if (id < 0 || !parseQuest(trim(Span{eq + 1, body.n - (size_t)(eq - body.p) - 1}), q)) return false;
        if (st) v = st->quests[id] == (uint8_t)q;
    } else {
        // counter>=N, counter<=N, counter=N or a flag
        size_t i = 0;
        while (i < t.n && isName(t.p[i])) i++;
        if (i == t.n) {
            const int id = sc.flagId(t.p, t.n);
            if (id < 0) return false;
            if (st) v = st->flag(id);
        } else {
            const int id = sc.counterId(t.p, i);
            if (id < 0) return false;
            Span op{t.p + i, t.n - i};
            int kind;   // 0 >=, 1 <=, 2 =
            size_t skip;
            if (op.starts(">=")) { kind = 0; skip = 2; }
            else if (op.starts("<=")) { kind = 1; skip = 2; }
            else if (op.starts("=")) { kind = 2; skip = 1; }
            else return false;
            int n;
            if (!parseInt(Span{op.p + skip, op.n - skip}, n)) return false;
            if (st) {
                const int c = st->counters[id];
                v = kind == 0 ? c >= n : kind == 1 ? c <= n : c == n;
            }
        }
    }
    result = neg ? !v : v;
    return true;
}

bool checkCond(const Script &sc, const StoryState *st, const char *cond, bool &result)
{
    result = true;
    if (!cond) return true;
    Span rest{cond, strlen(cond)}, term;
    bool any = false;
    while (nextPart(rest, '&', term)) {
        if (!term.n) return false;   // "a & & b"
        bool v;
        if (!evalTerm(sc, st, term, v)) return false;
        result = result && v;
        any = true;
    }
    return any || !*cond;
}

void addNote(Note *notes, int *count, int cap, NoteKind k, int id, int amount)
{
    if (!notes || !count || *count >= cap) return;
    notes[(*count)++] = Note{k, (uint8_t)id, (int16_t)amount};
}

// Runs (st != nullptr) or checks (st == nullptr) the effects.
bool runEffects(const Script &sc, StoryState *st, const char *effects, Host *host, Note *notes, int *noteCount, int noteCap)
{
    if (!effects) return true;
    Span rest{effects, strlen(effects)}, eff;
    while (nextPart(rest, ';', eff)) {
        if (!eff.n) return false;
        Span w[6];
        int wc = 0;
        Span r = eff, word;
        while (nextPart(r, ' ', word)) {
            if (!word.n) continue;   // Double spaces
            if (wc == 6) return false;
            w[wc++] = word;
        }
        if (!wc) return false;
        const Span verb = w[0];
        if (verb.eq("set") || verb.eq("clear")) {
            if (wc != 2) return false;
            const int id = sc.flagId(w[1].p, w[1].n);
            if (id < 0) return false;
            if (st) st->setFlag(id, verb.eq("set"));
        } else if (verb.eq("add") || verb.eq("cnt")) {
            int n;
            if (wc != 3 || !parseInt(w[2], n)) return false;
            const int id = sc.counterId(w[1].p, w[1].n);
            if (id < 0) return false;
            if (st) st->counters[id] = clampCounter(verb.eq("add") ? (long)st->counters[id] + n : n);
        } else if (verb.eq("give") || verb.eq("take")) {
            int n = 1;
            if (wc < 2 || wc > 3 || (wc == 3 && (!parseInt(w[2], n) || n < 1 || n > 255))) return false;
            const int id = sc.itemId(w[1].p, w[1].n);
            if (id < 0) return false;
            if (st) {
                const int old = st->items[id];
                const int cap = sc.items[id].cap;
                const int now = verb.eq("give") ? (old + n > cap ? cap : old + n) : (old - n < 0 ? 0 : old - n);
                st->items[id] = (uint8_t)now;
                if (now != old) addNote(notes, noteCount, noteCap, now > old ? NoteKind::ITEM_GOT : NoteKind::ITEM_LOST, id, now > old ? now - old : old - now);
            }
        } else if (verb.eq("quest")) {
            Quest q;
            if (wc != 3 || !parseQuest(w[2], q)) return false;
            const int id = sc.questId(w[1].p, w[1].n);
            if (id < 0) return false;
            if (st && st->quests[id] != (uint8_t)q) {
                st->quests[id] = (uint8_t)q;
                if (q == Quest::ACTIVE) addNote(notes, noteCount, noteCap, NoteKind::QUEST_STARTED, id, 0);
                if (q == Quest::DONE) addNote(notes, noteCount, noteCap, NoteKind::QUEST_DONE, id, 0);
            }
        } else if (verb.eq("world")) {
            if (wc < 2 || !validName(w[1]) || w[1].n >= 16) return false;
            if (w[1].eq("travel")) {
                if (wc != 3 || !validName(w[2]) || w[2].n >= 32) return false;
                if (st && host) {
                    char destination[32];
                    memcpy(destination, w[2].p, w[2].n);
                    destination[w[2].n] = 0;
                    host->worldTravel(destination);
                }
                continue;
            }
            int argv[3] = {0, 0, 0};
            int mapId = 1, argc = 0;
            bool hasMap = false;
            for (int i = 2; i < wc; i++) {
                if (w[i].n > 4 && !memcmp(w[i].p, "map=", 4)) {
                    if (hasMap || i != 2 || w[i].n > 7) return false;
                    int parsed;
                    if (!parseInt(Span{w[i].p + 4, w[i].n - 4}, parsed) || parsed < 1 || parsed > 255)
                        return false;
                    mapId = parsed;
                    hasMap = true;
                } else {
                    if (argc == 3 || !parseInt(w[i], argv[argc])) return false;
                    argc++;
                }
            }
            if (hasMap && !w[1].eq("open") && !w[1].eq("npc_move") && !w[1].eq("npc_remove"))
                return false;
            if (st && host) {
                char v[16];
                memcpy(v, w[1].p, w[1].n);
                v[w[1].n] = 0;
                if (hasMap) host->worldActionOnMap(v, mapId, argc, argv);
                else host->worldAction(v, argc, argv);
            }
        } else if (verb.eq("sound")) {
            if (wc != 2 || !validName(w[1]) || w[1].n >= 24) return false;
            if (st && host) {
                char v[24];
                memcpy(v, w[1].p, w[1].n);
                v[w[1].n] = 0;
                host->sound(v);
            }
        } else {
            return false;
        }
    }
    return true;
}

int findName(const char *const *names, int count, const char *name, size_t len)
{
    for (int i = 0; i < count; i++)
        if (strlen(names[i]) == len && memcmp(names[i], name, len) == 0) return i;
    return -1;
}

int findNamed(const Named *names, int count, const char *name, size_t len)
{
    for (int i = 0; i < count; i++)
        if (strlen(names[i].id) == len && memcmp(names[i].id, name, len) == 0) return i;
    return -1;
}

uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t crc = ~0u;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool isEnd(const char *id) { return id && strcmp(id, "END") == 0; }

}  // namespace

int utf8Len(const char *s)
{
    int n = 0;
    for (; s && *s; s++)
        if (((uint8_t)*s & 0xC0) != 0x80) n++;
    return n;
}

// ---- Script ---------------------------------------------------------------------------------

int Script::flagId(const char *name, size_t len) const { return findName(flags, flagCount, name, len); }
int Script::counterId(const char *name, size_t len) const { return findName(counters, counterCount, name, len); }
int Script::itemId(const char *name, size_t len) const { return findNamed(items, itemCount, name, len); }
int Script::questId(const char *name, size_t len) const { return findNamed(quests, questCount, name, len); }

const Npc *Script::npc(const char *id) const
{
    for (int i = 0; id && i < npcCount; i++)
        if (strcmp(npcs[i].id, id) == 0) return &npcs[i];
    return nullptr;
}

const Npc *Script::npcByNum(int num, int mapId) const
{
    for (int i = 0; i < npcCount; i++)
        if (npcs[i].num == num && npcs[i].mapId == mapId) return &npcs[i];
    return nullptr;
}

int Script::validate(char *err, size_t cap) const
{
    Errors e{err, cap, 0, 0};
    if (err && cap) err[0] = 0;
    if (flagCount > MAX_FLAGS || counterCount > MAX_COUNTERS || itemCount > MAX_ITEMS || questCount > MAX_QUESTS)
        e.add("too many flags / counters / items / quests");
    // Duplicate declarations
    for (int i = 0; i < flagCount; i++)
        if (findName(flags, i, flags[i], strlen(flags[i])) >= 0 || !validName(Span{flags[i], strlen(flags[i])}))
            e.add("flag '%s': duplicate or bad name", flags[i]);
    for (int i = 0; i < counterCount; i++)
        if (findName(counters, i, counters[i], strlen(counters[i])) >= 0 || findName(flags, flagCount, counters[i], strlen(counters[i])) >= 0)
            e.add("counter '%s': duplicate", counters[i]);
    for (int i = 0; i < itemCount; i++)
        if (findNamed(items, i, items[i].id, strlen(items[i].id)) >= 0 || !items[i].name.s[0])
            e.add("item '%s': duplicate or no name", items[i].id);
    for (int i = 0; i < questCount; i++)
        if (findNamed(quests, i, quests[i].id, strlen(quests[i].id)) >= 0 || !quests[i].name.s[0])
            e.add("quest '%s': duplicate or no name", quests[i].id);
    // People
    for (int i = 0; i < npcCount; i++) {
        const Npc &p = npcs[i];
        if (!p.id || !validName(Span{p.id, strlen(p.id)}) || strlen(p.id) > 24) e.add("npc %d: bad id", i);
        if (!p.name.s[0]) e.add("npc '%s': no name", p.id);
        if (p.num < 1 || p.num > 9) e.add("npc '%s': map number must be 1..9", p.id);
        if (p.mapId < 1) e.add("npc '%s': invalid internal map", p.id);
        for (int j = 0; j < i; j++) {
            if (p.num == npcs[j].num && p.mapId == npcs[j].mapId)
                e.add("npc '%s': map number %d used twice on map %d", p.id, p.num, p.mapId);
            if (p.id && npcs[j].id && strcmp(p.id, npcs[j].id) == 0) e.add("npc '%s': duplicate", p.id);
        }
    }
    auto nodeExists = [&](const char *id) {
        for (int i = 0; i < nodeCount; i++)
            if (strcmp(nodes[i].id, id) == 0) return true;
        return false;
    };
    // Nodes
    for (int i = 0; i < nodeCount; i++) {
        const Node &n = nodes[i];
        bool b;
        if (!n.id || !validName(Span{n.id, strlen(n.id)}) || isEnd(n.id)) { e.add("node %d: bad id", i); continue; }
        if (!checkCond(*this, nullptr, n.cond, b)) e.add("%s: bad condition '%s'", n.id, n.cond);
        if (!runEffects(*this, nullptr, n.effects, nullptr, nullptr, nullptr, 0)) e.add("%s: bad effects '%s'", n.id, n.effects);
        if (n.speaker && !npc(n.speaker)) e.add("%s: unknown speaker '%s'", n.id, n.speaker);
        if (!n.text.s[0] || !*n.text.s[0]) e.add("%s: no text", n.id);
        for (int l = 0; l < LANGS; l++)
            if (n.text.s[l] && utf8Len(n.text.s[l]) > MAX_TEXT_CHARS)
                e.add("%s: text too long (%d > %d chars, lang %d)", n.id, utf8Len(n.text.s[l]), MAX_TEXT_CHARS, l);
        bool gap = false;
        for (int c = 0; c < MAX_CHOICES; c++) {
            const Choice &ch = n.choices[c];
            if (!ch.text.s[0]) { gap = true; continue; }
            if (gap) e.add("%s: choice %d after an empty slot", n.id, c + 1);
            if (!ch.next || (!isEnd(ch.next) && !nodeExists(ch.next)))
                e.add("%s: choice %d goes to unknown node '%s'", n.id, c + 1, ch.next ? ch.next : "(null)");
            if (!checkCond(*this, nullptr, ch.cond, b)) e.add("%s: choice %d bad condition '%s'", n.id, c + 1, ch.cond);
            if (!runEffects(*this, nullptr, ch.effects, nullptr, nullptr, nullptr, 0))
                e.add("%s: choice %d bad effects '%s'", n.id, c + 1, ch.effects);
            for (int l = 0; l < LANGS; l++) {
                if (ch.text.s[l] && utf8Len(ch.text.s[l]) > MAX_CHOICE_CHARS)
                    e.add("%s: choice %d too long (%d > %d chars, lang %d)", n.id, c + 1, utf8Len(ch.text.s[l]), MAX_CHOICE_CHARS, l);
                if (ch.hint.s[l] && utf8Len(ch.hint.s[l]) > MAX_CHOICE_CHARS)
                    e.add("%s: choice %d hint too long (lang %d)", n.id, c + 1, l);
            }
            if (ch.hint.s[0] && !ch.cond) e.add("%s: choice %d has a hint but no condition", n.id, c + 1);
        }
    }
    // Every person can be talked to
    char sid[40];
    for (int i = 0; i < npcCount; i++) {
        snprintf(sid, sizeof(sid), "%s.start", npcs[i].id);
        if (!nodeExists(sid)) e.add("npc '%s': no node '%s'", npcs[i].id, sid);
    }
    // Reachability (by id: all variants of a reachable id are reachable). Fixed passes, small N.
    if (nodeCount <= 1024) {
        static_assert(MAX_CHOICES == 4, "");
        uint8_t reach[1024 / 8] = {};
        auto mark = [&](const char *id) {
            bool grew = false;
            for (int i = 0; i < nodeCount; i++)
                if (!(reach[i >> 3] & (1 << (i & 7))) && strcmp(nodes[i].id, id) == 0) {
                    reach[i >> 3] |= (uint8_t)(1 << (i & 7));
                    grew = true;
                }
            return grew;
        };
        for (int i = 0; i < npcCount; i++) {
            snprintf(sid, sizeof(sid), "%s.start", npcs[i].id);
            mark(sid);
        }
        for (bool grew = true; grew;) {
            grew = false;
            for (int i = 0; i < nodeCount; i++) {
                if (!(reach[i >> 3] & (1 << (i & 7)))) continue;
                for (int c = 0; c < MAX_CHOICES; c++) {
                    const Choice &ch = nodes[i].choices[c];
                    if (ch.text.s[0] && ch.next && !isEnd(ch.next) && mark(ch.next)) grew = true;
                }
            }
        }
        for (int i = 0; i < nodeCount; i++)
            if (!(reach[i >> 3] & (1 << (i & 7))) && nodes[i].id) e.add("%s: unreachable", nodes[i].id);
    } else {
        e.add("too many nodes");
    }
    return e.count;
}

// ---- State ----------------------------------------------------------------------------------

void StoryState::clear() { memset(this, 0, sizeof(*this)); }

void StoryState::setFlag(int id, bool on)
{
    if (id < 0 || id >= MAX_FLAGS) return;
    if (on) flags[id >> 3] |= (uint8_t)(1 << (id & 7));
    else flags[id >> 3] &= (uint8_t)~(1 << (id & 7));
}

// Layout (little endian): magic u32, version u16, reserved u16, body length u32, flags,
// counters (i16), items, quests, CRC32 of everything before it.
size_t StoryState::save(uint8_t *buf, size_t cap) const
{
    if (!buf || cap < SAVE_BYTES) return 0;
    uint8_t *p = buf;
    auto u8 = [&](int v) { *p++ = (uint8_t)v; };
    auto u16 = [&](int v) { u8(v & 0xFF); u8((v >> 8) & 0xFF); };
    auto u32 = [&](uint32_t v) { u16((int)(v & 0xFFFF)); u16((int)(v >> 16)); };
    u32(MAGIC);
    u16(VERSION);
    u16(0);
    u32((uint32_t)(SAVE_BYTES - 16));
    for (uint8_t f : flags) u8(f);
    for (int16_t c : counters) u16((uint16_t)c);
    for (uint8_t i : items) u8(i);
    for (uint8_t q : quests) u8(q);
    u32(crc32(buf, SAVE_BYTES - 4));
    return SAVE_BYTES;
}

bool StoryState::restore(const uint8_t *buf, size_t n)
{
    if (!buf || n != SAVE_BYTES) return false;
    const uint8_t *p = buf;
    auto u8 = [&]() { return (int)*p++; };
    auto u16 = [&]() { int v = u8(); return v | (u8() << 8); };
    auto u32 = [&]() { uint32_t v = (uint32_t)u16(); return v | ((uint32_t)u16() << 16); };
    if (u32() != MAGIC || u16() != VERSION) return false;
    u16();
    if (u32() != SAVE_BYTES - 16) return false;
    const uint8_t *t = buf + SAVE_BYTES - 4;
    const uint32_t crc = (uint32_t)t[0] | ((uint32_t)t[1] << 8) | ((uint32_t)t[2] << 16) | ((uint32_t)t[3] << 24);
    if (crc != crc32(buf, SAVE_BYTES - 4)) return false;
    StoryState s;
    for (uint8_t &f : s.flags) f = (uint8_t)u8();
    for (int16_t &c : s.counters) {
        c = (int16_t)(uint16_t)u16();
        if (c > COUNTER_LIMIT || c < -COUNTER_LIMIT) return false;
    }
    for (uint8_t &i : s.items) i = (uint8_t)u8();
    for (uint8_t &q : s.quests) {
        q = (uint8_t)u8();
        if (q > (uint8_t)Quest::DONE) return false;
    }
    *this = s;
    return true;
}

// ---- Evaluation -----------------------------------------------------------------------------

bool evalCond(const Script &sc, const StoryState &st, const char *cond)
{
    bool v;
    return checkCond(sc, &st, cond, v) && v;   // A broken condition never holds
}

bool applyEffects(const Script &sc, StoryState &st, const char *effects, Host *host, Note *notes, int *noteCount, int noteCap)
{
    return runEffects(sc, &st, effects, host, notes, noteCount, noteCap);
}

// ---- Dialog ---------------------------------------------------------------------------------

const Npc *Dialog::speaker() const { return node_ && sc_ ? sc_->npc(node_->speaker) : nullptr; }

bool Dialog::enter(const char *id)
{
    node_ = nullptr;
    count_ = 0;
    if (!id || isEnd(id)) return false;
    for (int i = 0; i < sc_->nodeCount; i++) {
        const Node &n = sc_->nodes[i];
        if (strcmp(n.id, id) == 0 && evalCond(*sc_, *st_, n.cond)) {
            node_ = &n;
            break;
        }
    }
    if (!node_) return false;
    applyEffects(*sc_, *st_, node_->effects, host_, notes_, &noteCount_, MAX_NOTES);
    for (int c = 0; c < MAX_CHOICES; c++) {
        const Choice &ch = node_->choices[c];
        if (!ch.text.s[0]) continue;
        const bool ok = evalCond(*sc_, *st_, ch.cond);
        if (!ok && !ch.hint.s[0]) continue;   // Hidden
        slot_[count_] = (uint8_t)c;
        enabled_[count_] = ok;
        count_++;
    }
    return true;
}

bool Dialog::start(const Script &sc, StoryState &st, const char *npcId, Host *host)
{
    sc_ = &sc;
    st_ = &st;
    host_ = host;
    noteCount_ = 0;
    node_ = nullptr;
    if (!npcId || strlen(npcId) > 24) return false;
    char id[40];
    snprintf(id, sizeof(id), "%s.start", npcId);
    return enter(id);
}

bool Dialog::choose(int i)
{
    if (!node_ || !enabled(i)) return false;
    noteCount_ = 0;
    const Choice &ch = choice(i);
    applyEffects(*sc_, *st_, ch.effects, host_, notes_, &noteCount_, MAX_NOTES);
    enter(ch.next);
    return true;
}

}  // namespace Story
}  // namespace Otr
