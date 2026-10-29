// Host test for components/otr_synth. Without arguments: error paths, header checks, the effects
// and selective loading against a small SoundFont built in memory. With
// "<font.sf2> <song.mid> [out.wav]" the song is rendered at 22050 Hz from a selectively loaded bank
// (and compared bit for bit with a full load when the file fits) and checked for signal, clipping
// and render speed (a WAV is written when out.wav is given).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <string>
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
    CHECK(Sf::probe(v2.data(), v2.size(), 40u << 20) == Sf::LoadErr::OK);   // v0.13.2: any size
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
    // A big file is refused by load() (whole file in RAM) but not by the header check
    const char *big = "/tmp/otr_big.sf2";
    fp = fopen(big, "wb");
    if (fp) {
        fwrite(v2.data(), 1, v2.size(), fp);
        fclose(fp);
        CHECK(truncate(big, (off_t)Sf::MAX_SF2 + 4096) == 0);
        Sf::Synth s;
        CHECK(s.load(big, 22050) == Sf::LoadErr::TOO_BIG);
        CHECK(Sf::probeFile(big) == Sf::LoadErr::OK);
        CHECK(s.open(big, 22050) != Sf::LoadErr::OK && !s.bankOpen());   // No pdta/sdta
        remove(big);
    }
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


// ---- A tiny SoundFont built in memory: 5 looped sine samples, 3 presets -----------------------
//   bank 0 / 0  "Piano": velocity 0-63 -> sample 0, 64-127 -> sample 1 (keys 0-127)
//   bank 128 / 0 "Drums": key 36 -> sample 2, key 38 -> sample 3
//   bank 0 / 5  "Unused": sample 4 (never played by the test song)
//   layered: the kick (key 36) gets a second, stacked zone playing sample 4
static void le16(std::vector<uint8_t> &v, unsigned x) { v.push_back((uint8_t)x), v.push_back((uint8_t)(x >> 8)); }
static void le32v(std::vector<uint8_t> &v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((uint8_t)(x >> (8 * i))); }
static void name20(std::vector<uint8_t> &v, const char *n)
{
    char b[20] = {};
    strncpy(b, n, 19);
    v.insert(v.end(), b, b + 20);
}

static const int SMP_LEN = 4000, SMP_GAP = 46, SMP_N = 5;

// rate: sample rate of every sample; oddLoop: 101-sample loops instead of 2000; sharedLoop: the
// loud piano zone plays sample 0 (like the soft one) with a loop 13 samples shorter (overlapping
// zones with different loops)
static std::vector<uint8_t> buildSf2(bool layered = false, uint32_t rate = 22050, bool oddLoop = false, bool sharedLoop = false)
{
    std::vector<uint8_t> smpl;
    for (int s = 0; s < SMP_N; s++) {
        for (int i = 0; i < SMP_LEN; i++) le16(smpl, (unsigned)(uint16_t)(int16_t)(12000 * sinf(6.2831853f * (float)i * (float)(s + 1) / 100.0f)));
        for (int i = 0; i < SMP_GAP; i++) le16(smpl, 0);
    }
    struct Gen { uint16_t op, amt; };
    // Instrument zones (igen) and their bags
    std::vector<std::vector<Gen>> izones = {
        {{43, 0x7F00}, {44, 0x3F00}, {54, 1}, {53, 0}},   // inst 0: vel 0-63
        {{43, 0x7F00}, {44, 0x7F40}, {54, 1}, {53, 1}},   // inst 0: vel 64-127
        {{43, 0x2424}, {53, 2}},                          // inst 1: key 36
        {{43, 0x2626}, {53, 3}},                          // inst 1: key 38
        {{54, 1}, {53, 4}},                               // inst 2
    };
    if (sharedLoop) izones[1] = {{43, 0x7F00}, {44, 0x7F40}, {54, 1}, {3, (uint16_t)-13}, {53, 0}};
    if (layered) izones.insert(izones.begin() + 4, std::vector<Gen>{{43, 0x2424}, {53, 4}});
    const int ex = layered ? 1 : 0;
    const int instFirstBag[] = {0, 2, 4 + ex, 5 + ex};
    std::vector<uint8_t> phdr, pbag, pmod(10, 0), pgen, inst, ibag, imod(10, 0), igen, shdr;
    struct P { const char *n; uint16_t preset, bank, instIdx; };
    const P presets[] = {{"Piano", 0, 0, 0}, {"Drums", 0, 128, 1}, {"Unused", 5, 0, 2}};
    for (int i = 0; i < 3; i++) {
        name20(phdr, presets[i].n), le16(phdr, presets[i].preset), le16(phdr, presets[i].bank), le16(phdr, (unsigned)i);
        le32v(phdr, 0), le32v(phdr, 0), le32v(phdr, 0);
        le16(pbag, (unsigned)i), le16(pbag, 0);
        le16(pgen, 41), le16(pgen, presets[i].instIdx);
    }
    name20(phdr, "EOP"), le16(phdr, 0), le16(phdr, 0), le16(phdr, 3), le32v(phdr, 0), le32v(phdr, 0), le32v(phdr, 0);
    le16(pbag, 3), le16(pbag, 0);
    le16(pgen, 0), le16(pgen, 0);
    const char *in[] = {"PianoI", "DrumsI", "UnusedI"};
    for (int i = 0; i < 3; i++) name20(inst, in[i]), le16(inst, (unsigned)instFirstBag[i]);
    name20(inst, "EOI"), le16(inst, (unsigned)(5 + ex));
    unsigned g = 0;
    for (auto &z : izones) {
        le16(ibag, g), le16(ibag, 0);
        for (auto &e : z) le16(igen, e.op), le16(igen, e.amt), g++;
    }
    le16(ibag, g), le16(ibag, 0);
    le16(igen, 0), le16(igen, 0);
    for (int s = 0; s < SMP_N; s++) {
        const uint32_t st = (uint32_t)s * (SMP_LEN + SMP_GAP);
        char nm[20];
        snprintf(nm, sizeof(nm), "Sine%d", s);
        name20(shdr, nm), le32v(shdr, st), le32v(shdr, st + SMP_LEN), le32v(shdr, st + 1000), le32v(shdr, st + 1000 + (oddLoop ? 101 : 2000));
        le32v(shdr, rate), shdr.push_back(60), shdr.push_back(0), le16(shdr, 0), le16(shdr, 1);
    }
    name20(shdr, "EOS"), le32v(shdr, 0), le32v(shdr, 0), le32v(shdr, 0), le32v(shdr, 0), le32v(shdr, 0), shdr.push_back(0), shdr.push_back(0), le16(shdr, 0), le16(shdr, 0);

    std::vector<uint8_t> info = {'I', 'N', 'F', 'O'};
    putChunk(info, "ifil", {2, 0, 1, 0});
    std::vector<uint8_t> sdta = {'s', 'd', 't', 'a'};
    putChunk(sdta, "smpl", smpl);
    std::vector<uint8_t> pdta = {'p', 'd', 't', 'a'};
    putChunk(pdta, "phdr", phdr), putChunk(pdta, "pbag", pbag), putChunk(pdta, "pmod", pmod);
    putChunk(pdta, "pgen", pgen), putChunk(pdta, "inst", inst), putChunk(pdta, "ibag", ibag);
    putChunk(pdta, "imod", imod), putChunk(pdta, "igen", igen), putChunk(pdta, "shdr", shdr);
    std::vector<uint8_t> body = {'s', 'f', 'b', 'k'};
    putChunk(body, "LIST", info), putChunk(body, "LIST", sdta), putChunk(body, "LIST", pdta);
    std::vector<uint8_t> out;
    putChunk(out, "RIFF", body);
    return out;
}

// Format 0 song: piano notes at velocity 40 and 100, kick and snare on the drum channel.
static std::vector<uint8_t> buildMidi(uint8_t softVel = 40)
{
    std::vector<uint8_t> t;
    auto ev = [&](uint8_t dt, uint8_t a, uint8_t b, uint8_t c) { t.push_back(dt), t.push_back(a), t.push_back(b), t.push_back(c); };
    t.push_back(0), t.push_back(0xC0), t.push_back(0);
    t.push_back(0), t.push_back(0xC9), t.push_back(0);
    ev(0, 0xB0, 91, 60), ev(0, 0xB0, 93, 30);
    ev(0, 0x90, 60, softVel), ev(0, 0x99, 36, 100);
    ev(48, 0x89, 36, 0), ev(48, 0x80, 60, 0);
    ev(0, 0x90, 64, 100), ev(0, 0x99, 38, 90);
    ev(48, 0x89, 38, 0), ev(48, 0x80, 64, 0);
    ev(0, 0x90, 67, softVel);
    ev(96, 0x80, 67, 0);
    t.push_back(0), t.push_back(0xFF), t.push_back(0x2F), t.push_back(0);
    std::vector<uint8_t> m = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k'};
    for (int i = 3; i >= 0; i--) m.push_back((uint8_t)(t.size() >> (8 * i)));
    m.insert(m.end(), t.begin(), t.end());
    return m;
}

// Renders the song (plus `tailSecs` of silence) and returns the PCM.
static std::vector<int16_t> renderSong(Sf::Synth &s, const Midi::Song &sg, double maxSecs, double tailSecs = 0)
{
    const int RATE = 22050, BLOCK = 256;
    Midi::Player p;
    p.load(&sg);
    p.seek(0, s);
    p.play(true);
    std::vector<int16_t> pcm;
    int32_t acc[BLOCK];
    const uint32_t blockUs = (uint32_t)(BLOCK * 1000000ull / RATE);
    const size_t maxN = (size_t)(maxSecs * RATE);
    size_t tailLeft = (size_t)(tailSecs * RATE);
    while (pcm.size() < maxN && (p.playing() || tailLeft)) {
        if (p.playing()) p.advance(blockUs, s);
        else tailLeft = tailLeft > (size_t)BLOCK ? tailLeft - BLOCK : 0;
        memset(acc, 0, sizeof(acc));
        s.render(acc, BLOCK, 256);
        for (int32_t v : acc) pcm.push_back((int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v));
    }
    return pcm;
}

static double rmsOf(const int16_t *p, size_t n)
{
    double s = 0;
    for (size_t i = 0; i < n; i++) s += (double)p[i] * p[i];
    return n ? sqrt(s / (double)n) : 0;
}

static void testSelective()
{
    const char *path = "/tmp/otr_mini.sf2";
    const std::vector<uint8_t> sf = buildSf2(), mid = buildMidi();
    FILE *fp = fopen(path, "wb");
    CHECK(fp != nullptr);
    if (!fp) return;
    fwrite(sf.data(), 1, sf.size(), fp);
    fclose(fp);
    Midi::Song sg;
    CHECK(Midi::parse(mid.data(), mid.size(), sg) == Midi::Err::OK);
    const Sf::Effects off = {false, 0, false, 0};

    // Hydra only: presets known, no samples, silent until prepared
    Sf::Synth s;
    s.setEffects(off);
    CHECK(s.open(path, 22050) == Sf::LoadErr::OK);
    CHECK(s.bankOpen() && s.selective() && !s.loaded() && s.presetCount() == 3);
    int32_t acc[256] = {};
    s.midi(0x90, 60, 100);
    s.render(acc, 256, 256);
    bool zero = true;
    for (int32_t v : acc) zero = zero && v == 0;
    CHECK(zero && s.activeVoices() == 0);

    // Selective: 2 presets, 4 zones, samples 0..3 only (the unused preset's sample stays on disk)
    int lastPct = -1, calls = 0;
    struct Prog { int *last, *calls; } pr = {&lastPct, &calls};
    CHECK(s.prepare(sg, [](void *u, int pct) { Prog *p = (Prog *)u; *p->last = pct; (*p->calls)++; }, &pr) == Sf::LoadErr::OK);
    CHECK(s.loaded() && lastPct == 100 && calls >= 3);
    const Sf::PrepStats st = s.stats();
    CHECK(st.presets == 2 && st.regions == 4 && !st.reduced && st.resampled == 0 && st.resampledSaved == 0);   // 22.05 kHz: as is
    CHECK(st.budget == Sf::HOST_BUDGET);
    CHECK(st.bytes >= 4u * SMP_LEN * 2 && st.bytes < 4u * (SMP_LEN + SMP_GAP + 2048) * 2);
    CHECK(st.bytes < (uint32_t)SMP_N * (SMP_LEN + SMP_GAP) * 2);
    printf("  mini bank: %u of %zu bytes loaded, %u presets, %u zones, %u reads\n", st.bytes, s.fileSize(), st.presets, st.regions, st.ranges);
    const std::vector<int16_t> sel = renderSong(s, sg, 30);

    // Same PCM as the full load
    Sf::Synth full;
    full.setEffects(off);
    CHECK(full.load(path, 22050) == Sf::LoadErr::OK && full.loaded() && !full.selective());
    const std::vector<int16_t> ref = renderSong(full, sg, 30);
    CHECK(!ref.empty() && ref.size() == sel.size() && memcmp(ref.data(), sel.data(), ref.size() * 2) == 0);
    CHECK(rmsOf(ref.data(), ref.size()) > 300);

    // Transpose: prepared again for the shifted keys; the unused preset stays silent
    s.setTranspose(12);
    CHECK(s.transpose() == 12 && s.prepare(sg) == Sf::LoadErr::OK && s.stats().regions == 4);
    const std::vector<int16_t> up = renderSong(s, sg, 30);
    CHECK(rmsOf(up.data(), up.size()) > 300);
    s.setTranspose(0);
    CHECK(s.prepare(sg) == Sf::LoadErr::OK);
    s.reset();
    s.midi(0xC0, 5, 0);
    s.midi(0x90, 60, 100);
    CHECK(s.activeVoices() == 0);

    // Voice capacity is board-configurable; the current limit cannot exceed its allocation.
    s.reset();
    CHECK(Sf::MAX_SUPPORTED_VOICES == 192);
    CHECK(s.voiceCapacity() == 32 && s.maxVoices() == 32);
    for (int i = 0; i < 40; i++) s.midi(0x90, 60, 100);
    CHECK(s.activeVoices() == 32);
    s.reset();
    s.setVoiceCapacity(Sf::MAX_SUPPORTED_VOICES);
    CHECK(s.voiceCapacity() == 192);
    s.setMaxVoices(2);
    for (int k = 0; k < 4; k++) s.midi(0x90, (uint8_t)(60 + k), 100);
    CHECK(s.maxVoices() == 2 && s.activeVoices() == 2);
    CHECK(s.takeVoiceSteals() > 0 && s.takeVoiceSteals() == 0);
    s.setMaxVoices(1);
    CHECK(s.activeVoices() == 1);
    s.setMaxVoices(Sf::MAX_SUPPORTED_VOICES + 1);
    CHECK(s.maxVoices() == Sf::MAX_SUPPORTED_VOICES);
    s.reset();
    for (int i = 0; i < Sf::MAX_SUPPORTED_VOICES + 4; i++) s.midi(0x90, 60, 100);
    CHECK(s.activeVoices() == Sf::MAX_SUPPORTED_VOICES);
    s.setMaxVoices(Sf::MAX_SUPPORTED_VOICES);
    s.reset();

    // Same file again: the hydra is kept
    CHECK(s.open(path, 22050) == Sf::LoadErr::OK && s.bankOpen());
    CHECK(s.prepare(sg) == Sf::LoadErr::OK && s.stats().bytes == st.bytes);

    // Budget: one velocity layer (the one holding velocity 100) answers every velocity
    const size_t oneLayer = (size_t)st.bytes - (size_t)(SMP_LEN + 2) * 2 + 64;
    CHECK(s.prepare(sg, nullptr, nullptr, oneLayer) == Sf::LoadErr::OK);
    CHECK(s.loaded() && s.stats().reduced && s.stats().regions == 3 && s.stats().bytes < st.bytes);
    s.reset();
    s.midi(0x90, 60, 30);   // Soft note -> loud layer
    CHECK(s.activeVoices() == 1);
    memset(acc, 0, sizeof(acc));
    s.render(acc, 256, 256);
    bool sound = false;
    for (int32_t v : acc) sound = sound || v != 0;
    CHECK(sound);
    CHECK(s.prepare(sg, nullptr, nullptr, 1000) == Sf::LoadErr::TOO_BIG);
    CHECK(!s.loaded() && s.bankOpen());   // The bank stays open for the next song
    CHECK(s.prepare(sg) == Sf::LoadErr::OK && s.loaded());

    // Last resort: one zone per key (stacked kick layer dropped, the kept zone plays louder)
    {
        const char *lp = "/tmp/otr_layer.sf2";
        const std::vector<uint8_t> lsf = buildSf2(true);
        FILE *lf = fopen(lp, "wb");
        CHECK(lf != nullptr);
        if (lf) {
            fwrite(lsf.data(), 1, lsf.size(), lf);
            fclose(lf);
            Sf::Synth l;
            l.setEffects(off);
            CHECK(l.open(lp, 22050) == Sf::LoadErr::OK && l.prepare(sg) == Sf::LoadErr::OK);
            const Sf::PrepStats lf0 = l.stats();
            CHECK(lf0.regions == 5 && !lf0.reduced && !lf0.thinned);
            l.reset();
            l.midi(0x99, 36, 100);
            CHECK(l.activeVoices() == 2);
            const size_t thin = (size_t)lf0.bytes - 2u * (SMP_LEN + SMP_GAP) * 2 + 64;
            CHECK(l.prepare(sg, nullptr, nullptr, thin) == Sf::LoadErr::OK);
            CHECK(l.stats().reduced && l.stats().thinned && l.stats().regions == 3 && l.stats().bytes < lf0.bytes);
            l.reset();
            l.midi(0x99, 36, 100);
            CHECK(l.activeVoices() == 1);
            int32_t b2[256] = {};
            l.render(b2, 256, 256);
            bool snd = false;
            for (int32_t v : b2) snd = snd || v != 0;
            CHECK(snd);
            CHECK(l.prepare(sg, nullptr, nullptr, thin - 4000) == Sf::LoadErr::TOO_BIG);
        }
        remove(lp);
    }

    // Effects on: finite, no clipping, the tail dies away
    s.setEffects(Sf::Effects{true, 100, true, 100});
    const std::vector<int16_t> wet = renderSong(s, sg, 60, 12);
    CHECK(wet.size() > ref.size());
    long clip = 0;
    for (int16_t v : wet) clip += v == 32767 || v == -32768;
    CHECK(clip == 0);
    CHECK(rmsOf(wet.data(), ref.size()) > rmsOf(ref.data(), ref.size()) * 0.9);
    CHECK(rmsOf(wet.data() + wet.size() - 2205, 2205) < 1.0);
    bool differs = false;
    for (size_t i = 0; i < ref.size() && !differs; i++) differs = wet[i] != ref[i];
    CHECK(differs);
    s.unload();
    CHECK(!s.bankOpen() && !s.loaded() && s.presetCount() == 0);
    remove(path);
}

// Relative RMS difference of two renders (same length expected)
static double relDiff(const std::vector<int16_t> &a, const std::vector<int16_t> &b)
{
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    double d = 0, r = 0;
    for (size_t i = 0; i < n; i++) d += ((double)a[i] - b[i]) * ((double)a[i] - b[i]), r += (double)a[i] * a[i];
    return r > 0 ? sqrt(d / r) : 1.0;
}

// Period (in samples, sub-sample accurate) of a periodic signal near `approx`: the lag with the best
// normalized autocorrelation, refined with a parabola
static double periodOf(const std::vector<int16_t> &x, double approx)
{
    auto corr = [&](int lag) {
        double s = 0, e = 0;
        for (size_t i = 0; i + (size_t)lag < x.size(); i++) s += (double)x[i] * x[i + lag], e += (double)x[i] * x[i];
        return e > 0 ? s / e : 0;
    };
    int best = (int)(approx * 0.9);
    double bv = -2;
    for (int lag = (int)(approx * 0.9); lag <= (int)(approx * 1.1) + 1; lag++) {
        const double c = corr(lag);
        if (c > bv) bv = c, best = lag;
    }
    const double cm = corr(best - 1), cp = corr(best + 1), den = cm - 2 * bv + cp;
    return best + (den != 0 ? 0.5 * (cm - cp) / den : 0);
}

// Banks above the output rate: samples are resampled to about 22.05 kHz, take that much less memory
// and sound the same as the unconverted samples (relative error against TinySoundFont playing the
// original rate); loops (long, short odd, two overlapping zones with different loops) keep their pitch.
static void testResampling()
{
    const char *path = "/tmp/otr_rs.sf2";
    Midi::Song sg;
    const std::vector<uint8_t> mid = buildMidi();
    CHECK(Midi::parse(mid.data(), mid.size(), sg) == Midi::Err::OK);
    const Sf::Effects off = {false, 0, false, 0};
    const uint32_t rates[] = {44100, 32000, 30536, 28000, 48000};
    for (uint32_t rate : rates)
        for (int variant = 0; variant < 3; variant++) {   // 2000 loops, 101 loops, shared sample 101/88
            const bool odd = variant > 0, shared = variant == 2;
            const std::vector<uint8_t> sf = buildSf2(false, rate, odd, shared);
            FILE *fp = fopen(path, "wb");
            CHECK(fp != nullptr);
            if (!fp) return;
            fwrite(sf.data(), 1, sf.size(), fp);
            fclose(fp);

            Sf::Synth ref, h;
            ref.setEffects(off), h.setEffects(off);
            ref.setResampling(false);
            CHECK(!ref.resampling() && h.resampling());
            CHECK(ref.open(path, 22050) == Sf::LoadErr::OK && ref.prepare(sg) == Sf::LoadErr::OK);
            CHECK(h.open(path, 22050) == Sf::LoadErr::OK && h.prepare(sg) == Sf::LoadErr::OK);
            const Sf::PrepStats a = ref.stats(), b = h.stats();
            CHECK(a.resampled == 0 && a.resampledSaved == 0 && a.regions == 4 && b.regions == 4);
            CHECK(b.resampled == 4);   // Every zone, short odd loops included
            const double r = 22050.0 / rate, got = (double)b.bytes / a.bytes;
            CHECK(got < r + 0.03 && got > r - 0.03);
            CHECK(b.resampledSaved > 0);   // Copied blocks merge gaps, resampled ones not: no exact difference
            const std::vector<int16_t> x = renderSong(ref, sg, 30), y = renderSong(h, sg, 30);
            const double rd = relDiff(x, y);
            CHECK(x.size() == y.size() && rmsOf(y.data(), y.size()) > 300);
            // Shared: both test loops jump in mid-wave (88/101 samples of a 100-sample sine), the
            // compensated 101 loop is stretched by up to half an output sample -> larger waveform error
            CHECK(rd < (shared ? 0.3 : 0.03));

            // Notes held in their loop for 3 s (soft and loud zone) keep the pitch: same loop period
            // (autocorrelation) and, where the zone's loop set the ratio, the same waveform (no drift)
            double worst = 0, pitchErr = 0;
            for (int vel = 40; vel <= 100; vel += 60) {
                ref.reset(), h.reset();
                ref.midi(0x90, 60, (uint8_t)vel), h.midi(0x90, 60, (uint8_t)vel);
                std::vector<int16_t> lx, ly;
                int32_t acc[256];
                for (int k = 0; k < 22050 * 3 / 256; k++) {
                    memset(acc, 0, sizeof(acc));
                    ref.render(acc, 256, 256);
                    for (int32_t v : acc) lx.push_back((int16_t)v);
                    memset(acc, 0, sizeof(acc));
                    h.render(acc, 256, 256);
                    for (int32_t v : acc) ly.push_back((int16_t)v);
                }
                const std::vector<int16_t> tx(lx.end() - 22050 / 2, lx.end()), ty(ly.end() - 22050 / 2, ly.end());
                CHECK(rmsOf(tx.data(), tx.size()) > 300);
                const int loop = shared && vel > 63 ? 88 : odd ? 101 : 100;   // Repeat in source samples (2000 = 20 x 100)
                const double expect = loop * 22050.0 / rate, px = periodOf(tx, expect), py = periodOf(ty, expect);
                const double pe = fabs(py / px - 1);
                if (pe > pitchErr) pitchErr = pe;
                const double d = relDiff(tx, ty);
                if (d > worst && !(shared && vel < 64)) worst = d;   // Shared: the soft zone's 101 loop is compensated
            }
            printf("  resampling %u Hz (%s loops): %u -> %u bytes, rel. diff song %.4f, held %.4f, period %.5f%%\n", rate,
                   shared ? "shared 101/88" : odd ? "odd 101" : "2000", a.bytes, b.bytes, rd, worst, pitchErr * 100);
            CHECK(worst < (shared ? 0.05 : 0.03));
            CHECK(pitchErr < (shared ? 0.0005 : 0.00002));   // 0.9 / 0.03 cent
        }
    remove(path);
}

static void testEffects()
{
    const int RATE = 22050;
    Sf::Reverb r;
    Sf::Chorus c;
    CHECK(r.init(RATE) && c.init(RATE) && r.ready() && c.ready());
    std::vector<float> in(256, 0.0f), out(256);
    bool finite = true;
    float peak = 0, last = 1;
    for (int b = 0; b < RATE * 10 / 256; b++) {
        if (b < 8) for (int i = 0; i < 256; i++) in[i] = (float)((i * 7919 + b * 104729) % 1000 - 500) / 1000.0f;   // Noise burst, peak 0.5
        else std::fill(in.begin(), in.end(), 0.0f);
        std::fill(out.begin(), out.end(), 0.0f);
        r.process(in.data(), out.data(), 256, 1.0f);
        c.process(in.data(), out.data(), 256, 1.0f);
        last = 0;
        for (float v : out) {
            finite = finite && std::isfinite(v);
            if (fabsf(v) > peak) peak = fabsf(v);
            if (fabsf(v) > last) last = fabsf(v);
        }
    }
    CHECK(finite && peak > 0.001f && peak < 1.2f);   // Both effects at full level on a 0.5 burst
    CHECK(last < 1e-5f);
    // CPU cost per second of audio (host)
    std::vector<float> noise(256);
    for (int i = 0; i < 256; i++) noise[i] = (float)((i * 7919) % 200 - 100) / 400.0f;
    clock_t t0 = clock();
    for (int b = 0; b < RATE * 60 / 256; b++) {
        r.process(noise.data(), out.data(), 256, 0.5f);
        c.process(noise.data(), out.data(), 256, 0.5f);
    }
    const double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("  effects: reverb+chorus %.2f%% CPU host (60 s in %.3f s)\n", 100.0 * secs / 60.0, secs);
    r.clear();
    std::fill(out.begin(), out.end(), 0.0f);
    std::fill(in.begin(), in.end(), 0.0f);
    r.process(in.data(), out.data(), 256, 1.0f);
    bool silent = true;
    for (float v : out) silent = silent && v == 0.0f;
    CHECK(silent);
}

static void testRender(const char *font, const char *song, const char *out)
{
    const int RATE = 22050;
    FILE *fp = fopen(song, "rb");
    CHECK(fp != nullptr);
    if (!fp) return;
    std::vector<uint8_t> d(Midi::MAX_FILE + 1);
    d.resize(fread(d.data(), 1, d.size(), fp));
    fclose(fp);
    Midi::Song sg;
    CHECK(Midi::parse(d.data(), d.size(), sg) == Midi::Err::OK);

    Sf::Synth s;
    timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    Sf::LoadErr le = s.open(font, RATE);
    clock_gettime(CLOCK_MONOTONIC, &b);
    CHECK(le == Sf::LoadErr::OK);
    if (le != Sf::LoadErr::OK) return;
    const double tOpen = (double)(b.tv_sec - a.tv_sec) + (double)(b.tv_nsec - a.tv_nsec) / 1e9;
    auto info = [](const Sf::PrepStats &st) {
        return st.thinned ? " (one zone per key)" : st.reduced ? " (one velocity layer)" : "";
    };

    // Without resampling first (bit-identical to the full load when the file fits in RAM). The
    // budget is unlimited here so the output shows what the song really needs.
    const size_t NO_LIMIT = (size_t)1 << 30;
    const Sf::Effects off = {false, 0, false, 0};
    s.setResampling(false);
    const Sf::LoadErr leFull = s.prepare(sg, nullptr, nullptr, NO_LIMIT);
    const Sf::PrepStats stFull = s.stats();
    if (leFull == Sf::LoadErr::OK)
        printf("  bank %.1f MB, %d presets: hydra %.3f s; no resampling: %.2f MB%s\n", (double)s.fileSize() / 1048576.0,
               s.presetCount(), tOpen, stFull.bytes / 1048576.0, info(stFull));
    else
        printf("  bank %.1f MB, %d presets: hydra %.3f s; no resampling: %s\n", (double)s.fileSize() / 1048576.0,
               s.presetCount(), tOpen, Sf::loadErrName(leFull));
    std::vector<int16_t> plain;   // First 60 s without resampling (for the comparison)
    if (leFull == Sf::LoadErr::OK) {
        s.setEffects(off);
        if (s.fileSize() <= Sf::MAX_SF2 && !stFull.reduced) {
            Sf::Synth full;
            full.setEffects(off);
            if (full.load(font, RATE) == Sf::LoadErr::OK) {
                const std::vector<int16_t> x = renderSong(full, sg, 30), y = renderSong(s, sg, 30);
                const bool same = x.size() == y.size() && memcmp(x.data(), y.data(), x.size() * 2) == 0;
                CHECK(same);
                printf("  selective == full (first %.0f s): %s\n", (double)x.size() / RATE, same ? "identical" : "DIFFERENT");
            }
        }
        plain = renderSong(s, sg, 60);
        if (out) {
            std::string o = std::string(out) + ".noresample.wav";
            wav(o.c_str(), plain, RATE);
        }
    }

    // With resampling (the device default)
    s.setResampling(true);
    clock_gettime(CLOCK_MONOTONIC, &b);
    le = s.prepare(sg, nullptr, nullptr, NO_LIMIT);
    clock_gettime(CLOCK_MONOTONIC, &a);
    const double tPrep = (double)(a.tv_sec - b.tv_sec) + (double)(a.tv_nsec - b.tv_nsec) / 1e9;
    CHECK(le == Sf::LoadErr::OK || le == Sf::LoadErr::TOO_BIG);   // TOO_BIG is a measurement result here
    if (le != Sf::LoadErr::OK) {
        printf("  %s: prepare failed: %s\n", song, Sf::loadErrName(le));
        return;
    }
    const Sf::PrepStats &st = s.stats();
    printf("  resampling: song needs %.2f MB (%.1f%%), %u presets, %u zones (%u resampled, %.2f MB saved), %u reads%s, %.3f s\n",
           st.bytes / 1048576.0, 100.0 * st.bytes / (double)s.fileSize(), st.presets, st.regions, st.resampled,
           st.resampledSaved / 1048576.0, st.ranges, info(st), tPrep);
    if (!plain.empty() && !st.reduced && !stFull.reduced) {
        s.setEffects(off);
        const std::vector<int16_t> y = renderSong(s, sg, 60);
        printf("  resampled vs not (first 60 s, dry): rel. diff %.3f\n", relDiff(plain, y));
    }
    // Typical device budgets (largest free PSRAM block - reserve): what is loaded at 6 and 7 MB
    for (size_t mb = 6; mb <= 7; mb++) {
        const Sf::LoadErr lb = s.prepare(sg, nullptr, nullptr, mb * 1048576);
        const Sf::PrepStats &sb = s.stats();
        if (lb == Sf::LoadErr::OK) printf("  budget %u MB: %.2f MB%s\n", (unsigned)mb, sb.bytes / 1048576.0, info(sb));
        else printf("  budget %u MB: %s\n", (unsigned)mb, Sf::loadErrName(lb));
    }
    CHECK(s.prepare(sg, nullptr, nullptr, NO_LIMIT) == Sf::LoadErr::OK);

    // Whole song: dry, then with reverb + chorus
    for (int pass = 0; pass < 2; pass++) {
        s.setEffects(pass ? Sf::Effects() : off);
        clock_t t0 = clock();
        const std::vector<int16_t> pcm = renderSong(s, sg, 1e9);
        const double secs = (double)(clock() - t0) / CLOCKS_PER_SEC, songSecs = (double)pcm.size() / RATE;
        long clip = 0;
        for (int16_t v : pcm) clip += v == 32767 || v == -32768;
        const double rms = rmsOf(pcm.data(), pcm.size());
        printf("  %s %s: %.0f s audio rendered in %.2f s (%.1f%% CPU host), rms %.0f, clipped %ld\n", song,
               pass ? "fx" : "dry", songSecs, secs, 100.0 * secs / songSecs, rms, clip);
        CHECK(rms > 300);
        CHECK(clip < (long)pcm.size() / 1000);
        if (out && pass == 0) wav(out, std::vector<int16_t>(pcm.begin(), pcm.begin() + (pcm.size() < (size_t)RATE * 60 ? pcm.size() : (size_t)RATE * 60)), RATE);
    }
}

int main(int argc, char **argv)
{
    testErrors();
    testProbe();
    testEffects();
    testSelective();
    testResampling();
    if (argc >= 3) testRender(argv[1], argv[2], argc >= 4 ? argv[3] : nullptr);
    CHECK_DONE();
}
