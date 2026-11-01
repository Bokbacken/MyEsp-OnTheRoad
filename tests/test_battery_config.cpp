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
#if defined(CONFIG_OTR_HW_PROFILE_P4_43)
    constexpr uint16_t defaultScale = 3000;
#else
    constexpr uint16_t defaultScale = 0;
#endif
    AppConfig defaults{};
    Config::defaults(defaults);
    CHECK(defaults.batteryScalePermille == defaultScale);
    CHECK(defaults.batteryFullMv == 4200);
    CHECK(defaults.karaVolume == 70);
    CHECK(defaults.karaVoices == 96);
    for (uint8_t voices : KARA_VOICE_OPTIONS) CHECK(Config::validKaraVoices(voices));
    CHECK(!Config::validKaraVoices(16) && !Config::validKaraVoices(193));
    CHECK(defaults.screenOffSeconds == 0);
    CHECK(!defaults.ecoSleep && !defaults.powerLog);

    TestNvs::numbers.clear();
    Config::begin();
    CHECK(Config::get().batteryScalePermille == defaultScale);
    CHECK(Config::get().batteryFullMv == 4200);
    CHECK(Config::get().karaVolume == 70);

    // Migrate the former implicit 32-voice default, then preserve an explicit 32 choice.
    TestNvs::numbers["schema"] = 1;
    TestNvs::numbers["kara_voices"] = 32;
    Config::begin();
    CHECK(Config::get().karaVoices == 96);
    CHECK(TestNvs::numbers["kara_voices"] == 96 && TestNvs::numbers["kara_vver"] == 1);
    Config::get().karaVoices = 32;
    CHECK(Config::save());
    TestNvs::numbers["kara_voices"] = 32;
    Config::begin();
    CHECK(Config::get().karaVoices == 32);
    for (uint8_t voices : KARA_VOICE_OPTIONS) {
        TestNvs::numbers["kara_voices"] = voices;
        Config::begin();
        CHECK(Config::get().karaVoices == voices);
    }
    TestNvs::numbers["kara_voices"] = 100;
    Config::begin();
    CHECK(Config::get().karaVoices == 96);

    // Older schema-1 configurations have no new keys and keep the safe defaults.
    TestNvs::numbers["schema"] = 1;
    TestNvs::numbers["kara_vol"] = 63;
    Config::begin();
    CHECK(Config::get().batteryScalePermille == defaultScale);
    CHECK(Config::get().batteryFullMv == 4200);
    CHECK(Config::get().karaVolume == 63);
    CHECK(Config::get().screenOffSeconds == 0);
    CHECK(!Config::get().ecoSleep && !Config::get().powerLog);
    Config::get().ecoSleep = true;
    Config::get().powerLog = true;
    CHECK(Config::save());
    Config::begin();
    CHECK(Config::get().ecoSleep && Config::get().powerLog);
    for (uint16_t seconds : SCREEN_OFF_SECONDS) {
        CHECK(Config::validScreenOffSeconds(seconds));
        Config::get().screenOffSeconds = seconds;
        CHECK(Config::save());
        Config::begin();
        CHECK(Config::get().screenOffSeconds == seconds);
    }
    CHECK(!Config::validScreenOffSeconds(1));
    CHECK(!Config::validScreenOffSeconds(65535));
    TestNvs::numbers["screen_off"] = 65535;
    Config::begin();
    CHECK(Config::get().screenOffSeconds == 0);

    CHECK(defaults.textSize == TEXT_SIZE_DEFAULT);
    for (uint8_t size = 0; size < TEXT_SIZE_COUNT; ++size) {
        Config::get().textSize = size;
        CHECK(Config::save());
        Config::begin();
        CHECK(Config::get().textSize == size);
    }
    TestNvs::numbers["text_size"] = 9;
    Config::begin();
    CHECK(Config::get().textSize == TEXT_SIZE_DEFAULT);
    Config::get().textSize = 200;
    CHECK(Config::save());
    CHECK(Config::get().textSize == TEXT_SIZE_DEFAULT);

    TestNvs::numbers["bat_scale"] = 0;
    Config::begin();
    CHECK(Config::get().batteryScalePermille == defaultScale);

    TestNvs::numbers["bat_scale"] = 4000;
    Config::begin();
    CHECK(Config::get().batteryScalePermille == 4000);
    TestNvs::numbers["bat_scale"] = 12001;
    TestNvs::numbers["bat_full"] = 3899;
    TestNvs::numbers["kara_vol"] = 255;
    Config::begin();
    CHECK(Config::get().batteryScalePermille == defaultScale);
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
    Config::get().batteryScalePermille = 0;
    CHECK(Config::save());
    CHECK(Config::get().batteryScalePermille == defaultScale);

    std::printf("Battery config: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
