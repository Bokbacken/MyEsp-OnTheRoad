#include "supernova_lyrics.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <limits>

namespace SupernovaLyrics {
namespace {

struct RawLine {
    const char *data;
    size_t size;
};

bool validUtf8(const char *data, size_t len)
{
    for (size_t i = 0; i < len;) {
        const uint8_t c = (uint8_t)data[i];
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
        if (i + count > len) return false;
        for (size_t j = 1; j < count; j++) {
            const uint8_t d = (uint8_t)data[i + j];
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

bool parseNumber(const char *begin, const char *end, double &out)
{
    std::string value(begin, end);
    std::replace(value.begin(), value.end(), ',', '.');
    char *tail = nullptr;
    out = strtod(value.c_str(), &tail);
    return tail != value.c_str() && *tail == '\0' && std::isfinite(out);
}

bool parseVersionPart(const char *begin, const char *end, uint32_t &out)
{
    if (begin == end) return false;
    out = 0;
    for (const char *p = begin; p < end; p++) {
        if (*p < '0' || *p > '9') return false;
        const uint8_t digit = (uint8_t)(*p - '0');
        if (out > (UINT32_MAX - digit) / 10) return false;
        out = out * 10 + digit;
    }
    return true;
}

bool readInteger(const char *&p, const char *end, int32_t &value)
{
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    if (p == end) return false;
    bool negative = false;
    if (*p == '+' || *p == '-') {
        negative = *p == '-';
        p++;
    }
    if (p == end || *p < '0' || *p > '9') return false;
    uint64_t parsed = 0;
    const uint64_t limit = negative ? (uint64_t)INT32_MAX + 1 : INT32_MAX;
    while (p < end && *p >= '0' && *p <= '9') {
        const uint8_t digit = (uint8_t)(*p - '0');
        if (parsed > (limit - digit) / 10) return false;
        parsed = parsed * 10 + digit;
        p++;
    }
    value = negative ? (parsed == (uint64_t)INT32_MAX + 1 ? INT32_MIN : -(int32_t)parsed)
                     : (int32_t)parsed;
    return true;
}

bool parseFlag(const char *p, const char *end, bool &value)
{
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) end--;
    const size_t len = (size_t)(end - p);
    if (len == 3 && (p[0] == 'y' || p[0] == 'Y') && (p[1] == 'e' || p[1] == 'E') &&
        (p[2] == 's' || p[2] == 'S')) {
        value = true;
        return true;
    }
    if (len == 2 && (p[0] == 'n' || p[0] == 'N') && (p[1] == 'o' || p[1] == 'O')) {
        value = false;
        return true;
    }
    return false;
}

// Beat -> milliseconds with BPM changes ("B beat bpm" lines): each segment keeps the time at its start.
struct Tempo {
    double beat, bpm, startMs;
};

bool timeFromTicks(const std::vector<Tempo> &tempo, double ticks, int32_t gap, uint32_t &out)
{
    size_t k = tempo.size() - 1;
    while (k > 0 && tempo[k].beat > ticks) k--;
    const double millis = tempo[k].startMs + (ticks - tempo[k].beat) * (60000.0 / (tempo[k].bpm * 4.0)) + gap;
    if (!std::isfinite(millis) || millis > UINT32_MAX) return false;
    if (millis < 0) {
        out = 0;
        return true;
    }
    out = (uint32_t)llround(millis);
    return true;
}

bool readDouble(const char *&p, const char *end, double &value)
{
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    const char *begin = p;
    while (p < end && *p != ' ' && *p != '\t') p++;
    return begin != p && parseNumber(begin, p, value);
}

// Optional numeric tag in seconds or milliseconds; bad values are ignored rather than rejecting the song.
bool scaledMs(const char *begin, const char *end, double scale, double &ms)
{
    double v = 0;
    if (!parseNumber(begin, end, v) || v * scale > INT32_MAX || v * scale < INT32_MIN) return false;
    ms = v * scale;
    return true;
}

bool iequals(const char *a, size_t aLen, const char *b)
{
    size_t bLen = 0;
    while (b[bLen]) bLen++;
    if (aLen != bLen) return false;
    for (size_t i = 0; i < aLen; i++) {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x -= 'a' - 'A';
        if (y >= 'a' && y <= 'z') y -= 'a' - 'A';
        if (x != y) return false;
    }
    return true;
}

bool parseNote(RawLine line, NoteKind &kind, Note &note)
{
    const char *p = line.data + 1;
    const char *end = line.data + line.size;
    int32_t start = 0, duration = 0, pitch = 0;
    if (!readInteger(p, end, start) || !readInteger(p, end, duration) ||
        !readInteger(p, end, pitch) || start < 0 || duration <= 0 || pitch < INT8_MIN || pitch > INT8_MAX)
        return false;
    if (p == end || (*p != ' ' && *p != '\t') || start > INT32_MAX - duration) return false;
    p++;
    if (p == end) return false;
    note.startMs = (uint32_t)start;
    note.endMs = (uint32_t)(start + duration);
    note.pitch = (int8_t)pitch;
    note.kind = kind;
    note.text.assign(p, end);
    return true;
}

}  // namespace

void Document::clear()
{
    title.clear();
    artist.clear();
    bpm = 0;
    gapMs = 0;
    relative = false;
    startMs = endMs = previewMs = 0;
    videoGapMs = 0;
    duet = false;
    for (std::string *s : {&audio, &instrumental, &vocals, &cover, &background, &genre, &language, &year,
                           &edition, &tags, &creator, &p1, &p2})
        s->clear();
    phrases.clear();
}

Err parse(const char *data, size_t len, Document &out)
{
    out.clear();
    if (!data || len == 0) return Err::EMPTY;
    if (len > MAX_FILE_BYTES) return Err::TOO_BIG;
    if (!validUtf8(data, len)) return Err::INVALID_UTF8;

    size_t pos = len >= 3 && (uint8_t)data[0] == 0xEF && (uint8_t)data[1] == 0xBB &&
                         (uint8_t)data[2] == 0xBF ? 3 : 0;

    bool body = false;
    bool hasVersion = false;
    bool hasBpm = false;
    uint32_t versionMajor = 1, versionMinor = 0, versionPatch = 0;
    int64_t relativeBase[3] = {0, 0, 0};   // Per singer: solo/P1/P2 each keep their own offset
    uint8_t singer = 0;
    std::vector<Tempo> tempo;
    size_t noteCount = 0;
    Phrase phrase;
    bool phraseHasNotes = false;

    auto flushPhrase = [&]() -> Err {
        if (!phraseHasNotes) return Err::OK;
        if (out.phrases.size() >= MAX_PHRASES) return Err::TOO_MANY_PHRASES;
        phrase.singer = singer;
        out.phrases.push_back(std::move(phrase));
        phrase = Phrase{};
        phraseHasNotes = false;
        return Err::OK;
    };

    while (pos < len) {
        size_t lineEnd = pos;
        while (lineEnd < len && data[lineEnd] != '\n') lineEnd++;
        size_t trimmedEnd = lineEnd;
        if (trimmedEnd > pos && data[trimmedEnd - 1] == '\r') trimmedEnd--;
        if (trimmedEnd - pos > MAX_LINE_BYTES) return out.clear(), Err::LINE_TOO_LONG;
        RawLine line{data + pos, trimmedEnd - pos};
        pos = lineEnd + (lineEnd < len ? 1 : 0);

        const char *p = line.data;
        const char *end = line.data + line.size;
        while (p < end && (*p == ' ' || *p == '\t')) p++;
        if (p == end) continue;

        if (!body && *p == '#') {
            const char *colon = p + 1;
            while (colon < end && *colon != ':') colon++;
            if (colon == end) return out.clear(), Err::BAD_HEADER;
            const char *key = p + 1;
            const size_t keyLen = (size_t)(colon - key);
            const char *value = colon + 1;
            while (value < end && (*value == ' ' || *value == '\t')) value++;
            const char *valueEnd = end;
            while (valueEnd > value && (valueEnd[-1] == ' ' || valueEnd[-1] == '\t')) valueEnd--;

            if (iequals(key, keyLen, "TITLE")) out.title.assign(value, valueEnd);
            else if (iequals(key, keyLen, "ARTIST")) out.artist.assign(value, valueEnd);
            else if (iequals(key, keyLen, "BPM")) {
                if (!parseNumber(value, valueEnd, out.bpm) || out.bpm <= 0 || out.bpm > 10000)
                    return out.clear(), Err::BAD_HEADER;
                hasBpm = true;
            } else if (iequals(key, keyLen, "GAP")) {
                double gap = 0;
                if (!parseNumber(value, valueEnd, gap) || gap < INT32_MIN || gap > INT32_MAX)
                    return out.clear(), Err::BAD_HEADER;
                out.gapMs = (int32_t)llround(gap);
            } else if (iequals(key, keyLen, "RELATIVE")) {
                if (!parseFlag(value, valueEnd, out.relative)) return out.clear(), Err::BAD_HEADER;
            } else if (iequals(key, keyLen, "VERSION")) {
                const char *dot1 = value;
                while (dot1 < valueEnd && *dot1 != '.') dot1++;
                const char *dot2 = dot1 < valueEnd ? dot1 + 1 : valueEnd;
                while (dot2 < valueEnd && *dot2 != '.') dot2++;
                const char *extraDot = dot2 < valueEnd ? dot2 + 1 : valueEnd;
                while (extraDot < valueEnd && *extraDot != '.') extraDot++;
                if (dot1 == valueEnd || dot2 == valueEnd || extraDot != valueEnd ||
                    !parseVersionPart(value, dot1, versionMajor) ||
                    !parseVersionPart(dot1 + 1, dot2, versionMinor) ||
                    !parseVersionPart(dot2 + 1, valueEnd, versionPatch))
                    return out.clear(), Err::UNSUPPORTED_VERSION;
                hasVersion = true;
            } else {
                auto text = [&](const char *name, std::string &dst) {
                    if (!iequals(key, keyLen, name)) return false;
                    dst.assign(value, valueEnd);
                    return true;
                };
                auto millis = [&](const char *name, double scale, uint32_t &dst) {
                    if (!iequals(key, keyLen, name)) return false;
                    double ms;
                    if (scaledMs(value, valueEnd, scale, ms) && ms > 0) dst = (uint32_t)llround(ms);
                    return true;
                };
                if (iequals(key, keyLen, "VIDEOGAP")) {
                    double ms;
                    if (scaledMs(value, valueEnd, 1000.0, ms)) out.videoGapMs = (int32_t)llround(ms);
                } else if (iequals(key, keyLen, "MP3")) {
                    if (out.audio.empty()) out.audio.assign(value, valueEnd);
                } else {
                    (void)(millis("START", 1000.0, out.startMs) || millis("END", 1.0, out.endMs) ||
                        millis("PREVIEWSTART", 1000.0, out.previewMs) || text("AUDIO", out.audio) ||
                        text("INSTRUMENTAL", out.instrumental) || text("VOCALS", out.vocals) ||
                        text("COVER", out.cover) || text("BACKGROUND", out.background) ||
                        text("GENRE", out.genre) || text("LANGUAGE", out.language) ||
                        text("YEAR", out.year) || text("EDITION", out.edition) || text("TAGS", out.tags) ||
                        text("CREATOR", out.creator) || text("P1", out.p1) || text("P2", out.p2));
                }
            }
            continue;
        }

        if (!body && !hasBpm) return out.clear(), Err::BAD_HEADER;
        if (!body) tempo.assign(1, Tempo{0, out.bpm, 0});
        body = true;
        if (*p == 'E') break;
        if (*p == 'P' && p + 1 < end && (p[1] == '1' || p[1] == '2')) {
            const Err result = flushPhrase();
            if (result != Err::OK) return out.clear(), result;
            singer = (uint8_t)(p[1] - '0');
            out.duet = true;
            continue;
        }
        if (*p == 'P' || *p == 'V') continue;
        if (*p == 'B') {
            double beat = 0, bpm = 0;
            const char *q = p + 1;
            if (!readDouble(q, end, beat) || !readDouble(q, end, bpm) || bpm <= 0 || bpm > 10000 || beat < 0)
                return out.clear(), Err::BAD_NOTE;
            if (out.relative) beat += (double)relativeBase[singer];
            if (beat > tempo.back().beat) {
                const double startMs = tempo.back().startMs +
                                       (beat - tempo.back().beat) * (60000.0 / (tempo.back().bpm * 4.0));
                tempo.push_back(Tempo{beat, bpm, startMs});
            } else if (beat == tempo.back().beat) {
                tempo.back().bpm = bpm;
            }
            continue;
        }
        if (*p == '-') {
            int32_t lineStart = 0, nextOffset = 0;
            p++;
            if (!readInteger(p, end, lineStart) || lineStart < 0) return out.clear(), Err::BAD_NOTE;
            const bool hasOffset = readInteger(p, end, nextOffset);
            const int64_t absolute = relativeBase[singer] + lineStart;
            if (phraseHasNotes) {
                uint32_t lineEndMs = 0;
                if (!timeFromTicks(tempo, (double)absolute, out.gapMs, lineEndMs))
                    return out.clear(), Err::BAD_NOTE;
                phrase.endMs = std::max(phrase.endMs, lineEndMs);
            }
            const Err result = flushPhrase();
            if (result != Err::OK) return out.clear(), result;
            if (out.relative) relativeBase[singer] = hasOffset ? relativeBase[singer] + nextOffset : absolute;
            continue;
        }

        NoteKind kind;
        switch (*p) {
            case ':': kind = NoteKind::NORMAL; break;
            case '*': kind = NoteKind::GOLDEN; break;
            case 'F': case 'f': kind = NoteKind::FREESTYLE; break;
            case 'R': case 'r': kind = NoteKind::RAP; break;
            case 'G': case 'g': kind = NoteKind::GOLDEN_RAP; break;
            default:
                if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'))
                    kind = NoteKind::FREESTYLE;
                else return out.clear(), Err::BAD_NOTE;
        }
        Note note;
        if (!parseNote(line, kind, note)) return out.clear(), Err::BAD_NOTE;
        if (++noteCount > MAX_NOTES) return out.clear(), Err::TOO_MANY_NOTES;
        const int64_t absoluteStart = relativeBase[singer] + note.startMs;
        const int64_t absoluteEnd = relativeBase[singer] + note.endMs;
        if (!timeFromTicks(tempo, (double)absoluteStart, out.gapMs, note.startMs) ||
            !timeFromTicks(tempo, (double)absoluteEnd, out.gapMs, note.endMs))
            return out.clear(), Err::BAD_NOTE;
        if (!phraseHasNotes) {
            phrase.startMs = note.startMs;
            phrase.endMs = note.endMs;
            phraseHasNotes = true;
        } else {
            phrase.startMs = std::min(phrase.startMs, note.startMs);
            phrase.endMs = std::max(phrase.endMs, note.endMs);
        }
        phrase.text += note.text;
        phrase.notes.push_back(std::move(note));
    }

    if (hasVersion && versionMajor != 1) return out.clear(), Err::UNSUPPORTED_VERSION;
    if (!hasBpm) return out.clear(), Err::BAD_HEADER;
    const Err result = flushPhrase();
    if (result != Err::OK) return out.clear(), result;
    if (out.phrases.empty()) return out.clear(), Err::EMPTY;
    if (out.duet)
        std::stable_sort(out.phrases.begin(), out.phrases.end(),
                         [](const Phrase &a, const Phrase &b) { return a.startMs < b.startMs; });
    return Err::OK;
}

std::string toDisplayText(const std::string &text)
{
    std::string display;
    display.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        if (i + 2 < text.size() && (uint8_t)text[i] == 0xE2 && (uint8_t)text[i + 1] == 0x80 &&
            ((uint8_t)text[i + 2] == 0x98 || (uint8_t)text[i + 2] == 0x99)) {
            display.push_back('\'');
            i += 3;
        } else if (i + 2 < text.size() && (uint8_t)text[i] == 0xE2 && (uint8_t)text[i + 1] == 0x80 &&
                   ((uint8_t)text[i + 2] == 0x9C || (uint8_t)text[i + 2] == 0x9D)) {
            display.push_back('"');
            i += 3;
        } else {
            display.push_back(text[i++]);
        }
    }
    return display;
}

uint32_t phraseAt(const Document &doc, uint32_t timeMs)
{
    if (doc.phrases.empty()) return 0;
    for (size_t i = 0; i < doc.phrases.size(); i++)
        if (timeMs < doc.phrases[i].endMs) return (uint32_t)i;
    return (uint32_t)(doc.phrases.size() - 1);
}

Countdown countdownAt(const Document &doc, uint32_t timeMs)
{
    Countdown cd;
    if (doc.phrases.empty()) return cd;
    const size_t next = phraseAt(doc, timeMs);
    const uint32_t nextStart = doc.phrases[next].startMs;
    if (timeMs >= nextStart) return cd;
    const uint32_t gapStart = next > 0 ? doc.phrases[next - 1].endMs : 0;
    if (timeMs < gapStart || nextStart <= gapStart || nextStart - gapStart < COUNTDOWN_GAP_MS) return cd;
    int dots = (int)((nextStart - gapStart + 500) / 1000);
    if (dots > COUNTDOWN_MAX) dots = COUNTDOWN_MAX;
    const int remainSec = (int)((nextStart - timeMs + 999) / 1000);
    cd.dots = dots;
    cd.lit = std::max(0, dots - remainSec);
    return cd;
}

uint32_t noteAt(const Phrase &phrase, uint32_t timeMs)
{
    for (size_t i = 0; i < phrase.notes.size(); i++)
        if (timeMs < phrase.notes[i].endMs) return (uint32_t)i;
    return phrase.notes.empty() ? 0 : (uint32_t)(phrase.notes.size() - 1);
}

}  // namespace SupernovaLyrics
