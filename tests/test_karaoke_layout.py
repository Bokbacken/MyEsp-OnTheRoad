#!/usr/bin/env python3
"""Run the complete production Karaoke UI with host LVGL and stubbed hardware/audio.

python3 tests/test_karaoke_layout.py [external build directory]
Requires managed_components/lvgl__lvgl, CMake and Ninja, like test_yatzy_layout.py.
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
WORK = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(tempfile.mkdtemp(prefix="karaoke-layout-"))
WORK.mkdir(parents=True, exist_ok=True)
(WORK / "driver").mkdir(exist_ok=True)
(WORK / "driver/i2c_master.h").write_text("#pragma once\nusing i2c_master_bus_handle_t = void *;\n")
(WORK / "esp_heap_caps.h").write_text("""#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
inline size_t heap_caps_get_free_size(int) { return 0; }
inline size_t heap_caps_get_largest_free_block(int) { return 0; }
""")
(WORK / "esp_log.h").write_text('#pragma once\n#define ESP_LOGI(tag, ...) ((void)tag)\n')
(WORK / "sdkconfig.h").write_text("")

engine = (ROOT / "main/karaoke.cpp").read_text()


def production_function(signature):
    start = engine.index(signature)
    opening = engine.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (engine[end] == "{") - (engine[end] == "}")
        end += 1
    return engine[start:end]


code = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include "ui_karaoke.cpp"

namespace Config {
static int saveCalls;
AppConfig &get() { static AppConfig c{}; return c; }
bool save() { ++saveCalls; return true; }
}
namespace BankChoice {
static bool s_open;
static std::string s_songSf2;
@BANK_REQUEST@
}
namespace Audio {
void music(Sound::Music) {}
void setVolumes(bool, uint8_t, bool, uint8_t) {}
bool webStream() { return false; }
}
namespace KaraWeb { int clients() { return 0; } }
static int menus;
static Theme::BatteryIndicator batterySample{true, 50};
static Theme::BatteryIndicator sampleBatteryIndicator() { return batterySample; }
namespace UiYatzy { void showMenu() { ++menus; Theme::loadScreen(Theme::screen()); } }
static bool rotateOK = true;
namespace Display {
bool setLandscape(bool on, bool flip) {
    if (!rotateOK) return false;
    lv_display_set_rotation(lv_display_get_default(), on
        ? (flip ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90) : LV_DISPLAY_ROTATION_0);
    return true;
}
}
namespace Karaoke {
static Midi::Song data;
static KaraIndex::Library library;
static SongFile file{"/sdcard/otr/karaoke/Test_Long title.kar",
    "A very long title with ÅÄÖ that must never collide with menu or back controls",
    "A very long artist name", 100, ""};
static View state{};
static FontState font = FontState::READY;
static bool opened, muted, rejectTranspose;
static int speed = 100, key = 0, channel = 0, vol = 80;
static int ensures, closes, unloads, opens;
static std::string usedBank;
bool loadIndex() { return true; }
void rescanAsync() {}
bool scanning() { return false; }
uint32_t libraryGen() { return 1; }
uint32_t externalOpenGen() { return 0; }
Err libraryError() { return Err::OK; }
void libraryLock() {}
void libraryUnlock() {}
const KaraIndex::Library &libraryRef() { return library; }
SongFile songAt(const KaraIndex::Library &lib, size_t i) {
    auto f = file; f.path = std::string(DIR_SONGS) + "/" + lib.rel(i); return f;
}
SongFile current() { return file; }
Err open(const SongFile &) {
    ++opens; opened = BankChoice::s_open = true; usedBank = BankChoice::currentRequest(); return Err::OK;
}
void close() { ++closes; opened = BankChoice::s_open = false; state.playing = false; }
bool isOpen() { return opened; }
const Midi::Song *song() { return opened ? &data : nullptr; }
const char *errText(Err) { return "Long error text: ÅÄÖ. Please select another song or sound bank."; }
FontState fontState() { return font; }
Err fontError() { return Err::NO_MEMORY; }
const char *fontName() { return Config::get().karaFont; }
const char *defaultFont() { return Config::get().karaFont; }
@SET_DEFAULT_FONT@
const char *songFont() { return ""; }
void setSongFont(const char *) { assert(false && "No player bank control"); }
bool fontWanted() { return false; }
void ensureFont(bool) { ++ensures; }
void startLoadFont() { ++ensures; }
void unloadFont() { ++unloads; close(); }
Err scanFonts(std::vector<FontFile> &out) {
    out = {{"Long sound bank name with Swedish Åäö.sf2", 1000000, Err::OK},
           {"Rejected.sf3", 1000000, Err::SF2_COMPRESSED}};
    return Err::OK;
}
View view() { return state; }
bool playing() { return state.playing; }
void play(bool b) { state.playing = b; }
void restart() { state.posUs = 0; }
void seekRel(int ms) { state.posUs = std::max<int64_t>(0, int64_t(state.posUs) + ms * 1000); }
int tempo() { return speed; }
void setTempo(int p) { speed = std::clamp(p, 50, 150); }
int transpose() { return key; }
void setTranspose(int p) { key = std::clamp(p, -12, 12); }
int melody() { return channel; }
void setMelody(int ch) { channel = ch; }
bool melodyMuted() { return muted; }
void setMelodyMuted(bool b) { muted = b; }
uint8_t volume() { return vol; }
void setVolume(uint8_t v) { vol = v; Config::get().karaVolume = v; }
bool takeTransposeRejected() { bool r = rejectTranspose; rejectTranspose = false; return r; }
}
using namespace UiKaraoke;
static void settle() {
    lv_tick_inc(600);
    for (int i = 0; i < 4; ++i) lv_timer_handler();
    lv_obj_update_layout(lv_screen_active());
}
static void click(lv_obj_t *b, int line = __builtin_LINE()) {
    if (!b) fprintf(stderr, "missing button at line %d\n", line);
    assert(b);
    lv_tick_inc(600);
    lv_obj_send_event(b, LV_EVENT_CLICKED, nullptr);
    settle();
}
static lv_obj_t *button(lv_obj_t *root, const char *text, bool prefix = false) {
    if (lv_obj_has_class(root, &lv_button_class) ||
        lv_obj_check_type(root, &lv_msgbox_footer_button_class)) {
        auto *label = lv_obj_get_child(root, 0);
        if (label && lv_obj_check_type(label, &lv_label_class)) {
            const char *value = lv_label_get_text(label);
            if (prefix ? strncmp(value, text, strlen(text)) == 0 : strcmp(value, text) == 0) return root;
        }
    }
    for (unsigned i = 0; i < lv_obj_get_child_count(root); ++i)
        if (auto *b = button(lv_obj_get_child(root, i), text, prefix)) return b;
    return nullptr;
}
static void inside(lv_obj_t *o, lv_obj_t *parent, int line = __builtin_LINE()) {
    if (!o || !parent || !lv_obj_is_valid(o) || !lv_obj_is_valid(parent))
        fprintf(stderr, "invalid containment objects at line %d\n", line);
    assert(o && parent && lv_obj_is_valid(o) && lv_obj_is_valid(parent));
    lv_area_t a, b; lv_obj_get_coords(o, &a); lv_obj_get_coords(parent, &b);
    if (!(a.x1 >= b.x1 && a.y1 >= b.y1 && a.x2 <= b.x2 && a.y2 <= b.y2))
        fprintf(stderr, "line %d outside %d,%d..%d,%d parent %d,%d..%d,%d\n",
                line,a.x1,a.y1,a.x2,a.y2,b.x1,b.y1,b.x2,b.y2);
    assert(a.x1 >= b.x1 && a.y1 >= b.y1 && a.x2 <= b.x2 && a.y2 <= b.y2);
}
static void fitsText(lv_obj_t *b, int line = __builtin_LINE()) {
    inside(lv_obj_get_child(b, 0), b, line);
    assert(lv_obj_get_height(b) >= 42 && lv_obj_get_width(b) >= 44);
}
static lv_obj_t *hit(lv_obj_t *b) {
    lv_area_t a; lv_obj_get_coords(b, &a);
    lv_point_t point{(a.x1+a.x2)/2, (a.y1+a.y2)/2};
    return lv_indev_search_obj(lv_screen_active(), &point);
}
static lv_obj_t *sheetBody() {
    assert(s_sheet);
    inside(s_sheet, lv_screen_active());
    auto *card = lv_obj_get_child(s_sheet, 0);
    inside(card, s_sheet);
    auto *close = button(card, LV_SYMBOL_CLOSE);
    fitsText(close); inside(close, card);
    auto *body = lv_obj_get_child(card, -1);
    inside(body, card);
    return body;
}
static void closeMenu() {
    click(button(s_sheet, LV_SYMBOL_CLOSE));
    assert(!s_sheet && !P.melBtnLbl);
}
static void makeLyrics(const char *word) {
    auto &s = Karaoke::data;
    s.clear();
    for (int row = 0; row < 6; ++row) {
        s.lines.push_back({uint32_t(row * 10000000), uint32_t((row+1) * 10000000),
                           uint32_t(s.syls.size()), 60, false});
        for (int syl = 0; syl < 60; ++syl) {
            const uint32_t offset = s.text.size();
            s.text += word;
            s.syls.push_back({uint32_t(row * 10000000 + syl * 100000), offset,
                             uint16_t(strlen(word)), 0});
        }
    }
    s.lengthUs = 60000000;
    s.channelMask = 0xffff;
    for (auto &ch : s.ch) snprintf(ch.trackName, sizeof(ch.trackName), "Långt spårnamn Åäö");
    Karaoke::state = {1000000, s.lengthUs, 1, 60, 0, 0, false, false, 1, 32, 10};
}
static void checkBattery(lv_obj_t *battery) {
    auto *body = lv_obj_get_child(battery, 0);
    auto *percent = lv_obj_get_child(body, 0);
    assert(lv_obj_get_width(battery) == Theme::BATTERY_STATUS_WIDTH && lv_obj_get_height(battery) == 32);
    assert(lv_obj_get_child_count(battery) == 2 && lv_obj_get_child_count(body) == 1);
    for (unsigned value : {0, 4, 5, 9, 10, 30, 31, 50, 100, 101}) {
        batterySample = {true, uint8_t(value)};
        settle();
        inside(percent, body);
        const uint32_t color = value > 100 ? Theme::MUTED :
            value > 30 ? Theme::GREEN : value >= 10 ? Theme::ACCENT : Theme::RED;
        assert(lv_color_eq(lv_obj_get_style_bg_color(body, LV_PART_MAIN), Theme::c(color)));
        char expected[8];
        if (value <= 100) snprintf(expected, sizeof expected, "%u%%", value);
        else strcpy(expected, "?");
        assert(!strcmp(lv_label_get_text(percent), expected));
        lv_point_t textSize;
        lv_text_get_size(&textSize, expected, &my_font_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        assert(textSize.x <= lv_obj_get_width(percent) && textSize.y <= lv_obj_get_height(percent));
    }
    batterySample = {false, 0};
    settle();
    assert(!strcmp(lv_label_get_text(percent), "?"));
    batterySample = {true, 50};
    settle();
}
static void checkDrawnVolumeKnob() {
    lv_area_t drawn{};
    auto capture = [](lv_event_t *event) {
        auto *task = lv_event_get_draw_task(event);
        if (!task) return;
        auto *dsc = static_cast<lv_draw_dsc_base_t *>(lv_draw_task_get_draw_dsc(task));
        if (dsc->part == LV_PART_KNOB)
            lv_draw_task_get_area(task, static_cast<lv_area_t *>(lv_event_get_user_data(event)));
    };
    lv_obj_set_send_draw_task_events(P.volumeSlider, true);
    lv_obj_add_event_cb(P.volumeSlider, capture, LV_EVENT_DRAW_TASK_ADDED, &drawn);
    for (bool pressed : {false, true}) {
        if (pressed) lv_obj_add_state(P.volumeSlider, LV_STATE_PRESSED);
        for (int value : {0, 50, 100}) {
            lv_slider_set_value(P.volumeSlider, value, LV_ANIM_OFF);
            lv_layer_t layer;
            lv_layer_init(&layer);
            layer.display = lv_display_get_default();
            layer._clip_area = layer.buf_area = layer.phy_clip_area =
                {0, 0, UiMetrics::screenW() - 1, UiMetrics::screenH() - 1};
            layer.color_format = LV_COLOR_FORMAT_RGB565;
            lv_obj_send_event(P.volumeSlider, LV_EVENT_DRAW_MAIN, &layer);
            lv_draw_layer_deinit(&layer);
            if (lv_area_get_width(&drawn) != KaraokeLayout::VOLUME_KNOB_WIDTH ||
                lv_area_get_height(&drawn) != KaraokeLayout::VOLUME_KNOB_HEIGHT)
                fprintf(stderr, "drawn volume knob at %d pressed=%d: %dx%d\n", value, pressed,
                        int(lv_area_get_width(&drawn)), int(lv_area_get_height(&drawn)));
            assert(lv_area_get_width(&drawn) == KaraokeLayout::VOLUME_KNOB_WIDTH);
            assert(lv_area_get_height(&drawn) == KaraokeLayout::VOLUME_KNOB_HEIGHT);
            assert(lv_area_get_width(&drawn) > 2 * lv_area_get_height(&drawn));
        }
    }
    lv_obj_remove_state(P.volumeSlider, LV_STATE_PRESSED);
    lv_obj_remove_event_cb(P.volumeSlider, capture);
    lv_obj_set_send_draw_task_events(P.volumeSlider, false);
    lv_slider_set_value(P.volumeSlider, Karaoke::volume(), LV_ANIM_OFF);
}
static void checkPlayer() {
    const auto l = layout();
    assert(P.scr == lv_screen_active());
    assert(l.lyricsH >= 150 && (l.w != 800 || l.lyricsH >= 290));
    inside(P.lyrics, P.scr);
    assert(lv_obj_get_height(P.lyrics) == l.lyricsH);
    assert(lv_obj_get_width(P.lyrics) == l.lyricsW);
    assert(lv_spangroup_get_mode(P.cur) == LV_SPAN_MODE_BREAK);
    assert(lv_spangroup_get_max_lines(P.cur) == -1);
    assert(lv_obj_get_height(P.cur) > my_font_24.line_height * 3);
    assert(lv_obj_get_scroll_bottom(P.lyrics) > 0);
    const char *controls[] = {LV_SYMBOL_PREV, "-5 s", LV_SYMBOL_PLAY, "+5 s", LV_SYMBOL_STOP};
    int right = 0;
    for (const char *name : controls) {
        auto *b = button(P.scr, name);
        fitsText(b); inside(b, P.scr);
        assert(lv_obj_get_y(b) == l.bottomY && lv_obj_get_x(b) >= right);
        right = lv_obj_get_x(b) + lv_obj_get_width(b);
    }
    assert(!button(P.scr, tr(Str::KARA_BTN_FONT)));
    assert(!button(P.scr, tr(Str::KARA_BTN_SETTINGS), true));
    auto *header = lv_obj_get_child(P.scr, 0);
    inside(lv_obj_get_child(header, 0), header);
    auto *battery = lv_obj_get_child(header, 1);
    inside(battery, header);
    checkBattery(battery);
    lv_area_t titleArea, batteryArea, volumeArea, lyricsArea, timeArea;
    lv_obj_get_coords(lv_obj_get_child(header, 0), &titleArea);
    lv_obj_get_coords(battery, &batteryArea);
    assert(titleArea.x2 < batteryArea.x1 || titleArea.y2 < batteryArea.y1);
    for (auto *nav : {button(P.scr, LV_SYMBOL_LIST), button(P.scr, LV_SYMBOL_LEFT)}) {
        lv_area_t navArea;
        inside(nav, P.scr);
        lv_obj_get_coords(nav, &navArea);
        assert(navArea.x2 < batteryArea.x1 || navArea.x1 > batteryArea.x2 ||
               navArea.y2 < batteryArea.y1 || navArea.y1 > batteryArea.y2);
    }
    inside(P.volumeSlider, P.scr);
    inside(P.volumeValue, P.scr);
    inside(P.volumeTrack, P.scr);
    assert(lv_obj_get_width(P.volumeSlider) == KaraokeLayout::VOLUME_TRACK_WIDTH &&
           lv_obj_get_height(P.volumeSlider) >= 100);
    lv_area_t clickArea;
    lv_obj_get_click_area(P.volumeSlider, &clickArea);
    assert(lv_area_get_width(&clickArea) >= l.volumeW && l.volumeW >= 52);
    for (int x : {l.volumeX, l.volumeX + l.volumeW - 1}) {
        lv_point_t point{x, l.volumeSliderY + l.volumeH / 2};
        assert(lv_indev_search_obj(lv_screen_active(), &point) == P.volumeSlider);
    }
    checkDrawnVolumeKnob();
    assert(lv_obj_get_style_radius(P.volumeSlider, LV_PART_KNOB) == 3);
    lv_obj_get_coords(P.volumeSlider, &volumeArea);
    lv_obj_get_coords(P.lyrics, &lyricsArea);
    lv_obj_get_coords(P.timeLbl, &timeArea);
    assert(lyricsArea.x2 < volumeArea.x1);
    assert(timeArea.x2 < volumeArea.x1);
    assert(volumeArea.y1 > l.infoY + my_font_14.line_height);
    assert(volumeArea.y2 < l.bottomY);
    assert(hit(P.volumeSlider) == P.volumeSlider);
    inside(button(P.scr, LV_SYMBOL_LIST), P.scr);
    const auto sungBefore = std::string(lv_span_get_text(P.spSung));
    Karaoke::state.syl = 108;
    updateLyrics(Karaoke::state, false);
    settle();
    assert(std::string(lv_span_get_text(P.spSung)).size() > sungBefore.size());
    assert(lv_obj_get_scroll_y(P.lyrics) > 0);
    auto coords = lv_spangroup_get_span_coords(P.cur, P.spSung);
    const int activeY = lv_obj_get_y(P.cur) + std::max({coords.heading.y2, coords.middle.y2, coords.trailing.y2})
                      - lv_obj_get_scroll_y(P.lyrics);
    assert(activeY >= 0 && activeY < l.lyricsH);
    Karaoke::state.cdDots=8; Karaoke::state.cdLit=3;
    onCountdown(8, 3); settle();
    assert(!lv_obj_is_hidden(P.cdRow));
    assert(lv_obj_get_y(P.cdRow) >= lv_obj_get_y(P.cur) + lv_obj_get_height(P.cur));
    Karaoke::state.cdDots=0;
    onCountdown(0, 0);
    click(button(P.scr, LV_SYMBOL_PLAY)); assert(Karaoke::playing());
    click(button(P.scr, LV_SYMBOL_PAUSE)); assert(!Karaoke::playing());
    const auto pos = Karaoke::state.posUs;
    click(button(P.scr, "+5 s")); assert(Karaoke::state.posUs == pos + 5000000);
    click(button(P.scr, "-5 s")); assert(Karaoke::state.posUs == pos);
    click(button(P.scr, LV_SYMBOL_PREV)); assert(!Karaoke::state.posUs);
    click(button(P.scr, LV_SYMBOL_PLAY));
    click(button(P.scr, LV_SYMBOL_STOP)); assert(!Karaoke::playing() && !Karaoke::state.posUs);

    click(button(P.scr, LV_SYMBOL_LIST));
    auto *body = sheetBody();
    assert(hit(P.playBtn) != P.playBtn);
    assert(hit(button(P.scr, LV_SYMBOL_LIST)) == s_sheet); // Backdrop blocks header taps too
    for (unsigned i=0; i<lv_obj_get_child_count(body); ++i) fitsText(lv_obj_get_child(body, i));
    const bool muted = Karaoke::melodyMuted();
    click(button(s_sheet, tr(muted ? Str::KARA_MELODY_OFF : Str::KARA_MELODY_ON)));
    assert(Karaoke::melodyMuted() != muted);
    assert(button(s_sheet, tr(muted ? Str::KARA_MELODY_ON : Str::KARA_MELODY_OFF)));
    closeMenu();
    assert(hit(P.playBtn) == P.playBtn);
    click(button(P.scr, LV_SYMBOL_PLAY)); assert(Karaoke::playing()); // No blocker left
    click(button(P.scr, LV_SYMBOL_LIST));
    click(button(s_sheet, tr(Str::KARA_BTN_SETTINGS), true));
    body = sheetBody();
    for (int k = 0; k < 2; ++k) {
        auto *row = lv_obj_get_child(body, k);
        lv_obj_scroll_to_view(row, LV_ANIM_OFF); settle(); inside(row, body);
        for (int j : {1, 3}) fitsText(lv_obj_get_child(row, j));
        click(lv_obj_get_child(row, 3));
        click(lv_obj_get_child(row, 1));
        click(lv_obj_get_child(row, 3));
    }
    assert(Karaoke::tempo() == 105 && Karaoke::transpose() == 1 && Karaoke::volume() == 80);
    assert(!button(s_sheet, tr(Str::KARA_VOLUME)));
    assert(lv_slider_get_value(P.volumeSlider) == 80);
    assert(std::string(lv_label_get_text(P.volumeValue)) == "80%");
    lv_tick_inc(800); settle(); // Flush any preceding stepper change before checking drag debounce.
    const int savesBeforeDrag = Config::saveCalls;
    lv_obj_send_event(P.volumeSlider, LV_EVENT_PRESSED, nullptr);
    lv_slider_set_value(P.volumeSlider, 55, LV_ANIM_OFF);
    lv_obj_send_event(P.volumeSlider, LV_EVENT_VALUE_CHANGED, nullptr);
    assert(Karaoke::volume() == 55 && Config::get().karaVolume == 55);
    assert(Config::saveCalls == savesBeforeDrag); // No NVS writes during a drag.
    lv_tick_inc(900); settle();
    assert(Config::saveCalls == savesBeforeDrag);
    lv_obj_send_event(P.volumeSlider, LV_EVENT_RELEASED, nullptr);
    lv_tick_inc(699); lv_timer_handler();
    assert(Config::saveCalls == savesBeforeDrag);
    lv_tick_inc(2); lv_timer_handler();
    assert(Config::saveCalls == savesBeforeDrag + 1);
    lv_obj_send_event(P.volumeSlider, LV_EVENT_PRESSED, nullptr);
    lv_slider_set_value(P.volumeSlider, 90, LV_ANIM_OFF);
    lv_obj_send_event(P.volumeSlider, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_send_event(P.volumeSlider, LV_EVENT_RELEASED, nullptr);
    lv_tick_inc(701); lv_timer_handler();
    assert(Karaoke::volume() == 90 && Config::get().karaVolume == 90);
    closeMenu();
    click(button(P.scr, LV_SYMBOL_LIST));
    assert(button(s_sheet, (std::string(tr(Str::KARA_BTN_SETTINGS)) + "  105%  +1").c_str()));
    click(button(s_sheet, tr(Str::KARA_BTN_TRACK), true));
    body = sheetBody();
    auto *list = lv_obj_get_child(body, 1);
    assert(lv_obj_get_child_count(list) == 16); // None + 15 non-drum channels
    auto *last = lv_obj_get_child(list, -1);
    lv_obj_scroll_to_view_recursive(last, LV_ANIM_OFF); settle(); inside(last, body);
    click(last); assert(!s_sheet && Karaoke::melody() == 15);
    click(button(P.scr, LV_SYMBOL_LIST));
    char track[40]; snprintf(track, sizeof(track), tr(Str::KARA_CH_FMT), 16);
    assert(button(s_sheet, (std::string(tr(Str::KARA_BTN_TRACK)) + "  " + track).c_str()));
    closeMenu();
    assert(Karaoke::melodyMuted() != muted);
}
int main() {
    lv_init();
    Theme::setBatteryIndicatorReader(sampleBatteryIndicator);
    auto *display = lv_display_create(320,480);
    for (auto size : {lv_point_t{320,480}, lv_point_t{480,800}}) {
        for (bool flip : {false,true}) for (const char *language : {"sv","en"}) {
            lv_display_set_rotation(display, LV_DISPLAY_ROTATION_0);
            lv_display_set_resolution(display, size.x, size.y);
            Lang::setLang(language);
            Config::get().soundOn = true; Config::get().karaFlip = flip; Config::get().karaVolume = 80;
            Karaoke::library.clear();
            for (int i = 0; i < 90; ++i) {
                char path[80]; snprintf(path,sizeof(path),"Test %03d.kar",i);
                assert(Karaoke::library.add(path, Karaoke::file.title.c_str(), "ÅÄÖ",100));
            }
            Karaoke::library.sort();
            Karaoke::opened = false;
            Karaoke::speed=100; Karaoke::key=0; Karaoke::vol=80;
            makeLyrics(language[0]=='s' ? "Lång sångrad Åäö " : "Long lyric words ");
            UiKaraoke::show(); settle();
            const auto l = layout();
            assert(l.w == size.y && l.h == size.x);
            printf("%dx%d %s flip=%d: lyrics=%dx%d, primary touch=%dx%d\n",
                   int(l.w),int(l.h),language,int(flip),int(l.w-2*l.inset),int(l.lyricsH),
                   int(l.bottomW),int(l.buttonH));
            inside(s_list, s_listScr); inside(s_jumpCol,s_listScr);
            assert(lv_obj_get_scroll_bottom(s_jumpCol) > 0);
            assert(lv_obj_get_child_count(s_list) <= PAGE + 2);
            click(button(s_list, LV_SYMBOL_RIGHT)); assert(s_page == 1);
            click(button(s_list, LV_SYMBOL_RIGHT)); assert(s_page == 2);
            auto *jump = lv_obj_get_child(s_jumpCol,-1);
            lv_obj_scroll_to_view(jump,LV_ANIM_OFF); settle(); inside(jump,s_jumpCol); fitsText(jump);
            click(jump); assert(s_page == 2);
            click(button(s_list, LV_SYMBOL_LEFT)); assert(s_page == 1);
            click(button(s_list, LV_SYMBOL_LEFT)); assert(s_page == 0);
            for (unsigned i=0; i<lv_obj_get_child_count(s_list)-1; ++i) {
                auto *songButton=lv_obj_get_child(s_list,i);
                inside(lv_obj_get_child(songButton,0),songButton);
            }
            auto *bank = button(s_listScr, tr(Str::KARA_BTN_FONT));
            fitsText(bank);
            click(bank); auto *body = sheetBody();
            auto *list = lv_obj_get_child(body,1);
            click(lv_obj_get_child(list,1));
            assert(!s_sheet && s_fonts.capacity() == 0 &&
                   std::string(Karaoke::defaultFont()).find("Åäö") != std::string::npos);
            auto *search = lv_obj_get_parent(s_searchLbl);
            click(search); assert(s_input);
            inside(s_input,s_listScr);
            for (unsigned i=0; i<lv_obj_get_child_count(s_input); ++i) inside(lv_obj_get_child(s_input,i),s_input);
            lv_obj_send_event(lv_obj_get_child(s_input,3),LV_EVENT_CANCEL,nullptr); settle();
            assert(!s_input && s_mode != ListMode::SEARCH);
            click(search); assert(s_input);
            lv_textarea_set_text(lv_obj_get_child(s_input,2)," long ");
            lv_obj_send_event(lv_obj_get_child(s_input,3),LV_EVENT_READY,nullptr); settle();
            assert(s_mode == ListMode::SEARCH && s_query == "long");
            openEntry(s_firstSong); settle();
            assert(Karaoke::usedBank == Karaoke::defaultFont());
            checkPlayer();
            assert(s_entries.capacity() == 0); // Old list screen and its UI-only view are gone
            click(button(P.scr,LV_SYMBOL_LEFT)); assert(!Karaoke::isOpen());
            assert(!s_sheet && s_listTimer);
            goUp(); // Leave search before exiting
            const int before=menus;
            click(button(s_listScr, LV_SYMBOL_LEFT)); settle();
            assert(menus == before+1 && lv_display_get_rotation(display) == LV_DISPLAY_ROTATION_0);
            assert(!P.scr && !s_listScr && !P.timer && !s_listTimer);
            assert(lv_obj_get_child_count(lv_layer_top()) == 0);
        }
        // Test the production bank-priority function, not a duplicated selection formula.
        BankChoice::s_songSf2="registered.sf2"; BankChoice::s_open=true;
        assert(BankChoice::currentRequest() == "registered.sf2");
        BankChoice::s_open=false;
        assert(BankChoice::currentRequest() == Karaoke::defaultFont());
        BankChoice::s_songSf2.clear();
    }
    // Both startup answers close the actual modal; loading/errors remain bounded and dismissible.
    for (bool yes : {false,true}) {
        Config::get().soundOn=false; UiKaraoke::show(); settle();
        auto *modal=lv_obj_get_child(lv_layer_top(),-1);
        auto *box=lv_obj_get_child(modal,0); inside(box,lv_screen_active());
        click(button(box,tr(yes ? Str::KARA_YES : Str::KARA_NO)));
        assert(Config::get().soundOn == yes && lv_obj_get_child_count(lv_layer_top()) == 0);
        Karaoke::font=Karaoke::FontState::LOADING;
        msg(tr(Str::KARA_WAIT_FONT),true); settle(); assert(s_waitDlg);
        inside(s_waitDlg,lv_screen_active());
        Karaoke::font=Karaoke::FontState::READY; updateFontLabel(); settle();
        assert(!s_waitDlg && lv_obj_get_child_count(lv_layer_top()) == 0);
    }
    Karaoke::opened = true; Karaoke::data.lines.clear();
    showPlayer(); settle();
    assert(!strcmp(lv_span_get_text(P.spRest),tr(Str::KARA_NO_LYRICS)));
    click(button(P.scr,LV_SYMBOL_LIST)); sheetBody(); closeMenu();
    msg(std::string(1600,'A').c_str()); settle();
    auto *longBox=lv_obj_get_child(lv_obj_get_child(lv_layer_top(),-1),0);
    inside(longBox,lv_screen_active());
    auto *content=lv_msgbox_get_content(longBox); inside(content,longBox);
    assert(lv_obj_get_scroll_bottom(content)>0);
    lv_msgbox_close(longBox); settle();
    rotateOK=false;
    const auto *old=s_listScr;
    UiKaraoke::show(); settle(); assert(old == s_listScr); // No misoriented screen on allocation failure
    auto *box=lv_obj_get_child(lv_obj_get_child(lv_layer_top(),-1),0);
    inside(box,lv_screen_active()); lv_msgbox_close(box);
    puts("Karaoke production UI: landscape 480x320/800x480, flip, SV/EN, lyrics, controls, menus, sheets, search and bank flow passed");
    lv_display_delete(display);
    lv_deinit();
}
'''.replace("@BANK_REQUEST@", production_function("std::string currentRequest()")).replace(
    "@SET_DEFAULT_FONT@", production_function("void setDefaultFont(const char *name)"))
(WORK / "test.cpp").write_text(code)
(WORK / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.22)
project(karaoke_layout LANGUAGES C CXX)
set(LV_CONF_SKIP ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_compile_definitions(LV_CONF_SKIP LV_COLOR_FORMAT_DEFAULT=LV_COLOR_FORMAT_RGB565 LV_DPI_DEF=130
 LV_MEM_SIZE=262144 LV_USE_ASSERT_MALLOC=1)
add_subdirectory("{ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(layout test.cpp
 "{ROOT}/components/my_ui/fonts/my_font_14.c" "{ROOT}/components/my_ui/fonts/my_font_18.c"
 "{ROOT}/components/my_ui/fonts/my_font_24.c" "{ROOT}/components/my_ui/fonts/my_font_36.c"
 "{ROOT}/components/my_ui/my_textinput.cpp" "{ROOT}/components/my_ui/my_charwheel.cpp"
 "{ROOT}/main/kara_index.cpp" "{ROOT}/components/otr_midi/otr_midi.cpp" "{ROOT}/components/otr_midi/otr_midi_gm.cpp"
 "{ROOT}/main/lang/lang.cpp" "{ROOT}/main/lang/lang_sv.cpp" "{ROOT}/main/lang/lang_en.cpp"
 "{ROOT}/main/ui_metrics.cpp" "{ROOT}/main/hw_profile.cpp")
target_include_directories(layout PRIVATE "{WORK}" "{ROOT}/components/my_ui/include"
 "{ROOT}/components/my_wifi/include" "{ROOT}/components/my_lvgl/include"
 "{ROOT}/components/otr_midi/include" "{ROOT}/main/lang" "{ROOT}/main")
target_link_libraries(layout PRIVATE lvgl m)
''')
subprocess.run(["cmake", "-S", str(WORK), "-B", str(WORK / "build"), "-G", "Ninja", "-DCMAKE_CXX_FLAGS="], check=True)
subprocess.run(["cmake", "--build", str(WORK / "build"), "--target", "layout", "-j", "2"], check=True)
subprocess.run([str(WORK / "build/layout")], check=True)
