// kara_overload.h - When karaoke synthesis is too heavy for the hardware (large SoundFont and/or many
// voices), playback is stopped and the user is told, instead of stuttering and starving the
// watchdog. Evaluated once per 5 s diagnostics period.
#pragma once
#include <stdint.h>

namespace KaraOverload {
constexpr uint32_t STOP_AVG_LOAD100 = 10000;   // Average render time >= 100 % of a mixer block

// Same-tick writes and DMA refill-queue overflows are diagnostics, not confirmed audio underruns.
inline bool shouldStop(uint32_t avgLoad100)
{
    return avgLoad100 >= STOP_AVG_LOAD100;
}
}  // namespace KaraOverload
