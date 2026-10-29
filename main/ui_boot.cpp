// ui_boot.cpp - boot / hardware self-test screen.
#include "ui_boot.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "board_display.h"
#include "board_pins.h"
#include "lang.h"
#include "my_theme.h"

namespace UiBoot {

static const char *itemName(int i)
{
    switch ((Item)i) {
    case Item::DISP: return LCD_MODEL;
    case Item::TOUCH: return TOUCH_MODEL;
    case Item::PSRAM: return "PSRAM";
    case Item::I2C: return tr(Str::BOOT_I2C);
    case Item::RTC: return tr(Str::BOOT_CLOCK);
    case Item::STORAGE: return tr(Str::BOOT_STORAGE);
    case Item::WIFI: return "Wi-Fi (STA)";
    case Item::WEB: return tr(Str::BOOT_WEB);
    default: return "?";
    }
}

static constexpr int32_t ROW_H = 33;
static lv_obj_t *s_screen = nullptr;
static lv_obj_t *s_bar = nullptr;
static lv_obj_t *s_status = nullptr;
static lv_obj_t *s_icon[(int)Item::COUNT];
static lv_obj_t *s_detail[(int)Item::COUNT];
static State s_state[(int)Item::COUNT];
static char s_text[(int)Item::COUNT][72];   // Last detail text (kept for the web overview)

void show()
{
    DisplayLock lock;
    s_screen = Theme::screen();

    // Title with a small die logo
    lv_obj_t *logo = lv_obj_create(s_screen);
    lv_obj_remove_style_all(logo);
    lv_obj_set_size(logo, 64, 64);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_set_style_bg_color(logo, Theme::c(Theme::ACCENT), 0);
    lv_obj_set_style_bg_opa(logo, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(logo, 14, 0);
    lv_obj_set_style_shadow_width(logo, 24, 0);
    lv_obj_set_style_shadow_color(logo, Theme::c(Theme::ACCENT), 0);
    lv_obj_set_style_shadow_opa(logo, LV_OPA_40, 0);
    static const int32_t pips[5][2] = {{-16, -16}, {16, -16}, {0, 0}, {-16, 16}, {16, 16}};
    for (auto &p : pips) {
        lv_obj_t *d = lv_obj_create(logo);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 11, 11);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, Theme::c(Theme::BG), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_align(d, LV_ALIGN_CENTER, p[0], p[1]);
    }

    lv_obj_t *title = Theme::label(s_screen, "OnTheRoad", &my_font_36);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 82);
    lv_obj_t *ver = Theme::label(s_screen, FW_NAME " v" FW_VERSION, &my_font_14, Theme::MUTED);
    lv_obj_align(ver, LV_ALIGN_TOP_MID, 0, 122);

    s_bar = lv_bar_create(s_screen);
    lv_obj_set_size(s_bar, 260, 10);
    lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, 146);
    lv_bar_set_range(s_bar, 0, 100);
    lv_obj_set_style_bg_color(s_bar, Theme::c(Theme::PANEL_RAISED), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, Theme::c(Theme::TEAL), LV_PART_INDICATOR);
    lv_obj_set_style_anim_duration(s_bar, 250, 0);

    lv_obj_t *list = Theme::card(s_screen, 296, (int)Item::COUNT * ROW_H + 18);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 164);
    lv_obj_set_scrollable(list, false);
    for (int i = 0; i < (int)Item::COUNT; i++) {
        int32_t y = i * ROW_H;
        s_icon[i] = Theme::label(list, LV_SYMBOL_BULLET, &my_font_18, Theme::LINE);
        lv_obj_set_pos(s_icon[i], 2, y + 6);
        lv_obj_t *name = Theme::label(list, itemName(i), &my_font_14);
        lv_obj_set_pos(name, 30, y);
        s_detail[i] = Theme::label(list, tr(Str::BOOT_WAITING), &my_font_14, Theme::MUTED);
        lv_label_set_long_mode(s_detail[i], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(s_detail[i], 240);
        lv_obj_set_pos(s_detail[i], 30, y + 17);
        s_state[i] = State::PENDING;
    }

    s_status = Theme::label(s_screen, tr(Str::BOOT_STARTING), &my_font_14, Theme::MUTED);
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_screen_load(s_screen);
}

void set(Item item, State state, const char *detail)
{
    int i = (int)item;
    if (i < 0 || i >= (int)Item::COUNT) return;
    s_state[i] = state;
    snprintf(s_text[i], sizeof(s_text[i]), "%s", detail ? detail : "");
    if (!s_screen) return;
    DisplayLock lock;
    const char *sym = LV_SYMBOL_BULLET;
    uint32_t col = Theme::LINE;
    const char *def = tr(Str::BOOT_WAITING);
    switch (state) {
    case State::RUNNING: sym = LV_SYMBOL_REFRESH; col = Theme::TEAL; def = tr(Str::BOOT_TESTING); break;
    case State::OK: sym = LV_SYMBOL_OK; col = Theme::GREEN; def = "OK"; break;
    case State::FAIL: sym = LV_SYMBOL_CLOSE; col = Theme::RED; def = tr(Str::BOOT_FAIL); break;
    case State::SKIP: sym = LV_SYMBOL_MINUS; col = Theme::MUTED; def = tr(Str::BOOT_SKIPPED); break;
    case State::WAITING: sym = LV_SYMBOL_REFRESH; col = Theme::ACCENT; def = tr(Str::BOOT_WAITING); break;
    default: break;
    }
    lv_label_set_text(s_icon[i], sym);
    lv_obj_set_style_text_color(s_icon[i], Theme::c(col), 0);
    lv_label_set_text(s_detail[i], detail && detail[0] ? detail : def);
    lv_obj_set_style_text_color(s_detail[i], Theme::c(state == State::FAIL ? Theme::RED : Theme::MUTED), 0);
}

void setProgress(int percent)
{
    if (!s_bar) return;
    DisplayLock lock;
    lv_bar_set_value(s_bar, LV_CLAMP(0, percent, 100), LV_ANIM_ON);
}

void setStatus(const char *text)
{
    if (!s_status) return;
    DisplayLock lock;
    lv_label_set_text(s_status, text);
}

const char *name(Item item) { return itemName((int)item); }
State state(Item item) { return (int)item >= 0 && item < Item::COUNT ? s_state[(int)item] : State::PENDING; }
const char *detail(Item item) { return (int)item >= 0 && item < Item::COUNT ? s_text[(int)item] : ""; }

int failures()
{
    int n = 0;
    for (int i = 0; i < (int)Item::COUNT; i++) n += s_state[i] == State::FAIL;
    return n;
}

}  // namespace UiBoot
