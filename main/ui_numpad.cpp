// ui_numpad.cpp - see header.
#include "ui_numpad.h"

#include <stdlib.h>
#include <string.h>

#include "lang.h"
#include "my_theme.h"
#include "player_db.h"
#include "ui_metrics.h"

namespace UiNumpad {

struct Pad {
    DoneCb cb;
    void *user;
    char pin[PLAYER_PIN_CAP];
    lv_obj_t *root;
    lv_obj_t *cells[PLAYER_PIN_LEN];
    lv_obj_t *hint;
    lv_obj_t *ok;
    bool closing;
};

static const char *const KEYS[] = {"1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n",
                                   LV_SYMBOL_BACKSPACE, "0", " ", ""};

static void refresh(Pad *p)
{
    const size_t n = strlen(p->pin);
    for (int i = 0; i < PLAYER_PIN_LEN; i++) {
        lv_obj_t *cell = p->cells[i];
        lv_label_set_text(lv_obj_get_child(cell, 0), (size_t)i < n ? "\xE2\x80\xA2" : "");   // U+2022 bullet
        lv_obj_set_style_border_color(cell, Theme::c((size_t)i == n ? Theme::ACCENT : Theme::LINE), 0);
    }
    if (n == PLAYER_PIN_LEN) lv_obj_remove_state(p->ok, LV_STATE_DISABLED);
    else lv_obj_add_state(p->ok, LV_STATE_DISABLED);
}

static void finish(Pad *p, bool ok)
{
    if (p->closing || (ok && strlen(p->pin) != PLAYER_PIN_LEN)) return;
    char pin[PLAYER_PIN_CAP];
    memcpy(pin, ok ? p->pin : "", ok ? sizeof(pin) : 1);
    if (!p->cb || p->cb(pin, ok, p->user)) close(p->root);
}

static void keyEvent(lv_event_t *e)
{
    Pad *p = (Pad *)lv_event_get_user_data(e);
    lv_obj_t *m = lv_event_get_target_obj(e);
    const char *t = lv_buttonmatrix_get_button_text(m, lv_buttonmatrix_get_selected_button(m));
    if (!t) return;
    size_t n = strlen(p->pin);
    if (strcmp(t, LV_SYMBOL_BACKSPACE) == 0) {
        if (n) p->pin[n - 1] = 0;
    } else if (t[0] >= '0' && t[0] <= '9' && n < PLAYER_PIN_LEN) {
        p->pin[n] = t[0];
        p->pin[n + 1] = 0;
    }
    lv_label_set_text(p->hint, "");
    refresh(p);
}

lv_obj_t *open(const char *title, const char *info, DoneCb cb, void *user, const char *cancelText)
{
    Pad *p = (Pad *)calloc(1, sizeof(Pad));
    if (!p) return nullptr;
    p->cb = cb;
    p->user = user;

    lv_obj_t *root = lv_obj_create(lv_layer_top());
    p->root = root;
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, Theme::c(Theme::BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_clickable(root, true);   // Swallow touches meant for the screen below
    lv_obj_set_user_data(root, p);
    lv_obj_add_event_cb(root, [](lv_event_t *e) { free(lv_event_get_user_data(e)); }, LV_EVENT_DELETE, p);

    const int32_t gap = 2 * UiMetrics::marginSmall();
    const int32_t width = LV_MIN(UiMetrics::screenW() - 2 * UiMetrics::marginNormal(), UiMetrics::buttonWMax());
    const int32_t cellW = UiMetrics::buttonWMin();
    const int32_t cellH = LV_MIN(UiMetrics::buttonH() + UiMetrics::marginLarge(), UiMetrics::screenH() / 8);
    const int32_t buttonH = LV_MIN(UiMetrics::buttonH() + gap, UiMetrics::screenH() / 9);
    const int32_t buttonY = UiMetrics::screenH() - gap - buttonH;
    lv_obj_t *t = Theme::label(root, title, &my_font_24);
    lv_obj_set_width(t, width);
    lv_obj_set_height(t, lv_font_get_line_height(&my_font_24));
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 3 * UiMetrics::marginSmall());
    lv_obj_update_layout(t);
    int32_t cellY = lv_obj_get_y(t) + lv_obj_get_height(t) + UiMetrics::marginSmall();
    if (info && *info) {
        lv_obj_t *i = Theme::label(root, info, &my_font_14, Theme::MUTED);
        lv_obj_set_width(i, width);
        lv_label_set_long_mode(i, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(i, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(i, LV_ALIGN_TOP_MID, 0, cellY);
        lv_obj_update_layout(i);
        lv_obj_set_height(i, LV_MIN(lv_obj_get_height(i), 2 * lv_font_get_line_height(&my_font_14)));
        lv_label_set_long_mode(i, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_update_layout(i);
        cellY += lv_obj_get_height(i) + UiMetrics::controlSpacing();
    }
    cellY = LV_MAX(cellY, 2 * LV_MIN(UiMetrics::buttonH(), UiMetrics::screenH() / 11));

    for (int k = 0; k < PLAYER_PIN_LEN; k++) {
        lv_obj_t *cell = lv_obj_create(root);
        lv_obj_remove_style_all(cell);
        lv_obj_set_size(cell, cellW, cellH);
        lv_obj_set_pos(cell, (UiMetrics::screenW() - PLAYER_PIN_LEN * cellW - (PLAYER_PIN_LEN - 1) * gap) / 2 +
                            k * (cellW + gap), cellY);
        lv_obj_set_style_radius(cell, 10, 0);
        lv_obj_set_style_bg_color(cell, Theme::c(Theme::PANEL), 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(cell, 2, 0);
        lv_obj_set_scrollable(cell, false);
        lv_obj_t *dot = Theme::label(cell, "", &my_font_24);
        lv_obj_center(dot);
        p->cells[k] = cell;
    }
    p->hint = Theme::label(root, "", &my_font_14, Theme::RED);
    lv_obj_set_width(p->hint, width);
    lv_obj_set_height(p->hint, lv_font_get_line_height(&my_font_14));
    lv_label_set_long_mode(p->hint, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(p->hint, LV_TEXT_ALIGN_CENTER, 0);
    const int32_t hintY = cellY + cellH + UiMetrics::controlSpacing();
    lv_obj_align(p->hint, LV_ALIGN_TOP_MID, 0, hintY);

    const int32_t keysY = hintY + lv_font_get_line_height(&my_font_14) + UiMetrics::marginSmall();
    lv_obj_t *m = lv_buttonmatrix_create(root);
    lv_buttonmatrix_set_map(m, KEYS);
    lv_buttonmatrix_set_button_ctrl(m, 11, LV_BUTTONMATRIX_CTRL_HIDDEN);   // Empty cell right of "0"
    lv_obj_set_size(m, width, buttonY - gap - keysY);
    lv_obj_align(m, LV_ALIGN_TOP_MID, 0, keysY);
    lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m, 0, 0);
    lv_obj_set_style_pad_all(m, 0, 0);
    lv_obj_set_style_pad_gap(m, gap, 0);
    lv_obj_set_style_bg_color(m, Theme::c(Theme::PANEL_RAISED), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(m, Theme::c(Theme::ACCENT_DARK), (lv_style_selector_t)LV_PART_ITEMS | (lv_style_selector_t)LV_STATE_PRESSED);
    lv_obj_set_style_border_color(m, Theme::c(Theme::LINE), LV_PART_ITEMS);
    lv_obj_set_style_border_width(m, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(m, 12, LV_PART_ITEMS);
    lv_obj_set_style_text_color(m, Theme::c(Theme::TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(m, &my_font_24, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
    lv_obj_set_scrollable(m, false);
    lv_obj_add_event_cb(m, keyEvent, LV_EVENT_VALUE_CHANGED, p);

    lv_obj_t *cancel = Theme::button(root, cancelText ? cancelText : tr(Str::BTN_CANCEL), (width - gap) / 2, buttonH, Theme::MUTED);
    lv_obj_set_pos(cancel, (UiMetrics::screenW() - width) / 2, buttonY);
    lv_obj_add_event_cb(cancel, [](lv_event_t *e) { finish((Pad *)lv_event_get_user_data(e), false); },
                        LV_EVENT_CLICKED, p);
    p->ok = Theme::button(root, LV_SYMBOL_OK " OK", (width - gap) / 2, buttonH, Theme::ACCENT, true);
    lv_obj_set_pos(p->ok, (UiMetrics::screenW() + gap) / 2, buttonY);
    lv_obj_add_event_cb(p->ok, [](lv_event_t *e) { finish((Pad *)lv_event_get_user_data(e), true); },
                        LV_EVENT_CLICKED, p);
    refresh(p);
    return root;
}

void setHint(lv_obj_t *pad, const char *text, bool error)
{
    if (!pad) return;
    Pad *p = (Pad *)lv_obj_get_user_data(pad);
    if (!p) return;
    lv_label_set_text(p->hint, text ? text : "");
    lv_obj_set_style_text_color(p->hint, Theme::c(error ? Theme::RED : Theme::MUTED), 0);
    if (error) {   // Start over after a rejected PIN
        p->pin[0] = 0;
        refresh(p);
    }
}

void close(lv_obj_t *pad)
{
    if (!pad) return;
    Pad *p = (Pad *)lv_obj_get_user_data(pad);
    if (p) {
        if (p->closing) return;
        p->closing = true;
    }
    lv_obj_delete_async(pad);
}

}  // namespace UiNumpad
