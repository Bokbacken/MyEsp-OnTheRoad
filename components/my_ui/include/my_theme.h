// ui_theme.h - shared colours, fonts and small style helpers for the on-device LVGL UI.
// Palette follows the MyPi-Hotspot dark web theme (navy panels, orange accent) with a teal
// secondary accent for the game screens.
#pragma once

#include <stdio.h>

#include "lvgl.h"

// Fonts generated with lv_font_conv: Montserrat Medium (ASCII + Latin-1, so å/ä/ö work)
// plus the LVGL FontAwesome symbol set (LV_SYMBOL_*).
LV_FONT_DECLARE(my_font_14);
LV_FONT_DECLARE(my_font_18);
LV_FONT_DECLARE(my_font_24);
LV_FONT_DECLARE(my_font_36);

namespace Theme {

constexpr int32_t BATTERY_STATUS_WIDTH = 60;

struct BatteryIndicator {
    bool voltageValid;
    uint16_t millivolts;
};

using BatteryIndicatorReader = BatteryIndicator (*)();

inline BatteryIndicatorReader &batteryIndicatorReader()
{
    static BatteryIndicatorReader reader = nullptr;
    return reader;
}

inline void setBatteryIndicatorReader(BatteryIndicatorReader reader)
{
    batteryIndicatorReader() = reader;
}

inline void refreshBatteryIndicator(lv_timer_t *timer)
{
    if (!timer) return;
    auto *label = static_cast<lv_obj_t *>(lv_timer_get_user_data(timer));
    if (!label || !lv_obj_is_valid(label)) return;
    const BatteryIndicatorReader reader = batteryIndicatorReader();
    const BatteryIndicator state = reader ? reader() : BatteryIndicator{};
    if (state.voltageValid && state.millivolts >= 2500 && state.millivolts <= 4500)
        lv_label_set_text_fmt(label, "%u.%uV", state.millivolts / 1000, (state.millivolts % 1000) / 100);
    else
        lv_label_set_text(label, "?");
}

// --- Palette (0xRRGGBB) ---
constexpr uint32_t BG          = 0x101B2B;   // Navy page background
constexpr uint32_t PANEL       = 0x17263A;
constexpr uint32_t PANEL_RAISED= 0x1D3048;
constexpr uint32_t LINE        = 0x34465D;
constexpr uint32_t TEXT        = 0xEDF2F8;
constexpr uint32_t MUTED       = 0xA9B8CA;
constexpr uint32_t ACCENT      = 0xFF9C3D;   // Orange (primary actions, focus)
constexpr uint32_t ACCENT_DARK = 0x4A2E12;
constexpr uint32_t TEAL        = 0x00B4D8;   // Secondary accent (game)
constexpr uint32_t GREEN       = 0x62D28A;
constexpr uint32_t RED         = 0xFF7373;
constexpr uint32_t BLUE        = 0x80BAFF;
constexpr uint32_t GOLD        = 0xFFD45C;
constexpr uint32_t DIE_FACE    = 0xF7F4EC;   // Ivory dice
constexpr uint32_t DIE_PIP     = 0x1A1F2B;
constexpr uint32_t NEST_BG     = 0x0B1420;   // Darker "nest" for held dice

inline lv_color_t c(uint32_t hex) { return lv_color_hex(hex); }

// Double-tap guard for toggle-type actions: returns true if the action may run, false when it
// arrives within `ms` of the previous accepted one. `last` is per-control state (0 = never).
inline bool debounce(uint32_t &last, uint32_t ms = 500)
{
    if (last != 0 && lv_tick_elaps(last) < ms) return false;
    last = lv_tick_get() | 1;   // Never store 0 (means "never")
    return true;
}

// Plain screen with the theme background.
inline lv_obj_t *screen()
{
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s, c(BG), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s, c(TEXT), 0);
    lv_obj_set_style_text_font(s, &my_font_18, 0);
    lv_obj_set_scrollable(s, false);
    return s;
}

// Rounded card (panel colour, thin line border), like the web UI cards.
inline lv_obj_t *card(lv_obj_t *parent, int32_t w, int32_t h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c(PANEL), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, c(LINE), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 12, 0);
    lv_obj_set_style_pad_all(o, 8, 0);
    lv_obj_set_style_text_color(o, c(TEXT), 0);
    return o;
}

inline lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font = &my_font_18,
                       uint32_t color = TEXT)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, c(color), 0);
    return l;
}

// Button with a coloured border; filled with the colour while pressed / when `filled`.
inline lv_obj_t *button(lv_obj_t *parent, const char *text, int32_t w, int32_t h,
                        uint32_t color = ACCENT, bool filled = false)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, c(color), 0);
    lv_obj_set_style_bg_color(b, c(filled ? color : PANEL_RAISED), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(b, c(filled ? BG : TEXT), 0);
    lv_obj_set_style_bg_color(b, c(color), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(b, c(BG), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_border_color(b, c(LINE), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(b, c(MUTED), LV_STATE_DISABLED);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &my_font_18, 0);
    lv_obj_center(l);
    return b;
}

// Header bar used by all app screens: title on the left, optional right-side widgets.
inline lv_obj_t *header(lv_obj_t *scr, const char *title)
{
    lv_obj_t *h = lv_obj_create(scr);
    lv_obj_remove_style_all(h);
    lv_obj_set_size(h, LV_PCT(100), 44);
    lv_obj_set_style_bg_color(h, c(PANEL), 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(h, 2, 0);
    lv_obj_set_style_border_color(h, c(ACCENT), 0);
    lv_obj_set_scrollable(h, false);
    lv_obj_t *t = label(h, title, &my_font_24);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_width(t, LV_PCT(76));
    lv_obj_set_height(t, lv_font_get_line_height(&my_font_24));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *battery = lv_obj_create(h);
    lv_obj_remove_style_all(battery);
    lv_obj_set_size(battery, BATTERY_STATUS_WIDTH, 32);
    lv_obj_align(battery, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_add_flag(battery, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_scrollable(battery, false);
    lv_obj_set_clickable(battery, false);

    lv_obj_t *body = lv_obj_create(battery);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, 18, 12);
    lv_obj_set_pos(body, 0, 10);
    lv_obj_set_style_radius(body, 2, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(body, c(ACCENT), 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_clickable(body, false);
    lv_obj_t *terminal = lv_obj_create(battery);
    lv_obj_remove_style_all(terminal);
    lv_obj_set_size(terminal, 3, 6);
    lv_obj_set_pos(terminal, 18, 13);
    lv_obj_set_style_bg_color(terminal, c(ACCENT), 0);
    lv_obj_set_style_bg_opa(terminal, LV_OPA_COVER, 0);
    lv_obj_set_clickable(terminal, false);
    lv_obj_t *voltage = label(battery, "?", &my_font_14, MUTED);
    lv_obj_set_size(voltage, 36, lv_font_get_line_height(&my_font_14));
    lv_obj_set_pos(voltage, 24, 8);
    lv_obj_set_clickable(voltage, false);
    lv_timer_t *timer = lv_timer_create(refreshBatteryIndicator, 1000, voltage);
    refreshBatteryIndicator(timer);
    if (timer) {
        lv_obj_add_event_cb(h, [](lv_event_t *e) {
            if (lv_event_get_target(e) == lv_event_get_current_target(e))
                lv_timer_delete(static_cast<lv_timer_t *>(lv_event_get_user_data(e)));
        }, LV_EVENT_DELETE, timer);
    }
    return h;
}

// Applies `font` to an object and all of its descendants. lv_msgbox builds its title, text and
// footer buttons with the LVGL default theme font (no Latin-1, so "Fortsätt" became "Forts#tt");
// call this after the last lv_msgbox_add_* call so every child uses one of our fonts.
inline void fontTree(lv_obj_t *o, const lv_font_t *font)
{
    lv_obj_set_style_text_font(o, font, 0);
    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) fontTree(lv_obj_get_child(o, (int32_t)i), font);
}

inline void msgboxFonts(lv_obj_t *mbox, const lv_font_t *font = &my_font_18)
{
    fontTree(mbox, font);
}

// Loads a screen with a short fade and deletes the previous one.
inline void loadScreen(lv_obj_t *scr)
{
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_FADE_IN, 180, 0, true);
}

}  // namespace Theme
