// ui_theme.h - shared colours, fonts and small style helpers for the on-device LVGL UI.
// Palette follows the MyPi-Hotspot dark web theme (navy panels, orange accent) with a teal
// secondary accent for the game screens.
#pragma once

#include <lvgl.h>

// Fonts generated with lv_font_conv: Montserrat Medium (ASCII + Latin-1, so å/ä/ö work)
// plus the LVGL FontAwesome symbol set (LV_SYMBOL_*).
LV_FONT_DECLARE(otr_font_14);
LV_FONT_DECLARE(otr_font_18);
LV_FONT_DECLARE(otr_font_24);
LV_FONT_DECLARE(otr_font_36);

namespace Theme {

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

// Plain screen with the theme background.
inline lv_obj_t *screen()
{
    lv_obj_t *s = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s, c(BG), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s, c(TEXT), 0);
    lv_obj_set_style_text_font(s, &otr_font_18, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    return s;
}

// Rounded card (panel colour, thin line border), like the web UI cards.
inline lv_obj_t *card(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
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

inline lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font = &otr_font_18,
                       uint32_t color = TEXT)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, c(color), 0);
    return l;
}

// Button with a coloured border; filled with the colour while pressed / when `filled`.
inline lv_obj_t *button(lv_obj_t *parent, const char *text, lv_coord_t w, lv_coord_t h,
                        uint32_t color = ACCENT, bool filled = false)
{
    lv_obj_t *b = lv_btn_create(parent);
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
    // Encoder focus ring
    lv_obj_set_style_outline_width(b, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(b, c(TEAL), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(b, 2, LV_STATE_FOCUS_KEY);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &otr_font_18, 0);
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
    lv_obj_clear_flag(h, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = label(h, title, &otr_font_24);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 10, 0);
    return h;
}

// One focus group per screen for the optional encoder. Deletes the previous screen's
// group and binds the new one to the encoder input device (no-op without encoder).
inline lv_group_t *newScreenGroup(void (*bind)(lv_group_t *))
{
    static lv_group_t *s_group = nullptr;
    if (s_group) lv_group_del(s_group);
    s_group = lv_group_create();
    if (bind) bind(s_group);
    return s_group;
}

// Loads a screen with a short fade and deletes the previous one.
inline void loadScreen(lv_obj_t *scr)
{
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, 180, 0, true);
}

}  // namespace Theme
