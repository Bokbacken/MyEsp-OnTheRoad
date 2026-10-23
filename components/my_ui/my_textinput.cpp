// my_textinput.cpp - QWERTY overlay + dispatcher between CharWheel and Qwerty.
#include "my_textinput.h"

#include <stdio.h>
#include <string.h>

#include "my_theme.h"

namespace TextInput {

namespace {

constexpr uint32_t MAGIC_QWERTY = 0x51575231;   // "QWR1"
Mode g_default = Mode::AUTO;

struct Qwerty {
    uint32_t magic;   // Must stay first (shared layout check with CharWheel)
    bool closing;
    Callback cb;
    void *user;
    lv_obj_t *root, *ta, *kb, *hint;
};

// Swedish layout. Special keys are handled by lv_keyboard itself: "ABC"/"abc"/"1#" switch
// modes, LV_SYMBOL_BACKSPACE deletes, LV_SYMBOL_KEYBOARD = cancel, LV_SYMBOL_OK = ready.
const char *const MAP_LOWER[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\xC3\xA5", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", "\xC3\xB6", "\xC3\xA4", "\n",
    "1#", "z", "x", "c", "v", "b", "n", "m", ",", ".", "-", "_", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};
const char *const MAP_UPPER[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\xC3\x85", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", "\xC3\x96", "\xC3\x84", "\n",
    "1#", "Z", "X", "C", "V", "B", "N", "M", ",", ".", "-", "_", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};

constexpr lv_buttonmatrix_ctrl_t K = (lv_buttonmatrix_ctrl_t)(LV_BUTTONMATRIX_CTRL_POPOVER | 2);
constexpr lv_buttonmatrix_ctrl_t S = (lv_buttonmatrix_ctrl_t)(LV_BUTTONMATRIX_CTRL_CHECKED | 3);
const lv_buttonmatrix_ctrl_t CTRL[] = {
    K, K, K, K, K, K, K, K, K, K, K, S,
    S, K, K, K, K, K, K, K, K, K, K, K,
    S, K, K, K, K, K, K, K, K, K, K, K,
    S, (lv_buttonmatrix_ctrl_t)3, (lv_buttonmatrix_ctrl_t)12, (lv_buttonmatrix_ctrl_t)3,
    (lv_buttonmatrix_ctrl_t)(LV_BUTTONMATRIX_CTRL_CHECKED | 4)};

uint32_t magicOf(lv_obj_t *o)
{
    const uint32_t *p = o ? (const uint32_t *)lv_obj_get_user_data(o) : nullptr;
    return p ? *p : 0;
}

Qwerty *getQ(lv_obj_t *o) { return magicOf(o) == MAGIC_QWERTY ? (Qwerty *)lv_obj_get_user_data(o) : nullptr; }

void notify(Qwerty *q, bool confirmed)
{
    if (!q->cb) return;
    char copy[CharWheel::TEXT_CAP];
    snprintf(copy, sizeof(copy), "%s", lv_textarea_get_text(q->ta));
    q->cb(copy, confirmed, q->user);
    memset(copy, 0, sizeof(copy));
}

void kbEvent(lv_event_t *e)
{
    Qwerty *q = (Qwerty *)lv_event_get_user_data(e);
    if (q->closing) return;
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        notify(q, true);
    } else if (code == LV_EVENT_CANCEL) {
        notify(q, false);
        close(q->root);
    } else if (code == LV_EVENT_VALUE_CHANGED) {
        lv_label_set_text(q->hint, "");
    }
}

void deleteEvent(lv_event_t *e)
{
    Qwerty *q = (Qwerty *)lv_event_get_user_data(e);
    memset(q, 0, sizeof(*q));
    lv_free(q);
}

lv_obj_t *openQwerty(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, const char *label,
                     const char *initial, size_t maxLen, bool mask, Callback cb, void *user)
{
    Qwerty *q = (Qwerty *)lv_malloc(sizeof(Qwerty));
    if (!q) return nullptr;
    memset(q, 0, sizeof(*q));
    q->magic = MAGIC_QWERTY;
    q->cb = cb;
    q->user = user;

    lv_obj_t *root = lv_obj_create(parent);
    q->root = root;
    lv_obj_remove_style_all(root);
    lv_obj_set_pos(root, x, y);
    lv_obj_set_size(root, w, h);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_clickable(root, true);   // Swallow touches behind the overlay
    lv_obj_set_style_bg_color(root, Theme::c(Theme::BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(root, Theme::c(Theme::TEXT), 0);
    lv_obj_set_style_text_font(root, &my_font_18, 0);
    lv_obj_set_user_data(root, q);
    lv_obj_add_event_cb(root, deleteEvent, LV_EVENT_DELETE, q);

    const int32_t pad = 6;
    lv_obj_t *cap = Theme::label(root, label ? label : "", &my_font_14, Theme::MUTED);
    lv_obj_set_pos(cap, pad, pad);

    q->hint = Theme::label(root, "", &my_font_14, Theme::GOLD);
    lv_obj_align(q->hint, LV_ALIGN_TOP_RIGHT, -pad, pad);

    q->ta = lv_textarea_create(root);
    lv_textarea_set_one_line(q->ta, true);
    lv_textarea_set_password_mode(q->ta, mask);
    if (maxLen) lv_textarea_set_max_length(q->ta, (uint32_t)maxLen);
    lv_textarea_set_text(q->ta, initial ? initial : "");
    lv_obj_set_pos(q->ta, pad, pad + 20);
    lv_obj_set_width(q->ta, w - 2 * pad);
    lv_obj_set_style_bg_color(q->ta, Theme::c(Theme::PANEL), 0);
    lv_obj_set_style_border_color(q->ta, Theme::c(Theme::ACCENT), 0);
    lv_obj_set_style_border_width(q->ta, 2, 0);
    lv_obj_set_style_radius(q->ta, 8, 0);
    lv_obj_set_style_text_color(q->ta, Theme::c(Theme::TEXT), 0);
    lv_obj_set_style_text_font(q->ta, &my_font_24, 0);
    lv_obj_add_state(q->ta, LV_STATE_FOCUSED);   // Show the cursor

    q->kb = lv_keyboard_create(root);
    lv_keyboard_set_map(q->kb, LV_KEYBOARD_MODE_TEXT_LOWER, MAP_LOWER, CTRL);
    lv_keyboard_set_map(q->kb, LV_KEYBOARD_MODE_TEXT_UPPER, MAP_UPPER, CTRL);
    lv_keyboard_set_textarea(q->kb, q->ta);
    lv_keyboard_set_popovers(q->kb, true);
    const int32_t kbY = pad + 20 + 56;
    lv_obj_set_pos(q->kb, 0, kbY);
    lv_obj_set_size(q->kb, w, h - kbY);
    lv_obj_set_style_bg_color(q->kb, Theme::c(Theme::BG), 0);
    lv_obj_set_style_border_width(q->kb, 0, 0);
    lv_obj_set_style_pad_all(q->kb, 4, 0);
    lv_obj_set_style_pad_gap(q->kb, 4, 0);
    lv_obj_set_style_bg_color(q->kb, Theme::c(Theme::PANEL_RAISED), LV_PART_ITEMS);
    lv_obj_set_style_text_color(q->kb, Theme::c(Theme::TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(q->kb, &my_font_18, LV_PART_ITEMS);
    lv_obj_set_style_border_color(q->kb, Theme::c(Theme::LINE), LV_PART_ITEMS);
    lv_obj_set_style_border_width(q->kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(q->kb, 8, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(q->kb, Theme::c(Theme::ACCENT_DARK), (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(q->kb, Theme::c(Theme::ACCENT), (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(q->kb, Theme::c(Theme::BG), (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_event_cb(q->kb, kbEvent, LV_EVENT_READY, q);
    lv_obj_add_event_cb(q->kb, kbEvent, LV_EVENT_CANCEL, q);
    lv_obj_add_event_cb(q->kb, kbEvent, LV_EVENT_VALUE_CHANGED, q);
    return root;
}

}  // namespace

void setDefaultMode(Mode m) { g_default = m; }
Mode defaultMode() { return g_default; }

lv_obj_t *open(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, const char *label,
               const char *initial, size_t maxLen, bool mask, Callback cb, void *user, Mode mode)
{
    if (mode == Mode::AUTO) mode = g_default;
    if (mode == Mode::AUTO) mode = w >= QWERTY_MIN_W ? Mode::QWERTY : Mode::WHEEL;
    if (mode == Mode::QWERTY) return openQwerty(parent, x, y, w, h, label, initial, maxLen, mask, cb, user);
    return CharWheel::create(parent, x, y, w, h, label, initial, maxLen, mask, cb, user);
}

void close(lv_obj_t *input)
{
    if (Qwerty *q = getQ(input)) {
        if (q->closing) return;
        q->closing = true;
        lv_textarea_set_text(q->ta, "");
        lv_obj_delete_async(input);
        return;
    }
    CharWheel::close(input);
}

void setHint(lv_obj_t *input, const char *message)
{
    if (Qwerty *q = getQ(input)) {
        if (!q->closing) lv_label_set_text(q->hint, message ? message : "");
        return;
    }
    CharWheel::setHint(input, message);
}

const char *text(lv_obj_t *input)
{
    if (Qwerty *q = getQ(input)) return q->closing ? nullptr : lv_textarea_get_text(q->ta);
    return CharWheel::text(input);
}

bool isQwerty(lv_obj_t *input) { return getQ(input) != nullptr; }

}  // namespace TextInput
