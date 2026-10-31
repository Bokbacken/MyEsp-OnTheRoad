// kara_lyrics.h - bounded UTF-8 parser for plain-text and LRC lyric sidecars.
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

namespace KaraLyrics {

constexpr size_t MAX_FILE = 64 * 1024;
constexpr size_t MAX_LINES = 1024;
constexpr size_t MAX_LINE_BYTES = 512;

struct Line {
    uint32_t us = 0;    // LRC timestamp; ignored for plain text
    std::string text;
};

struct Document {
    bool timed = false;
    std::vector<Line> lines;
    void clear()
    {
        timed = false;
        lines.clear();
    }
};

enum class Err : uint8_t { OK, EMPTY, TOO_BIG, INVALID_UTF8, BAD_LRC, TOO_MANY_LINES, LINE_TOO_LONG };
Err parse(const char *data, size_t len, Document &out);

}  // namespace KaraLyrics
