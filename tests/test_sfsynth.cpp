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
    if (argc >= 3) testRender(argv[1], argv[2], argc >= 4 ? argv[3] : nullptr);
    CHECK_DONE();
}
