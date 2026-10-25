// Host test for main/audio_synth.{h,cpp} and main/sound_bank.{h,cpp}
// g++ -std=c++17 -Wall -Wextra -Werror -Imain tests/test_audio.cpp main/audio_synth.cpp main/sound_bank.cpp
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "audio_synth.h"
#include "check.h"
#include "sound_bank.h"

using namespace Synth;

static void put16(std::vector<uint8_t> &v, uint16_t x) { v.push_back(x & 0xFF); v.push_back(x >> 8); }
static void put32(std::vector<uint8_t> &v, uint32_t x) { put16(v, x & 0xFFFF); put16(v, x >> 16); }
static void tag(std::vector<uint8_t> &v, const char *t) { v.insert(v.end(), t, t + 4); }

static std::vector<uint8_t> makeWav(uint16_t ch, uint16_t bits, uint32_t rate, uint32_t frames,
                                    bool extraChunk = false, uint16_t fmtTag = 1)
{
    std::vector<uint8_t> v;
    uint32_t data = frames * ch * (bits / 8);
    tag(v, "RIFF"); put32(v, 0); tag(v, "WAVE");
    tag(v, "fmt "); put32(v, 16); put16(v, fmtTag); put16(v, ch); put32(v, rate);
    put32(v, rate * ch * bits / 8); put16(v, ch * bits / 8); put16(v, bits);
    if (extraChunk) { tag(v, "LIST"); put32(v, 3); v.push_back('a'); v.push_back('b'); v.push_back('c'); v.push_back(0); }
    tag(v, "data"); put32(v, data);
    for (uint32_t i = 0; i < data; i++) v.push_back((uint8_t)i);
    uint32_t riff = (uint32_t)v.size() - 8;
    memcpy(&v[4], &riff, 4);
    return v;
}

static void testMidi()
{
    CHECK(midiHz(69) > 439.9f && midiHz(69) < 440.1f);
    CHECK(midiHz(81) > 879.9f && midiHz(81) < 880.1f);
    CHECK(midiHz(60) > 261.5f && midiHz(60) < 261.7f);
}

static void testMmlLength()
{
    // t120: quarter = 500 ms, eighth (default) = 250 ms
    CHECK(mmlLengthMs("c") == 250);
    CHECK(mmlLengthMs("c4") == 500);
    CHECK(mmlLengthMs("c4.") == 750);
    CHECK(mmlLengthMs("c4..") == 875);
    CHECK(mmlLengthMs("t60 c4") == 1000);
    CHECK(mmlLengthMs("l4 c d r e") == 2000);
    CHECK(mmlLengthMs("o5 c+ d- e# | > c < b") == 1250);
    CHECK(mmlLengthMs("x2,40,8,40 @3 v10 q6 s-5 c2") == 1000);
    CHECK(mmlLengthMs("") == 0);
    CHECK(mmlLengthMs("c4 k") == -1);       // unknown command
    CHECK(mmlLengthMs("c99") == -1);        // bad length
    CHECK(mmlLengthMs("x1,2 c") == -1);     // incomplete envelope
    CHECK(mmlLengthMs("@9 c") == -1);       // bad wave
}

static void testVoiceRender()
{
    Voice v;
    v.start("t120 v15 @0 x0,1,15,1 q8 o4 a4");   // 500 ms = 11025 samples
    std::vector<int32_t> acc(12000, 0);
    int chunk = 256, total = 0;
    while (total < 12000) {
        int n = (12000 - total) < chunk ? 12000 - total : chunk;
        bool more = v.render(acc.data() + total, n, 256);
        total += n;
        if (!more) break;
    }
    CHECK(v.done());
    CHECK(!v.error());
    int32_t peak = 0, crossings = 0;
    for (int i = 1; i < 11025; i++) {
        int32_t a = abs(acc[i]);
        if (a > peak) peak = a;
        if ((acc[i - 1] < 0) != (acc[i] < 0)) crossings++;
    }
    CHECK(peak > VOICE_AMP * 9 / 10 && peak <= VOICE_AMP);
    // 440 Hz for 0.5 s -> ~440 zero crossings
    CHECK(crossings > 420 && crossings < 460);
    // Silence after the note
    bool silent = true;
    for (int i = 11030; i < 12000; i++) silent = silent && acc[i] == 0;
    CHECK(silent);
    // Ends at zero (release fitted inside the note): last samples small
    CHECK(abs(acc[11020]) < VOICE_AMP / 4);
}

static void testGainAndRest()
{
    Voice v;
    v.start("v15 r4");
    std::vector<int32_t> acc(1000, 0);
    v.render(acc.data(), 1000, 256);
    bool silent = true;
    for (int32_t s : acc) silent = silent && s == 0;
    CHECK(silent);
    v.start("v15 x0,1,15,1 c4");
    std::fill(acc.begin(), acc.end(), 0);
    v.render(acc.data(), 1000, 0);
    silent = true;
    for (int32_t s : acc) silent = silent && s == 0;
    CHECK(silent);
    Voice e;
    e.start(nullptr);
    CHECK(e.done());
    CHECK(!e.render(acc.data(), 10, 256));
}

static void testAllWaves()
{
    for (int w = 0; w < (int)Wave::COUNT; w++) {
        char mml[48];
        snprintf(mml, sizeof mml, "@%d v15 o4 c8", w);
        Voice v;
        v.start(mml);
        std::vector<int32_t> acc(6000, 0);
        v.render(acc.data(), 6000, 256);
        int32_t peak = 0;
        for (int32_t s : acc) peak = abs(s) > peak ? abs(s) : peak;
        CHECK(peak > 1000 && peak <= VOICE_AMP);
        CHECK(!v.error());
    }
}

static void testSong()
{
    Song s = {{"c4", "e4 g4", nullptr, nullptr}, false};
    SongPlayer p;
    p.start(s);
    CHECK(p.playing());
    std::vector<int32_t> acc(512);
    int blocks = 0;
    while (p.render(acc.data(), 512, 256) && blocks < 1000) blocks++;
    // 1000 ms = 22050 samples = ~43 blocks
    CHECK(blocks >= 42 && blocks <= 44);
    CHECK(!p.playing());
    Song l = {{"c16", nullptr, nullptr, nullptr}, true};
    p.start(l);
    for (int i = 0; i < 100; i++) p.render(acc.data(), 512, 256);
    CHECK(p.playing());
    p.stop();
    CHECK(!p.playing());
}

static void testBank()
{
    for (int i = 0; i < (int)Sound::Sfx::COUNT; i++) {
        Sound::Sfx s = (Sound::Sfx)i;
        const char *n = Sound::sfxName(s);
        CHECK(n && *n);
        int32_t ms = mmlLengthMs(Sound::sfxMml(s));
        if (ms <= 0 || ms > 2500) printf("sfx %s length %d\n", n, (int)ms);
        CHECK(ms > 0 && ms <= 2500);
        for (int j = 0; j < i; j++) CHECK(strcmp(n, Sound::sfxName((Sound::Sfx)j)) != 0);
        for (const char *p = n; *p; p++) CHECK((*p >= 'a' && *p <= 'z') || *p == '_');
    }
    CHECK(Sound::sfxName(Sound::Sfx::COUNT) == nullptr);
    CHECK(Sound::musicSong(Sound::Music::NONE) == nullptr);
    for (Sound::Music m : {Sound::Music::ADVENTURE, Sound::Music::DICE}) {
        const Song *s = Sound::musicSong(m);
        CHECK(s && s->loop);
        CHECK(Sound::musicName(m) != nullptr);
        int32_t len0 = mmlLengthMs(s->parts[0]);
        CHECK(len0 > 5000);
        // All parts of a song have the same length (within rounding) so the loop stays in sync
        for (int i = 1; i < MAX_SONG_VOICES; i++) {
            if (!s->parts[i]) continue;
            int32_t li = mmlLengthMs(s->parts[i]);
            if (abs(li - len0) > 20) printf("music %s part %d: %d vs %d ms\n", Sound::musicName(m), i, (int)li, (int)len0);
            CHECK(abs(li - len0) <= 20);   // per-note rounding only
        }
    }
}

static void testWavParse()
{
    Wav::Info in{};
    auto w = makeWav(1, 16, 22050, 100);
    CHECK(Wav::parse(w.data(), w.size(), (uint32_t)w.size(), in));
    CHECK(in.channels == 1 && in.bits == 16 && in.rate == 22050);
    CHECK(in.dataOffset == 44 && in.dataBytes == 200 && in.frames() == 100);

    auto s = makeWav(2, 8, 11025, 50, true);
    CHECK(Wav::parse(s.data(), s.size(), 0, in));
    CHECK(in.channels == 2 && in.bits == 8 && in.rate == 11025);
    CHECK(in.dataOffset == 44 + 12 && in.frames() == 50);   // odd LIST chunk padded to 4 bytes

    // Truncated file clips dataBytes to whole frames
    CHECK(Wav::parse(w.data(), w.size(), 44 + 51, in));
    CHECK(in.dataBytes == 50);

    auto bad = makeWav(1, 24, 22050, 10);
    CHECK(!Wav::parse(bad.data(), bad.size(), 0, in));
    auto bad2 = makeWav(3, 16, 22050, 10);
    CHECK(!Wav::parse(bad2.data(), bad2.size(), 0, in));
    auto bad3 = makeWav(1, 16, 96000, 10);
    CHECK(!Wav::parse(bad3.data(), bad3.size(), 0, in));
    auto bad4 = makeWav(1, 16, 22050, 10, false, 3);   // IEEE float
    CHECK(!Wav::parse(bad4.data(), bad4.size(), 0, in));
    CHECK(!Wav::parse(w.data(), 30, 0, in));            // header cut before data chunk
    std::vector<uint8_t> junk(64, 0);
    CHECK(!Wav::parse(junk.data(), junk.size(), 0, in));
    CHECK(!Wav::parse(nullptr, 0, 0, in));
}

static void testToMono()
{
    Wav::Info f{};
    f.channels = 2; f.bits = 16;
    int16_t st[4] = {1000, 3000, -2000, -4000};
    int16_t out[2];
    Wav::toMono16((const uint8_t *)st, 2, f, out);
    CHECK(out[0] == 2000 && out[1] == -3000);
    f.channels = 1; f.bits = 8;
    uint8_t u8[3] = {128, 255, 0};
    Wav::toMono16(u8, 3, f, out);
    int16_t o3[3];
    Wav::toMono16(u8, 3, f, o3);
    CHECK(o3[0] == 0 && o3[1] == 127 * 256 && o3[2] == -128 * 256);
}

static void testResampler()
{
    // 44100 -> 22050 halves the count
    std::vector<int16_t> in(4410);
    for (size_t i = 0; i < in.size(); i++) in[i] = (int16_t)(i % 100);
    Wav::Resampler r;
    r.reset(44100);
    std::vector<int16_t> out(5000);
    size_t produced = 0, pos = 0;
    while (pos < in.size()) {
        size_t used = 0;
        size_t cap = 64 < out.size() - produced ? 64 : out.size() - produced;
        produced += r.process(in.data() + pos, std::min<size_t>(100, in.size() - pos), out.data() + produced, cap, used);
        pos += used;
        if (!used && !cap) break;
    }
    CHECK(produced >= 2203 && produced <= 2206);
    // 11025 -> 22050 doubles, linear interpolation of a ramp stays a ramp
    std::vector<int16_t> ramp(1000);
    for (int i = 0; i < 1000; i++) ramp[i] = (int16_t)(i * 10);
    r.reset(11025);
    std::vector<int16_t> o2(2100);
    size_t used = 0;
    size_t n = r.process(ramp.data(), ramp.size(), o2.data(), o2.size(), used);
    CHECK(used == 1000);
    CHECK(n >= 1997 && n <= 2000);
    bool ok = true;
    for (size_t i = 0; i < n; i++) ok = ok && abs(o2[i] - (int)(i * 5)) <= 1;
    CHECK(ok);
    // Same rate passes through
    r.reset(22050);
    n = r.process(ramp.data(), 10, o2.data(), 100, used);
    CHECK(n == 9 && o2[0] == 0 && o2[8] == 80);
    // Output capacity respected, input left over
    r.reset(22050);
    n = r.process(ramp.data(), 100, o2.data(), 5, used);
    CHECK(n == 5 && used < 100);
}

int main()
{
    testMidi();
    testMmlLength();
    testVoiceRender();
    testGainAndRest();
    testAllWaves();
    testSong();
    testBank();
    testWavParse();
    testToMono();
    testResampler();
    CHECK_DONE();
}
