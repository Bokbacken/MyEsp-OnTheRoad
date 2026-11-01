#include "ui_screen_power.h"

#include "app_config.h"
#include "board_backlight.h"
#include "board_display.h"
#include "my_lvgl.h"
#include "my_ota.h"
#include "my_sys.h"
#include "game_live.h"
#include "karaoke.h"
#include "my_wifi.h"
#include "screen_power_policy.h"
#include "esp_log.h"
#include "board_audio.h"
#include "adv_live.h"
#include "app_web_files.h"
#include <atomic>

namespace {
uint32_t s_lastActivity = 0;
uint32_t s_wakeAt = 0;
bool s_blockTouch = false;
std::atomic<bool> s_eco{false};
bool s_wakePending = false;
uint32_t s_wakeRetryAt = 0;

bool ecoBlocked()
{
    return MyOta::busy() || AppWebFiles::busy() || GameLive::running() ||
           AdvLive::active() || Karaoke::playing();
}

void wake(uint32_t now)
{
    if (Display::panelSleeping() && !Display::setPanelSleeping(false)) {
        s_wakePending = true;
        s_wakeRetryAt = now;
        ESP_LOGW("screen", "panel wake failed; light stays off, retry in one second");
        return;
    }
    s_wakePending = false;
    now = MySys::millis();
    Backlight::setSleeping(false);
    if (s_eco.exchange(false)) {
        Audio::setSleeping(false);
        MyWifi::setSuspended(false);
        ESP_LOGI("screen", "idle eco mode ended");
    }
    MyWifi::setScreenPowerSave(false);
    s_lastActivity = now;
    s_wakeAt = now;
    s_blockTouch = true;
    ESP_LOGI("screen", "backlight awake");
}

bool filterPointer(bool pressed)
{
    const uint32_t now = MySys::millis();
    if (s_wakePending && now - s_wakeRetryAt < 1000) return false;
    if (s_eco && (!Config::get().ecoSleep || ecoBlocked())) {
        wake(now);
        if (s_wakePending) return false;
    }
    if (pressed) s_lastActivity = now;
    if (Backlight::sleeping()) {
        if (pressed) wake(now);
        return false;
    }
    if (s_blockTouch) {
        // A contact begun during the guard stays blocked until it is released.
        if (!pressed && now - s_wakeAt >= 250) s_blockTouch = false;
        return false;
    }
    return pressed;
}

void tick(lv_timer_t *)
{
    const uint32_t now = MySys::millis();
    if (s_wakePending) {
        if (now - s_wakeRetryAt >= 1000) wake(now);
        if (s_wakePending) return;
    }
    if (s_eco && (!Config::get().ecoSleep || ecoBlocked())) wake(now);
    if (s_wakePending) return;
    if (MyOta::busy()) {
        if (Backlight::sleeping()) wake(now);
        s_lastActivity = now;
        return;
    }
    const uint16_t seconds = Config::get().screenOffSeconds;
    if (Karaoke::playing()) {
        if (Backlight::sleeping()) wake(now);
        s_lastActivity = now;
        return;
    }
    if (!Backlight::sleeping() &&
        ScreenPowerPolicy::shouldSleep(seconds, now - s_lastActivity, false, GameLive::running()))
        UiScreenPower::sleepNow();
}
}  // namespace

namespace UiScreenPower {
void begin()
{
    static bool started = false;
    if (started) return;
    started = true;
    s_lastActivity = MySys::millis();
    MyLvgl::setPointerFilter(filterPointer);
    lv_timer_create(tick, 100, nullptr);
}

void sleepNow()
{
    if (s_wakePending) {
        ESP_LOGW("screen", "screen-off blocked while panel wake is pending");
        return;
    }
    if (MyOta::busy()) {
        ESP_LOGW("screen", "screen-off blocked during OTA");
        return;
    }
    if (Config::get().ecoSleep) {
        if (ecoBlocked()) {
            ESP_LOGW("screen", "idle eco mode blocked by game, playback or transfer");
            s_lastActivity = MySys::millis();
            return;
        }
        if (!s_eco.exchange(true)) {
            Backlight::setSleeping(true);
            if (Display::panelSleepSupported() && !Display::setPanelSleeping(true)) {
                ESP_LOGW("screen", "panel sleep failed; cancelling idle request");
                wake(MySys::millis());
                return;
            }
            Audio::setSleeping(true);
            MyWifi::setSuspended(true);
            ESP_LOGI("screen", "idle eco mode requested; CPU and touch remain awake");
        }
    }
    MyWifi::setScreenPowerSave(true);
    Backlight::setSleeping(true);
    if (Display::panelSleepSupported() && !Display::panelSleeping() && !Display::setPanelSleeping(true))
        ESP_LOGW("screen", "panel sleep failed; backlight stays off");
    ESP_LOGI("screen", "backlight off; touch wake enabled");
}
bool ecoSleeping() { return s_eco.load(); }
}  // namespace UiScreenPower
