// Host tests for KaraStream (v0.13.5 web karaoke: PCM ring, lyric events, JSON frames).
// g++ -std=c++17 -Wall -Wextra -Imain -Icomponents/otr_midi/include -Itests tests/test_karastream.cpp
//     main/kara_stream.cpp components/otr_midi/*.cpp -o t && ./t
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
}

int main()
{
    testRing();
    testTracker();
    testText();
    CHECK_DONE();
}
