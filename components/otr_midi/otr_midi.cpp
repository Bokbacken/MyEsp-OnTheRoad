// otr_midi.cpp - see otr_midi.h.
#include "otr_midi.h"

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

namespace Midi {

namespace {

struct RawEv {
    uint32_t tick;
    uint32_t order;    // File order inside the track (stable sort key)
    uint8_t track;
    uint8_t kind;      // 0 channel message, 1 tempo, 2 text (FF01), 3 lyric (FF05)
    uint8_t st, d1, d2;
    uint32_t tempo;    // kind 1: us per quarter note
    uint32_t textOff;  // kind 2/3: offset in the raw text pool
    uint16_t textLen;
};

struct TextEv {
    uint32_t us;
    uint8_t track;
    bool lyric;   // FF05
    uint32_t off;
    uint16_t len;
};

uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

bool readVar(const uint8_t *&p, const uint8_t *end, uint32_t &v)
{
    v = 0;
    for (int i = 0; i < 4; i++) {
        if (p >= end) return false;
        uint8_t b = *p++;
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) return true;
    }
    return false;
}

int dataBytes(uint8_t status)
{
    switch (status & 0xF0) {
    case 0xC0:
    case 0xD0: return 1;
    default: return 2;
    }
}

bool validUtf8(const char *s, size_t n, bool &multi)
{
    multi = false;
    for (size_t i = 0; i < n;) {
        uint8_t c = (uint8_t)s[i];
        int k = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
        if (k < 0 || (k == 1 && c < 0xC2) || i + k >= n + (k ? 0 : 1)) return false;
        for (int j = 1; j <= k; j++)
            if (i + j >= n || ((uint8_t)s[i + j] & 0xC0) != 0x80) return false;
        if (k) multi = true;
        i += (size_t)k + 1;
    }
    return true;
}

void putUtf8(std::string &o, uint32_t cp)
{
    if (cp < 0x80) {
        o += (char)cp;
    } else if (cp < 0x800) {
        o += (char)(0xC0 | (cp >> 6));
        o += (char)(0x80 | (cp & 0x3F));
    } else {
        o += (char)(0xE0 | (cp >> 12));
        o += (char)(0x80 | ((cp >> 6) & 0x3F));
        o += (char)(0x80 | (cp & 0x3F));
    }
}

// Windows-1252 0x80..0x9F, folded to characters the Latin-1 UI font has (quotes, dashes, ...).
const char *cp1252Fold(uint8_t c)
{
    switch (c) {
    case 0x82: case 0x91: case 0x92: return "'";
    case 0x84: case 0x93: case 0x94: return "\"";
    case 0x85: return "...";
    case 0x96: case 0x97: return "-";
    case 0x8B: return "<";
    case 0x9B: return ">";
    case 0x80: return "E";
    default: return "";
    }
}

bool nameIsMelody(const char *n)
{
    char low[32];
    size_t i = 0;
    for (; n[i] && i < sizeof(low) - 1; i++) low[i] = (char)((n[i] >= 'A' && n[i] <= 'Z') ? n[i] + 32 : n[i]);
    low[i] = 0;
    static const char *const KEYS[] = {"melod", "vocal", "voice", "sang", "s\xe5ng", "lead vox", "singer", "sing"};
    for (const char *k : KEYS)
        if (strstr(low, k)) return true;
    return false;
}

}  // namespace

std::string toUtf8(const char *s, size_t n, bool *wasUtf8)
{
    bool multi = false;
    if (validUtf8(s, n, multi)) {
        if (wasUtf8) *wasUtf8 = multi;
        return std::string(s, n);
    }
    if (wasUtf8) *wasUtf8 = false;
    std::string o;
    o.reserve(n + 8);
    for (size_t i = 0; i < n; i++) {
        uint8_t c = (uint8_t)s[i];
        if (c >= 0x80 && c < 0xA0) o += cp1252Fold(c);
        else putUtf8(o, c);
    }
    return o;
}

const char *errName(Err e)
{
    switch (e) {
    case Err::OK: return "ok";
    case Err::NOT_MIDI: return "not a MIDI file";
    case Err::TRUNCATED: return "truncated / broken file";
    case Err::UNSUPPORTED: return "unsupported MIDI format";
    case Err::TOO_BIG: return "file too large";
    case Err::EMPTY: return "no notes";
    }
    return "?";
}

void Song::clear() { *this = Song(); }

int Song::sylAt(uint32_t us) const
{
    // Binary search: last syllable with start <= us
    int lo = 0, hi = (int)syls.size() - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (syls[(size_t)mid].us <= us) best = mid, lo = mid + 1;
        else hi = mid - 1;
    }
    return best;
}

int Song::lineAt(uint32_t us) const
{
    int lo = 0, hi = (int)lines.size() - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (lines[(size_t)mid].startUs <= us) best = mid, lo = mid + 1;
        else hi = mid - 1;
    }
    return best;
}

std::string Song::lineText(int line) const
{
    std::string a, b;
    lineSplit(line, -1, a, b);
    return b;
}

void Song::lineSplit(int line, int sylEnd, std::string &sung, std::string &rest) const
{
    sung.clear();
    rest.clear();
    if (line < 0 || line >= (int)lines.size()) return;
    const Line &l = lines[(size_t)line];
    for (uint32_t i = l.firstSyl; i < l.firstSyl + l.sylCount; i++) {
        const Syllable &s = syls[i];
        std::string &dst = ((int)i < sylEnd) ? sung : rest;
        dst.append(text, s.text, s.len);
    }
}

// ---------------------------------------------------------------------------------------------
Err parse(const uint8_t *data, size_t len, Song &out)
{
    out.clear();
    if (len > MAX_FILE) return Err::TOO_BIG;
    // RIFF-wrapped MIDI (.rmi): skip to the "MThd" chunk
    if (len >= 20 && !memcmp(data, "RIFF", 4) && !memcmp(data + 8, "RMID", 4)) {
        for (size_t i = 12; i + 8 <= len; i++)
            if (!memcmp(data + i, "MThd", 4)) { data += i; len -= i; break; }
    }
    if (len < 14 || memcmp(data, "MThd", 4) != 0) return Err::NOT_MIDI;
    uint32_t hlen = be32(data + 4);
    if (hlen < 6 || 8 + (size_t)hlen > len) return Err::TRUNCATED;
    out.format = (uint8_t)be16(data + 8);
    out.tracks = be16(data + 10);
    out.division = be16(data + 12);
    if (out.format > 1 || out.division == 0) return Err::UNSUPPORTED;

    std::vector<RawEv> raw;
    std::string pool;      // Raw text bytes
    std::vector<std::string> trackNames;
    raw.reserve(len / 3);
    const uint8_t *p = data + 8 + hlen, *end = data + len;
    uint16_t track = 0;
    while (p + 8 <= end && track < 255) {
        uint32_t clen = be32(p + 4);
        const bool isTrack = !memcmp(p, "MTrk", 4);
        p += 8;
        const uint8_t *tend = (clen > (uint32_t)(end - p)) ? end : p + clen;   // Tolerate a short last track
        if (!isTrack) { p = tend; continue; }
        trackNames.emplace_back();
        uint32_t tick = 0, order = 0;
        uint8_t running = 0;
        while (p < tend) {
            uint32_t delta;
            if (!readVar(p, tend, delta)) break;
            tick += delta;
            if (p >= tend) break;
            uint8_t b = *p;
            if (b == 0xFF) {
                if (p + 2 > tend) break;
                uint8_t type = p[1];
                p += 2;
                uint32_t mlen;
                if (!readVar(p, tend, mlen) || mlen > (uint32_t)(tend - p)) { p = tend; break; }
                if (type == 0x51 && mlen == 3) {
                    RawEv e{};
                    e.tick = tick, e.order = order++, e.track = (uint8_t)track, e.kind = 1;
                    e.tempo = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
                    raw.push_back(e);
                } else if ((type == 0x01 || type == 0x05) && mlen > 0) {
                    RawEv e{};
                    e.tick = tick, e.order = order++, e.track = (uint8_t)track, e.kind = type == 0x05 ? 3 : 2;
                    e.textOff = (uint32_t)pool.size();
                    e.textLen = (uint16_t)(mlen > 255 ? 255 : mlen);
                    pool.append((const char *)p, e.textLen);
                    raw.push_back(e);
                } else if (type == 0x03 && trackNames.back().empty()) {
                    trackNames.back().assign((const char *)p, mlen > 40 ? 40 : mlen);
                } else if (type == 0x2F) {
                    p += mlen;
                    break;
                }
                p += mlen;
                running = 0;
            } else if (b == 0xF0 || b == 0xF7) {
                p++;
                uint32_t slen;
                if (!readVar(p, tend, slen) || slen > (uint32_t)(tend - p)) { p = tend; break; }
                p += slen;
                running = 0;
            } else {
                uint8_t st;
                if (b & 0x80) {
                    st = b;
                    p++;
                    if (st >= 0xF0) { running = 0; continue; }   // System common: skip the status byte
                    running = st;
                } else if (running) {
                    st = running;
                } else {
                    p++;   // Data byte without status: skip
                    continue;
                }
                int n = dataBytes(st);
                if (p + n > tend) { p = tend; break; }
                RawEv e{};
                e.tick = tick, e.order = order++, e.track = (uint8_t)track, e.kind = 0;
                e.st = st;
                e.d1 = p[0] & 0x7F;
                e.d2 = n == 2 ? (p[1] & 0x7F) : 0;
                p += n;
                raw.push_back(e);
            }
        }
        p = tend;
        track++;
    }
    if (track == 0) return Err::TRUNCATED;

    std::stable_sort(raw.begin(), raw.end(), [](const RawEv &a, const RawEv &b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.track != b.track) return a.track < b.track;
        return a.order < b.order;
    });

    // Tempo map -> microseconds
    const bool smpte = out.division & 0x8000;
    double usPerTick;
    if (smpte) {
        int fps = -(int8_t)(out.division >> 8);
        int tpf = out.division & 0xFF;
        if (fps <= 0 || tpf <= 0) return Err::UNSUPPORTED;
        usPerTick = 1e6 / ((fps == 29 ? 29.97 : fps) * tpf);
    } else {
        usPerTick = 500000.0 / out.division;
    }
    double usAt = 0;
    uint32_t lastTick = 0;
    std::vector<TextEv> texts;
    out.events.reserve(raw.size());
    uint8_t held[16][128] = {};
    int heldCount[16] = {};
    uint32_t chTrackNotes[16][8] = {};   // Notes per channel for the first 8 tracks of interest
    uint8_t chTrackIdx[16][8];
    memset(chTrackIdx, 0xFF, sizeof(chTrackIdx));
    uint32_t overlaps[16] = {};
    bool progSeen[16] = {};
    for (const RawEv &r : raw) {
        usAt += (double)(r.tick - lastTick) * usPerTick;
        lastTick = r.tick;
        const uint32_t us = usAt > 4.0e9 ? 4000000000u : (uint32_t)usAt;
        if (r.kind == 1) {
            if (!smpte && r.tempo > 0) usPerTick = (double)r.tempo / out.division;
            continue;
        }
        if (r.kind >= 2) {
            texts.push_back({us, r.track, r.kind == 3, r.textOff, r.textLen});
            continue;
        }
        Event e{us, r.st, r.d1, r.d2, r.track};
        const int ch = r.st & 0x0F;
        const uint8_t type = r.st & 0xF0;
        if (type == 0x90 && r.d2 > 0) {
            ChannelInfo &ci = out.ch[ch];
            ci.notes++;
            out.channelMask |= (uint16_t)(1u << ch);
            if (heldCount[ch] > 0) overlaps[ch]++;
            if (held[ch][r.d1] < 255) held[ch][r.d1]++;
            heldCount[ch]++;
            if (heldCount[ch] > ci.maxPoly) ci.maxPoly = (uint8_t)(heldCount[ch] > 255 ? 255 : heldCount[ch]);
            for (int k = 0; k < 8; k++) {
                if (chTrackIdx[ch][k] == r.track || chTrackIdx[ch][k] == 0xFF) {
                    chTrackIdx[ch][k] = r.track;
                    chTrackNotes[ch][k]++;
                    break;
                }
            }
        } else if (type == 0x80 || (type == 0x90 && r.d2 == 0)) {
            if (held[ch][r.d1] > 0) {
                held[ch][r.d1]--;
                heldCount[ch]--;
            }
        } else if (type == 0xC0 && !progSeen[ch]) {
            progSeen[ch] = true;
            out.ch[ch].program = r.d1;
        }
        out.events.push_back(e);
        if (us > out.lengthUs) out.lengthUs = us;
    }
    for (int ch = 0; ch < 16; ch++) {
        ChannelInfo &ci = out.ch[ch];
        if (!ci.notes) continue;
        ci.overlapPct = (uint8_t)(overlaps[ch] * 100 / ci.notes);
        int best = 0;
        for (int k = 1; k < 8; k++)
            if (chTrackIdx[ch][k] != 0xFF && chTrackNotes[ch][k] > chTrackNotes[ch][best]) best = k;
        ci.track = chTrackIdx[ch][best];
        if (ci.track < trackNames.size()) {
            std::string n = toUtf8(trackNames[ci.track].data(), trackNames[ci.track].size());
            while (!n.empty() && (n.back() == ' ' || n.back() == '\0')) n.pop_back();
            snprintf(ci.trackName, sizeof(ci.trackName), "%s", n.c_str());
        }
    }
    if (!out.channelMask) return Err::EMPTY;
    out.lengthUs += 500000;   // Let the last notes ring

    // ---- Lyrics: pick the track/type with the most events ----
    int bestTrack = -1;
    bool bestLyric = false;
    size_t bestCount = 0;
    for (int pass = 0; pass < 2; pass++) {
        const bool lyric = pass == 0;
        std::vector<size_t> cnt(track, 0);
        for (const TextEv &t : texts)
            if (t.lyric == lyric && pool[t.off] != '@') cnt[t.track]++;
        for (int tr = 0; tr < track; tr++)
            if (cnt[(size_t)tr] > bestCount) bestCount = cnt[(size_t)tr], bestTrack = tr, bestLyric = lyric;
    }
    // Header info (@T title, artist) from any text event
    for (const TextEv &t : texts) {
        if (t.len >= 2 && pool[t.off] == '@' && pool[t.off + 1] == 'T') {
            std::string s = toUtf8(pool.data() + t.off + 2, t.len - 2u);
            while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
            if (out.title.empty()) out.title = s;
            else if (out.artist.empty()) out.artist = s;
        }
    }
    if (bestTrack >= 0 && bestCount >= 4) {
        out.lyricTrack = (int8_t)bestTrack;
        out.lyricFromText = !bestLyric;
        // Encoding is decided for the whole lyric track
        std::string all;
        for (const TextEv &t : texts)
            if (t.track == bestTrack && t.lyric == bestLyric) all.append(pool, t.off, t.len);
        bool multi = false;
        const bool isUtf8 = validUtf8(all.data(), all.size(), multi) && multi;
        out.utf8 = isUtf8;
        int brk = 0;   // Pending breaks before the next syllable: 1 = new line, 2+ = new paragraph
        auto emit = [&](uint32_t us, std::string &syl) {
            if (syl.empty()) return;
            std::string u = isUtf8 ? syl : toUtf8(syl.data(), syl.size());
            Syllable sy{us, (uint32_t)out.text.size(), (uint16_t)u.size(),
                        (uint8_t)(brk >= 2 ? SYL_PARA : brk == 1 ? SYL_LINE : 0)};
            out.text += u;
            out.syls.push_back(sy);
            syl.clear();
            brk = 0;
        };
        for (const TextEv &t : texts) {
            if (t.track != bestTrack || t.lyric != bestLyric) continue;
            const char *s = pool.data() + t.off;
            const size_t n = t.len;
            if (n && s[0] == '@') continue;
            std::string syl;
            bool prevCr = false;
            for (size_t i = 0; i < n; i++) {
                const char c = s[i];
                if (c == '\r' || (c == '\n' && !prevCr)) {   // CR, LF or CRLF = one break
                    emit(t.us, syl);
                    brk++;
                    prevCr = c == '\r';
                    continue;
                }
                const bool wasCr = prevCr;
                prevCr = false;
                if (c == '\n' && wasCr) continue;
                if (syl.empty() && (c == '/' || c == '\\')) {   // Soft Karaoke markers
                    const int want = c == '\\' ? 2 : 1;
                    if (brk < want) brk = want;
                    continue;
                }
                if ((uint8_t)c < 0x20) continue;
                syl += c;
            }
            emit(t.us, syl);
        }
        // Lines
        for (uint32_t i = 0; i < out.syls.size(); i++) {
            const Syllable &s = out.syls[i];
            if (out.lines.empty() || (s.flags & (SYL_LINE | SYL_PARA))) {
                Line l{s.us, 0, i, 0, out.lines.empty() || (s.flags & SYL_PARA)};
                out.lines.push_back(l);
            }
            out.lines.back().sylCount++;
        }
        // Drop lines that are only spaces (keeps the display clean)
        for (size_t i = 0; i < out.lines.size(); i++) {
            const Line &l = out.lines[i];
            bool blank = true;
            for (uint32_t k = l.firstSyl; k < l.firstSyl + l.sylCount && blank; k++)
                for (uint16_t j = 0; j < out.syls[k].len; j++)
                    if (out.text[out.syls[k].text + j] != ' ') { blank = false; break; }
            if (blank) {
                bool para = l.para;
                out.lines.erase(out.lines.begin() + (long)i);
                if (i < out.lines.size()) out.lines[i].para = out.lines[i].para || para;
                i--;
            }
        }
        for (size_t i = 0; i < out.lines.size(); i++)
            out.lines[i].endUs = i + 1 < out.lines.size() ? out.lines[i + 1].startUs : out.lengthUs;
    }
    if (out.title.empty()) {
        for (const std::string &n : trackNames) {
            if (!n.empty()) {
                out.title = toUtf8(n.data(), n.size());
                while (!out.title.empty() && out.title.back() == ' ') out.title.pop_back();
                break;
            }
        }
    }
    out.melody = (int8_t)detectMelody(out);
    return Err::OK;
}

int detectMelody(Song &s)
{
    // Note-on times per channel (only the ones we need: binary search per syllable)
    std::vector<uint32_t> on[16];
    for (const Event &e : s.events)
        if ((e.status & 0xF0) == 0x90 && e.d2 > 0) on[e.status & 0x0F].push_back(e.us);
    int best = -1, bestScore = -1;
    for (int ch = 0; ch < 16; ch++) {
        ChannelInfo &ci = s.ch[ch];
        if (!ci.notes || ch == DRUM_CH) continue;
        int match = 0;
        if (!s.syls.empty()) {
            uint32_t hit = 0, total = 0;
            for (const Syllable &y : s.syls) {
                if (y.len == 0) continue;
                total++;
                const uint32_t lo = y.us > 120000 ? y.us - 120000 : 0, hi = y.us + 120000;
                auto it = std::lower_bound(on[ch].begin(), on[ch].end(), lo);
                if (it != on[ch].end() && *it <= hi) hit++;
            }
            match = total ? (int)(hit * 100 / total) : 0;
        }
        ci.lyricMatchPct = (uint8_t)match;
        // Score: lyric alignment, mostly monophonic, name hint
        int score = match * (200 - ci.overlapPct) / 2;   // 0..10000
        if (nameIsMelody(ci.trackName)) score += 5000;
        if (s.syls.empty() && ci.overlapPct < 20) score += 100;   // No lyrics: prefer a single-line part
        if (score > bestScore) bestScore = score, best = ch;
    }
    if (bestScore < 2500 && !(best >= 0 && nameIsMelody(s.ch[best].trackName))) return -1;
    return best;
}

void nameFromFile(const std::string &fileName, std::string &title, std::string &artist)
{
    std::string stem = fileName;
    size_t slash = stem.find_last_of("/\\");
    if (slash != std::string::npos) stem = stem.substr(slash + 1);
    size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos && dot > 0) stem = stem.substr(0, dot);
    stem = toUtf8(stem.data(), stem.size());
    size_t us = stem.find('_');
    artist.clear();
    if (us != std::string::npos && us > 0 && us + 1 < stem.size()) {
        artist = stem.substr(0, us);
        stem = stem.substr(us + 1);
    }
    for (char &c : stem)
        if (c == '_') c = ' ';
    title = stem;
}

// ---- Registry ---------------------------------------------------------------------------------
void Registry::load(const std::string &text)
{
    entries_.clear();
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        unsigned crc = 0, size = 0;
        int mel = 0, tr = 0, tempo = 0, used = 0;
        if (sscanf(line.c_str(), "%8x %u %d %d %d %n", &crc, &size, &mel, &tr, &tempo, &used) < 5) continue;
        if (mel < -1 || mel > 15 || tr < -12 || tr > 12 || tempo < 50 || tempo > 150) continue;
        RegEntry e;
        e.crc = crc, e.size = size, e.melody = (int8_t)mel, e.transpose = (int8_t)tr, e.tempo = (uint8_t)tempo;
        if (used > 0 && (size_t)used <= line.size()) e.name = line.substr((size_t)used);
        if (e.name.compare(0, 4, "sf2=") == 0) {   // v0.13.1 per-song SoundFont
            const size_t sp = e.name.find(' ');
            const std::string tok = e.name.substr(4, sp == std::string::npos ? std::string::npos : sp - 4);
            e.name = sp == std::string::npos ? std::string() : e.name.substr(sp + 1);
            for (size_t i = 0; i < tok.size(); i++) {
                if (tok[i] == '%' && i + 2 < tok.size() && isxdigit((unsigned char)tok[i + 1]) &&
                    isxdigit((unsigned char)tok[i + 2])) {
                    const long v = strtol(tok.substr(i + 1, 2).c_str(), nullptr, 16);
                    if (v > 0) {
                        e.sf2 += (char)v;
                        i += 2;
                        continue;
                    }
                }
                e.sf2 += tok[i];
            }
            if (e.sf2.find_first_of("/\\") != std::string::npos || e.sf2.size() > 63) e.sf2.clear();   // File name only
        }
        set(e);
    }
}

std::string Registry::save() const
{
    std::string o = "# OnTheRoad karaoke registry: crc32 size melody(-1..15) transpose tempo% [sf2=font] name\n";
    char buf[64];
    for (const RegEntry &e : entries_) {
        snprintf(buf, sizeof(buf), "%08x %u %d %d %u ", (unsigned)e.crc, (unsigned)e.size, e.melody, e.transpose,
                 e.tempo);
        o += buf;
        if (!e.sf2.empty()) {
            o += "sf2=";
            for (char c : e.sf2) {
                if (c == ' ') o += "%20";
                else if (c == '%') o += "%25";
                else if (c == '\n' || c == '\r' || c == '\t') o += "%20";
                else o += c;
            }
            o += ' ';
        }
        for (char c : e.name) o += (c == '\n' || c == '\r') ? ' ' : c;
        o += '\n';
    }
    return o;
}

const RegEntry *Registry::find(uint32_t crc, uint32_t size) const
{
    for (const RegEntry &e : entries_)
        if (e.crc == crc && e.size == size) return &e;
    return nullptr;
}

void Registry::set(const RegEntry &e)
{
    for (RegEntry &x : entries_)
        if (x.crc == e.crc && x.size == e.size) { x = e; return; }
    entries_.push_back(e);
}

// ---- Player -----------------------------------------------------------------------------------
void Player::load(const Song *s)
{
    song_ = s;
    idx_ = 0;
    pos100_ = 0;
    playing_ = false;
}

void Player::setTempo(int pct) { tempo_ = pct < 50 ? 50 : pct > 150 ? 150 : pct; }

bool Player::finished() const { return !song_ || (idx_ >= song_->events.size() && position() >= song_->lengthUs); }

void Player::advance(uint32_t realUs, Sink &sink)
{
    if (!playing_ || !song_) return;
    pos100_ += (uint64_t)realUs * (uint64_t)tempo_;
    const uint32_t now = position();
    const std::vector<Event> &ev = song_->events;
    while (idx_ < ev.size() && ev[idx_].us <= now) {
        const Event &e = ev[idx_++];
        sink.midi(e.status, e.d1, e.d2);
    }
    if (finished()) playing_ = false;
}

void Player::seek(uint32_t songUs, Sink &sink)
{
    sink.reset();
    idx_ = 0;
    if (!song_) return;
    if (songUs > song_->lengthUs) songUs = song_->lengthUs;
    const std::vector<Event> &ev = song_->events;
    while (idx_ < ev.size() && ev[idx_].us < songUs) {
        const Event &e = ev[idx_++];
        const uint8_t t = e.status & 0xF0;
        if (t == 0xB0 || t == 0xC0 || t == 0xE0) sink.midi(e.status, e.d1, e.d2);
    }
    pos100_ = (uint64_t)songUs * 100;
}

}  // namespace Midi
