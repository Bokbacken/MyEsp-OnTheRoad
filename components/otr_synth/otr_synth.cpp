// otr_synth.cpp - SoundFont synth wrapper (see otr_synth.h).
#include "otr_synth.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <algorithm>
#include <vector>

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
int fileTell(void *data) { return (int)ftell((FILE *)data); }

void *scanAlloc(size_t n)   // Small scratch tables
{
#ifdef ESP_PLATFORM
    void *p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : calloc(1, n);
#else
    return calloc(1, n);
#endif
}
void scanFree(void *p)
{
#ifdef ESP_PLATFORM
    heap_caps_free(p);
#else
    free(p);
#endif
}

inline void setBit(uint64_t *m, int i) { m[i >> 6] |= 1ull << (i & 63); }
inline bool testBit(const uint64_t *m, int i) { return (m[i >> 6] >> (i & 63)) & 1; }
inline bool anyBit(const uint64_t *m, int lo, int hi)
{
    if (lo < 0) lo = 0;
    if (hi > 127) hi = 127;
    for (int i = lo; i <= hi; i++)
        if ((m[i >> 6] >> (i & 63)) & 1) return true;
    return false;
}
// TinySoundFont's own velocity conversion (tsf_note_on: (short)(vel * 127)), so the scan selects the
// same zones as playback even where the float round trip loses one step.
inline int tsfVelocity(int d2) { return (int)(short)((d2 / 127.0f) * 127); }

constexpr unsigned MERGE_GAP = 2048;   // Samples (4 kB): closer ranges are read in one go
constexpr unsigned PAD = 4;            // Silent samples at the buffer start and end
constexpr size_t PSRAM_RESERVE = 512u * 1024;   // Left free for the song, LVGL and the rest
}  // namespace

const char *loadErrName(LoadErr e)
{
    switch (e) {
    case LoadErr::OK: return "ok";
    case LoadErr::NO_FILE: return "no file";
    case LoadErr::TOO_BIG: return "too big";
    case LoadErr::BAD_FILE: return "bad file";
    case LoadErr::NO_MEMORY: return "no memory";
    case LoadErr::COMPRESSED: return "compressed (sf3)";
    }
    return "?";
}

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

LoadErr probe(const uint8_t *d, size_t n, size_t fileSize)
{
    if (fileSize <= 12 || n < 12) return LoadErr::BAD_FILE;
    if (memcmp(d, "RIFF", 4) || memcmp(d + 8, "sfbk", 4)) return LoadErr::BAD_FILE;
    size_t off = 12;
    while (off + 12 <= n) {
        const uint32_t sz = le32(d + off + 4);
        if (!memcmp(d + off, "LIST", 4) && !memcmp(d + off + 8, "INFO", 4)) {
            const size_t end = off + 8 + (size_t)sz < n ? off + 8 + (size_t)sz : n;
            size_t sub = off + 12;
            while (sub + 8 <= end) {
                const uint32_t ssz = le32(d + sub + 4);
                if (!memcmp(d + sub, "ifil", 4) && ssz >= 4 && sub + 12 <= end) {
                    const unsigned major = (unsigned)d[sub + 8] | (unsigned)d[sub + 9] << 8;
                    return major >= 3 ? LoadErr::COMPRESSED : LoadErr::OK;   // SF3 = Ogg samples
                }
                sub += 8 + (size_t)ssz + (ssz & 1);
            }
            return LoadErr::OK;   // No version chunk: treat as SF2
        }
        off += 8 + (size_t)sz + (sz & 1);
    }
    return LoadErr::OK;
}

LoadErr probeFile(const char *path, size_t *sizeOut)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return LoadErr::NO_FILE;
    fseek(fp, 0, SEEK_END);
    const long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t hdr[256];
    const size_t n = fread(hdr, 1, sizeof(hdr), fp);
    fclose(fp);
    if (sizeOut) *sizeOut = sz > 0 ? (size_t)sz : 0;
    return probe(hdr, n, sz > 0 ? (size_t)sz : 0);
}

Synth::Synth()
{
    memset(playedAs_, 0xFF, sizeof(playedAs_));
    memset(revSend_, 40, sizeof(revSend_));
    memset(choSend_, 0, sizeof(choSend_));
}
Synth::~Synth() { unload(); }

// Opens the file and validates its header. Returns the size via *sz.
static LoadErr openChecked(const char *path, FILE **out, long *sz)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return LoadErr::NO_FILE;
    fseek(fp, 0, SEEK_END);
    *sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t hdr[256];
    const size_t got = *sz > 0 ? fread(hdr, 1, sizeof(hdr), fp) : 0;
    const LoadErr pe = probe(hdr, got, *sz > 0 ? (size_t)*sz : 0);
    if (pe != LoadErr::OK) {
        fclose(fp);
        return pe;
    }
    fseek(fp, 0, SEEK_SET);
    *out = fp;
    return LoadErr::OK;
}

LoadErr Synth::load(const char *path, int sampleRate)
{
    unload();
    FILE *fp = nullptr;
    long sz = 0;
    const LoadErr pe = openChecked(path, &fp, &sz);
    if (pe != LoadErr::OK) return pe;
    if ((size_t)sz > MAX_SF2) {
        fclose(fp);
        return LoadErr::TOO_BIG;
    }
    setvbuf(fp, nullptr, _IOFBF, 16384);
    tsf_stream st = {fp, &fileRead, &fileSkip, &fileTell};
    f_ = tsf_load(&st);
    fclose(fp);
    if (!f_) return LoadErr::NO_MEMORY;   // Parse errors and allocation failures look the same
    size_ = (size_t)sz;
    full_ = true;
    rate_ = sampleRate;
    tsf_set_output(f_, TSF_MONO, sampleRate, -14.0f);   // Headroom: dense songs (30+ voices) must not clip
    if (!tsf_set_max_voices(f_, MAX_VOICES)) { unload(); return LoadErr::NO_MEMORY; }
    initFx();
    reset();
    return LoadErr::OK;
}

LoadErr Synth::open(const char *path, int sampleRate)
{
    if (hydra_ && f_ && !strcmp(path, path_) && sampleRate == rate_) {   // Same bank: keep the hydra
        allNotesOff();
        return LoadErr::OK;
    }
    unload();
    FILE *fp = nullptr;
    long sz = 0;
    const LoadErr pe = openChecked(path, &fp, &sz);
    if (pe != LoadErr::OK) return pe;
    setvbuf(fp, nullptr, _IOFBF, 16384);
    tsf_stream st = {fp, &fileRead, &fileSkip, &fileTell};
    hydra_ = (tsf_hydra *)scanAlloc(sizeof(tsf_hydra));
    if (!hydra_) {
        fclose(fp);
        return LoadErr::NO_MEMORY;
    }
    tsf_u32 pos = 0;
    f_ = tsf_load_impl(&st, hydra_, &pos);
    fclose(fp);
    if (!f_) {
        scanFree(hydra_);
        hydra_ = nullptr;
        return LoadErr::NO_MEMORY;
    }
    smplPos_ = pos;
    size_ = (size_t)sz;
    rate_ = sampleRate;
    snprintf(path_, sizeof(path_), "%s", path);
    tsf_set_output(f_, TSF_MONO, sampleRate, -14.0f);
    if (!tsf_set_max_voices(f_, MAX_VOICES)) { unload(); return LoadErr::NO_MEMORY; }
    initFx();
    reset();
    return LoadErr::OK;
}

void Synth::unload()
{
    if (f_) tsf_close(f_);   // Frees presets/regions and the sample buffer
    f_ = nullptr;
    if (hydra_) {
        tsf_hydra_free(hydra_);
        scanFree(hydra_);
        hydra_ = nullptr;
    }
    freeScan();
    path_[0] = 0;
    smplPos_ = 0;
    size_ = 0;
    full_ = prepared_ = scanning_ = false;
    stats_ = PrepStats();
}

void Synth::freeScan()
{
    scanFree(keyMask_);
    scanFree(velMask_);
    keyMask_ = velMask_ = nullptr;
}

int Synth::presetCount() const { return f_ ? f_->presetNum : 0; }

void Synth::recordNote(int ch, int key, int vel)
{
    const int pi = tsf_channel_get_preset_index(f_, ch);
    if (pi < 0 || pi >= f_->presetNum) return;
    uint64_t *km = keyMask_ + 2 * pi, *vm = velMask_ + 2 * pi;
    if (ch == Midi::DRUM_CH) {
        setBit(km, key);
    } else {
        for (int k = key - KEY_MARGIN; k <= key + KEY_MARGIN; k++)
            if (k >= 0 && k <= 127) setBit(km, k);
    }
    setBit(vm, vel);
}

namespace {
struct Range {
    uint32_t lo, hi;        // Sample indices in the file's smpl chunk, [lo, hi)
    tsf_region *region;
};
}  // namespace

LoadErr Synth::prepare(const Midi::Song &song, ProgressFn cb, void *user, size_t budget)
{
    if (!f_ || !hydra_) return LoadErr::NO_FILE;
    allNotesOff();
    prepared_ = false;
    stats_ = PrepStats();
    if (f_->fontSamples) {
        TSF_FREE(f_->fontSamples);
        f_->fontSamples = nullptr;
    }
    if (cb) cb(user, 0);
    const int np = f_->presetNum;

    // 1. Scan: run the song's events through TinySoundFont's channel logic (bank select, program
    //    changes, drum fallback) and record the preset, keys and velocities of every note-on.
    freeScan();
    keyMask_ = (uint64_t *)scanAlloc(sizeof(uint64_t) * 2 * (size_t)(np > 0 ? np : 1));
    velMask_ = (uint64_t *)scanAlloc(sizeof(uint64_t) * 2 * (size_t)(np > 0 ? np : 1));
    unsigned char *want = (unsigned char *)scanAlloc((size_t)(np > 0 ? np : 1));
    if (!keyMask_ || !velMask_ || !want) {
        freeScan();
        scanFree(want);
        return LoadErr::NO_MEMORY;
    }
    scanning_ = true;
    reset();
    for (const Midi::Event &e : song.events) midi(e.status, e.d1, e.d2);
    scanning_ = false;
    for (int i = 0; i < np; i++)
        if (keyMask_[2 * i] | keyMask_[2 * i + 1]) {
            want[i] = 1;
            stats_.presets++;
        }
    if (cb) cb(user, 5);

    // 2. Regions for the used presets only
    if (!tsf_rebuild_presets(f_, hydra_, want)) {
        scanFree(want);
        freeScan();
        return LoadErr::NO_MEMORY;
    }
    scanFree(want);

    size_t limit = budget ? budget : SAMPLE_BUDGET;
#ifdef ESP_PLATFORM
    const size_t freeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    const size_t avail = freeBlock > PSRAM_RESERVE ? freeBlock - PSRAM_RESERVE : 0;
    if (avail < limit) limit = avail;
#endif
    const uint32_t smplCount = hydra_->smplCount;

    // Last resort (pass 2): one layer per key. Big banks stack zones on the same key and velocity
    // (stereo pairs, panned double layers); the output is mono, so the first zone covering a key
    // plays alone. Each kept zone that lost a partner gets louder: +6 dB for a stereo pair (the
    // same signal summed), +3 dB for different layers (power sum).
    std::vector<std::pair<uint32_t, int>> starts;   // Sample start -> shdr index
    starts.reserve((size_t)(hydra_->shdrNum > 0 ? hydra_->shdrNum : 0));
    for (int i = 0; i < hydra_->shdrNum; i++) starts.push_back({hydra_->shdrs[i].start, i});
    std::sort(starts.begin(), starts.end());
    auto stereo = [&](const tsf_region &rg) {
        auto it = std::upper_bound(starts.begin(), starts.end(), std::make_pair(rg.offset, INT32_MAX));
        if (it == starts.begin()) return false;
        const tsf_hydra_shdr &sh = hydra_->shdrs[(--it)->second];
        return rg.offset < sh.end && (sh.sampleType & 6) != 0;   // rightSample / leftSample
    };
    std::vector<tsf_region *> extra;                       // Dropped in pass 2 (sorted)
    std::vector<std::pair<tsf_region *, float>> boost;     // Kept zone -> gain to add (dB)
    for (int pi = 0; pi < np; pi++) {
        tsf_preset &p = f_->presets[pi];
        const uint64_t *km = keyMask_ + 2 * pi;
        tsf_region *chosen[128] = {};   // First candidate zone per used key
        std::vector<tsf_region *> cand;
        for (int r = 0; r < p.regionNum; r++) {
            tsf_region &rg = p.regions[r];
            if (rg.lovel > REDUCED_VELOCITY || rg.hivel < REDUCED_VELOCITY || !anyBit(km, rg.lokey, rg.hikey)) continue;
            cand.push_back(&rg);
            for (int k = rg.lokey; k <= rg.hikey && k < 128; k++)
                if (testBit(km, k) && !chosen[k]) chosen[k] = &rg;
        }
        for (tsf_region *rg : cand) {
            bool used = false;
            for (int k = rg->lokey; k <= rg->hikey && k < 128; k++) used = used || chosen[k] == rg;
            if (used) continue;
            extra.push_back(rg);
            const float db = stereo(*rg) ? 6.0f : 3.0f;
            for (int k = rg->lokey; k <= rg->hikey && k < 128; k++)
                if (chosen[k]) boost.push_back({chosen[k], db});
        }
    }
    std::sort(extra.begin(), extra.end());

    // 3. Sample ranges of the zones the song can trigger; over budget -> keep one velocity layer,
    //    still over -> also one layer per key
    std::vector<Range> ranges;
    uint32_t total = 0;
    const int passes = extra.empty() ? 2 : 3;
    for (int pass = 0; pass < passes; pass++) {
        const bool reduce = pass >= 1, thin = pass == 2;
        ranges.clear();
        for (int pi = 0; pi < np; pi++) {
            tsf_preset &p = f_->presets[pi];
            const uint64_t *km = keyMask_ + 2 * pi, *vm = velMask_ + 2 * pi;
            for (int r = 0; r < p.regionNum; r++) {
                tsf_region &rg = p.regions[r];
                if (!anyBit(km, rg.lokey, rg.hikey)) continue;
                if (reduce) {
                    if (rg.lovel > REDUCED_VELOCITY || rg.hivel < REDUCED_VELOCITY) continue;
                } else if (!anyBit(vm, rg.lovel, rg.hivel)) {
                    continue;
                }
                if (thin && std::binary_search(extra.begin(), extra.end(), &rg)) continue;
                if (rg.end <= rg.offset || rg.offset >= smplCount) continue;
                uint32_t lo = rg.offset, hi = rg.end + 1;   // TSF interpolates with input[end]
                if (rg.loop_mode != TSF_LOOPMODE_NONE && rg.loop_start < rg.loop_end) {
                    if (rg.loop_start < lo) lo = rg.loop_start;
                    if (rg.loop_end + 2 > hi) hi = rg.loop_end + 2;
                }
                if (hi > smplCount) hi = smplCount;
                ranges.push_back({lo, hi, &rg});
            }
        }
        std::sort(ranges.begin(), ranges.end(), [](const Range &a, const Range &b) { return a.lo < b.lo; });
        total = 0;
        uint32_t curLo = 0, curHi = 0;
        bool open = false;
        for (const Range &r : ranges) {
            if (open && r.lo <= curHi + MERGE_GAP) {
                if (r.hi > curHi) curHi = r.hi;
                continue;
            }
            if (open) total += curHi - curLo;
            curLo = r.lo, curHi = r.hi, open = true;
        }
        if (open) total += curHi - curLo;
        if ((size_t)(total + 2 * PAD) * 2 <= limit) {
            stats_.reduced = reduce;
            stats_.thinned = thin;
            break;
        }
        if (pass == passes - 1) {
            freeScan();
            reset();
            return LoadErr::TOO_BIG;
        }
    }

    // Reduced: the kept layer answers every velocity; the dropped layers can no longer match
    if (stats_.reduced) {
        for (int pi = 0; pi < np; pi++) {
            tsf_preset &p = f_->presets[pi];
            for (int r = 0; r < p.regionNum; r++) {
                tsf_region &rg = p.regions[r];
                if (rg.lovel <= REDUCED_VELOCITY && rg.hivel >= REDUCED_VELOCITY) rg.lovel = 0, rg.hivel = 127;
                else rg.lovel = 127, rg.hivel = 0;
            }
        }
    }

    // Thinned: a kept zone gets the largest boost of the partners it lost (at most +6 dB)
    if (stats_.thinned) {
        std::sort(boost.begin(), boost.end());
        for (size_t i = 0; i < boost.size(); i++)
            if (i + 1 == boost.size() || boost[i + 1].first != boost[i].first) boost[i].first->attenuation -= boost[i].second;
    }

    // 4. Read the merged ranges in file order into one buffer
    short *buf = (short *)TSF_MALLOC(((size_t)total + 2 * PAD) * sizeof(short));
    if (!buf) {
        freeScan();
        reset();
        return LoadErr::NO_MEMORY;
    }
    memset(buf, 0, PAD * sizeof(short));
    memset(buf + PAD + total, 0, PAD * sizeof(short));
    FILE *fp = fopen(path_, "rb");
    if (!fp) {
        TSF_FREE(buf);
        freeScan();
        reset();
        return LoadErr::NO_FILE;
    }
    setvbuf(fp, nullptr, _IOFBF, 16384);
    std::vector<uint32_t> base(ranges.size());   // Per range: start of its merged block (lo) ...
    std::vector<uint32_t> dest(ranges.size());   // ... and where that block lives in buf
    uint32_t done = 0, out = PAD;
    bool ok = true;
    for (size_t i = 0; i < ranges.size() && ok;) {
        uint32_t lo = ranges[i].lo, hi = ranges[i].hi;
        size_t j = i + 1;
        while (j < ranges.size() && ranges[j].lo <= hi + MERGE_GAP) {
            if (ranges[j].hi > hi) hi = ranges[j].hi;
            j++;
        }
        for (size_t k = i; k < j; k++) base[k] = lo, dest[k] = out;
        if (fseek(fp, (long)(smplPos_ + (uint64_t)lo * 2), SEEK_SET) != 0) ok = false;
        uint32_t left = hi - lo;
        while (ok && left) {
            const uint32_t step = left > 16384 ? 16384 : left;
            if (fread(buf + out, sizeof(short), step, fp) != step) ok = false;
            out += step, left -= step, done += step;
            if (cb && total) cb(user, 5 + (int)((uint64_t)done * 95 / total));
        }
        stats_.ranges++;
        i = j;
    }
    fclose(fp);
    if (!ok) {
        TSF_FREE(buf);
        freeScan();
        reset();
        return LoadErr::BAD_FILE;
    }

    // 5. Re-point the zones: kept ones into buf, the rest can never sound
    std::vector<tsf_region *> kept(ranges.size());
    for (size_t i = 0; i < ranges.size(); i++) {
        tsf_region &rg = *ranges[i].region;
        const uint32_t d = dest[i] - base[i];
        rg.offset += d, rg.end += d, rg.loop_start += d, rg.loop_end += d;
        kept[i] = &rg;
    }
    std::sort(kept.begin(), kept.end());
    for (int pi = 0; pi < np; pi++) {
        tsf_preset &p = f_->presets[pi];
        for (int r = 0; r < p.regionNum; r++) {
            tsf_region *rg = &p.regions[r];
            if (std::binary_search(kept.begin(), kept.end(), rg)) continue;
            rg->offset = rg->end = rg->loop_start = rg->loop_end = 0;
            rg->lovel = 127, rg->hivel = 0;
        }
    }
    stats_.regions = (uint16_t)(ranges.size() > 0xFFFF ? 0xFFFF : ranges.size());
    stats_.bytes = total * 2;
    f_->fontSamples = buf;
    freeScan();
    prepared_ = true;
    reset();
    if (cb) cb(user, 100);
    return LoadErr::OK;
}

void Synth::initFx()
{
    if (fx_.reverb) reverb_.init(rate_);
    if (fx_.chorus) chorus_.init(rate_);
}

void Synth::setEffects(const Effects &e)
{
    fx_ = e;
    if (fx_.reverbLevel > 100) fx_.reverbLevel = 100;
    if (fx_.chorusLevel > 100) fx_.chorusLevel = 100;
    if (f_) initFx();
    if (!fx_.reverb) reverb_.clear();
    if (!fx_.chorus) chorus_.clear();
}

void Synth::reset()
{
    memset(playedAs_, 0xFF, sizeof(playedAs_));
    memset(revSend_, 40, sizeof(revSend_));   // GM defaults
    memset(choSend_, 0, sizeof(choSend_));
    reverb_.clear();
    chorus_.clear();
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
            if (scanning_) {
                recordNote(ch, d1 & 0x7F, tsfVelocity(d2 & 0x7F));
                return;
            }
            if (!loaded() || ((muteMask_ >> ch) & 1)) return;
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
        if (scanning_) return;
        if (playedAs_[ch][d1] != 0xFF) {
            tsf_channel_note_off(f_, ch, playedAs_[ch][d1]);
            playedAs_[ch][d1] = 0xFF;
        }
        return;
    case 0xB0:
        if (d1 == 91) revSend_[ch] = d2 & 0x7F;
        else if (d1 == 93) choSend_[ch] = d2 & 0x7F;
        if (scanning_ && (d1 == 64 || d1 == 120 || d1 == 123)) return;   // No voices to touch
        tsf_channel_midi_control(f_, ch, d1, d2);
        return;
    case 0xC0: tsf_channel_set_presetnumber(f_, ch, d1, ch == Midi::DRUM_CH); return;
    case 0xE0:
        if (!scanning_) tsf_channel_set_pitchwheel(f_, ch, d1 | (d2 << 7));
        return;
    default: return;
    }
}

// Voices are rendered in groups of channels that share the same CC91/CC93 sends (usually just one
// or two groups), so each group feeds the send buses with its own level.
void Synth::renderFx(int n, bool rev, bool cho)
{
    memset(dry_, 0, sizeof(float) * (size_t)n);
    if (rev) memset(rev_, 0, sizeof(float) * (size_t)n);
    if (cho) memset(cho_, 0, sizeof(float) * (size_t)n);
    tsf_voice *v0 = f_->voices, *vEnd = v0 + f_->voiceNum;
    uint16_t pending = 0;   // Channels with active voices
    for (tsf_voice *v = v0; v != vEnd; v++)
        if (v->playingPreset != -1) pending |= (uint16_t)(1u << (v->playingChannel & 15));
    while (pending) {
        const int first = __builtin_ctz(pending);
        const uint8_t r = rev ? revSend_[first] : 0, c = cho ? choSend_[first] : 0;
        uint16_t group = 0;
        for (int ch = first; ch < 16; ch++)
            if (((pending >> ch) & 1) && (rev ? revSend_[ch] : 0) == r && (cho ? choSend_[ch] : 0) == c) group |= (uint16_t)(1u << ch);
        pending &= (uint16_t)~group;
        float *dst = (r || c) ? tmp_ : dry_;   // No sends: straight into the dry bus
        if (dst == tmp_) memset(tmp_, 0, sizeof(float) * (size_t)n);
        for (tsf_voice *v = v0; v != vEnd; v++)
            if (v->playingPreset != -1 && ((group >> (v->playingChannel & 15)) & 1)) tsf_voice_render(f_, v, dst, n);
        if (dst == dry_) continue;
        const float rs = r * (1.0f / 127.0f), cs = c * (1.0f / 127.0f);
        for (int i = 0; i < n; i++) dry_[i] += tmp_[i];
        if (r) for (int i = 0; i < n; i++) rev_[i] += tmp_[i] * rs;
        if (c) for (int i = 0; i < n; i++) cho_[i] += tmp_[i] * cs;
    }
    if (rev) reverb_.process(rev_, dry_, n, fx_.reverbLevel * 0.01f);
    if (cho) chorus_.process(cho_, dry_, n, fx_.chorusLevel * 0.01f);
}

void Synth::render(int32_t *acc, int n, int gainQ8)
{
    if (!loaded()) return;
    const float g = 32767.0f * (float)gainQ8 / 256.0f;
    const bool rev = fx_.reverb && fx_.reverbLevel && reverb_.ready();
    const bool cho = fx_.chorus && fx_.chorusLevel && chorus_.ready();
    while (n > 0) {
        const int k = n > 256 ? 256 : n;
        if (rev || cho) renderFx(k, rev, cho);
        else tsf_render_float(f_, dry_, k, 0);
        for (int i = 0; i < k; i++) {
            float v = dry_[i] * g;
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
