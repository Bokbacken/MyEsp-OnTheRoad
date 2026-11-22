// supernova_playlists.cpp - see supernova_playlists.h.
#include "supernova_playlists.h"

#include <stdlib.h>
#include <strings.h>

#include <algorithm>

namespace SupernovaPlaylists {

namespace {

bool plain(const std::string &s)
{
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7f) return false;
    return true;
}

std::string trimmed(const std::string &s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
    return s.substr(a, b - a);
}

}  // namespace

bool Store::validName(const std::string &name)
{
    return !name.empty() && name.size() <= MAX_NAME && plain(name) && name == trimmed(name);
}

bool Store::validFolder(const std::string &folder)
{
    return !folder.empty() && folder.size() <= MAX_FOLDER && plain(folder) &&
           folder.find('/') == std::string::npos && folder.find('\\') == std::string::npos &&
           folder != "." && folder != "..";
}

const Playlist *Store::find(uint16_t id) const
{
    for (const Playlist &p : m_lists)
        if (p.id == id) return &p;
    return nullptr;
}

Playlist *Store::get(uint16_t id)
{
    for (Playlist &p : m_lists)
        if (p.id == id) return &p;
    return nullptr;
}

bool Store::nameTaken(const std::string &name, uint16_t except) const
{
    for (const Playlist &p : m_lists)
        if (p.id != except && strcasecmp(p.name.c_str(), name.c_str()) == 0) return true;
    return false;
}

Err Store::create(const std::string &rawName, uint16_t &idOut)
{
    const std::string name = trimmed(rawName);
    if (!validName(name)) return Err::BAD_NAME;
    if (nameTaken(name, 0)) return Err::DUPLICATE_NAME;
    if (m_lists.size() >= MAX_LISTS) return Err::FULL;
    uint16_t id = 1;
    for (const Playlist &p : m_lists) id = std::max<uint16_t>(id, (uint16_t)(p.id + 1));
    if (id == 0) return Err::FULL;
    Playlist p;
    p.id = id;
    p.name = name;
    m_lists.push_back(std::move(p));
    idOut = id;
    return Err::OK;
}

Err Store::rename(uint16_t id, const std::string &rawName)
{
    Playlist *p = get(id);
    if (!p) return Err::NO_LIST;
    const std::string name = trimmed(rawName);
    if (!validName(name)) return Err::BAD_NAME;
    if (nameTaken(name, id)) return Err::DUPLICATE_NAME;
    p->name = name;
    return Err::OK;
}

Err Store::remove(uint16_t id)
{
    const auto it = std::find_if(m_lists.begin(), m_lists.end(), [id](const Playlist &p) { return p.id == id; });
    if (it == m_lists.end()) return Err::NO_LIST;
    m_lists.erase(it);
    return Err::OK;
}

Err Store::add(uint16_t id, const std::string &folder)
{
    Playlist *p = get(id);
    if (!p) return Err::NO_LIST;
    if (!validFolder(folder)) return Err::BAD_FOLDER;
    if (std::find(p->folders.begin(), p->folders.end(), folder) != p->folders.end()) return Err::OK;
    if (p->folders.size() >= MAX_SONGS) return Err::FULL;
    p->folders.push_back(folder);
    return Err::OK;
}

Err Store::removeSong(uint16_t id, const std::string &folder)
{
    Playlist *p = get(id);
    if (!p) return Err::NO_LIST;
    const auto it = std::find(p->folders.begin(), p->folders.end(), folder);
    if (it == p->folders.end()) return Err::NO_SONG;
    p->folders.erase(it);
    return Err::OK;
}

Err Store::move(uint16_t id, const std::string &folder, int delta)
{
    Playlist *p = get(id);
    if (!p) return Err::NO_LIST;
    const auto it = std::find(p->folders.begin(), p->folders.end(), folder);
    if (it == p->folders.end()) return Err::NO_SONG;
    const long from = it - p->folders.begin();
    const long to = from + (delta < 0 ? -1 : 1);
    if (to < 0 || to >= (long)p->folders.size()) return Err::OK;
    std::swap(p->folders[(size_t)from], p->folders[(size_t)to]);
    return Err::OK;
}

std::string Store::encode() const
{
    std::string out;
    for (const Playlist &p : m_lists) {
        out += "#\t" + std::to_string(p.id) + "\t" + p.name + "\n";
        for (const std::string &f : p.folders) out += f + "\n";
    }
    return out;
}

bool Store::decode(const char *data, size_t len)
{
    m_lists.clear();
    if (!data) return false;
    Playlist *cur = nullptr;
    size_t pos = 0;
    while (pos < len) {
        size_t eol = pos;
        while (eol < len && data[eol] != '\n') eol++;
        size_t end = eol;
        if (end > pos && data[end - 1] == '\r') end--;
        const std::string line(data + pos, end - pos);
        pos = eol + 1;
        if (line.empty()) continue;
        if (line.compare(0, 2, "#\t") == 0) {
            cur = nullptr;
            const size_t t2 = line.find('\t', 2);
            if (t2 == std::string::npos || m_lists.size() >= MAX_LISTS) continue;
            const long id = strtol(line.substr(2, t2 - 2).c_str(), nullptr, 10);
            const std::string name = line.substr(t2 + 1);
            if (id < 1 || id > UINT16_MAX || !validName(name) || find((uint16_t)id) || nameTaken(name, 0)) continue;
            Playlist p;
            p.id = (uint16_t)id;
            p.name = name;
            m_lists.push_back(std::move(p));
            cur = &m_lists.back();
        } else if (cur && validFolder(line) && cur->folders.size() < MAX_SONGS &&
                   std::find(cur->folders.begin(), cur->folders.end(), line) == cur->folders.end()) {
            cur->folders.push_back(line);
        }
    }
    return true;
}

}  // namespace SupernovaPlaylists
