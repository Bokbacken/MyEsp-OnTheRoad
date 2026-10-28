#pragma once

#include <stddef.h>
#include <stdint.h>

namespace BatteryMath {

constexpr uint16_t SCALE_MIN_PERMILLE = 1500;
constexpr uint16_t SCALE_MAX_PERMILLE = 12000;
constexpr int32_t BATTERY_MIN_MV = 2500;
constexpr int32_t BATTERY_MAX_MV = 4500;
constexpr uint16_t BATTERY_FULL_MIN_MV = 3900;
constexpr uint16_t BATTERY_FULL_MAX_MV = 4300;
constexpr uint16_t BATTERY_FULL_DEFAULT_MV = 4200;
constexpr int32_t ADC_MIN_MV = 200;
constexpr int32_t ADC_MAX_MV = 3100;
constexpr uint32_t LOW_BATTERY_ALERT_INTERVAL_MS = 30000;

struct ChargePoint {
    uint16_t millivolts;
    uint8_t percent;
};

constexpr ChargePoint LIPO_REST_CURVE[] = {
    {3000, 0}, {3300, 5}, {3600, 10}, {3700, 20}, {3740, 30}, {3790, 40},
    {3820, 50}, {3870, 60}, {3920, 70}, {4000, 85}, {4100, 95}, {4200, 100},
};

constexpr bool validScale(uint16_t scale)
{
    return scale == 0 || (scale >= SCALE_MIN_PERMILLE && scale <= SCALE_MAX_PERMILLE);
}

constexpr uint16_t sanitizeScale(uint16_t scale)
{
    return scale == 0 || (scale >= SCALE_MIN_PERMILLE && scale <= SCALE_MAX_PERMILLE) ? scale : 0;
}

constexpr bool validFullVoltage(uint16_t batteryMv)
{
    return batteryMv >= BATTERY_FULL_MIN_MV && batteryMv <= BATTERY_FULL_MAX_MV;
}

constexpr uint16_t sanitizeFullVoltage(uint16_t batteryMv)
{
    return validFullVoltage(batteryMv) ? batteryMv : BATTERY_FULL_DEFAULT_MV;
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

constexpr bool estimatePercent(uint16_t batteryMv, uint16_t fullMv, uint8_t &percent)
{
    if (batteryMv < BATTERY_MIN_MV || batteryMv > BATTERY_MAX_MV || !validFullVoltage(fullMv)) return false;
    if (batteryMv <= LIPO_REST_CURVE[0].millivolts) {
        percent = 0;
        return true;
    }
    if (batteryMv >= fullMv) {
        percent = 100;
        return true;
    }
    constexpr size_t pointCount = sizeof(LIPO_REST_CURVE) / sizeof(LIPO_REST_CURVE[0]);
    const uint16_t curveMv = static_cast<uint16_t>(
        LIPO_REST_CURVE[0].millivolts +
        (static_cast<uint32_t>(batteryMv - LIPO_REST_CURVE[0].millivolts) *
         (LIPO_REST_CURVE[pointCount - 1].millivolts - LIPO_REST_CURVE[0].millivolts) +
         (fullMv - LIPO_REST_CURVE[0].millivolts) / 2) /
            (fullMv - LIPO_REST_CURVE[0].millivolts));
    for (size_t i = 1; i < pointCount; ++i) {
        const ChargePoint &right = LIPO_REST_CURVE[i];
        if (curveMv <= right.millivolts) {
            const ChargePoint &left = LIPO_REST_CURVE[i - 1];
            const uint32_t spanMv = right.millivolts - left.millivolts;
            const uint32_t spanPercent = right.percent - left.percent;
            percent = static_cast<uint8_t>(left.percent +
                (static_cast<uint32_t>(curveMv - left.millivolts) * spanPercent + spanMv / 2) / spanMv);
            return true;
        }
    }
    percent = 100;
    return true;
}

constexpr bool lowBatteryAlertDue(bool percentValid, uint8_t percent, bool soundOn, uint32_t now,
                                  uint32_t &lastBeep)
{
    if (!percentValid || percent >= 5 || !soundOn) {
        lastBeep = 0;
        return false;
    }
    if (!lastBeep) {
        lastBeep = now ? now : 1;
        return false;
    }
    if (static_cast<uint32_t>(now - lastBeep) < LOW_BATTERY_ALERT_INTERVAL_MS) return false;
    lastBeep = now;
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
