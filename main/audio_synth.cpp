// audio_synth.cpp - see audio_synth.h. Pure C++, no ESP-IDF dependencies.
#include "audio_synth.h"

#include <math.h>
#include <string.h>

namespace Synth {

namespace {

int16_t s_sine[256];
bool s_sineReady = false;

void initSine()
{
    if (s_sineReady) return;
    for (int i = 0; i < 256; i++)
        s_sine[i] = (int16_t)lrintf(32767.0f * sinf((float)i * 6.2831853f / 256.0f));
    s_sineReady = true;
}

inline char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

uint32_t msToSamples(uint32_t ms)
{
    uint32_t s = (uint32_t)((uint64_t)ms * RATE / 1000);
    return s ? s : 1;
}

uint32_t hzToInc(float hz) { return (uint32_t)(hz * (4294967296.0f / (float)RATE)); }

}  // namespace

float midiHz(int note) { return 440.0f * powf(2.0f, (float)(note - 69) / 12.0f); }

void Voice::start(const char *mml)
{
    initSine();
    src_ = pos_ = mml;
    done_ = (mml == nullptr || *mml == 0);
    error_ = false;
    octave_ = 4; defLen_ = 8; tempo_ = 120; vol_ = 12; gate_ = 7; slide_ = 0;
    wave_ = Wave::SQUARE50;
    atkMs_ = 2; decMs_ = 60; susLvl_ = 10; relMs_ = 40;
    rest_ = true; noteLen_ = gateLen_ = t_ = 0;
    phase_ = 0; inc_ = 0; incStep_ = 0;
    lfsr_ = 0x7FFF; noiseOut_ = 0; noisePhase_ = 0;
    env_ = relFrom_ = 0;
}

// Reads an unsigned decimal number at pos_. false (and pos_ unchanged) if there is no digit.
static bool readNum(const char *&p, int &v)
{
    if (*p < '0' || *p > '9') return false;
    long n = 0;
    while (*p >= '0' && *p <= '9') {
        if (n < 100000) n = n * 10 + (*p - '0');
        p++;
    }
    v = (int)n;
    return true;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool Voice::nextNote()
{
    static const int8_t SEMI[7] = {9, 11, 0, 2, 4, 5, 7};   // a b c d e f g
    if (!pos_) return false;
    while (*pos_) {
        char c = lower(*pos_++);
        int n = 0;
        switch (c) {
        case ' ': case '|': case '\n': case '\r': case '\t':
            break;
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': case 'r': {
            int midi = -1;
            if (c != 'r') {
                midi = (octave_ + 1) * 12 + SEMI[c - 'a'];
                while (*pos_ == '+' || *pos_ == '#' || *pos_ == '-') midi += (*pos_++ == '-') ? -1 : 1;
            }
            int len = defLen_;
            if (readNum(pos_, n)) {
                if (n < 1 || n > 64) error_ = true;
                len = clampi(n, 1, 64);
            }
            uint64_t base = (uint64_t)RATE * 240 / ((uint64_t)tempo_ * (uint64_t)len);
            uint64_t total = base;
            while (*pos_ == '.') { base /= 2; total += base; pos_++; }
            beginNote(midi, (uint32_t)(total ? total : 1));
            return true;
        }
        case 'x': {
            // Envelope x<a>,<d>,<s>,<r>
            int a = 0, d = 0, s = 0, r = 0;
            bool ok = readNum(pos_, a);
            ok = ok && *pos_ == ',' && (++pos_, readNum(pos_, d));
            ok = ok && *pos_ == ',' && (++pos_, readNum(pos_, s));
            ok = ok && *pos_ == ',' && (++pos_, readNum(pos_, r));
            if (!ok) { error_ = true; break; }
            atkMs_ = (uint16_t)clampi(a, 0, 5000);
            decMs_ = (uint16_t)clampi(d, 0, 5000);
            susLvl_ = (uint16_t)clampi(s, 0, 15);
            relMs_ = (uint16_t)clampi(r, 0, 5000);
            break;
        }
        case 'o':
            if (readNum(pos_, n)) octave_ = clampi(n, 1, 7); else error_ = true;
            break;
        case '<': if (octave_ > 1) octave_--; break;
        case '>': if (octave_ < 7) octave_++; break;
        case 'l':
            if (readNum(pos_, n) && n >= 1 && n <= 64) defLen_ = n; else error_ = true;
            break;
        case 't':
            if (readNum(pos_, n)) tempo_ = clampi(n, 30, 300); else error_ = true;
            break;
        case 'v':
            if (readNum(pos_, n)) vol_ = clampi(n, 0, 15); else error_ = true;
            break;
        case 'q':
            if (readNum(pos_, n)) gate_ = clampi(n, 1, 8); else error_ = true;
            break;
        case '@':
            if (readNum(pos_, n) && n < (int)Wave::COUNT) wave_ = (Wave)n; else error_ = true;
            break;
        case 's': {
            bool neg = false;
            if (*pos_ == '-' || *pos_ == '+') neg = (*pos_++ == '-');
            if (readNum(pos_, n)) slide_ = clampi(neg ? -n : n, -48, 48); else error_ = true;
            break;
        }
        default:
            error_ = true;
            break;
        }
    }
    return false;
}

void Voice::beginNote(int midi, uint32_t samples)
{
    noteLen_ = samples;
    t_ = 0;
    rest_ = (midi < 0);
    if (rest_) { env_ = 0; return; }
    midi = clampi(midi, 12, 120);
    float hz = midiHz(midi);
    inc_ = hzToInc(hz);
    incStep_ = 0;
    if (slide_) {
        int64_t target = (int64_t)hzToInc(hz * powf(2.0f, (float)slide_ / 12.0f));
        incStep_ = (int32_t)((target - (int64_t)inc_) / (int64_t)samples);
    }
    atk_ = msToSamples(atkMs_);
    dec_ = msToSamples(decMs_);
    rel_ = msToSamples(relMs_);
    gateLen_ = (uint32_t)((uint64_t)samples * (uint32_t)gate_ / 8);
    // Fit the release inside the note so every note ends at zero (no clicks).
    uint32_t room = samples - gateLen_;
    if (rel_ > room) {
        uint32_t minRel = msToSamples(3);
        if (minRel > samples / 2) minRel = samples / 2 ? samples / 2 : 1;
        if (room < minRel) { gateLen_ = samples - minRel; room = minRel; }
        rel_ = room ? room : 1;
    }
    relFrom_ = 0;
}

int32_t Voice::envelope()
{
    const int32_t sus = (int32_t)susLvl_ * 32767 / 15;
    int32_t e;
    if (t_ < gateLen_) {
        if (t_ < atk_) e = (int32_t)((int64_t)32767 * t_ / atk_);
        else if (t_ < atk_ + dec_) e = 32767 - (int32_t)((int64_t)(32767 - sus) * (t_ - atk_) / dec_);
        else e = sus;
        relFrom_ = e;
    } else {
        uint32_t d = t_ - gateLen_;
        e = d < rel_ ? (int32_t)((int64_t)relFrom_ * (rel_ - d) / rel_) : 0;
    }
    t_++;
    return e;
}

bool Voice::render(int32_t *acc, int n, int gainQ8)
{
    if (done_) return false;
    const int32_t ampVol = VOICE_AMP * vol_ / 15;
    for (int i = 0; i < n; i++) {
        if (t_ >= noteLen_) {
            if (!nextNote()) { done_ = true; return false; }
        }
        if (rest_) { t_++; continue; }
        int32_t env = envelope();
        int32_t s;
        switch (wave_) {
        case Wave::SQUARE50: s = (phase_ < 0x80000000u) ? 32767 : -32767; break;
        case Wave::SQUARE25: s = (phase_ < 0x40000000u) ? 32767 : -32767; break;
        case Wave::SQUARE12: s = (phase_ < 0x20000000u) ? 32767 : -32767; break;
        case Wave::TRIANGLE: {
            int32_t p = (int32_t)(phase_ >> 16);
            s = p < 32768 ? p * 2 - 32768 : (65535 - p) * 2 - 32768;
            break;
        }
        case Wave::SAW: s = (int32_t)(phase_ >> 16) - 32768; break;
        case Wave::NOISE: {
            uint64_t a = (uint64_t)noisePhase_ + (uint64_t)inc_ * 8;
            uint32_t steps = (uint32_t)(a >> 32);
            noisePhase_ = (uint32_t)a;
            if (steps > 8) steps = 8;
            while (steps--) {
                uint32_t bit = (lfsr_ ^ (lfsr_ >> 1)) & 1;
                lfsr_ = (lfsr_ >> 1) | (bit << 14);
                noiseOut_ = (lfsr_ & 1) ? 32767 : -32767;
            }
            s = noiseOut_;
            break;
        }
        default: s = s_sine[phase_ >> 24]; break;
        }
        phase_ += inc_;
        if (incStep_) {
            int64_t ni = (int64_t)inc_ + incStep_;
            inc_ = (uint32_t)(ni < 1000 ? 1000 : (ni > 0x7FFFFFFF ? 0x7FFFFFFF : ni));
        }
        int32_t v = (s * ampVol) >> 15;
        v = (v * env) >> 15;
        acc[i] += (v * gainQ8) >> 8;
    }
    return true;
}

int32_t mmlLengthMs(const char *mml)
{
    Voice v;
    v.start(mml);
    uint64_t samples = 0;
    while (v.nextNote()) samples += v.noteLen_;
    if (v.error_) return -1;
    return (int32_t)((samples * 1000 + RATE / 2) / RATE);
}

void SongPlayer::start(const Song &s)
{
    song_ = s;
    playing_ = false;
    for (int i = 0; i < MAX_SONG_VOICES; i++) {
        v_[i].start(s.parts[i]);
        if (!v_[i].done()) playing_ = true;
    }
}

void SongPlayer::stop()
{
    playing_ = false;
    for (auto &v : v_) v.stop();
}

bool SongPlayer::render(int32_t *acc, int n, int gainQ8)
{
    if (!playing_) return false;
    bool any = false;
    for (auto &v : v_)
        if (v.render(acc, n, gainQ8)) any = true;
    if (!any) {
        if (song_.loop) start(song_);
        else playing_ = false;
    }
    return playing_;
}

}  // namespace Synth

namespace Wav {

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool parse(const uint8_t *buf, size_t len, uint32_t fileSize, Info &out)
{
    if (!buf || len < 12 || memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) return false;
    bool haveFmt = false;
    Info info{};
    size_t off = 12;
    while (off + 8 <= len) {
        const uint8_t *ck = buf + off;
        uint32_t size = rd32(ck + 4);
        if (memcmp(ck, "fmt ", 4) == 0) {
            if (size < 16 || off + 8 + 16 > len) return false;
            const uint8_t *f = ck + 8;
            uint16_t tag = rd16(f);
            if (tag == 0xFFFE) {   // WAVE_FORMAT_EXTENSIBLE: sub-format GUID starts at +24
                if (size < 40 || off + 8 + 26 > len || rd16(f + 24) != 1) return false;
            } else if (tag != 1) {
                return false;
            }
            info.channels = rd16(f + 2);
            info.rate = rd32(f + 4);
            info.bits = rd16(f + 14);
            if (info.channels < 1 || info.channels > 2) return false;
            if (info.bits != 8 && info.bits != 16) return false;
            if (info.rate < 8000 || info.rate > 48000) return false;
            haveFmt = true;
        } else if (memcmp(ck, "data", 4) == 0) {
            if (!haveFmt) return false;
            info.dataOffset = (uint32_t)(off + 8);
            info.dataBytes = size;
            if (fileSize) {
                uint32_t avail = fileSize > info.dataOffset ? fileSize - info.dataOffset : 0;
                if (info.dataBytes > avail) info.dataBytes = avail;
            }
            uint32_t frame = info.channels * (info.bits / 8);
            info.dataBytes -= info.dataBytes % frame;
            out = info;
            return true;
        }
        uint64_t next = (uint64_t)off + 8 + size + (size & 1);
        if (next > 0xFFFFFFFFu) return false;
        off = (size_t)next;
    }
    return false;
}

void toMono16(const uint8_t *in, size_t frames, const Info &fmt, int16_t *out)
{
    for (size_t i = 0; i < frames; i++) {
        int32_t l, r;
        if (fmt.bits == 16) {
            const uint8_t *p = in + i * 2 * fmt.channels;
            l = (int16_t)rd16(p);
            r = fmt.channels == 2 ? (int16_t)rd16(p + 2) : l;
        } else {
            const uint8_t *p = in + i * fmt.channels;
            l = ((int32_t)p[0] - 128) << 8;
            r = fmt.channels == 2 ? ((int32_t)p[1] - 128) << 8 : l;
        }
        out[i] = (int16_t)((l + r) / 2);
    }
}

void Resampler::reset(uint32_t srcRate)
{
    if (srcRate < 1000) srcRate = 1000;
    step_ = (uint32_t)(((uint64_t)srcRate << 16) / Synth::RATE);
    frac_ = 0;
    prev_ = 0;
    primed_ = false;
}

size_t Resampler::process(const int16_t *in, size_t nIn, int16_t *out, size_t outCap, size_t &used)
{
    used = 0;
    size_t produced = 0;
    if (!primed_) {
        if (nIn == 0) return 0;
        prev_ = in[used++];
        primed_ = true;
        frac_ = 0;
    }
    while (produced < outCap) {
        while (frac_ >= 65536) {
            if (used >= nIn) return produced;
            prev_ = in[used++];
            frac_ -= 65536;
        }
        if (used >= nIn) break;
        int32_t next = in[used];
        out[produced++] = (int16_t)(prev_ + (int32_t)(((int64_t)(next - prev_) * frac_) >> 16));
        frac_ += step_;
    }
    return produced;
}

}  // namespace Wav
