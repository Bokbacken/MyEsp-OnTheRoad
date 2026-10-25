// kara_index.h - v0.13.7 song library for the karaoke module: a compact, sorted list of song files
// found under /sdcard/otr/karaoke (and its subfolders), the songs.idx file format and the folder /
// search / A-Ö queries used by the device UI and the web song picker.
//
// Pure C++ (no ESP-IDF calls) so it is host-tested (tests/test_karaindex.cpp). All strings live in
// one pool and the records in one array; on the device both are allocated in PSRAM (small
// std::string allocations would land in the scarce internal RAM: 2000 songs x 4 strings).
//
// Paths are stored relative to the karaoke root with '/' separators ("ABBA/Gold/ABBA_Waterloo.kar");
// the folder of a song is the part before the last '/' ("" = the root itself).
//
// songs.idx (little endian):
//   "OTRK" magic, u8 version, u32 scan time (unix), u64 SD free bytes, u32 root mtime, u32 count,
//   per song: u16 path length + path, u8 title length + title, u8 artist length + artist, u32 size,
//   u32 FNV-1a checksum of everything before it.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <string>
#include <vector>

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

namespace KaraIndex {

constexpr uint8_t VERSION = 1;
constexpr size_t MAX_SONGS = 2000;
constexpr size_t MAX_PATH = 255;   // Relative path bytes (FAT long names are max 255 per path here)

// Allocator: PSRAM on the device (falls back to the normal heap), malloc on the host.
template <class T> struct PsAlloc {
    using value_type = T;
    PsAlloc() = default;
    template <class U> PsAlloc(const PsAlloc<U> &) {}
    T *allocate(size_t n)
    {
        void *p = nullptr;
#if defined(ESP_PLATFORM)
        p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
        if (!p) p = malloc(n * sizeof(T));
        if (!p) abort();   // Same as the default allocator without exceptions
        return (T *)p;
    }
    void deallocate(T *p, size_t) { free(p); }
    template <class U> bool operator==(const PsAlloc<U> &) const { return true; }
    template <class U> bool operator!=(const PsAlloc<U> &) const { return false; }
};
template <class T> using PsVector = std::vector<T, PsAlloc<T>>;

struct Header {
    uint8_t version = VERSION;
    uint32_t scanTime = 0;     // Unix time of the scan (0 = clock not set)
    uint64_t freeBytes = 0;    // SD free space (without the log folder) when the index was written
    uint32_t rootMtime = 0;    // mtime of /sdcard/otr/karaoke
    uint32_t count = 0;
};

// Sort / A-Ö rank of the first letter: 0 = none, 1 = symbol, 10..19 = digit, 30..55 = a..z,
// 56 = å, 57 = ä (æ), 58 = ö (ø). Accented Latin letters rank as their base letter (é = e, ü = y).
uint16_t rankAt(const char *&p);   // Rank of the UTF-8 character at p; advances p
uint16_t firstRank(const char *s);
int collate(const char *a, const char *b);   // Swedish order, case-insensitive
std::string fold(const char *s);   // Lower case (ASCII + Latin-1 in UTF-8) for searching
bool safeRel(const char *rel);     // Relative path without "..", "\", empty parts or a leading '/'
bool songExt(const char *name);    // .mid .kar .midi .rmi

class Library {
public:
    void clear();
    // false when full (MAX_SONGS), the path is unsafe or too long
    bool add(const char *rel, const char *title, const char *artist, uint32_t size);
    void sort();   // By sort name (artist, or title when there is no artist), then title
    size_t size() const { return m_recs.size(); }
    bool full() const { return m_recs.size() >= MAX_SONGS; }

    const char *rel(size_t i) const { return &m_pool[m_recs[i].rel]; }
    const char *title(size_t i) const { return &m_pool[m_recs[i].title]; }
    const char *artist(size_t i) const { return &m_pool[m_recs[i].artist]; }
    uint32_t fileSize(size_t i) const { return m_recs[i].size; }
    std::string dir(size_t i) const { return std::string(rel(i), m_recs[i].dirLen); }
    const char *sortName(size_t i) const { return *artist(i) ? artist(i) : title(i); }
    uint16_t rank(size_t i) const { return firstRank(sortName(i)); }

    // Immediate subfolders of dir ("" = root), sorted (Swedish order)
    std::vector<std::string> subdirs(const std::string &dir) const;
    // Songs in dir and all its subfolders ("" = all songs), in library order
    std::vector<uint16_t> songsIn(const std::string &dir) const;
    // Case-insensitive substring match of every word of q in "artist title" (all folders)
    std::vector<uint16_t> search(const char *q) const;
    int find(const char *rel) const;   // -1 when missing

    void encode(const Header &h, std::vector<uint8_t> &out) const;   // h.count is taken from size()
    bool decode(const uint8_t *data, size_t len, Header &h);         // false = corrupt / other version

private:
    struct Rec {
        uint32_t rel, title, artist;   // Offsets into the pool (NUL-terminated)
        uint32_t size;
        uint16_t dirLen;               // Bytes of rel that form the folder
    };
    uint32_t put(const char *s, size_t n);
    PsVector<char> m_pool;
    PsVector<Rec> m_recs;
};

// Index of the first entry of view whose rank is >= r (A-Ö jump); view.size() when none
size_t jumpIndex(const Library &lib, const std::vector<uint16_t> &view, uint16_t r);

}  // namespace KaraIndex
