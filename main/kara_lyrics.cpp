// kara_lyrics.cpp - see kara_lyrics.h.
#include "kara_lyrics.h"

#include <algorithm>
#include <limits>

namespace KaraLyrics {
namespace {

bool validUtf8(const char *s, size_t n)
{
    for (size_t i = 0; i < n;) {
        const uint8_t c = (uint8_t)s[i];
        if (c < 0x80) {
            if (c == 0 || c == 0x7F || (c < 0x20 && c != '\t' && c != '\r' && c != '\n')) return false;
            i++;
            continue;
        }
        uint32_t cp;
        size_t count;
        if (c >= 0xC2 && c <= 0xDF) cp = c & 0x1F, count = 2;
        else if (c >= 0xE0 && c <= 0xEF) cp = c & 0x0F, count = 3;
        else if (c >= 0xF0 && c <= 0xF4) cp = c & 0x07, count = 4;
        else return false;
        if (i + count > n) return false;
        for (size_t j = 1; j < count; j++) {
            const uint8_t d = (uint8_t)s[i + j];
            if ((d & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d & 0x3F);
        }
        if ((count == 3 && cp < 0x800) || (count == 4 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += count;
    }
    return true;
}

bool digits(const char *s, size_t n, uint32_t &value)
{
    if (!n) return false;
    value = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        const uint32_t digit = (uint32_t)(s[i] - '0');
        if (value > (std::numeric_limits<uint32_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return true;
}

bool digits64(const char *s, size_t n, uint64_t &value)
{
    if (!n) return false;
    value = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        const uint64_t digit = (uint64_t)(s[i] - '0');
        if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return true;
}

bool timestamp(const char *s, size_t n, size_t &end, uint64_t &millis)
{
    if (n < 2 || s[0] != '[') return false;
    size_t colon = 1;
    while (colon < n && s[colon] >= '0' && s[colon] <= '9') colon++;
    if (colon == 1 || colon >= n || s[colon] != ':') return false;
    size_t close = colon + 1;
    while (close < n && s[close] != ']') close++;
    if (close >= n) return false;
    const size_t secondsPartLen = close - colon - 1;
    size_t fraction = secondsPartLen;
    for (size_t i = 0; i < secondsPartLen; i++)
        if (s[colon + 1 + i] == '.') { fraction = i; break; }
    const size_t fractionLen = fraction < secondsPartLen ? secondsPartLen - fraction - 1 : 0;
    uint32_t minutes = 0, seconds = 0;
    if (fraction != 2 || (fraction < secondsPartLen && fractionLen != 2 && fractionLen != 3) ||
        !digits(s + 1, colon - 1, minutes) || !digits(s + colon + 1, 2, seconds) || seconds > 59)
        return false;
    uint32_t fractionalMillis = 0;
    if (fractionLen) {
        if (!digits(s + colon + 1 + fraction + 1, fractionLen, fractionalMillis)) return false;
        if (fractionLen == 2) fractionalMillis *= 10;
    }
    millis = ((uint64_t)minutes * 60 + seconds) * 1000 + fractionalMillis;
    end = close + 1;
    return true;
}

bool metadata(const std::string &line)
{
    if (line.size() < 4 || line[0] != '[') return false;
    const size_t colon = line.find(':');
    return colon != std::string::npos && colon > 1 && line.back() == ']';
}

bool parseOffset(const std::string &line, bool &negative, uint64_t &millis)
{
    static const char PREFIX[] = "[offset:";
    if (line.compare(0, sizeof(PREFIX) - 1, PREFIX) != 0 || line.empty() || line.back() != ']') return false;
    const size_t first = sizeof(PREFIX) - 1;
    if (line.size() <= first + 1) return false;
    size_t pos = first;
    negative = false;
    if (line[pos] == '-' || line[pos] == '+') {
        negative = line[pos] == '-';
        pos++;
    }
    return pos < line.size() - 1 && digits64(line.data() + pos, line.size() - pos - 1, millis);
}

std::string normalizeText(const char *s, size_t n)
{
    std::string text;
    text.reserve(n);
    for (size_t i = 0; i < n; i++) text.push_back(s[i] == '\t' || s[i] == '\r' ? ' ' : s[i]);
    while (!text.empty() && text.back() == ' ') text.pop_back();
    size_t first = 0;
    while (first < text.size() && text[first] == ' ') first++;
    if (first) text.erase(0, first);
    return text;
}

}  // namespace

Err parse(const char *data, size_t len, Document &out)
{
    out.clear();
    if (!data || len == 0) return Err::EMPTY;
    if (len > MAX_FILE) return Err::TOO_BIG;
    if (!validUtf8(data, len)) return Err::INVALID_UTF8;
    size_t start = len >= 3 && (uint8_t)data[0] == 0xEF && (uint8_t)data[1] == 0xBB &&
                           (uint8_t)data[2] == 0xBF ? 3 : 0;
    std::vector<std::string> raw;
    while (start < len) {
        size_t end = start;
        while (end < len && data[end] != '\n') end++;
        size_t lineEnd = end;
        if (lineEnd > start && data[lineEnd - 1] == '\r') lineEnd--;
        if (lineEnd - start > MAX_LINE_BYTES) return out.clear(), Err::LINE_TOO_LONG;
        raw.push_back(normalizeText(data + start, lineEnd - start));
        if (raw.size() > MAX_LINES) return out.clear(), Err::TOO_MANY_LINES;
        start = end + (end < len ? 1 : 0);
    }
    if (raw.size() > MAX_LINES) return out.clear(), Err::TOO_MANY_LINES;

    bool isLrc = false;
    for (const std::string &line : raw) {
        if ((line.size() > 1 && line[0] == '[' && line[1] >= '0' && line[1] <= '9') || metadata(line)) {
            isLrc = true;
            break;
        }
    }
    if (!isLrc) {
        bool hasText = false;
        for (const std::string &line : raw) {
            hasText = hasText || !line.empty();
            out.lines.push_back({0, line});
        }
        if (!hasText) return out.clear(), Err::EMPTY;
        return Err::OK;
    }

    out.timed = true;
    bool hasOffset = false, offsetNegative = false;
    uint64_t offsetMillis = 0;
    struct TimedLine {
        uint64_t millis;
        std::string text;
    };
    std::vector<TimedLine> timedLines;
    for (const std::string &line : raw) {
        if (line.compare(0, 8, "[offset:") == 0) {
            if (hasOffset || !parseOffset(line, offsetNegative, offsetMillis))
                return out.clear(), Err::BAD_LRC;
            hasOffset = true;
            continue;
        }
        size_t pos = 0;
        std::vector<uint64_t> times;
        while (pos < line.size() && line[pos] == '[') {
            size_t end = 0;
            uint64_t millis = 0;
            if (!timestamp(line.data() + pos, line.size() - pos, end, millis)) {
                if (!times.empty() || (pos + 1 < line.size() && line[pos + 1] >= '0' && line[pos + 1] <= '9'))
                    return out.clear(), Err::BAD_LRC;
                break;
            }
            times.push_back(millis);
            pos += end;
        }
        if (times.empty()) {
            if (!line.empty() && !metadata(line)) return out.clear(), Err::BAD_LRC;
            continue;
        }
        std::string text = normalizeText(line.data() + pos, line.size() - pos);
        for (uint64_t millis : times) timedLines.push_back({millis, text});
        if (timedLines.size() > MAX_LINES) return out.clear(), Err::TOO_MANY_LINES;
    }
    if (timedLines.empty()) return out.clear(), Err::EMPTY;
    const uint64_t maxMillis = std::numeric_limits<uint32_t>::max() / 1000;
    for (TimedLine &line : timedLines) {
        if (offsetNegative) line.millis = offsetMillis >= line.millis ? 0 : line.millis - offsetMillis;
        else {
            if (offsetMillis > maxMillis || line.millis > maxMillis - offsetMillis)
                return out.clear(), Err::BAD_LRC;
            line.millis += offsetMillis;
        }
        if (line.millis > maxMillis) return out.clear(), Err::BAD_LRC;
    }
    std::stable_sort(timedLines.begin(), timedLines.end(),
                     [](const TimedLine &a, const TimedLine &b) { return a.millis < b.millis; });
    for (const TimedLine &line : timedLines)
        out.lines.push_back({(uint32_t)(line.millis * 1000), line.text});
    return Err::OK;
}

}  // namespace KaraLyrics
