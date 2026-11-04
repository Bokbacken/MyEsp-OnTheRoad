#include <cstdio>

#include "board_power_math.h"

int main()
{
    uint16_t millivolts = 0;
    if (!BoardPowerMath::batteryMillivolts(3727, millivolts) || millivolts != 4100) return 1;
    if (BoardPowerMath::batteryMillivolts(0, millivolts) ||
        BoardPowerMath::batteryMillivolts(8192, millivolts) ||
        BoardPowerMath::batteryMillivolts(2272, millivolts))
        return 1;

    uint8_t percent = 0;
    if (!BoardPowerMath::percentage(0, percent) || percent != 0 ||
        !BoardPowerMath::percentage(100, percent) || percent != 100 ||
        BoardPowerMath::percentage(255, percent))
        return 1;
    std::puts("AXP2101 voltage/gauge checks passed");
    return 0;
}
