// ui_settings.cpp - settings screen.
#include "ui_settings.h"

#include "my_sys.h"
#include <stdio.h>

#include "board_backlight.h"
#include "app_config.h"
#include "board_rgb.h"
#include "board_touch.h"
#include "my_wifi.h"
#include "ui_clock.h"
#include "my_theme.h"
#include "ui_wifi.h"
#include "ui_yatzy.h"

namespace UiSettings {

static AppConfig s_edit;            // Working copy, committed on "Spara"
static lv_obj_t *s_brightVal, *s_ledVal, *s_animVal, *s_netInfo, *s_touchInfo;
static lv_timer_t *s_timer = nullptr;

void applyHardware()
{
    const AppConfig &c = Config::get();
    Backlight::set(c.brightness);
    Rgb::setEnabled(c.ledEnabled);
    Rgb::setBrightness(c.ledBrightness);
}

static void refreshInfo(lv_timer_t *)
{
    char buf[200];
    MyWifi::Info wi = MyWifi::info();
    if (wi.connected)
        snprintf(buf, sizeof(buf), "Wi-Fi: %s – %s\nIP: %s  %d dBm  kanal %u\nAP: %s\nWebb: http://%s.local", wi.ssid,
                 MyWifi::statusText(wi.status), wi.ip, wi.rssi, wi.channel, wi.bssid,
                 Config::get().hostname);
    else
        snprintf(buf, sizeof(buf), "Wi-Fi: %s – %s\nWebb: http://%s.local", wi.ssid[0] ? wi.ssid : "–",
                 MyWifi::statusText(wi.status), Config::get().hostname);
    lv_label_set_text(s_netInfo, buf);
    Touch::Info ti = Touch::info();
    snprintf(buf, sizeof(buf), "Touch: %s  x=%u y=%u  tryck=%lu", ti.present ? "OK" : "saknas", ti.lastX, ti.lastY,
             (unsigned long)ti.touchCount);
    lv_label_set_text(s_touchInfo, buf);
}

static lv_obj_t *sliderRow(lv_obj_t *parent, const char *title, int min, int max, int value, lv_obj_t **valLabel,
                           lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 58);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
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

static void leave(bool save)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = nullptr;
    }
    if (save) {
        Config::get().brightness = s_edit.brightness;
        Config::get().ledBrightness = s_edit.ledBrightness;
        Config::get().ledEnabled = s_edit.ledEnabled;
        Config::get().rollAnimMs = s_edit.rollAnimMs;
        Config::get().defaultVariant = s_edit.defaultVariant;
        Config::get().maxiSavedRolls = s_edit.maxiSavedRolls;
        Config::save();
    }
    applyHardware();   // Reverts the live preview when cancelled
    UiYatzy::showMenu();
}

static lv_obj_t *switchRow(lv_obj_t *parent, const char *title, bool on, lv_event_cb_t cb)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), 36);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = Theme::label(row, title, &my_font_14);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 50, 26);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(sw, Theme::c(Theme::ACCENT), (lv_style_selector_t)LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return sw;
}

void show()
{
    s_edit = Config::get();
    lv_obj_t *scr = Theme::screen();
    Theme::header(scr, "Inställningar");

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_remove_style_all(body);
    lv_obj_set_pos(body, 0, 46);
    lv_obj_set_size(body, 320, 480 - 46 - 56);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, 8, 0);
    lv_obj_set_style_pad_row(body, 8, 0);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);

    lv_obj_t *disp = Theme::card(body, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(disp, LV_FLEX_FLOW_COLUMN);
    sliderRow(disp, "Skärmljusstyrka", 5, 100, s_edit.brightness, &s_brightVal, onBright);
    switchRow(disp, "RGB-LED", s_edit.ledEnabled, [](lv_event_t *e) {
        s_edit.ledEnabled = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
        Rgb::setEnabled(s_edit.ledEnabled);
    });
    sliderRow(disp, "LED-ljusstyrka", 0, 255, s_edit.ledBrightness, &s_ledVal, onLed);

    lv_obj_t *game = Theme::card(body, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(game, LV_FLEX_FLOW_COLUMN);
    sliderRow(game, "Kastanimation", 300, 2000, s_edit.rollAnimMs, &s_animVal, onAnim);
    lv_obj_t *dd = lv_dropdown_create(game);
    lv_dropdown_set_options(dd, "Yatzy\nMaxi-Yatzy\nYatzy i ordning\nMaxi-Yatzy i ordning");
    lv_dropdown_set_selected(dd, s_edit.defaultVariant);
    lv_obj_set_width(dd, LV_PCT(100));
    lv_obj_set_style_bg_color(dd, Theme::c(Theme::PANEL_RAISED), 0);
    lv_obj_set_style_text_color(dd, Theme::c(Theme::TEXT), 0);
    lv_obj_set_style_border_color(dd, Theme::c(Theme::LINE), 0);
    lv_obj_add_event_cb(dd, [](lv_event_t *e) {
        s_edit.defaultVariant = (uint8_t)lv_dropdown_get_selected(lv_event_get_target_obj(e));
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    switchRow(game, "Maxi: spara oanvända kast", s_edit.maxiSavedRolls, [](lv_event_t *e) {
        s_edit.maxiSavedRolls = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    });

    lv_obj_t *clk = Theme::card(body, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(clk, LV_FLEX_FLOW_COLUMN);
    Theme::label(clk, "Klocka", &my_font_14, Theme::ACCENT);
    UiClock::attachLabel(Theme::label(clk, "", &my_font_18), true);
    lv_obj_t *clkBtn = Theme::button(clk, LV_SYMBOL_EDIT " Ställ klockan", LV_PCT(100), 40, Theme::TEAL);
    lv_obj_add_event_cb(clkBtn, [](lv_event_t *) {
        if (s_timer) {
            lv_timer_delete(s_timer);
            s_timer = nullptr;
        }
        applyHardware();   // Discard unsaved live previews
        UiClock::showSetter(show);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *net = Theme::card(body, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(net, LV_FLEX_FLOW_COLUMN);
    Theme::label(net, "Nätverk och diagnostik", &my_font_14, Theme::ACCENT);
    s_netInfo = Theme::label(net, "", &my_font_14);
    lv_label_set_long_mode(s_netInfo, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s_netInfo, LV_PCT(100));
    s_touchInfo = Theme::label(net, "", &my_font_14, Theme::MUTED);
    lv_obj_t *wifiBtn = Theme::button(net, LV_SYMBOL_WIFI " Byt Wi-Fi-nätverk", LV_PCT(100), 40, Theme::TEAL);
    lv_obj_add_event_cb(wifiBtn, [](lv_event_t *) {
        if (s_timer) {
            lv_timer_delete(s_timer);
            s_timer = nullptr;
        }
        applyHardware();   // Discard unsaved live previews
        UiWifi::showNetworks();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *cancel = Theme::button(scr, LV_SYMBOL_CLOSE " Avbryt", 146, 44, Theme::MUTED);
    lv_obj_set_pos(cancel, 8, 480 - 50);
    lv_obj_add_event_cb(cancel, [](lv_event_t *) { leave(false); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *save = Theme::button(scr, LV_SYMBOL_SAVE " Spara", 152, 44, Theme::ACCENT, true);
    lv_obj_set_pos(save, 160, 480 - 50);
    lv_obj_add_event_cb(save, [](lv_event_t *) { leave(true); }, LV_EVENT_CLICKED, nullptr);

    refreshInfo(nullptr);
    s_timer = lv_timer_create(refreshInfo, 1000, nullptr);
    Theme::loadScreen(scr);
}

}  // namespace UiSettings
