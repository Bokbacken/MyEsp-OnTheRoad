// ui_theme.h - shared colours, fonts and small style helpers for the on-device LVGL UI.
// Palette follows the MyPi-Hotspot dark web theme (navy panels, orange accent) with a teal
// secondary accent for the game screens.
#pragma once

#include <stdio.h>

#include "lvgl.h"

// Fonts generated with lv_font_conv (tools/gen_fonts.sh): Montserrat Medium (ASCII + Latin-1, so
// å/ä/ö work) plus the LVGL FontAwesome symbol set. my_font_14/18/24/36 are text-size roles.
#include "my_fonts.h"

namespace Theme {

inline int32_t statusControlSize()
{
    return LV_MAX(MyFonts::p4Pixels(32), my_font_18.line_height + 4);
}
inline int32_t batteryStatusWidth()
{
    lv_point_t text;
    lv_text_get_size(&text, "100%", &my_font_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return LV_MAX(MyFonts::p4Pixels(62), text.x + 12);
}
inline int32_t statusReservedWidth(bool share = false)
{
    return batteryStatusWidth() + (share ? 4 : 3) * (statusControlSize() + 6) + 8;
}
inline int32_t headerHeight()
{
    return LV_MAX(MyFonts::p4Pixels(44),
                  LV_MAX(statusControlSize(), my_font_24.line_height) + 12);
}
constexpr uint32_t TEXT        = 0xEDF2F8;
constexpr uint32_t ACCENT      = 0xFF9C3D;
constexpr uint32_t GREEN       = 0x62D28A;
constexpr uint32_t RED         = 0xFF7373;
constexpr uint32_t MUTED       = 0xA9B8CA;
inline lv_color_t c(uint32_t hex);

struct BatteryIndicator {
    bool percentValid;
    uint8_t percent;
};

using BatteryIndicatorReader = BatteryIndicator (*)();
struct StatusInfo {
    bool soundOn;
    bool wifiOn;
    bool wifiConnected;
    uint8_t wifiBars;
};
struct StatusActions {
    void (*openMenu)();
    void (*toggleSound)();
    void (*openSoundSettings)();
    void (*togglePowerSaving)();
    void (*openShare)();
};
using StatusInfoReader = StatusInfo (*)();

inline bool &statusShareAvailable()
{
    static bool available = false;
    return available;
}

inline void setStatusShareAvailable(bool available) { statusShareAvailable() = available; }

inline StatusActions &statusActions()
{
    static StatusActions actions{};
    return actions;
}

inline StatusInfoReader &statusInfoReader()
{
    static StatusInfoReader reader = nullptr;
    return reader;
}

inline void setStatusActions(StatusActions actions) { statusActions() = actions; }
inline void setStatusInfoReader(StatusInfoReader reader) { statusInfoReader() = reader; }

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
    auto *battery = static_cast<lv_obj_t *>(lv_timer_get_user_data(timer));
    if (!battery || !lv_obj_is_valid(battery) || lv_obj_get_child_count(battery) < 2) return;
    const BatteryIndicatorReader reader = batteryIndicatorReader();
    const BatteryIndicator state = reader ? reader() : BatteryIndicator{};
    auto *body = lv_obj_get_child(battery, 0);
    auto *terminal = lv_obj_get_child(battery, 1);
    if (!body || !terminal || lv_obj_get_child_count(body) == 0) return;
    auto *label = lv_obj_get_child(body, 0);
    const bool known = state.percentValid && state.percent <= 100;
    const bool critical = known && state.percent < 5;
    const bool blinkOff = critical && ((lv_tick_get() / 500) & 1);
    const uint32_t color = !known ? MUTED : state.percent > 30 ? GREEN : state.percent >= 10 ? ACCENT : RED;
    lv_obj_set_style_border_color(body, c(color), 0);
    lv_obj_set_style_bg_color(terminal, c(color), 0);
    lv_obj_set_style_bg_color(body, c(color), 0);
    lv_obj_set_style_bg_opa(body, blinkOff ? LV_OPA_40 : LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(body, blinkOff ? LV_OPA_40 : LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(terminal, blinkOff ? LV_OPA_40 : LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_opa(label, blinkOff ? LV_OPA_40 : LV_OPA_COVER, 0);
    if (known) lv_label_set_text_fmt(label, "%u%%", state.percent);
    else lv_label_set_text(label, "?");
}

inline void refreshStatusIndicators(lv_timer_t *timer)
{
    if (!timer) return;
    auto *header = static_cast<lv_obj_t *>(lv_timer_get_user_data(timer));
    if (!header || !lv_obj_is_valid(header) || lv_obj_get_child_count(header) < 6) return;
    const StatusInfoReader reader = statusInfoReader();
    const StatusInfo state = reader ? reader() : StatusInfo{};
    auto *speaker = lv_obj_get_child(header, 3);
    auto *wifi = lv_obj_get_child(header, 4);
    auto *share = lv_obj_get_child(header, 5);
    if (!speaker || !wifi) return;
    auto *speakerLabel = lv_obj_get_child(speaker, 0);
    if (speakerLabel)
        lv_obj_set_style_text_color(speakerLabel, c(state.soundOn ? TEXT : MUTED), 0);
    for (int i = 0; i < 5; ++i) {
        auto *arc = lv_obj_get_child(wifi, i);
        if (!arc) continue;
        const bool active = state.wifiConnected && i < state.wifiBars;
        lv_obj_set_style_arc_color(arc, c(active ? GREEN : MUTED), LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(arc, active || !state.wifiConnected ? LV_OPA_COVER : LV_OPA_30,
                                 LV_PART_INDICATOR);
    }
    auto *strike = lv_obj_get_child(wifi, 5);
    if (strike) {
        if (!state.wifiOn) lv_obj_clear_flag(strike, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(strike, LV_OBJ_FLAG_HIDDEN);
    }
    if (share) {
        if (statusShareAvailable()) lv_obj_clear_flag(share, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(share, LV_OBJ_FLAG_HIDDEN);
    }
}

// --- Palette (0xRRGGBB) ---
constexpr uint32_t BG          = 0x101B2B;   // Navy page background
constexpr uint32_t PANEL       = 0x17263A;
constexpr uint32_t PANEL_RAISED= 0x1D3048;
constexpr uint32_t LINE        = 0x34465D;
constexpr uint32_t ACCENT_DARK = 0x4A2E12;
constexpr uint32_t TEAL        = 0x00B4D8;   // Secondary accent (game)
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
    lv_obj_set_style_radius(o, MyFonts::p4Pixels(12), 0);
    lv_obj_set_style_pad_all(o, MyFonts::p4Pixels(8), 0);
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
    lv_obj_set_width(l, LV_PCT(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_center(l);
    return b;
}

inline lv_obj_t *menuCard(lv_obj_t *parent, int32_t width, const char *title, const char *description,
                          const void *image, const char *fallback, uint32_t accent, bool selected = false)
{
    const int32_t pad = MyFonts::p4Pixels(8);
    const int32_t icon = LV_MAX(MyFonts::p4Pixels(40), lv_font_get_line_height(&my_font_24) + pad);
    const int32_t titleHeight = lv_font_get_line_height(&my_font_24);
    const int32_t descriptionHeight = 2 * lv_font_get_line_height(&my_font_14);
    const int32_t gap = LV_MAX(2, lv_font_get_line_height(&my_font_14) / 5);
    const int32_t height = LV_MAX(icon + 2 * pad, 2 * pad + titleHeight + gap + descriptionHeight);
    lv_obj_t *card = lv_button_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, width, height);
    lv_obj_update_layout(parent);
    const int32_t cardWidth = lv_obj_get_width(card);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_bg_color(card, c(PANEL), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, selected ? 2 : 1, 0);
    lv_obj_set_style_border_color(card, c(selected ? ACCENT : LINE), 0);
    lv_obj_set_style_bg_color(card, c(PANEL_RAISED), LV_STATE_PRESSED);

    lv_obj_t *iconBox = lv_obj_create(card);
    lv_obj_remove_style_all(iconBox);
    lv_obj_set_size(iconBox, icon, icon);
    lv_obj_align(iconBox, LV_ALIGN_LEFT_MID, pad, 0);
    lv_obj_set_style_radius(iconBox, 8, 0);
    lv_obj_set_style_bg_color(iconBox, c(PANEL_RAISED), 0);
    lv_obj_set_style_bg_opa(iconBox, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(iconBox, 1, 0);
    lv_obj_set_style_border_color(iconBox, c(accent), 0);
    lv_obj_set_clickable(iconBox, false);
    if (image) {
        lv_obj_t *picture = lv_image_create(iconBox);
        lv_image_set_src(picture, image);
        lv_obj_set_size(picture, MyFonts::p4Pixels(40), MyFonts::p4Pixels(40));
        lv_image_set_inner_align(picture, LV_IMAGE_ALIGN_CONTAIN);
        lv_obj_center(picture);
        lv_obj_set_clickable(picture, false);
    } else {
        lv_obj_t *symbol = label(iconBox, fallback ? fallback : LV_SYMBOL_HOME, &my_font_24, accent);
        lv_obj_center(symbol);
        lv_obj_set_clickable(symbol, false);
    }

    const int32_t textX = 2 * pad + icon;
    const int32_t textWidth = LV_MAX(1, cardWidth - textX - pad);
    lv_obj_t *heading = label(card, title, &my_font_24);
    lv_obj_set_width(heading, textWidth);
    lv_obj_set_height(heading, titleHeight);
    lv_label_set_long_mode(heading, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(heading, textX, pad);
    lv_obj_t *detail = label(card, description ? description : "", &my_font_14, MUTED);
    lv_obj_set_width(detail, textWidth);
    lv_obj_set_height(detail, descriptionHeight);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_pos(detail, textX, pad + titleHeight + gap);
    lv_obj_set_clickable(heading, false);
    lv_obj_set_clickable(detail, false);
    return card;
}

// Header bar used by all app screens: title on the left, optional right-side widgets.
inline lv_obj_t *header(lv_obj_t *scr, const char *title, bool withStatusControls = true)
{
    lv_obj_t *h = lv_obj_create(scr);
    lv_obj_remove_style_all(h);
    const int32_t control = statusControlSize();
    const int32_t batteryWidth = batteryStatusWidth();
    const int32_t right = batteryWidth + 4;
    lv_obj_set_size(h, LV_PCT(100), headerHeight());
    lv_obj_set_style_bg_color(h, c(PANEL), 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(h, 2, 0);
    lv_obj_set_style_border_color(h, c(ACCENT), 0);
    lv_obj_set_scrollable(h, false);
    lv_obj_t *t = label(h, title, &my_font_24);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_width(t, withStatusControls ? LV_PCT(40) : LV_PCT(76));
    lv_obj_set_height(t, lv_font_get_line_height(&my_font_24));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *battery = lv_obj_create(h);
    lv_obj_remove_style_all(battery);
    lv_obj_set_size(battery, batteryWidth, control);
    lv_obj_add_flag(battery, LV_OBJ_FLAG_FLOATING);
    lv_obj_update_layout(h);
    lv_area_t headerArea;
    lv_obj_get_coords(h, &headerArea);
    lv_display_t *display = lv_display_get_default();
    const int32_t headerWidth = display ? lv_display_get_horizontal_resolution(display)
                                        : headerArea.x2 - headerArea.x1 + 1;
    lv_obj_set_pos(battery, headerWidth - right, 6);
    lv_obj_set_width(t, LV_MAX(1, headerWidth - (withStatusControls
        ? statusReservedWidth(statusShareAvailable()) : batteryWidth + 12) - 10));
    lv_obj_set_scrollable(battery, false);
    lv_obj_set_clickable(battery, false);

    lv_obj_t *body = lv_obj_create(battery);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, batteryWidth - 4, control - 6);
    lv_obj_set_pos(body, 0, 3);
    lv_obj_set_style_radius(body, 2, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(body, c(ACCENT), 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_clickable(body, false);
    lv_obj_t *terminal = lv_obj_create(battery);
    lv_obj_remove_style_all(terminal);
    lv_obj_set_size(terminal, 4, 10);
    lv_obj_set_pos(terminal, batteryWidth - 4, (control - 10) / 2);
    lv_obj_set_style_bg_color(terminal, c(ACCENT), 0);
    lv_obj_set_style_bg_opa(terminal, LV_OPA_COVER, 0);
    lv_obj_set_clickable(terminal, false);
    lv_obj_t *voltage = label(body, "?", &my_font_14, MUTED);
    lv_obj_set_size(voltage, batteryWidth - 8, lv_font_get_line_height(&my_font_14));
    lv_obj_set_style_text_align(voltage, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(voltage);
    lv_obj_set_clickable(voltage, false);
    lv_timer_t *timer = lv_timer_create(refreshBatteryIndicator, 500, battery);
    refreshBatteryIndicator(timer);
    if (timer) {
        lv_obj_add_event_cb(h, [](lv_event_t *e) {
            if (lv_event_get_target(e) == lv_event_get_current_target(e))
                lv_timer_delete(static_cast<lv_timer_t *>(lv_event_get_user_data(e)));
        }, LV_EVENT_DELETE, timer);
    }
    if (withStatusControls) {
        lv_obj_t *menu = button(h, LV_SYMBOL_LIST, control, control, LINE);
        lv_obj_add_flag(menu, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_pos(menu, headerWidth - right - 3 * (control + 6), 6);
        lv_obj_add_event_cb(menu, [](lv_event_t *) {
            if (statusActions().openMenu) statusActions().openMenu();
        }, LV_EVENT_CLICKED, nullptr);

        static bool speakerLongPressed = false;
        lv_obj_t *speaker = button(h, LV_SYMBOL_AUDIO, control, control, LINE);
        lv_obj_add_flag(speaker, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_pos(speaker, headerWidth - right - 2 * (control + 6), 6);
        lv_obj_add_event_cb(speaker, [](lv_event_t *) { speakerLongPressed = false; },
                            LV_EVENT_PRESSED, nullptr);
        lv_obj_add_event_cb(speaker, [](lv_event_t *) {
            if (speakerLongPressed) {
                speakerLongPressed = false;
                return;
            }
            if (statusActions().toggleSound) statusActions().toggleSound();
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(speaker, [](lv_event_t *) {
            speakerLongPressed = true;
            if (statusActions().openSoundSettings) statusActions().openSoundSettings();
        }, LV_EVENT_LONG_PRESSED, nullptr);

        lv_obj_t *wifi = lv_obj_create(h);
        lv_obj_remove_style_all(wifi);
        lv_obj_set_size(wifi, control, control);
        lv_obj_add_flag(wifi, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_pos(wifi, headerWidth - right - control - 6, 6);
        lv_obj_set_scrollable(wifi, false);
        lv_obj_set_clickable(wifi, false);
        for (int i = 0; i < 5; ++i) {
            lv_obj_t *arc = lv_arc_create(wifi);
            lv_obj_remove_style_all(arc);
            const int32_t diameter = (8 + 6 * i) * control / 32;
            lv_obj_set_size(arc, diameter, diameter);
            lv_obj_align(arc, LV_ALIGN_BOTTOM_MID, 0, -1);
            lv_arc_set_bg_angles(arc, 225, 315);
            lv_arc_set_angles(arc, 225, 315);
            lv_obj_set_style_arc_width(arc, 2, LV_PART_INDICATOR);
            lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
            lv_obj_set_style_arc_opa(arc, LV_OPA_30, LV_PART_INDICATOR);
            lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
            lv_obj_set_clickable(arc, false);
        }
        lv_obj_t *strike = lv_obj_create(wifi);
        lv_obj_remove_style_all(strike);
        lv_obj_set_size(strike, 3, 25);
        lv_obj_set_pos(strike, 14, 2);
        lv_obj_set_style_radius(strike, 2, 0);
        lv_obj_set_style_bg_color(strike, c(RED), 0);
        lv_obj_set_style_bg_opa(strike, LV_OPA_COVER, 0);
        lv_obj_set_style_transform_rotation(strike, 450, 0);
        lv_obj_set_clickable(strike, false);
        lv_obj_add_flag(strike, LV_OBJ_FLAG_HIDDEN);

        lv_obj_add_event_cb(battery, [](lv_event_t *) {
            if (statusActions().togglePowerSaving) statusActions().togglePowerSaving();
        }, LV_EVENT_LONG_PRESSED, nullptr);
        lv_obj_set_clickable(battery, true);

        lv_timer_t *statusTimer = lv_timer_create(refreshStatusIndicators, 1000, h);
        refreshStatusIndicators(statusTimer);
        if (statusTimer) {
            lv_obj_add_event_cb(h, [](lv_event_t *e) {
                if (lv_event_get_target(e) == lv_event_get_current_target(e))
                    lv_timer_delete(static_cast<lv_timer_t *>(lv_event_get_user_data(e)));
            }, LV_EVENT_DELETE, statusTimer);

            lv_obj_t *share = button(h, "", control, control, TEAL);
            lv_obj_add_flag(share, LV_OBJ_FLAG_FLOATING);
            lv_obj_add_flag(share, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(share, headerWidth - right - 4 * (control + 6), 6);
            lv_obj_clean(share);
            auto node = [share](int32_t x, int32_t y) {
                lv_obj_t *dot = lv_obj_create(share);
                lv_obj_remove_style_all(dot);
                lv_obj_set_size(dot, 8, 8);
                lv_obj_set_pos(dot, x, y);
                lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
                lv_obj_set_style_bg_color(dot, c(TEXT), 0);
                lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
                lv_obj_set_clickable(dot, false);
            };
            auto connector = [share](int32_t x, int32_t y, int32_t angle) {
                lv_obj_t *bar = lv_obj_create(share);
                lv_obj_remove_style_all(bar);
                lv_obj_set_size(bar, 16, 2);
                lv_obj_set_pos(bar, x, y);
                lv_obj_set_style_bg_color(bar, c(TEXT), 0);
                lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
                lv_obj_set_style_transform_rotation(bar, angle, 0);
                lv_obj_set_clickable(bar, false);
            };
            connector(8, 10, 330);
            connector(8, 20, 300);
            node(4, 12);
            node(20, 4);
            node(20, 20);
            lv_obj_add_event_cb(share, [](lv_event_t *) {
                if (statusActions().openShare) statusActions().openShare();
            }, LV_EVENT_CLICKED, nullptr);
            refreshStatusIndicators(statusTimer);
        }
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
