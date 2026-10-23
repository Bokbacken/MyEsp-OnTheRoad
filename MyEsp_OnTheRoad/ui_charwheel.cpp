// ui_charwheel.cpp - LVGL 8 port of the MyPi-Hotspot scroll-wheel keyboard (see header).
#include "ui_charwheel.h"

#include <stdio.h>
#include <string.h>

#include "encoder_input.h"
#include "ui_theme.h"

namespace CharWheel {

// ----------------------------------------------------------------------------------------
// UTF-8 helpers
// ----------------------------------------------------------------------------------------
bool utf8Append(char *buf, size_t cap, uint32_t cp)
{
    char enc[4];
    size_t n;
    if (cp < 0x80) { enc[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { enc[0] = (char)(0xC0 | (cp >> 6)); enc[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000) {
        enc[0] = (char)(0xE0 | (cp >> 12)); enc[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        enc[2] = (char)(0x80 | (cp & 0x3F)); n = 3;
    } else {
        enc[0] = (char)(0xF0 | (cp >> 18)); enc[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        enc[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); enc[3] = (char)(0x80 | (cp & 0x3F)); n = 4;
    }
    size_t used = strlen(buf);
    if (used + n + 1 > cap) return false;
    memcpy(buf + used, enc, n);
    buf[used + n] = '\0';
    return true;
}

bool utf8Backspace(char *buf)
{
    size_t n = strlen(buf);
    if (!n) return false;
    do { n--; } while (n > 0 && ((uint8_t)buf[n] & 0xC0) == 0x80);
    buf[n] = '\0';
    return true;
}

static size_t utf8Len(uint8_t lead)
{
    return lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : 4;
}

static size_t charCount(const char *t)
{
    size_t n = 0;
    for (; t && *t; t++) n += ((uint8_t)*t & 0xC0) != 0x80;
    return n;
}

// ----------------------------------------------------------------------------------------
// Character ranges (identical to mypi_kbd_core)
// ----------------------------------------------------------------------------------------
enum Range { R_LOWER = 0, R_UPPER, R_DIGIT, R_SYMBOL, R_LATIN, R_COUNT };

static const uint16_t LATIN[] = {
    0x00E5, 0x00E4, 0x00F6, 0x00C5, 0x00C4, 0x00D6,                         // å ä ö Å Ä Ö
    0x00E9, 0x00E8, 0x00EA, 0x00EB, 0x00E0, 0x00E2, 0x00E1,                 // é è ê ë à â á
    0x00F9, 0x00FB, 0x00FC, 0x00FA, 0x00EF, 0x00EE, 0x00ED, 0x00F4, 0x00F3, // ù û ü ú ï î í ô ó
    0x00E7, 0x00E6, 0x00F8, 0x00F1, 0x00DF,                                 // ç æ ø ñ ß
    0x00C9, 0x00C8, 0x00DC, 0x00C6, 0x00D8, 0x00D1, 0x00C7,                 // É È Ü Æ Ø Ñ Ç
};
static const char SYMBOLS[] = " !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";

static int rangeCount(int r)
{
    switch (r) {
    case R_LOWER: case R_UPPER: return 26;
    case R_DIGIT: return 10;
    case R_SYMBOL: return (int)sizeof(SYMBOLS) - 1;
    case R_LATIN: return (int)(sizeof(LATIN) / sizeof(LATIN[0]));
    default: return 0;
    }
}

static uint32_t rangeCp(int r, int i)
{
    switch (r) {
    case R_LOWER: return 'a' + i;
    case R_UPPER: return 'A' + i;
    case R_DIGIT: return '0' + i;
    case R_SYMBOL: return (uint8_t)SYMBOLS[i];
    default: return LATIN[i];
    }
}

static int wrap(int v, int n) { if (n <= 0) return 0; v %= n; return v < 0 ? v + n : v; }

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
static const char *const TAB_TEXT[R_COUNT] = {"az", "AZ", "09", "#+", "\xC3\xA5\xC3\xA4"};

struct Wheel {
    uint32_t magic;
    char text[TEXT_CAP];
    size_t maxLen;
    int range, index;
    bool mask, closing, hintMessage;
    Callback cb;
    void *user;
    const lv_font_t *font;
    lv_obj_t *root, *fieldText, *hint, *wheel;
    lv_obj_t *tabs[R_COUNT];
    lv_obj_t *rowLabels[ROWS];
    lv_coord_t rowY[ROWS + 1];
    lv_coord_t fieldInnerW;
    lv_group_t *group;
    lv_coord_t totX, totY, accY;
    uint8_t gesture;   // 0 undecided, 1 vertical, 2 horizontal, 3 done
};

static Wheel *get(lv_obj_t *o)
{
    Wheel *k = o ? (Wheel *)lv_obj_get_user_data(o) : nullptr;
    return k && k->magic == MAGIC ? k : nullptr;
}

static uint32_t current(const Wheel *k, int offset)
{
    return rangeCp(k->range, wrap(k->index + offset, rangeCount(k->range)));
}

static void step(Wheel *k, int d) { k->index = wrap(k->index + d, rangeCount(k->range)); }

static void setRange(Wheel *k, int r)
{
    r = wrap(r, R_COUNT);
    if (r != k->range) k->index = 0;
    k->range = r;
}

static void refreshField(Wheel *k)
{
    char shown[TEXT_CAP + 2];
    if (k->mask) {
        size_t n = charCount(k->text);
        if (n > TEXT_CAP - 1) n = TEXT_CAP - 1;
        memset(shown, '*', n);
        shown[n] = '\0';
    } else {
        snprintf(shown, sizeof(shown), "%s", k->text);
    }
    size_t used = strlen(shown);
    shown[used] = '_';
    shown[used + 1] = '\0';
    // Keep the cursor visible: drop leading characters until the text fits.
    const char *p = shown;
    lv_point_t size;
    for (;;) {
        lv_txt_get_size(&size, p, k->font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x <= k->fieldInnerW || p[1] == '\0') break;
        p += utf8Len((uint8_t)*p);
    }
    lv_label_set_text(k->fieldText, p);
    memset(shown, 0, sizeof(shown));
}

static void refresh(Wheel *k)
{
    refreshField(k);
    for (int r = 0; r < R_COUNT; r++) {
        bool active = r == k->range;
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
    snprintf(copy, sizeof(copy), "%s", k->text);
    k->cb(copy, confirmed, k->user);
    memset(copy, 0, sizeof(copy));
}

static void insertCurrent(Wheel *k)
{
    if (strlen(k->text) >= k->maxLen || !utf8Append(k->text, k->maxLen + 1, current(k, 0))) {
        setHint(k->root, "Maxlängd nådd");
    }
}

static void tabEvent(lv_event_t *e)
{
    Wheel *k = (Wheel *)lv_event_get_user_data(e);
    if (k->closing) return;
    lv_obj_t *tab = lv_event_get_current_target(e);
    for (int r = 0; r < R_COUNT; r++) if (k->tabs[r] == tab) setRange(k, r);
    k->hintMessage = false;
    refresh(k);
}

static void wheelEvent(lv_event_t *e)
{
    Wheel *k = (Wheel *)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    if (k->closing) return;
    bool changed = false;

    if (code == LV_EVENT_KEY) {
        // External encoder / keypad
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_RIGHT || key == LV_KEY_DOWN) { step(k, 1); changed = true; }
        else if (key == LV_KEY_LEFT || key == LV_KEY_UP) { step(k, -1); changed = true; }
        else if (key == LV_KEY_ENTER) { insertCurrent(k); changed = true; }
        if (changed) { refresh(k); }
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) return;
    lv_coord_t smallH = k->rowY[1] - k->rowY[0];
    lv_coord_t stepPx = LV_MAX(8, smallH);
    lv_coord_t slop = LV_MAX(6, smallH / 3);
    lv_coord_t swipePx = LV_MAX(24, lv_obj_get_width(k->wheel) / 5);

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
            setRange(k, k->range + (k->totX < 0 ? 1 : -1));   // Swipe left = next range
            k->gesture = 3;
            changed = true;
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (k->gesture == 0) {
            lv_point_t p;
            lv_area_t a;
            lv_indev_get_point(indev, &p);
            lv_obj_get_coords(k->wheel, &a);
            lv_coord_t y = p.y - a.y1;
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
    int id = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    k->hintMessage = false;
    switch (id) {
    case 0: notify(k, false); close(k->root); return;
    case 1: utf8Backspace(k->text); break;
    case 2: insertCurrent(k); break;
    default: notify(k, true); return;
    }
    refresh(k);
}

static void deleteEvent(lv_event_t *e)
{
    Wheel *k = (Wheel *)lv_event_get_user_data(e);
    if (k->group) lv_group_del(k->group);
    memset(k, 0, sizeof(*k));
    lv_mem_free(k);
}

static lv_obj_t *box(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                 const char *label, const char *initial, size_t maxLen, bool mask,
                 Callback cb, void *user)
{
    if (!parent || w < 120 || h < 200) return nullptr;
    Wheel *k = (Wheel *)lv_mem_alloc(sizeof(Wheel));
    if (!k) return nullptr;
    memset(k, 0, sizeof(*k));
    k->magic = MAGIC;
    k->maxLen = maxLen && maxLen < TEXT_CAP ? maxLen : TEXT_CAP - 1;
    k->mask = mask;
    k->cb = cb;
    k->user = user;
    k->font = &otr_font_18;
    // Copy whole UTF-8 sequences only.
    size_t used = 0;
    for (const char *p = initial ? initial : ""; *p;) {
        size_t n = utf8Len((uint8_t)*p);
        if (used + n > k->maxLen || strnlen(p, n) < n) break;
        memcpy(k->text + used, p, n);
        used += n;
        p += n;
    }

    const lv_coord_t pad = 6, gap = 5;
    lv_coord_t fieldH = h * 14 / 100, tabsH = h * 9 / 100, hintH = h * 6 / 100, btnH = h * 14 / 100;
    lv_coord_t wheelH = h - fieldH - tabsH - hintH - btnH;
    lv_coord_t innerW = w - 2 * pad;
    lv_coord_t lineH = lv_font_get_line_height(k->font);

    lv_obj_t *root = lv_obj_create(parent);
    k->root = root;
    lv_obj_remove_style_all(root);
    lv_obj_set_pos(root, x, y);
    lv_obj_set_size(root, w, h);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);   // Swallow touches behind the overlay
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
        lv_obj_t *t = Theme::label(field, label, &otr_font_14, Theme::MUTED);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_width(t, k->fieldInnerW);
        lv_obj_set_pos(t, pad, 2);
    }
    k->fieldText = lv_label_create(field);
    lv_label_set_long_mode(k->fieldText, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(k->fieldText, k->fieldInnerW);
    lv_obj_set_pos(k->fieldText, pad, LV_MAX(2, fieldH - pad - lineH - 6));

    // Range tabs
    lv_coord_t tabW = (innerW - (R_COUNT - 1) * gap) / R_COUNT;
    for (int r = 0; r < R_COUNT; r++) {
        lv_obj_t *tab = box(root, pad + r * (tabW + gap), fieldH + gap / 2, tabW, tabsH - gap);
        lv_obj_add_flag(tab, LV_OBJ_FLAG_CLICKABLE);
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
    lv_coord_t hintY = fieldH + tabsH;
    k->hint = lv_label_create(root);
    lv_obj_set_style_text_font(k->hint, &otr_font_14, 0);
    lv_label_set_long_mode(k->hint, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(k->hint, innerW);
    lv_obj_set_style_text_align(k->hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(k->hint, pad, hintY + LV_MAX(0, (hintH - 16) / 2));

    // Wheel
    lv_coord_t wheelY = hintY + hintH;
    k->wheel = box(root, pad, wheelY, innerW, wheelH);
    lv_obj_add_flag(k->wheel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(k->wheel, LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_outline_width(k->wheel, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(k->wheel, Theme::c(Theme::TEAL), LV_STATE_FOCUS_KEY);
    int ratioSum = 0;
    for (int i = 0; i < ROWS; i++) ratioSum += ROW_RATIO[i];
    lv_coord_t smallH = wheelH * ROW_RATIO[0] / ratioSum;
    for (int i = 0; i < ROWS; i++) {
        lv_coord_t rowH = i == 2 ? wheelH - 4 * smallH : smallH;
        k->rowY[i + 1] = k->rowY[i] + rowH;
        lv_coord_t inset = i == 2 ? 0 : innerW / 8;
        lv_obj_t *row = box(k->wheel, inset, k->rowY[i], innerW - 2 * inset, rowH - 2);
        k->rowLabels[i] = lv_label_create(row);
        lv_obj_center(k->rowLabels[i]);
        if (i == 2) {
            lv_obj_set_style_text_font(k->rowLabels[i], &otr_font_36, 0);
            lv_obj_set_style_bg_color(row, Theme::c(Theme::ACCENT_DARK), 0);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(row, 3, 0);
            lv_obj_set_style_border_color(row, Theme::c(Theme::ACCENT), 0);
            lv_obj_set_style_radius(row, 10, 0);
        } else {
            lv_obj_set_style_text_font(k->rowLabels[i], &otr_font_24, 0);
            lv_obj_set_style_text_color(row, Theme::c(i == 1 || i == 3 ? Theme::MUTED : Theme::LINE), 0);
        }
    }
    lv_obj_add_event_cb(k->wheel, wheelEvent, LV_EVENT_PRESSED, k);
    lv_obj_add_event_cb(k->wheel, wheelEvent, LV_EVENT_PRESSING, k);
    lv_obj_add_event_cb(k->wheel, wheelEvent, LV_EVENT_RELEASED, k);
    lv_obj_add_event_cb(k->wheel, wheelEvent, LV_EVENT_KEY, k);

    // Buttons
    k->group = lv_group_create();
    lv_group_add_obj(k->group, k->wheel);
    lv_coord_t btnY = h - btnH + gap, bh = btnH - gap - pad;
    // Wider BACK / OK, narrow DEL / NEXT icon buttons.
    const lv_coord_t weights[BUTTONS] = {3, 2, 2, 3};
    lv_coord_t unit = (innerW - (BUTTONS - 1) * gap) / 10, bx = pad;
    for (int i = 0; i < BUTTONS; i++) {
        lv_coord_t bw = unit * weights[i];
        lv_obj_t *btn = Theme::button(root, BUTTON_TEXT[i], bw, bh, BUTTON_COLOR[i]);
        lv_obj_set_pos(btn, bx, btnY);
        bx += bw + gap;
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, buttonEvent, LV_EVENT_CLICKED, k);
        lv_group_add_obj(k->group, btn);
    }
    Encoder::setGroup(k->group);

    refresh(k);
    return root;
}

void close(lv_obj_t *wheel)
{
    Wheel *k = get(wheel);
    if (!k || k->closing) return;
    k->closing = true;
    memset(k->text, 0, sizeof(k->text));
    lv_obj_del_async(wheel);
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
    return k && !k->closing ? k->text : nullptr;
}

}  // namespace CharWheel
