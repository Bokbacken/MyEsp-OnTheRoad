// kara_stream.cpp - see kara_stream.h.
#include "kara_stream.h"

#include <stdio.h>
#include <string.h>

namespace KaraStream {

void Stream::init(int16_t *pcm, uint32_t cap, Event *ev, uint32_t evCap)
{
    pcm_ = pcm, cap_ = cap, ev_ = ev, evCap_ = evCap;
    w_ = 0, r_ = 0, ew_ = 0, er_ = 0;
    enabled_ = false, resync_ = false, dropped_ = 0;
}

void Stream::pushPcm(const int16_t *s, uint32_t n)
{
    if (!pcm_ || !enabled()) return;
    const uint64_t w = w_.load(std::memory_order_relaxed);
    const uint64_t r = r_.load(std::memory_order_acquire);
    if (w - r + n > cap_) {   // Consumer too slow: drop the block (never block the mixer)
        dropped_.fetch_add(n, std::memory_order_relaxed);
        return;
    }
    for (uint32_t i = 0; i < n; i++) pcm_[(w + i) & (cap_ - 1)] = s[i];
    w_.store(w + n, std::memory_order_release);
}

bool Stream::pushEvent(const Event &e)
{
    if (!ev_ || !enabled()) return false;
    const uint32_t w = ew_.load(std::memory_order_relaxed);
    if (w - er_.load(std::memory_order_acquire) >= evCap_) return false;
    Event &d = ev_[w & (evCap_ - 1)];
    d = e;
    d.sample = w_.load(std::memory_order_relaxed);
    ew_.store(w + 1, std::memory_order_release);
    return true;
}

void Stream::setEnabled(bool on)
{
    if (on == enabled()) return;
    if (on) drain();
    enabled_.store(on, std::memory_order_release);
}

uint32_t Stream::available() const
{
    return (uint32_t)(w_.load(std::memory_order_acquire) - r_.load(std::memory_order_relaxed));
}

uint32_t Stream::readPcm(int16_t *out, uint32_t max, bool wantFull)
{
    if (!pcm_) return 0;
    const uint64_t r = r_.load(std::memory_order_relaxed);
    const uint64_t w = w_.load(std::memory_order_acquire);
    uint64_t limit = w;
    bool boundary = false;
    const uint32_t er = er_.load(std::memory_order_relaxed);
    if (er != ew_.load(std::memory_order_acquire)) {
        const uint64_t at = ev_[er & (evCap_ - 1)].sample;
        if (at <= limit) limit = at < r ? r : at, boundary = true;
    }
    uint64_t n = limit - r;
    if (n > max) n = max, boundary = false;
    if (wantFull && n < max && !boundary) return 0;
    for (uint64_t i = 0; i < n; i++) out[i] = pcm_[(r + i) & (cap_ - 1)];
    r_.store(r + n, std::memory_order_release);
    return (uint32_t)n;
}

bool Stream::popEvent(Event &e)
{
    if (!ev_) return false;
    const uint32_t er = er_.load(std::memory_order_relaxed);
    if (er == ew_.load(std::memory_order_acquire)) return false;
    const Event &s = ev_[er & (evCap_ - 1)];
    if (s.sample > r_.load(std::memory_order_relaxed)) return false;   // PCM before it not sent yet
    e = s;
    er_.store(er + 1, std::memory_order_release);
    return true;
}

void Stream::drain()
{
    // Events first: an event pushed between the two stores refers to PCM that is dropped too, and
    // popEvent() hands it out as soon as its sample is reached (harmless: at most one stale event)
    er_.store(ew_.load(std::memory_order_acquire), std::memory_order_release);
    r_.store(w_.load(std::memory_order_acquire), std::memory_order_release);
}

// ---------------------------------------------------------------------------------------------
size_t copyUtf8(char *dst, size_t cap, const char *src, size_t n)
{
    if (!cap) return 0;
    size_t len = n < cap - 1 ? n : cap - 1;
    if (len < n)   // Truncated: do not end inside a multi-byte character
        while (len > 0 && ((uint8_t)src[len] & 0xC0) == 0x80) len--;
    for (size_t i = 0; i < len; i++) {
        const uint8_t c = (uint8_t)src[i];
        dst[i] = c < 0x20 || c == 0x7F ? ' ' : (char)c;
    }
    dst[len] = 0;
    return len;
}

namespace {

// Appends src to dst[pos..cap) as UTF-8 (control characters -> spaces). Returns the new length.
size_t append(char *dst, size_t pos, size_t cap, const char *src, size_t n)
{
    if (pos + 1 >= cap) return pos;
    return pos + copyUtf8(dst + pos, cap - pos, src, n);
}

}  // namespace

void LyricTracker::update(const Midi::Song &song, uint32_t posUs, Stream &st)
{
    if (!st.enabled()) {
        reset();   // Everything is sent again once a client is there
        return;
    }
    if (st.takeResync()) reset();
    const int row = song.lineAt(posUs);
    const int syl = song.sylAt(posUs);
    do {   // Text events (break = unchanged, retry on the next block, or nothing more to send)
        if (row == row_ && syl == syl_) break;
        Event e{};
        e.ts = posUs / 1000;
        e.row = row;
        if (row != row_) {
            if (row < 0) {
                e.kind = Kind::CLEAR;
                if (!st.pushEvent(e)) break;   // Retried on the next block
            } else {
                e.kind = Kind::ROW;
                const Midi::Line &l = song.lines[(size_t)row];
                size_t pos = 0;
                for (uint32_t i = l.firstSyl; i < l.firstSyl + l.sylCount && i < song.syls.size(); i++) {
                    const Midi::Syllable &s = song.syls[i];
                    if (i != l.firstSyl && pos + 2 < TEXT_CAP) e.text[pos++] = SYL_SEP, e.text[pos] = 0;
                    pos = append(e.text, pos, TEXT_CAP, song.text.data() + s.text, s.len);
                }
                if ((size_t)row + 1 < song.lines.size() && pos + 2 < TEXT_CAP) {
                    e.text[pos++] = NEXT_SEP;
                    e.text[pos] = 0;
                    const Midi::Line &nl = song.lines[(size_t)row + 1];
                    for (uint32_t i = nl.firstSyl; i < nl.firstSyl + nl.sylCount && i < song.syls.size(); i++)
                        pos = append(e.text, pos, TEXT_CAP, song.text.data() + song.syls[i].text, song.syls[i].len);
                }
                if (!st.pushEvent(e)) break;
            }
            row_ = row;
            syl_ = INVALID;
        }
        if (syl != syl_) {
            syl_ = syl;
            if (row < 0 || syl < 0) break;
            const Midi::Line &l = song.lines[(size_t)row];
            if ((uint32_t)syl < l.firstSyl || (uint32_t)syl >= l.firstSyl + l.sylCount) break;   // Not in this row
            Event s{};
            s.kind = Kind::SYL;
            s.ts = posUs / 1000;
            s.row = row;
            s.syl = syl - (int)l.firstSyl;
            const Midi::Syllable &y = song.syls[(size_t)syl];
            copyUtf8(s.text, TEXT_CAP, song.text.data() + y.text, y.len);
            if (!st.pushEvent(s)) syl_ = INVALID;   // Retried on the next block
        }
    } while (false);
    // v0.13.6 countdown: first frame when the pause starts, then one per lit dot (at the exact
    // block where the song position crosses the second), dots 0 when it ends early (seek)
    const Countdown cd = countdownAt(song, posUs);
    if (cd.dots == cdDots_ && cd.elapsed == cdLit_ && cd.nextSyl == cdSyl_) return;
    if (cd.dots == 0 && cdDots_ == 0) {   // Nothing shown, nothing to hide
        cdLit_ = cd.elapsed, cdSyl_ = cd.nextSyl;
        return;
    }
    if (cd.dots == 0 && syl_ == cdSyl_) {   // The line it counted down to started: the lyric takes over
        cdDots_ = 0, cdLit_ = 0, cdSyl_ = cd.nextSyl;
        return;
    }
    Event c{};
    c.kind = Kind::COUNTDOWN;
    c.ts = posUs / 1000;
    c.row = cd.dots;
    c.syl = cd.elapsed;
    if (st.pushEvent(c)) cdDots_ = cd.dots, cdLit_ = cd.elapsed, cdSyl_ = cd.nextSyl;
}

Countdown countdownAt(const Midi::Song &song, uint32_t posUs)
{
    Countdown cd;
    if (song.syls.empty() || song.lines.empty()) return cd;
    const int s = song.sylAt(posUs);
    const int n = s + 1;
    if ((size_t)n >= song.syls.size()) return cd;
    const uint32_t nextUs = song.syls[(size_t)n].us;
    // The next syllable must start a line
    const int nl = song.lineAt(nextUs);
    if (nl < 0 || song.lines[(size_t)nl].firstSyl != (uint32_t)n) return cd;
    uint32_t start = 0;
    if (s >= 0) {
        const uint32_t lastUs = song.syls[(size_t)s].us;
        uint32_t hold = COUNTDOWN_HOLD_ONE_MS * 1000u;
        const int row = song.lineAt(lastUs);
        if (row >= 0) {
            const Midi::Line &l = song.lines[(size_t)row];
            if (l.sylCount >= 2 && (uint32_t)s > l.firstSyl && (size_t)l.firstSyl < song.syls.size()) {
                const uint32_t span = lastUs - song.syls[l.firstSyl].us;
                const uint32_t avg = span / (uint32_t)(s - (int)l.firstSyl);
                hold = avg < COUNTDOWN_HOLD_MAX_MS * 1000u ? avg : COUNTDOWN_HOLD_MAX_MS * 1000u;
            }
        }
        start = lastUs + hold;
    }
    if (nextUs <= start || nextUs - start < COUNTDOWN_GAP_MS * 1000u || posUs < start) return cd;
    int dots = (int)((nextUs - start + 500000u) / 1000000u);
    if (dots > COUNTDOWN_MAX) dots = COUNTDOWN_MAX;
    const uint32_t remain = nextUs - posUs;                       // > 0 (posUs < nextUs)
    const int remainSec = (int)((remain + 999999u) / 1000000u);   // ceil
    int lit = dots - remainSec;
    if (lit < 0) lit = 0;
    cd.dots = dots;
    cd.elapsed = lit;
    cd.nextSyl = n;
    cd.startUs = start;
    cd.endUs = nextUs;
    return cd;
}

bool jsonEscape(const char *s, size_t n, char *&p, char *end)
{
    for (size_t i = 0; i < n; i++) {
        const uint8_t c = (uint8_t)s[i];
        if (c == '"' || c == '\\') {
            if (end - p < 3) return false;
            *p++ = '\\';
            *p++ = (char)c;
        } else if (c < 0x20) {
            if (end - p < 7) return false;
            snprintf(p, 7, "\\u%04x", c);
            p += 6;
        } else {
            if (end - p < 2) return false;
            *p++ = (char)c;
        }
    }
    *p = 0;
    return true;
}

namespace {

bool put(char *&p, char *end, const char *s)
{
    const size_t n = strlen(s);
    if ((size_t)(end - p) <= n) return false;
    memcpy(p, s, n + 1);
    p += n;
    return true;
}

}  // namespace

size_t toJson(const Event &e, char *out, size_t cap)
{
    if (cap < 2) return 0;
    char *p = out, *end = out + cap;
    char num[64];
    bool ok = true;
    switch (e.kind) {
    case Kind::CLEAR:
        snprintf(num, sizeof(num), "{\"type\":\"clear\",\"ts\":%u}", (unsigned)e.ts);
        ok = put(p, end, num);
        break;
    case Kind::SYL:
        ok = put(p, end, "{\"type\":\"lyric\",\"text\":\"") && jsonEscape(e.text, strlen(e.text), p, end);
        snprintf(num, sizeof(num), "\",\"row\":%d,\"k\":%d,\"ts\":%u}", (int)e.row, (int)e.syl, (unsigned)e.ts);
        ok = ok && put(p, end, num);
        break;
    case Kind::COUNTDOWN:
        snprintf(num, sizeof(num), "{\"type\":\"countdown\",\"dots\":%d,\"elapsed\":%d,\"ts\":%u}", (int)e.row,
                 (int)e.syl, (unsigned)e.ts);
        ok = put(p, end, num);
        break;
    case Kind::ROW: {
        snprintf(num, sizeof(num), "{\"type\":\"newrow\",\"row\":%d,\"ts\":%u,\"syls\":[\"", (int)e.row, (unsigned)e.ts);
        ok = put(p, end, num);
        const char *s = e.text;
        const char *next = strchr(s, NEXT_SEP);
        const char *rowEnd = next ? next : s + strlen(s);
        while (ok) {
            const char *sep = (const char *)memchr(s, SYL_SEP, (size_t)(rowEnd - s));
            const char *segEnd = sep ? sep : rowEnd;
            ok = jsonEscape(s, (size_t)(segEnd - s), p, end);
            if (!sep) break;
            ok = ok && put(p, end, "\",\"");
            s = sep + 1;
        }
        ok = ok && put(p, end, "\"],\"next\":\"");
        if (next) ok = ok && jsonEscape(next + 1, strlen(next + 1), p, end);
        ok = ok && put(p, end, "\"}");
        break;
    }
    }
    if (!ok) {
        out[0] = 0;
        return 0;
    }
    return (size_t)(p - out);
}

}  // namespace KaraStream
