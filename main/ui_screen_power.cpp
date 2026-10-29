#include "ui_screen_power.h"

#include "app_config.h"
#include "board_backlight.h"
#include "my_lvgl.h"
#include "my_ota.h"
#include "my_sys.h"
#include "esp_log.h"

namespace {
uint32_t s_lastActivity = 0;
uint32_t s_wakeAt = 0;
bool s_blockTouch = false;

void wake(uint32_t now)
{
    Backlight::setSleeping(false);
    s_lastActivity = now;
    s_wakeAt = now;
    s_blockTouch = true;
    ESP_LOGI("screen", "backlight awake");
}

bool filterPointer(bool pressed)
{
    const uint32_t now = MySys::millis();
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
    if (MyOta::busy()) {
        if (Backlight::sleeping()) wake(now);
        s_lastActivity = now;
        return;
    }
    const uint16_t seconds = Config::get().screenOffSeconds;
    if (!Backlight::sleeping() && seconds && now - s_lastActivity >= uint32_t(seconds) * 1000)
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
    if (MyOta::busy()) {
        ESP_LOGW("screen", "screen-off blocked during OTA");
        return;
    }
    Backlight::setSleeping(true);
    ESP_LOGI("screen", "backlight off; touch wake enabled");
}
}  // namespace UiScreenPower
