// ui_karaoke.cpp - see ui_karaoke.h.
#include "ui_karaoke.h"

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "board_audio.h"
#include "karaoke.h"
#include "lang.h"
#include "my_lvgl.h"
#include "my_theme.h"
#include "ui_yatzy.h"

namespace UiKaraoke {

namespace {

constexpr uint32_t TICK_MS = 40;
constexpr uint32_t DEBOUNCE_MS = 300;

std::vector<Karaoke::SongFile> s_files;
int s_current = -1;

// List screen
lv_obj_t *s_listScr = nullptr;
lv_obj_t *s_fontLbl = nullptr;
lv_timer_t *s_listTimer = nullptr;

// Player screen
struct Player {
    lv_obj_t *scr, *prev, *cur, *next, *next2, *slider, *timeLbl, *playBtn, *playLbl, *melBtnLbl, *diag;
    lv_span_t *spSung, *spRest;
    lv_obj_t *sheet;
    lv_timer_t *timer;
    int line, syl;
    uint32_t lastSec;
    bool dragging, finished;
};
Player P{};

void showPlayer();

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
        UiYatzy::showMenu();
    }, LV_EVENT_CLICKED, nullptr);

    s_fontLbl = Theme::label(scr, "", &my_font_14, Theme::MUTED);
    lv_obj_set_width(s_fontLbl, 300);
    lv_label_set_long_mode(s_fontLbl, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_pos(s_fontLbl, 10, 50);
    Karaoke::startLoadFont();
    updateFontLabel();

    Karaoke::Err e = Karaoke::scan(s_files);
    lv_obj_t *info = Theme::label(scr, "", &my_font_14, Theme::MUTED);
    lv_obj_set_pos(info, 10, 88);
    lv_obj_set_width(info, 300);
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
    lv_obj_set_size(list, 320, 480 - 112);
    lv_obj_set_pos(list, 0, 112);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_style_pad_hor(list, 10, 0);
    lv_obj_set_style_pad_bottom(list, 10, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    for (size_t i = 0; i < s_files.size(); i++) {
        const Karaoke::SongFile &f = s_files[i];
        lv_obj_t *b = lv_button_create(list);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 300, f.artist.empty() ? 44 : 58);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_bg_color(b, Theme::c(Theme::PANEL), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(b, (int)i == s_current ? 2 : 1, 0);
        lv_obj_set_style_border_color(b, Theme::c((int)i == s_current ? Theme::ACCENT : Theme::LINE), 0);
        lv_obj_set_style_bg_color(b, Theme::c(Theme::PANEL_RAISED), LV_STATE_PRESSED);
        lv_obj_t *t = Theme::label(b, f.title.c_str(), &my_font_18);
        lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(t, 280);
        lv_obj_set_pos(t, 10, 10);
        if (!f.artist.empty()) {
            lv_obj_t *a = Theme::label(b, f.artist.c_str(), &my_font_14, Theme::MUTED);
            lv_label_set_long_mode(a, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_width(a, 280);
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

void tick(lv_timer_t *)
{
    const Karaoke::View v = Karaoke::view();
    updateLyrics(v, false);
    const uint32_t sec = v.posUs / 1000000;
    if (sec != P.lastSec || v.finished != P.finished) {
        P.lastSec = sec;
        P.finished = v.finished;
        char a[12], b[12], buf[64];
        fmtTime(a, sizeof(a), v.posUs);
        fmtTime(b, sizeof(b), v.lengthUs);
        snprintf(buf, sizeof(buf), "%s / %s%s%s", a, b, v.finished ? "  " : "", v.finished ? tr(Str::KARA_END) : "");
        lv_label_set_text(P.timeLbl, buf);
        if (!P.dragging && v.lengthUs) lv_slider_set_value(P.slider, (int32_t)(v.posUs / 100000), LV_ANIM_OFF);
        updatePlayLabel(v.playing);
        if (Karaoke::fontState() == Karaoke::FontState::READY)
            snprintf(buf, sizeof(buf), "%d/%d  %u%%", v.voices, 32, (unsigned)v.cpuPct);
        else
            snprintf(buf, sizeof(buf), "%s", tr(Str::KARA_SILENT));
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
    if (P.sheet) lv_obj_delete(P.sheet);
    P.sheet = nullptr;
}

lv_obj_t *sheetBase(const char *title, int32_t h)
{
    closeSheet();
    P.sheet = lv_obj_create(P.scr);
    lv_obj_remove_style_all(P.sheet);
    lv_obj_set_size(P.sheet, 320, 480);
    lv_obj_set_style_bg_color(P.sheet, Theme::c(0x000000), 0);
    lv_obj_set_style_bg_opa(P.sheet, LV_OPA_60, 0);
    lv_obj_set_clickable(P.sheet, true);   // Swallow taps behind the card
    lv_obj_t *c = Theme::card(P.sheet, 304, h);
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
    lv_obj_t *c = sheetBase(tr(Str::KARA_BTN_SETTINGS), 300);
    const Str names[3] = {Str::KARA_TEMPO, Str::KARA_KEY, Str::KARA_VOLUME};
    for (int k = 0; k < 3; k++) {
        const int32_t y = 50 + k * 74;
        lv_obj_t *n = Theme::label(c, tr(names[k]), &my_font_14, Theme::MUTED);
        lv_obj_set_pos(n, 4, y);
        ctlButton(c, LV_SYMBOL_MINUS, 4, y + 20, 70, 44, Theme::TEAL, stepCb, (void *)(intptr_t)(k * 2));
        ctlButton(c, LV_SYMBOL_PLUS, 210, y + 20, 70, 44, Theme::TEAL, stepCb, (void *)(intptr_t)(k * 2 + 1));
        s_steps[k].value = Theme::label(c, "", &my_font_24);
        s_steps[k].kind = k;
        lv_obj_set_width(s_steps[k].value, 120);
        lv_obj_set_style_text_align(s_steps[k].value, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_steps[k].value, 82, y + 28);
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
    lv_obj_t *c = sheetBase(tr(Str::KARA_PICK_MELODY), 420);
    lv_obj_t *hint = Theme::label(c, tr(Str::KARA_PICK_HINT), &my_font_14, Theme::MUTED);
    lv_obj_set_pos(hint, 4, 34);
    lv_obj_t *list = lv_obj_create(c);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 288, 340);
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
        lv_obj_set_size(b, 288, ch < 0 ? 40 : 54);
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
        lv_obj_set_width(t, 270);
        lv_obj_set_pos(t, 8, 6);
        if (ch >= 0) {
            snprintf(buf, sizeof(buf), "%u %s  |  %u%% %s  |  max %u%s%s", (unsigned)s->ch[ch].notes, LV_SYMBOL_AUDIO,
                     (unsigned)s->ch[ch].lyricMatchPct, tr(Str::KARA_MATCH), (unsigned)s->ch[ch].maxPoly,
                     ch == s->melody ? "  |  " : "", ch == s->melody ? tr(Str::KARA_AUTO) : "");
            lv_obj_t *d = Theme::label(b, buf, &my_font_14, Theme::MUTED);
            lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_width(d, 270);
            lv_obj_set_pos(d, 8, 28);
        }
        lv_obj_add_event_cb(b, pickCb, LV_EVENT_CLICKED, (void *)(intptr_t)(ch + 1));
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
    lv_obj_t *scr = P.scr = Theme::screen();
    lv_obj_t *hdr = Theme::header(scr, "");
    lv_obj_t *title = lv_obj_get_child(hdr, 0);
    lv_label_set_text(title, f.title.c_str());
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(title, 240);
    static uint32_t backLast = 0;
    lv_obj_add_event_cb(backButton(scr), [](lv_event_t *) {
        if (!Theme::debounce(backLast, DEBOUNCE_MS)) return;
        Karaoke::close();
        showList();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *artist = Theme::label(scr, f.artist.c_str(), &my_font_14, Theme::MUTED);
    lv_label_set_long_mode(artist, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(artist, 200);
    lv_obj_set_pos(artist, 10, 50);
    P.diag = Theme::label(scr, "", &my_font_14, Theme::LINE);
    lv_obj_align(P.diag, LV_ALIGN_TOP_RIGHT, -10, 50);

    auto lyricLabel = [&](int32_t y, int32_t h, const lv_font_t *font, uint32_t color) {
        lv_obj_t *l = Theme::label(scr, "", font, color);
        lv_obj_set_width(l, 304);
        lv_obj_set_height(l, h);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(l, 8, y);
        return l;
    };
    P.prev = lyricLabel(74, 44, &my_font_18, Theme::LINE);
    P.cur = lv_spangroup_create(scr);
    lv_obj_set_width(P.cur, 304);
    lv_obj_set_height(P.cur, 96);
    lv_obj_set_pos(P.cur, 8, 126);
    lv_obj_set_style_text_align(P.cur, LV_TEXT_ALIGN_CENTER, 0);   // Fixed width+height = wrap
    lv_spangroup_set_max_lines(P.cur, 3);
    lv_spangroup_set_overflow(P.cur, LV_SPAN_OVERFLOW_ELLIPSIS);
    lv_obj_set_style_text_font(P.cur, &my_font_24, 0);
    lv_obj_set_style_text_line_space(P.cur, 4, 0);
    P.spSung = lv_spangroup_add_span(P.cur);
    P.spRest = lv_spangroup_add_span(P.cur);
    lv_style_set_text_color(lv_span_get_style(P.spSung), Theme::c(Theme::GOLD));
    lv_style_set_text_color(lv_span_get_style(P.spRest), Theme::c(Theme::TEXT));
    P.next = lyricLabel(228, 46, &my_font_18, Theme::MUTED);
    P.next2 = lyricLabel(278, 40, &my_font_14, Theme::LINE);

    P.slider = lv_slider_create(scr);
    lv_obj_set_size(P.slider, 280, 10);
    lv_obj_set_pos(P.slider, 20, 326);
    lv_slider_set_range(P.slider, 0, (int32_t)(song->lengthUs / 100000 > 0 ? song->lengthUs / 100000 : 1));
    lv_obj_set_style_bg_color(P.slider, Theme::c(Theme::LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(P.slider, Theme::c(Theme::ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(P.slider, Theme::c(Theme::ACCENT), LV_PART_KNOB);
    lv_obj_set_ext_click_area(P.slider, 14);
    lv_obj_add_event_cb(P.slider, [](lv_event_t *) { P.dragging = true; }, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(P.slider, [](lv_event_t *) {
        P.dragging = false;
        const int64_t target = (int64_t)lv_slider_get_value(P.slider) * 100000;
        Karaoke::seekRel((int)((target - (int64_t)Karaoke::view().posUs) / 1000));
        P.line = -3;
    }, LV_EVENT_RELEASED, nullptr);
    P.timeLbl = Theme::label(scr, "", &my_font_14, Theme::MUTED);
    lv_obj_set_width(P.timeLbl, 300);
    lv_obj_set_style_text_align(P.timeLbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(P.timeLbl, 10, 342);

    // Row 1: restart, -5 s, play/pause, +5 s
    static uint32_t lastRow1[4] = {};
    ctlButton(scr, LV_SYMBOL_PREV, 8, 366, 70, 48, Theme::LINE, [](lv_event_t *) {
        if (!Theme::debounce(lastRow1[0], DEBOUNCE_MS)) return;
        Karaoke::restart();
        P.line = -3;
    });
    ctlButton(scr, "-5 s", 86, 366, 70, 48, Theme::LINE, [](lv_event_t *) {
        if (!Theme::debounce(lastRow1[1], 200)) return;
        Karaoke::seekRel(-5000);
        P.line = -3;
    });
    P.playBtn = ctlButton(scr, LV_SYMBOL_PLAY, 164, 366, 70, 48, Theme::ACCENT, [](lv_event_t *) {
        if (!Theme::debounce(lastRow1[2], DEBOUNCE_MS)) return;
        Karaoke::play(!Karaoke::playing());
        updatePlayLabel(Karaoke::playing());
    });
    P.playLbl = lv_obj_get_child(P.playBtn, 0);
    ctlButton(scr, "+5 s", 242, 366, 70, 48, Theme::LINE, [](lv_event_t *) {
        if (!Theme::debounce(lastRow1[3], 200)) return;
        Karaoke::seekRel(5000);
        P.line = -3;
    });
    // Row 2: tempo/key sheet, melody on/off, melody track picker
    static uint32_t lastRow2[3] = {};
    ctlButton(scr, tr(Str::KARA_BTN_SETTINGS), 8, 422, 112, 48, Theme::TEAL, [](lv_event_t *) {
        if (Theme::debounce(lastRow2[0], DEBOUNCE_MS)) showSettings();
    });
    lv_obj_t *mel = ctlButton(scr, "", 126, 422, 112, 48, Theme::TEAL, [](lv_event_t *) {
        if (!Theme::debounce(lastRow2[1], DEBOUNCE_MS)) return;
        Karaoke::setMelodyMuted(!Karaoke::melodyMuted());
        updateMelodyButton();
    });
    P.melBtnLbl = lv_obj_get_child(mel, 0);
    ctlButton(scr, tr(Str::KARA_BTN_TRACK), 244, 422, 68, 48, Theme::TEAL, [](lv_event_t *) {
        if (Theme::debounce(lastRow2[2], DEBOUNCE_MS)) showTracks();
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
    showList();
}

}  // namespace UiKaraoke
