#pragma once

#include <stdint.h>

namespace BoardPowerMath {

constexpr bool batteryMillivolts(uint16_t raw, uint16_t &millivolts)
{
    if (raw == 0 || raw > 0x1FFF) return false;
    const uint32_t converted = (static_cast<uint32_t>(raw) * 11 + 5) / 10;
    if (converted < 2500 || converted > 4500) return false;
    millivolts = static_cast<uint16_t>(converted);
    return true;
}

constexpr bool percentage(uint8_t raw, uint8_t &percent)
{
    if (raw > 100) return false;
    percent = raw;
    return true;
}

}  // namespace BoardPowerMath
