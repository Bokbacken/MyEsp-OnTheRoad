// supernova_index.h - song library for Supernova: a compact list of UltraStar song folders with
// the metadata needed to browse 1000+ songs (artist, title, genre, language, year), the
// supernova.idx cache format, sorting, filtering and search.
//
// Pure C++ (no ESP-IDF calls) so it is host-tested (tests/test_supernova_index.cpp). Strings share
// one pool in PSRAM on the device (see KaraIndex::PsAlloc). Media files are not stored: the folder
// name is enough to find them again when a song starts.
//
// supernova.idx (little endian):
//   "OTRS" magic, u8 version, u32 count,
//   per song: u16 folder length + folder, u8 length + string for artist, title, genre, language,
//   u16 year, u32 folder mtime, then a u32 FNV-1a checksum of everything before it.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "kara_index.h"

namespace SupernovaIndex {

constexpr uint8_t VERSION = 1;
constexpr size_t MAX_SONGS = 2000;
constexpr size_t MAX_FOLDER = 255;
constexpr size_t MAX_FIELD = 120;          // Bytes kept per metadata string
constexpr size_t TAG_SCAN_BYTES = 4096;    // Only the start of the .txt is read for the index

struct Tags {
    std::string title, artist, genre, language;
    uint16_t year = 0;   // 0 = unknown
    // Audio file names from the USDX 1.1.0 tags; used at play time only, not stored in the index.
    // #AUDIO supersedes #MP3, which is kept separately.
    std::string audio, mp3, instrumental, vocals;
};

// Reads the "#KEY:value" header lines (TITLE, ARTIST, GENRE, LANGUAGE, YEAR) and stops at the
// first note/body line. Values that are not valid UTF-8 are ignored. Never fails.
void parseTags(const char *data, size_t len, Tags &out);
// "Artist - Title" folder name -> artist/title (artist empty when there is no separator)
void namesFromFolder(const std::string &folder, std::string &artist, std::string &title);

enum class Sort : uint8_t { ARTIST, TITLE, YEAR };
enum class FilterKind : uint8_t { NONE, GENRE, LANGUAGE, DECADE };

struct Query {
    Sort sort = Sort::ARTIST;
    FilterKind kind = FilterKind::NONE;
    std::string value;   // Genre/language (case-insensitive) or decade start such as "1980"
    std::string text;    // Every word must occur in "artist title" (case-insensitive)
};

struct Value {
    std::string name;   // Shown to the user ("Pop", "1980s")
    std::string key;    // Goes into Query::value ("Pop", "1980")
    uint16_t count = 0;
};

class Library {
public:
    void clear();
    // false when full or the folder name is empty/too long
    bool add(const char *folder, const char *artist, const char *title, const char *genre,
             const char *language, uint16_t year, uint32_t mtime);
    size_t size() const { return m_recs.size(); }
    bool full() const { return m_recs.size() >= MAX_SONGS; }
    int find(const char *folder) const;   // -1 when missing

    const char *folder(size_t i) const { return &m_pool[m_recs[i].folder]; }
    const char *artist(size_t i) const { return &m_pool[m_recs[i].artist]; }
    const char *title(size_t i) const { return &m_pool[m_recs[i].title]; }
    const char *genre(size_t i) const { return &m_pool[m_recs[i].genre]; }
    const char *language(size_t i) const { return &m_pool[m_recs[i].language]; }
    uint16_t year(size_t i) const { return m_recs[i].year; }
    uint32_t mtime(size_t i) const { return m_recs[i].mtime; }

    // Indexes matching the query, in sort order
    std::vector<uint16_t> view(const Query &q) const;
    // Distinct genres/languages (a comma/semicolon separated list counts each name) or decades,
    // sorted; count = songs carrying the value
    std::vector<Value> values(FilterKind kind) const;
    // A-Ö rank used by the jump column for the sort (0 for Sort::YEAR)
    uint16_t rank(size_t i, Sort sort) const;

    void encode(std::vector<uint8_t> &out) const;
    bool decode(const uint8_t *data, size_t len);   // false = corrupt / other version (library cleared)

private:
    struct Rec {
        uint32_t folder, artist, title, genre, language;   // Pool offsets (NUL-terminated)
        uint32_t mtime;
        uint16_t year;
    };
    uint32_t put(const char *s, size_t maxLen);
    KaraIndex::PsVector<char> m_pool;
    KaraIndex::PsVector<Rec> m_recs;
};

// Index in view of the first entry whose rank is >= r (A-Ö jump); view.size() when none
size_t jumpIndex(const Library &lib, const std::vector<uint16_t> &view, Sort sort, uint16_t r);

}  // namespace SupernovaIndex
