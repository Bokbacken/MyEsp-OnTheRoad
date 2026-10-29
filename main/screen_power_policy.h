#pragma once

#include <stdint.h>

namespace ScreenPowerPolicy {

constexpr bool shouldSleep(uint16_t timeoutSeconds, uint32_t idleMs, bool songPlaying, bool gameRunning)
{
    if (!timeoutSeconds || songPlaying) return false;
    const uint32_t multiplier = gameRunning ? 2 : 1;
    return idleMs >= uint32_t(timeoutSeconds) * multiplier * 1000;
}

}  // namespace ScreenPowerPolicy
