// otr_synth.cpp - SoundFont synth wrapper (see otr_synth.h).
#include "otr_synth.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#define TSF_MALLOC(n) heap_caps_malloc((n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define TSF_REALLOC(p, n) heap_caps_realloc((p), (n), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define TSF_FREE(p) heap_caps_free(p)
#endif
// Single precision math everywhere (the ESP32-S3 FPU has no double support)
#define TSF_POW(a, b) powf((float)(a), (float)(b))
#define TSF_POWF powf
#define TSF_EXPF expf
#define TSF_LOG(x) logf((float)(x))
#define TSF_TAN(x) tanf((float)(x))
#define TSF_LOG10(x) log10f((float)(x))
#define TSF_SQRT(x) sqrtf((float)(x))
#define TSF_SQRTF sqrtf
#define TSF_NO_STDIO
#define TSF_IMPLEMENTATION
#define TSF_STATIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#include "tsf.h"
#pragma GCC diagnostic pop

namespace Sf {

namespace {
int fileRead(void *data, void *ptr, unsigned int size) { return (int)fread(ptr, 1, size, (FILE *)data) == (int)size ? (int)size : 0; }
int fileSkip(void *data, unsigned int count) { return fseek((FILE *)data, (long)count, SEEK_CUR) == 0; }
}  // namespace

const char *loadErrName(LoadErr e)
{
    switch (e) {
    case LoadErr::OK: return "ok";
    case LoadErr::NO_FILE: return "no file";
    case LoadErr::TOO_BIG: return "too big";
    case LoadErr::BAD_FILE: return "bad file";
    case LoadErr::NO_MEMORY: return "no memory";
    }
    return "?";
}

Synth::Synth() { memset(playedAs_, 0xFF, sizeof(playedAs_)); }
Synth::~Synth() { unload(); }

LoadErr Synth::load(const char *path, int sampleRate)
{
    unload();
    FILE *fp = fopen(path, "rb");
    if (!fp) return LoadErr::NO_FILE;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 12) { fclose(fp); return LoadErr::BAD_FILE; }
    if ((size_t)sz > MAX_SF2) { fclose(fp); return LoadErr::TOO_BIG; }
    char hdr[12];
    if (fread(hdr, 1, 12, fp) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "sfbk", 4)) {
        fclose(fp);
        return LoadErr::BAD_FILE;
    }
    fseek(fp, 0, SEEK_SET);
    setvbuf(fp, nullptr, _IOFBF, 16384);
    tsf_stream st = {fp, &fileRead, &fileSkip};
    f_ = tsf_load(&st);
    fclose(fp);
    if (!f_) return LoadErr::NO_MEMORY;   // Parse errors and allocation failures look the same
    size_ = (size_t)sz;
    tsf_set_output(f_, TSF_MONO, sampleRate, -14.0f);   // Headroom: dense songs (30+ voices) must not clip
    if (!tsf_set_max_voices(f_, MAX_VOICES)) { unload(); return LoadErr::NO_MEMORY; }
    reset();
    return LoadErr::OK;
}

void Synth::unload()
{
    if (f_) tsf_close(f_);
    f_ = nullptr;
    size_ = 0;
}

void Synth::reset()
{
    memset(playedAs_, 0xFF, sizeof(playedAs_));
    if (!f_) return;
    tsf_reset(f_);
    for (int ch = 0; ch < 16; ch++) {
        tsf_channel_set_presetnumber(f_, ch, 0, ch == Midi::DRUM_CH);
        tsf_channel_midi_control(f_, ch, 121, 0);
        tsf_channel_set_pitchwheel(f_, ch, 8192);
        tsf_channel_set_sustain(f_, ch, 0);
    }
}

void Synth::allNotesOff()
{
    memset(playedAs_, 0xFF, sizeof(playedAs_));
    if (f_) tsf_note_off_all(f_);
}

void Synth::midi(uint8_t status, uint8_t d1, uint8_t d2)
{
    if (!f_) return;
    const int ch = status & 0x0F;
    switch (status & 0xF0) {
    case 0x90:
        if (d2) {
            if ((muteMask_ >> ch) & 1) return;
            int key = d1;
            if (ch != Midi::DRUM_CH) key += transpose_;
            if (key < 0 || key > 127) return;
            if (playedAs_[ch][d1] != 0xFF) tsf_channel_note_off(f_, ch, playedAs_[ch][d1]);
            playedAs_[ch][d1] = (uint8_t)key;
            tsf_channel_note_on(f_, ch, key, d2 / 127.0f);
            return;
        }
        [[fallthrough]];
    case 0x80:
        if (playedAs_[ch][d1] != 0xFF) {
            tsf_channel_note_off(f_, ch, playedAs_[ch][d1]);
            playedAs_[ch][d1] = 0xFF;
        }
        return;
    case 0xB0: tsf_channel_midi_control(f_, ch, d1, d2); return;
    case 0xC0: tsf_channel_set_presetnumber(f_, ch, d1, ch == Midi::DRUM_CH); return;
    case 0xE0: tsf_channel_set_pitchwheel(f_, ch, d1 | (d2 << 7)); return;
    default: return;
    }
}

void Synth::render(int32_t *acc, int n, int gainQ8)
{
    if (!f_) return;
    const float g = 32767.0f * (float)gainQ8 / 256.0f;
    while (n > 0) {
        const int k = n > 256 ? 256 : n;
        tsf_render_float(f_, buf_, k, 0);
        for (int i = 0; i < k; i++) {
            float v = buf_[i] * g;
            v = v > 32767.f ? 32767.f : v < -32768.f ? -32768.f : v;
            acc[i] += (int32_t)v;
        }
        acc += k;
        n -= k;
    }
}

void Synth::setTranspose(int semitones)
{
    semitones = semitones < -12 ? -12 : semitones > 12 ? 12 : semitones;
    if (semitones == transpose_) return;
    transpose_ = semitones;
    allNotesOff();
}

void Synth::setMuted(int ch, bool m)
{
    if (ch < 0 || ch > 15) return;
    if (m) {
        muteMask_ |= (uint16_t)(1u << ch);
        if (f_) tsf_channel_note_off_all(f_, ch);
        for (int k = 0; k < 128; k++) playedAs_[ch][k] = 0xFF;
    } else {
        muteMask_ &= (uint16_t)~(1u << ch);
    }
}

int Synth::activeVoices() const { return f_ ? tsf_active_voice_count(f_) : 0; }

}  // namespace Sf
