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

    CHECK(BatteryMath::supportsS3BatteryAdc(true, true));
    CHECK(!BatteryMath::supportsS3BatteryAdc(false, true));
    CHECK(!BatteryMath::supportsS3BatteryAdc(true, false));
    CHECK(BatteryMath::filterRaw(-1, 1000) == 1000);
    CHECK(BatteryMath::filterRaw(1000, 1200) == 1050);
    CHECK(BatteryMath::filterRaw(1050, 4000) == 1788);

    std::printf("Battery math: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
