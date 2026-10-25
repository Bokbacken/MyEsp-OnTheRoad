// otr_synth.h - SoundFont (.sf2) synthesizer for the karaoke player, built on a patched TinySoundFont
// (MIT, components/otr_synth/tsf.h: 16-bit samples, float filter, fixed-point mono loop, voice
// stealing). Implements Midi::Sink so the Midi::Player can drive it directly.
// Not thread safe: the owner serializes calls (the karaoke engine holds a mutex).
#pragma once

#include <stdint.h>
#include <stdio.h>

#include "otr_midi.h"

struct tsf;

namespace Sf {

constexpr int MAX_VOICES = 32;
constexpr size_t MAX_SF2 = 7u * 1024 * 1024;   // Refuse larger files (8 MB PSRAM, 16-bit samples)

enum class LoadErr : uint8_t { OK, NO_FILE, TOO_BIG, BAD_FILE, NO_MEMORY };
const char *loadErrName(LoadErr e);

class Synth : public Midi::Sink {
public:
    Synth();
    ~Synth() override;
    LoadErr load(const char *path, int sampleRate);
    void unload();
    bool loaded() const { return f_ != nullptr; }
    size_t fileSize() const { return size_; }

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
    ::tsf *f_ = nullptr;
    size_t size_ = 0;
    int transpose_ = 0;
    uint16_t muteMask_ = 0;
    uint8_t playedAs_[16][128];   // Key actually started for a MIDI note (0xFF = none)
    float buf_[256];
};

}  // namespace Sf
