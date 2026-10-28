#pragma once

#include <stdint.h>

namespace BatteryMath {

constexpr uint16_t SCALE_MIN_PERMILLE = 1500;
constexpr uint16_t SCALE_MAX_PERMILLE = 12000;
constexpr int32_t BATTERY_MIN_MV = 2500;
constexpr int32_t BATTERY_MAX_MV = 4500;
constexpr int32_t ADC_MIN_MV = 200;
constexpr int32_t ADC_MAX_MV = 3100;

constexpr bool validScale(uint16_t scale)
{
    return scale == 0 || (scale >= SCALE_MIN_PERMILLE && scale <= SCALE_MAX_PERMILLE);
}

constexpr uint16_t sanitizeScale(uint16_t scale)
{
    return scale == 0 || (scale >= SCALE_MIN_PERMILLE && scale <= SCALE_MAX_PERMILLE) ? scale : 0;
}

constexpr bool scaleFromReference(int32_t batteryMv, int32_t adcMv, uint16_t &scale)
{
    if (batteryMv < BATTERY_MIN_MV || batteryMv > BATTERY_MAX_MV ||
        adcMv < ADC_MIN_MV || adcMv > ADC_MAX_MV)
        return false;
    const int32_t calculated = (batteryMv * 1000 + adcMv / 2) / adcMv;
    if (calculated < SCALE_MIN_PERMILLE || calculated > SCALE_MAX_PERMILLE) return false;
    scale = static_cast<uint16_t>(calculated);
    return true;
}

constexpr bool batteryMillivolts(int32_t adcMv, uint16_t scale, uint16_t &batteryMv)
{
    if (!validScale(scale) || scale == 0 || adcMv < ADC_MIN_MV || adcMv > ADC_MAX_MV) return false;
    const int32_t calculated = (adcMv * scale + 500) / 1000;
    if (calculated < BATTERY_MIN_MV || calculated > BATTERY_MAX_MV) return false;
    batteryMv = static_cast<uint16_t>(calculated);
    return true;
}

constexpr bool supportsS3BatteryAdc(bool s3Target, bool s3Profile)
{
    return s3Target && s3Profile;
}

constexpr int32_t filterRaw(int32_t previous, int32_t current)
{
    return previous < 0 ? current : (previous * 3 + current + 2) / 4;
}

}  // namespace BatteryMath
