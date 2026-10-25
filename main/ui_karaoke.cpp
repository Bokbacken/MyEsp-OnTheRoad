// ui_karaoke.cpp - see ui_karaoke.h.
#include "ui_karaoke.h"

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "app_config.h"
#include "board_audio.h"
#include "board_display.h"
#include "kara_web.h"
#include "karaoke.h"
#include "lang.h"
#include "my_lvgl.h"
#include "my_theme.h"
#include "ui_yatzy.h"

namespace UiKaraoke {

namespace {

constexpr uint32_t TICK_MS = 40;
constexpr uint32_t DEBOUNCE_MS = 500;
// v0.13.1: the karaoke module runs in landscape (LVGL software rotation), 480 x 320.
constexpr int32_t W = 480, H = 320;

std::vector<Karaoke::SongFile> s_files;
int s_current = -1;

// List screen
lv_obj_t *s_listScr = nullptr;
lv_obj_t *s_fontLbl = nullptr;
lv_timer_t *s_listTimer = nullptr;

// Bottom sheet (settings, tracks, sound bank) on the active screen
lv_obj_t *s_sheet = nullptr;
std::vector<Karaoke::FontFile> s_fonts;
bool s_fontForSong = false;

// Player screen
struct Player {
    lv_obj_t *scr, *prev, *cur, *next, *next2, *slider, *timeLbl, *playBtn, *playLbl, *melBtnLbl, *diag, *fontLbl;
    lv_span_t *spSung, *spRest;
    Karaoke::FontState fontState;
    lv_timer_t *timer;
    int line, syl;
    uint32_t lastSec;
    bool dragging, finished;
};
Player P{};

void showPlayer();
void showFonts(bool forSong);

void leaveToMenu()
{
    Display::setLandscape(false);   // Back to portrait (keeps the 180-degree setting)
    UiYatzy::showMenu();
}

void fmtTime(char *buf, size_t cap, uint32_t us)
{
    const uint32_t s = us / 1000000;
    snprintf(buf, cap, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

lv_obj_t *backButton(lv_obj_t *scr)
{
    lv_obj_t *b = Theme::button(scr, LV_SYMBOL_LEFT, 52, 36, Theme::LINE);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -6, 4);
    return b;
}

// ---- Song list ---------------------------------------------------------------------------------
void updateFontLabel()
{
    if (!s_fontLbl) return;
    switch (Karaoke::fontState()) {
    case Karaoke::FontState::LOADING:
        lv_label_set_text(s_fontLbl, tr(Str::KARA_FONT_LOADING));
        lv_obj_set_style_text_color(s_fontLbl, Theme::c(Theme::GOLD), 0);
        break;
    case Karaoke::FontState::READY: {
        char buf[96];
        snprintf(buf, sizeof(buf), tr(Str::KARA_FONT_READY), Karaoke::fontName());
        lv_label_set_text(s_fontLbl, buf);
        lv_obj_set_style_text_color(s_fontLbl, Theme::c(Theme::GREEN), 0);
        break;
    }
    case Karaoke::FontState::FAILED: {
        char buf[160];
        snprintf(buf, sizeof(buf), "%s\n%s", Karaoke::errText(Karaoke::fontError()), tr(Str::KARA_SILENT));
        lv_label_set_text(s_fontLbl, buf);
        lv_obj_set_style_text_color(s_fontLbl, Theme::c(Theme::RED), 0);
        break;
    }
    default: lv_label_set_text(s_fontLbl, ""); break;
    }
}

void msg(const char *text)
{
    lv_obj_t *m = lv_msgbox_create(nullptr);
    lv_msgbox_add_text(m, text);
    lv_msgbox_add_close_button(m);
    Theme::msgboxFonts(m);
}

void openSong(int idx)
{
    if (idx < 0 || idx >= (int)s_files.size()) return;
    if (Karaoke::fontState() == Karaoke::FontState::LOADING) {
        msg(tr(Str::KARA_WAIT_FONT));
        return;
    }
    Karaoke::Err e = Karaoke::open(s_files[(size_t)idx]);
    if (e != Karaoke::Err::OK) {
        msg(Karaoke::errText(e));
        return;
    }
    s_current = idx;
    showPlayer();
}

void listDeleted(lv_event_t *e)
{
    if (lv_event_get_target(e) != s_listScr) return;   // An older list screen fading out
    s_listScr = nullptr;
    if (s_listTimer) lv_timer_delete(s_listTimer);
    s_listTimer = nullptr;
    s_fontLbl = nullptr;
}

void showList()
{
    Audio::music(Sound::Music::NONE);
    if (s_listTimer) lv_timer_delete(s_listTimer);
    s_listTimer = nullptr;
    lv_obj_t *scr = s_listScr = Theme::screen();
    Theme::header(scr, tr(Str::MENU_KARAOKE));
    static uint32_t backLast = 0;
    lv_obj_add_event_cb(backButton(scr), [](lv_event_t *) {
        if (!Theme::debounce(backLast, DEBOUNCE_MS)) return;
        if (Karaoke::fontState() == Karaoke::FontState::LOADING) {
            msg(tr(Str::KARA_WAIT_FONT));
            return;
        }
        Karaoke::unloadFont();
        leaveToMenu();
    }, LV_EVENT_CLICKED, nullptr);

    s_fontLbl = Theme::label(scr, "", &my_font_14, Theme::MUTED);
    lv_obj_set_width(s_fontLbl, W - 140);
    lv_label_set_long_mode(s_fontLbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_pos(s_fontLbl, 10, 50);
    static uint32_t fontLast = 0;
    lv_obj_t *fb = Theme::button(scr, tr(Str::KARA_BTN_FONT), 118, 36, Theme::TEAL);
    lv_obj_set_pos(fb, W - 126, 50);
    lv_obj_add_event_cb(fb, [](lv_event_t *) {
        if (Theme::debounce(fontLast, DEBOUNCE_MS)) showFonts(false);
    }, LV_EVENT_CLICKED, nullptr);
    // Load the default font only when none is loaded; a song font from the last song stays until a
    // song wants another one (saves a few seconds of loading).
    const Karaoke::FontState fs = Karaoke::fontState();
    if (fs == Karaoke::FontState::NONE || fs == Karaoke::FontState::FAILED) Karaoke::startLoadFont();
    updateFontLabel();

    Karaoke::Err e = Karaoke::scan(s_files);
    lv_obj_t *info = Theme::label(scr, "", &my_font_14, Theme::MUTED);
    lv_obj_set_pos(info, 10, 90);
    lv_obj_set_width(info, W - 140);
    lv_label_set_long_mode(info, LV_LABEL_LONG_MODE_WRAP);
    if (e != Karaoke::Err::OK) {
        lv_label_set_text(info, Karaoke::errText(e));
    } else if (s_files.empty()) {
        lv_label_set_text(info, tr(Str::KARA_NO_SONGS));
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), tr(Str::KARA_SONGS), (unsigned)s_files.size());
        lv_label_set_text(info, buf);
    }

    lv_obj_t *list = lv_obj_create(scr);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, W, H - 112);
    lv_obj_set_pos(list, 0, 112);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_ROW_WRAP);   // Two columns in landscape
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_style_pad_column(list, 8, 0);
    lv_obj_set_style_pad_hor(list, 10, 0);
    lv_obj_set_style_pad_bottom(list, 10, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    for (size_t i = 0; i < s_files.size(); i++) {
        const Karaoke::SongFile &f = s_files[i];
        lv_obj_t *b = lv_button_create(list);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, (W - 28) / 2, 58);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_bg_color(b, Theme::c(Theme::PANEL), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(b, (int)i == s_current ? 2 : 1, 0);
        lv_obj_set_style_border_color(b, Theme::c((int)i == s_current ? Theme::ACCENT : Theme::LINE), 0);
        lv_obj_set_style_bg_color(b, Theme::c(Theme::PANEL_RAISED), LV_STATE_PRESSED);
        lv_obj_t *t = Theme::label(b, f.title.c_str(), &my_font_18);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(t, (W - 28) / 2 - 20);
        lv_obj_set_pos(t, 10, f.artist.empty() ? 18 : 10);
        if (!f.artist.empty()) {
            lv_obj_t *a = Theme::label(b, f.artist.c_str(), &my_font_14, Theme::MUTED);
            lv_label_set_long_mode(a, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_width(a, (W - 28) / 2 - 20);
            lv_obj_set_pos(a, 10, 34);
        }
        static uint32_t openLast = 0;
        lv_obj_add_event_cb(b, [](lv_event_t *ev) {
            if (!Theme::debounce(openLast, 600)) return;
            openSong((int)(intptr_t)lv_event_get_user_data(ev));
        }, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    s_listTimer = lv_timer_create([](lv_timer_t *) { updateFontLabel(); }, 250, nullptr);
    lv_obj_add_event_cb(scr, listDeleted, LV_EVENT_DELETE, nullptr);
    Theme::loadScreen(scr);
}

// ---- Player ------------------------------------------------------------------------------------
void setLine(lv_obj_t *l, int line)
{
    const Midi::Song *s = Karaoke::song();
    lv_label_set_text(l, s && line >= 0 && line < (int)s->lines.size() ? s->lineText(line).c_str() : "");
}

void updateLyrics(const Karaoke::View &v, bool force)
{
    const Midi::Song *s = Karaoke::song();
    if (!s) return;
    if (s->lines.empty()) {
        if (force) {
            lv_spangroup_set_span_text(P.cur, P.spSung, "");
            lv_spangroup_set_span_text(P.cur, P.spRest, tr(Str::KARA_NO_LYRICS));
        }
        return;
    }
    // Before the first line: show what is coming
    int line = v.line;
    if (line != P.line || force) {
        P.line = line;
        setLine(P.prev, line - 1);
        setLine(P.next, line + 1);
        setLine(P.next2, line + 2);
        P.syl = -2;
    }
    if (v.syl != P.syl || force) {
        P.syl = v.syl;
        std::string sung, rest;
        if (line < 0) {
            s->lineSplit(0, -1, sung, rest);   // First line, nothing sung yet
            setLine(P.next, 1);
            setLine(P.next2, 2);
        } else {
            s->lineSplit(line, v.syl + 1, sung, rest);
        }
        lv_spangroup_set_span_text(P.cur, P.spSung, sung.c_str());
        lv_spangroup_set_span_text(P.cur, P.spRest, rest.c_str());
        lv_spangroup_refresh(P.cur);
    }
}

void updatePlayLabel(bool playing)
{
    lv_label_set_text(P.playLbl, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

void updatePlayerFont()
{
    if (!P.fontLbl) return;
    char buf[80];
    switch (Karaoke::fontState()) {
    case Karaoke::FontState::LOADING: snprintf(buf, sizeof(buf), "%s", tr(Str::KARA_FONT_LOADING)); break;
    case Karaoke::FontState::READY: snprintf(buf, sizeof(buf), LV_SYMBOL_AUDIO " %.60s", Karaoke::fontName()); break;
    case Karaoke::FontState::FAILED: snprintf(buf, sizeof(buf), "%s", Karaoke::errText(Karaoke::fontError())); break;
    default: buf[0] = 0; break;
    }
    lv_label_set_text(P.fontLbl, buf);
    const Karaoke::FontState st = Karaoke::fontState();
    lv_obj_set_style_text_color(P.fontLbl, Theme::c(st == Karaoke::FontState::LOADING ? Theme::GOLD
                                                    : st == Karaoke::FontState::FAILED ? Theme::RED : Theme::MUTED), 0);
}

void refreshSteppers();

void tick(lv_timer_t *)
{
    // The song wants another sound bank (per-song choice, or the default changed): reload
    if (Karaoke::fontWanted()) Karaoke::ensureFont(false);
    // v0.13.4: a new key needed more samples than fit; the previous key is back
    if (Karaoke::takeTransposeRejected()) {
        refreshSteppers();
        msg(tr(Str::KARA_TR_REJECTED));
    }
    if (Karaoke::fontState() != P.fontState) {
        P.fontState = Karaoke::fontState();
        updatePlayerFont();
        P.lastSec = 0xFFFFFFFF;   // Refresh the diagnostics line
        P.line = -3;
    }
    const Karaoke::View v = Karaoke::view();
    updateLyrics(v, false);
    const uint32_t sec = v.posUs / 1000000;
    if (sec != P.lastSec || v.finished != P.finished) {
        P.lastSec = sec;
        P.finished = v.finished;
        char a[12], b[12], buf[128];
        fmtTime(a, sizeof(a), v.posUs);
        fmtTime(b, sizeof(b), v.lengthUs);
        snprintf(buf, sizeof(buf), "%s / %s%s%s", a, b, v.finished ? "  " : "", v.finished ? tr(Str::KARA_END) : "");
        lv_label_set_text(P.timeLbl, buf);
        if (!P.dragging && v.lengthUs) lv_slider_set_value(P.slider, (int32_t)(v.posUs / 100000), LV_ANIM_OFF);
        updatePlayLabel(v.playing);
        if (Karaoke::fontState() == Karaoke::FontState::READY)
            snprintf(buf, sizeof(buf), "%d/%d  %u%%", v.voices, v.maxVoices, (unsigned)v.cpuPct);
        else
            snprintf(buf, sizeof(buf), "%s", tr(Str::KARA_SILENT));
        if (Audio::webStream()) {   // v0.13.5: the browser plays the sound, the speaker is muted
            const size_t used = strlen(buf);
            snprintf(buf + used, sizeof(buf) - used, "  |  ");
            const size_t used2 = strlen(buf);
            snprintf(buf + used2, sizeof(buf) - used2, tr(Str::KARA_WEB_ACTIVE), KaraWeb::clients());
        }
        lv_label_set_text(P.diag, buf);
    }
}

void playerDeleted(lv_event_t *e)
{
    if (lv_event_get_target(e) != P.scr) return;
    if (P.timer) lv_timer_delete(P.timer);
    P = Player{};
}

lv_obj_t *ctlButton(lv_obj_t *parent, const char *text, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color,
                    lv_event_cb_t cb, void *ud = nullptr)
{
    lv_obj_t *b = Theme::button(parent, text, w, h, color);
    lv_obj_set_pos(b, x, y);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

// ---- Settings sheet (tempo, key, volume) -------------------------------------------------------
void closeSheet()
{
    if (s_sheet) lv_obj_delete(s_sheet);   // The DELETE callback clears s_sheet
}

lv_obj_t *sheetBase(const char *title, int32_t h)
{
    closeSheet();
    s_sheet = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_sheet);
    lv_obj_set_size(s_sheet, W, H);
    lv_obj_set_style_bg_color(s_sheet, Theme::c(0x000000), 0);
    lv_obj_set_style_bg_opa(s_sheet, LV_OPA_60, 0);
    lv_obj_set_clickable(s_sheet, true);   // Swallow taps behind the card
    lv_obj_add_event_cb(s_sheet, [](lv_event_t *e) {
        if (lv_event_get_target(e) == s_sheet) s_sheet = nullptr;
    }, LV_EVENT_DELETE, nullptr);
    lv_obj_t *c = Theme::card(s_sheet, W - 16, h);
    lv_obj_align(c, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_scrollable(c, false);
    lv_obj_t *t = Theme::label(c, title, &my_font_24);
    lv_obj_set_pos(t, 4, 2);
    static uint32_t closeLast = 0;
    lv_obj_t *x = Theme::button(c, LV_SYMBOL_CLOSE, 44, 36, Theme::LINE);
    lv_obj_align(x, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(x, [](lv_event_t *) {
        if (Theme::debounce(closeLast, DEBOUNCE_MS)) lv_async_call([](void *) { closeSheet(); }, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    return c;
}

struct Stepper {
    lv_obj_t *value;
    int kind;   // 0 tempo, 1 key, 2 volume
};
Stepper s_steps[3];

void refreshSteppers()
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d %%", Karaoke::tempo());
    if (s_steps[0].value) lv_label_set_text(s_steps[0].value, buf);
    snprintf(buf, sizeof(buf), "%+d", Karaoke::transpose());
    if (s_steps[1].value) lv_label_set_text(s_steps[1].value, buf);
    snprintf(buf, sizeof(buf), "%u", (unsigned)Karaoke::volume());
    if (s_steps[2].value) lv_label_set_text(s_steps[2].value, buf);
}

void stepCb(lv_event_t *e)
{
    static uint32_t last[6] = {};
    const int code = (int)(intptr_t)lv_event_get_user_data(e);   // kind * 2 + (0 = minus, 1 = plus)
    if (!Theme::debounce(last[code], 200)) return;
    const int dir = (code & 1) ? 1 : -1;
    switch (code / 2) {
    case 0: Karaoke::setTempo(Karaoke::tempo() + dir * 5); break;
    case 1: Karaoke::setTranspose(Karaoke::transpose() + dir); break;
    case 2: {
        int v = Karaoke::volume() + dir * 10;
        Karaoke::setVolume((uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v));
        break;
    }
    }
    refreshSteppers();
}

void showSettings()
{
    lv_obj_t *c = sheetBase(tr(Str::KARA_BTN_SETTINGS), 262);
    const Str names[3] = {Str::KARA_TEMPO, Str::KARA_KEY, Str::KARA_VOLUME};
    for (int k = 0; k < 3; k++) {
        const int32_t y = 44 + k * 58;   // One row per setting: name | - | value | +
        lv_obj_t *n = Theme::label(c, tr(names[k]), &my_font_18, Theme::MUTED);
        lv_obj_set_pos(n, 4, y + 12);
        ctlButton(c, LV_SYMBOL_MINUS, 180, y, 70, 48, Theme::TEAL, stepCb, (void *)(intptr_t)(k * 2));
        ctlButton(c, LV_SYMBOL_PLUS, 370, y, 70, 48, Theme::TEAL, stepCb, (void *)(intptr_t)(k * 2 + 1));
        s_steps[k].value = Theme::label(c, "", &my_font_24);
        s_steps[k].kind = k;
        lv_obj_set_width(s_steps[k].value, 110);
        lv_obj_set_style_text_align(s_steps[k].value, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_steps[k].value, 255, y + 10);
        lv_obj_add_event_cb(s_steps[k].value, [](lv_event_t *ev) {
            s_steps[(int)(intptr_t)lv_event_get_user_data(ev)].value = nullptr;
        }, LV_EVENT_DELETE, (void *)(intptr_t)k);
    }
    lv_obj_t *note = Theme::label(c, tr(Str::KARA_SAVED_NOTE), &my_font_14, Theme::MUTED);
    lv_obj_align(note, LV_ALIGN_BOTTOM_LEFT, 4, 0);
    refreshSteppers();
}

// ---- Melody track picker -----------------------------------------------------------------------
void pickCb(lv_event_t *e)
{
    static uint32_t last = 0;
    if (!Theme::debounce(last, DEBOUNCE_MS)) return;
    Karaoke::setMelody((int)(intptr_t)lv_event_get_user_data(e) - 1);
    lv_async_call([](void *) { closeSheet(); }, nullptr);
}

void showTracks()
{
    const Midi::Song *s = Karaoke::song();
    if (!s) return;
    lv_obj_t *c = sheetBase(tr(Str::KARA_PICK_MELODY), H - 16);
    lv_obj_t *hint = Theme::label(c, tr(Str::KARA_PICK_HINT), &my_font_14, Theme::MUTED);
    lv_obj_set_pos(hint, 4, 34);
    lv_obj_t *list = lv_obj_create(c);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, W - 40, H - 16 - 58 - 24);
    lv_obj_set_pos(list, 0, 58);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    const int sel = Karaoke::melody();
    for (int ch = -1; ch < 16; ch++) {
        if (ch >= 0 && !(s->channelMask & (1u << ch))) continue;
        if (ch == Midi::DRUM_CH) continue;   // Drums are never the melody
        lv_obj_t *b = lv_button_create(list);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, W - 40, ch < 0 ? 40 : 54);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_bg_color(b, Theme::c(Theme::PANEL_RAISED), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(b, ch == sel ? 2 : 1, 0);
        lv_obj_set_style_border_color(b, Theme::c(ch == sel ? Theme::ACCENT : Theme::LINE), 0);
        lv_obj_set_style_bg_color(b, Theme::c(Theme::LINE), LV_STATE_PRESSED);
        char buf[96];
        if (ch < 0) {
            snprintf(buf, sizeof(buf), "%s", tr(Str::KARA_CH_NONE));
        } else {
            char head[24];
            snprintf(head, sizeof(head), tr(Str::KARA_CH_FMT), ch + 1);
            snprintf(buf, sizeof(buf), "%s  %s%s%s", head, s->ch[ch].trackName, s->ch[ch].trackName[0] ? " - " : "",
                     Midi::gmName(s->ch[ch].program));
        }
        lv_obj_t *t = Theme::label(b, buf, &my_font_14, ch == sel ? Theme::ACCENT : Theme::TEXT);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(t, W - 60);
        lv_obj_set_pos(t, 8, 6);
        if (ch >= 0) {
            snprintf(buf, sizeof(buf), "%u %s  |  %u%% %s  |  max %u%s%s", (unsigned)s->ch[ch].notes, LV_SYMBOL_AUDIO,
                     (unsigned)s->ch[ch].lyricMatchPct, tr(Str::KARA_MATCH), (unsigned)s->ch[ch].maxPoly,
                     ch == s->melody ? "  |  " : "", ch == s->melody ? tr(Str::KARA_AUTO) : "");
            lv_obj_t *d = Theme::label(b, buf, &my_font_14, Theme::MUTED);
            lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_width(d, W - 60);
            lv_obj_set_pos(d, 8, 28);
        }
        lv_obj_add_event_cb(b, pickCb, LV_EVENT_CLICKED, (void *)(intptr_t)(ch + 1));
    }
}

// ---- Sound bank picker (global default from the list, per song from the player) ----------------
void fontPickCb(lv_event_t *e)
{
    static uint32_t last = 0;
    if (!Theme::debounce(last, DEBOUNCE_MS)) return;
    const int idx = (int)(intptr_t)lv_event_get_user_data(e) - 1;   // -1 = automatic / use the default
    const char *name = idx >= 0 && idx < (int)s_fonts.size() ? s_fonts[(size_t)idx].name.c_str() : "";
    if (s_fontForSong) {
        Karaoke::setSongFont(name);
        updatePlayLabel(false);
    } else {
        Karaoke::setDefaultFont(name);
    }
    Karaoke::ensureFont(true);   // Reloads only when the wanted font changed (or failed before)
    lv_async_call([](void *) { closeSheet(); }, nullptr);
}

void showFonts(bool forSong)
{
    if (Karaoke::fontState() == Karaoke::FontState::LOADING) {
        msg(tr(Str::KARA_WAIT_FONT));
        return;
    }
    if (forSong && !Karaoke::isOpen()) return;
    s_fontForSong = forSong;
    const Karaoke::Err se = Karaoke::scanFonts(s_fonts);
    lv_obj_t *c = sheetBase(tr(forSong ? Str::KARA_FONT_SONG : Str::KARA_FONT_DEFAULT), H - 16);
    lv_obj_t *hint = Theme::label(c, tr(forSong ? Str::KARA_FONT_SONG_HINT : Str::KARA_FONT_SAVED), &my_font_14,
                                  Theme::MUTED);
    lv_obj_set_pos(hint, 4, 34);
    lv_obj_t *list = lv_obj_create(c);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, W - 40, H - 16 - 58 - 24);
    lv_obj_set_pos(list, 0, 58);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    const std::string sel = forSong ? Karaoke::songFont() : Karaoke::defaultFont();
    for (int i = -1; i < (int)s_fonts.size(); i++) {
        const bool isSel = i < 0 ? sel.empty() : s_fonts[(size_t)i].name == sel;
        const bool ok = i < 0 || s_fonts[(size_t)i].err == Karaoke::Err::OK;
        lv_obj_t *b = lv_button_create(list);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, W - 40, i < 0 ? 40 : 54);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_bg_color(b, Theme::c(Theme::PANEL_RAISED), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(b, isSel ? 2 : 1, 0);
        lv_obj_set_style_border_color(b, Theme::c(isSel ? Theme::ACCENT : Theme::LINE), 0);
        lv_obj_set_style_bg_color(b, Theme::c(Theme::LINE), LV_STATE_PRESSED);
        char buf[96];
        if (i < 0) {
            snprintf(buf, sizeof(buf), "%s", tr(forSong ? Str::KARA_FONT_USE_DEFAULT : Str::KARA_FONT_AUTO));
        } else {
            const Karaoke::FontFile &f = s_fonts[(size_t)i];
            const bool loaded = f.name == Karaoke::fontName();
            snprintf(buf, sizeof(buf), "%.60s%s", f.name.c_str(), loaded ? "  " LV_SYMBOL_OK : "");
        }
        lv_obj_t *t = Theme::label(b, buf, &my_font_18, !ok ? Theme::MUTED : isSel ? Theme::ACCENT : Theme::TEXT);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(t, W - 60);
        lv_obj_set_pos(t, 8, i < 0 ? 9 : 5);
        if (i >= 0) {
            const Karaoke::FontFile &f = s_fonts[(size_t)i];
            char d[160];
            if (ok) snprintf(d, sizeof(d), "%.1f MB", (double)f.size / (1024.0 * 1024.0));
            else snprintf(d, sizeof(d), "%s", Karaoke::errText(f.err));
            lv_obj_t *dl = Theme::label(b, d, &my_font_14, ok ? Theme::MUTED : Theme::RED);
            lv_label_set_long_mode(dl, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_width(dl, W - 60);
            lv_obj_set_pos(dl, 8, 30);
        }
        if (ok) lv_obj_add_event_cb(b, fontPickCb, LV_EVENT_CLICKED, (void *)(intptr_t)(i + 1));
        else lv_obj_set_clickable(b, false);   // Rejected file (SF3, too big, not a SoundFont)
    }
    if (se != Karaoke::Err::OK || s_fonts.empty()) {
        lv_obj_t *n = Theme::label(list, se != Karaoke::Err::OK ? Karaoke::errText(se) : tr(Str::KARA_FONT_NONE),
                                   &my_font_14, Theme::RED);
        lv_obj_set_width(n, W - 60);
        lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_WRAP);
    }
}

void updateMelodyButton()
{
    if (P.melBtnLbl) lv_label_set_text(P.melBtnLbl, tr(Karaoke::melodyMuted() ? Str::KARA_MELODY_OFF : Str::KARA_MELODY_ON));
}

void showPlayer()
{
    const Midi::Song *song = Karaoke::song();
    if (!song) return;
    const Karaoke::SongFile &f = s_files[(size_t)s_current];
    if (P.timer) lv_timer_delete(P.timer);
    P = Player{};
    P.line = -3;
    P.syl = -3;
    P.lastSec = 0xFFFFFFFF;
    P.fontState = (Karaoke::FontState)0xFF;   // Forces the first font label update
    lv_obj_t *scr = P.scr = Theme::screen();
    lv_obj_t *hdr = Theme::header(scr, "");
    lv_obj_t *title = lv_obj_get_child(hdr, 0);
    lv_label_set_text(title, f.title.c_str());
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(title, W - 80);
    static uint32_t backLast = 0;
    lv_obj_add_event_cb(backButton(scr), [](lv_event_t *) {
        if (!Theme::debounce(backLast, DEBOUNCE_MS)) return;
        Karaoke::close();
        showList();
    }, LV_EVENT_CLICKED, nullptr);

    // Info line: artist | active sound bank | voices + CPU
    lv_obj_t *artist = Theme::label(scr, f.artist.c_str(), &my_font_14, Theme::MUTED);
    lv_label_set_long_mode(artist, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(artist, 170);
    lv_obj_set_pos(artist, 8, 47);
    P.fontLbl = Theme::label(scr, "", &my_font_14, Theme::MUTED);
    lv_label_set_long_mode(P.fontLbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(P.fontLbl, 190);
    lv_obj_set_pos(P.fontLbl, 184, 47);
    P.diag = Theme::label(scr, "", &my_font_14, Theme::LINE);
    lv_label_set_long_mode(P.diag, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(P.diag, 96);
    lv_obj_set_style_text_align(P.diag, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(P.diag, W - 104, 47);

    // Lyrics: full-width single lines around the current line (up to 3 lines when it is long)
    auto lyricLabel = [&](int32_t y, int32_t h, const lv_font_t *font, uint32_t color) {
        lv_obj_t *l = Theme::label(scr, "", font, color);
        lv_obj_set_width(l, W - 16);
        lv_obj_set_height(l, h);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(l, 8, y);
        return l;
    };
    P.prev = lyricLabel(66, 22, &my_font_18, Theme::LINE);
    P.cur = lv_spangroup_create(scr);
    lv_obj_set_width(P.cur, W - 16);
    lv_obj_set_height(P.cur, 88);
    lv_obj_set_pos(P.cur, 8, 89);
    lv_obj_set_style_text_align(P.cur, LV_TEXT_ALIGN_CENTER, 0);   // Fixed width+height = wrap
    lv_spangroup_set_max_lines(P.cur, 3);
    lv_spangroup_set_overflow(P.cur, LV_SPAN_OVERFLOW_ELLIPSIS);
    lv_obj_set_style_text_font(P.cur, &my_font_24, 0);
    lv_obj_set_style_text_line_space(P.cur, 2, 0);
    P.spSung = lv_spangroup_add_span(P.cur);
    P.spRest = lv_spangroup_add_span(P.cur);
    lv_style_set_text_color(lv_span_get_style(P.spSung), Theme::c(Theme::GOLD));
    lv_style_set_text_color(lv_span_get_style(P.spRest), Theme::c(Theme::TEXT));
    P.next = lyricLabel(178, 22, &my_font_18, Theme::MUTED);
    P.next2 = lyricLabel(200, 18, &my_font_14, Theme::LINE);

    P.slider = lv_slider_create(scr);
    lv_obj_set_size(P.slider, 320, 8);
    lv_obj_set_pos(P.slider, 18, 226);
    lv_slider_set_range(P.slider, 0, (int32_t)(song->lengthUs / 100000 > 0 ? song->lengthUs / 100000 : 1));
    lv_obj_set_style_bg_color(P.slider, Theme::c(Theme::LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(P.slider, Theme::c(Theme::ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(P.slider, Theme::c(Theme::ACCENT), LV_PART_KNOB);
    lv_obj_set_ext_click_area(P.slider, 10);
    lv_obj_add_event_cb(P.slider, [](lv_event_t *) { P.dragging = true; }, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(P.slider, [](lv_event_t *) {
        P.dragging = false;
        const int64_t target = (int64_t)lv_slider_get_value(P.slider) * 100000;
        Karaoke::seekRel((int)((target - (int64_t)Karaoke::view().posUs) / 1000));
        P.line = -3;
    }, LV_EVENT_RELEASED, nullptr);
    P.timeLbl = Theme::label(scr, "", &my_font_14, Theme::MUTED);
    lv_label_set_long_mode(P.timeLbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(P.timeLbl, W - 356);
    lv_obj_set_style_text_align(P.timeLbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(P.timeLbl, 348, 220);

    // Two rows of four buttons (113 x 38) across the 480 px width
    constexpr int32_t BW = 113, BH = 38, X0 = 8, DX = BW + 4, Y1 = 238, Y2 = 280;
    static uint32_t lastRow1[4] = {};
    ctlButton(scr, LV_SYMBOL_PREV, X0, Y1, BW, BH, Theme::LINE, [](lv_event_t *) {
        if (!Theme::debounce(lastRow1[0], DEBOUNCE_MS)) return;
        Karaoke::restart();
        P.line = -3;
    });
    ctlButton(scr, "-5 s", X0 + DX, Y1, BW, BH, Theme::LINE, [](lv_event_t *) {
        if (!Theme::debounce(lastRow1[1], 200)) return;
        Karaoke::seekRel(-5000);
        P.line = -3;
    });
    P.playBtn = ctlButton(scr, LV_SYMBOL_PLAY, X0 + 2 * DX, Y1, BW, BH, Theme::ACCENT, [](lv_event_t *) {
        if (!Theme::debounce(lastRow1[2], DEBOUNCE_MS)) return;
        if (Karaoke::fontState() == Karaoke::FontState::LOADING) {
            msg(tr(Str::KARA_WAIT_FONT));
            return;
        }
        Karaoke::play(!Karaoke::playing());
        updatePlayLabel(Karaoke::playing());
    });
    P.playLbl = lv_obj_get_child(P.playBtn, 0);
    ctlButton(scr, "+5 s", X0 + 3 * DX, Y1, BW, BH, Theme::LINE, [](lv_event_t *) {
        if (!Theme::debounce(lastRow1[3], 200)) return;
        Karaoke::seekRel(5000);
        P.line = -3;
    });
    // Row 2: tempo/key sheet, melody on/off, melody track picker, sound bank for this song
    static uint32_t lastRow2[4] = {};
    ctlButton(scr, tr(Str::KARA_BTN_SETTINGS), X0, Y2, BW, BH, Theme::TEAL, [](lv_event_t *) {
        if (Theme::debounce(lastRow2[0], DEBOUNCE_MS)) showSettings();
    });
    lv_obj_t *mel = ctlButton(scr, "", X0 + DX, Y2, BW, BH, Theme::TEAL, [](lv_event_t *) {
        if (!Theme::debounce(lastRow2[1], DEBOUNCE_MS)) return;
        Karaoke::setMelodyMuted(!Karaoke::melodyMuted());
        updateMelodyButton();
    });
    P.melBtnLbl = lv_obj_get_child(mel, 0);
    ctlButton(scr, tr(Str::KARA_BTN_TRACK), X0 + 2 * DX, Y2, BW, BH, Theme::TEAL, [](lv_event_t *) {
        if (Theme::debounce(lastRow2[2], DEBOUNCE_MS)) showTracks();
    });
    ctlButton(scr, tr(Str::KARA_BTN_FONT), X0 + 3 * DX, Y2, BW, BH, Theme::TEAL, [](lv_event_t *) {
        if (Theme::debounce(lastRow2[3], DEBOUNCE_MS)) showFonts(true);
    });
    updateMelodyButton();

    updateLyrics(Karaoke::view(), true);
    tick(nullptr);
    P.timer = lv_timer_create(tick, TICK_MS, nullptr);
    lv_obj_add_event_cb(scr, playerDeleted, LV_EVENT_DELETE, nullptr);
    Theme::loadScreen(scr);
}

}  // namespace

void show()
{
    s_current = -1;
    Display::setLandscape(true, Config::get().karaFlip != 0);   // Before the screen is created
    showList();
    if (!Config::get().soundOn) {   // Karaoke without sound makes little sense: offer to turn it on
        lv_obj_t *q = lv_msgbox_create(nullptr);
        lv_msgbox_add_text(q, tr(Str::KARA_SOUND_OFF_Q));
        lv_obj_t *yes = lv_msgbox_add_footer_button(q, tr(Str::KARA_YES));
        lv_obj_t *no = lv_msgbox_add_footer_button(q, tr(Str::KARA_NO));
        static uint32_t ynLast = 0;
        lv_obj_add_event_cb(yes, [](lv_event_t *e) {
            if (!Theme::debounce(ynLast, DEBOUNCE_MS)) return;
            AppConfig &c = Config::get();   // Same as the settings page: persist, then apply
            c.soundOn = true;
            Config::save();
            Audio::setVolumes(c.soundOn, c.sfxVol, c.musicOn, c.musicVol);
            lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
        }, LV_EVENT_CLICKED, q);
        lv_obj_add_event_cb(no, [](lv_event_t *e) {
            if (!Theme::debounce(ynLast, DEBOUNCE_MS)) return;
            lv_msgbox_close_async((lv_obj_t *)lv_event_get_user_data(e));
        }, LV_EVENT_CLICKED, q);
        Theme::msgboxFonts(q);
    }
}

}  // namespace UiKaraoke
