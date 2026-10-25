// otr_fx.h - Light mono send effects for the karaoke synth: a Freeverb-style reverb (8 combs +
// 4 allpasses, delays scaled to the sample rate) and a single-tap chorus (LFO-modulated delay
// line). Float math (the ESP32-S3 has a single precision FPU). Pure C++, host tested.
#pragma once

#include <stdint.h>

namespace Sf {

class Reverb {
public:
    ~Reverb();
    bool init(int sampleRate);      // Allocates the delay lines (~24 kB at 22050 Hz)
    void clear();                   // Silence the tail
    // Adds the wet signal of `in` (send bus) to `out`; level 0..1.
    void process(const float *in, float *out, int n, float level);
    bool ready() const { return mem_ != nullptr; }

private:
    static constexpr int COMBS = 8, ALLPASSES = 4;
    float *mem_ = nullptr;
    float *comb_[COMBS] = {}, *ap_[ALLPASSES] = {};
    int combLen_[COMBS] = {}, apLen_[ALLPASSES] = {};
    int combPos_[COMBS] = {}, apPos_[ALLPASSES] = {};
    float combStore_[COMBS] = {};
    int total_ = 0;
};

class Chorus {
public:
    ~Chorus();
    bool init(int sampleRate);      // ~4 kB delay line
    void clear();
    void process(const float *in, float *out, int n, float level);
    bool ready() const { return buf_ != nullptr; }

private:
    float *buf_ = nullptr;
    int len_ = 0, pos_ = 0;
    float phase_ = 0, phaseInc_ = 0, base_ = 0, depth_ = 0;
};

}  // namespace Sf
