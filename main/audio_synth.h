// audio_synth.h - pure audio building blocks (no ESP-IDF, host-tested by tests/test_audio.cpp):
//
//  - Synth::Voice: one chiptune voice driven by an MML string (Music Macro Language subset):
//      c d e f g a b   note, optional + / # (sharp) or - (flat), optional length (1..64) and dots
//      r               rest (same length syntax)
//      o<n> < >        octave 1..7 (default 4), down / up
//      l<n>            default length (default 8 = eighth note)
//      t<n>            tempo in quarter notes per minute (default 120)
//      v<n>            volume 0..15 (default 12)
//      q<n>            gate 1..8 (eighths of the note that sound before the release, default 7)
//      @<n>            waveform: 0 square 50 %, 1 square 25 %, 2 square 12.5 %, 3 triangle,
//                      4 saw, 5 noise, 6 sine
//      x<a>,<d>,<s>,<r> envelope: attack ms, decay ms, sustain level 0..15, release ms
//      s<n>            pitch slide of the following notes: n semitones (-48..48) over the note
//      spaces and '|' are ignored
//  - Synth::Song: up to 4 voices played together, optionally looping.
//  - Wav: RIFF/WAVE header parser (PCM 8/16 bit, mono/stereo, 8..48 kHz) and conversion to mono
//    16 bit, plus a streaming linear resampler to the mixer rate.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Synth {

int32_t mmlLengthMs(const char *mml);

constexpr int RATE = 22050;          // Mixer sample rate (Hz), mono 16 bit
constexpr int VOICE_AMP = 7000;      // Peak amplitude of one voice at v15 (headroom for ~4 voices)
constexpr int MAX_SONG_VOICES = 4;

enum class Wave : uint8_t { SQUARE50 = 0, SQUARE25, SQUARE12, TRIANGLE, SAW, NOISE, SINE, COUNT };

// Frequency of MIDI note n in Hz (69 = A4 = 440 Hz).
float midiHz(int note);

class Voice {
    friend int32_t mmlLengthMs(const char *mml);
public:
    void start(const char *mml);   // nullptr / "" = silent, done() immediately
    void stop() { done_ = true; }
    bool done() const { return done_; }
    // Adds n samples (scaled by gainQ8, 256 = unity) to acc. Returns false once finished.
    bool render(int32_t *acc, int n, int gainQ8);
    // Parser error seen (unknown command / bad number) - for tests; the voice skips the byte.
    bool error() const { return error_; }

private:
    bool nextNote();               // Parses up to the next note / rest. false = end of text
    void beginNote(int midi, uint32_t samples);
    int32_t envelope();            // Current envelope level Q15 (0..32767), advances one sample

    const char *src_ = nullptr, *pos_ = nullptr;
    bool done_ = true, error_ = false;
    // Parser state
    int octave_ = 4, defLen_ = 8, tempo_ = 120, vol_ = 12, gate_ = 7, slide_ = 0;
    Wave wave_ = Wave::SQUARE50;
    uint16_t atkMs_ = 2, decMs_ = 60, susLvl_ = 10, relMs_ = 40;
    // Note state
    bool rest_ = true;
    uint32_t noteLen_ = 0, gateLen_ = 0, t_ = 0;   // samples
    uint32_t phase_ = 0, inc_ = 0;
    int32_t incStep_ = 0;                           // Slide: inc change per sample
    uint32_t lfsr_ = 0x7FFF;
    int16_t noiseOut_ = 0;
    uint32_t noisePhase_ = 0;
    int32_t env_ = 0, relFrom_ = 0;                 // Q15
    uint32_t atk_ = 1, dec_ = 1, rel_ = 1;          // samples
};

struct Song {
    const char *parts[MAX_SONG_VOICES];
    bool loop;
};

class SongPlayer {
public:
    void start(const Song &s);
    void stop();
    bool playing() const { return playing_; }
    bool render(int32_t *acc, int n, int gainQ8);   // false once a non-looping song ended

private:
    Song song_{};
    Voice v_[MAX_SONG_VOICES];
    bool playing_ = false;
};

// Total length of one MML part in milliseconds (parses the text; -1 on a parse error).
int32_t mmlLengthMs(const char *mml);

}  // namespace Synth

namespace Wav {

struct Info {
    uint16_t channels;      // 1 or 2
    uint16_t bits;          // 8 or 16
    uint32_t rate;          // 8000..48000
    uint32_t dataOffset;    // Byte offset of the sample data in the file
    uint32_t dataBytes;     // Length of the data chunk (clipped to whole frames)
    uint32_t frames() const { return dataBytes / (channels * (bits / 8)); }
};

// Parses the header in buf (the first bytes of the file; 512..4096 bytes recommended).
// fileSize (0 = unknown) clips dataBytes. False for anything but PCM 8/16 bit, 1-2 channels,
// 8..48 kHz, or if the "data" chunk header is not inside buf.
bool parse(const uint8_t *buf, size_t len, uint32_t fileSize, Info &out);

// Converts `frames` frames of raw data to mono int16 (stereo is averaged, 8 bit is unsigned).
void toMono16(const uint8_t *in, size_t frames, const Info &fmt, int16_t *out);

// Streaming linear resampler (mono int16) from srcRate to Synth::RATE.
class Resampler {
public:
    void reset(uint32_t srcRate);
    // Consumes input, produces output. Returns produced samples; `used` = consumed samples.
    size_t process(const int16_t *in, size_t nIn, int16_t *out, size_t outCap, size_t &used);

private:
    uint32_t step_ = 65536;   // Q16 source samples per output sample
    uint32_t frac_ = 0;       // Q16 position between prev_ and the next input sample
    int16_t prev_ = 0;
    bool primed_ = false;
};

}  // namespace Wav
