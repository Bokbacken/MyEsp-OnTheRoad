#include "adv_live.h"

#include <mutex>
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace AdvLive {

using Otr::Obj;
using Otr::Tile;

static void copyStr(char *dst, size_t cap, const char *src)
{
    if (!cap) return;
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80) n--;   // Never cut a UTF-8 sequence
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

static int16_t clamp16(int v) { return (int16_t)(v < -32000 ? -32000 : v > 32000 ? 32000 : v); }

// ---- Filling (pure) ----------------------------------------------------------------------
void fillMap(Snapshot &s, const Source &src)
{
    const Otr::World &w = *src.world;
    s.worldId = w.mapId();
    s.worldRev = w.revision();
    s.w = (uint8_t)w.width();
    s.h = (uint8_t)w.height();
    s.fog = w.fog();
    s.hasDark = false;
    copyStr(s.world, sizeof(s.world), src.worldName);
    for (int y = 0; y < s.h; y++)
        for (int x = 0; x < s.w; x++) {
            s.hasDark = s.hasDark || w.darkCell(x, y);
            const bool fogHidden = s.fog && !w.revealed(x, y);
            const bool dark = !fogHidden && w.darkHidden(x, y);
            const bool hidden = fogHidden || dark;
            const Obj o = w.obj(x, y);
            const Tile t = w.tile(x, y);
            s.terrain[y][x] = dark ? '#' : hidden ? '?' : (t == Tile::GATE && w.gateOpenAt(x, y) ? 'M' : (char)('a' + (int)t));
            if (hidden) s.obj[y][x] = fogHidden && w.flagged(x, y) ? '!' : '.';
            else s.obj[y][x] = (o == Obj::NONE || o == Obj::NPC) ? '.' : (char)('A' + (int)o);
            s.hint[y][x] = (s.fog && !hidden) ? (char)('0' + w.mineHint(x, y)) : '0';
        }
}

void fillState(Snapshot &s, const Source &src)
{
    const Otr::World &w = *src.world;
    const int L = src.lang;
    s.px = (uint8_t)w.playerX();
    s.py = (uint8_t)w.playerY();
    s.facing = (uint8_t)w.facing();
    s.sailing = w.sailing();
    s.boat = w.hasBoat();
    s.suction = w.hasSuction();
    s.skates = w.hasSkates();
    s.flashlight = (uint8_t)w.flashlight();
    s.pogo = (uint8_t)w.pogo();
    s.gatesOpen = w.gatesOpen();
    s.gameOver = w.gameOver();
    s.exitReached = w.exitReached();
    s.energy = clamp16(w.energy());
    s.energyMax = clamp16(w.maxEnergy());
    s.lives = clamp16(w.livesLeft());
    s.livesUsed = clamp16(w.livesUsed());
    s.keys = clamp16(w.keys());
    s.gems = clamp16(w.gems());
    s.gemsTotal = clamp16(w.gemsTotal());
    s.suit = clamp16(w.suitCharges());
    s.cpX = (uint8_t)w.checkpointX();
    s.cpY = (uint8_t)w.checkpointY();
    s.steps = (uint32_t)w.steps();
    s.mapId=w.mapId();
    if(src.worldId) s.worldId=(uint8_t)src.worldId;
    s.mineLevel=w.mineLevel(); s.completed=src.completed;
    s.bomb3=w.bombs(3); s.bomb4=w.bombs(4); s.safeLeft=w.safeLeft(); s.lang=src.lang;
    s.bombSize=src.bombSize; s.bombX=src.bombX; s.bombY=src.bombY;

    // Moving actors are separate overlays, never object glyphs.
    s.enemyCount = 0;
    for (int i = 0; i < w.enemyCount() && s.enemyCount < Otr::MAX_ENEMIES; ++i) {
        const auto &e = w.enemy(i);
        if ((w.fog() && !w.revealed(e.x, e.y)) || w.darkHidden(e.x, e.y)) continue;
        s.enemies[s.enemyCount++] = EnemyInfo{e.x, e.y, w.enemyDef(i).kind == Otr::EnemyKind::CHASER};
    }

    // People: map numbers 1..9 that stand somewhere
    s.npcCount = 0;
    for (int num = 1; num <= MAX_NPCS; num++) {
        int x, y;
        if (!w.findNpc(num, x, y)) continue;
        if ((w.fog() && !w.revealed(x, y)) || w.darkHidden(x, y)) continue;
        NpcInfo &n = s.npcs[s.npcCount++];
        const Otr::Story::Npc *def = src.story ? src.story->npcByNum(num) : nullptr;
        n.num = (uint8_t)num;
        n.x = (uint8_t)x;
        n.y = (uint8_t)y;
        n.look = def ? def->look : (uint8_t)(num - 1);
        n.talking = num == src.talkNum;
        copyStr(n.name, sizeof(n.name), def ? def->name.get(L) : "");
    }

    // Carried story items
    s.itemCount = 0;
    if (src.story && src.storyState) {
        for (int i = 0; i < src.story->itemCount && i < Otr::Story::MAX_ITEMS && s.itemCount < MAX_ITEMS; i++) {
            const uint8_t c = src.storyState->items[i];
            if (!c) continue;
            ItemInfo &it = s.items[s.itemCount++];
            copyStr(it.name, sizeof(it.name), src.story->items[i].name.get(L));
            it.count = c;
            it.cap = src.story->items[i].cap;
        }
    }
    s.resourceTypeCount = s.resourceCount = 0;
    copyStr(s.resourceFolder, sizeof s.resourceFolder, src.folder);
    if (src.adventure && src.story) {
        const auto &adv = *src.adventure;
        for (int i = 0; i < adv.resourceCount && i < Adv::MAX_RESOURCES; ++i) {
            const auto &r = adv.resources[i];
            const int item = src.story->itemId(r.id, strlen(r.id));
            if (item < 0) continue;
            auto &type = s.resourceTypes[s.resourceTypeCount++];
            type.item = item;
            copyStr(type.name, sizeof type.name, r.name.get(L));
            copyStr(type.image, sizeof type.image, r.image);
        }
        for (int y = 0; y < w.height(); ++y) for (int x = 0; x < w.width(); ++x) {
            if (w.obj(x, y) != Obj::RESOURCE || (w.fog() && !w.revealed(x, y)) || w.darkHidden(x, y)) continue;
            for (int i = 0; i < s.resourceTypeCount && s.resourceCount < Adv::MAX_RES_PLACE; ++i)
                if (w.objAmount(x, y) == s.resourceTypes[i].item)
                    s.resources[s.resourceCount++] = {(uint8_t)x, (uint8_t)y, (uint8_t)i};
        }
    }

    // Current dialogue line (full text; the device's typewriter is not mirrored)
    DialogInfo &d = s.dialog;
    const Otr::Story::Dialog *dl = src.dialog;
    d.active = dl && dl->active() && dl->node();
    d.choiceCount = 0;
    if (!d.active) {
        d.narrator = false;
        d.look = 0;
        d.speaker[0] = d.text[0] = d.ref[0] = 0;
        return;
    }
    const Otr::Story::Node *nd = dl->node();
    const Otr::Story::Npc *sp = dl->speaker();
    d.narrator = sp == nullptr;
    d.look = sp ? sp->look : 0;
    copyStr(d.speaker, sizeof(d.speaker), sp ? sp->name.get(L) : "");
    copyStr(d.text, sizeof(d.text), nd->text.get(L));
    const char *ref = nd->ref ? nd->ref : nd->quote;
    if (ref) snprintf(d.ref, sizeof(d.ref), src.refFormat ? src.refFormat : "%s", ref);
    else d.ref[0] = 0;
    bool any = false;
    const int n = dl->choiceCount();
    for (int i = 0; i < n && d.choiceCount < MAX_CHOICES; i++) {
        const Otr::Story::Choice &c = dl->choice(i);
        const bool en = dl->enabled(i);
        any = any || en;
        ChoiceInfo &ci = d.choices[d.choiceCount++];
        copyStr(ci.text, sizeof(ci.text), c.text.get(L));
        copyStr(ci.hint, sizeof(ci.hint), en ? "" : c.hint.get(L));
        ci.locked = !en;
    }
    // Same fallbacks as the device (ui_adventure talkShowChoices())
    const char *extra = !n ? src.okText : !any ? src.byeText : nullptr;
    if (extra && d.choiceCount < MAX_CHOICES) {
        ChoiceInfo &ci = d.choices[d.choiceCount++];
        copyStr(ci.text, sizeof(ci.text), extra);
        ci.hint[0] = 0;
        ci.locked = false;
    }
}

// ---- JSON (pure) -------------------------------------------------------------------------
namespace {

struct Out {
    char *buf;
    size_t cap, len;
    bool ok;
    void raw(const char *s, size_t n)
    {
        if (!ok) return;
        if (len + n >= cap) {
            ok = false;
            return;
        }
        memcpy(buf + len, s, n);
        len += n;
        buf[len] = 0;
    }
    void put(const char *s) { raw(s, strlen(s)); }
    void fmt(const char *f, ...) __attribute__((format(printf, 2, 3)))
    {
        if (!ok) return;
        va_list ap;
        va_start(ap, f);
        const int n = vsnprintf(buf + len, cap - len, f, ap);
        va_end(ap);
        if (n < 0 || (size_t)n >= cap - len) {
            ok = false;
            buf[len] = 0;
            return;
        }
        len += (size_t)n;
    }
    void str(const char *s)   // JSON string with escaping (UTF-8 passes through)
    {
        put("\"");
        for (const char *p = s; *p && ok; p++) {
            const unsigned char c = (unsigned char)*p;
            if (c == '"') put("\\\"");
            else if (c == '\\') put("\\\\");
            else if (c == '\n') put("\\n");
            else if (c == '\r') put("\\r");
            else if (c == '\t') put("\\t");
            else if (c < 0x20) fmt("\\u%04x", c);
            else raw(p, 1);
        }
        put("\"");
    }
    void key(const char *k)
    {
        str(k);
        put(":");
    }
    void boolean(bool b) { put(b ? "true" : "false"); }
};

const char *const DIR_NAME[4] = {"up", "right", "down", "left"};

void rows(Out &o, const char (*layer)[Otr::MAX_W], int w, int h)
{
    o.put("[");
    for (int y = 0; y < h; y++) {
        if (y) o.put(",");
        o.put("\"");
        for (int x = 0; x < w; ++x) {
            if (layer[y][x] == '\\') o.put("\\\\");   // Appended object IDs include backslash.
            else if (layer[y][x] == '"') o.put("\\\"");
            else o.raw(&layer[y][x], 1);
        }
        o.put("\"");
    }
    o.put("]");
}

}  // namespace

size_t toJson(const Snapshot &s, uint32_t clientMapRev, char *buf, size_t cap)
{
    if (!buf || !cap) return 0;
    Out o{buf, cap, 0, true};
    buf[0] = 0;
    if (!s.active) {
        o.fmt("{\"active\":false,\"version\":%u}", (unsigned)s.version);
        return o.ok ? o.len : 0;
    }
    o.fmt("{\"active\":true,\"version\":%u,", (unsigned)s.version);
    o.key("world");
    o.str(s.world);
    o.fmt(",\"world_id\":%u,\"map_rev\":%u,", (unsigned)s.worldId, (unsigned)s.mapRev);
    if (clientMapRev != s.mapRev) {
        o.fmt("\"map\":{\"w\":%u,\"h\":%u,\"fog\":%s,\"terrain\":", (unsigned)s.w, (unsigned)s.h,
              s.fog ? "true" : "false");
        rows(o, s.terrain, s.w, s.h);
        o.put(",\"obj\":");
        rows(o, s.obj, s.w, s.h);
        o.put(",\"hint\":");
        if (s.fog) rows(o, s.hint, s.w, s.h);
        else o.put("[]");
        o.put("},");
    }
    o.fmt("\"player\":{\"x\":%u,\"y\":%u,\"dir\":\"%s\",\"sailing\":%s},", (unsigned)s.px, (unsigned)s.py,
          DIR_NAME[s.facing & 3], s.sailing ? "true" : "false");
    o.fmt("\"energy\":%d,\"energy_max\":%d,\"lives\":%d,\"lives_used\":%d,\"keys\":%d,\"gems\":%d,"
          "\"gems_total\":%d,\"suit\":%d,",
          s.energy, s.energyMax, s.lives, s.livesUsed, s.keys, s.gems, s.gemsTotal, s.suit);
    o.fmt("\"map_id\":%u,\"mine_level\":%u,\"completed\":%u,\"bomb3\":%u,\"bomb4\":%u,"
          "\"safe_left\":%d,\"lang\":%u,\"bomb_preview\":[%d,%d,%d],",
          s.mapId,s.mineLevel,s.completed,s.bomb3,s.bomb4,s.safeLeft,s.lang,s.bombSize,s.bombX,s.bombY);
    o.put("\"boat\":");
    o.boolean(s.boat);
    o.fmt(",\"suction\":%s,\"skates\":%s,\"flashlight\":%u,\"pogo\":%u",
          s.suction ? "true" : "false", s.skates ? "true" : "false",
          (unsigned)s.flashlight, (unsigned)s.pogo);
    o.put(",\"gates_open\":");
    o.boolean(s.gatesOpen);
    o.put(",\"game_over\":");
    o.boolean(s.gameOver);
    o.put(",\"exit\":");
    o.boolean(s.exitReached);
    o.fmt(",\"checkpoint\":[%u,%u],\"steps\":%u,\"npcs\":[", (unsigned)s.cpX, (unsigned)s.cpY, (unsigned)s.steps);
    for (int i = 0; i < s.npcCount; i++) {
        const NpcInfo &n = s.npcs[i];
        if (i) o.put(",");
        o.fmt("{\"num\":%u,\"x\":%u,\"y\":%u,\"look\":%u,\"talking\":%s,\"name\":", (unsigned)n.num, (unsigned)n.x,
              (unsigned)n.y, (unsigned)n.look, n.talking ? "true" : "false");
        o.str(n.name);
        o.put("}");
    }
    o.put("],\"enemies\":[");
    for (int i = 0; i < s.enemyCount; ++i) {
        const EnemyInfo &e = s.enemies[i];
        if (i) o.put(",");
        o.fmt("{\"x\":%u,\"y\":%u,\"kind\":\"%s\"}",
              (unsigned)e.x, (unsigned)e.y, e.chaser ? "chaser" : "patrol");
    }
    o.put("],\"items\":[");
    for (int i = 0; i < s.itemCount; i++) {
        if (i) o.put(",");
        o.put("{\"name\":");
        o.str(s.items[i].name);
        o.fmt(",\"count\":%u,\"cap\":%u}", (unsigned)s.items[i].count, (unsigned)s.items[i].cap);
    }
    o.put("],\"resource_folder\":");
    o.str(s.resourceFolder);
    o.put(",\"resource_types\":[");
    for (int i = 0; i < s.resourceTypeCount; ++i) {
        if (i) o.put(",");
        o.put("{\"name\":"); o.str(s.resourceTypes[i].name);
        o.put(",\"image\":"); o.str(s.resourceTypes[i].image);
        o.put("}");
    }
    o.put("],\"resources\":[");
    for (int i = 0; i < s.resourceCount; ++i) {
        if (i) o.put(",");
        const auto &r = s.resources[i];
        o.fmt("{\"x\":%u,\"y\":%u,\"type\":%u}", r.x, r.y, r.type);
    }
    o.put("],\"dialog\":");
    const DialogInfo &d = s.dialog;
    if (!d.active) {
        o.put("null");
    } else {
        o.put("{\"speaker\":");
        o.str(d.speaker);
        o.put(",\"narrator\":");
        o.boolean(d.narrator);
        o.fmt(",\"look\":%u,\"text\":", (unsigned)d.look);
        o.str(d.text);
        o.put(",\"ref\":");
        o.str(d.ref);
        o.put(",\"choices\":[");
        for (int i = 0; i < d.choiceCount; i++) {
            const ChoiceInfo &c = d.choices[i];
            if (i) o.put(",");
            o.put("{\"text\":");
            o.str(c.text);
            o.put(",\"locked\":");
            o.boolean(c.locked);
            o.put(",\"hint\":");
            o.str(c.hint);
            o.put("}");
        }
        o.put("]}");
    }
    o.put("}");
    return o.ok ? o.len : 0;
}

// ---- Thread-safe snapshot ----------------------------------------------------------------
static std::mutex s_mtx;
static Snapshot *s_snap = nullptr;   // PSRAM on the device; allocated by the first publish()
static uint32_t s_version = 0;       // Kept outside the snapshot so version() works before it exists
static bool s_mapDirty = true;       // invalidateMap(): copy the map on the next publish()

static Snapshot *snapLocked()
{
    if (!s_snap) {
#ifdef ESP_PLATFORM
        void *m = heap_caps_calloc(1, sizeof(Snapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        void *m = calloc(1, sizeof(Snapshot));
#endif
        if (m) s_snap = new (m) Snapshot();
    }
    return s_snap;
}

void publish(const Source &src)
{
    if (!src.world) return;
    std::lock_guard<std::mutex> lk(s_mtx);
    Snapshot *s = snapLocked();
    if (!s) return;
    const Otr::World &w = *src.world;
    if (s_mapDirty || !s->active || s->mapRev == 0 || s->worldRev != w.revision() || s->worldId != w.mapId() ||
        s->w != w.width() || s->h != w.height() ||
        (s->hasDark && (s->px != w.playerX() || s->py != w.playerY() ||
         s->facing != (uint8_t)w.facing() || s->flashlight != w.flashlight())) ||
        strcmp(s->world, src.worldName ? src.worldName : "") != 0) {
        fillMap(*s, src);
        s->mapRev++;
        if (s->mapRev == 0) s->mapRev = 1;   // 0 means "no map" to a client
        s_mapDirty = false;
    }
    fillState(*s, src);
    s->active = true;
    s->version = ++s_version;
}

void invalidateMap()
{
    std::lock_guard<std::mutex> lk(s_mtx);
    s_mapDirty = true;
}

void setInactive()
{
    std::lock_guard<std::mutex> lk(s_mtx);
    if (!s_snap || !s_snap->active) return;
    s_snap->active = false;
    s_snap->dialog.active = false;
    s_snap->version = ++s_version;
}

bool active()
{
    std::lock_guard<std::mutex> lk(s_mtx);
    return s_snap && s_snap->active;
}

uint32_t version()
{
    std::lock_guard<std::mutex> lk(s_mtx);
    return s_version;
}

size_t writeJson(uint32_t clientMapRev, char *buf, size_t cap)
{
    std::lock_guard<std::mutex> lk(s_mtx);
    if (!s_snap || !s_snap->active) {
        const int n = snprintf(buf, cap, "{\"active\":false,\"version\":%u}", (unsigned)s_version);
        return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
    }
    return toJson(*s_snap, clientMapRev, buf, cap);
}

size_t writeBriefJson(char *buf, size_t cap)
{
    if (!buf || !cap) return 0;
    std::lock_guard<std::mutex> lk(s_mtx);
    Out o{buf, cap, 0, true};
    buf[0] = 0;
    const bool on = s_snap && s_snap->active;
    o.fmt("{\"active\":%s,\"version\":%u", on ? "true" : "false", (unsigned)s_version);
    if (on) {
        o.put(",\"world\":");
        o.str(s_snap->world);
    }
    o.put("}");
    return o.ok ? o.len : 0;
}

}  // namespace AdvLive
