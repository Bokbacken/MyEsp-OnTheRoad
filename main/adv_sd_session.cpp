// adv_sd_session.cpp - see adv_sd_session.h.
#include "adv_sd_session.h"

#include <stdio.h>
#include <string.h>

namespace Adv {

namespace {

constexpr size_t HDR = 56;   // magic4 ver2 mapIndex2 folder40 worldLen4 storyLen4

void put16(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
void put32(uint8_t *p, uint32_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF; }
uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// Player resources carried between maps.
struct PlayerSnap {
    int energy, maxEnergy, suit, keys, livesLeft, livesUsed;
    bool boat;
    int flash, pogo;   // v0.18.0: permanent abilities (always carried, not gated by the carry mask)
    int colorKeys[(int)Otr::KeyColor::COUNT];
};
PlayerSnap snapPlayer(const Otr::World &w)
{
    PlayerSnap snap = {w.energy(), w.maxEnergy(), w.suitCharges(), w.keys(), w.livesLeft(), w.livesUsed(),
                       w.hasBoat(), w.flashlight(), w.pogo(), {}};
    for (int i = 0; i < (int)Otr::KeyColor::COUNT; i++)
        snap.colorKeys[i] = w.keys((Otr::KeyColor)i);
    return snap;
}

// Story progress snapshot keyed by NAME, so it survives a different map's script (whose item/flag
// indices differ). Static (serial use, too big for an ESP task stack).
constexpr int ID_CAP = 40;
struct NameVal {
    char id[ID_CAP];
    int val;
};
struct StorySnap {
    NameVal items[Otr::Story::MAX_ITEMS];
    int itemCount;
    NameVal counters[Otr::Story::MAX_COUNTERS];
    int counterCount;
    NameVal quests[Otr::Story::MAX_QUESTS];
    int questCount;
    char flags[Adv::MAX_FLAGS][ID_CAP];
    int flagCount;
};
StorySnap g_snap;

void snapStory(const Otr::Story::Script *sc, const Otr::Story::StoryState &st)
{
    g_snap.itemCount = g_snap.counterCount = g_snap.questCount = g_snap.flagCount = 0;
    if (!sc) return;
    for (int i = 0; i < sc->itemCount && i < Otr::Story::MAX_ITEMS; i++)
        if (st.items[i] > 0 && sc->items[i].id) {
            snprintf(g_snap.items[g_snap.itemCount].id, ID_CAP, "%s", sc->items[i].id);
            g_snap.items[g_snap.itemCount].val = st.items[i];
            g_snap.itemCount++;
        }
    for (int i = 0; i < sc->counterCount && i < Otr::Story::MAX_COUNTERS; i++)
        if (sc->counters[i]) {
            snprintf(g_snap.counters[g_snap.counterCount].id, ID_CAP, "%s", sc->counters[i]);
            g_snap.counters[g_snap.counterCount].val = st.counters[i];
            g_snap.counterCount++;
        }
    for (int i = 0; i < sc->questCount && i < Otr::Story::MAX_QUESTS; i++)
        if (st.quests[i] != (uint8_t)Otr::Story::Quest::NONE && sc->quests[i].id) {
            snprintf(g_snap.quests[g_snap.questCount].id, ID_CAP, "%s", sc->quests[i].id);
            g_snap.quests[g_snap.questCount].val = st.quests[i];
            g_snap.questCount++;
        }
    for (int i = 0; i < sc->flagCount && g_snap.flagCount < Adv::MAX_FLAGS; i++)
        if (st.flag(i) && sc->flags[i]) {
            snprintf(g_snap.flags[g_snap.flagCount], ID_CAP, "%s", sc->flags[i]);
            g_snap.flagCount++;
        }
}

// Re-applies the name-keyed snapshot into a (cleared) StoryState for the NEW map's script. Entries
// whose id does not exist in the new script are silently dropped.
void applyStory(const Otr::Story::Script *sc, Otr::Story::StoryState &st)
{
    if (!sc) return;
    for (int i = 0; i < g_snap.itemCount; i++) {
        int id = sc->itemId(g_snap.items[i].id, strlen(g_snap.items[i].id));
        if (id >= 0) {
            int v = g_snap.items[i].val;
            st.items[id] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
    }
    for (int i = 0; i < g_snap.counterCount; i++) {
        int id = sc->counterId(g_snap.counters[i].id, strlen(g_snap.counters[i].id));
        if (id >= 0) {
            int v = g_snap.counters[i].val;
            if (v > Otr::Story::COUNTER_LIMIT) v = Otr::Story::COUNTER_LIMIT;
            if (v < -Otr::Story::COUNTER_LIMIT) v = -Otr::Story::COUNTER_LIMIT;
            st.counters[id] = (int16_t)v;
        }
    }
    for (int i = 0; i < g_snap.questCount; i++) {
        int id = sc->questId(g_snap.quests[i].id, strlen(g_snap.quests[i].id));
        if (id >= 0) st.quests[id] = (uint8_t)g_snap.quests[i].val;
    }
    for (int i = 0; i < g_snap.flagCount; i++) {
        int id = sc->flagId(g_snap.flags[i], strlen(g_snap.flags[i]));
        if (id >= 0) st.setFlag(id, true);
    }
}

}  // namespace

void SdSession::clear()
{
    root_[0] = folder_[0] = 0;
    mapCount_ = mapIndex_ = 0;
    completed_ = false;
}

bool SdSession::loadMapInto(int index, Loaded &scratch, Otr::World &w, Otr::Story::StoryState &,
                            const Otr::Story::Script *&outScript, char *err, size_t errCap)
{
    if (index < 0 || index >= mapCount_) {
        snprintf(err, errCap, "map index %d out of range", index);
        return false;
    }
    char path[256];
    if ((size_t)snprintf(path, sizeof(path), "%s/%s/%s", root_, folder_, maps_[index]) >= sizeof(path)) {
        snprintf(err, errCap, "map path too long");
        return false;
    }
    if (!parseFile(path, scratch, err, errCap)) return false;
    if (!w.load(scratch.map)) {
        snprintf(err, errCap, "engine rejected %s", maps_[index]);
        return false;
    }
    outScript = scratch.nodeCount > 0 ? &scratch.script : nullptr;
    mapIndex_ = index;
    return true;
}

bool SdSession::start(const char *root, const CatalogEntry &e, Loaded &scratch, Otr::World &w,
                      Otr::Story::StoryState &story, const Otr::Story::Script *&outScript, char *err, size_t errCap)
{
    clear();
    snprintf(root_, sizeof(root_), "%s", root);
    snprintf(folder_, sizeof(folder_), "%s", e.folder);
    mapCount_ = e.mapCount > MAX_MAPS_PER_ADV ? MAX_MAPS_PER_ADV : e.mapCount;
    for (int i = 0; i < mapCount_; i++) snprintf(maps_[i], MAP_NAME_CAP, "%s", e.maps[i]);
    if (mapCount_ <= 0) {
        snprintf(err, errCap, "adventure '%s' has no maps", e.folder);
        return false;
    }
    story.clear();
    completed_ = false;
    return loadMapInto(0, scratch, w, story, outScript, err, errCap);
}

bool SdSession::restart(Loaded &scratch, Otr::World &w, Otr::Story::StoryState &story,
                        const Otr::Story::Script *&outScript, char *err, size_t errCap)
{
    if (mapCount_ <= 0) {
        snprintf(err, errCap, "no adventure active");
        return false;
    }
    story.clear();
    completed_ = false;
    return loadMapInto(0, scratch, w, story, outScript, err, errCap);
}

bool SdSession::travel(Loaded &scratch, Otr::World &w, Otr::Story::StoryState &story,
                       const Otr::Story::Script *&outScript, char *err, size_t errCap)
{
    if (!hasNext()) {
        snprintf(err, errCap, "no next map");
        return false;
    }
    // The map we are LEAVING declares (in its @rules) what the player carries onward. Snapshot the
    // old player resources + story progress before the scratch buffer is overwritten.
    const PlayerSnap old = snapPlayer(w);
    const uint32_t carryMask = scratch.carryMask, resetMask = scratch.resetMask;
    snapStory(outScript, story);

    if (!loadMapInto(mapIndex_ + 1, scratch, w, story, outScript, err, errCap)) return false;

    auto keep = [&](uint32_t b) { return (carryMask & b) && !(resetMask & b); };
    // Player resources: set max before current so a carried energy is not clamped to the new default.
    if (keep(CARRY_MAXENERGY)) w.setMaxEnergy(old.maxEnergy);
    if (keep(CARRY_ENERGY)) w.setEnergy(old.energy);
    if (keep(CARRY_SUIT)) w.setSuitCharges(old.suit);
    if (keep(CARRY_KEYS)) {
        w.setKeys(old.keys);
        for (int i = 0; i < (int)Otr::KeyColor::COUNT; i++)
            w.setKeys((Otr::KeyColor)i, old.colorKeys[i]);
    }
    if (keep(CARRY_BOAT)) w.setBoat(old.boat);
    if (keep(CARRY_LIVES)) w.setLives(old.livesLeft, old.livesUsed);
    // GEMS are map-local (each map has its own total), so they are never carried across maps.
    // v0.18.0: flashlight + pogo are PERMANENT abilities - always re-applied, regardless of the
    // per-map carry/reset mask. (World::load() zeroed them; restore their levels here.)
    w.setFlashlight(old.flash);
    w.setPogo(old.pogo);

    // Story items/flags/counters/quests: the raw bitset indices belong to the old script, so start
    // fresh and (when items carry) re-apply the snapshot by name onto the new script.
    story.clear();
    if (keep(CARRY_ITEMS)) applyStory(outScript, story);
    return true;
}

size_t SdSession::save(const Otr::World &w, const Otr::Story::StoryState &story, uint8_t *out, size_t cap) const
{
    if (mapCount_ <= 0) return 0;
    const size_t ws = w.saveSize();
    const size_t ss = Otr::Story::StoryState::SAVE_BYTES;
    const size_t need = HDR + ws + ss;
    if (cap < need) return 0;
    memset(out, 0, HDR);
    put32(out, SAVE_MAGIC);
    put16(out + 4, SAVE_VERSION);
    put16(out + 6, (uint16_t)mapIndex_);
    snprintf((char *)out + 8, FOLDER_CAP, "%s", folder_);
    put32(out + 48, (uint32_t)ws);
    put32(out + 52, (uint32_t)ss);
    if (w.save(out + HDR, ws) != ws) return 0;
    if (story.save(out + HDR + ws, ss) != ss) return 0;
    return need;
}

bool SdSession::restore(const uint8_t *data, size_t n, Loaded &scratch, Otr::World &w,
                        Otr::Story::StoryState &story, const Otr::Story::Script *&outScript, char *err, size_t errCap)
{
    if (n < HDR) {
        snprintf(err, errCap, "save too small");
        return false;
    }
    if (get32(data) != SAVE_MAGIC || get16(data + 4) != SAVE_VERSION) {
        snprintf(err, errCap, "bad save header");
        return false;
    }
    const int mapIndex = (int)get16(data + 6);
    const uint32_t ws = get32(data + 48), ss = get32(data + 52);
    if (HDR + (size_t)ws + (size_t)ss > n) {
        snprintf(err, errCap, "save truncated");
        return false;
    }
    char folder[FOLDER_CAP];
    snprintf(folder, sizeof(folder), "%.*s", FOLDER_CAP - 1, (const char *)data + 8);
    if (folder_[0] && strcmp(folder, folder_) != 0) {
        snprintf(err, errCap, "save folder '%s' != '%s'", folder, folder_);
        return false;
    }
    if (mapIndex < 0 || mapIndex >= mapCount_) {
        snprintf(err, errCap, "save map index %d out of range", mapIndex);
        return false;
    }
    // Validate the story image into a temp first so `story` stays untouched on error.
    Otr::Story::StoryState tmp;
    if (!tmp.restore(data + HDR + ws, ss)) {
        snprintf(err, errCap, "bad story image");
        return false;
    }
    if (!loadMapInto(mapIndex, scratch, w, story, outScript, err, errCap)) return false;
    if (!w.restore(data + HDR, ws)) {
        snprintf(err, errCap, "bad world image");
        return false;
    }
    story = tmp;
    completed_ = w.exitReached() && !hasNext();
    return true;
}

}  // namespace Adv
