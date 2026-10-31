// adv_sd_session.cpp - see adv_sd_session.h.
#include "adv_sd_session.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <new>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace Adv {

namespace {

constexpr size_t HDR = 56;   // magic4 ver2 mapIndex2 folder40 worldLen4 storyLen4
constexpr size_t MULTIMAP_META = 12;
constexpr size_t MAP_RECORD = 6;
constexpr uint16_t LEGACY_SAVE_VERSION = 1;

void put16(uint8_t *p, uint16_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
void put32(uint8_t *p, uint32_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF; }
uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

uint32_t fileSignature(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t hash = 2166136261u;
    uint8_t block[512];
    size_t n;
    while ((n = fread(block, 1, sizeof(block), f)) != 0)
        for (size_t i = 0; i < n; ++i) hash = (hash ^ block[i]) * 16777619u;
    const bool failed = ferror(f);
    fclose(f);
    return failed ? 0 : hash;
}

void *restoreBuffer(size_t size)
{
#ifdef ESP_PLATFORM
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return malloc(size);
#endif
}

// Player resources carried between maps.
struct PlayerSnap {
    int energy, maxEnergy, suit, keys, livesLeft, livesUsed;
    bool boat;
    bool skates;
    bool suction;
    int flash, pogo;   // v0.18.0: permanent abilities (always carried, not gated by the carry mask)
    int colorKeys[(int)Otr::KeyColor::COUNT];
};
PlayerSnap snapPlayer(const Otr::World &w)
{
    PlayerSnap snap = {w.energy(), w.maxEnergy(), w.suitCharges(), w.keys(), w.livesLeft(), w.livesUsed(),
                       w.hasBoat(), w.hasSkates(), w.hasSuction(), w.flashlight(), w.pogo(), {}};
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
            st.items[id] = (uint8_t)(v < 0 ? 0 : v > sc->items[id].cap ? sc->items[id].cap : v);
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
    for (int i = 0; i < MAX_INTERNAL_MAPS; ++i) {
        free(worldImages_[i]);
        worldImages_[i] = nullptr;
        worldImageSizes_[i] = 0;
        visited_[i] = false;
        internalMapIds_[i] = 0;
    }
    inventory_ = nullptr;
    script_ = nullptr;
    root_[0] = folder_[0] = 0;
    mapCount_ = mapIndex_ = 0;
    completed_ = false;
    multimap_ = false;
    internalMapCount_ = internalIndex_ = 0;
    fileSignature_ = 0;
    checkpointIndex_ = 0;
    checkpointX_ = checkpointY_ = 0;
    portalSuppressed_ = false;
    suppressedMapId_ = suppressedX_ = suppressedY_ = 0;
    memset(&mainRules_, 0, sizeof(mainRules_));
}

bool SdSession::collect(uint8_t item)
{
    if (!inventory_ || !script_ || item >= script_->itemCount ||
        inventory_->items[item] >= script_->items[item].cap) return false;
    ++inventory_->items[item];
    return true;
}

bool SdSession::loadMapInto(int index, Loaded &scratch, Otr::World &w, Otr::Story::StoryState &story,
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
    if (mapCount_ != 1 &&
        (scratch.internalMapCount > 1 || scratch.portalCount > 0 || scratch.destinationCount > 0)) {
        snprintf(err, errCap, "internal maps cannot be mixed with separate .adv files");
        return false;
    }
    if (scratch.script.validate(err, errCap)) return false;
    if (!w.load(scratch.map)) {
        snprintf(err, errCap, "engine rejected %s", maps_[index]);
        return false;
    }
    outScript = scratch.nodeCount > 0 || scratch.itemCount > 0 ? &scratch.script : nullptr;
    inventory_ = &story;
    script_ = &scratch.script;
    w.setItemCollector(this);
    mapIndex_ = index;
    return true;
}

bool SdSession::loadInternalMap(int index, Loaded &scratch, Otr::World &w, Otr::Story::StoryState &story,
                               const Otr::Story::Script *&outScript, char *err, size_t errCap)
{
    (void)story;
    if (!multimap_ || index < 0 || index >= internalMapCount_) {
        snprintf(err, errCap, "internal map index out of range");
        return false;
    }
    char path[256];
    if ((size_t)snprintf(path, sizeof(path), "%s/%s/%s", root_, folder_, maps_[0]) >= sizeof(path)) {
        snprintf(err, errCap, "map path too long");
        return false;
    }
    if (!parseMapFile(path, internalMapIds_[index], scratch, err, errCap)) return false;
    if (scratch.internalMapCount != internalMapCount_ ||
        memcmp(scratch.internalMapIds, internalMapIds_, sizeof(internalMapIds_))) {
        snprintf(err, errCap, "internal map declarations changed while loading");
        return false;
    }
    if (scratch.script.validate(err, errCap)) return false;
    scratch.map.rules.startEnergy = mainRules_.startEnergy;
    scratch.map.rules.energyCap = mainRules_.energyCap;
    scratch.map.rules.lives = mainRules_.lives;
    if (!w.load(scratch.map)) {
        snprintf(err, errCap, "engine rejected internal map %d", internalMapIds_[index]);
        return false;
    }
    outScript = scratch.nodeCount > 0 || scratch.itemCount > 0 || scratch.npcCount > 0 ?
        &scratch.script : nullptr;
    w.setItemCollector(this);
    return true;
}

bool SdSession::validateInternalMaps(Loaded &scratch, char *err, size_t errCap)
{
    void *memory = restoreBuffer(sizeof(Otr::World));
    if (!memory) {
        snprintf(err, errCap, "out of PSRAM validating internal maps");
        return false;
    }
    auto *candidate = new (memory) Otr::World;
    Otr::Story::StoryState story{};
    const Otr::Story::Script *script = nullptr;
    bool ok = true;
    for (int i = 0; i < internalMapCount_ && ok; ++i) {
        if (!loadInternalMap(i, scratch, *candidate, story, script, err, errCap)) { ok = false; break; }
        if (scratch.map.rules.mineLevel) {
            snprintf(err, errCap, "mine_level is not supported in internal-map adventures");
            ok = false;
            break;
        }
        for (int j = 0; j < scratch.destinationCount; ++j) {
            const Destination &d = scratch.destinations[j];
            if (d.mapId == internalMapIds_[i] && !candidate->safeArrivalAt(d.x, d.y)) {
                snprintf(err, errCap, "destination '%s' is not safe and walkable on map %d",
                         d.name, internalMapIds_[i]);
                ok = false;
                break;
            }
        }
        for (int j = 0; ok && j < scratch.portalCount; ++j) {
            const Portal &p = scratch.portals[j];
            if (p.mapId == internalMapIds_[i] &&
                (candidate->tile(p.x, p.y) == Otr::Tile::EXIT || !candidate->safeArrivalAt(p.x, p.y))) {
                snprintf(err, errCap, "portal source is not safe and walkable on map %d",
                         internalMapIds_[i]);
                ok = false;
            }
        }
    }
    free(memory);
    return ok;
}

bool SdSession::saveInternalWorld(int index, const Otr::World &w)
{
    if (index < 0 || index >= internalMapCount_) return false;
    const size_t n = w.saveSize();
    if (!n || n > WORLD_IMAGE_CAP) return false;
    if (!worldImages_[index] || worldImageSizes_[index] != n) {
        void *memory = restoreBuffer(n);
        if (!memory) return false;
        free(worldImages_[index]);
        worldImages_[index] = static_cast<uint8_t *>(memory);
        worldImageSizes_[index] = n;
    }
    if (w.save(worldImages_[index], worldImageSizes_[index]) != n) return false;
    visited_[index] = true;
    return true;
}

int SdSession::currentMapId() const
{
    if (multimap_ && internalIndex_ >= 0 && internalIndex_ < internalMapCount_)
        return internalMapIds_[internalIndex_];
    return 1;
}

int SdSession::checkpointMapId() const
{
    return multimap_ && checkpointIndex_ >= 0 && checkpointIndex_ < internalMapCount_ ?
        internalMapIds_[checkpointIndex_] : 1;
}

bool SdSession::destination(const Loaded &loaded, const char *name, int &mapId, int &x, int &y) const
{
    if (!name || !*name) return false;
    for (int i = 0; i < loaded.destinationCount; ++i) {
        const Destination &d = loaded.destinations[i];
        if (!strcmp(name, d.name)) {
            mapId = d.mapId;
            x = d.x;
            y = d.y;
            return true;
        }
    }
    return false;
}

bool SdSession::applySharedPlayer(const Otr::World &source, Otr::World &destinationWorld) const
{
    const PlayerSnap p = snapPlayer(source);
    destinationWorld.setMaxEnergy(p.maxEnergy);
    destinationWorld.setEnergy(p.energy);
    destinationWorld.setSuitCharges(p.suit);
    destinationWorld.setKeys(p.keys);
    for (int i = 0; i < (int)Otr::KeyColor::COUNT; ++i)
        destinationWorld.setKeys((Otr::KeyColor)i, p.colorKeys[i]);
    destinationWorld.setBoat(p.boat);
    destinationWorld.setLives(p.livesLeft, p.livesUsed);
    destinationWorld.setFlashlight(p.flash);
    destinationWorld.setPogo(p.pogo);
    destinationWorld.setSkates(p.skates);
    destinationWorld.setSuction(p.suction);
    return true;
}

bool SdSession::travelToDestination(const char *name, Loaded &scratch, Otr::World &w,
                                   Otr::Story::StoryState &story, const Otr::Story::Script *&outScript,
                                   char *err, size_t errCap)
{
    if (!multimap_) {
        snprintf(err, errCap, "named travel requires an internal-map adventure");
        return false;
    }
    int mapId, x, y;
    if (!destination(scratch, name, mapId, x, y)) {
        snprintf(err, errCap, "unknown destination '%s'", name ? name : "");
        return false;
    }
    int index = -1;
    for (int i = 0; i < internalMapCount_; ++i)
        if (internalMapIds_[i] == mapId) { index = i; break; }
    if (index < 0) {
        snprintf(err, errCap, "destination references missing map %d", mapId);
        return false;
    }
    return switchInternalMap(index, x, y, scratch, w, story, outScript, err, errCap);
}

SdSession::PortalResult SdSession::afterAction(int oldX, int oldY, uint32_t events, Loaded &scratch,
                                               Otr::World &w, Otr::Story::StoryState &story,
                                               const Otr::Story::Script *&outScript,
                                               char *err, size_t errCap)
{
    if (!multimap_) return PORTAL_NONE;
    if ((events & Otr::EV_CHECKPOINT) ||
        ((events & Otr::EV_MOVED) && !(events & (Otr::EV_DIED | Otr::EV_GAME_OVER)) &&
         w.tile(w.playerX(), w.playerY()) == Otr::Tile::CHECKPOINT)) {
        checkpointIndex_ = internalIndex_;
        checkpointX_ = (uint8_t)w.playerX();
        checkpointY_ = (uint8_t)w.playerY();
    }
    if ((events & Otr::EV_DIED) && !w.gameOver()) {
        if (checkpointIndex_ != internalIndex_) {
            if (!switchInternalMap(checkpointIndex_, checkpointX_, checkpointY_, scratch, w, story,
                                   outScript, err, errCap)) return PORTAL_FAILED;
        }
        portalSuppressed_ = true;
        suppressedMapId_ = (uint8_t)currentMapId();
        suppressedX_ = (uint8_t)w.playerX();
        suppressedY_ = (uint8_t)w.playerY();
        return PORTAL_CHECKPOINT_RESPAWNED;
    }
    if (events & Otr::EV_GAME_OVER) return PORTAL_NONE;

    if (portalSuppressed_ &&
        (suppressedMapId_ != currentMapId() || suppressedX_ != w.playerX() || suppressedY_ != w.playerY()))
        portalSuppressed_ = false;
    if (portalSuppressed_ || (oldX == w.playerX() && oldY == w.playerY())) return PORTAL_NONE;

    for (int i = 0; i < scratch.portalCount; ++i) {
        const Portal &p = scratch.portals[i];
        if (p.mapId != currentMapId() || p.x != w.playerX() || p.y != w.playerY()) continue;
        if (travelToDestination(p.destination, scratch, w, story, outScript, err, errCap))
            return PORTAL_TRAVELLED;
        return strstr(err, "blocked") ? PORTAL_BLOCKED : PORTAL_FAILED;
    }
    return PORTAL_NONE;
}

bool SdSession::respawnAtCheckpoint(Loaded &scratch, Otr::World &w, Otr::Story::StoryState &story,
                                    const Otr::Story::Script *&outScript, char *err, size_t errCap)
{
    if (!multimap_) return true;
    if (checkpointIndex_ != internalIndex_) {
        if (!switchInternalMap(checkpointIndex_, checkpointX_, checkpointY_, scratch, w, story,
                               outScript, err, errCap)) return false;
    }
    portalSuppressed_ = true;
    suppressedMapId_ = (uint8_t)currentMapId();
    suppressedX_ = (uint8_t)w.playerX();
    suppressedY_ = (uint8_t)w.playerY();
    return true;
}

bool SdSession::applyWorldActionOnMap(const char *verb, int mapId, int argc, const int *argv,
                                      Loaded &scratch, Otr::World &w, Otr::Story::StoryState &story,
                                      const Otr::Story::Script *&outScript, char *err, size_t errCap)
{
    (void)scratch;   // Preserve the caller-owned active script backing during mapped actions.
    (void)outScript; // The active script remains the one the caller already holds.
    if (!multimap_ || !verb || mapId < 1) {
        snprintf(err, errCap, "mapped world action requires an internal-map adventure");
        return false;
    }
    int index = -1;
    for (int i = 0; i < internalMapCount_; ++i)
        if (internalMapIds_[i] == mapId) { index = i; break; }
    if (index < 0) {
        snprintf(err, errCap, "world action references missing map %d", mapId);
        return false;
    }
    if (argc < 0 || argc > 3 || (argc && !argv) || strlen(verb) >= 16) {
        snprintf(err, errCap, "invalid mapped world action arguments");
        return false;
    }
    char verbCopy[16];
    snprintf(verbCopy, sizeof(verbCopy), "%s", verb);
    int argsCopy[3] = {};
    for (int i = 0; i < argc; ++i) argsCopy[i] = argv[i];
    auto reservedCell = [&](int x, int y) {
        for (int i = 0; i < scratch.destinationCount; ++i) {
            const Destination &d = scratch.destinations[i];
            if (d.mapId == mapId && d.x == x && d.y == y) return true;
        }
        for (int i = 0; i < scratch.portalCount; ++i) {
            const Portal &p = scratch.portals[i];
            if (p.mapId == mapId && p.x == x && p.y == y) return true;
        }
        return false;
    };
    auto apply = [&](Otr::World &target) {
        if (!strcmp(verbCopy, "open") && argc == 2) return target.clearObj(argsCopy[0], argsCopy[1]);
        if (!strcmp(verbCopy, "npc_move") && argc == 3)
            return target.moveNpc(argsCopy[0], argsCopy[1], argsCopy[2]);
        if (!strcmp(verbCopy, "npc_remove") && argc == 1) return target.removeNpc(argsCopy[0]);
        if (!strcmp(verbCopy, "tile") && argc == 3) {
            if (reservedCell(argsCopy[0], argsCopy[1])) return false;
            return target.setStoryTerrain(argsCopy[0], argsCopy[1], argsCopy[2]);
        }
        if (!strcmp(verbCopy, "object") && argc == 3) {
            if (reservedCell(argsCopy[0], argsCopy[1])) return false;
            return target.placeStoryObject(argsCopy[0], argsCopy[1], argsCopy[2]);
        }
        return false;
    };
    if (index == internalIndex_) {
        if (!apply(w)) { snprintf(err, errCap, "mapped world action %s failed", verbCopy); return false; }
        return true;
    }

    if (!saveInternalWorld(internalIndex_, w)) {
        snprintf(err, errCap, "out of memory saving map state");
        return false;
    }
    void *targetScratchMemory = restoreBuffer(sizeof(Loaded));
    void *worldMemory = restoreBuffer(sizeof(Otr::World));
    if (!targetScratchMemory || !worldMemory) {
        free(targetScratchMemory); free(worldMemory);
        snprintf(err, errCap, "out of PSRAM applying mapped world action");
        return false;
    }
    auto *targetScratch = new (targetScratchMemory) Loaded;
    auto *candidate = new (worldMemory) Otr::World;
    const Otr::Story::Script *unused = nullptr;
    bool ok = loadInternalMap(index, *targetScratch, *candidate, story, unused, err, errCap);
    if (ok && visited_[index]) {
        ok = worldImages_[index] && candidate->restore(worldImages_[index], worldImageSizes_[index]);
        if (!ok) snprintf(err, errCap, "saved state for map %d is invalid", mapId);
    }
    if (ok && !apply(*candidate)) {
        snprintf(err, errCap, "mapped world action %s failed", verbCopy);
        ok = false;
    }
    if (ok && !saveInternalWorld(index, *candidate)) {
        snprintf(err, errCap, "out of memory saving mapped world action");
        ok = false;
    }
    // Keep the caller's Loaded/Script backing and outScript untouched. Story::applyEffects()
    // may still be iterating effect strings and argument spans inside that storage.
    free(targetScratchMemory); free(worldMemory);
    return ok;
}

bool SdSession::restoreMultimap(const uint8_t *data, size_t n, Loaded &scratch, Otr::World &w,
                               Otr::Story::StoryState &story, const Otr::Story::Script *&outScript,
                               char *err, size_t errCap)
{
    if (n < HDR + MULTIMAP_META || get16(data + 4) != SAVE_VERSION) {
        snprintf(err, errCap, "multimap save too small or wrong version");
        return false;
    }
    char folder[FOLDER_CAP];
    snprintf(folder, sizeof(folder), "%.*s", FOLDER_CAP - 1, (const char *)data + 8);
    if (strcmp(folder, folder_) != 0) {
        snprintf(err, errCap, "save folder '%s' != '%s'", folder, folder_);
        return false;
    }
    const int activeId = get16(data + 6);
    int active = -1;
    for (int i = 0; i < internalMapCount_; ++i)
        if (internalMapIds_[i] == activeId) active = i;
    if (active < 0) {
        snprintf(err, errCap, "save internal map %d is not declared", activeId);
        return false;
    }
    const uint32_t ws = get32(data + 48), ss = get32(data + 52);
    if (ws > WORLD_IMAGE_CAP || ss != Otr::Story::StoryState::SAVE_BYTES ||
        HDR + (size_t)ws + (size_t)ss + MULTIMAP_META > n) {
        snprintf(err, errCap, "multimap save lengths are invalid");
        return false;
    }
    Otr::Story::StoryState savedStory;
    if (!savedStory.restore(data + HDR + ws, ss)) {
        snprintf(err, errCap, "bad story image");
        return false;
    }
    const size_t meta = HDR + ws + ss;
    if (data[meta] != internalMapCount_ || data[meta + 7] > 1 ||
        get32(data + meta + 8) != fileSignature_) {
        snprintf(err, errCap, "save signature or internal map count does not match");
        return false;
    }
    char sourcePath[256];
    if ((size_t)snprintf(sourcePath, sizeof(sourcePath), "%s/%s/%s", root_, folder_, maps_[0]) >=
            sizeof(sourcePath) ||
        fileSignature(sourcePath) != fileSignature_) {
        snprintf(err, errCap, "adventure source file changed since session start");
        return false;
    }
    int checkpoint = -1;
    for (int i = 0; i < internalMapCount_; ++i)
        if (internalMapIds_[i] == data[meta + 1]) checkpoint = i;
    if (checkpoint < 0) {
        snprintf(err, errCap, "save checkpoint map is not declared");
        return false;
    }
    int suppressed = -1;
    if (data[meta + 7]) {
        for (int i = 0; i < internalMapCount_; ++i)
            if (internalMapIds_[i] == data[meta + 4]) suppressed = i;
        if (suppressed < 0) {
            snprintf(err, errCap, "save portal arrival map is not declared");
            return false;
        }
    } else if (data[meta + 4] || data[meta + 5] || data[meta + 6]) {
        snprintf(err, errCap, "invalid portal arrival state");
        return false;
    }

    uint8_t *staged[MAX_INTERNAL_MAPS] = {};
    size_t stagedSizes[MAX_INTERNAL_MAPS] = {};
    bool stagedVisited[MAX_INTERNAL_MAPS] = {};
    auto failRestore = [&]() {
        for (int i = 0; i < MAX_INTERNAL_MAPS; ++i) free(staged[i]);
        return false;
    };
    size_t pos = meta + MULTIMAP_META;
    for (int i = 0; i < internalMapCount_; ++i) {
        if (i == active) { stagedVisited[i] = true; continue; }
        if (pos + MAP_RECORD > n || data[pos] != internalMapIds_[i] || data[pos + 1] > 1) {
            snprintf(err, errCap, "invalid saved internal-map record");
            return failRestore();
        }
        const bool visited = data[pos + 1] != 0;
        const uint32_t imageSize = get32(data + pos + 2);
        pos += MAP_RECORD;
        if ((visited && (!imageSize || imageSize > WORLD_IMAGE_CAP)) ||
            (!visited && imageSize) || pos + imageSize > n) {
            snprintf(err, errCap, "invalid saved map-state length");
            return failRestore();
        }
        stagedVisited[i] = visited;
        if (visited) {
            staged[i] = static_cast<uint8_t *>(restoreBuffer(imageSize));
            if (!staged[i]) { snprintf(err, errCap, "out of PSRAM restoring map states"); return failRestore(); }
            memcpy(staged[i], data + pos, imageSize);
            stagedSizes[i] = imageSize;
        }
        pos += imageSize;
    }
    if (pos != n) {
        snprintf(err, errCap, "unexpected trailing multimap save data");
        return failRestore();
    }

    void *backup = restoreBuffer(sizeof(Loaded));
    void *worldMemory = restoreBuffer(sizeof(Otr::World));
    if (!backup || !worldMemory) {
        free(backup); free(worldMemory);
        snprintf(err, errCap, "out of PSRAM restoring multimap");
        return failRestore();
    }
    memcpy(backup, &scratch, sizeof(scratch));
    auto *candidate = new (worldMemory) Otr::World;
    Otr::Story::StoryState scratchStory{};
    const Otr::Story::Script *newScript = nullptr;
    bool valid = true;
    for (int i = 0; i < internalMapCount_; ++i) {
        if (i == active || !stagedVisited[i]) continue;
        if (!loadInternalMap(i, scratch, *candidate, scratchStory, newScript, err, errCap) ||
            !candidate->restore(staged[i], stagedSizes[i])) {
            if (!err[0]) snprintf(err, errCap, "saved state for map %d is invalid", internalMapIds_[i]);
            valid = false;
            break;
        }
        for (int item = 0; item < scratch.itemCount; ++item)
            if (savedStory.items[item] > scratch.items[item].cap) {
                snprintf(err, errCap, "saved item exceeds cap: %s", scratch.items[item].id);
                valid = false;
                break;
            }
        if (!valid) break;
        if (i == checkpoint && !candidate->safeArrivalAt(data[meta + 2], data[meta + 3])) {
            snprintf(err, errCap, "saved checkpoint is unsafe");
            valid = false;
            break;
        }
    }
    if (valid && (!loadInternalMap(active, scratch, *candidate, scratchStory, newScript, err, errCap) ||
                  !candidate->restore(data + HDR, ws))) {
        if (!err[0]) snprintf(err, errCap, "saved active map state is invalid");
        valid = false;
    }
    if (valid) {
        for (int item = 0; item < scratch.itemCount; ++item)
            if (savedStory.items[item] > scratch.items[item].cap) {
                snprintf(err, errCap, "saved item exceeds cap: %s", scratch.items[item].id);
                valid = false;
                break;
            }
    }
    if (valid && checkpoint == active &&
        !candidate->safeArrivalAt(data[meta + 2], data[meta + 3])) {
        snprintf(err, errCap, "saved checkpoint is unsafe");
        valid = false;
    }
    if (valid && data[meta + 7] && suppressed == active &&
        (data[meta + 5] >= Otr::MAX_W || data[meta + 6] >= Otr::MAX_H)) {
        snprintf(err, errCap, "saved portal arrival is outside map bounds");
        valid = false;
    }
    if (!valid) {
        memcpy(&scratch, backup, sizeof(scratch));
        free(backup); free(worldMemory);
        return failRestore();
    }
    w = *candidate;
    outScript = newScript;
    story = savedStory;
    script_ = newScript;
    inventory_ = &story;
    internalIndex_ = active;
    mapIndex_ = 0;
    checkpointIndex_ = checkpoint;
    checkpointX_ = data[meta + 2];
    checkpointY_ = data[meta + 3];
    portalSuppressed_ = data[meta + 7] != 0;
    suppressedMapId_ = data[meta + 4];
    suppressedX_ = data[meta + 5];
    suppressedY_ = data[meta + 6];
    for (int i = 0; i < internalMapCount_; ++i) {
        free(worldImages_[i]);
        worldImages_[i] = staged[i];
        staged[i] = nullptr;
        worldImageSizes_[i] = stagedSizes[i];
        visited_[i] = stagedVisited[i];
    }
    worldImages_[active] = nullptr;
    worldImageSizes_[active] = 0;
    visited_[active] = true;
    completed_ = w.exitReached() && !hasNext();
    free(backup); free(worldMemory);
    return true;
}

bool SdSession::switchInternalMap(int index, int x, int y, Loaded &scratch, Otr::World &w,
                                  Otr::Story::StoryState &story, const Otr::Story::Script *&outScript,
                                  char *err, size_t errCap)
{
    if (!multimap_ || index < 0 || index >= internalMapCount_) {
        snprintf(err, errCap, "internal destination map does not exist");
        return false;
    }
    if (index == internalIndex_) {
        if (!w.safeArrivalAt(x, y)) {
            snprintf(err, errCap, "portal destination is blocked or unsafe");
            return false;
        }
        if (!saveInternalWorld(index, w) || !w.setPlayerPosition(x, y)) {
            snprintf(err, errCap, "could not save or enter portal destination");
            return false;
        }
        portalSuppressed_ = true;
        suppressedMapId_ = (uint8_t)internalMapIds_[index];
        suppressedX_ = (uint8_t)x;
        suppressedY_ = (uint8_t)y;
        return true;
    }

    if (!saveInternalWorld(internalIndex_, w)) {
        snprintf(err, errCap, "out of memory saving map state");
        return false;
    }
    void *backup = restoreBuffer(sizeof(Loaded));
    void *worldMemory = restoreBuffer(sizeof(Otr::World));
    if (!backup || !worldMemory) {
        free(backup); free(worldMemory);
        snprintf(err, errCap, "out of PSRAM changing maps");
        return false;
    }
    memcpy(backup, &scratch, sizeof(scratch));
    auto *candidate = new (worldMemory) Otr::World;
    const Otr::Story::Script *newScript = nullptr;
    bool ok = loadInternalMap(index, scratch, *candidate, story, newScript, err, errCap);
    if (ok && visited_[index]) {
        ok = worldImages_[index] && candidate->restore(worldImages_[index], worldImageSizes_[index]);
        if (!ok) snprintf(err, errCap, "saved state for map %d is invalid", internalMapIds_[index]);
    }
    if (ok) {
        applySharedPlayer(w, *candidate);
        if (!candidate->safeArrivalAt(x, y)) {
            snprintf(err, errCap, "portal destination is blocked or unsafe");
            ok = false;
        } else if (!candidate->setPlayerPosition(x, y)) {
            snprintf(err, errCap, "portal destination is blocked or unsafe");
            ok = false;
        }
    }
    if (!ok) {
        memcpy(&scratch, backup, sizeof(scratch));
        free(backup); free(worldMemory);
        return false;
    }
    w = *candidate;
    outScript = newScript;
    script_ = newScript;
    inventory_ = &story;
    internalIndex_ = index;
    visited_[index] = true;
    portalSuppressed_ = true;
    suppressedMapId_ = (uint8_t)internalMapIds_[index];
    suppressedX_ = (uint8_t)x;
    suppressedY_ = (uint8_t)y;
    free(backup); free(worldMemory);
    return true;
}

bool SdSession::start(const char *root, const CatalogEntry &e, Loaded &scratch, Otr::World &w,
                      Otr::Story::StoryState &story, const Otr::Story::Script *&outScript, char *err, size_t errCap)
{
    clear();
    if (!root) {
        snprintf(err, errCap, "adventure root is missing");
        return false;
    }
    size_t rootLen = 0;
    while (rootLen < sizeof(root_) && root[rootLen]) ++rootLen;
    if (rootLen == sizeof(root_)) {
        snprintf(err, errCap, "adventure root path is too long");
        return false;
    }
    if (!memchr(e.folder, 0, sizeof(e.folder))) {
        snprintf(err, errCap, "adventure folder name is too long");
        return false;
    }
    snprintf(root_, sizeof(root_), "%s", root);
    snprintf(folder_, sizeof(folder_), "%s", e.folder);
    if (e.mapCount > MAX_MAPS_PER_ADV) {
        snprintf(err, errCap, "adventure has too many separate files");
        clear();
        return false;
    }
    mapCount_ = e.mapCount;
    for (int i = 0; i < mapCount_; i++) snprintf(maps_[i], MAP_NAME_CAP, "%s", e.maps[i]);
    if (mapCount_ <= 0) {
        snprintf(err, errCap, "adventure '%s' has no maps", e.folder);
        return false;
    }
    story.clear();
    completed_ = false;
    if (!loadMapInto(0, scratch, w, story, outScript, err, errCap)) return false;
    if (scratch.internalMapCount > 1 || scratch.portalCount > 0 || scratch.destinationCount > 0) {
        if (mapCount_ != 1) {
            snprintf(err, errCap, "internal maps cannot be mixed with separate .adv files");
            clear();
            return false;
        }
        multimap_ = true;
        internalMapCount_ = scratch.internalMapCount;
        memcpy(internalMapIds_, scratch.internalMapIds, sizeof(internalMapIds_));
        mainRules_ = scratch.map.rules;
        char path[256];
        if ((size_t)snprintf(path, sizeof(path), "%s/%s/%s", root_, folder_, maps_[0]) >= sizeof(path)) {
            snprintf(err, errCap, "map path too long"); clear(); return false;
        }
        fileSignature_ = fileSignature(path);
        if (!fileSignature_) { snprintf(err, errCap, "cannot fingerprint multimap adventure"); clear(); return false; }
        if (!validateInternalMaps(scratch, err, errCap)) { clear(); return false; }
        if (!loadInternalMap(0, scratch, w, story, outScript, err, errCap)) { clear(); return false; }
        internalIndex_ = 0;
        visited_[0] = true;
        checkpointIndex_ = 0;
        checkpointX_ = (uint8_t)w.checkpointX();
        checkpointY_ = (uint8_t)w.checkpointY();
    }
    return true;
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
    if (multimap_) {
        for (int i = 0; i < internalMapCount_; ++i) {
            free(worldImages_[i]);
            worldImages_[i] = nullptr;
            worldImageSizes_[i] = 0;
            visited_[i] = i == 0;
        }
        internalIndex_ = checkpointIndex_ = 0;
        portalSuppressed_ = false;
        if (!loadInternalMap(0, scratch, w, story, outScript, err, errCap)) return false;
        script_ = outScript;
        inventory_ = &story;
        checkpointX_ = (uint8_t)w.checkpointX();
        checkpointY_ = (uint8_t)w.checkpointY();
        return true;
    }
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
    // Flashlight, pogo, skates and suction are PERMANENT abilities - always re-applied, regardless of the
    // per-map carry/reset mask. (World::load() zeroed them; restore their levels here.)
    w.setFlashlight(old.flash);
    w.setPogo(old.pogo);
    w.setSkates(old.skates);
    w.setSuction(old.suction);

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
    if (!out || ws > WORLD_IMAGE_CAP) return 0;
    if (!multimap_) {
        const size_t need = HDR + ws + ss;
        if (cap < need) return 0;
        memset(out, 0, HDR);
        put32(out, SAVE_MAGIC);
        put16(out + 4, LEGACY_SAVE_VERSION);
        put16(out + 6, (uint16_t)mapIndex_);
        snprintf((char *)out + 8, FOLDER_CAP, "%s", folder_);
        put32(out + 48, (uint32_t)ws);
        put32(out + 52, (uint32_t)ss);
        if (w.save(out + HDR, ws) != ws) return 0;
        if (story.save(out + HDR + ws, ss) != ss) return 0;
        return need;
    }

    size_t need = HDR + ws + ss + MULTIMAP_META;
    for (int i = 0; i < internalMapCount_; ++i) {
        if (i == internalIndex_) continue;
        const size_t imageLen = visited_[i] ? worldImageSizes_[i] : 0;
        if (imageLen > WORLD_IMAGE_CAP || (visited_[i] && (!worldImages_[i] || !imageLen))) return 0;
        need += MAP_RECORD + imageLen;
    }
    if (cap < need) return 0;
    memset(out, 0, HDR);
    put32(out, SAVE_MAGIC);
    put16(out + 4, SAVE_VERSION);
    put16(out + 6, (uint16_t)internalMapIds_[internalIndex_]);
    snprintf((char *)out + 8, FOLDER_CAP, "%s", folder_);
    put32(out + 48, (uint32_t)ws);
    put32(out + 52, (uint32_t)ss);
    if (w.save(out + HDR, ws) != ws) return 0;
    if (story.save(out + HDR + ws, ss) != ss) return 0;
    size_t pos = HDR + ws + ss;
    out[pos] = (uint8_t)internalMapCount_;
    out[pos + 1] = (uint8_t)internalMapIds_[checkpointIndex_];
    out[pos + 2] = checkpointX_;
    out[pos + 3] = checkpointY_;
    out[pos + 4] = portalSuppressed_ ? suppressedMapId_ : 0;
    out[pos + 5] = portalSuppressed_ ? suppressedX_ : 0;
    out[pos + 6] = portalSuppressed_ ? suppressedY_ : 0;
    out[pos + 7] = portalSuppressed_ ? 1 : 0;
    put32(out + pos + 8, fileSignature_);
    pos += MULTIMAP_META;
    for (int i = 0; i < internalMapCount_; ++i) {
        if (i == internalIndex_) continue;
        out[pos] = (uint8_t)internalMapIds_[i];
        out[pos + 1] = visited_[i] ? 1 : 0;
        const size_t imageLen = visited_[i] ? worldImageSizes_[i] : 0;
        put32(out + pos + 2, (uint32_t)imageLen);
        pos += MAP_RECORD;
        if (imageLen) {
            memcpy(out + pos, worldImages_[i], imageLen);
            pos += imageLen;
        }
    }
    return need;
}

bool SdSession::restore(const uint8_t *data, size_t n, Loaded &scratch, Otr::World &w,
                        Otr::Story::StoryState &story, const Otr::Story::Script *&outScript, char *err, size_t errCap)
{
    if (n < HDR) {
        snprintf(err, errCap, "save too small");
        return false;
    }
    const uint16_t version = get16(data + 4);
    if (get32(data) != SAVE_MAGIC || (version != LEGACY_SAVE_VERSION && version != SAVE_VERSION)) {
        snprintf(err, errCap, "bad save header");
        return false;
    }
    if (version == SAVE_VERSION) {
        if (!multimap_) {
            snprintf(err, errCap, "multimap save used with a single-map adventure");
            return false;
        }
        return restoreMultimap(data, n, scratch, w, story, outScript, err, errCap);
    }
    if (multimap_) {
        snprintf(err, errCap, "legacy v1 save cannot restore a multimap adventure");
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
    // Loaded owns the script's strings. Back it up at its original addresses, and stage
    // the world separately: an invalid cap/image must not reset the running game.
    void *backup = restoreBuffer(sizeof(Loaded));
    void *worldMemory = restoreBuffer(sizeof(Otr::World));
    if (!backup || !worldMemory) {
        free(backup); free(worldMemory);
        snprintf(err, errCap, "out of PSRAM restoring adventure");
        return false;
    }
    memcpy(backup, &scratch, sizeof scratch);
    auto *candidate = new (worldMemory) Otr::World;
    const int oldIndex = mapIndex_;
    const auto *oldScript = script_;
    auto *oldInventory = inventory_;
    const Otr::Story::Script *newScript = nullptr;
    auto rollback = [&]() {
        memcpy(static_cast<void *>(&scratch), backup, sizeof scratch);
        mapIndex_ = oldIndex;
        script_ = oldScript;
        inventory_ = oldInventory;
        free(backup); free(worldMemory);
        return false;
    };
    if (!loadMapInto(mapIndex, scratch, *candidate, story, newScript, err, errCap)) return rollback();
    for (int i = 0; i < scratch.itemCount; ++i)
        if (tmp.items[i] > scratch.items[i].cap) {
            snprintf(err, errCap, "saved item exceeds cap: %s", scratch.items[i].id);
            return rollback();
        }
    if (!candidate->restore(data + HDR, ws)) {
        snprintf(err, errCap, "bad world image");
        return rollback();
    }
    w = *candidate;
    outScript = newScript;
    free(backup); free(worldMemory);
    story = tmp;
    completed_ = w.exitReached() && !hasNext();
    return true;
}

}  // namespace Adv
