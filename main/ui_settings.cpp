// ui_settings.cpp - settings screen.
#include "ui_settings.h"

#include "my_sys.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "board_audio.h"
#include "board_backlight.h"
#include "board_sd.h"
#include "board_display.h"
#include "app_config.h"
#include "time_zones.h"
#include "my_time.h"
#include "board_rgb.h"
#include "board_battery.h"
#include "board_touch.h"
#include "battery_math.h"
#include "board_pins.h"
#include "karaoke.h"
#include "my_wifi.h"
#include "my_web.h"
#include "my_ota.h"
#include "app_web_files.h"
#include "ui_clock.h"
#include "ui_numpad.h"
#include "lang.h"
#include "my_theme.h"
#include "ui_wifi.h"
#include "ui_yatzy.h"
#include "ui_sudoku.h"
#include "ui_screen_power.h"
#include "ui_metrics.h"
#include "hw_profile.h"

namespace UiSettings {

enum class Page : uint8_t { GAME, USER, ADMIN };

static AppConfig s_edit;            // Working copy, committed on "Spara"
static lv_obj_t *s_brightVal, *s_ledVal, *s_animVal, *s_netInfo, *s_touchInfo, *s_batteryInfo;
static lv_obj_t *s_masterVal, *s_sfxVal, *s_musVal, *s_zoneVal, *s_slowVal, *s_fastVal, *s_holdVal, *s_sdInfo;
static lv_obj_t *s_slowSl, *s_fastSl;
static lv_obj_t *s_sudokuDeadZoneVal = nullptr, *s_deadZoneActive = nullptr;
static lv_timer_t *s_timer = nullptr;
static lv_obj_t *s_adminCards[5];
static lv_obj_t *s_adminPad = nullptr;
static lv_obj_t *s_soundSwitch = nullptr, *s_ecoSwitch = nullptr;
static lv_obj_t *s_gameCard = nullptr, *s_joyCard = nullptr, *s_sudokuCard = nullptr;
static lv_obj_t *s_userDisplay = nullptr, *s_soundCard = nullptr;

static void showPage(Page page);

static bool adminPinDone(const char *pin, bool ok, void *)
{
    if (!ok) {
        s_adminPad = nullptr;
        return true;
    }
    if (!Config::adminOk(pin)) {
        UiNumpad::setHint(s_adminPad, tr(Str::PERR_BAD_PIN), true);
        return false;
    }
    s_adminPad = nullptr;
    showPage(Page::ADMIN);
    return true;
}

static void openAdministration()
{
    if (!Config::get().adminPin[0]) {
        showPage(Page::ADMIN);
        return;
    }
    s_adminPad = UiNumpad::open(tr(Str::MENU_SETTINGS_ADMIN), tr(Str::SET_ADMIN_PIN_INFO),
                                adminPinDone, nullptr, tr(Str::BTN_CANCEL),
                                UiNumpad::MIN_PIN_LENGTH, UiNumpad::MAX_PIN_LENGTH);
}

// Display (MADCTL) and touch mirroring together. Also called from the web server task, hence
// the (recursive) LVGL lock.
static void applyRotation(bool r180)
{
    DisplayLock l;
    Display::setRotation(r180);
    Touch::setRotation(r180);
}

uint8_t maxTextSize()
{
    const int32_t w = UiMetrics::screenW(), h = UiMetrics::screenH();
    return (uint8_t)MyFonts::maxFor(w < h ? w : h);
}

void applyTextSize()
{
    static_assert((int)MyFonts::TextSize::COUNT == TEXT_SIZE_COUNT, "text size levels");
    static_assert((int)MyFonts::ScreenZoom::COUNT == SCREEN_ZOOM_COUNT, "screen zoom levels");
    DisplayLock lock;
    const int32_t w = UiMetrics::screenW(), h = UiMetrics::screenH();
    MyFonts::apply((MyFonts::TextSize)Config::get().textSize, w < h ? w : h,
                   HwProfile::active().pixelPitchYUm(), (MyFonts::ScreenZoom)Config::get().screenZoom);
}

void applyHardware()
{
    DisplayLock lock;
    const AppConfig &c = Config::get();
    Backlight::set(c.brightness);
    Rgb::setEnabled(c.ledEnabled);
    Rgb::setBrightness(c.ledBrightness);
    applyRotation(c.rotation == 180);
    Audio::setVolumes(c.soundOn, c.sfxVol, c.musicOn, c.musicVol);
    Audio::setMasterVolume(c.masterVol);
}

bool toggleSound()
{
    AppConfig &config = Config::get();
    const bool previous = config.soundOn;
    config.soundOn = !previous;
    if (!Config::save()) {
        config.soundOn = previous;
        return false;
    }
    if (s_soundSwitch) {
        s_edit.soundOn = config.soundOn;
        lv_obj_set_state(s_soundSwitch, LV_STATE_CHECKED, config.soundOn);
    }
    Audio::setVolumes(config.soundOn, config.sfxVol, config.musicOn, config.musicVol);
    return true;
}

bool togglePowerSaving()
{
    AppConfig &config = Config::get();
    const bool previous = config.ecoSleep;
    config.ecoSleep = !previous;
    if (!Config::save()) {
        config.ecoSleep = previous;
        return false;
    }
    if (s_ecoSwitch) {
        s_edit.ecoSleep = config.ecoSleep;
        lv_obj_set_state(s_ecoSwitch, LV_STATE_CHECKED, config.ecoSleep);
    }
    return true;
}

void prepareForMenuNavigation()
{
    if (!s_timer) return;
    lv_timer_delete(s_timer);
    s_timer = nullptr;
    applyHardware();
}

static void previewSound()
{
    Audio::setVolumes(s_edit.soundOn, s_edit.sfxVol, s_edit.musicOn, s_edit.musicVol);
    Audio::setMasterVolume(s_edit.masterVol);
}

static void refreshInfo(lv_timer_t *)
{
    char buf[200];
    MyWifi::Info wi = MyWifi::info();
    if (wi.connected)
        snprintf(buf, sizeof(buf), tr(Str::SET_NET_INFO_CONNECTED), wi.ssid,
                 MyWifi::statusText(wi.status), wi.ip, wi.rssi, wi.channel, wi.bssid,
                 Config::get().hostname);
    else
        snprintf(buf, sizeof(buf), tr(Str::SET_NET_INFO), wi.ssid[0] ? wi.ssid : "–",
                 MyWifi::statusText(wi.status), Config::get().hostname);
    lv_label_set_text(s_netInfo, buf);
    Touch::Info ti = Touch::info();
    snprintf(buf, sizeof(buf), tr(Str::SET_TOUCH_INFO), ti.present ? "OK" : tr(Str::SET_MISSING), ti.lastX, ti.lastY,
             (unsigned long)ti.touchCount);
    lv_label_set_text(s_touchInfo, buf);
    BoardSd::Info si = BoardSd::info();
    int n = si.mounted ? snprintf(buf, sizeof(buf), tr(Str::SET_SD_INFO), si.name, si.width, (unsigned)(si.totalBytes >> 20),
                                  (unsigned)(si.freeBytes >> 20))
                       : snprintf(buf, sizeof(buf), "%s", tr(Str::SET_SD_NONE));
    Audio::Status as = Audio::status();
    if (n > 0 && n < (int)sizeof(buf) - 2) {
        buf[n++] = '\n';
        snprintf(buf + n, sizeof(buf) - n, tr(Str::SET_AUDIO_INFO), as.running ? "OK" : tr(Str::SET_MISSING));
    }
    lv_label_set_text(s_sdInfo, buf);

    char batteryText[180];
    const BoardBattery::Status bs = BoardBattery::status();
    if (!bs.supported) {
        snprintf(batteryText, sizeof(batteryText), "%s", tr(Str::BATTERY_UNSUPPORTED));
    } else if (!bs.initialized) {
        snprintf(batteryText, sizeof(batteryText), "%s",
                 tr(bs.powerManaged ? Str::BATTERY_PMIC_FAILED : Str::BATTERY_ADC_FAILED));
    } else if (!bs.sampleValid) {
        snprintf(batteryText, sizeof(batteryText), "%s", tr(Str::BATTERY_WAIT));
    } else if (bs.powerManaged) {
        char voltage[80];
        if (!bs.batteryConnected)
            snprintf(voltage, sizeof(voltage), "%s", tr(Str::BATTERY_ABSENT));
        else if (bs.pmicVoltageValid)
            snprintf(voltage, sizeof(voltage), tr(Str::BATTERY_VOLTAGE_FMT),
                     (unsigned)(bs.pmicMillivolts / 1000), (unsigned)(bs.pmicMillivolts % 1000));
        else
            snprintf(voltage, sizeof(voltage), "%s", tr(Str::BATTERY_WAIT));
        snprintf(batteryText, sizeof(batteryText), tr(Str::BATTERY_PMIC_INFO_FMT), voltage,
                 bs.usbPresent, bs.charging, bs.chargeDone, bs.pmicPercentValid ? bs.pmicPercent : -1);
    } else if (!bs.adcCalibrated) {
        char raw[48];
        snprintf(raw, sizeof(raw), tr(Str::BATTERY_RAW_FMT), (long)bs.raw);
        snprintf(batteryText, sizeof(batteryText), "%s\n%s\n%s", tr(Str::BATTERY_ADC_CAL_MISSING),
                 raw, tr(Str::BATTERY_UNKNOWN));
    } else {
        uint16_t batteryMv = 0;
        const bool calibrated = BatteryMath::batteryMillivolts(bs.adcMillivolts, s_edit.batteryScalePermille,
                                                                batteryMv);
        char raw[48], adc[48], voltage[48];
        snprintf(raw, sizeof(raw), tr(Str::BATTERY_RAW_FMT), (long)bs.raw);
        snprintf(adc, sizeof(adc), tr(Str::BATTERY_ADC_MV_FMT), (unsigned)bs.adcMillivolts);
        if (calibrated) {
            snprintf(voltage, sizeof(voltage), tr(Str::BATTERY_VOLTAGE_FMT),
                     (unsigned)(batteryMv / 1000), (unsigned)(batteryMv % 1000));
            snprintf(batteryText, sizeof(batteryText), "%s\n%s\n%s", raw, adc, voltage);
        } else {
            snprintf(batteryText, sizeof(batteryText), "%s\n%s\n%s", raw, adc, tr(Str::BATTERY_UNKNOWN));
        }
    }
    lv_label_set_text(s_batteryInfo, batteryText);
}

static bool batteryCalibrationDone(const char *reference, bool ok, void *pad)
{
    if (!ok) return true;
    const BoardBattery::Status bs = BoardBattery::status();
    uint16_t scale = 0;
    const long batteryMv = reference ? strtol(reference, nullptr, 10) : 0;
    if (bs.powerManaged || !bs.supported || !bs.sampleValid || !bs.adcCalibrated ||
        !BatteryMath::scaleFromReference((int32_t)batteryMv, bs.adcMillivolts, scale)) {
        UiNumpad::setHint((lv_obj_t *)pad, tr(Str::BATTERY_CALIBRATION_FAILED), true);
        return false;
    }

    AppConfig &c = Config::get();
    const uint16_t previous = c.batteryScalePermille;
    c.batteryScalePermille = scale;
    if (!Config::save()) {
        c.batteryScalePermille = previous;
        UiNumpad::setHint((lv_obj_t *)pad, tr(Str::BATTERY_SAVE_FAILED), true);
        return false;
    }
    s_edit.batteryScalePermille = scale;
    refreshInfo(nullptr);
    return true;
}

static void resetBatteryCalibration()
{
    AppConfig &c = Config::get();
    const uint16_t previous = c.batteryScalePermille;
    if (previous == BAT_DEFAULT_SCALE_PERMILLE) return;
    c.batteryScalePermille = BAT_DEFAULT_SCALE_PERMILLE;
    if (!Config::save()) {
        c.batteryScalePermille = previous;
        return;
    }
    s_edit.batteryScalePermille = c.batteryScalePermille;
    refreshInfo(nullptr);
}

struct SliderGesture {
    enum class Direction { PENDING, HORIZONTAL, VERTICAL };
    lv_point_t start{};
    Direction direction = Direction::PENDING;
    bool tracking = false;
};

static void sliderGesture(lv_event_t *e)
{
    auto *gesture = static_cast<SliderGesture *>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) {
        delete gesture;
        return;
    }
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
        code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) return;
    lv_indev_t *indev = lv_event_get_indev(e);
    if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) return;
    lv_obj_t *slider = lv_event_get_current_target_obj(e);
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    if (code == LV_EVENT_PRESSED) {
        gesture->start = point;
        gesture->direction = SliderGesture::Direction::PENDING;
        gesture->tracking = true;
        return;
    }
    if (!gesture->tracking) return;
    if (gesture->direction == SliderGesture::Direction::PENDING) {
        const int32_t dx = LV_ABS(point.x - gesture->start.x);
        const int32_t dy = LV_ABS(point.y - gesture->start.y);
        const int32_t slop = LV_MAX(1, MyFonts::uiPixels(8));
        if (LV_MAX(dx, dy) >= slop)
            gesture->direction = dx > dy ? SliderGesture::Direction::HORIZONTAL
                                        : SliderGesture::Direction::VERTICAL;
    }
    if (code == LV_EVENT_PRESSING) {
        if (gesture->direction != SliderGesture::Direction::HORIZONTAL)
            lv_event_stop_processing(e);
        else
            lv_obj_set_scroll_chain_ver(slider, false);
        return;
    }
    gesture->tracking = false;
    if (gesture->direction == SliderGesture::Direction::VERTICAL ||
        (code == LV_EVENT_PRESS_LOST && gesture->direction == SliderGesture::Direction::PENDING)) {
        // Keep base release cleanup, but skip the slider's release-to-set-value behavior.
        lv_obj_event_base(&lv_slider_class, e);
        lv_obj_set_scroll_chain_ver(slider, true);
        lv_event_stop_processing(e);
    }
}

static lv_obj_t *sliderRow(lv_obj_t *parent, const char *title, int min, int max, int value, lv_obj_t **valLabel,
                           lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 58);
    lv_obj_set_scrollable(row, false);
    lv_obj_t *t = Theme::label(row, title, &my_font_14, Theme::MUTED);
    lv_obj_set_pos(t, 0, 0);
    *valLabel = Theme::label(row, "", &my_font_14, Theme::ACCENT);
    lv_obj_align(*valLabel, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t *s = lv_slider_create(row);
    lv_obj_set_size(s, LV_PCT(92), 10);
    lv_obj_align(s, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_slider_set_range(s, min, max);
    lv_slider_set_value(s, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s, Theme::c(Theme::PANEL_RAISED), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, Theme::c(Theme::ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, Theme::c(Theme::TEXT), LV_PART_KNOB);
    lv_obj_add_event_cb(s, sliderGesture, static_cast<lv_event_code_t>(LV_EVENT_ALL | LV_EVENT_PREPROCESS),
                        new SliderGesture{});
    lv_obj_add_event_cb(s, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_send_event(s, LV_EVENT_VALUE_CHANGED, nullptr);   // Initial value text
    return s;
}

static void onBright(lv_event_t *e)
{
    s_edit.brightness = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(e));
    lv_label_set_text_fmt(s_brightVal, "%d %%", s_edit.brightness);
    Backlight::set(s_edit.brightness);   // Live preview
}

static void onLed(lv_event_t *e)
{
    s_edit.ledBrightness = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(e));
    lv_label_set_text_fmt(s_ledVal, "%d", s_edit.ledBrightness);
    Rgb::setBrightness(s_edit.ledBrightness);
}

static void onAnim(lv_event_t *e)
{
    s_edit.rollAnimMs = (uint16_t)(lv_slider_get_value(lv_event_get_target_obj(e)) / 50 * 50);
    lv_label_set_text_fmt(s_animVal, "%d ms", s_edit.rollAnimMs);
}

static void onSfx(lv_event_t *e)
{
    s_edit.sfxVol = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(e));
    lv_label_set_text_fmt(s_sfxVal, "%d %%", s_edit.sfxVol);
    previewSound();
}

static void onMasterVolume(lv_event_t *e)
{
    s_edit.masterVol = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(e));
    lv_label_set_text_fmt(s_masterVal, "%d %%", s_edit.masterVol);
    Audio::setMasterVolume(s_edit.masterVol);
}

static void onMusic(lv_event_t *e)
{
    s_edit.musicVol = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(e));
    lv_label_set_text_fmt(s_musVal, "%d %%", s_edit.musicVol);
    previewSound();
}

static void onZones(lv_event_t *e)
{
    s_edit.joyZones = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(e));
    lv_label_set_text_fmt(s_zoneVal, "%d", s_edit.joyZones);
}

// Slowest / fastest step interval; the other slider follows so that fast <= slow.
static void onSlow(lv_event_t *e)
{
    s_edit.joySlowMs = (uint16_t)(lv_slider_get_value(lv_event_get_target_obj(e)) / 5 * 5);
    lv_label_set_text_fmt(s_slowVal, tr(Str::SET_MS_PER_STEP), s_edit.joySlowMs);
    if (s_fastSl && s_edit.joyFastMs > s_edit.joySlowMs) {
        s_edit.joyFastMs = s_edit.joySlowMs;
        lv_slider_set_value(s_fastSl, s_edit.joyFastMs, LV_ANIM_OFF);
        lv_label_set_text_fmt(s_fastVal, tr(Str::SET_MS_PER_STEP), s_edit.joyFastMs);
    }
}

static void onFast(lv_event_t *e)
{
    s_edit.joyFastMs = (uint16_t)(lv_slider_get_value(lv_event_get_target_obj(e)) / 5 * 5);
    lv_label_set_text_fmt(s_fastVal, tr(Str::SET_MS_PER_STEP), s_edit.joyFastMs);
    if (s_slowSl && s_edit.joySlowMs < s_edit.joyFastMs) {
        s_edit.joySlowMs = s_edit.joyFastMs;
        lv_slider_set_value(s_slowSl, s_edit.joySlowMs, LV_ANIM_OFF);
        lv_label_set_text_fmt(s_slowVal, tr(Str::SET_MS_PER_STEP), s_edit.joySlowMs);
    }
}

static void onHold(lv_event_t *e)
{
    s_edit.joyHoldMs = (uint16_t)(lv_slider_get_value(lv_event_get_target_obj(e)) / 10 * 10);
    lv_label_set_text_fmt(s_holdVal, "%d ms", s_edit.joyHoldMs);
}

static void updateDeadZonePreview()
{
    const int zone = s_edit.sudokuDeadZone;
    lv_obj_set_size(s_deadZoneActive, 48 - 2 * zone, 48 - 2 * zone);
    lv_obj_set_pos(s_deadZoneActive, zone, zone);
}

static void onSudokuDeadZone(lv_event_t *e)
{
    s_edit.sudokuDeadZone = (uint8_t)lv_slider_get_value(lv_event_get_target_obj(e));
    lv_label_set_text_fmt(s_sudokuDeadZoneVal, "%d px", s_edit.sudokuDeadZone);
    if (s_deadZoneActive) updateDeadZonePreview();
}

static void leave(bool save)
{
    if (save) {
        const AppConfig previous = Config::get();
        Config::get().brightness = s_edit.brightness;
        Config::get().screenOffSeconds = s_edit.screenOffSeconds;
        Config::get().ecoSleep = s_edit.ecoSleep;
        Config::get().powerLog = s_edit.powerLog;
        Config::get().menuAnimations = s_edit.menuAnimations;
        snprintf(Config::get().tz, sizeof(Config::get().tz), "%s", s_edit.tz);
        Config::get().textSize = s_edit.textSize;
        Config::get().screenZoom = s_edit.screenZoom;
        Config::get().ledBrightness = s_edit.ledBrightness;
        Config::get().ledEnabled = s_edit.ledEnabled;
        Config::get().rollAnimMs = s_edit.rollAnimMs;
        Config::get().defaultVariant = s_edit.defaultVariant;
        Config::get().resultProtocolFirst = s_edit.resultProtocolFirst;
        Config::get().maxiSavedRolls = s_edit.maxiSavedRolls;
        Config::get().throwMode = s_edit.throwMode;
        Config::get().rotation = s_edit.rotation;
        Config::get().soundOn = s_edit.soundOn;
        Config::get().masterVol = s_edit.masterVol;
        Config::get().sfxVol = s_edit.sfxVol;
        Config::get().musicOn = s_edit.musicOn;
        Config::get().musicVol = s_edit.musicVol;
        Config::get().batteryScalePermille = s_edit.batteryScalePermille;
        Config::get().sudokuDeadZone = s_edit.sudokuDeadZone;
        Config::get().sudokuHelpHints = s_edit.sudokuHelpHints;
        Config::get().joyZones = s_edit.joyZones;
        Config::get().joySlowMs = s_edit.joySlowMs;
        Config::get().joyFastMs = s_edit.joyFastMs;
        Config::get().joyHoldMs = s_edit.joyHoldMs;
        Config::get().joyDiag = s_edit.joyDiag;
        snprintf(Config::get().lang, sizeof(Config::get().lang), "%s", s_edit.lang);
        if (!Config::save()) {
            Config::get() = previous;
            lv_obj_t *mb = lv_msgbox_create(nullptr);
            lv_msgbox_add_text(mb, tr(Str::SET_SAVE_FAILED));
            lv_msgbox_add_close_button(mb);
            Theme::msgboxFonts(mb);
            return;
        }
        Lang::setLang(Config::get().lang);
        MyTime::setTz(Config::get().tz);
    }
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = nullptr;
    }
    applyHardware();   // Reverts the live preview when cancelled
    if (save) applyTextSize();
    show();
}

static lv_obj_t *styledDropdown(lv_obj_t *parent, const char *opts, uint32_t sel)
{
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_dropdown_set_options(dd, opts);
    lv_dropdown_set_selected(dd, sel);
    lv_obj_set_width(dd, LV_PCT(100));
    lv_obj_set_height(dd, UiMetrics::buttonH());
    lv_obj_set_style_bg_color(dd, Theme::c(Theme::PANEL_RAISED), 0);
    lv_obj_set_style_text_color(dd, Theme::c(Theme::TEXT), 0);
    lv_obj_set_style_border_color(dd, Theme::c(Theme::LINE), 0);
    lv_obj_set_style_text_font(dd, &my_font_18, 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(dd), &my_font_18, 0);
    return dd;
}

static lv_obj_t *switchRow(lv_obj_t *parent, const char *title, bool on, lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(row, UiMetrics::buttonH(), 0);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, UiMetrics::marginSmall(), 0);
    lv_obj_t *t = Theme::label(row, title, &my_font_14);
    lv_obj_set_width(t, 0);
    lv_obj_set_flex_grow(t, 1);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_pad_ver(row, UiMetrics::marginSmall(), 0);
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, UiMetrics::buttonWMin(), MyFonts::uiPixels(26));
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(sw, Theme::c(Theme::ACCENT), (lv_style_selector_t)LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    // Double-tap guard: a second flip within 500 ms is undone silently (setting the state from
    // code does not emit VALUE_CHANGED) and never reaches the real callback.
    struct SwCtx { lv_event_cb_t cb; uint32_t last; };
    auto *ctx = new SwCtx{cb, 0};
    lv_obj_add_event_cb(sw, [](lv_event_t *e) {
        auto *c = (SwCtx *)lv_event_get_user_data(e);
        lv_obj_t *o = lv_event_get_target_obj(e);
        if (!Theme::debounce(c->last)) {
            lv_obj_set_state(o, LV_STATE_CHECKED, !lv_obj_has_state(o, LV_STATE_CHECKED));
            return;
        }
        c->cb(e);
    }, LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_add_event_cb(sw, [](lv_event_t *e) { delete (SwCtx *)lv_event_get_user_data(e); }, LV_EVENT_DELETE, ctx);
    return sw;
}

void show()
{
    Theme::setStatusShareAvailable(false);
    lv_obj_t *scr = Theme::screen();
    lv_obj_t *header = Theme::header(scr, tr(Str::SET_TITLE), false);
    lv_obj_set_height(header, LV_MAX(Theme::headerHeight(), UiMetrics::buttonH() + 12));
    lv_obj_t *back = Theme::button(header, LV_SYMBOL_LEFT, UiMetrics::buttonWMin(), UiMetrics::buttonH(), Theme::MUTED);
    lv_obj_set_width(lv_obj_get_child(header, 0), LV_MAX(1, UiMetrics::screenW() -
        Theme::batteryStatusWidth() - UiMetrics::buttonWMin() - 30));
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -Theme::batteryStatusWidth() - 8, 0);
    lv_obj_add_event_cb(back, [](lv_event_t *) {
        prepareForMenuNavigation();
        UiYatzy::showMenu();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_remove_style_all(body);
    const int32_t sw = UiMetrics::screenW(), sh = UiMetrics::screenH();
    lv_obj_set_pos(body, UiMetrics::marginNormal(), UiMetrics::headerH());
    lv_obj_set_size(body, sw - 2 * UiMetrics::marginNormal(), sh - UiMetrics::headerH() - UiMetrics::footerH());
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, UiMetrics::cardSpacing(), 0);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);

    lv_obj_t *game = Theme::menuCard(body, LV_PCT(100), tr(Str::MENU_SETTINGS_GAME),
                                     tr(Str::MENU_SETTINGS_GAME_DESC), UiYatzy::faceSprite(40, 6),
                                     LV_SYMBOL_IMAGE, Theme::ACCENT);
    lv_obj_add_event_cb(game, [](lv_event_t *) { showPage(Page::GAME); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *players = Theme::menuCard(body, LV_PCT(100), tr(Str::MENU_SETTINGS_PLAYERS),
                                        tr(Str::MENU_SETTINGS_PLAYERS_DESC), nullptr,
                                        LV_SYMBOL_LIST, Theme::TEAL);
    lv_obj_add_event_cb(players, [](lv_event_t *) { UiYatzy::showPlayerCenter(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *user = Theme::menuCard(body, LV_PCT(100), tr(Str::MENU_SETTINGS_USER),
                                     tr(Str::MENU_SETTINGS_USER_DESC), nullptr,
                                     LV_SYMBOL_HOME, Theme::GREEN);
    lv_obj_add_event_cb(user, [](lv_event_t *) { showPage(Page::USER); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *admin = Theme::menuCard(body, LV_PCT(100), tr(Str::MENU_SETTINGS_ADMIN),
                                      tr(Str::MENU_SETTINGS_ADMIN_DESC), nullptr,
                                      LV_SYMBOL_WIFI, Theme::GOLD);
    lv_obj_add_event_cb(admin, [](lv_event_t *) { openAdministration(); }, LV_EVENT_CLICKED, nullptr);
    Theme::loadScreen(scr);
}

static void showPage(Page page)
{
    Theme::setStatusShareAvailable(false);
    s_edit = Config::get();
    memset(s_adminCards, 0, sizeof(s_adminCards));
    s_gameCard = s_joyCard = s_sudokuCard = s_userDisplay = s_soundCard = nullptr;
    s_sudokuDeadZoneVal = s_deadZoneActive = nullptr;
    s_adminPad = nullptr;
    s_soundSwitch = s_ecoSwitch = nullptr;
    lv_obj_t *scr = Theme::screen();
    lv_obj_add_event_cb(scr, [](lv_event_t *e) {
        if (lv_event_get_target(e) == lv_event_get_current_target(e))
            s_soundSwitch = s_ecoSwitch = nullptr;
    }, LV_EVENT_DELETE, nullptr);
    const char *title = page == Page::GAME ? tr(Str::MENU_SETTINGS_GAME) :
                        page == Page::USER ? tr(Str::MENU_SETTINGS_USER) : tr(Str::MENU_SETTINGS_ADMIN);
    lv_obj_t *header = Theme::header(scr, title, false);
    lv_obj_set_height(header, LV_MAX(Theme::headerHeight(), UiMetrics::buttonH() + 12));
    lv_obj_t *back = Theme::button(header, LV_SYMBOL_LEFT, UiMetrics::buttonWMin(), UiMetrics::buttonH(), Theme::MUTED);
    lv_obj_set_width(lv_obj_get_child(header, 0), LV_MAX(1, UiMetrics::screenW() -
        Theme::batteryStatusWidth() - UiMetrics::buttonWMin() - 30));
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -Theme::batteryStatusWidth() - 8, 0);
    lv_obj_add_event_cb(back, [](lv_event_t *) { leave(false); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_remove_style_all(body);
    const int32_t sw = UiMetrics::screenW(), sh = UiMetrics::screenH();
    const int32_t top = LV_MAX(Theme::headerHeight(), UiMetrics::buttonH() + 12) + UiMetrics::marginSmall();
    const int32_t footer = UiMetrics::buttonH() + 2 * UiMetrics::marginSmall();
    lv_obj_set_pos(body, 0, top);
    lv_obj_set_size(body, sw, sh - top - footer);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, 8, 0);
    lv_obj_set_style_pad_row(body, 8, 0);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);

    // Language first, so a user who cannot read the current language finds it immediately.
    lv_obj_t *lang = Theme::card(body, LV_PCT(100), LV_SIZE_CONTENT);
    s_adminCards[0] = lang;
    lv_obj_set_flex_flow(lang, LV_FLEX_FLOW_COLUMN);
    Theme::label(lang, trp(LV_SYMBOL_LIST "  ", Str::SET_LANGUAGE), &my_font_14, Theme::ACCENT);
    static char langOpts[Lang::MAX_LANGS * 24];
    langOpts[0] = 0;
    int langSel = 0;
    for (int i = 0; i < Lang::count(); i++) {
        if (i) strlcat(langOpts, "\n", sizeof(langOpts));
        strlcat(langOpts, Lang::name(i), sizeof(langOpts));
        if (strcmp(Lang::code(i), s_edit.lang) == 0) langSel = i;
    }
    lv_obj_t *ld = styledDropdown(lang, langOpts, langSel);
    lv_obj_add_event_cb(ld, [](lv_event_t *e) {
        int i = (int)lv_dropdown_get_selected(lv_event_get_target_obj(e));
        snprintf(s_edit.lang, sizeof(s_edit.lang), "%s", Lang::code(i));
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *disp = Theme::card(body, LV_PCT(100), LV_SIZE_CONTENT);
    s_userDisplay = disp;
    lv_obj_set_flex_flow(disp, LV_FLEX_FLOW_COLUMN);
    sliderRow(disp, tr(Str::SET_BRIGHTNESS), 5, 100, s_edit.brightness, &s_brightVal, onBright);
    Theme::label(disp, tr(Str::SET_SCREEN_ZOOM), &my_font_14, Theme::MUTED);
    lv_obj_t *screenZoom = styledDropdown(disp, tr(Str::SET_SCREEN_ZOOM_OPTIONS), s_edit.screenZoom);
    lv_obj_add_event_cb(screenZoom, [](lv_event_t *e) {
        s_edit.screenZoom = (uint8_t)lv_dropdown_get_selected(lv_event_get_target_obj(e));
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_t *zoomHelp = Theme::label(disp, tr(Str::SET_SCREEN_ZOOM_HELP), &my_font_14, Theme::MUTED);
    lv_obj_set_width(zoomHelp, LV_PCT(100));
    lv_label_set_long_mode(zoomHelp, LV_LABEL_LONG_MODE_WRAP);
    // Both choices apply on save when the menu is rebuilt.
    Theme::label(disp, tr(Str::SET_TEXT_SIZE), &my_font_14, Theme::MUTED);
    static char sizeOpts[96];
    snprintf(sizeOpts, sizeof(sizeOpts), "%s", tr(Str::SET_TEXT_SIZE_OPTIONS));
    {
        int lines = 0;   // Keep options 0..maxTextSize()
        for (char *p = sizeOpts; *p; ++p)
            if (*p == '\n' && ++lines > maxTextSize()) { *p = 0; break; }
    }
    if (s_edit.textSize > maxTextSize()) s_edit.textSize = maxTextSize();
    lv_obj_t *textSize = styledDropdown(disp, sizeOpts, s_edit.textSize);
    lv_obj_add_event_cb(textSize, [](lv_event_t *e) {
        s_edit.textSize = (uint8_t)lv_dropdown_get_selected(lv_event_get_target_obj(e));
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    Theme::label(disp, tr(Str::SCREEN_TIMEOUT), &my_font_14, Theme::MUTED);
    uint32_t timeoutSelection = 0;
    for (uint32_t i = 0; i < sizeof(SCREEN_OFF_SECONDS) / sizeof(SCREEN_OFF_SECONDS[0]); ++i)
        if (SCREEN_OFF_SECONDS[i] == s_edit.screenOffSeconds) timeoutSelection = i;
    lv_obj_t *timeout = styledDropdown(disp, tr(Str::SCREEN_TIMEOUT_OPTIONS), timeoutSelection);
    lv_obj_add_event_cb(timeout, [](lv_event_t *e) {
        s_edit.screenOffSeconds = SCREEN_OFF_SECONDS[lv_dropdown_get_selected(lv_event_get_target_obj(e))];
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    s_ecoSwitch = switchRow(disp, tr(Str::SCREEN_ECO), s_edit.ecoSleep, [](lv_event_t *e) {
        s_edit.ecoSleep = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    });
    lv_obj_t *screenHelp = Theme::label(disp, tr(Str::SCREEN_SLEEP_HELP), &my_font_14, Theme::MUTED);
    lv_obj_set_width(screenHelp, LV_PCT(100));
    lv_label_set_long_mode(screenHelp, LV_LABEL_LONG_MODE_WRAP);
    if (HwProfile::active().hasRgbLed) {
        switchRow(disp, tr(Str::SET_RGB_LED), s_edit.ledEnabled, [](lv_event_t *e) {
            s_edit.ledEnabled = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
            Rgb::setEnabled(s_edit.ledEnabled);
        });
        sliderRow(disp, tr(Str::SET_LED_BRIGHTNESS), 0, 255, s_edit.ledBrightness, &s_ledVal, onLed);
    }
    // Rotation: previewed immediately (MADCTL + touch), kept on Save, reverted on Cancel.
    Theme::label(disp, tr(Str::SET_ROTATION), &my_font_14, Theme::MUTED);
    static char rotOpts[96];
    snprintf(rotOpts, sizeof(rotOpts), "%s\n%s", tr(Str::SET_ROT_0), tr(Str::SET_ROT_180));
    lv_obj_t *rd = styledDropdown(disp, rotOpts, s_edit.rotation == 180 ? 1 : 0);
    lv_obj_add_event_cb(rd, [](lv_event_t *e) {
        s_edit.rotation = lv_dropdown_get_selected(lv_event_get_target_obj(e)) ? 180 : 0;
        applyRotation(s_edit.rotation == 180);
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *powerAdmin = Theme::card(body, LV_PCT(100), LV_SIZE_CONTENT);
    s_adminCards[1] = powerAdmin;
    lv_obj_set_flex_flow(powerAdmin, LV_FLEX_FLOW_COLUMN);
    Theme::label(powerAdmin, tr(Str::SET_ADMIN), &my_font_14, Theme::ACCENT);
    switchRow(powerAdmin, tr(Str::SET_MENU_ANIMATIONS), s_edit.menuAnimations, [](lv_event_t *e) {
        s_edit.menuAnimations = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    });
    switchRow(powerAdmin, tr(Str::POWER_HISTORY), s_edit.powerLog, [](lv_event_t *e) {
        s_edit.powerLog = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    });
    lv_obj_t *screenOff = Theme::button(powerAdmin, tr(Str::SCREEN_SLEEP_NOW), LV_PCT(100), 44, Theme::LINE);
    lv_obj_add_event_cb(screenOff, [](lv_event_t *) { UiScreenPower::sleepNow(); }, LV_EVENT_CLICKED, nullptr);

    // Sound: previewed live (volumes + a sample effect when a slider is released)
    lv_obj_t *snd = Theme::card(body, LV_PCT(100), LV_SIZE_CONTENT);
    s_soundCard = snd;
    lv_obj_set_flex_flow(snd, LV_FLEX_FLOW_COLUMN);
    Theme::label(snd, trp(LV_SYMBOL_AUDIO "  ", Str::SET_SOUND), &my_font_14, Theme::ACCENT);
    s_soundSwitch = switchRow(snd, tr(Str::SET_SOUND_ON), s_edit.soundOn, [](lv_event_t *e) {
        s_edit.soundOn = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
        previewSound();
        Audio::play(Sound::Sfx::CLICK);
    });
    sliderRow(snd, tr(Str::SET_MASTER_VOL), 0, 100, s_edit.masterVol, &s_masterVal, onMasterVolume);
    lv_obj_t *sfx = sliderRow(snd, tr(Str::SET_SFX_VOL), 0, 100, s_edit.sfxVol, &s_sfxVal, onSfx);
    lv_obj_add_event_cb(sfx, [](lv_event_t *) { Audio::play(Sound::Sfx::SCORE); }, LV_EVENT_RELEASED, nullptr);
    switchRow(snd, tr(Str::SET_MUSIC_ON), s_edit.musicOn, [](lv_event_t *e) {
        s_edit.musicOn = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
        previewSound();
    });
    lv_obj_t *mus = sliderRow(snd, tr(Str::SET_MUSIC_VOL), 0, 100, s_edit.musicVol, &s_musVal, onMusic);
    lv_obj_add_event_cb(mus, [](lv_event_t *) { Audio::play(Sound::Sfx::CLICK); }, LV_EVENT_RELEASED, nullptr);

    lv_obj_t *game = Theme::card(body, LV_PCT(100), LV_SIZE_CONTENT);
    s_gameCard = game;
    lv_obj_set_flex_flow(game, LV_FLEX_FLOW_COLUMN);
    sliderRow(game, tr(Str::SET_ROLL_ANIM), 300, 2000, s_edit.rollAnimMs, &s_animVal, onAnim);
    Theme::label(game, tr(Str::SET_DEFAULT_VARIANT), &my_font_14, Theme::MUTED);
    static char varOpts[224];
    snprintf(varOpts, sizeof(varOpts), "%s\n%s\n%s\n%s\n%s\n%s",
             tr(Str::VAR_YATZY), tr(Str::VAR_MAXI), tr(Str::VAR_YATZY_ORDER),
             tr(Str::VAR_MAXI_ORDER), tr(Str::VAR_MIDI), tr(Str::VAR_MIDI_ORDER));
    lv_obj_t *dd = styledDropdown(game, varOpts, s_edit.defaultVariant);
    lv_obj_add_event_cb(dd, [](lv_event_t *e) {
        s_edit.defaultVariant = (uint8_t)lv_dropdown_get_selected(lv_event_get_target_obj(e));
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    Theme::label(game, tr(Str::SET_RESULT_VIEW), &my_font_14, Theme::MUTED);
    static char resultViewOpts[64];
    snprintf(resultViewOpts, sizeof(resultViewOpts), "%s\n%s",
             tr(Str::RES_RANKING_VIEW), tr(Str::RES_PROTOCOL_VIEW));
    lv_obj_t *resultView = styledDropdown(game, resultViewOpts, s_edit.resultProtocolFirst ? 1 : 0);
    lv_obj_add_event_cb(resultView, [](lv_event_t *e) {
        s_edit.resultProtocolFirst = lv_dropdown_get_selected(lv_event_get_target_obj(e)) == 1;
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    switchRow(game, tr(Str::SET_MAXI_SAVED), s_edit.maxiSavedRolls, [](lv_event_t *e) {
        s_edit.maxiSavedRolls = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    });
    Theme::label(game, tr(Str::SET_THROW_MODE), &my_font_14, Theme::MUTED);
    static char tmOpts[96];
    snprintf(tmOpts, sizeof(tmOpts), "%s\n%s", tr(Str::SET_THROW_CUP), tr(Str::SET_THROW_BUTTON));
    lv_obj_t *tm = styledDropdown(game, tmOpts, s_edit.throwMode ? 1 : 0);
    lv_obj_add_event_cb(tm, [](lv_event_t *e) {
        s_edit.throwMode = (uint8_t)lv_dropdown_get_selected(lv_event_get_target_obj(e));
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Adventure joystick (components/otr_engine/include/otr_joystick.h)
    lv_obj_t *joy = Theme::card(body, LV_PCT(100), LV_SIZE_CONTENT);
    s_joyCard = joy;
    lv_obj_set_flex_flow(joy, LV_FLEX_FLOW_COLUMN);
    Theme::label(joy, trp(LV_SYMBOL_GPS "  ", Str::SET_JOY), &my_font_14, Theme::ACCENT);
    s_slowSl = s_fastSl = nullptr;
    sliderRow(joy, tr(Str::SET_JOY_ZONES), 1, 4, s_edit.joyZones, &s_zoneVal, onZones);
    s_slowSl = sliderRow(joy, tr(Str::SET_JOY_SLOW), JOY_MS_MIN, JOY_MS_MAX, s_edit.joySlowMs, &s_slowVal, onSlow);
    s_fastSl = sliderRow(joy, tr(Str::SET_JOY_FAST), JOY_MS_MIN, JOY_MS_MAX, s_edit.joyFastMs, &s_fastVal, onFast);
    sliderRow(joy, tr(Str::SET_JOY_HOLD), JOY_HOLD_MIN, JOY_HOLD_MAX, s_edit.joyHoldMs, &s_holdVal, onHold);
    Theme::label(joy, tr(Str::SET_JOY_DIAG), &my_font_14, Theme::MUTED);
    static char diagOpts[96];
    snprintf(diagOpts, sizeof(diagOpts), "%s\n%s", tr(Str::SET_JOY_DIAG_DOM), tr(Str::SET_JOY_DIAG_ALT));
    lv_obj_t *jd = styledDropdown(joy, diagOpts, s_edit.joyDiag ? 1 : 0);
    lv_obj_add_event_cb(jd, [](lv_event_t *e) {
        s_edit.joyDiag = (uint8_t)lv_dropdown_get_selected(lv_event_get_target_obj(e));
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *sudoku = Theme::card(body, LV_PCT(100), LV_SIZE_CONTENT);
    s_sudokuCard = sudoku;
    lv_obj_set_flex_flow(sudoku, LV_FLEX_FLOW_COLUMN);
    Theme::label(sudoku, tr(Str::SUDOKU_TITLE), &my_font_14, Theme::ACCENT);
    sliderRow(sudoku, tr(Str::SET_SUDOKU_DEAD_ZONE), SUDOKU_DEAD_ZONE_MIN,
              SUDOKU_DEAD_ZONE_MAX, s_edit.sudokuDeadZone, &s_sudokuDeadZoneVal, onSudokuDeadZone);
    switchRow(sudoku, tr(Str::SET_SUDOKU_HELP_HINTS), s_edit.sudokuHelpHints, [](lv_event_t *event) {
        s_edit.sudokuHelpHints = lv_obj_has_state(lv_event_get_target_obj(event), LV_STATE_CHECKED);
    });
    lv_obj_t *deadZoneHelp = Theme::label(sudoku, tr(Str::SET_SUDOKU_DEAD_ZONE_HELP), &my_font_14, Theme::MUTED);
    lv_obj_set_width(deadZoneHelp, LV_PCT(100));
    lv_label_set_long_mode(deadZoneHelp, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_t *preview = lv_obj_create(sudoku);
    lv_obj_remove_style_all(preview);
    lv_obj_set_size(preview, LV_PCT(100), 56);
    lv_obj_set_scrollable(preview, false);
    lv_obj_t *cell = lv_obj_create(preview);
    lv_obj_remove_style_all(cell);
    lv_obj_set_size(cell, 48, 48);
    lv_obj_set_pos(cell, 4, 4);
    lv_obj_set_style_bg_color(cell, Theme::c(Theme::RED), 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
    lv_obj_t *active = lv_obj_create(cell);
    lv_obj_remove_style_all(active);
    s_deadZoneActive = active;
    lv_obj_set_style_bg_color(active, Theme::c(Theme::TEXT), 0);
    lv_obj_set_style_bg_opa(active, LV_OPA_COVER, 0);
    lv_obj_t *digit = lv_label_create(active);
    lv_label_set_text(digit, "8");
    lv_obj_set_style_text_font(digit, &my_font_24, 0);
    lv_obj_set_style_text_color(digit, Theme::c(Theme::BG), 0);
    lv_obj_center(digit);
    updateDeadZonePreview();
    lv_obj_t *resetProgress = Theme::button(sudoku, tr(Str::SUDOKU_RESET_PROGRESS),
                                            LV_PCT(100), UiMetrics::buttonH(), Theme::MUTED);
    lv_obj_add_event_cb(resetProgress, [](lv_event_t *) {
        UiSudoku::confirmResetProgress();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *clk = Theme::card(body, LV_PCT(100), LV_SIZE_CONTENT);
    s_adminCards[2] = clk;
    lv_obj_set_flex_flow(clk, LV_FLEX_FLOW_COLUMN);
    Theme::label(clk, tr(Str::SET_CLOCK), &my_font_14, Theme::ACCENT);
    UiClock::attachLabel(Theme::label(clk, "", &my_font_18), true);
    Theme::label(clk, tr(Str::SET_TIME_ZONE), &my_font_14, Theme::MUTED);
    static char zoneOptions[2048];
    zoneOptions[0] = 0;
    size_t used = 0;
    for (const auto &zone : TimeZones::ZONES)
        used += snprintf(zoneOptions + used, sizeof(zoneOptions) - used, "%s%s",
                         used ? "\n" : "", zone.label);
    const int selectedZone = TimeZones::find(s_edit.tz);
    if (selectedZone < 0)
        snprintf(zoneOptions + used, sizeof(zoneOptions) - used, "\n%s", tr(Str::SET_TIME_ZONE_SAVED));
    lv_obj_t *zone = styledDropdown(clk, zoneOptions,
                                   selectedZone < 0 ? TimeZones::COUNT : selectedZone);
    lv_obj_add_event_cb(zone, [](lv_event_t *e) {
        const unsigned selected = lv_dropdown_get_selected(lv_event_get_target_obj(e));
        if (selected < TimeZones::COUNT)
            snprintf(s_edit.tz, sizeof(s_edit.tz), "%s", TimeZones::ZONES[selected].tz);
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_t *clkBtn = Theme::button(clk, trp(LV_SYMBOL_EDIT " ", Str::CLOCK_TITLE), LV_PCT(100), 40, Theme::TEAL);
    lv_obj_add_event_cb(clkBtn, [](lv_event_t *) {
        if (s_timer) {
            lv_timer_delete(s_timer);
            s_timer = nullptr;
        }
        applyHardware();   // Discard unsaved live previews
        UiClock::showSetter(show);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *battery = Theme::card(body, LV_PCT(100), LV_SIZE_CONTENT);
    s_adminCards[3] = battery;
    lv_obj_set_flex_flow(battery, LV_FLEX_FLOW_COLUMN);
    Theme::label(battery, tr(Str::SET_BATTERY), &my_font_14, Theme::ACCENT);
    const bool powerManaged = BoardBattery::status().powerManaged;
    lv_obj_t *help = Theme::label(battery, tr(powerManaged ? Str::BATTERY_PMIC_HELP : Str::BATTERY_HELP),
                                  &my_font_14, Theme::MUTED);
    lv_label_set_long_mode(help, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(help, LV_PCT(100));
    s_batteryInfo = Theme::label(battery, "", &my_font_14);
    lv_label_set_long_mode(s_batteryInfo, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s_batteryInfo, LV_PCT(100));
    lv_obj_t *calibrate = Theme::button(battery, tr(Str::BATTERY_CALIBRATE), LV_PCT(100), 40, Theme::TEAL);
    lv_obj_add_event_cb(calibrate, [](lv_event_t *) {
        UiNumpad::open(tr(Str::BATTERY_CALIBRATE), tr(Str::BATTERY_ENTER_MV),
                       batteryCalibrationDone, nullptr, tr(Str::BTN_CANCEL));
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *reset = Theme::button(battery, tr(Str::BATTERY_RESET), LV_PCT(100), 40, Theme::MUTED);
    lv_obj_add_event_cb(reset, [](lv_event_t *) { resetBatteryCalibration(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_hidden(calibrate, powerManaged);
    lv_obj_set_hidden(reset, powerManaged);

    lv_obj_t *net = Theme::card(body, LV_PCT(100), LV_SIZE_CONTENT);
    s_adminCards[4] = net;
    lv_obj_set_flex_flow(net, LV_FLEX_FLOW_COLUMN);
    Theme::label(net, tr(Str::SET_NETWORK), &my_font_14, Theme::ACCENT);
    s_netInfo = Theme::label(net, "", &my_font_14);
    lv_label_set_long_mode(s_netInfo, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s_netInfo, LV_PCT(100));
    s_touchInfo = Theme::label(net, "", &my_font_14, Theme::MUTED);
    s_sdInfo = Theme::label(net, "", &my_font_14, Theme::MUTED);
    lv_label_set_long_mode(s_sdInfo, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s_sdInfo, LV_PCT(100));
    lv_obj_t *sdBtn = Theme::button(net, trp(LV_SYMBOL_SD_CARD " ", Str::SET_SD_REMOUNT), LV_PCT(100), 40, Theme::TEAL);
    lv_obj_add_event_cb(sdBtn, [](lv_event_t *) {
        BoardSd::remount();
        Audio::invalidateFiles();
        refreshInfo(nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *wifiBtn = Theme::button(net, trp(LV_SYMBOL_WIFI " ", Str::SET_CHANGE_WIFI), LV_PCT(100), 40, Theme::TEAL);
    lv_obj_add_event_cb(wifiBtn, [](lv_event_t *) {
        if (s_timer) {
            lv_timer_delete(s_timer);
            s_timer = nullptr;
        }
        applyHardware();   // Discard unsaved live previews
        UiWifi::showNetworks();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *restart = Theme::button(net, tr(Str::SET_REBOOT), LV_PCT(100), 40, Theme::MUTED);
    lv_obj_add_event_cb(restart, [](lv_event_t *) {
        lv_obj_t *box = lv_msgbox_create(nullptr);
        lv_msgbox_add_text(box, tr(Str::SET_REBOOT_Q));
        lv_msgbox_add_close_button(box);
        lv_obj_t *confirm = lv_msgbox_add_footer_button(box, tr(Str::SET_REBOOT));
        lv_obj_add_event_cb(confirm, [](lv_event_t *e) {
            auto *dialog = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
            if (MyOta::busy() || AppWebFiles::busy()) {
                lv_msgbox_add_text(dialog, tr(Str::SET_REBOOT_BUSY));
                Theme::msgboxFonts(dialog);
                return;
            }
            if (!MyWeb::rebootSoon()) {
                lv_msgbox_add_text(dialog, tr(Str::SET_REBOOT_FAILED));
                Theme::msgboxFonts(dialog);
                return;
            }
            lv_obj_add_state(lv_event_get_target_obj(e), LV_STATE_DISABLED);
            lv_msgbox_add_text(dialog, tr(Str::OTA_RESTARTING));
            Theme::msgboxFonts(dialog);
        }, LV_EVENT_CLICKED, box);
        Theme::msgboxFonts(box);
    }, LV_EVENT_CLICKED, nullptr);

    for (lv_obj_t *card : s_adminCards)
        if (card) lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
    for (lv_obj_t *card : s_adminCards)
        if (page == Page::ADMIN && card) lv_obj_clear_flag(card, LV_OBJ_FLAG_HIDDEN);
    if (page != Page::USER) {
        lv_obj_add_flag(s_userDisplay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_soundCard, LV_OBJ_FLAG_HIDDEN);
    }
    if (page != Page::GAME) {
        lv_obj_add_flag(s_gameCard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_joyCard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_sudokuCard, LV_OBJ_FLAG_HIDDEN);
    }

    const int32_t inset = UiMetrics::marginSmall();
    const int32_t halfW = (sw - 3 * inset) / 2;
    lv_obj_t *cancel = Theme::button(scr, trp(LV_SYMBOL_CLOSE " ", Str::BTN_CANCEL), halfW, UiMetrics::buttonH(), Theme::MUTED);
    lv_obj_set_pos(cancel, inset, sh - UiMetrics::buttonH() - inset);
    lv_obj_add_event_cb(cancel, [](lv_event_t *) { leave(false); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *save = Theme::button(scr, trp(LV_SYMBOL_SAVE " ", Str::BTN_SAVE), halfW, UiMetrics::buttonH(), Theme::ACCENT, true);
    lv_obj_set_pos(save, sw - inset - halfW, sh - UiMetrics::buttonH() - inset);
    lv_obj_add_event_cb(save, [](lv_event_t *) { leave(true); }, LV_EVENT_CLICKED, nullptr);

    refreshInfo(nullptr);
    s_timer = lv_timer_create(refreshInfo, 1000, nullptr);
    Theme::loadScreen(scr);
}

}  // namespace UiSettings
