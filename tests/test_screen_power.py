#!/usr/bin/env python3
"""Exercise production screen power and backlight drivers for both board polarities."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

STUB = r'''
#pragma once
#include <cassert>
#include <cstdint>
constexpr int ESP_OK=0, ESP_FAIL=-1;
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
namespace Display {
#ifdef CONFIG_OTR_HW_PROFILE_P4_43
inline bool supported=true;
#else
inline bool supported=false;
#endif
inline bool asleep=false, failSleep=false, failWake=false;
inline int sleepCalls=0, wakeCalls=0;
inline bool panelSleepSupported() { return supported; }
inline bool panelSleeping() { return asleep; }
inline bool setPanelSleeping(bool sleep) {
    if (sleep) { ++sleepCalls; asleep=true; return !failSleep; }
    ++wakeCalls;
    if (failWake) return false;
    asleep=false; return true;
}
}
namespace MySys { inline uint32_t now=0; inline uint32_t millis() { return now; } }
namespace MyOta { inline bool updating=false; inline bool busy() { return updating; } }
namespace Karaoke { inline bool songPlaying=false; inline bool playing() { return songPlaying; } }
namespace GameLive { inline bool gameRunning=false; inline bool running() { return gameRunning; } }
namespace AdvLive { inline bool adventure=false; inline bool active() { return adventure; } }
namespace AppWebFiles { inline bool transferring=false; inline bool busy() { return transferring; } }
namespace Audio { inline bool idle=false, musicPaused=false; inline void setSleeping(bool enabled) { idle=enabled; }
inline void setMusicPaused(bool paused) { musicPaused=paused; } }
namespace MyWifi {
inline bool powerSave=false, idle=false;
inline void setScreenPowerSave(bool enabled) { powerSave=enabled; }
inline void setSuspended(bool enabled) { idle=enabled; }
}
struct lv_timer_t {};
inline void (*timerTick)(lv_timer_t *)=nullptr;
inline lv_timer_t *lv_timer_create(void (*fn)(lv_timer_t *), uint32_t period, void *) {
    assert(period==100); assert(!timerTick); timerTick=fn; return nullptr;
}
namespace MyLvgl {
    inline bool (*filter)(bool)=nullptr;
    inline void setPointerFilter(bool (*fn)(bool)) { filter=fn; }
}
using ledc_mode_t=int;
using ledc_channel_t=int;
using gpio_num_t=int;
constexpr int LEDC_LOW_SPEED_MODE=0, LEDC_CHANNEL_0=0, LEDC_TIMER_8_BIT=8;
constexpr int LEDC_TIMER_0=0, LEDC_AUTO_CLK=0, GPIO_MODE_OUTPUT=1;
struct ledc_timer_config_t { int speed_mode, duty_resolution, timer_num, freq_hz, clk_cfg; };
struct ledc_channel_config_t {
    int gpio_num, speed_mode, channel, timer_sel, duty;
    struct { unsigned output_invert; } flags;
};
inline bool pwmFailure=false, inverted=false;
inline uint32_t duty=0;
inline int gpioLevel=-1, configuredPin=-1;
inline int ledc_timer_config(const ledc_timer_config_t *) { return pwmFailure ? ESP_FAIL : ESP_OK; }
inline int ledc_channel_config(const ledc_channel_config_t *c) {
    inverted=c->flags.output_invert; configuredPin=c->gpio_num; return ESP_OK;
}
inline void ledc_set_duty(int, int, uint32_t v) { duty=v; }
inline void ledc_update_duty(int, int) {}
inline void gpio_reset_pin(int) {}
inline void gpio_set_direction(int, int) {}
inline void gpio_set_level(int pin, int level) { configuredPin=pin; gpioLevel=level; }
'''

TEST = r'''
#include "stubs.h"
#include "app_config.h"
#include "board_backlight.h"
#include "board_pins.h"
#include "ui_screen_power.h"
#include "screen_power_policy.h"
namespace Karaoke { extern bool songPlaying; }
namespace GameLive { extern bool gameRunning; }
namespace MyWifi { extern bool powerSave; }
namespace Config { AppConfig config{}; AppConfig &get() { return config; } }
static bool sample(uint32_t now, bool pressed) {
    MySys::now=now; return MyLvgl::filter(pressed);
}
static void tick(uint32_t now) { MySys::now=now; timerTick(nullptr); }
int main() {
    assert(!ScreenPowerPolicy::shouldSleep(30, 30000, true, false));
    assert(!ScreenPowerPolicy::shouldSleep(30, 29999, false, false));
    assert(ScreenPowerPolicy::shouldSleep(30, 30000, false, false));
    assert(!ScreenPowerPolicy::shouldSleep(30, 59999, false, true));
    assert(ScreenPowerPolicy::shouldSleep(30, 60000, false, true));
    assert(!ScreenPowerPolicy::shouldSleep(0, UINT32_MAX, false, true));
    assert(Backlight::begin(80) && Backlight::get()==80);
    assert(configuredPin==PIN_LCD_BL && inverted==LCD_BL_ACTIVE_LOW);
    const uint32_t normalDuty=duty;
    UiScreenPower::begin();
    UiScreenPower::begin(); // One filter and timer only.
    tick(60000); // Disabled by default.
    assert(!Backlight::sleeping());
    Config::get().screenOffSeconds=30;
    assert(sample(60000,true));
    tick(89999);
    assert(!Backlight::sleeping());
    tick(90000);
    assert(Backlight::sleeping() && duty==0 && Backlight::get()==80);
    assert(Display::asleep==Display::supported); // Panel sleeps on every screen-off, not only eco.
    assert(!sample(90010,true)); // Wake is invisible to LVGL.
    assert(!Backlight::sleeping() && duty==normalDuty && !Display::asleep);
    assert(!sample(90259,false)); // 249 ms is insufficient even after release.
    assert(!sample(90260,true)); // New contact in guard stays blocked.
    assert(!sample(90500,true)); // Holding past 250 ms cannot become a press.
    assert(!sample(90501,false));
    assert(sample(90502,true)); // Only a new touch after release reaches controls.
    assert(!sample(90503,false));
    // Held input refreshes inactivity; screen must never blank mid-gesture.
    assert(sample(120000,true));
    tick(120001);
    assert(!Backlight::sleeping());
    assert(!sample(120002,false));
    UiScreenPower::sleepNow();
    Backlight::set(55); // Web/settings brightness cannot accidentally wake the screen.
    assert(Backlight::sleeping() && duty==0 && Backlight::get()==55);
    assert(!Audio::musicPaused);
    MyOta::updating=true;
    tick(120010);
    assert(!Backlight::sleeping() && duty>0 && Audio::musicPaused); // No background music during OTA
    UiScreenPower::sleepNow();
    assert(!Backlight::sleeping());
    tick(999000);
    assert(!Backlight::sleeping()); // OTA never times out the light.
    MyOta::updating=false;
    tick(999100);
    assert(!Audio::musicPaused); // Failed/aborted update resumes the music
    assert(!sample(999300,false)); // Finish forced-wake guard.
    Config::get().screenOffSeconds=0;
    tick(1500000);
    assert(!Backlight::sleeping());
    UiScreenPower::sleepNow(); // Manual still works with automatic off disabled.
    assert(Backlight::sleeping());
    assert(!sample(1500010,true));
    assert(!sample(1500260,false)); // Exactly 250 ms.
    assert(sample(1500261,true));
    assert(!sample(1500262,false));
    // Millisecond wraparound cannot make the cooldown or timeout end early.
    Config::get().screenOffSeconds=30;
    assert(sample(UINT32_MAX-100,true));
    tick(100);
    assert(!Backlight::sleeping());
    tick(29899);
    assert(Backlight::sleeping()); // 30,000 elapsed modulo 2^32.
    assert(!sample(UINT32_MAX-100,true));
    assert(!sample(148,false));
    assert(!sample(149,false));
    assert(sample(150,true));
    // GPIO fallback respects both board polarities and brightness restoration.
    pwmFailure=true;
    assert(!Backlight::begin(80));
    assert(gpioLevel==!LCD_BL_ACTIVE_LOW);
    Backlight::setSleeping(true);
    assert(gpioLevel==LCD_BL_ACTIVE_LOW);
    Backlight::set(40);
    assert(gpioLevel==LCD_BL_ACTIVE_LOW);
    Backlight::setSleeping(false);
    assert(gpioLevel==!LCD_BL_ACTIVE_LOW && Backlight::get()==40);
    Config::get().screenOffSeconds=30;
    Karaoke::songPlaying=true;
    tick(1000000);
    assert(!Backlight::sleeping());
    Karaoke::songPlaying=false;
    tick(1029999);
    assert(!Backlight::sleeping());
    tick(1030000);
    assert(Backlight::sleeping() && MyWifi::powerSave);
    assert(!sample(1030010,true) && !MyWifi::powerSave);
    GameLive::gameRunning=true;
    tick(1090009);
    assert(!Backlight::sleeping());
    tick(1090010);
    assert(Backlight::sleeping() && MyWifi::powerSave);
    assert(!sample(1090020,true));
    Config::get().ecoSleep=true;
    UiScreenPower::sleepNow();
    assert(!UiScreenPower::ecoSleeping()); // Running dice game is protected.
    GameLive::gameRunning=false;
    AdvLive::adventure=true;
    UiScreenPower::sleepNow();
    assert(!UiScreenPower::ecoSleeping());
    AdvLive::adventure=false;
    AppWebFiles::transferring=true;
    UiScreenPower::sleepNow();
    assert(!UiScreenPower::ecoSleeping());
    AppWebFiles::transferring=false;
    UiScreenPower::sleepNow();
    assert(UiScreenPower::ecoSleeping() && Audio::idle && MyWifi::idle);
    assert(!sample(1090030,true));
    assert(!UiScreenPower::ecoSleeping() && !Audio::idle && !MyWifi::idle);
    UiScreenPower::sleepNow();
    Config::get().ecoSleep=false;
    tick(1090040);
    assert(!UiScreenPower::ecoSleeping() && !Backlight::sleeping());
    if (Display::supported) {
        assert(Display::sleepCalls>2 && !Display::asleep);
        Config::get().ecoSleep=true;
        UiScreenPower::sleepNow();
        assert(Display::asleep && Backlight::sleeping());
        Display::failWake=true;
        assert(!sample(1090050,true));
        assert(Display::asleep && Backlight::sleeping());
        const int wakeCalls=Display::wakeCalls;
        tick(1091049);
        assert(Display::wakeCalls==wakeCalls);
        Display::failWake=false;
        tick(1091050);
        assert(!Display::asleep && !Backlight::sleeping() && !UiScreenPower::ecoSleeping());
        Display::failSleep=true;
        UiScreenPower::sleepNow();
        assert(!Display::asleep && !Backlight::sleeping() && !UiScreenPower::ecoSleeping());
        Config::get().ecoSleep=false;
        UiScreenPower::sleepNow(); // Plain screen-off keeps the light off if panel sleep fails.
        assert(Backlight::sleeping() && !UiScreenPower::ecoSleeping());
        Display::failSleep=false;
        assert(!sample(1091100,true) && !Backlight::sleeping() && !Display::asleep);
    } else {
        assert(Display::sleepCalls==0 && Display::wakeCalls==0);
    }
}
'''

# The shared port must filter before constructing LVGL's pressed/released report.
port = (ROOT / "components/my_lvgl/my_lvgl.cpp").read_text()
pointer = port.split("void pointerCb(", 1)[1].split("void lvglTask(", 1)[0]
assert pointer.index("g_pointerFilter(pressed)") < pointer.index("d->state =")

with tempfile.TemporaryDirectory(prefix="otr-screen-power-") as directory:
    work = pathlib.Path(directory)
    (work / "stubs.h").write_text(STUB)
    for name in ["my_sys.h", "my_ota.h", "my_lvgl.h", "esp_log.h",
                 "driver/gpio.h", "driver/ledc.h", "sdkconfig.h", "adv_live.h",
                 "board_audio.h", "app_web_files.h", "board_display.h"]:
        header = work / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#include "stubs.h"\n')
    (work / "test.cpp").write_text(TEST)
    (work / "screen.cpp").write_text((ROOT / "main/ui_screen_power.cpp").read_text())
    for p4 in [False, True]:
        binary = work / f"power-{p4}"
        subprocess.run([
            "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            * (["-DCONFIG_OTR_HW_PROFILE_P4_43=1"] if p4 else []),
            f"-I{work}", f"-I{ROOT / 'main'}", f"-I{ROOT / 'components/my_wifi/include'}",
            f"-I{ROOT / 'components/otr_midi/include'}",
            str(work / "screen.cpp"), str(ROOT / "main/board_backlight.cpp"),
            str(work / "test.cpp"), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
print("S3/P4 screen power: idle/manual off, touch release + 250 ms, OTA, brightness, wrap and polarity passed")
