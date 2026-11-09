// kara_overload.h - When karaoke synthesis is too heavy for the hardware (large SoundFont and/or many
// voices), playback is stopped and the user is told, instead of stuttering and starving the
// watchdog. Evaluated once per 5 s diagnostics period.
#pragma once
#include <stdint.h>

namespace KaraOverload {
constexpr uint32_t STOP_AVG_LOAD100 = 10000;   // Average render time >= 100 % of a mixer block
constexpr uint32_t WARN_NEW_UNDERRUNS = 5;      // Late mixer blocks within one period: a strike
constexpr uint32_t STOP_NEW_UNDERRUNS = 15;     // ...or so many that one period is enough
constexpr uint8_t STOP_STRIKES = 2;             // Consecutive strike periods before stopping

// A "late block" means the mixer fell behind while the 87 ms I2S buffer still had audio, so a single
// period with a few of them is usually inaudible (owner S3 log v0.26.1: +7, no audible dropout).
// Sustained lateness or a full-period average over 100 % is what starved the watchdog (v0.23.18).
class Guard {
public:
    bool shouldStop(uint32_t avgLoad100, uint32_t newUnderruns)
    {
        if (avgLoad100 >= STOP_AVG_LOAD100 || newUnderruns >= STOP_NEW_UNDERRUNS) return reset(), true;
        if (newUnderruns < WARN_NEW_UNDERRUNS) return reset(), false;
        if (++strikes_ < STOP_STRIKES) return false;
        reset();
        return true;
    }
    void reset() { strikes_ = 0; }

private:
    uint8_t strikes_ = 0;
};
}  // namespace KaraOverload
