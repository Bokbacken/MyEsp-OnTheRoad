// ui_wifi.cpp - Wi-Fi network list, password entry and connecting screens (see header).
#include "ui_wifi.h"

#include "my_sys.h"
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "my_wifi.h"
#include "my_charwheel.h"
#include "lang.h"
#include "my_theme.h"
#include "ui_yatzy.h"

// Extra FontAwesome glyphs included in my_font_14/18/24.
#define SYM_LOCK   "\xEF\x80\xA3"   // U+F023
#define SYM_UNLOCK "\xEF\x8F\x81"   // U+F3C1

namespace UiWifi {

// One timer for whichever Wi-Fi screen is active. Killed before every navigation so it
// never touches objects of a deleted screen.
static lv_timer_t *s_timer = nullptr;

static void killTimer()
{
    if (s_timer) {
        lv_timer_delete(s_timer);
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
    lv_obj_set_scrollable(box, false);
    lv_obj_set_clickable(box, false);
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
        lv_obj_set_clickable(b, false);
    }
    return box;
}

// ========================================================================================
// Screen 1: network list
// ========================================================================================
static MyWifi::Network s_nets[MyWifi::MAX_NETWORKS];
static int s_netCount = 0;
static lv_obj_t *s_list, *s_busy, *s_busyLabel, *s_netStatus, *s_rescanBtn;
static uint32_t s_scanStartMs;

static void listStatusLine()
{
    MyWifi::Info wi = MyWifi::info();
    char buf[96];
    if (wi.connected) snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %s  \xE2\x80\xA2  %s", wi.ssid, wi.ip);
    else if (wi.status == MyWifi::Status::CONNECTING) snprintf(buf, sizeof(buf), tr(Str::WIFI_CONNECTING_TO_FMT), wi.ssid);
    else if (wi.status == MyWifi::Status::SEARCHING) snprintf(buf, sizeof(buf), "%s", tr(Str::WIFI_SEARCHING_SAVED));
    else if (MyWifi::hasSavedCredentials()) snprintf(buf, sizeof(buf), "%s", tr(Str::WIFI_NOT_CONNECTED_FORGET));
    else snprintf(buf, sizeof(buf), "%s", tr(Str::WIFI_NOT_CONNECTED));
    lv_label_set_text(s_netStatus, buf);
    lv_obj_set_style_text_color(s_netStatus, Theme::c(wi.connected ? Theme::GREEN : Theme::MUTED), 0);
}

static void startListScan();
static bool s_longPressed = false;
static char s_forgetSsid[33];

static void forgetDialog(const char *ssid)
{
    snprintf(s_forgetSsid, sizeof(s_forgetSsid), "%s", ssid);
    char txt[96];
    snprintf(txt, sizeof(txt), tr(Str::WIFI_FORGET_Q), ssid);
    lv_obj_t *mb = lv_msgbox_create(nullptr);
    lv_msgbox_add_title(mb, tr(Str::WIFI_SAVED_NETWORK));
    lv_msgbox_add_text(mb, txt);
    lv_obj_set_width(mb, 280);
    lv_obj_center(mb);
    lv_obj_set_style_bg_color(mb, Theme::c(Theme::PANEL), 0);
    lv_obj_set_style_text_color(mb, Theme::c(Theme::TEXT), 0);
    lv_obj_set_style_text_font(mb, &my_font_18, 0);
    lv_obj_t *bForget = lv_msgbox_add_footer_button(mb, tr(Str::WIFI_FORGET));
    lv_obj_t *bCancel = lv_msgbox_add_footer_button(mb, tr(Str::BTN_CANCEL));
    Theme::msgboxFonts(mb);
    lv_obj_add_event_cb(bForget, [](lv_event_t *e) {
        lv_msgbox_close((lv_obj_t *)lv_event_get_user_data(e));
        MyWifi::savedRemove(s_forgetSsid);
        startListScan();
    }, LV_EVENT_CLICKED, mb);
    lv_obj_add_event_cb(bCancel, [](lv_event_t *e) {
        lv_msgbox_close((lv_obj_t *)lv_event_get_user_data(e));
    }, LV_EVENT_CLICKED, mb);
}

static void rowEvent(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= s_netCount) return;
    if (code == LV_EVENT_PRESSED) {
        s_longPressed = false;
    } else if (code == LV_EVENT_LONG_PRESSED) {
        NetList *nl = (NetList *)lv_malloc(sizeof(NetList));
        bool saved = false;
        if (nl) { MyWifi::savedCopy(*nl); saved = nl->find(s_nets[i].ssid) >= 0; memset(nl, 0, sizeof(NetList)); lv_free(nl); }
        if (saved) { s_longPressed = true; forgetDialog(s_nets[i].ssid); }
    } else if (code == LV_EVENT_CLICKED && !s_longPressed) {
        killTimer();
        if (s_nets[i].secure) showPassword(s_nets[i].ssid);
        else showConnecting(s_nets[i].ssid, "");
    }
}

static void showBusy(const char *text, bool spinner)
{
    lv_obj_clean(s_list);
    lv_obj_set_hidden(s_busy, false);
    lv_obj_t *sp = lv_obj_get_child(s_busy, 0);
    if (spinner) lv_obj_set_hidden(sp, false);
    else lv_obj_set_hidden(sp, true);
    lv_label_set_text(s_busyLabel, text);
}

static void fillList()
{
    s_netCount = MyWifi::scanResults(s_nets, MyWifi::MAX_NETWORKS);
    lv_obj_clean(s_list);
    if (s_netCount == 0) {
        showBusy(tr(Str::WIFI_NONE_FOUND), false);
        return;
    }
    lv_obj_set_hidden(s_busy, true);
    MyWifi::Info wi = MyWifi::info();
    int savedIdx[MyWifi::MAX_NETWORKS];
    {
        NetList *nl = (NetList *)lv_malloc(sizeof(NetList));   // ~5 kB, keep it off the stack
        for (int i = 0; i < s_netCount; i++) savedIdx[i] = -1;
        if (nl) {
            MyWifi::savedCopy(*nl);
            for (int i = 0; i < s_netCount; i++) savedIdx[i] = nl->find(s_nets[i].ssid);
            memset(nl, 0, sizeof(NetList));
            lv_free(nl);
        }
    }
    for (int i = 0; i < s_netCount; i++) {
        const MyWifi::Network &n = s_nets[i];
        lv_obj_t *row = lv_button_create(s_list);
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
        lv_obj_set_scroll_on_focus(row, true);
        lv_obj_set_scrollable(row, false);

        if (savedIdx[i] >= 0) {   // Saved network: priority number on the left
            char pr[12];
            snprintf(pr, sizeof(pr), "%d", savedIdx[i] + 1);
            lv_obj_t *badge = Theme::label(row, pr, &my_font_14, Theme::GOLD);
            lv_obj_align(badge, LV_ALIGN_LEFT_MID, 6, 0);
        }

        bool isCurrent = wi.connected && strcmp(wi.ssid, n.ssid) == 0;
        char name[48];
        snprintf(name, sizeof(name), "%s%s", n.ssid, isCurrent ? " " LV_SYMBOL_OK : "");
        lv_obj_t *l = Theme::label(row, name, &my_font_18, isCurrent ? Theme::GREEN : Theme::TEXT);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(l, 178);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 24, 0);

        lv_obj_t *sec = n.secure ? Theme::label(row, SYM_LOCK, &my_font_14, Theme::MUTED)
                                 : Theme::label(row, trp(SYM_UNLOCK " ", Str::WIFI_OPEN), &my_font_14, Theme::GREEN);
        lv_obj_align(sec, LV_ALIGN_RIGHT_MID, -44, 0);

        lv_obj_t *bars = signalBars(row, MyWifi::signalBars(n.rssi));
        lv_obj_align(bars, LV_ALIGN_RIGHT_MID, -8, 0);

        lv_obj_add_event_cb(row, rowEvent, LV_EVENT_ALL, (void *)(intptr_t)i);
    }
}

static void startListScan()
{
    showBusy(tr(Str::WIFI_SCANNING), true);
    s_scanStartMs = MySys::millis();
    MyWifi::startScan();
}

static void listTick(lv_timer_t *)
{
    listStatusLine();
    if (lv_obj_is_hidden(s_busy)) return;   // List is shown
    lv_obj_t *sp = lv_obj_get_child(s_busy, 0);
    if (lv_obj_is_hidden(sp)) return;       // Final message shown
    int st = MyWifi::scanStatus();
    if (st >= 0) {
        fillList();
    } else if (st == -1 || MySys::millis() - s_scanStartMs > 12000) {
        showBusy(tr(Str::WIFI_SCAN_FAILED), false);
    }
}

void showNetworks()
{
    killTimer();
    lv_obj_t *scr = Theme::screen();
    Theme::header(scr, tr(Str::WIFI_CHOOSE));

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
    lv_obj_set_scrollable(s_busy, false);
    lv_obj_set_clickable(s_busy, false);
    lv_obj_t *sp = lv_spinner_create(s_busy);
    lv_spinner_set_anim_params(sp, 1000, 60);                       // child 0
    lv_obj_set_size(sp, 64, 64);
    lv_obj_align(sp, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_arc_color(sp, Theme::c(Theme::LINE), LV_PART_MAIN);
    lv_obj_set_style_arc_color(sp, Theme::c(Theme::ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(sp, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(sp, 6, LV_PART_INDICATOR);
    s_busyLabel = Theme::label(s_busy, "", &my_font_18, Theme::MUTED);
    lv_obj_set_width(s_busyLabel, 300);
    lv_label_set_long_mode(s_busyLabel, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_busyLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_busyLabel, LV_ALIGN_TOP_MID, 0, 80);

    s_netStatus = Theme::label(scr, "", &my_font_14, Theme::MUTED);
    lv_label_set_long_mode(s_netStatus, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_netStatus, 304);
    lv_obj_set_pos(s_netStatus, 10, 400);

    s_rescanBtn = Theme::button(scr, trp(LV_SYMBOL_REFRESH " ", Str::WIFI_REFRESH), 148, 46, Theme::ACCENT);
    lv_obj_set_pos(s_rescanBtn, 8, 426);
    lv_obj_add_event_cb(s_rescanBtn, [](lv_event_t *) { startListScan(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancel = Theme::button(scr, trp(LV_SYMBOL_CLOSE " ", Str::BTN_CANCEL), 148, 46, Theme::MUTED);
    lv_obj_set_pos(cancel, 164, 426);
    lv_obj_add_event_cb(cancel, [](lv_event_t *) { toMenu(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_user_data(s_rescanBtn, cancel);

    startListScan();
    listStatusLine();
    s_timer = lv_timer_create(listTick, 200, nullptr);
    Theme::loadScreen(scr);
}

// ========================================================================================
// Screen 2: password entry with the on-screen scroll wheel (CharWheel / mypi_kbd_core)
// ========================================================================================
static constexpr size_t PASS_MAX = 64;
static char s_ssid[33];
static char s_pass[PASS_MAX + 1];

// Navigation after the wheel closed: the wheel deletes itself asynchronously, so leave the
// screen from a one-shot timer that runs after that deletion.
static void navLater(void (*fn)())
{
    lv_timer_t *t = lv_timer_create([](lv_timer_t *tm) {
        void (*f)() = (void (*)())lv_timer_get_user_data(tm);
        lv_timer_delete(tm);
        f();
    }, 40, (void *)fn);
    lv_timer_set_repeat_count(t, 1);
}

static void passEntered(const char *text, bool confirmed, void *user)
{
    lv_obj_t *wheel = (lv_obj_t *)user;
    if (!confirmed) {   // BACK: the wheel closes itself
        memset(s_pass, 0, sizeof(s_pass));
        navLater(showNetworks);
        return;
    }
    if (!text[0]) return CharWheel::setHint(wheel, tr(Str::WIFI_ENTER_PASSWORD));
    if (!Config::validWifiPass(text, false)) {
        size_t n = strlen(text);
        CharWheel::setHint(wheel, tr(n < 8 ? Str::WIFI_MIN8 : n == 64 ? Str::WIFI_HEX64 : Str::WIFI_MAX63));
        return;
    }
    snprintf(s_pass, sizeof(s_pass), "%s", text);
    CharWheel::close(wheel);
    navLater([]() { showConnecting(s_ssid, s_pass); });
}

void showPassword(const char *ssid)
{
    killTimer();
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid ? ssid : "");
    memset(s_pass, 0, sizeof(s_pass));
    MyWifi::savedPassword(s_ssid, s_pass, sizeof(s_pass));   // Pre-fill a saved network

    lv_obj_t *scr = Theme::screen();
    char label[64];
    snprintf(label, sizeof(label), trp(SYM_LOCK " ", Str::WIFI_PASSWORD_FOR), s_ssid);
    // Wheel user data = the wheel itself (needed for setHint/close in the callback).
    static lv_obj_t *s_wheel;
    s_wheel = CharWheel::create(scr, 0, 0, 320, 480, label, s_pass, PASS_MAX, true,
                                [](const char *t, bool ok, void *) { passEntered(t, ok, s_wheel); }, nullptr);
    memset(s_pass, 0, sizeof(s_pass));
    if (s_wheel && MyWifi::hasSavedCredentials() && CharWheel::text(s_wheel)[0])
        CharWheel::setHint(s_wheel, tr(Str::WIFI_SAVED_FILLED));
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
    s_phaseMs = MySys::millis();
    lv_obj_set_hidden(s_spinner, true);
    lv_obj_set_hidden(s_connBar, true);
    lv_obj_set_hidden(s_connTime, true);
    lv_obj_set_hidden(s_resultIcon, false);
    lv_label_set_text(s_resultIcon, ok ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(s_resultIcon, Theme::c(ok ? Theme::GREEN : Theme::RED), 0);
    lv_label_set_text(s_connTitle, tr(ok ? Str::WIFI_CONNECTED : Str::WIFI_CONNECT_FAILED));
    lv_obj_set_style_text_color(s_connTitle, Theme::c(ok ? Theme::GREEN : Theme::RED), 0);
    lv_label_set_text(s_connMsg, msg);
    lv_label_set_text(s_connBtnLabel, trp(LV_SYMBOL_RIGHT " ", Str::BTN_CONTINUE));
}

static void connNext()
{
    killTimer();
    if (s_phase == ConnPhase::OK) UiYatzy::showMenu();
    else showNetworks();
}

static void connTick(lv_timer_t *)
{
    uint32_t now = MySys::millis();
    if (s_phase == ConnPhase::OK) {
        if (now - s_phaseMs > 2000) connNext();
        return;
    }
    if (s_phase == ConnPhase::FAIL) {
        if (now - s_phaseMs > 3000) connNext();
        return;
    }
    if (MyWifi::attemptId() != s_attempt) {   // Driver re-try or next saved network
        s_attempt = MyWifi::attemptId();
        s_uiStartMs = now;
        lv_label_set_text(s_connSsid, MyWifi::info().ssid);
    }
    MyWifi::Status st = MyWifi::status();
    if (st == MyWifi::Status::SEARCHING) {
        lv_label_set_text(s_connSsid, tr(Str::WIFI_SAVED_NETWORK_DOTS));
        lv_label_set_text(s_connTime, tr(Str::WIFI_SEARCHING));
        s_uiStartMs = now;
        return;
    }
    if (st == MyWifi::Status::CONNECTED) {
        char buf[64];
        snprintf(buf, sizeof(buf), "IP: %s\nhttp://%s.local", MyWifi::ipStr(), Config::get().hostname);
        connResult(true, buf);
        return;
    }
    if (st == MyWifi::Status::FAILED || st == MyWifi::Status::IDLE || st == MyWifi::Status::ERROR) {
        MyWifi::Info wi = MyWifi::info();
        connResult(false, wi.lastFail != MyWifi::FailReason::NONE ? MyWifi::failText(wi.lastFail) : tr(Str::WIFI_UNKNOWN_ERROR));
        return;
    }
    uint32_t el = MyWifi::connectElapsedMs();
    if (el > MyWifi::CONNECT_TIMEOUT_MS) el = MyWifi::CONNECT_TIMEOUT_MS;
    uint32_t left = MyWifi::CONNECT_TIMEOUT_MS - el;
    lv_bar_set_value(s_connBar, (int32_t)(left / 100), LV_ANIM_OFF);
    lv_label_set_text_fmt(s_connTime, "%lu s", (unsigned long)((left + 999) / 1000));
    if (now - s_uiStartMs > MyWifi::CONNECT_TIMEOUT_MS + 3000) {   // Safety net
        MyWifi::cancelConnect();
        connResult(false, MyWifi::failText(MyWifi::FailReason::TIMEOUT));
    }
}

void showConnecting(const char *ssid, const char *pass, bool alreadyStarted)
{
    killTimer();
    char ssidCopy[33];
    snprintf(ssidCopy, sizeof(ssidCopy), "%s", ssid ? ssid : "");

    lv_obj_t *scr = Theme::screen();

    s_connTitle = Theme::label(scr, tr(Str::WIFI_CONNECTING_TO), &my_font_24);
    lv_obj_align(s_connTitle, LV_ALIGN_TOP_MID, 0, 70);
    s_connSsid = Theme::label(scr, ssidCopy, &my_font_24, Theme::ACCENT);
    lv_label_set_long_mode(s_connSsid, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_connSsid, 300);
    lv_obj_set_style_text_align(s_connSsid, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_connSsid, LV_ALIGN_TOP_MID, 0, 106);

    s_spinner = lv_spinner_create(scr);
    lv_spinner_set_anim_params(s_spinner, 1000, 60);
    lv_obj_set_size(s_spinner, 110, 110);
    lv_obj_align(s_spinner, LV_ALIGN_TOP_MID, 0, 160);
    lv_obj_set_style_arc_color(s_spinner, Theme::c(Theme::LINE), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_spinner, Theme::c(Theme::BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(s_spinner, 9, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_spinner, 9, LV_PART_INDICATOR);

    s_resultIcon = Theme::label(scr, LV_SYMBOL_OK, &my_font_36, Theme::GREEN);
    lv_obj_align(s_resultIcon, LV_ALIGN_TOP_MID, 0, 196);
    lv_obj_set_hidden(s_resultIcon, true);

    s_connBar = lv_bar_create(scr);
    lv_obj_set_size(s_connBar, 240, 10);
    lv_obj_align(s_connBar, LV_ALIGN_TOP_MID, 0, 296);
    lv_bar_set_range(s_connBar, 0, MyWifi::CONNECT_TIMEOUT_MS / 100);
    lv_bar_set_value(s_connBar, MyWifi::CONNECT_TIMEOUT_MS / 100, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_connBar, Theme::c(Theme::PANEL_RAISED), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_connBar, Theme::c(Theme::BLUE), LV_PART_INDICATOR);
    s_connTime = Theme::label(scr, "15 s", &my_font_14, Theme::MUTED);
    lv_obj_align(s_connTime, LV_ALIGN_TOP_MID, 0, 312);

    s_connMsg = Theme::label(scr, "", &my_font_18, Theme::TEXT);
    lv_label_set_long_mode(s_connMsg, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s_connMsg, 300);
    lv_obj_set_style_text_align(s_connMsg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_connMsg, LV_ALIGN_TOP_MID, 0, 340);

    lv_obj_t *btn = Theme::button(scr, trp(LV_SYMBOL_CLOSE " ", Str::BTN_CANCEL), 200, 48, Theme::MUTED);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -12);
    s_connBtnLabel = lv_obj_get_child(btn, 0);
    lv_obj_add_event_cb(btn, [](lv_event_t *) {
        if (s_phase == ConnPhase::WAIT) {
            MyWifi::cancelConnect();
            killTimer();
            showNetworks();
        } else {
            connNext();
        }
    }, LV_EVENT_CLICKED, nullptr);

    s_phase = ConnPhase::WAIT;
    s_uiStartMs = MySys::millis();
    if (!alreadyStarted) {
        if (!MyWifi::connect(ssidCopy, pass ? pass : "")) connResult(false, tr(Str::WIFI_INVALID));
    } else if (MyWifi::status() == MyWifi::Status::CONNECTING) {
        s_uiStartMs = MySys::millis() - MyWifi::connectElapsedMs();   // Attempt began during boot
    } else if (MyWifi::status() != MyWifi::Status::CONNECTED && MyWifi::status() != MyWifi::Status::SEARCHING) {
        MyWifi::reconnectSaved();                              // Boot sequence already ended: retry once
    }
    s_attempt = MyWifi::attemptId();
    memset(s_pass, 0, sizeof(s_pass));   // MyWifi::connect() keeps its own copy
    if (alreadyStarted && MyWifi::status() == MyWifi::Status::SEARCHING) lv_label_set_text(s_connTitle, tr(Str::WIFI_AUTO_CONNECT));
    s_timer = lv_timer_create(connTick, 100, nullptr);
    connTick(s_timer);                   // Immediate state (e.g. already connected)
    Theme::loadScreen(scr);
}

}  // namespace UiWifi
