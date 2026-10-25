// otr_synth.h - SoundFont (.sf2) synthesizer for the karaoke player, built on a patched TinySoundFont
// (MIT, components/otr_synth/tsf.h: 16-bit samples, float filter, fixed-point mono loop, voice
// stealing). Implements Midi::Sink so the Midi::Player can drive it directly.
// Not thread safe: the owner serializes calls (the karaoke engine holds a mutex).
//
// v0.13.2 selective loading (idea from copych/ESP32_SF2_Sampler_Synthesizer, MIT): open() reads
// only the SF2 structure (hydra) and remembers where the sample data starts; prepare(song) scans
// the song for the presets, keys and velocities it really plays (exactly as TinySoundFont selects
// them, incl. bank select and the drum channel), builds regions for those presets only and reads
// just the needed sample ranges from the card into one PSRAM buffer. The bank file can therefore
// be much larger than PSRAM; the budget applies to what one song needs.
//
// v0.13.4 resampling (replaces the 2:1 halving of v0.13.3): every sample whose zones are recorded
// above the output rate (28/30.5/32/44.1/48 kHz for 22.05 kHz out) is converted to about the output
// rate while it is read from the card, with a polyphase Kaiser-windowed sinc (75 dB stopband, cut-off
// 0.45 x the output rate). Saves PSRAM and avoids the aliasing of TinySoundFont's linear
// interpolation when it skips source samples. Loops must stay whole samples: each cluster of
// overlapping zones uses a ratio that makes its shortest loop an exact integer length, other loops
// are rounded and their zone's sample_rate (float in the patched tsf_region) compensates the pitch
// exactly. The scan records the keys exactly as played (with the current transpose); a new
// transpose needs a new prepare().
#pragma once

#include <stdint.h>
#include <stdio.h>

#include "otr_fx.h"
#include "otr_midi.h"

struct tsf;
struct tsf_hydra;

namespace Sf {

constexpr int MAX_VOICES = 32;                      // Voices allocated; setMaxVoices() can use fewer
constexpr size_t MAX_SF2 = 7u * 1024 * 1024;        // load() (whole file in PSRAM) refuses larger files
constexpr size_t PSRAM_RESERVE = 512u * 1024;       // prepare() auto budget: largest free PSRAM block minus this
constexpr size_t HOST_BUDGET = 8u * 1024 * 1024;    // prepare() auto budget off target (tests/tools)
constexpr int REDUCED_VELOCITY = 100;               // Velocity layer kept when a song is over budget
constexpr uint32_t RESAMPLE_MIN_LOOP = 8;           // Loops shorter than this (in output samples) stay at full rate

// TOO_BIG: load(): file > MAX_SF2; prepare(): the song needs more than the budget even with one
// velocity layer and one zone per key.
enum class LoadErr : uint8_t { OK, NO_FILE, TOO_BIG, BAD_FILE, NO_MEMORY, COMPRESSED };
const char *loadErrName(LoadErr e);

// Validates the start of a SoundFont (first ~256 bytes) without loading it: RIFF/sfbk header
// (any size, v0.13.2) and the ifil version (3.x = SF3 with Ogg-compressed samples, which TinySoundFont
// cannot play -> COMPRESSED). Used by load() and for the font list.
LoadErr probe(const uint8_t *data, size_t n, size_t fileSize);
LoadErr probeFile(const char *path, size_t *sizeOut = nullptr);

struct PrepStats {
    uint32_t bytes = 0;        // Sample data loaded for the song
    uint16_t presets = 0;      // Presets the song uses
    uint16_t regions = 0;      // Zones kept
    uint16_t ranges = 0;       // Merged sample ranges read from the card
    uint16_t resampled = 0;    // Zones whose samples were resampled to about the output rate
    uint32_t resampledSaved = 0;   // Bytes saved by resampling
    uint32_t budget = 0;       // Budget used (bytes)
    bool reduced = false;      // Over budget: only the velocity layer at REDUCED_VELOCITY was kept
    bool thinned = false;      // Still over budget: one zone per key (stereo pairs/stacked layers dropped)
};

struct Effects {
    bool reverb = true;
    uint8_t reverbLevel = 50;  // 0..100
    bool chorus = true;
    uint8_t chorusLevel = 50;  // 0..100
};

typedef void (*ProgressFn)(void *user, int pct);

class Synth : public Midi::Sink {
public:
    Synth();
    ~Synth() override;
    LoadErr load(const char *path, int sampleRate);   // Whole file (<= MAX_SF2), all presets
    LoadErr open(const char *path, int sampleRate);   // Structure only; needs prepare() before sound
    // Loads the samples `song` needs (bank opened with open()) for the current transpose. budget 0 =
    // automatic (largest free PSRAM block - PSRAM_RESERVE; HOST_BUDGET off target); the free PSRAM
    // also limits an explicit budget. Keeps the bank open on failure.
    LoadErr prepare(const Midi::Song &song, ProgressFn cb = nullptr, void *user = nullptr, size_t budget = 0);
    void unload();
    bool bankOpen() const { return f_ != nullptr; }
    bool loaded() const { return f_ != nullptr && (full_ || prepared_); }   // Ready to play
    bool selective() const { return hydra_ != nullptr; }
    size_t fileSize() const { return size_; }
    const PrepStats &stats() const { return stats_; }
    int presetCount() const;

    void setEffects(const Effects &e);
    const Effects &effects() const { return fx_; }
    // Resample samples above the output rate in prepare() (default on; takes effect at the next prepare)
    void setResampling(bool on) { resample_ = on; }
    bool resampling() const { return resample_; }
    // Voice limit 1..MAX_VOICES (default MAX_VOICES); applies at once (voices above it are cut)
    void setMaxVoices(int n);
    int maxVoices() const { return maxVoices_; }

    void midi(uint8_t status, uint8_t d1, uint8_t d2) override;
    void reset() override;
    void allNotesOff();   // Release every note (pause)

    // Mixes n mono samples into acc (16-bit scale). gainQ8: 256 = unity.
    void render(int32_t *acc, int n, int gainQ8);

    // -12..12, not applied to the drum channel. A selective bank only has the keys of the last
    // prepare(): call prepare() again after a change.
    void setTranspose(int semitones);
    int transpose() const { return transpose_; }
    void setMuted(int ch, bool muted);  // Muted channels play no new notes (sing the melody yourself)
    bool muted(int ch) const { return ch >= 0 && ch < 16 && (muteMask_ >> ch) & 1; }
    int activeVoices() const;

private:
    void renderFx(int n, bool rev, bool cho);
    void recordNote(int ch, int key, int vel);
    void initFx();
    void freeScan();
    void applyVoices();

    ::tsf *f_ = nullptr;
    ::tsf_hydra *hydra_ = nullptr;
    char path_[160] = "";
    uint32_t smplPos_ = 0;
    size_t size_ = 0;
    bool full_ = false, prepared_ = false, scanning_ = false, resample_ = true;
    int maxVoices_ = MAX_VOICES;
    PrepStats stats_;
    uint64_t *keyMask_ = nullptr, *velMask_ = nullptr;   // Scan: 2 x 64 bit per preset
    Effects fx_;
    Reverb reverb_;
    Chorus chorus_;
    uint8_t revSend_[16], choSend_[16];   // CC91 / CC93 per channel
    int rate_ = 22050;
    float dry_[256], rev_[256], cho_[256], tmp_[256];
    int transpose_ = 0;
    uint16_t muteMask_ = 0;
    uint8_t playedAs_[16][128];   // Key actually started for a MIDI note (0xFF = none)
};

}  // namespace Sf
