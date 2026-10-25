// Host test for components/otr_synth. Without arguments only the error paths are tested.
// With "<font.sf2> <song.mid> [out.wav]" the song is rendered at 22050 Hz and checked for signal,
// clipping and render speed (a WAV is written when out.wav is given).
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <vector>

#include "check.h"
#include "otr_synth.h"

static void testErrors()
{
    Sf::Synth s;
    CHECK(s.load("/nonexistent/x.sf2", 22050) == Sf::LoadErr::NO_FILE);
    const char *tmp = "/tmp/otr_bad.sf2";
    FILE *fp = fopen(tmp, "wb");
    if (fp) {
        fputs("RIFF\x10\x00\x00\x00WAVEjunkjunkjunk", fp);
        fclose(fp);
        CHECK(s.load(tmp, 22050) == Sf::LoadErr::BAD_FILE);
        remove(tmp);
    }
    CHECK(!s.loaded() && s.activeVoices() == 0);
    int32_t acc[64] = {};
    s.midi(0x90, 60, 100);   // No font: ignored
    s.render(acc, 64, 256);
    bool zero = true;
    for (int32_t v : acc) zero = zero && v == 0;
    CHECK(zero);
    s.setTranspose(20);
    CHECK(s.transpose() == 12);
    s.setMuted(3, true);
    CHECK(s.muted(3) && !s.muted(2) && !s.muted(16));
    s.setMuted(3, false);
    CHECK(!s.muted(3));
    CHECK(strcmp(Sf::loadErrName(Sf::LoadErr::NO_MEMORY), "no memory") == 0);
}

// v0.13.1: header validation (SF2 v2 accepted, SF3 / non-RIFF / too big refused)
static void putChunk(std::vector<uint8_t> &v, const char *id, const std::vector<uint8_t> &body)
{
    v.insert(v.end(), id, id + 4);
    const uint32_t n = (uint32_t)body.size();
    for (int i = 0; i < 4; i++) v.push_back((uint8_t)(n >> (8 * i)));
    v.insert(v.end(), body.begin(), body.end());
    if (n & 1) v.push_back(0);
}

static std::vector<uint8_t> fakeSf(unsigned major)
{
    std::vector<uint8_t> info = {'I', 'N', 'F', 'O'};
    putChunk(info, "isng", {'E', 'M', 'U', '8', '0', '0', '0', 0});
    putChunk(info, "ifil", {(uint8_t)major, 0, 4, 0});
    std::vector<uint8_t> body = {'s', 'f', 'b', 'k'};
    putChunk(body, "LIST", info);
    std::vector<uint8_t> out;
    putChunk(out, "RIFF", body);
    return out;
}

static void testProbe()
{
    std::vector<uint8_t> v2 = fakeSf(2), v3 = fakeSf(3);
    CHECK(Sf::probe(v2.data(), v2.size(), 3u << 20) == Sf::LoadErr::OK);
    CHECK(Sf::probe(v3.data(), v3.size(), 3u << 20) == Sf::LoadErr::COMPRESSED);
    CHECK(Sf::probe(v2.data(), v2.size(), Sf::MAX_SF2 + 1) == Sf::LoadErr::TOO_BIG);
    CHECK(Sf::probe(v2.data(), 8, 3u << 20) == Sf::LoadErr::BAD_FILE);
    std::vector<uint8_t> wav = v2;
    memcpy(wav.data() + 8, "WAVE", 4);
    CHECK(Sf::probe(wav.data(), wav.size(), 3u << 20) == Sf::LoadErr::BAD_FILE);
    std::vector<uint8_t> trunc(v2.begin(), v2.begin() + 20);   // Header only: accepted (loader decides)
    CHECK(Sf::probe(trunc.data(), trunc.size(), 3u << 20) == Sf::LoadErr::OK);
    const char *tmp = "/tmp/otr_sf3.sf2";
    FILE *fp = fopen(tmp, "wb");
    if (fp) {
        fwrite(v3.data(), 1, v3.size(), fp);
        fclose(fp);
        Sf::Synth s;
        CHECK(s.load(tmp, 22050) == Sf::LoadErr::COMPRESSED);
        size_t sz = 0;
        CHECK(Sf::probeFile(tmp, &sz) == Sf::LoadErr::COMPRESSED && sz == v3.size());
        remove(tmp);
    }
    CHECK(Sf::probeFile("/nonexistent/x.sf2") == Sf::LoadErr::NO_FILE);
}

static void wav(const char *path, const std::vector<int16_t> &pcm, int rate)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return;
    uint32_t data = (uint32_t)pcm.size() * 2, riff = 36 + data, fmt = 16, br = (uint32_t)rate * 2;
    uint16_t pcmFmt = 1, ch = 1, align = 2, bits = 16;
    fwrite("RIFF", 1, 4, fp), fwrite(&riff, 4, 1, fp), fwrite("WAVEfmt ", 1, 8, fp), fwrite(&fmt, 4, 1, fp);
    fwrite(&pcmFmt, 2, 1, fp), fwrite(&ch, 2, 1, fp), fwrite(&rate, 4, 1, fp), fwrite(&br, 4, 1, fp);
    fwrite(&align, 2, 1, fp), fwrite(&bits, 2, 1, fp), fwrite("data", 1, 4, fp), fwrite(&data, 4, 1, fp);
    fwrite(pcm.data(), 2, pcm.size(), fp);
    fclose(fp);
}

static void testRender(const char *font, const char *song, const char *out)
{
    const int RATE = 22050, BLOCK = 256;
    Sf::Synth s;
    clock_t t0 = clock();
    Sf::LoadErr le = s.load(font, RATE);
    CHECK(le == Sf::LoadErr::OK);
    if (le != Sf::LoadErr::OK) return;
    printf("  font %zu bytes loaded in %.2f s\n", s.fileSize(), (double)(clock() - t0) / CLOCKS_PER_SEC);
    FILE *fp = fopen(song, "rb");
    CHECK(fp != nullptr);
    if (!fp) return;
    std::vector<uint8_t> d(Midi::MAX_FILE + 1);
    d.resize(fread(d.data(), 1, d.size(), fp));
    fclose(fp);
    Midi::Song sg;
    CHECK(Midi::parse(d.data(), d.size(), sg) == Midi::Err::OK);
    Midi::Player p;
    p.load(&sg);
    p.seek(0, s);
    p.play(true);
    std::vector<int16_t> pcm;
    int32_t acc[BLOCK];
    long clip = 0, peakVoices = 0;
    double sumSq = 0;
    const uint32_t blockUs = (uint32_t)(BLOCK * 1000000ull / RATE);
    t0 = clock();
    while (p.playing()) {
        p.advance(blockUs, s);
        memset(acc, 0, sizeof(acc));
        s.render(acc, BLOCK, 256);
        if (s.activeVoices() > peakVoices) peakVoices = s.activeVoices();
        for (int32_t v : acc) {
            if (v >= 32767 || v <= -32768) clip++;
            v = v > 32767 ? 32767 : v < -32768 ? -32768 : v;
            sumSq += (double)v * v;
            pcm.push_back((int16_t)v);
        }
    }
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC, songSecs = (double)pcm.size() / RATE;
    double rms = pcm.empty() ? 0 : __builtin_sqrt(sumSq / (double)pcm.size());
    printf("  %s: %.0f s audio rendered in %.2f s (%.1f%% CPU host), rms %.0f, clipped %ld, peak voices %ld\n",
           song, songSecs, secs, 100.0 * secs / songSecs, rms, clip, peakVoices);
    CHECK(rms > 300);
    CHECK(clip < (long)pcm.size() / 1000);
    CHECK(peakVoices <= Sf::MAX_VOICES);
    if (out) wav(out, pcm, RATE);
}

int main(int argc, char **argv)
{
    testErrors();
    testProbe();
    if (argc >= 3) testRender(argv[1], argv[2], argc >= 4 ? argv[3] : nullptr);
    CHECK_DONE();
}
