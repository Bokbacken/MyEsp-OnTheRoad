#pragma once
#include <stddef.h>
#include <stdint.h>
#include <map>
#include <string>
#include <vector>

// Per-song lyrics sync corrections and a "broken sync" mark for Supernova. Pure C++ (host-tested).
// A correction is a delta (ms) that applies from a position in the song onwards, so a drift that
// starts half-way is fixed only from there. Positive delta = lyrics appear later.
namespace SupernovaSync {

constexpr size_t MAX_SONGS = 4096;
constexpr size_t MAX_ADJ = 32;
constexpr int32_t MAX_TOTAL_MS = 120000;
constexpr uint32_t MERGE_MS = 2000;   // Adjustments this close to an existing one are merged into it

struct Adj {
    uint32_t posMs = 0;
    int32_t deltaMs = 0;
};

struct Entry {
    bool broken = false;
    std::vector<Adj> adj;   // Sorted by posMs
    bool empty() const { return !broken && adj.empty(); }
};

// Total offset in effect at posMs.
int32_t offsetAt(const Entry &e, uint32_t posMs);
// Time to look up in the lyrics for an audio/video position (never below 0).
uint32_t lyricTime(const Entry *e, uint32_t posMs);

class Store {
public:
    const Entry *find(const std::string &folder) const;
    bool broken(const std::string &folder) const;
    // Adds deltaMs from posMs onwards. False if the folder is invalid or limits would be exceeded.
    bool adjust(const std::string &folder, uint32_t posMs, int32_t deltaMs);
    bool setBroken(const std::string &folder, bool broken);
    void resetAdjustments(const std::string &folder);
    size_t size() const { return m_map.size(); }
    std::string encode() const;
    bool decode(const char *data, size_t len);

private:
    static bool validFolder(const std::string &folder);
    void prune(const std::string &folder);
    std::map<std::string, Entry> m_map;
};

}  // namespace SupernovaSync
