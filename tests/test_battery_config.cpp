#include <cstdio>

#include "app_config.h"
#include "my_storage.h"

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
    AppConfig defaults{};
    Config::defaults(defaults);
    CHECK(defaults.batteryScalePermille == 0);
    CHECK(defaults.batteryFullMv == 4200);
    CHECK(defaults.karaVolume == 70);

    TestNvs::numbers.clear();
    Config::begin();
    CHECK(Config::get().batteryScalePermille == 0);
    CHECK(Config::get().batteryFullMv == 4200);
    CHECK(Config::get().karaVolume == 70);

    // Older schema-1 configurations have no new keys and keep the safe defaults.
    TestNvs::numbers["schema"] = 1;
    TestNvs::numbers["kara_vol"] = 63;
    Config::begin();
    CHECK(Config::get().batteryScalePermille == 0);
    CHECK(Config::get().batteryFullMv == 4200);
    CHECK(Config::get().karaVolume == 63);

    TestNvs::numbers["bat_scale"] = 4000;
    Config::begin();
    CHECK(Config::get().batteryScalePermille == 4000);
    TestNvs::numbers["bat_scale"] = 12001;
    TestNvs::numbers["bat_full"] = 3899;
    TestNvs::numbers["kara_vol"] = 255;
    Config::begin();
    CHECK(Config::get().batteryScalePermille == 0);
    CHECK(Config::get().batteryFullMv == 4200);
    CHECK(Config::get().karaVolume == 70);

    Config::get().batteryScalePermille = 5000;
    Config::get().batteryFullMv = 4150;
    Config::get().karaVolume = 42;
    CHECK(Config::save());
    CHECK(TestNvs::numbers["bat_scale"] == 5000);
    CHECK(TestNvs::numbers["bat_full"] == 4150);
    CHECK(TestNvs::numbers["kara_vol"] == 42);
    Config::begin();
    CHECK(Config::get().batteryScalePermille == 5000);
    CHECK(Config::get().batteryFullMv == 4150);
    CHECK(Config::get().karaVolume == 42);

    std::printf("Battery config: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
