// adv_sd_session.h - Plays one SD adventure (a folder of ordered .adv maps) on the engine.
//
// It parses each map with the firmware-safe Adv loader into a caller-owned PSRAM Adv::Loaded,
// pushes the built MapDef into Otr::World and the built Script into the UI, and carries/resets the
// player's resources between maps according to that map's @rules (carry:/reset:). Save/restore is
// keyed by the adventure's folder name (not a WorldId), so each SD adventure keeps its own
// progress; the josef folder therefore keeps the Joseph line's progress separate as before.
//
// No LVGL / ESP-IDF dependency (pure engine + loader), so the carry/reset and save-roundtrip logic
// is host-tested by tests/test_adv_catalog.cpp and tests/test_adv_resources.cpp. The big Adv::Loaded scratch buffer is supplied
// by the caller (UI owns it in PSRAM and reuses it across maps).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "adv_catalog.h"
#include "otr_story.h"
#include "otr_world.h"

namespace Adv {

class SdSession : public Otr::ItemCollector {
public:
    enum PortalResult : uint8_t {
        PORTAL_NONE,
        PORTAL_TRAVELLED,
        PORTAL_BLOCKED,
        PORTAL_CHECKPOINT_RESPAWNED,
        PORTAL_FAILED
    };

    static constexpr size_t WORLD_IMAGE_CAP =
        16 + 32 + 7 * Otr::MAX_W * Otr::MAX_H + 6 + 8 + 2 * Otr::PLATE_GROUPS +
        1 + 6 + 4 * Otr::MAX_ENEMIES + 4;
    static constexpr size_t SAVE_CAP = 56 + WORLD_IMAGE_CAP + Otr::Story::StoryState::SAVE_BYTES +
        12 + (MAX_INTERNAL_MAPS - 1) * (6 + WORLD_IMAGE_CAP);

    ~SdSession() { clear(); }

    bool collect(uint8_t item) override;
    // Loads the first map of adventure `e` (found under `root`) into `w`, clears `story`, builds the
    // script into `scratch` and points `outScript` at it (nullptr when the map has no dialogue).
    // Returns false (with err) if the map file is missing, fails to parse, or the engine rejects it.
    bool start(const char *root, const CatalogEntry &e, Loaded &scratch, Otr::World &w,
               Otr::Story::StoryState &story, const Otr::Story::Script *&outScript, char *err, size_t errCap);

    // Reloads the current adventure's FIRST map fresh (story cleared). Used by "restart".
    bool restart(Loaded &scratch, Otr::World &w, Otr::Story::StoryState &story,
                 const Otr::Story::Script *&outScript, char *err, size_t errCap);

    // Advances to the next map after an exit, carrying/resetting resources per the LEAVING map's
    // @rules. "keys" covers legacy and all six colored key counters; reset wins over carry.
    // Skates persist like flashlight/pogo regardless of the carry/reset masks.
    // Returns false (with err) if there is no next map or it fails to load. Call hasNext()
    // first to tell "adventure complete" from "load error".
    bool travel(Loaded &scratch, Otr::World &w, Otr::Story::StoryState &story,
                const Otr::Story::Script *&outScript, char *err, size_t errCap);

    int currentMapId() const;
    bool hasInternalMaps() const { return internalMapCount_ > 1; }
    int internalMapCount() const { return internalMapCount_; }
    int checkpointMapId() const;
    PortalResult afterAction(int oldX, int oldY, uint32_t events, Loaded &scratch, Otr::World &w,
                             Otr::Story::StoryState &story, const Otr::Story::Script *&outScript,
                             char *err, size_t errCap);
    bool travelToDestination(const char *name, Loaded &scratch, Otr::World &w,
                             Otr::Story::StoryState &story, const Otr::Story::Script *&outScript,
                             char *err, size_t errCap);
    bool applyWorldActionOnMap(const char *verb, int mapId, int argc, const int *argv,
                               Loaded &scratch, Otr::World &w, Otr::Story::StoryState &story,
                               const Otr::Story::Script *&outScript, char *err, size_t errCap);
    bool respawnAtCheckpoint(Loaded &scratch, Otr::World &w, Otr::Story::StoryState &story,
                             const Otr::Story::Script *&outScript, char *err, size_t errCap);

    bool hasNext() const { return mapIndex_ + 1 < mapCount_; }
    int mapIndex() const { return mapIndex_; }
    int mapCount() const { return mapCount_; }
    const char *folder() const { return folder_; }
    bool active() const { return mapCount_ > 0; }
    bool completed() const { return completed_; }
    void markCompleted() { completed_ = true; }

    // Serialises {folder, map index} + World image + StoryState image. Returns bytes written (0 if
    // cap too small or nothing active).
    size_t save(const Otr::World &w, const Otr::Story::StoryState &story, uint8_t *out, size_t cap) const;

    // Restores a save made by save(): re-reads the recorded map from SD into `w`, overlays the saved
    // world + story state, and sets map index. Leaves everything untouched on any error.
    bool restore(const uint8_t *data, size_t n, Loaded &scratch, Otr::World &w,
                 Otr::Story::StoryState &story, const Otr::Story::Script *&outScript, char *err, size_t errCap);

    void clear();

    static constexpr uint32_t SAVE_MAGIC = 0x53564441;   // "ADVS"
    static constexpr uint16_t SAVE_VERSION = 2;

private:
    Otr::Story::StoryState *inventory_ = nullptr;
    const Otr::Story::Script *script_ = nullptr;
    // Parses map `index` into scratch, loads it into the world/story, points outScript at it.
    bool loadMapInto(int index, Loaded &scratch, Otr::World &w, Otr::Story::StoryState &story,
                     const Otr::Story::Script *&outScript, char *err, size_t errCap);
    bool loadInternalMap(int index, Loaded &scratch, Otr::World &w, Otr::Story::StoryState &story,
                         const Otr::Story::Script *&outScript, char *err, size_t errCap);
    bool saveInternalWorld(int index, const Otr::World &w);
    bool switchInternalMap(int index, int x, int y, Loaded &scratch, Otr::World &w,
                           Otr::Story::StoryState &story, const Otr::Story::Script *&outScript,
                           char *err, size_t errCap);
    bool validateInternalMaps(Loaded &scratch, char *err, size_t errCap);
    bool destination(const Loaded &loaded, const char *name, int &mapId, int &x, int &y) const;
    bool applySharedPlayer(const Otr::World &source, Otr::World &destination) const;
    bool restoreMultimap(const uint8_t *data, size_t n, Loaded &scratch, Otr::World &w,
                         Otr::Story::StoryState &story, const Otr::Story::Script *&outScript,
                         char *err, size_t errCap);

    char root_[64] = {0};
    char folder_[FOLDER_CAP] = {0};
    char maps_[MAX_MAPS_PER_ADV][MAP_NAME_CAP] = {{0}};
    int mapCount_ = 0;
    int mapIndex_ = 0;
    bool completed_ = false;
    bool multimap_ = false;
    int internalMapIds_[MAX_INTERNAL_MAPS] = {};
    int internalMapCount_ = 0;
    int internalIndex_ = 0;
    uint8_t *worldImages_[MAX_INTERNAL_MAPS] = {};
    size_t worldImageSizes_[MAX_INTERNAL_MAPS] = {};
    bool visited_[MAX_INTERNAL_MAPS] = {};
    uint32_t fileSignature_ = 0;
    int checkpointIndex_ = 0;
    uint8_t checkpointX_ = 0, checkpointY_ = 0;
    bool portalSuppressed_ = false;
    uint8_t suppressedMapId_ = 0, suppressedX_ = 0, suppressedY_ = 0;
    Otr::Rules mainRules_{};
};

}  // namespace Adv
