// ui_charwheel.cpp - LVGL 8 port of the MyPi-Hotspot scroll-wheel keyboard (mypi_kbd.c).
// All text/range logic lives in mypi_kbd_core.h (shared with MyPi-Hotspot); this file is UI only.
#include "my_charwheel.h"

#include <stdio.h>
#include <string.h>

#include "mypi_kbd_core.h"
#include "my_theme.h"

namespace CharWheel {

// ----------------------------------------------------------------------------------------
// Pure helpers (thin wrappers around mypi_kbd_core.h, kept for existing callers/tests)
// ----------------------------------------------------------------------------------------
bool utf8Append(char *buf, size_t cap, uint32_t cp) { return mypi_utf8_append(buf, cap, cp); }
bool utf8Backspace(char *buf) { return mypi_utf8_backspace(buf); }

// Only offer characters the big wheel font can actually draw (same filter as mypi_kbd.c).
static bool fontHasGlyph(uint32_t cp, void *ctx)
{
    lv_font_glyph_dsc_t dsc;
    return cp == ' ' || (lv_font_get_glyph_dsc((const lv_font_t *)ctx, &dsc, cp, 0) && dsc.box_w > 0);
}

// ----------------------------------------------------------------------------------------
// Widget
// ----------------------------------------------------------------------------------------
constexpr uint32_t MAGIC = 0x43575031;   // "CWP1"
constexpr int ROWS = 5;
constexpr int BUTTONS = 4;
static const uint8_t ROW_RATIO[ROWS] = {20, 20, 32, 20, 20};
static const char *const BUTTON_TEXT[BUTTONS] = {LV_SYMBOL_LEFT " Tillbaka", LV_SYMBOL_BACKSPACE,
                                                 LV_SYMBOL_PLUS, LV_SYMBOL_OK " OK"};
static const uint32_t BUTTON_COLOR[BUTTONS] = {Theme::MUTED, Theme::RED, Theme::TEAL, Theme::ACCENT};
static const char *const TAB_TEXT[MYPI_KBD_RANGE_COUNT] = {"az", "AZ", "09", "#+", "\xC3\xA5\xC3\xA4"};

struct Wheel {
    uint32_t magic;
    mypi_kbd_core_t core;
    bool mask, closing, hintMessage;
    Callback cb;
    void *user;
    const lv_font_t *font;
    lv_obj_t *root, *fieldText, *hint, *wheel;
    lv_obj_t *tabs[MYPI_KBD_RANGE_COUNT];
    lv_obj_t *rowLabels[ROWS];
    int32_t rowY[ROWS + 1];
    int32_t fieldInnerW;
    int32_t totX, totY, accY;
    uint8_t gesture;   // 0 undecided, 1 vertical, 2 horizontal, 3 done
};

static Wheel *get(lv_obj_t *o)
{
    Wheel *k = o ? (Wheel *)lv_obj_get_user_data(o) : nullptr;
    return k && k->magic == MAGIC ? k : nullptr;
}

static uint32_t current(const Wheel *k, int offset) { return mypi_kbd_current(&k->core, offset); }
static void step(Wheel *k, int d) { mypi_kbd_step(&k->core, d); }

static void refreshField(Wheel *k)
{
    char shown[TEXT_CAP + 2];
    if (k->mask) {
        size_t n = mypi_kbd_char_count(k->core.text);
        if (n > TEXT_CAP - 1) n = TEXT_CAP - 1;
        memset(shown, '*', n);
        shown[n] = '\0';
    } else {
        snprintf(shown, sizeof(shown), "%s", k->core.text);
    }
    size_t used = strlen(shown);
    shown[used] = '_';
    shown[used + 1] = '\0';
    // Keep the cursor visible: drop leading characters until the text fits.
    const char *p = shown;
    lv_point_t size;
    for (;;) {
        lv_text_get_size(&size, p, k->font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x <= k->fieldInnerW || p[1] == '\0') break;
        p += mypi_kbd_utf8_len((uint8_t)*p);
    }
    lv_label_set_text(k->fieldText, p);
    memset(shown, 0, sizeof(shown));
}

static void refresh(Wheel *k)
{
    refreshField(k);
    for (int r = 0; r < MYPI_KBD_RANGE_COUNT; r++) {
        bool active = r == k->core.range;
        if (!mypi_kbd_range_count(&k->core, r)) lv_obj_add_state(k->tabs[r], LV_STATE_DISABLED);
        lv_obj_set_style_bg_opa(k->tabs[r], active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(k->tabs[r], Theme::c(active ? Theme::ACCENT : Theme::LINE), 0);
        lv_obj_set_style_text_color(k->tabs[r], Theme::c(active ? Theme::TEXT : Theme::MUTED), 0);
    }
    if (!k->hintMessage) {
        lv_obj_set_style_text_color(k->hint, Theme::c(Theme::MUTED), 0);
        lv_label_set_text(k->hint, LV_SYMBOL_LEFT " grupp " LV_SYMBOL_RIGHT "    " LV_SYMBOL_UP " tecken " LV_SYMBOL_DOWN);
    }
    for (int i = 0; i < ROWS; i++) {
        char buf[8] = "";
        uint32_t cp = current(k, i - 2);
        if (cp == ' ') snprintf(buf, sizeof(buf), "%s", i == 2 ? "SPACE" : "spc");
        else utf8Append(buf, sizeof(buf), cp);
        lv_label_set_text(k->rowLabels[i], buf);
    }
}

static void notify(Wheel *k, bool confirmed)
{
    if (!k->cb) return;
    char copy[TEXT_CAP];
    snprintf(copy, sizeof(copy), "%s", k->core.text);
    k->cb(copy, confirmed, k->user);
    memset(copy, 0, sizeof(copy));
}

static void insertCurrent(Wheel *k)
{
    if (!mypi_kbd_insert(&k->core)) {
        setHint(k->root, "Maxlängd nådd");
    }
}

static void tabEvent(lv_event_t *e)
{
    Wheel *k = (Wheel *)lv_event_get_user_data(e);
    if (k->closing) return;
    lv_obj_t *tab = lv_event_get_current_target_obj(e);
    for (int r = 0; r < MYPI_KBD_RANGE_COUNT; r++) if (k->tabs[r] == tab) mypi_kbd_set_range(&k->core, r);
    k->hintMessage = false;
    refresh(k);
}

static void wheelEvent(lv_event_t *e)
{
    Wheel *k = (Wheel *)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (k->closing) return;
    bool changed = false;

    lv_indev_t *indev = lv_indev_active();
    if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) return;
    int32_t smallH = k->rowY[1] - k->rowY[0];
    int32_t stepPx = LV_MAX(8, smallH);
    int32_t slop = LV_MAX(6, smallH / 3);
    int32_t swipePx = LV_MAX(24, lv_obj_get_width(k->wheel) / 5);

    if (code == LV_EVENT_PRESSED) {
        k->totX = k->totY = k->accY = 0;
        k->gesture = 0;
    } else if (code == LV_EVENT_PRESSING) {
        lv_point_t v;
        lv_indev_get_vect(indev, &v);
        k->totX += v.x;
        k->totY += v.y;
        if (k->gesture == 0 && (LV_ABS(k->totX) > slop || LV_ABS(k->totY) > slop)) {
            k->gesture = LV_ABS(k->totX) > LV_ABS(k->totY) ? 2 : 1;
            k->accY = k->totY;
        } else if (k->gesture == 1) {
            k->accY += v.y;
        }
        if (k->gesture == 1) {
            // Finger down pulls the previous characters into the centre, like a real wheel.
            while (k->accY >= stepPx) { step(k, -1); k->accY -= stepPx; changed = true; }
            while (k->accY <= -stepPx) { step(k, 1); k->accY += stepPx; changed = true; }
        } else if (k->gesture == 2 && LV_ABS(k->totX) >= swipePx) {
            mypi_kbd_next_range(&k->core, k->totX < 0 ? 1 : -1);   // Swipe left = next range
            k->gesture = 3;
            changed = true;
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (k->gesture == 0) {
            lv_point_t p;
            lv_area_t a;
            lv_indev_get_point(indev, &p);
            lv_obj_get_coords(k->wheel, &a);
            int32_t y = p.y - a.y1;
            for (int i = 0; i < ROWS; i++) {
                if (y >= k->rowY[i] && y < k->rowY[i + 1] && i != 2) { step(k, i - 2); changed = true; }
            }
        }
        k->gesture = 0;
    }
    if (changed) {
        k->hintMessage = false;
        refresh(k);
    }
}

static void buttonEvent(lv_event_t *e)
{
    Wheel *k = (Wheel *)lv_event_get_user_data(e);
    if (k->closing) return;
    int id = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target_obj(e));
    k->hintMessage = false;
    switch (id) {
    case 0: notify(k, false); close(k->root); return;
    case 1: mypi_kbd_delete_char(&k->core); break;
    case 2: insertCurrent(k); break;
    default: notify(k, true); return;
    }
    refresh(k);
}

static void deleteEvent(lv_event_t *e)
{
    Wheel *k = (Wheel *)lv_event_get_user_data(e);
    memset(k, 0, sizeof(*k));
    lv_free(k);
}

static lv_obj_t *box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

lv_obj_t *create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                 const char *label, const char *initial, size_t maxLen, bool mask,
                 Callback cb, void *user)
{
    if (!parent || w < 120 || h < 200) return nullptr;
    Wheel *k = (Wheel *)lv_malloc(sizeof(Wheel));
    if (!k) return nullptr;
    memset(k, 0, sizeof(*k));
    k->magic = MAGIC;
    k->mask = mask;
    k->cb = cb;
    k->user = user;
    k->font = &my_font_18;
    mypi_kbd_core_init(&k->core, initial, maxLen, fontHasGlyph, (void *)&my_font_36);

    const int32_t pad = 6, gap = 5;
    int32_t fieldH = h * 14 / 100, tabsH = h * 9 / 100, hintH = h * 6 / 100, btnH = h * 14 / 100;
    int32_t wheelH = h - fieldH - tabsH - hintH - btnH;
    int32_t innerW = w - 2 * pad;
    int32_t lineH = lv_font_get_line_height(k->font);

    lv_obj_t *root = lv_obj_create(parent);
    k->root = root;
    lv_obj_remove_style_all(root);
    lv_obj_set_pos(root, x, y);
    lv_obj_set_size(root, w, h);
    lv_obj_set_scrollable(root, false);
    lv_obj_set_clickable(root, true);   // Swallow touches behind the overlay
    lv_obj_set_style_bg_color(root, Theme::c(Theme::BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(root, k->font, 0);
    lv_obj_set_style_text_color(root, Theme::c(Theme::TEXT), 0);
    lv_obj_set_user_data(root, k);
    lv_obj_add_event_cb(root, deleteEvent, LV_EVENT_DELETE, k);

    // Field: caption above the entered text with cursor.
    lv_obj_t *field = box(root, pad, pad, innerW, fieldH - pad);
    lv_obj_set_style_bg_color(field, Theme::c(Theme::PANEL), 0);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(field, 2, 0);
    lv_obj_set_style_border_color(field, Theme::c(Theme::ACCENT), 0);
    lv_obj_set_style_radius(field, 8, 0);
    k->fieldInnerW = innerW - 2 * pad - 4;
    if (label && label[0]) {
        lv_obj_t *t = Theme::label(field, label, &my_font_14, Theme::MUTED);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(t, k->fieldInnerW);
        lv_obj_set_pos(t, pad, 2);
    }
    k->fieldText = lv_label_create(field);
    lv_label_set_long_mode(k->fieldText, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(k->fieldText, k->fieldInnerW);
    lv_obj_set_pos(k->fieldText, pad, LV_MAX(2, fieldH - pad - lineH - 6));

    // Range tabs
    int32_t tabW = (innerW - (MYPI_KBD_RANGE_COUNT - 1) * gap) / MYPI_KBD_RANGE_COUNT;
    for (int r = 0; r < MYPI_KBD_RANGE_COUNT; r++) {
        lv_obj_t *tab = box(root, pad + r * (tabW + gap), fieldH + gap / 2, tabW, tabsH - gap);
        lv_obj_set_clickable(tab, true);
        lv_obj_set_style_bg_color(tab, Theme::c(Theme::ACCENT_DARK), 0);
        lv_obj_set_style_border_width(tab, 1, 0);
        lv_obj_set_style_radius(tab, 6, 0);
        lv_obj_set_ext_click_area(tab, gap / 2);
        lv_obj_t *t = lv_label_create(tab);
        lv_label_set_text(t, TAB_TEXT[r]);
        lv_obj_center(t);
        lv_obj_add_event_cb(tab, tabEvent, LV_EVENT_CLICKED, k);
        k->tabs[r] = tab;
    }

    // Hint row
    int32_t hintY = fieldH + tabsH;
    k->hint = lv_label_create(root);
    lv_obj_set_style_text_font(k->hint, &my_font_14, 0);
    lv_label_set_long_mode(k->hint, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(k->hint, innerW);
    lv_obj_set_style_text_align(k->hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(k->hint, pad, hintY + LV_MAX(0, (hintH - 16) / 2));

    // Wheel
    int32_t wheelY = hintY + hintH;
    k->wheel = box(root, pad, wheelY, innerW, wheelH);
    lv_obj_set_clickable(k->wheel, true);
    lv_obj_set_scroll_chain(k->wheel, false);
    lv_obj_set_gesture_bubble(k->wheel, false);
    int ratioSum = 0;
    for (int i = 0; i < ROWS; i++) ratioSum += ROW_RATIO[i];
    int32_t smallH = wheelH * ROW_RATIO[0] / ratioSum;
    for (int i = 0; i < ROWS; i++) {
        int32_t rowH = i == 2 ? wheelH - 4 * smallH : smallH;
        k->rowY[i + 1] = k->rowY[i] + rowH;
        int32_t inset = i == 2 ? 0 : innerW / 8;
        lv_obj_t *row = box(k->wheel, inset, k->rowY[i], innerW - 2 * inset, rowH - 2);
        k->rowLabels[i] = lv_label_create(row);
        lv_obj_center(k->rowLabels[i]);
        if (i == 2) {
            lv_obj_set_style_text_font(k->rowLabels[i], &my_font_36, 0);
            lv_obj_set_style_bg_color(row, Theme::c(Theme::ACCENT_DARK), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(row, 3, 0);
            lv_obj_set_style_border_color(row, Theme::c(Theme::ACCENT), 0);
            lv_obj_set_style_radius(row, 10, 0);
        } else {
            lv_obj_set_style_text_font(k->rowLabels[i], &my_font_24, 0);
            lv_obj_set_style_text_color(row, Theme::c(i == 1 || i == 3 ? Theme::MUTED : Theme::LINE), 0);
        }
    }
    lv_obj_add_event_cb(k->wheel, wheelEvent, LV_EVENT_PRESSED, k);
    lv_obj_add_event_cb(k->wheel, wheelEvent, LV_EVENT_PRESSING, k);
    lv_obj_add_event_cb(k->wheel, wheelEvent, LV_EVENT_RELEASED, k);

    // Buttons
    int32_t btnY = h - btnH + gap, bh = btnH - gap - pad;
    // Wider BACK / OK, narrow DEL / NEXT icon buttons.
    const int32_t weights[BUTTONS] = {3, 2, 2, 3};
    int32_t unit = (innerW - (BUTTONS - 1) * gap) / 10, bx = pad;
    for (int i = 0; i < BUTTONS; i++) {
        int32_t bw = unit * weights[i];
        lv_obj_t *btn = Theme::button(root, BUTTON_TEXT[i], bw, bh, BUTTON_COLOR[i]);
        lv_obj_set_pos(btn, bx, btnY);
        bx += bw + gap;
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, buttonEvent, LV_EVENT_CLICKED, k);
    }

    refresh(k);
    return root;
}

void close(lv_obj_t *wheel)
{
    Wheel *k = get(wheel);
    if (!k || k->closing) return;
    k->closing = true;
    memset(k->core.text, 0, sizeof(k->core.text));
    lv_obj_delete_async(wheel);
}

void setHint(lv_obj_t *wheel, const char *message)
{
    Wheel *k = get(wheel);
    if (!k || k->closing) return;
    k->hintMessage = message && message[0];
    if (k->hintMessage) {
        lv_obj_set_style_text_color(k->hint, Theme::c(Theme::GOLD), 0);
        lv_label_set_text(k->hint, message);
    }
}

const char *text(lv_obj_t *wheel)
{
    Wheel *k = get(wheel);
    return k && !k->closing ? k->core.text : nullptr;
}

}  // namespace CharWheel
