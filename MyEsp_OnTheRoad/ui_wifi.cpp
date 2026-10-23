// ui_wifi.cpp - Wi-Fi network list, password entry and connecting screens (see header).
#include "ui_wifi.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "board_config.h"
#include "board_wifi.h"
#include "encoder_input.h"
#include "ui_theme.h"
#include "ui_yatzy.h"

// Extra FontAwesome glyphs included in otr_font_14/18/24.
#define SYM_LOCK   "\xEF\x80\xA3"   // U+F023
#define SYM_UNLOCK "\xEF\x8F\x81"   // U+F3C1

namespace UiWifi {

// One timer for whichever Wi-Fi screen is active. Killed before every navigation so it
// never touches objects of a deleted screen.
static lv_timer_t *s_timer = nullptr;

static void killTimer()
{
    if (s_timer) {
        lv_timer_del(s_timer);
        s_timer = nullptr;
    }
}

static void toMenu()
{
    killTimer();
    UiYatzy::showMenu();
}

// 5 signal bars (heights 6..18 px) in a small container.
static lv_obj_t *signalBars(lv_obj_t *parent, int bars)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 5 * 5 + 4 * 2, 18);
    lv_obj_clear_flag(box, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    uint32_t col = bars >= 4 ? Theme::GREEN : bars == 3 ? Theme::ACCENT : Theme::RED;
    for (int i = 0; i < 5; i++) {
        lv_obj_t *b = lv_obj_create(box);
        lv_obj_remove_style_all(b);
        int h = 6 + i * 3;
        lv_obj_set_size(b, 5, h);
        lv_obj_set_pos(b, i * 7, 18 - h);
        lv_obj_set_style_radius(b, 1, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, Theme::c(i < bars ? col : Theme::LINE), 0);
        lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
    }
    return box;
}

// ========================================================================================
// Screen 1: network list
// ========================================================================================
static Wifi::Network s_nets[Wifi::MAX_NETWORKS];
static int s_netCount = 0;
static lv_obj_t *s_list, *s_busy, *s_busyLabel, *s_netStatus, *s_rescanBtn;
static lv_group_t *s_listGroup;
static uint32_t s_scanStartMs;

static void listStatusLine()
{
    Wifi::Info wi = Wifi::info();
    char buf[96];
    if (wi.connected) snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %s  \xE2\x80\xA2  %s", wi.ssid, wi.ip.toString().c_str());
    else if (wi.status == Wifi::Status::CONNECTING) snprintf(buf, sizeof(buf), "Ansluter till %s…", wi.ssid);
    else if (Wifi::hasSavedCredentials()) snprintf(buf, sizeof(buf), "Inte ansluten  (sparat: %s)", Wifi::savedSsid());
    else snprintf(buf, sizeof(buf), "Inte ansluten");
    lv_label_set_text(s_netStatus, buf);
    lv_obj_set_style_text_color(s_netStatus, Theme::c(wi.connected ? Theme::GREEN : Theme::MUTED), 0);
}

static void rowEvent(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *row = lv_event_get_current_target(e);
    lv_obj_t *arrow = lv_obj_get_child(row, 0);
    if (code == LV_EVENT_FOCUSED) {
        lv_obj_clear_flag(arrow, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_obj_add_flag(arrow, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_CLICKED) {
        int i = (int)(intptr_t)lv_event_get_user_data(e);
        if (i < 0 || i >= s_netCount) return;
        killTimer();
        if (s_nets[i].secure) showPassword(s_nets[i].ssid);
        else showConnecting(s_nets[i].ssid, "");
    }
}

static void showBusy(const char *text, bool spinner)
{
    lv_obj_clean(s_list);
    lv_obj_clear_flag(s_busy, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *sp = lv_obj_get_child(s_busy, 0);
    if (spinner) lv_obj_clear_flag(sp, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(sp, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_busyLabel, text);
}

static void fillList()
{
    s_netCount = Wifi::scanResults(s_nets, Wifi::MAX_NETWORKS);
    lv_obj_clean(s_list);
    if (s_netCount == 0) {
        showBusy("Inga nätverk hittades.\nTryck på Uppdatera för att söka igen.", false);
        lv_group_focus_obj(s_rescanBtn);
        return;
    }
    lv_obj_add_flag(s_busy, LV_OBJ_FLAG_HIDDEN);
    Wifi::Info wi = Wifi::info();
    lv_obj_t *first = nullptr;
    for (int i = 0; i < s_netCount; i++) {
        const Wifi::Network &n = s_nets[i];
        lv_obj_t *row = lv_btn_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), 50);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, Theme::c(Theme::PANEL), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, Theme::c(Theme::LINE), 0);
        lv_obj_set_style_bg_color(row, Theme::c(Theme::PANEL_RAISED), LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(row, Theme::c(Theme::PANEL_RAISED), LV_STATE_FOCUSED);
        lv_obj_set_style_border_width(row, 2, LV_STATE_FOCUSED);
        lv_obj_set_style_border_color(row, Theme::c(Theme::ACCENT), LV_STATE_FOCUSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *arrow = Theme::label(row, LV_SYMBOL_PLAY, &otr_font_14, Theme::ACCENT);   // child 0
        lv_obj_align(arrow, LV_ALIGN_LEFT_MID, 6, 0);
        lv_obj_add_flag(arrow, LV_OBJ_FLAG_HIDDEN);

        bool isCurrent = wi.connected && strcmp(wi.ssid, n.ssid) == 0;
        char name[48];
        snprintf(name, sizeof(name), "%s%s", n.ssid, isCurrent ? " " LV_SYMBOL_OK : "");
        lv_obj_t *l = Theme::label(row, name, &otr_font_18, isCurrent ? Theme::GREEN : Theme::TEXT);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, 178);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 24, 0);

        lv_obj_t *sec = n.secure ? Theme::label(row, SYM_LOCK, &otr_font_14, Theme::MUTED)
                                 : Theme::label(row, SYM_UNLOCK " Öppen", &otr_font_14, Theme::GREEN);
        lv_obj_align(sec, LV_ALIGN_RIGHT_MID, -44, 0);

        lv_obj_t *bars = signalBars(row, Wifi::signalBars(n.rssi));
        lv_obj_align(bars, LV_ALIGN_RIGHT_MID, -8, 0);

        lv_obj_add_event_cb(row, rowEvent, LV_EVENT_ALL, (void *)(intptr_t)i);
        lv_group_add_obj(s_listGroup, row);
        if (!first) first = row;
    }
    // Keep the bottom buttons last in the focus order.
    lv_group_remove_obj(s_rescanBtn);
    lv_group_add_obj(s_listGroup, s_rescanBtn);
    lv_obj_t *cancel = (lv_obj_t *)lv_obj_get_user_data(s_rescanBtn);
    lv_group_remove_obj(cancel);
    lv_group_add_obj(s_listGroup, cancel);
    lv_group_focus_obj(first);
}

static void startListScan()
{
    showBusy("Söker nätverk…", true);
    s_scanStartMs = millis();
    Wifi::startScan();
}

static void listTick(lv_timer_t *)
{
    listStatusLine();
    if (lv_obj_has_flag(s_busy, LV_OBJ_FLAG_HIDDEN)) return;   // List is shown
    lv_obj_t *sp = lv_obj_get_child(s_busy, 0);
    if (lv_obj_has_flag(sp, LV_OBJ_FLAG_HIDDEN)) return;       // Final message shown
    int st = Wifi::scanStatus();
    if (st >= 0) {
        fillList();
    } else if (st == -1 || millis() - s_scanStartMs > 12000) {
        showBusy("Sökningen misslyckades.\nTryck på Uppdatera för att försöka igen.", false);
        lv_group_focus_obj(s_rescanBtn);
    }
}

void showNetworks()
{
    killTimer();
    lv_obj_t *scr = Theme::screen();
    s_listGroup = Theme::newScreenGroup(Encoder::setGroup);
    Theme::header(scr, "Välj nätverk");

    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_pos(s_list, 0, 48);
    lv_obj_set_size(s_list, 320, 344);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(s_list, 8, 0);
    lv_obj_set_style_pad_ver(s_list, 4, 0);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_ACTIVE);

    s_busy = lv_obj_create(scr);
    lv_obj_remove_style_all(s_busy);
    lv_obj_set_size(s_busy, 300, 200);
    lv_obj_align(s_busy, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_clear_flag(s_busy, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_t *sp = lv_spinner_create(s_busy, 1000, 60);                       // child 0
    lv_obj_set_size(sp, 64, 64);
    lv_obj_align(sp, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_arc_color(sp, Theme::c(Theme::LINE), LV_PART_MAIN);
    lv_obj_set_style_arc_color(sp, Theme::c(Theme::ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(sp, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(sp, 6, LV_PART_INDICATOR);
    s_busyLabel = Theme::label(s_busy, "", &otr_font_18, Theme::MUTED);
    lv_obj_set_width(s_busyLabel, 300);
    lv_label_set_long_mode(s_busyLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_busyLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_busyLabel, LV_ALIGN_TOP_MID, 0, 80);

    s_netStatus = Theme::label(scr, "", &otr_font_14, Theme::MUTED);
    lv_label_set_long_mode(s_netStatus, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_netStatus, 304);
    lv_obj_set_pos(s_netStatus, 10, 400);

    s_rescanBtn = Theme::button(scr, LV_SYMBOL_REFRESH " Uppdatera", 148, 46, Theme::ACCENT);
    lv_obj_set_pos(s_rescanBtn, 8, 426);
    lv_obj_add_event_cb(s_rescanBtn, [](lv_event_t *) { startListScan(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancel = Theme::button(scr, LV_SYMBOL_CLOSE " Avbryt", 148, 46, Theme::MUTED);
    lv_obj_set_pos(cancel, 164, 426);
    lv_obj_add_event_cb(cancel, [](lv_event_t *) { toMenu(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_user_data(s_rescanBtn, cancel);
    lv_group_add_obj(s_listGroup, s_rescanBtn);
    lv_group_add_obj(s_listGroup, cancel);

    startListScan();
    listStatusLine();
    s_timer = lv_timer_create(listTick, 200, nullptr);
    Theme::loadScreen(scr);
}

// ========================================================================================
// Screen 2: password entry with a rotating character strip
// ========================================================================================
static const char CHARSET[] =
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789"
    "!@#$%&*?-_.,:;+=/\\()[]{}<>'\"^~`| ";
static constexpr int CHARSET_LEN = sizeof(CHARSET) - 1;
static constexpr int JUMP_LOWER = 0, JUMP_UPPER = 26, JUMP_DIGIT = 52, JUMP_SYMBOL = 62;
static constexpr size_t PASS_MAX = 64;
static constexpr int STRIP_CELLS = 7, STRIP_CENTER = 3, CELL_W = 40, CENTER_W = 64;

static char s_ssid[33];
static char s_pass[PASS_MAX + 1];
static bool s_showPass = false;
static int s_charIdx = 0;
static bool s_cursorOn = true;
static lv_obj_t *s_passLabel, *s_countLabel, *s_hint, *s_showBtnLabel, *s_strip;
static lv_obj_t *s_cells[STRIP_CELLS];
static int32_t s_dragAcc = 0;
static bool s_dragged = false;

static int wrapIdx(int i) { return ((i % CHARSET_LEN) + CHARSET_LEN) % CHARSET_LEN; }

static void cellText(lv_obj_t *l, char ch, bool center)
{
    if (ch == ' ') {
        lv_label_set_text(l, "spc");
        lv_obj_set_style_text_font(l, center ? &otr_font_18 : &otr_font_14, 0);
        return;
    }
    char buf[2] = {ch, 0};
    lv_label_set_text(l, buf);
}

static void renderStrip()
{
    for (int k = 0; k < STRIP_CELLS; k++) {
        int d = k - STRIP_CENTER;
        int ad = d < 0 ? -d : d;
        lv_obj_t *l = s_cells[k];
        lv_obj_set_style_text_font(l, ad == 0 ? &otr_font_36 : ad == 1 ? &otr_font_24 : ad == 2 ? &otr_font_18 : &otr_font_14, 0);
        cellText(l, CHARSET[wrapIdx(s_charIdx + d)], ad == 0);
    }
}

static void renderPass()
{
    size_t n = strlen(s_pass);
    constexpr size_t SHOW = 22;   // Characters that fit in the field
    char buf[PASS_MAX * 3 + 8];
    size_t start = n > SHOW ? n - SHOW : 0, o = 0;
    if (start) o += snprintf(buf + o, sizeof(buf) - o, "\xE2\x80\xA6");   // Ellipsis
    for (size_t i = start; i < n && o < sizeof(buf) - 5; i++) {
        if (s_showPass) buf[o++] = s_pass[i];
        else { memcpy(buf + o, "\xE2\x80\xA2", 3); o += 3; }   // Bullet
    }
    if (s_cursorOn && n < PASS_MAX) buf[o++] = '_';
    buf[o] = 0;
    lv_label_set_text(s_passLabel, buf);
    lv_label_set_text_fmt(s_countLabel, "%u/%u", (unsigned)n, (unsigned)PASS_MAX);
}

static void setHint(const char *text, bool error)
{
    lv_label_set_text(s_hint, text);
    lv_obj_set_style_text_color(s_hint, Theme::c(error ? Theme::RED : Theme::MUTED), 0);
}

static void appendChar(char ch)
{
    size_t n = strlen(s_pass);
    if (n >= PASS_MAX) {
        setHint("Max 64 tecken.", true);
        return;
    }
    s_pass[n] = ch;
    s_pass[n + 1] = 0;
    s_cursorOn = true;
    renderPass();
}

static void stepChar(int delta)
{
    s_charIdx = wrapIdx(s_charIdx + delta);
    renderStrip();
}

static void stripEvent(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    lv_indev_type_t type = indev ? lv_indev_get_type(indev) : LV_INDEV_TYPE_NONE;
    if (code == LV_EVENT_KEY) {
        uint32_t key = lv_event_get_key(e);
        if (key == LV_KEY_RIGHT || key == LV_KEY_UP) stepChar(1);
        else if (key == LV_KEY_LEFT || key == LV_KEY_DOWN) stepChar(-1);
        else if (key == LV_KEY_BACKSPACE) { /* handled by the Rensa button */ }
    } else if (code == LV_EVENT_PRESSED && type == LV_INDEV_TYPE_POINTER) {
        s_dragAcc = 0;
        s_dragged = false;
    } else if (code == LV_EVENT_PRESSING && type == LV_INDEV_TYPE_POINTER) {
        lv_point_t v;
        lv_indev_get_vect(indev, &v);
        s_dragAcc += v.x;
        while (s_dragAcc <= -CELL_W) { s_dragAcc += CELL_W; stepChar(1); s_dragged = true; }
        while (s_dragAcc >= CELL_W) { s_dragAcc -= CELL_W; stepChar(-1); s_dragged = true; }
    } else if (code == LV_EVENT_CLICKED) {
        if (type == LV_INDEV_TYPE_POINTER) {
            if (s_dragged) return;
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            lv_area_t a;
            lv_obj_get_coords(s_strip, &a);
            int x = p.x - a.x1;
            // Cell boundaries: 3 x 40 | 64 | 3 x 40
            int k = x < 3 * CELL_W ? x / CELL_W
                  : x < 3 * CELL_W + CENTER_W ? STRIP_CENTER
                  : 4 + (x - 3 * CELL_W - CENTER_W) / CELL_W;
            if (k < 0) k = 0;
            if (k >= STRIP_CELLS) k = STRIP_CELLS - 1;
            s_charIdx = wrapIdx(s_charIdx + k - STRIP_CENTER);
            renderStrip();
        }
        appendChar(CHARSET[s_charIdx]);
    }
}

// The strip must be "editable" so the encoder sends LV_KEY_LEFT/RIGHT to it while
// editing (rotate = step) and a click on press (append) instead of moving the focus.
static lv_obj_t *createStrip(lv_obj_t *parent)
{
    static lv_obj_class_t cls;
    static bool inited = false;
    if (!inited) {
        memset(&cls, 0, sizeof(cls));
        cls.base_class = &lv_obj_class;
        cls.editable = LV_OBJ_CLASS_EDITABLE_TRUE;
        cls.group_def = LV_OBJ_CLASS_GROUP_DEF_FALSE;
        cls.instance_size = sizeof(lv_obj_t);
        inited = true;
    }
    lv_obj_t *o = lv_obj_class_create_obj(&cls, parent);
    lv_obj_class_init_obj(o);
    return o;
}

static void passTick(lv_timer_t *)
{
    s_cursorOn = !s_cursorOn;
    renderPass();
}

static void jumpBtn(lv_obj_t *scr, lv_group_t *g, const char *text, int x, int target)
{
    lv_obj_t *b = Theme::button(scr, text, 70, 38, Theme::TEAL);
    lv_obj_set_pos(b, x, 248);
    lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &otr_font_14, 0);
    lv_obj_add_event_cb(b, [](lv_event_t *e) {
        s_charIdx = (int)(intptr_t)lv_event_get_user_data(e);
        renderStrip();
    }, LV_EVENT_CLICKED, (void *)(intptr_t)target);
    lv_group_add_obj(g, b);
}

static void onConnectPressed(lv_event_t *)
{
    size_t n = strlen(s_pass);
    bool hex64 = n == 64;
    for (size_t i = 0; hex64 && i < n; i++) hex64 = isxdigit((unsigned char)s_pass[i]);
    if (n == 0) return setHint("Ange lösenordet först.", true);
    if (n < 8) return setHint("Lösenordet måste vara minst 8 tecken.", true);
    if (n == 64 && !hex64) return setHint("64 tecken måste vara en hex-nyckel (0-9, a-f).", true);
    killTimer();
    showConnecting(s_ssid, s_pass);
}

void showPassword(const char *ssid)
{
    killTimer();
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid ? ssid : "");
    s_pass[0] = 0;
    if (strcmp(s_ssid, Wifi::savedSsid()) == 0) snprintf(s_pass, sizeof(s_pass), "%s", Config::get().staPass);
    s_showPass = false;
    s_cursorOn = true;
    s_charIdx = 0;

    lv_obj_t *scr = Theme::screen();
    lv_group_t *g = Theme::newScreenGroup(Encoder::setGroup);
    lv_obj_t *hdr = Theme::header(scr, "");
    lv_obj_t *title = lv_obj_get_child(hdr, 0);
    lv_label_set_text_fmt(title, SYM_LOCK " %s", s_ssid);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 300);

    lv_obj_t *lbl = Theme::label(scr, "Lösenord:", &otr_font_14, Theme::MUTED);
    lv_obj_set_pos(lbl, 12, 54);
    s_countLabel = Theme::label(scr, "", &otr_font_14, Theme::MUTED);
    lv_obj_align(s_countLabel, LV_ALIGN_TOP_RIGHT, -12, 54);

    lv_obj_t *field = Theme::card(scr, 304, 50);
    lv_obj_set_pos(field, 8, 74);
    lv_obj_clear_flag(field, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(field, Theme::c(Theme::NEST_BG), 0);
    s_passLabel = Theme::label(field, "", &otr_font_18);
    lv_label_set_long_mode(s_passLabel, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(s_passLabel, 284);
    lv_obj_align(s_passLabel, LV_ALIGN_LEFT_MID, 2, 0);

    // Character strip: 3 x 40 px | 64 px centre | 3 x 40 px
    s_strip = createStrip(scr);
    lv_obj_remove_style_all(s_strip);
    lv_obj_set_size(s_strip, 304, 96);
    lv_obj_set_pos(s_strip, 8, 138);
    lv_obj_set_style_radius(s_strip, 12, 0);
    lv_obj_set_style_bg_color(s_strip, Theme::c(Theme::PANEL), 0);
    lv_obj_set_style_bg_opa(s_strip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_strip, 1, 0);
    lv_obj_set_style_border_color(s_strip, Theme::c(Theme::LINE), 0);
    lv_obj_set_style_outline_width(s_strip, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(s_strip, Theme::c(Theme::TEAL), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(s_strip, Theme::c(Theme::ACCENT), LV_STATE_EDITED);
    lv_obj_set_style_outline_pad(s_strip, 2, LV_STATE_FOCUS_KEY);
    lv_obj_clear_flag(s_strip, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE));
    lv_obj_add_flag(s_strip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *hl = lv_obj_create(s_strip);
    lv_obj_remove_style_all(hl);
    lv_obj_set_size(hl, CENTER_W - 4, 84);
    lv_obj_set_pos(hl, 3 * CELL_W + 2, 6);
    lv_obj_set_style_radius(hl, 10, 0);
    lv_obj_set_style_bg_color(hl, Theme::c(Theme::ACCENT), 0);
    lv_obj_set_style_bg_opa(hl, LV_OPA_COVER, 0);
    lv_obj_clear_flag(hl, LV_OBJ_FLAG_CLICKABLE);
    for (int k = 0; k < STRIP_CELLS; k++) {
        int d = k - STRIP_CENTER, ad = d < 0 ? -d : d;
        int x = k < STRIP_CENTER ? k * CELL_W : k == STRIP_CENTER ? 3 * CELL_W : 3 * CELL_W + CENTER_W + (k - 4) * CELL_W;
        int w = k == STRIP_CENTER ? CENTER_W : CELL_W;
        uint32_t col = ad == 0 ? Theme::BG : ad == 1 ? Theme::TEXT : Theme::MUTED;
        lv_obj_t *l = Theme::label(s_strip, "", &otr_font_18, col);
        lv_obj_set_width(l, w);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, x, 0);
        if (ad == 3) lv_obj_set_style_text_opa(l, LV_OPA_60, 0);
        lv_obj_clear_flag(l, LV_OBJ_FLAG_CLICKABLE);
        s_cells[k] = l;
    }
    lv_obj_add_event_cb(s_strip, stripEvent, LV_EVENT_ALL, nullptr);
    lv_group_add_obj(g, s_strip);

    // Quick jumps
    jumpBtn(scr, g, "abc", 8, JUMP_LOWER);
    jumpBtn(scr, g, "ABC", 86, JUMP_UPPER);
    jumpBtn(scr, g, "123", 164, JUMP_DIGIT);
    jumpBtn(scr, g, "#+=", 242, JUMP_SYMBOL);

    lv_obj_t *showBtn = Theme::button(scr, LV_SYMBOL_EYE_OPEN " Visa", 148, 44, Theme::LINE);
    lv_obj_set_pos(showBtn, 8, 296);
    s_showBtnLabel = lv_obj_get_child(showBtn, 0);
    lv_obj_add_event_cb(showBtn, [](lv_event_t *) {
        s_showPass = !s_showPass;
        lv_label_set_text(s_showBtnLabel, s_showPass ? LV_SYMBOL_EYE_CLOSE " Dölj" : LV_SYMBOL_EYE_OPEN " Visa");
        renderPass();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *clearBtn = Theme::button(scr, LV_SYMBOL_BACKSPACE " Rensa", 148, 44, Theme::LINE);
    lv_obj_set_pos(clearBtn, 164, 296);
    lv_obj_add_event_cb(clearBtn, [](lv_event_t *) {
        size_t n = strlen(s_pass);
        if (n) s_pass[n - 1] = 0;
        renderPass();
    }, LV_EVENT_CLICKED, nullptr);

    s_hint = Theme::label(scr, "", &otr_font_14, Theme::MUTED);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_hint, 300);
    lv_obj_set_pos(s_hint, 10, 350);
    setHint(Encoder::active() ? "Vrid hjulet för att välja tecken, tryck för att lägga till. "
                                "Långt tryck = till knapparna."
                              : "Tryck på ett tecken för att lägga till det. Dra raden i sidled för fler tecken.",
            false);

    lv_obj_t *connectBtn = Theme::button(scr, LV_SYMBOL_OK " Anslut", 148, 46, Theme::ACCENT, true);
    lv_obj_set_pos(connectBtn, 8, 426);
    lv_obj_add_event_cb(connectBtn, onConnectPressed, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancelBtn = Theme::button(scr, LV_SYMBOL_CLOSE " Avbryt", 148, 46, Theme::MUTED);
    lv_obj_set_pos(cancelBtn, 164, 426);
    lv_obj_add_event_cb(cancelBtn, [](lv_event_t *) {
        memset(s_pass, 0, sizeof(s_pass));
        showNetworks();
    }, LV_EVENT_CLICKED, nullptr);

    lv_group_add_obj(g, connectBtn);
    lv_group_add_obj(g, clearBtn);
    lv_group_add_obj(g, showBtn);
    lv_group_add_obj(g, cancelBtn);
    lv_group_focus_obj(s_strip);
    lv_group_set_editing(g, true);   // Encoder starts on the strip: rotate = step character

    renderStrip();
    renderPass();
    s_timer = lv_timer_create(passTick, 500, nullptr);
    Theme::loadScreen(scr);
}

// ========================================================================================
// Screen 3: connecting
// ========================================================================================
enum class ConnPhase : uint8_t { WAIT, OK, FAIL };
static ConnPhase s_phase;
static uint32_t s_phaseMs, s_uiStartMs, s_attempt;
static lv_obj_t *s_connTitle, *s_connSsid, *s_spinner, *s_resultIcon, *s_connBar, *s_connTime, *s_connMsg,
    *s_connBtnLabel;

static void connResult(bool ok, const char *msg)
{
    s_phase = ok ? ConnPhase::OK : ConnPhase::FAIL;
    s_phaseMs = millis();
    lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_connBar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_connTime, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_resultIcon, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_resultIcon, ok ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(s_resultIcon, Theme::c(ok ? Theme::GREEN : Theme::RED), 0);
    lv_label_set_text(s_connTitle, ok ? "Ansluten!" : "Kunde inte ansluta");
    lv_obj_set_style_text_color(s_connTitle, Theme::c(ok ? Theme::GREEN : Theme::RED), 0);
    lv_label_set_text(s_connMsg, msg);
    lv_label_set_text(s_connBtnLabel, LV_SYMBOL_RIGHT " Fortsätt");
}

static void connNext()
{
    killTimer();
    if (s_phase == ConnPhase::OK) UiYatzy::showMenu();
    else showNetworks();
}

static void connTick(lv_timer_t *)
{
    uint32_t now = millis();
    if (s_phase == ConnPhase::OK) {
        if (now - s_phaseMs > 2000) connNext();
        return;
    }
    if (s_phase == ConnPhase::FAIL) {
        if (now - s_phaseMs > 3000) connNext();
        return;
    }
    if (Wifi::attemptId() != s_attempt) s_attempt = Wifi::attemptId();   // e.g. driver re-try
    Wifi::Status st = Wifi::status();
    if (st == Wifi::Status::CONNECTED) {
        char buf[64];
        snprintf(buf, sizeof(buf), "IP: %s\nhttp://%s.local", Wifi::ip().toString().c_str(), Config::get().hostname);
        connResult(true, buf);
        return;
    }
    if (st == Wifi::Status::FAILED || st == Wifi::Status::IDLE || st == Wifi::Status::ERROR) {
        Wifi::Info wi = Wifi::info();
        connResult(false, wi.lastFail != Wifi::FailReason::NONE ? Wifi::failText(wi.lastFail) : "Okänt fel");
        return;
    }
    uint32_t el = Wifi::connectElapsedMs();
    if (el > Wifi::CONNECT_TIMEOUT_MS) el = Wifi::CONNECT_TIMEOUT_MS;
    uint32_t left = Wifi::CONNECT_TIMEOUT_MS - el;
    lv_bar_set_value(s_connBar, (int32_t)(left / 100), LV_ANIM_OFF);
    lv_label_set_text_fmt(s_connTime, "%lu s", (unsigned long)((left + 999) / 1000));
    if (now - s_uiStartMs > Wifi::CONNECT_TIMEOUT_MS + 3000) {   // Safety net
        Wifi::cancelConnect();
        connResult(false, Wifi::failText(Wifi::FailReason::TIMEOUT));
    }
}

void showConnecting(const char *ssid, const char *pass, bool alreadyStarted)
{
    killTimer();
    char ssidCopy[33];
    snprintf(ssidCopy, sizeof(ssidCopy), "%s", ssid ? ssid : "");

    lv_obj_t *scr = Theme::screen();
    lv_group_t *g = Theme::newScreenGroup(Encoder::setGroup);

    s_connTitle = Theme::label(scr, "Ansluter till", &otr_font_24);
    lv_obj_align(s_connTitle, LV_ALIGN_TOP_MID, 0, 70);
    s_connSsid = Theme::label(scr, ssidCopy, &otr_font_24, Theme::ACCENT);
    lv_label_set_long_mode(s_connSsid, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_connSsid, 300);
    lv_obj_set_style_text_align(s_connSsid, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_connSsid, LV_ALIGN_TOP_MID, 0, 106);

    s_spinner = lv_spinner_create(scr, 1000, 60);
    lv_obj_set_size(s_spinner, 110, 110);
    lv_obj_align(s_spinner, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_set_style_arc_color(s_spinner, Theme::c(Theme::LINE), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_spinner, Theme::c(Theme::BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_spinner, 9, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_spinner, 9, LV_PART_INDICATOR);

    s_resultIcon = Theme::label(scr, LV_SYMBOL_OK, &otr_font_36, Theme::GREEN);
    lv_obj_align(s_resultIcon, LV_ALIGN_TOP_MID, 0, 196);
    lv_obj_add_flag(s_resultIcon, LV_OBJ_FLAG_HIDDEN);

    s_connBar = lv_bar_create(scr);
    lv_obj_set_size(s_connBar, 240, 10);
    lv_obj_align(s_connBar, LV_ALIGN_TOP_MID, 0, 296);
    lv_bar_set_range(s_connBar, 0, Wifi::CONNECT_TIMEOUT_MS / 100);
    lv_bar_set_value(s_connBar, Wifi::CONNECT_TIMEOUT_MS / 100, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_connBar, Theme::c(Theme::PANEL_RAISED), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_connBar, Theme::c(Theme::BLUE), LV_PART_INDICATOR);
    s_connTime = Theme::label(scr, "15 s", &otr_font_14, Theme::MUTED);
    lv_obj_align(s_connTime, LV_ALIGN_TOP_MID, 0, 312);

    s_connMsg = Theme::label(scr, "", &otr_font_18, Theme::TEXT);
    lv_label_set_long_mode(s_connMsg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_connMsg, 300);
    lv_obj_set_style_text_align(s_connMsg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_connMsg, LV_ALIGN_TOP_MID, 0, 340);

    lv_obj_t *btn = Theme::button(scr, LV_SYMBOL_CLOSE " Avbryt", 200, 48, Theme::MUTED);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -12);
    s_connBtnLabel = lv_obj_get_child(btn, 0);
    lv_obj_add_event_cb(btn, [](lv_event_t *) {
        if (s_phase == ConnPhase::WAIT) {
            Wifi::cancelConnect();
            killTimer();
            showNetworks();
        } else {
            connNext();
        }
    }, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(g, btn);

    s_phase = ConnPhase::WAIT;
    s_uiStartMs = millis();
    if (!alreadyStarted) {
        if (!Wifi::connect(ssidCopy, pass ? pass : "")) connResult(false, "Ogiltigt nätverk eller lösenord");
    } else if (Wifi::status() == Wifi::Status::CONNECTING) {
        s_uiStartMs = millis() - Wifi::connectElapsedMs();   // Attempt began during boot
    } else if (Wifi::status() != Wifi::Status::CONNECTED) {
        Wifi::reconnectSaved();                              // Boot attempt already ended: retry once
    }
    s_attempt = Wifi::attemptId();
    memset(s_pass, 0, sizeof(s_pass));   // Wifi::connect() keeps its own copy
    s_timer = lv_timer_create(connTick, 100, nullptr);
    connTick(s_timer);                   // Immediate state (e.g. already connected)
    Theme::loadScreen(scr);
}

}  // namespace UiWifi
