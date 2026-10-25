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
// v0.13.3 halving: samples recorded at >= 1.8 x the output rate (44.1/48 kHz for 22.05 kHz out)
// are low-pass filtered (47-tap half-band FIR, -6 dB at a quarter of the source rate) and decimated
// 2:1 while they are read, and their zones get half the sample rate. They take half the PSRAM and
// play without the aliasing that TinySoundFont's linear interpolation produces when it skips every
// other source sample. Zones with a short, odd loop (< HALVE_MIN_ODD_LOOP samples) stay at full
// rate, because a halved loop length would detune the sustained note.
#pragma once

#include <stdint.h>
#include <stdio.h>

#include "otr_fx.h"
#include "otr_midi.h"

struct tsf;
struct tsf_hydra;

namespace Sf {

constexpr int MAX_VOICES = 32;
constexpr size_t MAX_SF2 = 7u * 1024 * 1024;        // load() (whole file in PSRAM) refuses larger files
constexpr size_t SAMPLE_BUDGET = 6u * 1024 * 1024;  // prepare(): max sample data per song
constexpr int KEY_MARGIN = 12;                      // Melodic keys are widened by +-12 (transpose never reloads)
constexpr int REDUCED_VELOCITY = 100;               // Velocity layer kept when a song is over budget
constexpr uint32_t HALVE_MIN_ODD_LOOP = 1500;       // Odd loops shorter than this are not halved (< 1.2 cent error)

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
    uint16_t halved = 0;       // Zones whose samples were decimated 2:1
    uint32_t halvedSaved = 0;  // Bytes saved by halving
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
    // Loads the samples `song` needs (bank opened with open()). budget 0 = SAMPLE_BUDGET; the free
    // PSRAM also limits it. Keeps the bank open on failure.
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
    // Halve high-rate samples in prepare() (default on; takes effect at the next prepare)
    void setHalving(bool on) { halve_ = on; }
    bool halving() const { return halve_; }

    void midi(uint8_t status, uint8_t d1, uint8_t d2) override;
    void reset() override;
    void allNotesOff();   // Release every note (pause)

    // Mixes n mono samples into acc (16-bit scale). gainQ8: 256 = unity.
    void render(int32_t *acc, int n, int gainQ8);

    void setTranspose(int semitones);   // -12..12, not applied to the drum channel
    int transpose() const { return transpose_; }
    void setMuted(int ch, bool muted);  // Muted channels play no new notes (sing the melody yourself)
    bool muted(int ch) const { return ch >= 0 && ch < 16 && (muteMask_ >> ch) & 1; }
    int activeVoices() const;

private:
    void renderFx(int n, bool rev, bool cho);
    void recordNote(int ch, int key, int vel);
    void initFx();
    void freeScan();

    ::tsf *f_ = nullptr;
    ::tsf_hydra *hydra_ = nullptr;
    char path_[160] = "";
    uint32_t smplPos_ = 0;
    size_t size_ = 0;
    bool full_ = false, prepared_ = false, scanning_ = false, halve_ = true;
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
