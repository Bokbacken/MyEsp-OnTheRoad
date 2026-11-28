#pragma once

#include <stdint.h>

#include <string>
#include <vector>

namespace SupernovaLyrics {

constexpr size_t MAX_FILE_BYTES = 64 * 1024;
constexpr size_t MAX_NOTES = 4096;
constexpr size_t MAX_PHRASES = 1024;
constexpr size_t MAX_LINE_BYTES = 512;

enum class NoteKind : uint8_t { NORMAL, GOLDEN, FREESTYLE, RAP, GOLDEN_RAP };

struct Note {
    uint32_t startMs = 0;
    uint32_t endMs = 0;
    int8_t pitch = 0;
    NoteKind kind = NoteKind::NORMAL;
    std::string text;
};

struct Phrase {
    uint32_t startMs = 0;
    uint32_t endMs = 0;
    uint8_t singer = 0;   // 0 = solo / everyone, 1 = P1, 2 = P2 (duets)
    std::string text;
    std::vector<Note> notes;
};

struct Document {
    std::string title;
    std::string artist;
    double bpm = 0;
    int32_t gapMs = 0;
    bool relative = false;
    // Optional UltraStar tags. Times are milliseconds, converted from the file's units.
    uint32_t startMs = 0;        // #START: where in the audio the song begins
    uint32_t endMs = 0;          // #END: where the song ends (0 = play to the end)
    int32_t videoGapMs = 0;      // #VIDEOGAP: video runs this much ahead of the audio
    uint32_t previewMs = 0;      // #PREVIEWSTART
    bool duet = false;
    std::string audio, instrumental, vocals, cover, background;
    std::string genre, language, year, edition, tags, creator, p1, p2;
    std::vector<Phrase> phrases;   // Duets: both singers, ordered by start time
    void clear();
};

enum class Err : uint8_t {
    OK,
    EMPTY,
    TOO_BIG,
    INVALID_UTF8,
    BAD_HEADER,
    BAD_NOTE,
    UNSUPPORTED_VERSION,
    TOO_MANY_NOTES,
    TOO_MANY_PHRASES,
    LINE_TOO_LONG
};

Err parse(const char *data, size_t len, Document &out);
std::string toDisplayText(const std::string &text);
uint32_t phraseAt(const Document &doc, uint32_t timeMs);
uint32_t noteAt(const Phrase &phrase, uint32_t timeMs);

// Countdown dots for a long pause before the next phrase (same rules as MIDI karaoke): gaps of at
// least COUNTDOWN_GAP_MS show round(gap seconds) dots, at most COUNTDOWN_MAX; lit dots grow left to right.
constexpr uint32_t COUNTDOWN_GAP_MS = 3000;
constexpr int COUNTDOWN_MAX = 8;
struct Countdown {
    int dots = 0;   // 0 = no countdown
    int lit = 0;
};
Countdown countdownAt(const Document &doc, uint32_t timeMs);

}  // namespace SupernovaLyrics
