#include "kara_settings.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>

namespace KaraSettings {
namespace {

bool parseUnsigned(const std::string &s, unsigned &value)
{
    if (s.empty()) return false;
    unsigned n = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        const unsigned digit = (unsigned)(c - '0');
        if (n > (10000 - digit) / 10) return false;
        n = n * 10 + digit;
    }
    value = n;
    return true;
}

bool parseSigned(const std::string &s, int &value)
{
    if (s.empty()) return false;
    size_t i = 0;
    bool negative = false;
    if (s[0] == '-') negative = true, i++;
    if (i == s.size()) return false;
    unsigned n = 0;
    if (!parseUnsigned(s.substr(i), n)) return false;
    value = negative ? -(int)n : (int)n;
    return true;
}

bool decodeString(const std::string &value, std::string &out)
{
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') return false;
    out.clear();
    for (size_t i = 1; i + 1 < value.size(); i++) {
        char c = value[i];
        if (c == '\\') {
            if (++i + 1 >= value.size() || (value[i] != '\\' && value[i] != '"')) return false;
            c = value[i];
        }
        if ((unsigned char)c < 0x20 || c == 0x7f) return false;
        out.push_back(c);
    }
    return true;
}

bool encodeString(const std::string &value, std::string &out)
{
    out = "\"";
    for (char c : value) {
        if ((unsigned char)c < 0x20 || c == 0x7f) return false;
        if (c == '\\' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return true;
}

bool inRange(unsigned value, unsigned lo, unsigned hi) { return value >= lo && value <= hi; }

}  // namespace

bool validSoundfont(const std::string &name)
{
    if (name.empty()) return true;
    if (name.size() > 47 || name == "." || name == "..") return false;
    for (unsigned char c : name)
        if (c < 0x20 || c == 0x7f || c == '/' || c == '\\' || c == ':') return false;
    const size_t dot = name.find_last_of('.');
    return dot != std::string::npos && name.size() - dot == 4 &&
           tolower((unsigned char)name[dot + 1]) == 's' &&
           tolower((unsigned char)name[dot + 2]) == 'f' &&
           name[dot + 3] == '2';
}

Err parse(const char *data, size_t size, Song &out)
{
    if (size > MAX_FILE_SIZE) return Err::TOO_BIG;
    if (!data) return Err::INVALID;
    Song next;
    bool versionSeen = false;
    bool soundfontSeen = false, midiVolumeSeen = false, melodyChannelSeen = false;
    bool melodyMutedSeen = false, melodyGainSeen = false, tempoSeen = false, transposeSeen = false;
    bool headerSeen = false;
    size_t maps = 0;
    size_t pos = 0;
    while (pos < size) {
        size_t end = pos;
        while (end < size && data[end] != '\n') end++;
        size_t len = end - pos;
        if (len && data[pos + len - 1] == '\r') len--;
        if (len > 255) return Err::INVALID;
        std::string line(data + pos, len);
        pos = end < size ? end + 1 : size;
        size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;
        line = line.substr(first);
        const size_t equal = line.find('=');
        if (equal == std::string::npos || equal == 0) return Err::INVALID;
        const std::string key = line.substr(0, equal);
        const std::string value = line.substr(equal + 1);
        unsigned n = 0;
        int signedValue = 0;
        if (key == "version") {
            if (versionSeen || !parseUnsigned(value, n) || n != 1) return Err::INVALID;
            versionSeen = true;
        } else if (key == "soundfont") {
            if (soundfontSeen || !decodeString(value, next.soundfont) || !validSoundfont(next.soundfont))
                return Err::INVALID;
            soundfontSeen = true;
        } else if (key == "midi_volume") {
            if (midiVolumeSeen || !parseUnsigned(value, n) || !inRange(n, 0, 200)) return Err::INVALID;
            next.midiVolume = (uint8_t)n;
            midiVolumeSeen = true;
        } else if (key == "melody_channel") {
            if (melodyChannelSeen) return Err::INVALID;
            if (value == "none") next.melodyChannel = -1;
            else if (!parseUnsigned(value, n) || !inRange(n, 1, 16)) return Err::INVALID;
            else next.melodyChannel = (int8_t)(n - 1);
            melodyChannelSeen = true;
        } else if (key == "melody_muted") {
            if (melodyMutedSeen || (value != "true" && value != "false")) return Err::INVALID;
            next.melodyMuted = value == "true";
            melodyMutedSeen = true;
        } else if (key == "melody_gain_pct") {
            if (melodyGainSeen || !parseUnsigned(value, n) || !inRange(n, 0, 200)) return Err::INVALID;
            next.melodyGainPct = (uint8_t)n;
            melodyGainSeen = true;
        } else if (key == "tempo") {
            if (tempoSeen || !parseUnsigned(value, n) || !inRange(n, 50, 150)) return Err::INVALID;
            next.tempo = (uint8_t)n;
            tempoSeen = true;
        } else if (key == "transpose") {
            if (transposeSeen || !parseSigned(value, signedValue) || signedValue < -12 || signedValue > 12)
                return Err::INVALID;
            next.transpose = (int8_t)signedValue;
            transposeSeen = true;
        } else if (key == "instrument_map") {
            if (++maps > MAX_INSTRUMENT_MAPS) return Err::INVALID;
            unsigned ch, source, replacement;
            const size_t comma1 = value.find(',');
            const size_t comma2 = comma1 == std::string::npos ? comma1 : value.find(',', comma1 + 1);
            if (comma1 == std::string::npos || comma2 == std::string::npos ||
                value.find(',', comma2 + 1) != std::string::npos ||
                !parseUnsigned(value.substr(0, comma1), ch) ||
                !parseUnsigned(value.substr(comma1 + 1, comma2 - comma1 - 1), source) ||
                !parseUnsigned(value.substr(comma2 + 1), replacement) ||
                !inRange(ch, 1, 16) || !inRange(source, 1, 128) || !inRange(replacement, 1, 128))
                return Err::INVALID;
            InstrumentMap m{(uint8_t)(ch - 1), (uint8_t)(source - 1), (uint8_t)(replacement - 1)};
            for (const InstrumentMap &old : next.instrumentMaps)
                if (old.channel == m.channel && old.sourceProgram == m.sourceProgram) return Err::INVALID;
            next.instrumentMaps.push_back(m);
        } else {
            return Err::INVALID;
        }
        headerSeen = true;
    }
    if (!headerSeen || !versionSeen) return Err::INVALID;
    out = std::move(next);
    return Err::OK;
}

Err serialize(const Song &song, std::string &out)
{
    if (!validSoundfont(song.soundfont) || song.midiVolume > 200 ||
        song.melodyChannel < -2 || song.melodyChannel > 15 || song.melodyGainPct > 200 ||
        song.tempo < 50 || song.tempo > 150 || song.transpose < -12 || song.transpose > 12 ||
        song.instrumentMaps.size() > MAX_INSTRUMENT_MAPS)
        return Err::INVALID;
    bool seen[16][128] = {};
    for (const InstrumentMap &m : song.instrumentMaps) {
        if (m.channel > 15 || m.sourceProgram > 127 || m.replacementProgram > 127 ||
            seen[m.channel][m.sourceProgram])
            return Err::INVALID;
        seen[m.channel][m.sourceProgram] = true;
    }
    std::string text = "# MyEsp-OnTheRoad karaoke settings v1\nversion=1\n";
    if (!song.soundfont.empty()) {
        std::string encoded;
        if (!encodeString(song.soundfont, encoded)) return Err::INVALID;
        text += "soundfont=" + encoded + "\n";
    }
    if (song.midiVolume != 100) text += "midi_volume=" + std::to_string(song.midiVolume) + "\n";
    if (song.melodyChannel == -1) text += "melody_channel=none\n";
    else if (song.melodyChannel >= 0) text += "melody_channel=" + std::to_string(song.melodyChannel + 1) + "\n";
    if (song.melodyMuted) text += "melody_muted=true\n";
    if (song.melodyGainPct != 100) text += "melody_gain_pct=" + std::to_string(song.melodyGainPct) + "\n";
    if (song.tempo != 100) text += "tempo=" + std::to_string(song.tempo) + "\n";
    if (song.transpose) text += "transpose=" + std::to_string(song.transpose) + "\n";
    for (const InstrumentMap &m : song.instrumentMaps)
        text += "instrument_map=" + std::to_string(m.channel + 1) + "," +
                std::to_string(m.sourceProgram + 1) + "," +
                std::to_string(m.replacementProgram + 1) + "\n";
    if (text.size() > MAX_FILE_SIZE) return Err::TOO_BIG;
    out = std::move(text);
    return Err::OK;
}

}  // namespace KaraSettings
