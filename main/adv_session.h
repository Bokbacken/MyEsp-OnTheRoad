// Adventure session: two-map demo plus campaign, using existing World/StoryState codecs.
#pragma once
#include <vector>
#include "otr_maps.h"
#include "otr_story.h"
namespace Adventure {
class Session {
public:
    static constexpr size_t SAVE_CAP = 64 * 1024;
    bool start(int worldId, Otr::World &w, Otr::Story::StoryState &story);
    bool travel(Otr::World &w); // Triggered only after EV_EXIT; spawn never lies on a portal
    bool next(Otr::World &w);
    bool restart(Otr::World &w);
    int worldId() const { return worldId_; }
    int mapIndex() const { return map_; }
    int completed() const { return completed_; }
    void recordWin(const Otr::World &w);
    size_t save(const Otr::World &w, const Otr::Story::StoryState &story, uint8_t *out, size_t cap) const;
    // Transactional, including old OTRS images; legacy .sts is supplied separately by the UI.
    bool restore(const uint8_t *data, size_t n, Otr::World &w, Otr::Story::StoryState &story);
    void clear();
private:
    int worldId_ = 0, map_ = 0, completed_ = 0;
    std::vector<uint8_t> maps_[2];
};
bool loadMap(int worldId, int index, int level, Otr::World &w);
uint32_t saveCrc(const uint8_t *p, size_t n);
}
