// ui_karaoke.cpp - see ui_karaoke.h.
#include "ui_karaoke.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "app_config.h"
#include "board_audio.h"
#include "board_display.h"
#include "kara_web.h"
#include "karaoke_layout.h"
#include "karaoke.h"
#include "hw_profile.h"
#include "lang.h"
#include "my_lvgl.h"
#include "my_textinput.h"
#include "my_theme.h"
#include "ui_metrics.h"
#include "ui_yatzy.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

namespace UiKaraoke {

namespace {

constexpr uint32_t TICK_MS = 40;
constexpr uint32_t DEBOUNCE_MS = 500;
struct Layout {
    int32_t w, h, inset, gap, buttonH, headerH, infoY, lyricsY, lyricsH;
    int32_t visibleLines;
    int32_t lyricsW, seekY, seekH, seekW, timeX, bottomY, bottomW, listY, listW, jumpW, toolbarY, toolbarW;
    int32_t volumeX, volumeW, volumeLabelY, volumeSliderY, volumeH;
};

Layout layout()
{
    Layout l{};
    l.w = UiMetrics::screenW();
    l.h = UiMetrics::screenH();
    l.inset = std::clamp<int32_t>(UiMetrics::marginNormal(), 8, 12);
    l.gap = UiMetrics::marginSmall();
    l.buttonH = std::clamp<int32_t>(UiMetrics::buttonH(), 44, 52);
    l.headerH = l.buttonH + l.gap;
    l.infoY = l.headerH;
    l.lyricsY = l.infoY + my_font_14.line_height + l.gap;
    l.bottomW = (l.w - 2 * l.inset - 4 * l.gap) / 5;
    l.bottomY = l.h - l.inset - l.buttonH;
    l.seekH = std::max<int32_t>(28, my_font_14.line_height + 12);
    l.seekY = l.bottomY - l.gap - l.seekH;
    l.lyricsH = l.seekY - l.gap - l.lyricsY;
    l.visibleLines = KaraokeLayout::visibleLyricLines(l.w, l.lyricsH, my_font_24.line_height, l.gap);
    constexpr int32_t timeW = 124;
    const KaraokeLayout::VolumeRail rail = KaraokeLayout::volumeRail(
        l.w, l.inset, l.gap, l.buttonH, l.infoY, l.bottomY, my_font_14.line_height,
        UiMetrics::buttonWMin(), timeW);
    l.volumeX = rail.x;
    l.volumeW = rail.width;
    l.volumeLabelY = rail.labelY;
    l.volumeSliderY = rail.sliderY;
    l.volumeH = rail.height;
    l.lyricsW = rail.lyricsWidth;
    l.seekW = rail.seekWidth;
    l.timeX = rail.timeX;
    l.jumpW = std::clamp<int32_t>(l.w / 12, 44, 52);
    l.toolbarY = l.headerH + l.gap;
    l.toolbarW = l.w - l.jumpW - 2 * l.inset - l.gap;
    l.listY = l.toolbarY + l.buttonH + my_font_14.line_height + 2 * l.gap;
    l.listW = l.w - l.jumpW - l.gap;
    return l;
}

void memoryMark(const char *point)
{
    ESP_LOGI("kara-ui", "%s: internal=%u PSRAM=%u largest=%u", point,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

// List screen
lv_obj_t *s_listScr = nullptr;
lv_obj_t *s_fontLbl = nullptr;
lv_timer_t *s_listTimer = nullptr;

// Bottom sheet (settings, tracks, sound bank) on the active screen
lv_obj_t *s_sheet = nullptr;
// v0.13.6.1: "sound bank loading" dialog, closed automatically when loading ends
lv_obj_t *s_waitDlg = nullptr;
std::vector<Karaoke::FontFile> s_fonts;
bool s_fontForSong = false;

// Player screen
struct Player {
    lv_obj_t *scr, *lyrics, *cur, *slider, *volumeTrack, *volumeSlider, *volumeValue, *timeLbl,
        *playBtn, *playLbl, *melBtnLbl, *diag, *fontLbl;
    std::vector<lv_obj_t *> before, after;
    lv_span_t *spSung, *spRest;
    lv_obj_t *cdRow;                                // v0.13.6 countdown dots (hidden outside pauses)
    lv_obj_t *cdDot[Karaoke::COUNTDOWN_MAX];
    int cdDots, cdLit;
    Karaoke::FontState fontState;
    lv_timer_t *timer;
    int line, syl;
    uint32_t lastSec;
    bool dragging, volumeDragging, volumeUpdating, volumeChanged, finished;
};
Player P{};
lv_timer_t *s_volumeSaveTimer = nullptr;

void persistVolume(lv_timer_t *timer)
{
    if (timer) {
        if (timer != s_volumeSaveTimer) return;
        s_volumeSaveTimer = nullptr;  // A one-shot timer deletes itself after this callback.
    } else if (s_volumeSaveTimer) {
        lv_timer_delete(s_volumeSaveTimer);
        s_volumeSaveTimer = nullptr;
    }
    P.volumeChanged = false;
    Config::save();
}

void scheduleVolumeSave()
{
    if (!s_volumeSaveTimer) s_volumeSaveTimer = lv_timer_create(persistVolume, 700, nullptr);
    if (s_volumeSaveTimer) {
        lv_timer_set_repeat_count(s_volumeSaveTimer, 1);
        lv_timer_reset(s_volumeSaveTimer);
    } else Config::save();
}

void syncVolumeControls()
{
    if (!P.volumeSlider) return;
    const uint8_t volume = Karaoke::volume();
    if (!P.volumeDragging && lv_slider_get_value(P.volumeSlider) != volume) {
        P.volumeUpdating = true;
        lv_slider_set_value(P.volumeSlider, volume, LV_ANIM_OFF);
        P.volumeUpdating = false;
    }
    if (P.volumeValue) lv_label_set_text_fmt(P.volumeValue, "%u%%", (unsigned)volume);
}

void showPlayer();
void showFonts(bool forSong);
void closeSheet();

void screenCleanup(lv_event_t *)
{
    // Async runs after LVGL has deleted the screen's children (including its text allocations).
    lv_async_call([](void *) { memoryMark("screen-cleanup"); }, nullptr);
}

void leaveToMenu()
{
    if (!Display::setLandscape(false)) return;   // Keeps the 180-degree setting
    memoryMark("portrait");
    UiYatzy::showMenu();
}

void fmtTime(char *buf, size_t cap, uint32_t us)
{
    const uint32_t s = us / 1000000;
    snprintf(buf, cap, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

void singleLine(lv_obj_t *label, int32_t width)
{
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(label, width, lv_obj_get_style_text_font(label, LV_PART_MAIN)->line_height);
}

lv_obj_t *backButton(lv_obj_t *scr)
{
    const auto l = layout();
    lv_obj_t *b = Theme::button(scr, LV_SYMBOL_LEFT, l.buttonH, l.buttonH, Theme::LINE);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -l.inset - l.gap - Theme::BATTERY_STATUS_WIDTH, 0);
    return b;
}

// ---- Song list ---------------------------------------------------------------------------------
void updateFontLabel()
{
    if (s_waitDlg && Karaoke::fontState() != Karaoke::FontState::LOADING) {
        lv_msgbox_close_async(s_waitDlg);
        s_waitDlg = nullptr;
    }
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

void msg(const char *text, bool isWait = false)
{
    if (isWait && s_waitDlg) {
        lv_msgbox_close_async(s_waitDlg);
        s_waitDlg = nullptr;
    }
    lv_obj_t *m = lv_msgbox_create(nullptr);
    lv_msgbox_add_text(m, text);
    if (isWait) {
        s_waitDlg = m;   // No close button: closed when the font state leaves LOADING
        lv_obj_add_event_cb(m, [](lv_event_t *e) {
            if (lv_event_get_target(e) == s_waitDlg) s_waitDlg = nullptr;
        }, LV_EVENT_DELETE, nullptr);
    } else {
        lv_msgbox_add_close_button(m);
    }
    Theme::msgboxFonts(m);
    const auto l = layout();
    lv_obj_set_width(m, std::min<int32_t>(l.w - 2 * l.inset, 600));
    lv_obj_set_style_max_height(m, l.h - 2 * l.inset, 0);
    lv_obj_set_scroll_dir(lv_msgbox_get_content(m), LV_DIR_VER);
    lv_obj_center(m);
}

// ---- v0.13.7 song browser: folders, search, A-Ö jump, pages ------------------------------------
enum class ListMode : uint8_t { ROOT, DIR, SEARCH };
struct Entry {
    bool folder;
    std::string name, path;   // Folder: display name + path relative to DIR_SONGS
    uint16_t idx;             // Song: library index (valid for s_gen)
    uint16_t count;           // Folder: songs inside (incl. subfolders)
};
constexpr size_t PAGE = 40;            // Entries per page (LVGL objects stay bounded with 2000 songs)
struct JumpKey {
    const char *text;
    uint16_t rank;
};
const JumpKey JUMP[] = {{"#", 0},  {"A", 30}, {"D", 33}, {"G", 36}, {"J", 39}, {"M", 42},
                        {"P", 45}, {"S", 48}, {"V", 51}, {"\xc3\x85", 56}, {"\xc3\x84", 57}, {"\xc3\x96", 58}};
constexpr int JUMP_N = (int)(sizeof(JUMP) / sizeof(JUMP[0]));

ListMode s_mode = ListMode::ROOT;
std::string s_dir, s_query, s_lastRel;
size_t s_page = 0;
uint32_t s_gen = 0, s_extSeen = 0;
bool s_wasScanning = false;
std::vector<Entry> s_entries;
size_t s_firstSong = 0;                // Index of the first song entry in s_entries
lv_obj_t *s_list = nullptr, *s_info = nullptr, *s_searchLbl = nullptr, *s_jumpCol = nullptr, *s_rescanBtn = nullptr;
lv_obj_t *s_input = nullptr;

void renderList(int scrollTo = -1);

std::string parentDir(const std::string &d)
{
    const size_t slash = d.find_last_of('/');
    return slash == std::string::npos ? std::string() : d.substr(0, slash);
}

void openEntry(size_t e)
{
    if (e >= s_entries.size()) return;
    const Entry &en = s_entries[e];
    if (en.folder) {
        s_mode = ListMode::DIR;
        s_dir = en.path;
        s_page = 0;
        renderList();
        return;
    }
    if (Karaoke::fontState() == Karaoke::FontState::LOADING) {
        msg(tr(Str::KARA_WAIT_FONT), true);
        return;
    }
    Karaoke::SongFile f;
    bool ok = false;
    Karaoke::withLibrary([&](const KaraIndex::Library &lib, uint32_t gen) {
        if (gen == s_gen && en.idx < lib.size()) f = Karaoke::songAt(lib, en.idx), ok = true;
    });
    if (!ok) {   // The library changed meanwhile (web rescan): show the new one
        renderList();
        return;
    }
    Karaoke::Err err = Karaoke::open(f);
    if (err != Karaoke::Err::OK) {
        msg(Karaoke::errText(err));
        return;
    }
    s_lastRel = f.path;
    s_extSeen = Karaoke::externalOpenGen();
    showPlayer();
}

// Builds s_entries for the current mode from the library
void buildEntries()
{
    s_entries.clear();
    s_firstSong = 0;
    Karaoke::withLibrary([](const KaraIndex::Library &lib, uint32_t gen) {
        s_gen = gen;
        std::vector<uint16_t> songs;
        if (s_mode == ListMode::SEARCH) {
            songs = lib.search(s_query.c_str());
        } else {
            std::vector<std::string> dirs = lib.subdirs(s_dir);
            if (s_mode == ListMode::ROOT && dirs.empty()) s_mode = ListMode::DIR;   // No folders: songs directly
            if (s_mode == ListMode::DIR && !s_dir.empty() && lib.songsIn(s_dir).empty()) {   // Folder gone
                s_dir.clear();
                dirs = lib.subdirs(s_dir);
                s_mode = dirs.empty() ? ListMode::DIR : ListMode::ROOT;
            }
            if (s_mode == ListMode::ROOT)
                s_entries.push_back(Entry{true, tr(Str::KARA_ALL_SONGS), std::string(), 0, (uint16_t)lib.size()});
            for (const std::string &d : dirs) {
                const std::string p = s_dir.empty() ? d : s_dir + "/" + d;
                s_entries.push_back(Entry{true, d, p, 0, (uint16_t)lib.songsIn(p).size()});
            }
            if (s_mode == ListMode::DIR) songs = lib.songsIn(s_dir);
        }
        s_firstSong = s_entries.size();
        for (uint16_t i : songs) s_entries.push_back(Entry{false, std::string(), std::string(), i, 0});
    });
}

void songButton(lv_obj_t *list, const KaraIndex::Library &lib, size_t e)
{
    const Entry &en = s_entries[e];
    const int32_t bw = (layout().listW - 28) / 2;
    lv_obj_t *b = lv_button_create(list);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, bw, 58);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_bg_color(b, Theme::c(Theme::PANEL), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    const bool cur = !s_lastRel.empty() && s_lastRel == std::string(Karaoke::DIR_SONGS) + "/" + lib.rel(en.idx);
    lv_obj_set_style_border_width(b, cur ? 2 : 1, 0);
    lv_obj_set_style_border_color(b, Theme::c(cur ? Theme::ACCENT : Theme::LINE), 0);
    lv_obj_set_style_bg_color(b, Theme::c(Theme::PANEL_RAISED), LV_STATE_PRESSED);
    const char *artist = lib.artist(en.idx);
    lv_obj_t *t = Theme::label(b, lib.title(en.idx), &my_font_18);
    singleLine(t, bw - 20);
    lv_obj_set_pos(t, 10, *artist ? 10 : 18);
    if (*artist) {
        lv_obj_t *a = Theme::label(b, artist, &my_font_14, Theme::MUTED);
        singleLine(a, bw - 20);
        lv_obj_set_pos(a, 10, 34);
    }
    static uint32_t openLast = 0;
    lv_obj_add_event_cb(b, [](lv_event_t *ev) {
        if (!Theme::debounce(openLast, 600)) return;
        openEntry((size_t)(intptr_t)lv_event_get_user_data(ev));
    }, LV_EVENT_CLICKED, (void *)(intptr_t)e);
}

lv_obj_t *rowButton(lv_obj_t *list, const char *text, const char *right, uint32_t border, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_button_create(list);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, layout().listW - 20, layout().buttonH);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_bg_color(b, Theme::c(Theme::PANEL_RAISED), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, Theme::c(border), 0);
    lv_obj_set_style_bg_color(b, Theme::c(Theme::PANEL), LV_STATE_PRESSED);
    lv_obj_t *t = Theme::label(b, text, &my_font_18);
    singleLine(t, layout().listW - 130);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 10, 0);
    if (right && *right) {
        lv_obj_t *r = Theme::label(b, right, &my_font_14, Theme::MUTED);
        lv_obj_align(r, LV_ALIGN_RIGHT_MID, -10, 0);
    }
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

void goUp()
{
    if (s_mode == ListMode::SEARCH) {
        s_query.clear();
        s_mode = ListMode::ROOT;
        s_dir.clear();
    } else if (s_mode == ListMode::DIR && !s_dir.empty()) {
        s_dir = parentDir(s_dir);
        if (s_dir.empty()) s_mode = ListMode::ROOT;
    } else {
        s_mode = ListMode::ROOT;
    }
    s_page = 0;
    renderList();
}

// Can the header back button go up inside the browser (instead of leaving karaoke)?
bool canGoUp()
{
    if (s_mode == ListMode::SEARCH) return true;
    if (s_mode != ListMode::DIR) return false;
    if (!s_dir.empty()) return true;
    // "All songs" view: up = folder view, but only when folders exist
    bool dirs = false;
    Karaoke::withLibrary([&](const KaraIndex::Library &lib, uint32_t) { dirs = !lib.subdirs("").empty(); });
    return dirs;
}

void updateInfo()
{
    if (!s_info) return;
    char buf[200];
    const Karaoke::Err e = Karaoke::libraryError();
    const size_t songs = s_entries.size() - s_firstSong;
    if (Karaoke::scanning()) {
        snprintf(buf, sizeof(buf), "%s", tr(Str::KARA_SCANNING));
    } else if (e != Karaoke::Err::OK) {
        snprintf(buf, sizeof(buf), "%s", Karaoke::errText(e));
    } else if (s_mode == ListMode::SEARCH) {
        if (songs)
            snprintf(buf, sizeof(buf), tr(Str::KARA_SEARCH_RESULT), (unsigned)songs, s_query.c_str());
        else
            snprintf(buf, sizeof(buf), "%s: \"%s\"", tr(Str::KARA_NO_MATCH), s_query.c_str());
    } else if (s_entries.empty()) {
        snprintf(buf, sizeof(buf), "%s", tr(Str::KARA_NO_SONGS));
    } else if (s_mode == ListMode::ROOT) {
        snprintf(buf, sizeof(buf), tr(Str::KARA_SONGS), (unsigned)s_entries[0].count);
    } else {
        // "Pop / 80s  ·  12 songs"
        char n[48];
        snprintf(n, sizeof(n), tr(Str::KARA_N_SONGS), (unsigned)songs);
        snprintf(buf, sizeof(buf), LV_SYMBOL_DIRECTORY " %.120s  -  %s",
                 s_dir.empty() ? tr(Str::KARA_ALL_SONGS) : s_dir.c_str(), n);
    }
    bool full = false;
    Karaoke::withLibrary([&](const KaraIndex::Library &lib, uint32_t) { full = lib.full(); });
    if (full && !Karaoke::scanning() && s_mode == ListMode::ROOT) {
        const size_t used = strlen(buf);
        snprintf(buf + used, sizeof(buf) - used, "  (");
        const size_t used2 = strlen(buf);
        snprintf(buf + used2, sizeof(buf) - used2, tr(Str::KARA_LIMIT), (unsigned)Karaoke::MAX_SONGS);
        const size_t used3 = strlen(buf);
        snprintf(buf + used3, sizeof(buf) - used3, ")");
    }
    lv_label_set_text(s_info, buf);
    lv_label_set_text(s_searchLbl, s_mode == ListMode::SEARCH ? s_query.c_str() : tr(Str::KARA_SEARCH_HINT));
    lv_obj_set_style_text_color(s_searchLbl, Theme::c(s_mode == ListMode::SEARCH ? Theme::TEXT : Theme::MUTED), 0);
    if (s_rescanBtn) {
        if (Karaoke::scanning()) lv_obj_add_state(s_rescanBtn, LV_STATE_DISABLED);
        else lv_obj_remove_state(s_rescanBtn, LV_STATE_DISABLED);
    }
}

// Rebuilds the visible page. scrollTo: entry index to scroll into view (A-Ö jump), -1 = top.
void renderList(int scrollTo)
{
    if (!s_list) return;
    buildEntries();
    const size_t pages = s_entries.empty() ? 1 : (s_entries.size() + PAGE - 1) / PAGE;
    if (scrollTo >= 0) s_page = (size_t)scrollTo / PAGE;
    if (s_page >= pages) s_page = pages - 1;
    lv_obj_clean(s_list);
    lv_obj_t *target = nullptr;

    if (s_mode != ListMode::ROOT && canGoUp()) {
        static uint32_t upLast = 0;
        char t[64];
        snprintf(t, sizeof(t), LV_SYMBOL_LEFT "  %s", tr(Str::KARA_UP));
        rowButton(s_list, t, nullptr, Theme::ACCENT, [](lv_event_t *) {
            if (Theme::debounce(upLast, DEBOUNCE_MS)) goUp();
        }, nullptr);
    }
    const size_t from = s_page * PAGE, to = std::min(s_entries.size(), from + PAGE);
    Karaoke::withLibrary([&](const KaraIndex::Library &lib, uint32_t gen) {
        if (gen != s_gen) return;   // Replaced between build and draw: the timer redraws
        for (size_t e = from; e < to; e++) {
            const Entry &en = s_entries[e];
            lv_obj_t *o;
            if (en.folder) {
                static uint32_t dirLast = 0;
                char t[160], r[32];
                snprintf(t, sizeof(t), "%s  %.140s", en.path.empty() ? LV_SYMBOL_LIST : LV_SYMBOL_DIRECTORY, en.name.c_str());
                snprintf(r, sizeof(r), tr(Str::KARA_N_SONGS), (unsigned)en.count);
                o = rowButton(s_list, t, r, en.path.empty() ? Theme::TEAL : Theme::LINE, [](lv_event_t *ev) {
                    if (!Theme::debounce(dirLast, DEBOUNCE_MS)) return;
                    openEntry((size_t)(intptr_t)lv_event_get_user_data(ev));
                }, (void *)(intptr_t)e);
            } else {
                songButton(s_list, lib, e);
                o = lv_obj_get_child(s_list, (int32_t)lv_obj_get_child_count(s_list) - 1);
            }
            if ((int)e == scrollTo) target = o;
        }
    });
    if (pages > 1) {   // Page row: < Page 2/5 >
        lv_obj_t *row = lv_obj_create(s_list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, layout().listW - 20, layout().buttonH);
        static uint32_t pgLast = 0;
        lv_obj_t *pb = Theme::button(row, LV_SYMBOL_LEFT, 90, 40, Theme::LINE);
        lv_obj_align(pb, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t *nb = Theme::button(row, LV_SYMBOL_RIGHT, 90, 40, Theme::LINE);
        lv_obj_align(nb, LV_ALIGN_RIGHT_MID, 0, 0);
        if (s_page == 0) lv_obj_add_state(pb, LV_STATE_DISABLED);
        if (s_page + 1 >= pages) lv_obj_add_state(nb, LV_STATE_DISABLED);
        char t[48];
        snprintf(t, sizeof(t), tr(Str::KARA_PAGE), (unsigned)(s_page + 1), (unsigned)pages);
        lv_obj_t *l = Theme::label(row, t, &my_font_18, Theme::MUTED);
        lv_obj_center(l);
        lv_obj_add_event_cb(pb, [](lv_event_t *) {
            if (!Theme::debounce(pgLast, 300) || s_page == 0) return;
            s_page--;
            renderList();
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(nb, [](lv_event_t *) {
            if (!Theme::debounce(pgLast, 300)) return;
            s_page++;
            renderList();
        }, LV_EVENT_CLICKED, nullptr);
    }
    lv_obj_update_layout(s_list);
    if (target) lv_obj_scroll_to_view(target, LV_ANIM_OFF);
    else lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
    // The A-Ö column only makes sense while songs are listed
    if (s_jumpCol) lv_obj_set_hidden(s_jumpCol, s_entries.size() <= s_firstSong + 8);
    updateInfo();
}

void jumpTo(uint16_t rank)
{
    size_t i = 0;
    Karaoke::withLibrary([&](const KaraIndex::Library &lib, uint32_t gen) {
        if (gen != s_gen) return;
        std::vector<uint16_t> view;
        for (size_t e = s_firstSong; e < s_entries.size(); e++) view.push_back(s_entries[e].idx);
        i = KaraIndex::jumpIndex(lib, view, rank);
        if (i == view.size() && i) i--;   // Nothing at/after the letter: the last song
    });
    renderList((int)(s_firstSong + i));
}

void searchDone(const char *text, bool confirmed, void *)
{
    if (!confirmed) {   // BACK closes the widget itself
        s_input = nullptr;
        return;
    }
    TextInput::close(s_input);
    s_input = nullptr;
    std::string q = text ? text : "";
    while (!q.empty() && q.back() == ' ') q.pop_back();
    while (!q.empty() && q[0] == ' ') q.erase(0, 1);
    if (q.empty()) {
        if (s_mode == ListMode::SEARCH) goUp();
        return;
    }
    s_query = q;
    s_mode = ListMode::SEARCH;
    s_page = 0;
    renderList();
}

void listTimer(lv_timer_t *)
{
    updateFontLabel();
    // A song was opened from the web song picker: follow it
    if (Karaoke::externalOpenGen() != s_extSeen && Karaoke::isOpen()) {
        s_extSeen = Karaoke::externalOpenGen();
        s_lastRel = Karaoke::current().path;
        if (s_input) {
            TextInput::close(s_input);
            s_input = nullptr;
        }
        lv_async_call([](void *) { showPlayer(); }, nullptr);
        return;
    }
    const bool sc = Karaoke::scanning();
    if (Karaoke::libraryGen() != s_gen && !sc && !s_input) {
        renderList();
    } else if (sc != s_wasScanning) {
        updateInfo();
    }
    s_wasScanning = sc;
}

void listDeleted(lv_event_t *e)
{
    if (lv_event_get_target(e) != s_listScr) return;   // An older list screen fading out
    s_listScr = nullptr;
    if (s_listTimer) lv_timer_delete(s_listTimer);
    s_listTimer = nullptr;
    s_fontLbl = s_list = s_info = s_searchLbl = s_jumpCol = s_rescanBtn = s_input = nullptr;
    std::vector<Entry>().swap(s_entries);   // The page callbacks/timer no longer need this UI-only view
}

void showList()
{
    closeSheet();
    const auto l = layout();
    Audio::music(Sound::Music::NONE);
    if (P.timer) lv_timer_pause(P.timer);   // Do not poll lyrics after Karaoke::close during the fade
    if (s_listTimer) lv_timer_delete(s_listTimer);
    s_listTimer = nullptr;
    lv_obj_t *scr = s_listScr = Theme::screen();
    lv_obj_t *header = Theme::header(scr, tr(Str::MENU_KARAOKE));
    lv_obj_set_height(header, l.headerH);
    singleLine(lv_obj_get_child(header, 0),
               l.w - l.buttonH - Theme::BATTERY_STATUS_WIDTH - 3 * l.inset - l.gap);
    static uint32_t backLast = 0;
    lv_obj_add_event_cb(backButton(scr), [](lv_event_t *) {
        if (!Theme::debounce(backLast, DEBOUNCE_MS)) return;
        if (canGoUp() || s_mode == ListMode::SEARCH) {   // v0.13.7: up one folder level first
            goUp();
            return;
        }
        if (Karaoke::fontState() == Karaoke::FontState::LOADING) {
            msg(tr(Str::KARA_WAIT_FONT), true);
            return;
        }
        Karaoke::unloadFont();
        leaveToMenu();
    }, LV_EVENT_CLICKED, nullptr);

    // Row 1: search field | rescan | sound bank
    lv_obj_t *sf = lv_button_create(scr);
    lv_obj_remove_style_all(sf);
    const int32_t actionW = std::max<int32_t>(120, l.toolbarW / 4);
    const int32_t searchW = l.toolbarW - 2 * (actionW + l.gap);
    lv_obj_set_size(sf, searchW, l.buttonH);
    lv_obj_set_pos(sf, l.inset, l.toolbarY);
    lv_obj_set_style_radius(sf, 8, 0);
    lv_obj_set_style_bg_color(sf, Theme::c(Theme::NEST_BG), 0);
    lv_obj_set_style_bg_opa(sf, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sf, 1, 0);
    lv_obj_set_style_border_color(sf, Theme::c(Theme::LINE), 0);
    lv_obj_t *mag = Theme::label(sf, LV_SYMBOL_EDIT, &my_font_14, Theme::MUTED);
    lv_obj_align(mag, LV_ALIGN_LEFT_MID, 8, 0);
    s_searchLbl = Theme::label(sf, "", &my_font_14, Theme::MUTED);
    singleLine(s_searchLbl, searchW - 38);
    lv_obj_align(s_searchLbl, LV_ALIGN_LEFT_MID, 30, 0);
    static uint32_t searchLast = 0;
    lv_obj_add_event_cb(sf, [](lv_event_t *) {
        if (!Theme::debounce(searchLast, DEBOUNCE_MS) || s_input) return;
        const auto l = layout();
        s_input = TextInput::open(lv_screen_active(), 0, 0, l.w, l.h, tr(Str::KARA_SEARCH_TITLE),
                                  s_mode == ListMode::SEARCH ? s_query.c_str() : "", 40, false, searchDone, nullptr);
    }, LV_EVENT_CLICKED, nullptr);

    static uint32_t rescanLast = 0;
    s_rescanBtn = Theme::button(scr, tr(Str::KARA_RESCAN), actionW, l.buttonH, Theme::GOLD);
    lv_obj_set_pos(s_rescanBtn, l.inset + searchW + l.gap, l.toolbarY);
    lv_obj_add_event_cb(s_rescanBtn, [](lv_event_t *) {
        if (!Theme::debounce(rescanLast, 2000) || Karaoke::scanning()) return;
        Karaoke::rescanAsync();
        updateInfo();
    }, LV_EVENT_CLICKED, nullptr);

    static uint32_t fontLast = 0;
    lv_obj_t *fb = Theme::button(scr, tr(Str::KARA_BTN_FONT), actionW, l.buttonH, Theme::TEAL);
    lv_obj_set_pos(fb, l.inset + searchW + actionW + 2 * l.gap, l.toolbarY);
    lv_obj_add_event_cb(fb, [](lv_event_t *) {
        if (Theme::debounce(fontLast, DEBOUNCE_MS)) showFonts(false);
    }, LV_EVENT_CLICKED, nullptr);

    // Row 2: where we are / count (left) and the sound bank state (right)
    s_info = Theme::label(scr, "", &my_font_14, Theme::MUTED);
    singleLine(s_info, l.toolbarW / 2 - l.gap);
    lv_obj_set_pos(s_info, l.inset, l.toolbarY + l.buttonH + l.gap);
    s_fontLbl = Theme::label(scr, "", &my_font_14, Theme::MUTED);
    lv_obj_set_width(s_fontLbl, l.toolbarW / 2);
    lv_label_set_long_mode(s_fontLbl, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(s_fontLbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_fontLbl, l.inset + l.toolbarW / 2, l.toolbarY + l.buttonH + l.gap);
    // Load the default font only when none is loaded; a song font from the last song stays until a
    // song wants another one (saves a few seconds of loading).
    const Karaoke::FontState fs = Karaoke::fontState();
    if (fs == Karaoke::FontState::NONE || fs == Karaoke::FontState::FAILED) Karaoke::startLoadFont();
    updateFontLabel();

    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, l.listW, l.h - l.listY);
    lv_obj_set_pos(s_list, 0, l.listY);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_ROW_WRAP);   // Two song columns in landscape
    lv_obj_set_style_pad_row(s_list, 6, 0);
    lv_obj_set_style_pad_column(s_list, 8, 0);
    lv_obj_set_style_pad_hor(s_list, 10, 0);
    lv_obj_set_style_pad_bottom(s_list, 10, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

    // A-Ö jump column along the right edge
    s_jumpCol = lv_obj_create(scr);
    lv_obj_remove_style_all(s_jumpCol);
    lv_obj_set_size(s_jumpCol, l.jumpW, l.h - l.toolbarY);
    lv_obj_set_pos(s_jumpCol, l.w - l.jumpW, l.toolbarY);
    lv_obj_set_scroll_dir(s_jumpCol, LV_DIR_VER);
    const int32_t kh = std::max<int32_t>(l.buttonH, (l.h - l.toolbarY) / JUMP_N);
    for (int i = 0; i < JUMP_N; i++) {
        lv_obj_t *k = lv_button_create(s_jumpCol);
        lv_obj_remove_style_all(k);
        lv_obj_set_size(k, l.jumpW, kh - 2);
        lv_obj_set_pos(k, 0, i * kh);
        lv_obj_set_style_radius(k, 6, 0);
        lv_obj_set_style_bg_color(k, Theme::c(Theme::PANEL), 0);
        lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(k, Theme::c(Theme::ACCENT), LV_STATE_PRESSED);
        lv_obj_t *l = Theme::label(k, JUMP[i].text, &my_font_14, Theme::TEXT);
        lv_obj_center(l);
        static uint32_t jumpLast = 0;
        lv_obj_add_event_cb(k, [](lv_event_t *ev) {
            if (!Theme::debounce(jumpLast, 250)) return;
            jumpTo(JUMP[(int)(intptr_t)lv_event_get_user_data(ev)].rank);
        }, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    s_wasScanning = Karaoke::scanning();
    renderList();
    s_listTimer = lv_timer_create(listTimer, 250, nullptr);
    lv_obj_add_event_cb(scr, listDeleted, LV_EVENT_DELETE, nullptr);
    lv_obj_add_event_cb(scr, screenCleanup, LV_EVENT_DELETE, nullptr);
    Theme::loadScreen(scr);
    memoryMark("list");
}

// ---- Player ------------------------------------------------------------------------------------
void setLine(lv_obj_t *label, int line)
{
    const Midi::Song *s = Karaoke::song();
    lv_label_set_text(label, s && line >= 0 && line < (int)s->lines.size() ? s->lineText(line).c_str() : "");
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
        const int current = line < 0 ? 0 : line;
        const int beforeCount = (int)P.before.size();
        for (int i = 0; i < beforeCount; i++) setLine(P.before[i], current - beforeCount + i);
        for (int i = 0; i < (int)P.after.size(); i++) setLine(P.after[i], current + i + 1);
        P.syl = -2;
    }
    if (v.syl != P.syl || force) {
        P.syl = v.syl;
        std::string sung, rest;
        if (line < 0) {
            s->lineSplit(0, -1, sung, rest);   // First line, nothing sung yet
        } else {
            s->lineSplit(line, v.syl + 1, sung, rest);
        }
        lv_spangroup_set_span_text(P.cur, P.spSung, sung.c_str());
        lv_spangroup_set_span_text(P.cur, P.spRest, rest.c_str());
        lv_spangroup_refresh(P.cur);
        lv_obj_update_layout(P.lyrics);
        // Follow the sung wrap row, not just the start of a potentially very long active line.
        const auto coords = lv_spangroup_get_span_coords(P.cur, P.spSung);
        const int32_t sungY = sung.empty() ? 0 : std::max({coords.heading.y2, coords.middle.y2, coords.trailing.y2});
        const int32_t target = lv_obj_get_y(P.cur) + sungY - lv_obj_get_height(P.lyrics) / 2;
        lv_obj_scroll_to_y(P.lyrics, std::max<int32_t>(0, target), LV_ANIM_OFF);
    }
}

// v0.13.6: countdown in long pauses. dots = total (0 hides the row), elapsed = lit dots (left to
// right, sung-syllable colour); the rest are white. Only redraws on change.
constexpr int32_t CD_DOT = 13, CD_GAP = 8;
void onCountdown(int dots, int elapsed)
{
    if (!P.cdRow || (dots == P.cdDots && elapsed == P.cdLit)) return;
    if (dots > Karaoke::COUNTDOWN_MAX) dots = Karaoke::COUNTDOWN_MAX;
    P.cdDots = dots;
    P.cdLit = elapsed;
    if (dots <= 0) {
        lv_obj_set_hidden(P.cdRow, true);
        return;
    }
    lv_obj_set_width(P.cdRow, dots * CD_DOT + (dots - 1) * CD_GAP);
    for (int i = 0; i < Karaoke::COUNTDOWN_MAX; i++) {
        if (i >= dots) {
            lv_obj_set_hidden(P.cdDot[i], true);
            continue;
        }
        lv_obj_set_hidden(P.cdDot[i], false);
        lv_obj_set_pos(P.cdDot[i], i * (CD_DOT + CD_GAP), 0);
        lv_obj_set_style_bg_color(P.cdDot[i], Theme::c(i < elapsed ? Theme::GOLD : Theme::TEXT), 0);
    }
    lv_obj_set_hidden(P.cdRow, false);
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
void closeSheet();

void tick(lv_timer_t *)
{
    // v0.13.7: another song was opened from the web song picker: show it
    if (Karaoke::externalOpenGen() != s_extSeen && Karaoke::isOpen()) {
        s_extSeen = Karaoke::externalOpenGen();
        s_lastRel = Karaoke::current().path;
        closeSheet();
        lv_async_call([](void *) { showPlayer(); }, nullptr);
        return;
    }
    // The song wants another sound bank (per-song choice, or the default changed): reload
    if (Karaoke::fontWanted()) Karaoke::ensureFont(false);
    // v0.13.4: a new key needed more samples than fit; the previous key is back
    if (Karaoke::takeTransposeRejected()) {
        refreshSteppers();
        msg(tr(Str::KARA_TR_REJECTED));
    }
    if (Karaoke::fontState() != P.fontState) {
        P.fontState = Karaoke::fontState();
        if (s_waitDlg && P.fontState != Karaoke::FontState::LOADING) {
            lv_msgbox_close_async(s_waitDlg);
            s_waitDlg = nullptr;
        }
        updatePlayerFont();
        P.lastSec = 0xFFFFFFFF;   // Refresh the diagnostics line
        P.line = -3;
    }
    const Karaoke::View v = Karaoke::view();
    syncVolumeControls();
    updateLyrics(v, false);
    onCountdown(v.cdDots, v.cdLit);
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
    if (s_volumeSaveTimer || P.volumeChanged) persistVolume(nullptr);
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
    const auto l = layout();
    s_sheet = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_sheet);
    lv_obj_set_size(s_sheet, l.w, l.h);
    lv_obj_set_scrollable(s_sheet, false);
    lv_obj_set_style_bg_color(s_sheet, Theme::c(0x000000), 0);
    lv_obj_set_style_bg_opa(s_sheet, LV_OPA_60, 0);
    lv_obj_set_clickable(s_sheet, true);   // Swallow taps behind the card
    lv_obj_add_event_cb(s_sheet, [](lv_event_t *e) {
        if (lv_event_get_target(e) == s_sheet) s_sheet = nullptr;
    }, LV_EVENT_DELETE, nullptr);
    lv_obj_t *c = Theme::card(s_sheet, l.w - 2 * l.inset, std::min(h, l.h - 2 * l.inset));
    lv_obj_align(c, LV_ALIGN_BOTTOM_MID, 0, -l.inset);
    lv_obj_set_scrollable(c, false);
    lv_obj_t *t = Theme::label(c, title, &my_font_24);
    lv_obj_set_pos(t, 0, 0);
    lv_obj_set_width(t, l.w - 2 * l.inset - 18 - l.buttonH - l.gap);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
    static uint32_t closeLast = 0;
    lv_obj_t *x = Theme::button(c, LV_SYMBOL_CLOSE, l.buttonH, l.buttonH, Theme::LINE);
    lv_obj_align(x, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(x, [](lv_event_t *) {
        if (Theme::debounce(closeLast, DEBOUNCE_MS)) lv_async_call([](void *) { closeSheet(); }, nullptr);
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_update_layout(c);
    const int32_t top = std::max(l.buttonH, lv_obj_get_height(t)) + l.gap;
    lv_obj_t *body = lv_obj_create(c);
    lv_obj_remove_style_all(body);
    lv_obj_set_pos(body, 0, top);
    lv_obj_set_size(body, lv_obj_get_content_width(c), lv_obj_get_content_height(c) - top);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, l.gap, 0);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scroll_chain(body, false);
    return body;
}

struct Stepper {
    lv_obj_t *value;
    int kind;   // 0 tempo, 1 key
};
Stepper s_steps[2];

void refreshSteppers()
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d %%", Karaoke::tempo());
    if (s_steps[0].value) lv_label_set_text(s_steps[0].value, buf);
    snprintf(buf, sizeof(buf), "%+d", Karaoke::transpose());
    if (s_steps[1].value) lv_label_set_text(s_steps[1].value, buf);
    syncVolumeControls();
}

void stepCb(lv_event_t *e)
{
    static uint32_t last[4] = {};
    const int code = (int)(intptr_t)lv_event_get_user_data(e);   // kind * 2 + (0 = minus, 1 = plus)
    if (!Theme::debounce(last[code], 200)) return;
    const int dir = (code & 1) ? 1 : -1;
    switch (code / 2) {
    case 0: Karaoke::setTempo(Karaoke::tempo() + dir * 5); break;
    case 1: Karaoke::setTranspose(Karaoke::transpose() + dir); break;
    }
    refreshSteppers();
}

void showSettings()
{
    const auto l = layout();
    lv_obj_t *c = sheetBase(tr(Str::KARA_BTN_SETTINGS), 220);
    const Str names[2] = {Str::KARA_TEMPO, Str::KARA_KEY};
    for (int k = 0; k < 2; k++) {
        lv_obj_t *row = lv_obj_create(c);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), l.buttonH);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, l.gap, 0);
        lv_obj_t *n = Theme::label(row, tr(names[k]), &my_font_18, Theme::MUTED);
        lv_obj_set_width(n, 0);
        lv_obj_set_flex_grow(n, 1);
        ctlButton(row, LV_SYMBOL_MINUS, 0, 0, l.buttonH, l.buttonH, Theme::TEAL, stepCb, (void *)(intptr_t)(k * 2));
        s_steps[k].value = Theme::label(row, "", &my_font_24);
        s_steps[k].kind = k;
        lv_obj_set_width(s_steps[k].value, 84);
        lv_obj_set_style_text_align(s_steps[k].value, LV_TEXT_ALIGN_CENTER, 0);
        ctlButton(row, LV_SYMBOL_PLUS, 0, 0, l.buttonH, l.buttonH, Theme::TEAL, stepCb, (void *)(intptr_t)(k * 2 + 1));
        lv_obj_add_event_cb(s_steps[k].value, [](lv_event_t *ev) {
            s_steps[(int)(intptr_t)lv_event_get_user_data(ev)].value = nullptr;
        }, LV_EVENT_DELETE, (void *)(intptr_t)k);
    }
    lv_obj_t *note = Theme::label(c, tr(Str::KARA_SAVED_NOTE), &my_font_14, Theme::MUTED);
    lv_obj_set_width(note, LV_PCT(100));
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
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
    lv_obj_t *c = sheetBase(tr(Str::KARA_PICK_MELODY), layout().h);
    lv_obj_t *hint = Theme::label(c, tr(Str::KARA_PICK_HINT), &my_font_14, Theme::MUTED);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_t *list = lv_obj_create(c);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_scrollable(list, false);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    const int sel = Karaoke::melody();
    for (int ch = -1; ch < 16; ch++) {
        if (ch >= 0 && !(s->channelMask & (1u << ch))) continue;
        if (ch == Midi::DRUM_CH) continue;   // Drums are never the melody
        lv_obj_t *b = lv_button_create(list);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, LV_PCT(100), ch < 0 ? layout().buttonH : 58);
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
        singleLine(t, LV_PCT(95));
        lv_obj_set_pos(t, 8, 6);
        if (ch >= 0) {
            snprintf(buf, sizeof(buf), "%u %s  |  %u%% %s  |  max %u%s%s", (unsigned)s->ch[ch].notes, LV_SYMBOL_AUDIO,
                     (unsigned)s->ch[ch].lyricMatchPct, tr(Str::KARA_MATCH), (unsigned)s->ch[ch].maxPoly,
                     ch == s->melody ? "  |  " : "", ch == s->melody ? tr(Str::KARA_AUTO) : "");
            lv_obj_t *d = Theme::label(b, buf, &my_font_14, Theme::MUTED);
            singleLine(d, LV_PCT(95));
            lv_obj_set_pos(d, 8, 28);
        }
        lv_obj_add_event_cb(b, pickCb, LV_EVENT_CLICKED, (void *)(intptr_t)(ch + 1));
    }
}

// ---- Sound bank picker (global default from the list, per song from the player) ----------------
void voicePickCb(lv_event_t *e)
{
    static uint32_t last = 0;
    if (!Theme::debounce(last, DEBOUNCE_MS)) return;
    const uint8_t voices = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    if (!Config::validKaraVoices(voices) || voices > HwProfile::active().maxKaraokeVoices) return;
    Karaoke::setVoiceLimit(voices);
    lv_obj_t *row = lv_obj_get_parent(static_cast<lv_obj_t *>(lv_event_get_target(e)));
    for (uint32_t i = 0; i < KARA_VOICE_OPTION_COUNT; i++) {
        if (KARA_VOICE_OPTIONS[i] > HwProfile::active().maxKaraokeVoices) break;
        lv_obj_t *button = lv_obj_get_child(row, (int32_t)i);
        const bool selected = KARA_VOICE_OPTIONS[i] == voices;
        lv_obj_set_style_bg_color(button, Theme::c(selected ? Theme::TEAL : Theme::LINE), 0);
        lv_obj_set_style_border_width(button, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(button, Theme::c(selected ? Theme::ACCENT : Theme::LINE), 0);
    }
}

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
        msg(tr(Str::KARA_WAIT_FONT), true);
        return;
    }
    if (forSong && !Karaoke::isOpen()) return;
    closeSheet();
    s_fontForSong = forSong;
    const Karaoke::Err se = Karaoke::scanFonts(s_fonts);
    lv_obj_t *c = sheetBase(tr(forSong ? Str::KARA_FONT_SONG : Str::KARA_FONT_DEFAULT), layout().h);
    lv_obj_add_event_cb(c, [](lv_event_t *) {
        std::vector<Karaoke::FontFile>().swap(s_fonts);   // Only the bank sheet uses this UI cache
    }, LV_EVENT_DELETE, nullptr);
    lv_obj_t *hint = Theme::label(c, tr(forSong ? Str::KARA_FONT_SONG_HINT : Str::KARA_FONT_SAVED), &my_font_14,
                                  Theme::MUTED);
    lv_obj_set_width(hint, LV_PCT(100));
    lv_obj_t *voiceLabel = Theme::label(c, tr(Str::KARA_VOICE_LIMIT), &my_font_14, Theme::MUTED);
    lv_obj_set_width(voiceLabel, LV_PCT(100));
    lv_obj_t *voiceRow = lv_obj_create(c);
    lv_obj_remove_style_all(voiceRow);
    lv_obj_set_size(voiceRow, LV_PCT(100), layout().buttonH);
    lv_obj_set_flex_flow(voiceRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(voiceRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(voiceRow, layout().gap, 0);
    for (uint8_t i = 0; i < KARA_VOICE_OPTION_COUNT; i++) {
        const uint8_t voices = KARA_VOICE_OPTIONS[i];
        if (voices > HwProfile::active().maxKaraokeVoices) break;
        const uint8_t selectedLimit = Config::get().karaVoices > HwProfile::active().maxKaraokeVoices
                                          ? HwProfile::active().maxKaraokeVoices
                                          : Config::get().karaVoices;
        const bool selected = selectedLimit == voices;
        char text[4];
        snprintf(text, sizeof(text), "%u", (unsigned)voices);
        lv_obj_t *button = Theme::button(voiceRow, text, 52, layout().buttonH,
                                         selected ? Theme::TEAL : Theme::LINE);
        lv_obj_set_width(button, 0);
        lv_obj_set_flex_grow(button, 1);
        lv_obj_set_style_border_width(button, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(button, Theme::c(selected ? Theme::ACCENT : Theme::LINE), 0);
        lv_obj_add_event_cb(button, voicePickCb, LV_EVENT_CLICKED, (void *)(intptr_t)voices);
    }
    lv_obj_t *list = lv_obj_create(c);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_scrollable(list, false);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    const std::string sel = forSong ? Karaoke::songFont() : Karaoke::defaultFont();
    for (int i = -1; i < (int)s_fonts.size(); i++) {
        const bool isSel = i < 0 ? sel.empty() : s_fonts[(size_t)i].name == sel;
        const bool ok = i < 0 || s_fonts[(size_t)i].err == Karaoke::Err::OK;
        lv_obj_t *b = lv_button_create(list);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, LV_PCT(100), i < 0 ? layout().buttonH : 58);
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
        singleLine(t, LV_PCT(95));
        lv_obj_set_pos(t, 8, i < 0 ? 9 : 5);
        if (i >= 0) {
            const Karaoke::FontFile &f = s_fonts[(size_t)i];
            char d[160];
            if (ok) snprintf(d, sizeof(d), "%.1f MB", (double)f.size / (1024.0 * 1024.0));
            else snprintf(d, sizeof(d), "%s", Karaoke::errText(f.err));
            lv_obj_t *dl = Theme::label(b, d, &my_font_14, ok ? Theme::MUTED : Theme::RED);
            singleLine(dl, LV_PCT(95));
            lv_obj_set_pos(dl, 8, 30);
        }
        if (ok) lv_obj_add_event_cb(b, fontPickCb, LV_EVENT_CLICKED, (void *)(intptr_t)(i + 1));
        else lv_obj_set_clickable(b, false);   // Rejected file (SF3, too big, not a SoundFont)
    }
    if (se != Karaoke::Err::OK || s_fonts.empty()) {
        lv_obj_t *n = Theme::label(list, se != Karaoke::Err::OK ? Karaoke::errText(se) : tr(Str::KARA_FONT_NONE),
                                   &my_font_14, Theme::RED);
        lv_obj_set_width(n, LV_PCT(100));
        lv_label_set_long_mode(n, LV_LABEL_LONG_MODE_WRAP);
    }
}

void updateMelodyButton()
{
    if (P.melBtnLbl) lv_label_set_text(P.melBtnLbl, tr(Karaoke::melodyMuted() ? Str::KARA_MELODY_OFF : Str::KARA_MELODY_ON));
}

void showPlayerMenu()
{
    const auto l = layout();
    lv_obj_t *body = sheetBase(LV_SYMBOL_LIST, l.buttonH * 4 + 56);
    auto item = [&](const char *text, lv_event_cb_t cb) {
        lv_obj_t *b = ctlButton(body, text, 0, 0, 1, l.buttonH, Theme::TEAL, cb);
        lv_obj_set_width(b, LV_PCT(100));
        return lv_obj_get_child(b, 0);
    };
    char text[128];
    snprintf(text, sizeof(text), "%s  %d%%  %+d", tr(Str::KARA_BTN_SETTINGS), Karaoke::tempo(), Karaoke::transpose());
    item(text, [](lv_event_t *) { lv_async_call([](void *) { showSettings(); }, nullptr); });
    P.melBtnLbl = item("", [](lv_event_t *) {
        static uint32_t last = 0;
        if (!Theme::debounce(last, DEBOUNCE_MS)) return;
        Karaoke::setMelodyMuted(!Karaoke::melodyMuted());
        updateMelodyButton();
    });
    lv_obj_add_event_cb(P.melBtnLbl, [](lv_event_t *e) {
        if (lv_event_get_target(e) == P.melBtnLbl) P.melBtnLbl = nullptr;
    }, LV_EVENT_DELETE, nullptr);
    const int ch = Karaoke::melody();
    char track[40];
    if (ch < 0) snprintf(track, sizeof(track), "%s", tr(Str::KARA_CH_NONE));
    else snprintf(track, sizeof(track), tr(Str::KARA_CH_FMT), ch + 1);
    snprintf(text, sizeof(text), "%s  %s", tr(Str::KARA_BTN_TRACK), track);
    item(text, [](lv_event_t *) { lv_async_call([](void *) { showTracks(); }, nullptr); });
    updateMelodyButton();
}

void showPlayer()
{
    const Midi::Song *song = Karaoke::song();
    if (!song) return;
    Karaoke::setVolume(Config::get().karaVolume);
    closeSheet();
    const auto l = layout();
    if (s_listTimer) lv_timer_pause(s_listTimer);
    const Karaoke::SongFile f = Karaoke::current();
    if (P.timer) lv_timer_delete(P.timer);
    P = Player{};
    P.line = -3;
    P.syl = -3;
    P.lastSec = 0xFFFFFFFF;
    P.fontState = (Karaoke::FontState)0xFF;   // Forces the first font label update
    lv_obj_t *scr = P.scr = Theme::screen();
    lv_obj_t *hdr = Theme::header(scr, "");
    lv_obj_set_height(hdr, l.headerH);
    lv_obj_t *title = lv_obj_get_child(hdr, 0);
    lv_label_set_text(title, f.title.c_str());
    singleLine(title, l.w - 2 * l.buttonH - Theme::BATTERY_STATUS_WIDTH - 3 * l.inset - 3 * l.gap);
    ctlButton(scr, LV_SYMBOL_LIST,
              l.w - 2 * l.buttonH - Theme::BATTERY_STATUS_WIDTH - l.inset - 2 * l.gap, 0,
              l.buttonH, l.buttonH, Theme::TEAL, [](lv_event_t *) { showPlayerMenu(); });
    static uint32_t backLast = 0;
    lv_obj_add_event_cb(backButton(scr), [](lv_event_t *) {
        if (!Theme::debounce(backLast, DEBOUNCE_MS)) return;
        Karaoke::close();
        showList();
    }, LV_EVENT_CLICKED, nullptr);

    // Info line: artist | active sound bank | voices + CPU
    lv_obj_t *artist = Theme::label(scr, f.artist.c_str(), &my_font_14, Theme::MUTED);
    const int32_t infoW = l.w - 2 * l.inset;
    singleLine(artist, infoW / 3 - l.gap);
    lv_obj_set_pos(artist, l.inset, l.infoY);
    P.fontLbl = Theme::label(scr, "", &my_font_14, Theme::MUTED);
    singleLine(P.fontLbl, infoW * 5 / 12 - l.gap);
    lv_obj_set_pos(P.fontLbl, l.inset + infoW / 3, l.infoY);
    P.diag = Theme::label(scr, "", &my_font_14, Theme::LINE);
    singleLine(P.diag, infoW / 4);
    lv_obj_set_style_text_align(P.diag, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(P.diag, l.inset + infoW * 3 / 4, l.infoY);

    // The line block fills the available lyric area and stays centered around the active line.
    P.lyrics = lv_obj_create(scr);
    lv_obj_remove_style_all(P.lyrics);
    lv_obj_set_pos(P.lyrics, l.inset, l.lyricsY);
    lv_obj_set_size(P.lyrics, l.lyricsW, l.lyricsH);
    lv_obj_set_flex_flow(P.lyrics, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(P.lyrics, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(P.lyrics, l.gap, 0);
    lv_obj_set_scroll_dir(P.lyrics, LV_DIR_VER);
    lv_obj_set_scroll_chain(P.lyrics, false);
    auto lyricLabel = [&](const lv_font_t *font, uint32_t color) {
        lv_obj_t *label = Theme::label(P.lyrics, "", font, color);
        lv_obj_set_width(label, LV_PCT(100));
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(label, 4, 0);
        return label;
    };
    const int beforeCount = (l.visibleLines - 1) / 2;
    const int afterCount = l.visibleLines - 1 - beforeCount;
    for (int i = 0; i < beforeCount; i++)
        P.before.push_back(lyricLabel(&my_font_18, Theme::LINE));
    P.cur = lv_spangroup_create(P.lyrics);
    lv_obj_set_width(P.cur, l.lyricsW);
    lv_obj_set_height(P.cur, LV_SIZE_CONTENT);
    lv_spangroup_set_max_lines(P.cur, -1);
    lv_spangroup_set_overflow(P.cur, LV_SPAN_OVERFLOW_CLIP);
    lv_obj_set_style_text_align(P.cur, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(P.cur, &my_font_24, 0);
    lv_obj_set_style_text_line_space(P.cur, 4, 0);
    P.spSung = lv_spangroup_add_span(P.cur);
    P.spRest = lv_spangroup_add_span(P.cur);
    lv_style_set_text_color(lv_span_get_style(P.spSung), Theme::c(Theme::GOLD));
    lv_style_set_text_color(lv_span_get_style(P.spRest), Theme::c(Theme::TEXT));
    // Countdown participates in lyric flow: it cannot cover wrapped text or controls.
    P.cdRow = lv_obj_create(P.lyrics);
    lv_obj_remove_style_all(P.cdRow);
    lv_obj_set_size(P.cdRow, CD_DOT, CD_DOT);
    lv_obj_set_clickable(P.cdRow, false);
    lv_obj_set_scrollable(P.cdRow, false);
    for (int i = 0; i < Karaoke::COUNTDOWN_MAX; i++) {
        lv_obj_t *d = lv_obj_create(P.cdRow);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, CD_DOT, CD_DOT);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(d, Theme::c(Theme::TEXT), 0);
        lv_obj_set_clickable(d, false);
        P.cdDot[i] = d;
    }
    lv_obj_set_hidden(P.cdRow, true);
    P.cdDots = P.cdLit = -1;   // Forces the first update
    for (int i = 0; i < afterCount; i++)
        P.after.push_back(lyricLabel(i == afterCount - 1 && l.visibleLines > 4 ? &my_font_14 : &my_font_18,
                                     i == 0 ? Theme::MUTED : Theme::LINE));

    P.slider = lv_slider_create(scr);
    const int32_t timeW = 124;
    lv_obj_set_size(P.slider, l.seekW, 8);
    lv_obj_set_pos(P.slider, 2 * l.inset, l.seekY + l.seekH / 2 - 4);
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
    singleLine(P.timeLbl, timeW);
    lv_obj_set_style_text_align(P.timeLbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(P.timeLbl, l.timeX, l.seekY + (l.seekH - my_font_14.line_height) / 2);

    P.volumeValue = Theme::label(scr, "", &my_font_14, Theme::MUTED);
    lv_obj_set_width(P.volumeValue, l.volumeW);
    lv_obj_set_style_text_align(P.volumeValue, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(P.volumeValue, l.volumeX, l.volumeLabelY);
    P.volumeTrack = lv_obj_create(scr);
    lv_obj_remove_style_all(P.volumeTrack);
    lv_obj_set_size(P.volumeTrack, KaraokeLayout::VOLUME_TRACK_WIDTH, l.volumeH);
    lv_obj_set_pos(P.volumeTrack, l.volumeX + (l.volumeW - KaraokeLayout::VOLUME_TRACK_WIDTH) / 2, l.volumeSliderY);
    lv_obj_set_style_radius(P.volumeTrack, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(P.volumeTrack, Theme::c(Theme::LINE), 0);
    lv_obj_set_style_bg_opa(P.volumeTrack, LV_OPA_30, 0);
    lv_obj_set_clickable(P.volumeTrack, false);
    lv_obj_set_scrollable(P.volumeTrack, false);
    P.volumeSlider = lv_slider_create(scr);
    lv_obj_remove_style_all(P.volumeSlider);
    lv_obj_set_size(P.volumeSlider, KaraokeLayout::VOLUME_TRACK_WIDTH, l.volumeH);
    lv_obj_set_pos(P.volumeSlider, l.volumeX + (l.volumeW - KaraokeLayout::VOLUME_TRACK_WIDTH) / 2, l.volumeSliderY);
    lv_obj_set_ext_click_area(P.volumeSlider, (l.volumeW - KaraokeLayout::VOLUME_TRACK_WIDTH + 1) / 2);
    lv_slider_set_orientation(P.volumeSlider, LV_SLIDER_ORIENTATION_VERTICAL);
    lv_slider_set_range(P.volumeSlider, 0, 100);
    lv_slider_set_value(P.volumeSlider, Karaoke::volume(), LV_ANIM_OFF);
    lv_obj_set_style_bg_opa(P.volumeSlider, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(P.volumeSlider, Theme::c(Theme::ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(P.volumeSlider, LV_OPA_40, LV_PART_INDICATOR);
    lv_obj_set_style_border_width(P.volumeSlider, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(P.volumeSlider, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(P.volumeSlider, Theme::c(Theme::TEXT), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(P.volumeSlider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_color(P.volumeSlider, Theme::c(Theme::ACCENT), LV_PART_KNOB);
    lv_obj_set_style_border_width(P.volumeSlider, 2, LV_PART_KNOB);
    lv_obj_set_style_radius(P.volumeSlider, 3, LV_PART_KNOB);
    // LVGL derives the drawn knob from the slider width plus padding, not knob width/height styles.
    lv_obj_set_style_pad_left(P.volumeSlider,
        (KaraokeLayout::VOLUME_KNOB_WIDTH - KaraokeLayout::VOLUME_TRACK_WIDTH) / 2, LV_PART_KNOB);
    lv_obj_set_style_pad_right(P.volumeSlider,
        (KaraokeLayout::VOLUME_KNOB_WIDTH - KaraokeLayout::VOLUME_TRACK_WIDTH) / 2, LV_PART_KNOB);
    lv_obj_set_style_pad_top(P.volumeSlider,
        (KaraokeLayout::VOLUME_KNOB_HEIGHT - KaraokeLayout::VOLUME_TRACK_WIDTH) / 2, LV_PART_KNOB);
    lv_obj_set_style_pad_bottom(P.volumeSlider,
        (KaraokeLayout::VOLUME_KNOB_HEIGHT - KaraokeLayout::VOLUME_TRACK_WIDTH) / 2, LV_PART_KNOB);
    lv_obj_add_event_cb(P.volumeSlider, [](lv_event_t *) {
        P.volumeDragging = true;
        P.volumeChanged = false;
    }, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(P.volumeSlider, [](lv_event_t *e) {
        if (P.volumeUpdating) return;
        Karaoke::setVolume((uint8_t)lv_slider_get_value(lv_event_get_target_obj(e)));
        P.volumeChanged = true;
        syncVolumeControls();
        if (!P.volumeDragging) scheduleVolumeSave();
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(P.volumeSlider, [](lv_event_t *) {
        P.volumeDragging = false;
        if (P.volumeChanged) {
            scheduleVolumeSave();
            P.volumeChanged = false;
        }
    }, LV_EVENT_RELEASED, nullptr);
    syncVolumeControls();

    // Five primary controls fit both supported landscape formats without a second row.
    const int32_t BW = l.bottomW, BH = l.buttonH, X0 = l.inset, DX = BW + l.gap, Y1 = l.bottomY;
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
            msg(tr(Str::KARA_WAIT_FONT), true);
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
    ctlButton(scr, LV_SYMBOL_STOP, X0 + 4 * DX, Y1, BW, BH, Theme::LINE, [](lv_event_t *) {
        static uint32_t last = 0;
        if (!Theme::debounce(last, DEBOUNCE_MS)) return;
        Karaoke::play(false);
        Karaoke::restart();
        updatePlayLabel(false);
        P.line = -3;
    });

    updateLyrics(Karaoke::view(), true);
    tick(nullptr);
    P.timer = lv_timer_create(tick, TICK_MS, nullptr);
    lv_obj_add_event_cb(scr, playerDeleted, LV_EVENT_DELETE, nullptr);
    lv_obj_add_event_cb(scr, screenCleanup, LV_EVENT_DELETE, nullptr);
    Theme::loadScreen(scr);
    memoryMark("player");
}

}  // namespace

void show()
{
    memoryMark("enter");
    if (!Display::setLandscape(true, Config::get().karaFlip != 0)) {
        msg(tr(Str::KARA_ERR_MEMORY));
        return;
    }
    memoryMark("landscape");
    // v0.13.7: always start in the folder view. The index makes this quick; without a valid index
    // the scan runs in the background (the list shows "scanning" and fills in when done).
    s_mode = ListMode::ROOT;
    s_dir.clear();
    s_query.clear();
    s_page = 0;
    s_lastRel.clear();
    s_extSeen = Karaoke::externalOpenGen();
    if (!Karaoke::loadIndex()) Karaoke::rescanAsync();
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
        const auto l = layout();
        lv_obj_set_width(q, std::min<int32_t>(l.w - 2 * l.inset, 600));
        lv_obj_set_style_max_height(q, l.h - 2 * l.inset, 0);
        lv_obj_set_scroll_dir(lv_msgbox_get_content(q), LV_DIR_VER);
        lv_obj_center(q);
    }
}

}  // namespace UiKaraoke
