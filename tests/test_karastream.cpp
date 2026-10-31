// Host tests for KaraStream (v0.13.5 web karaoke: PCM ring, lyric events, JSON frames).
// g++ -std=c++17 -Wall -Wextra -Imain -Icomponents/otr_midi/include -Itests tests/test_karastream.cpp
//     main/kara_stream.cpp components/otr_midi/*.cpp -o t && ./t
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "check.h"
#include "kara_stream.h"

using namespace KaraStream;

static int16_t s_pcm[1024];
static Event s_ev[4];

// Song with two lines: "Hel|lo " (0.5 s, 1.0 s) and "world" (2.0 s); quote and UTF-8 in line 2.
static Midi::Song makeSong()
{
    Midi::Song s;
    auto syl = [&](uint32_t us, const char *t, uint8_t flags) {
        Midi::Syllable y{us, (uint32_t)s.text.size(), (uint16_t)strlen(t), flags};
        s.text += t;
        s.syls.push_back(y);
    };
    syl(500000, "Hel", Midi::SYL_LINE);
    syl(1000000, "lo ", 0);
    syl(2000000, "w\"\xc3\xb6rld", Midi::SYL_LINE);
    s.lines.push_back(Midi::Line{500000, 2000000, 0, 2, true});
    s.lines.push_back(Midi::Line{2000000, 3000000, 2, 1, false});
    s.lengthUs = 3000000;
    return s;
}

static void testRing()
{
    Stream st;
    st.init(s_pcm, 1024, s_ev, 4);
    int16_t blk[256], out[1024];
    for (int i = 0; i < 256; i++) blk[i] = (int16_t)i;
    st.pushPcm(blk, 256);
    CHECK(st.available() == 0);   // Disabled: dropped
    st.setEnabled(true);
    st.pushPcm(blk, 256);
    CHECK(st.available() == 256);
    CHECK(st.readPcm(out, 512) == 0);   // Not a full frame yet
    st.pushPcm(blk, 256);
    CHECK(st.readPcm(out, 512) == 512 && out[0] == 0 && out[255] == 255 && out[256] == 0 && out[511] == 255);
    for (int i = 0; i < 4; i++) st.pushPcm(blk, 256);
    CHECK(st.available() == 1024);
    st.pushPcm(blk, 256);   // Full: dropped, never overwrites unread data
    CHECK(st.available() == 1024 && st.dropped() == 256);
    CHECK(st.readPcm(out, 512) == 512 && st.readPcm(out, 512) == 512 && st.available() == 0);
    // Wrap-around keeps the order
    for (int k = 0; k < 6; k++) {
        st.pushPcm(blk, 256);
        st.pushPcm(blk, 256);
        CHECK(st.readPcm(out, 512) == 512 && out[100] == 100 && out[300] == 44);
    }
    // Events split the PCM at their sample
    st.pushPcm(blk, 256);
    Event e{};
    e.kind = Kind::CLEAR;
    CHECK(st.pushEvent(e));
    st.pushPcm(blk, 256);
    Event got{};
    CHECK(!st.popEvent(got));                  // PCM before it still pending
    CHECK(st.readPcm(out, 512) == 256);        // Partial frame up to the event
    CHECK(st.popEvent(got) && got.kind == Kind::CLEAR && got.sample == st.written() - 256);
    CHECK(st.readPcm(out, 512) == 0 && st.available() == 256);
    // Event queue full -> refused; drain clears everything
    for (int i = 0; i < 4; i++) CHECK(st.pushEvent(e));
    CHECK(!st.pushEvent(e));
    st.drain();
    CHECK(st.available() == 0 && !st.popEvent(got));
    // Disable / enable starts fresh
    st.pushPcm(blk, 256);
    st.setEnabled(false);
    st.setEnabled(true);
    CHECK(st.available() == 0);
}

static std::vector<std::string> collect(Stream &st)
{
    std::vector<std::string> v;
    int16_t out[1024];
    Event e{};
    char js[JSON_CAP];
    for (int guard = 0; guard < 100; guard++) {
        while (st.popEvent(e)) {
            CHECK(toJson(e, js, sizeof(js)) > 0);
            v.emplace_back(js);
        }
        if (!st.readPcm(out, 512, false)) break;
    }
    return v;
}

static void testTracker()
{
    static int16_t pcm[4096];
    static Event ev[16];
    Stream st;
    st.init(pcm, 4096, ev, 16);
    const Midi::Song song = makeSong();
    LyricTracker tr;
    int16_t blk[256] = {};
    // Disabled: nothing recorded
    tr.update(song, 600000, st);
    CHECK(collect(st).empty());
    st.setEnabled(true);
    tr.update(song, 100000, st);   // Before the first line
    st.pushPcm(blk, 256);
    std::vector<std::string> v = collect(st);
    CHECK(v.size() == 1 && v[0] == "{\"type\":\"clear\",\"ts\":100}");
    tr.update(song, 100000, st);   // No change: no event
    CHECK(collect(st).empty());
    tr.update(song, 600000, st);
    st.pushPcm(blk, 256);
    v = collect(st);
    CHECK(v.size() == 2);
    if (v.size() == 2) {
        CHECK(v[0] == "{\"type\":\"newrow\",\"row\":0,\"ts\":600,\"syls\":[\"Hel\",\"lo \"],\"next\":\"w\\\"\xc3\xb6rld\"}");
        CHECK(v[1] == "{\"type\":\"lyric\",\"text\":\"Hel\",\"row\":0,\"k\":0,\"ts\":600}");
    }
    tr.update(song, 1100000, st);
    v = collect(st);
    CHECK(v.size() == 1 && v[0] == "{\"type\":\"lyric\",\"text\":\"lo \",\"row\":0,\"k\":1,\"ts\":1100}");
    tr.update(song, 2500000, st);
    v = collect(st);
    CHECK(v.size() == 2);
    if (v.size() == 2) {
        CHECK(v[0] == "{\"type\":\"newrow\",\"row\":1,\"ts\":2500,\"syls\":[\"w\\\"\xc3\xb6rld\"],\"next\":\"\"}");
        CHECK(v[1].find("\"k\":0") != std::string::npos);
    }
    // Resync (new client) re-sends the current row and syllable
    st.requestResync();
    tr.update(song, 2600000, st);
    v = collect(st);
    CHECK(v.size() == 2 && v[0].find("newrow") != std::string::npos);
    // Seek back to the start
    tr.update(song, 0, st);
    v = collect(st);
    CHECK(v.size() == 1 && v[0].find("clear") != std::string::npos);
    // Event timing: the event sits at the stream sample where it was recorded
    st.drain();
    tr.update(song, 600000, st);   // reset by the clear: row 0 again
    const uint64_t at = st.written();
    st.pushPcm(blk, 256);
    Event e{};
    CHECK(st.popEvent(e) && e.sample == at && e.kind == Kind::ROW);
}

static void testText()
{
    char b[8];
    CHECK(copyUtf8(b, sizeof(b), "abc", 3) == 3 && !strcmp(b, "abc"));
    // 7 bytes room: "abcde" + "ö" (2 bytes) would need 7 -> fits exactly 7? cap-1 = 7 bytes
    CHECK(copyUtf8(b, sizeof(b), "abcdef\xc3\xb6", 8) == 6 && !strcmp(b, "abcdef"));   // ö not split
    CHECK(copyUtf8(b, sizeof(b), "a\tb\nc", 5) == 5 && !strcmp(b, "a b c"));
    char js[64];
    char *p = js;
    CHECK(jsonEscape("a\"b\\c", 5, p, js + sizeof(js)) && !strcmp(js, "a\\\"b\\\\c"));
    p = js;
    const char ctl[2] = {'\x01', 0};
    CHECK(jsonEscape(ctl, 1, p, js + sizeof(js)) && !strcmp(js, "\\u0001"));
    p = js;
    CHECK(!jsonEscape("0123456789", 10, p, js + 5));
    Event e{};
    e.kind = Kind::SYL;
    snprintf(e.text, sizeof(e.text), "x");
    CHECK(toJson(e, js, 10) == 0 && js[0] == 0);   // Too small: empty, not truncated JSON
    // Long row text stays inside TEXT_CAP and valid
    Midi::Song s;
    for (int i = 0; i < 100; i++) {
        s.syls.push_back(Midi::Syllable{(uint32_t)(1000 + i), (uint32_t)s.text.size(), 4, (uint8_t)(i ? 0 : Midi::SYL_LINE)});
        s.text += "\xc3\xa5\xc3\xa4";   // åä
    }
    s.lines.push_back(Midi::Line{1000, 9000, 0, 100, true});
    static int16_t pcm[1024];
    static Event ev[4];
    Stream st;
    st.init(pcm, 1024, ev, 4);
    st.setEnabled(true);
    LyricTracker tr;
    tr.update(s, 5000, st);
    Event got{};
    CHECK(st.popEvent(got) && got.kind == Kind::ROW && strlen(got.text) < TEXT_CAP);
    static char big[JSON_CAP];
    CHECK(toJson(got, big, sizeof(big)) > 0 && big[strlen(big) - 1] == '}');
    // A maximum-size .lyr row and its next row must both reach the browser intact.
    s.clear();
    const std::string current(512, 'a'), next(512, 'b');
    s.text = current + next;
    s.syls.push_back(Midi::Syllable{1000, 0, 512, Midi::SYL_LINE});
    s.syls.push_back(Midi::Syllable{5000, 512, 512, Midi::SYL_LINE});
    s.lines.push_back(Midi::Line{1000, 5000, 0, 1, false});
    s.lines.push_back(Midi::Line{5000, 9000, 1, 1, false});
    st.drain(); tr.reset();
    tr.update(s, 1000, st);
    CHECK(st.popEvent(got) && got.kind == Kind::ROW);
    CHECK(std::string(got.text) == current + NEXT_SEP + next);
    CHECK(toJson(got, big, sizeof(big)) > 0);
    CHECK(std::string(big).find(current) != std::string::npos && std::string(big).find(next) != std::string::npos);
}

// ---- v0.13.6 countdown dots ---------------------------------------------------------------------
struct SongBuilder {
    Midi::Song s;
    // One line per call: syllable start times in ms
    void line(std::initializer_list<uint32_t> ms)
    {
        Midi::Line l{0, 0, (uint32_t)s.syls.size(), (uint16_t)ms.size(), s.lines.empty()};
        for (uint32_t t : ms) {
            Midi::Syllable y{t * 1000u, (uint32_t)s.text.size(), 2, (uint8_t)(t == *ms.begin() ? Midi::SYL_LINE : 0)};
            s.text += "la";
            s.syls.push_back(y);
        }
        l.startUs = *ms.begin() * 1000u;
        if (!s.lines.empty()) s.lines.back().endUs = l.startUs;
        s.lines.push_back(l);
        l.endUs = s.lengthUs = s.syls.back().us + 2000000u;
        s.lines.back().endUs = s.lengthUs;
    }
};

struct CdFrame {
    int dots, elapsed;
    uint64_t sample;
};

// Plays the song block by block like the mixer (tracker before the PCM tap) and returns the
// countdown frames with their stream sample; also checks every frame is valid JSON-ish.
static std::vector<CdFrame> runCountdown(const Midi::Song &song, uint32_t fromUs, uint32_t toUs, int *rows = nullptr)
{
    static int16_t pcm[8192];
    static Event ev[16];
    Stream st;
    st.init(pcm, 8192, ev, 16);
    st.setEnabled(true);
    LyricTracker tr;
    int16_t blk[256] = {};
    int16_t out[1024];
    std::vector<CdFrame> v;
    char js[JSON_CAP];
    const uint64_t first = (uint64_t)fromUs * RATE / 1000000u;
    for (uint64_t smp = first;; smp += 256) {
        const uint32_t us = (uint32_t)(smp * 1000000u / RATE);
        if (us > toUs) break;
        tr.update(song, us, st);
        st.pushPcm(blk, 256);
        Event e{};
        for (int g = 0; g < 8; g++) {
            while (st.popEvent(e)) {
                CHECK(toJson(e, js, sizeof(js)) > 0);
                if (e.kind == Kind::COUNTDOWN) {
                    v.push_back({e.row, e.syl, first + e.sample});
                    char want[96];
                    snprintf(want, sizeof(want), "{\"type\":\"countdown\",\"dots\":%d,\"elapsed\":%d,", e.row, e.syl);
                    CHECK(strncmp(js, want, strlen(want)) == 0);
                }
                if (e.kind == Kind::ROW && rows) ++*rows;
            }
            if (!st.readPcm(out, 1024, false)) break;
        }
    }
    return v;
}

static uint64_t sampleAt(uint32_t ms) { return (uint64_t)ms * RATE / 1000u; }

static void testCountdown()
{
    // Line 1: 0.5 / 1.0 / 1.5 s (spacing 0.5 s -> hold 0.5 s, pause from 2.0 s); line 2 at 7.0 s
    // (pause 5 s -> 5 dots); line 3 at 9.0 s (pause 1 s after 7.5 + 0.5: none)
    SongBuilder b;
    b.line({500, 1000, 1500});
    b.line({7000, 7500});
    b.line({9000});
    const Midi::Song &s = b.s;
    CHECK(countdownAt(s, 0).dots == 0);          // Intro 0.5 s: too short
    CHECK(countdownAt(s, 1600000).dots == 0);    // Last syllable still held
    CHECK(countdownAt(s, 1999999).dots == 0);
    Countdown c = countdownAt(s, 2000000);
    CHECK(c.dots == 5 && c.elapsed == 0 && c.nextSyl == 3 && c.startUs == 2000000 && c.endUs == 7000000);
    CHECK(countdownAt(s, 2999999).elapsed == 0);
    CHECK(countdownAt(s, 3000000).elapsed == 1);
    CHECK(countdownAt(s, 5500000).elapsed == 3);
    CHECK(countdownAt(s, 6999999).elapsed == 4);
    CHECK(countdownAt(s, 7000000).dots == 0);    // Next line started
    CHECK(countdownAt(s, 8200000).dots == 0);    // Short pause before line 3

    // Stream: first frame when the pause starts, then one per second, each at the right sample
    int rows = 0;
    std::vector<CdFrame> f = runCountdown(s, 0, 9500000, &rows);
    CHECK(rows == 3);
    CHECK(f.size() == 5);
    for (size_t i = 0; i < f.size() && i < 5; i++) {
        CHECK(f[i].dots == 5);
        CHECK(f[i].elapsed == (int)i);   // Sequence 0, 1, 2, 3, 4
        const uint64_t want = sampleAt(2000 + 1000 * (uint32_t)i);
        CHECK(f[i].sample >= want && f[i].sample < want + 256);   // Within the mixer block
    }
    // Consecutive updates are one second (22 050 samples) apart, within a block
    for (size_t i = 1; i < f.size(); i++) {
        const int64_t d = (int64_t)(f[i].sample - f[i - 1].sample);
        CHECK(d > RATE - 256 && d < RATE + 256);
    }

    // Pause < 3 s: no countdown at all (2.9 s between pause start and next line)
    SongBuilder b2;
    b2.line({500, 1000, 1500});   // Pause starts at 2.0 s
    b2.line({4900});
    CHECK(runCountdown(b2.s, 0, 5500000).empty());
    SongBuilder b3;
    b3.line({500, 1000, 1500});
    b3.line({5000});              // Exactly 3.0 s -> 3 dots
    f = runCountdown(b3.s, 0, 5500000);
    CHECK(f.size() == 3 && f[0].dots == 3 && f[2].elapsed == 2);

    // Long pause: max 8 dots, all white until 7 s are left (the 8th lights as the line starts)
    SongBuilder b4;
    b4.line({500, 1000, 1500});
    b4.line({22000});             // 20 s pause
    c = countdownAt(b4.s, 2500000);
    CHECK(c.dots == COUNTDOWN_MAX && c.elapsed == 0);
    CHECK(countdownAt(b4.s, 14999999).elapsed == 0);
    CHECK(countdownAt(b4.s, 15000000).elapsed == 1);   // 7 s left
    CHECK(countdownAt(b4.s, 21500000).elapsed == 7);
    f = runCountdown(b4.s, 0, 22500000);
    CHECK(f.size() == 8 && f.back().elapsed == 7);

    // Rounding: 4.6 s -> 5 dots, 4.4 s -> 4; one-syllable line holds 0.5 s; hold capped at 1 s
    SongBuilder b5;
    b5.line({1000});              // Pause from 1.5 s
    b5.line({6100});              // 4.6 s
    b5.line({7000, 9000});        // Spacing 2 s -> hold 1 s, pause from 10 s
    b5.line({14400});             // 4.4 s
    CHECK(countdownAt(b5.s, 1500000).dots == 5);
    CHECK(countdownAt(b5.s, 1400000).dots == 0);
    CHECK(countdownAt(b5.s, 1500000).elapsed == 0);   // ceil(4.6) = 5 left
    CHECK(countdownAt(b5.s, 2100000).elapsed == 1);
    CHECK(countdownAt(b5.s, 9999999).dots == 0);
    CHECK(countdownAt(b5.s, 10000000).dots == 4);

    // Intro: a 4.2 s wait before the first line counts down from the start
    SongBuilder b6;
    b6.line({4200});
    c = countdownAt(b6.s, 0);
    CHECK(c.dots == 4 && c.elapsed == 0);
    CHECK(countdownAt(b6.s, 1200000).elapsed == 1);

    // Seek out of a pause: a dots 0 frame hides the countdown
    {
        static int16_t pcm[4096];
        static Event ev[16];
        Stream st;
        st.init(pcm, 4096, ev, 16);
        st.setEnabled(true);
        LyricTracker tr;
        tr.update(s, 2500000, st);
        std::vector<std::string> v = collect(st);
        CHECK(v.size() == 3 && v[2] == "{\"type\":\"countdown\",\"dots\":5,\"elapsed\":0,\"ts\":2500}");
        tr.update(s, 1200000, st);   // Back into line 1
        v = collect(st);
        CHECK(!v.empty() && v.back() == "{\"type\":\"countdown\",\"dots\":0,\"elapsed\":0,\"ts\":1200}");
        tr.update(s, 1300000, st);
        CHECK(collect(st).empty());
        // Resync during a pause re-sends the countdown
        tr.update(s, 4500000, st);   // Last syllable of line 1, then the countdown (2 lit)
        v = collect(st);
        CHECK(v.size() == 2 && v.back().find("\"elapsed\":2") != std::string::npos);
        st.requestResync();
        tr.update(s, 4600000, st);
        v = collect(st);
        CHECK(!v.empty() && v.back() == "{\"type\":\"countdown\",\"dots\":5,\"elapsed\":2,\"ts\":4600}");
    }
    // Empty song
    Midi::Song empty;
    CHECK(countdownAt(empty, 0).dots == 0);
}

int main()
{
    testRing();
    testTracker();
    testText();
    testCountdown();
    CHECK_DONE();
}
