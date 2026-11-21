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
    std::string text;
    std::vector<Note> notes;
};

struct Document {
    std::string title;
    std::string artist;
    double bpm = 0;
    int32_t gapMs = 0;
    bool relative = false;
    std::vector<Phrase> phrases;
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

}  // namespace SupernovaLyrics
