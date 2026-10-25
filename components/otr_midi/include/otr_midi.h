// otr_midi.h - Standard MIDI File (SMF type 0/1, .mid/.kar) parser, karaoke lyrics, melody-channel
// detection, a small song registry format and a tempo-scalable sequencer. Pure C++17 (no ESP-IDF),
// host tested in tests/test_midi.cpp.
//
// Lyrics: both common karaoke conventions are understood
//   - Soft Karaoke (.kar): FF 01 text events in one track ("Words"), "@T" title/artist headers,
//     "\" at the start = new paragraph, "/" = new line
//   - Lyric meta events (FF 05), line breaks as "\r" / "\n" (inside or as separate events)
// The track/type with the most events is used. Text that is not valid UTF-8 is read as Windows-1252
// (superset of Latin-1) and converted to UTF-8, so å/ä/ö show up with the Latin-1 UI font.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace Midi {

constexpr size_t MAX_FILE = 1024 * 1024;   // Larger files are refused (karaoke files are ~30-200 kB)
constexpr int DRUM_CH = 9;

struct Event {
    uint32_t us;      // Absolute song time in microseconds (tempo map applied)
    uint8_t status;   // 0x80..0xEF channel message (running status resolved)
    uint8_t d1, d2;
    uint8_t track;
};

enum SylFlag : uint8_t { SYL_LINE = 1, SYL_PARA = 2 };   // Syllable starts a new line / paragraph

struct Syllable {
    uint32_t us;
    uint32_t text;   // Offset into Song::text (UTF-8)
    uint16_t len;
    uint8_t flags;
};

struct Line {
    uint32_t startUs, endUs;   // endUs = start of the next line (or song end)
    uint32_t firstSyl;
    uint16_t sylCount;
    bool para;                 // First line of a paragraph (verse / chorus)
};

struct ChannelInfo {
    uint32_t notes;
    uint8_t program;        // First program change (GM 0..127)
    uint8_t track;          // Track holding most of the channel's notes
    uint8_t maxPoly;        // Most notes held at once
    uint8_t overlapPct;     // Note-ons that start while another note on the channel still sounds
    uint8_t lyricMatchPct;  // Syllables that have a note-on on this channel close by
    char trackName[24];
};

struct Song {
    std::vector<Event> events;
    std::vector<Syllable> syls;
    std::vector<Line> lines;
    std::string text;
    std::string title, artist;   // From "@T" headers or the first track name (may be empty)
    ChannelInfo ch[16] = {};
    uint16_t channelMask = 0;    // Channels with notes
    uint32_t lengthUs = 0;
    uint8_t format = 0;
    uint16_t tracks = 0, division = 0;
    int8_t melody = -1;          // Auto-detected melody channel, -1 = none
    int8_t lyricTrack = -1;
    bool lyricFromText = false;  // FF 01 (Soft Karaoke) instead of FF 05
    bool utf8 = false;           // Lyrics were already UTF-8

    void clear();
    int lineAt(uint32_t us) const;   // Line being sung at `us`: last line started (-1 before the first)
    int sylAt(uint32_t us) const;    // Last syllable started (-1 before the first)
    std::string lineText(int line) const;
    // Line text split at syllable `sylEnd` (exclusive): sung part, rest. Used for highlighting.
    void lineSplit(int line, int sylEnd, std::string &sung, std::string &rest) const;
};

enum class Err : uint8_t { OK, NOT_MIDI, TRUNCATED, UNSUPPORTED, TOO_BIG, EMPTY };
Err parse(const uint8_t *data, size_t len, Song &out);
const char *errName(Err e);

// Picks the channel that most likely carries the sung melody (track name, lyric alignment, mostly
// one note at a time). Fills ch[].lyricMatchPct. Called by parse().
int detectMelody(Song &s);

const char *gmName(uint8_t program);   // General MIDI instrument name ("Acoustic Grand Piano")
uint32_t crc32(const uint8_t *data, size_t len, uint32_t crc = 0);

// Converts Windows-1252 / Latin-1 bytes to UTF-8 (valid UTF-8 input is kept as is).
std::string toUtf8(const char *s, size_t n, bool *wasUtf8 = nullptr);

// Display name from a file name "Artist_Title.kar" -> artist "Artist", title "Title" (underscores in
// the title become spaces). Without '_' the whole stem is the title and artist is empty.
void nameFromFile(const std::string &fileName, std::string &title, std::string &artist);

// ---- Song registry -------------------------------------------------------------------------------
// One line per song, text file (karaoke.reg on the SD card), editable by hand:
//   <crc32 hex8> <size> <melody -1..15> <transpose -12..12> <tempo 50..150> <name...>
// Keyed by file content (CRC + size), so renaming or moving a file keeps its settings.
struct RegEntry {
    uint32_t crc = 0, size = 0;
    int8_t melody = -1;
    int8_t transpose = 0;
    uint8_t tempo = 100;
    std::string name;
};

class Registry {
public:
    void load(const std::string &text);   // Ignores broken lines and '#' comments
    std::string save() const;
    const RegEntry *find(uint32_t crc, uint32_t size) const;
    void set(const RegEntry &e);           // Insert or replace
    size_t size() const { return entries_.size(); }

private:
    std::vector<RegEntry> entries_;
};

// ---- Sequencer -------------------------------------------------------------------------------------
class Sink {
public:
    virtual ~Sink() = default;
    virtual void midi(uint8_t status, uint8_t d1, uint8_t d2) = 0;
    virtual void reset() = 0;   // All notes off now + controllers/programs back to defaults
};

class Player {
public:
    void load(const Song *s);              // Position 0, paused
    void play(bool on) { playing_ = on && song_; }
    bool playing() const { return playing_; }
    void setTempo(int pct);                // 50..150 %
    int tempo() const { return tempo_; }
    void advance(uint32_t realUs, Sink &sink);   // Dispatches due events while playing
    void seek(uint32_t songUs, Sink &sink);      // Resets the sink and chases programs/controllers
    uint32_t position() const { return (uint32_t)(pos100_ / 100); }
    bool finished() const;
    const Song *song() const { return song_; }

private:
    const Song *song_ = nullptr;
    size_t idx_ = 0;
    uint64_t pos100_ = 0;   // Song time in us * 100 (tempo scaling without floats)
    int tempo_ = 100;
    bool playing_ = false;
};

}  // namespace Midi
