#include <cstdio>

#include "battery_math.h"

static int failed = 0;
static int passed = 0;

#define CHECK(condition)                                                                                              \
    do {                                                                                                               \
        if (condition) ++passed;                                                                                       \
        else {                                                                                                         \
            ++failed;                                                                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                          \
        }                                                                                                              \
    } while (0)

int main()
{
    CHECK(BatteryMath::validScale(0));
    CHECK(BatteryMath::validScale(BatteryMath::SCALE_MIN_PERMILLE));
    CHECK(BatteryMath::validScale(BatteryMath::SCALE_MAX_PERMILLE));
    CHECK(!BatteryMath::validScale(1499));
    CHECK(!BatteryMath::validScale(12001));
    CHECK(BatteryMath::sanitizeScale(0) == 0);
    CHECK(BatteryMath::sanitizeScale(2000) == 2000);
    CHECK(BatteryMath::sanitizeScale(1499) == 0);

    uint16_t scale = 0;
    CHECK(BatteryMath::scaleFromReference(3800, 950, scale) && scale == 4000);
    CHECK(!BatteryMath::scaleFromReference(2400, 950, scale));
    CHECK(!BatteryMath::scaleFromReference(4600, 950, scale));
    CHECK(!BatteryMath::scaleFromReference(3800, 199, scale));
    CHECK(!BatteryMath::scaleFromReference(3800, 3101, scale));
    CHECK(!BatteryMath::scaleFromReference(3800, 3000, scale));

    uint16_t batteryMv = 0;
    CHECK(BatteryMath::batteryMillivolts(950, 4000, batteryMv) && batteryMv == 3800);
    CHECK(!BatteryMath::batteryMillivolts(950, 0, batteryMv));
    CHECK(!BatteryMath::batteryMillivolts(950, 1499, batteryMv));
    CHECK(!BatteryMath::batteryMillivolts(200, 4000, batteryMv));
    CHECK(!BatteryMath::batteryMillivolts(1200, 4000, batteryMv));
    CHECK(!BatteryMath::batteryMillivolts(600, 4000, batteryMv));

    uint8_t percent = 0;
    CHECK(BatteryMath::estimatePercent(3000, 4200, percent) && percent == 0);
    CHECK(BatteryMath::estimatePercent(3300, 4200, percent) && percent == 5);
    CHECK(BatteryMath::estimatePercent(3500, 4200, percent) && percent == 10);
    CHECK(BatteryMath::estimatePercent(3700, 4200, percent) && percent == 50);
    CHECK(BatteryMath::estimatePercent(3760, 4200, percent) && percent == 80);
    CHECK(BatteryMath::estimatePercent(3800, 4200, percent) && percent == 85);
    CHECK(BatteryMath::estimatePercent(4000, 4200, percent) && percent == 97);
    CHECK(BatteryMath::estimatePercent(4100, 4200, percent) && percent == 99);
    CHECK(BatteryMath::estimatePercent(4200, 4200, percent) && percent == 100);
    CHECK(BatteryMath::estimatePercent(4500, 4200, percent) && percent == 100);
    CHECK(BatteryMath::estimatePercent(4100, 4100, percent) && percent == 100);
    CHECK(BatteryMath::estimatePercent(3000, 4100, percent) && percent == 0);
    CHECK(!BatteryMath::estimatePercent(2499, 4200, percent));
    CHECK(!BatteryMath::estimatePercent(4000, 3899, percent));
    CHECK(BatteryMath::validFullVoltage(4200));
    CHECK(!BatteryMath::validFullVoltage(3899));
    CHECK(BatteryMath::sanitizeFullVoltage(4301) == 4200);

    uint32_t lastBeep = 0;
    CHECK(!BatteryMath::lowBatteryAlertDue(true, 4, true, 1000, lastBeep));
    CHECK(lastBeep == 1000);
    CHECK(!BatteryMath::lowBatteryAlertDue(true, 4, true, 30999, lastBeep));
    CHECK(BatteryMath::lowBatteryAlertDue(true, 4, true, 31000, lastBeep));
    CHECK(!BatteryMath::lowBatteryAlertDue(true, 4, true, 60999, lastBeep));
    CHECK(BatteryMath::lowBatteryAlertDue(true, 4, true, 61000, lastBeep));
    CHECK(!BatteryMath::lowBatteryAlertDue(true, 5, true, 62000, lastBeep) && lastBeep == 0);
    CHECK(!BatteryMath::lowBatteryAlertDue(true, 4, false, 63000, lastBeep));

    CHECK(BatteryMath::supportsS3BatteryAdc(true, true));
    CHECK(!BatteryMath::supportsS3BatteryAdc(false, true));
    CHECK(!BatteryMath::supportsS3BatteryAdc(true, false));
    CHECK(BatteryMath::filterRaw(-1, 1000) == 1000);
    CHECK(BatteryMath::filterRaw(1000, 1200) == 1050);
    CHECK(BatteryMath::filterRaw(1050, 4000) == 1788);
    CHECK(!BatteryMath::batteryLogDue(false, BatteryMath::LOG_INTERVAL_MS, 0));
    CHECK(!BatteryMath::batteryLogDue(true, BatteryMath::LOG_INTERVAL_MS - 1, 0));
    CHECK(BatteryMath::batteryLogDue(true, BatteryMath::LOG_INTERVAL_MS, 0));
    CHECK(BatteryMath::batteryLogDue(true, 200000, UINT32_MAX - 100000));

    std::printf("Battery math: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
