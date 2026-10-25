// otr_fx.cpp - see otr_fx.h. Reverb after Jezar's public domain Freeverb (tunings for 44.1 kHz,
// scaled to the output rate; room 0.84, damping 0.2).
#include "otr_fx.h"

#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
static float *fxAlloc(int n)   // Internal RAM is faster; PSRAM as fallback
{
    void *p = heap_caps_calloc((size_t)n, sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_calloc((size_t)n, sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return (float *)p;
}
static void fxFree(void *p) { heap_caps_free(p); }
#else
static float *fxAlloc(int n) { return (float *)calloc((size_t)n, sizeof(float)); }
static void fxFree(void *p) { free(p); }
#endif

namespace Sf {

namespace {
constexpr int COMB_TUNING[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr int AP_TUNING[4] = {556, 441, 341, 225};
constexpr float FEEDBACK = 0.84f, DAMP = 0.2f, AP_FEEDBACK = 0.5f, INPUT_GAIN = 0.015f, WET_SCALE = 3.0f;
inline float flush(float x) { return (x > -1e-15f && x < 1e-15f) ? 0.0f : x; }   // No denormals
}  // namespace

Reverb::~Reverb() { fxFree(mem_); }

bool Reverb::init(int sampleRate)
{
    if (mem_) return true;
    const float scale = (float)sampleRate / 44100.0f;
    total_ = 0;
    for (int i = 0; i < COMBS; i++) total_ += combLen_[i] = (int)(COMB_TUNING[i] * scale) + 1;
    for (int i = 0; i < ALLPASSES; i++) total_ += apLen_[i] = (int)(AP_TUNING[i] * scale) + 1;
    mem_ = fxAlloc(total_);
    if (!mem_) return false;
    float *p = mem_;
    for (int i = 0; i < COMBS; i++) comb_[i] = p, p += combLen_[i];
    for (int i = 0; i < ALLPASSES; i++) ap_[i] = p, p += apLen_[i];
    clear();
    return true;
}

void Reverb::clear()
{
    if (mem_) memset(mem_, 0, sizeof(float) * (size_t)total_);
    memset(combPos_, 0, sizeof(combPos_));
    memset(apPos_, 0, sizeof(apPos_));
    memset(combStore_, 0, sizeof(combStore_));
}

// Filter by filter over the block (locals and restrict pointers: the state stays in registers)
void Reverb::process(const float *in, float *out, int n, float level)
{
    if (!mem_) return;
    const float wet = level * WET_SCALE;
    float x[256], acc[256];
    while (n > 0) {
        const int k = n > 256 ? 256 : n;
        for (int s = 0; s < k; s++) x[s] = in[s] * INPUT_GAIN, acc[s] = 0.0f;
        for (int c = 0; c < COMBS; c++) {
            float *__restrict b = comb_[c];
            const int len = combLen_[c];
            int pos = combPos_[c];
            float store = combStore_[c];
            for (int s = 0; s < k; s++) {
                const float y = b[pos];
                store = flush(y * (1.0f - DAMP) + store * DAMP);
                b[pos] = x[s] + store * FEEDBACK;
                if (++pos >= len) pos = 0;
                acc[s] += y;
            }
            combPos_[c] = pos;
            combStore_[c] = store;
        }
        for (int a = 0; a < ALLPASSES; a++) {
            float *__restrict b = ap_[a];
            const int len = apLen_[a];
            int pos = apPos_[a];
            for (int s = 0; s < k; s++) {
                const float bo = b[pos];
                b[pos] = flush(acc[s] + bo * AP_FEEDBACK);
                acc[s] = bo - acc[s];
                if (++pos >= len) pos = 0;
            }
            apPos_[a] = pos;
        }
        for (int s = 0; s < k; s++) out[s] += acc[s] * wet;
        in += k, out += k, n -= k;
    }
}

Chorus::~Chorus() { fxFree(buf_); }

bool Chorus::init(int sampleRate)
{
    if (buf_) return true;
    len_ = sampleRate / 20 + 2;   // 50 ms
    buf_ = fxAlloc(len_);
    if (!buf_) return false;
    base_ = 0.015f * (float)sampleRate;    // 15 ms
    depth_ = 0.004f * (float)sampleRate;   // +-4 ms
    phaseInc_ = 0.7f / (float)sampleRate;  // 0.7 Hz triangle
    clear();
    return true;
}

void Chorus::clear()
{
    if (buf_) memset(buf_, 0, sizeof(float) * (size_t)len_);
    pos_ = 0;
    phase_ = 0;
}

void Chorus::process(const float *in, float *out, int n, float level)
{
    if (!buf_) return;
    for (int s = 0; s < n; s++) {
        buf_[pos_] = in[s];
        phase_ += phaseInc_;
        if (phase_ >= 1.0f) phase_ -= 1.0f;
        const float tri = phase_ < 0.5f ? 4.0f * phase_ - 1.0f : 3.0f - 4.0f * phase_;   // -1..1
        const float d = base_ + depth_ * tri;
        float rp = (float)pos_ - d;
        if (rp < 0) rp += (float)len_;
        const int i0 = (int)rp;
        const float fr = rp - (float)i0;
        const int i1 = i0 + 1 >= len_ ? 0 : i0 + 1;
        out[s] += (buf_[i0] + (buf_[i1] - buf_[i0]) * fr) * level;
        if (++pos_ >= len_) pos_ = 0;
    }
}

}  // namespace Sf
