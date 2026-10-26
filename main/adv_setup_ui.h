// Portrait-only adventure helpers, also exercised by the host LVGL test.
#pragma once
#include "my_theme.h"
#include "lang/lang.h"
#include "player_db.h"

namespace AdvUi {
inline void resultLayout(lv_obj_t *mb) {
    auto *display = lv_obj_get_display(mb);
    lv_obj_set_width(mb, lv_display_get_horizontal_resolution(display) - 24);
    auto *footer = lv_msgbox_get_footer(mb);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(footer, 10, 0);
    lv_obj_set_style_pad_row(footer, 8, 0);
    const int count = lv_obj_get_child_count(footer);
    lv_obj_set_height(footer, 20 + count * 48 + (count - 1) * 8);
    for (int i = 0; i < count; ++i) {
        auto *button = lv_obj_get_child(footer, i);
        lv_obj_set_size(button, lv_pct(100), 48);
    }
    Theme::msgboxFonts(mb);
    lv_obj_set_style_max_height(mb, lv_display_get_vertical_resolution(display) - 24, 0);
    lv_obj_center(mb);
}

struct NameForm {
    lv_obj_t *root = nullptr, *input = nullptr, *error = nullptr, *keyboard = nullptr;
    uint32_t keyTap = 0;
};
inline void nameForm(NameForm &f, lv_obj_t *parent, lv_event_cb_t save, lv_event_cb_t cancel) {
    f = {};
    f.root = lv_obj_create(parent);
    lv_obj_remove_style_all(f.root);
    auto *display = lv_obj_get_display(parent);
    const int w = lv_display_get_horizontal_resolution(display);
    const int h = lv_display_get_vertical_resolution(display);
    lv_obj_set_size(f.root, w, h);
    lv_obj_set_style_bg_color(f.root, Theme::c(Theme::BG), 0);
    lv_obj_set_style_bg_opa(f.root, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(f.root, false);
    auto *title = Theme::label(f.root, tr(Str::PICK_NEW), &my_font_18);
    lv_obj_set_pos(title, 12, 12);
    f.input = lv_textarea_create(f.root);
    lv_obj_set_pos(f.input, 12, 44);
    lv_obj_set_size(f.input, w - 24, 48);
    lv_textarea_set_one_line(f.input, true);
    lv_textarea_set_max_length(f.input, PLAYER_NAME_CAP - 1);
    lv_obj_set_style_text_font(f.input, &my_font_18, 0);
    f.error = Theme::label(f.root, "", &my_font_14, Theme::RED);
    lv_obj_set_pos(f.error, 12, 100);
    lv_obj_set_size(f.error, w - 24, 68);
    lv_label_set_long_mode(f.error, LV_LABEL_LONG_MODE_WRAP);
    const int bw = (w - 36) / 2;
    auto *saveBtn = Theme::button(f.root, tr(Str::BTN_SAVE), bw, 48, Theme::GREEN);
    auto *cancelBtn = Theme::button(f.root, tr(Str::BTN_CANCEL), bw, 48, Theme::LINE);
    lv_obj_set_pos(saveBtn, 12, 178);
    lv_obj_set_pos(cancelBtn, 24 + bw, 178);
    lv_obj_add_event_cb(saveBtn, save, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(cancelBtn, cancel, LV_EVENT_CLICKED, nullptr);
    f.keyboard = lv_keyboard_create(f.root);
    lv_obj_set_align(f.keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(f.keyboard, 0, 240);
    lv_obj_set_size(f.keyboard, w, h - 240);
    lv_obj_set_style_text_font(f.keyboard, &my_font_18, 0);
    lv_keyboard_set_textarea(f.keyboard, f.input);
    // Keep LVGL's character/keyboard-mode handling but guard physical duplicate key events.
    lv_obj_remove_event_cb(f.keyboard, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(f.keyboard, [](lv_event_t *e) {
        auto *form = static_cast<NameForm *>(lv_event_get_user_data(e));
        if (Theme::debounce(form->keyTap, 120)) lv_keyboard_def_event_cb(e);
    }, LV_EVENT_VALUE_CHANGED, &f);
    lv_obj_add_event_cb(f.keyboard, save, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(f.keyboard, cancel, LV_EVENT_CANCEL, nullptr);
}
}
