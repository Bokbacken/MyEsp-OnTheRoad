#pragma once

#include <stdint.h>

namespace BoardPowerMath {

constexpr bool batteryMillivolts(uint16_t raw, uint16_t &millivolts)
{
    if (raw == 0 || raw > 0x1FFF) return false;
    if (raw < 2500 || raw > 4500) return false;
    millivolts = raw;
    return true;
}

constexpr bool percentage(uint8_t raw, uint8_t &percent)
{
    if (raw > 100) return false;
    percent = raw;
    return true;
}

}  // namespace BoardPowerMath
