// Portrait-only adventure helpers, also exercised by the host LVGL test.
#pragma once
#include "my_theme.h"
#include "lang/lang.h"
#include "player_db.h"
#include "ui_metrics.h"

namespace AdvUi {
inline int32_t scale(int32_t value) { return value * UiMetrics::marginNormal() / 10; }

inline int32_t rowWidth() {
    return UiMetrics::screenW() - 4 * scale(6);
}

inline lv_obj_t *scrollColumn(lv_obj_t *parent, int32_t top, int32_t bottom, int32_t rowGap) {
    const int32_t inset = scale(6);
    auto *list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, inset, scale(top));
    lv_obj_set_size(list, UiMetrics::screenW() - 2 * inset,
                    UiMetrics::screenH() - scale(top) - scale(bottom));
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, scale(rowGap), 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    return list;
}

inline void bottomButton(lv_obj_t *button, int32_t height, int32_t sideInset, int32_t bottomInset) {
    const int32_t margin = scale(sideInset);
    const int32_t bottom = scale(bottomInset);
    const int32_t buttonHeight = scale(height);
    lv_obj_set_size(button, UiMetrics::screenW() - 2 * margin, buttonHeight);
    lv_obj_set_pos(button, margin, UiMetrics::screenH() - bottom - buttonHeight);
}

inline int32_t dialogWidth() {
    const int32_t inset = scale(12);
    const int32_t width = UiMetrics::screenW() - 2 * inset;
    const int32_t maxWidth = UiMetrics::buttonWMax() - scale(4);
    return width < maxWidth ? width : maxWidth;
}

inline void dialogLayout(lv_obj_t *mb) {
    auto *display = lv_obj_get_display(mb);
    const int32_t inset = scale(12);
    lv_obj_set_style_max_width(mb, dialogWidth(), 0);
    lv_obj_set_style_max_height(mb, lv_display_get_vertical_resolution(display) - 2 * inset, 0);
    auto *content = lv_msgbox_get_content(mb);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    Theme::msgboxFonts(mb);
    lv_obj_center(mb);
}

inline void resultLayout(lv_obj_t *mb) {
    dialogLayout(mb);
    lv_obj_set_width(mb, dialogWidth());
    auto *footer = lv_msgbox_get_footer(mb);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_COLUMN);
    const int32_t pad = scale(10);
    const int32_t gap = scale(8);
    const int32_t buttonHeight = scale(48);
    lv_obj_set_style_pad_all(footer, pad, 0);
    lv_obj_set_style_pad_row(footer, gap, 0);
    const int count = lv_obj_get_child_count(footer);
    lv_obj_set_height(footer, 2 * pad + count * buttonHeight + (count - 1) * gap);
    for (int i = 0; i < count; ++i) {
        auto *button = lv_obj_get_child(footer, i);
        lv_obj_set_size(button, lv_pct(100), buttonHeight);
    }
    Theme::msgboxFonts(mb);
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
    const int w = UiMetrics::screenW();
    const int h = UiMetrics::screenH();
    const int inset = scale(12);
    const int titleY = scale(12);
    const int inputY = scale(44);
    const int inputH = scale(48);
    const int errorY = scale(100);
    const int errorH = scale(68);
    const int buttonY = scale(178);
    const int buttonH = scale(48);
    const int keyboardY = scale(240);
    lv_obj_set_size(f.root, w, h);
    lv_obj_set_style_bg_color(f.root, Theme::c(Theme::BG), 0);
    lv_obj_set_style_bg_opa(f.root, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(f.root, false);
    auto *title = Theme::label(f.root, tr(Str::PICK_NEW), &my_font_18);
    lv_obj_set_pos(title, inset, titleY);
    f.input = lv_textarea_create(f.root);
    lv_obj_set_pos(f.input, inset, inputY);
    lv_obj_set_size(f.input, w - 2 * inset, inputH);
    lv_textarea_set_one_line(f.input, true);
    lv_textarea_set_max_length(f.input, PLAYER_NAME_CAP - 1);
    lv_obj_set_style_text_font(f.input, &my_font_18, 0);
    f.error = Theme::label(f.root, "", &my_font_14, Theme::RED);
    lv_obj_set_pos(f.error, inset, errorY);
    lv_obj_set_size(f.error, w - 2 * inset, errorH);
    lv_label_set_long_mode(f.error, LV_LABEL_LONG_MODE_WRAP);
    const int bw = (w - 3 * inset) / 2;
    auto *saveBtn = Theme::button(f.root, tr(Str::BTN_SAVE), bw, buttonH, Theme::GREEN);
    auto *cancelBtn = Theme::button(f.root, tr(Str::BTN_CANCEL), bw, buttonH, Theme::LINE);
    lv_obj_set_pos(saveBtn, inset, buttonY);
    lv_obj_set_pos(cancelBtn, 2 * inset + bw, buttonY);
    lv_obj_add_event_cb(saveBtn, save, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(cancelBtn, cancel, LV_EVENT_CLICKED, nullptr);
    f.keyboard = lv_keyboard_create(f.root);
    lv_obj_set_align(f.keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(f.keyboard, 0, keyboardY);
    lv_obj_set_size(f.keyboard, w, h - keyboardY);
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
