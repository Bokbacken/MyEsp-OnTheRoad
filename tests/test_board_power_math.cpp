#include <cstdio>
#include <initializer_list>

#include "board_power_math.h"

int main()
{
    uint16_t millivolts = 0;
    for (uint16_t raw : {uint16_t(2500), uint16_t(3727), uint16_t(4076), uint16_t(4100), uint16_t(4500)}) {
        if (!BoardPowerMath::batteryMillivolts(raw, millivolts) || millivolts != raw) return 1;
    }
    if (BoardPowerMath::batteryMillivolts(0, millivolts) ||
        BoardPowerMath::batteryMillivolts(8192, millivolts) ||
        BoardPowerMath::batteryMillivolts(2499, millivolts) ||
        BoardPowerMath::batteryMillivolts(4501, millivolts))
        return 1;

    uint8_t percent = 0;
    if (!BoardPowerMath::percentage(0, percent) || percent != 0 ||
        !BoardPowerMath::percentage(100, percent) || percent != 100 ||
        BoardPowerMath::percentage(255, percent))
        return 1;
    std::puts("AXP2101 voltage/gauge checks passed");
    return 0;
}
