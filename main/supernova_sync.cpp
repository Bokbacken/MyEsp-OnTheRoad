#include "supernova_sync.h"

#include <stdlib.h>

#include <algorithm>

namespace SupernovaSync {

int32_t offsetAt(const Entry &e, uint32_t posMs)
{
    int32_t total = 0;
    for (const Adj &a : e.adj) {
        if (a.posMs > posMs) break;
        total += a.deltaMs;
    }
    return total;
}

uint32_t lyricTime(const Entry *e, uint32_t posMs)
{
    if (!e) return posMs;
    const int64_t t = (int64_t)posMs - offsetAt(*e, posMs);
    return t < 0 ? 0 : (uint32_t)t;
}

bool Store::validFolder(const std::string &folder)
{
    return !folder.empty() && folder.find_first_of("\t\r\n/\\") == std::string::npos;
}

const Entry *Store::find(const std::string &folder) const
{
    const auto it = m_map.find(folder);
    return it == m_map.end() ? nullptr : &it->second;
}

bool Store::broken(const std::string &folder) const
{
    const Entry *e = find(folder);
    return e && e->broken;
}

void Store::prune(const std::string &folder)
{
    const auto it = m_map.find(folder);
    if (it != m_map.end() && it->second.empty()) m_map.erase(it);
}

bool Store::adjust(const std::string &folder, uint32_t posMs, int32_t deltaMs)
{
    if (!validFolder(folder) || deltaMs == 0) return false;
    if (m_map.find(folder) == m_map.end() && m_map.size() >= MAX_SONGS) return false;
    Entry &e = m_map[folder];
    if (std::abs((int64_t)offsetAt(e, UINT32_MAX) + deltaMs) > MAX_TOTAL_MS) {
        prune(folder);
        return false;
    }
    auto near = std::find_if(e.adj.begin(), e.adj.end(), [&](const Adj &a) {
        return (a.posMs > posMs ? a.posMs - posMs : posMs - a.posMs) <= MERGE_MS;
    });
    if (near != e.adj.end()) {
        near->deltaMs += deltaMs;
        near->posMs = std::min(near->posMs, posMs);   // Must already apply at the current position
        if (near->deltaMs == 0) e.adj.erase(near);
    } else if (e.adj.size() < MAX_ADJ) {
        const Adj fresh{posMs, deltaMs};
        e.adj.insert(std::upper_bound(e.adj.begin(), e.adj.end(), fresh,
                                      [](const Adj &a, const Adj &b) { return a.posMs < b.posMs; }),
                     fresh);
    } else {
        prune(folder);
        return false;
    }
    prune(folder);
    return true;
}

bool Store::setBroken(const std::string &folder, bool value)
{
    if (!validFolder(folder)) return false;
    if (m_map.find(folder) == m_map.end()) {
        if (!value) return true;
        if (m_map.size() >= MAX_SONGS) return false;
    }
    m_map[folder].broken = value;
    prune(folder);
    return true;
}

void Store::resetAdjustments(const std::string &folder)
{
    const auto it = m_map.find(folder);
    if (it == m_map.end()) return;
    it->second.adj.clear();
    prune(folder);
}

// One song per line: folder TAB flags ("B" = broken, may be empty) TAB pos:delta,pos:delta
std::string Store::encode() const
{
    std::string out;
    for (const auto &kv : m_map) {
        out += kv.first;
        out += '\t';
        if (kv.second.broken) out += 'B';
        out += '\t';
        for (size_t i = 0; i < kv.second.adj.size(); i++) {
            if (i) out += ',';
            out += std::to_string(kv.second.adj[i].posMs) + ":" + std::to_string(kv.second.adj[i].deltaMs);
        }
        out += '\n';
    }
    return out;
}

bool Store::decode(const char *data, size_t len)
{
    m_map.clear();
    if (!data) return false;
    size_t pos = 0;
    while (pos < len) {
        size_t eol = pos;
        while (eol < len && data[eol] != '\n') eol++;
        size_t end = eol;
        if (end > pos && data[end - 1] == '\r') end--;
        const std::string line(data + pos, end - pos);
        pos = eol + 1;
        const size_t t1 = line.find('\t');
        const size_t t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        const std::string folder = line.substr(0, t1);
        if (!validFolder(folder) || m_map.size() >= MAX_SONGS) continue;
        Entry e;
        e.broken = line.substr(t1 + 1, t2 - t1 - 1) == "B";
        const std::string list = line.substr(t2 + 1);
        size_t i = 0;
        while (i < list.size() && e.adj.size() < MAX_ADJ) {
            size_t comma = list.find(',', i);
            if (comma == std::string::npos) comma = list.size();
            const std::string item = list.substr(i, comma - i);
            i = comma + 1;
            const size_t colon = item.find(':');
            if (colon == std::string::npos) continue;
            const long long p = strtoll(item.c_str(), nullptr, 10);
            const long long d = strtoll(item.c_str() + colon + 1, nullptr, 10);
            if (p < 0 || p > 86400000 || d == 0 || d > MAX_TOTAL_MS || d < -MAX_TOTAL_MS) continue;
            e.adj.push_back({(uint32_t)p, (int32_t)d});
        }
        std::sort(e.adj.begin(), e.adj.end(), [](const Adj &a, const Adj &b) { return a.posMs < b.posMs; });
        if (!e.empty()) m_map[folder] = std::move(e);
    }
    return true;
}

}  // namespace SupernovaSync
