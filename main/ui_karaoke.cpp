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
#include "karaoke.h"
#include "lang.h"
#include "my_lvgl.h"
#include "my_textinput.h"
#include "my_theme.h"
#include "ui_yatzy.h"

namespace UiKaraoke {

namespace {

constexpr uint32_t TICK_MS = 40;
constexpr uint32_t DEBOUNCE_MS = 500;
// v0.13.1: the karaoke module runs in landscape (LVGL software rotation), 480 x 320.
constexpr int32_t W = 480, H = 320;


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
    lv_obj_t *scr, *prev, *cur, *next, *next2, *slider, *timeLbl, *playBtn, *playLbl, *melBtnLbl, *diag, *fontLbl;
    lv_span_t *spSung, *spRest;
    lv_obj_t *cdRow;                                // v0.13.6 countdown dots (hidden outside pauses)
    lv_obj_t *cdDot[Karaoke::COUNTDOWN_MAX];
    int cdDots, cdLit;
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
constexpr int32_t JUMP_W = 40;         // A-Ö column on the right edge
constexpr int32_t LIST_Y = 108, LIST_W = W - JUMP_W - 4;
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
    const int32_t bw = (LIST_W - 28) / 2;
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
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(t, bw - 20);
    lv_obj_set_pos(t, 10, *artist ? 10 : 18);
    if (*artist) {
        lv_obj_t *a = Theme::label(b, artist, &my_font_14, Theme::MUTED);
        lv_label_set_long_mode(a, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(a, bw - 20);
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
    lv_obj_set_size(b, LIST_W - 20, 44);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_bg_color(b, Theme::c(Theme::PANEL_RAISED), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, Theme::c(border), 0);
    lv_obj_set_style_bg_color(b, Theme::c(Theme::PANEL), LV_STATE_PRESSED);
    lv_obj_t *t = Theme::label(b, text, &my_font_18);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(t, LIST_W - 130);
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
        lv_obj_set_size(row, LIST_W - 20, 44);
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
    lv_obj_set_size(sf, 170, 36);
    lv_obj_set_pos(sf, 10, 50);
    lv_obj_set_style_radius(sf, 8, 0);
    lv_obj_set_style_bg_color(sf, Theme::c(Theme::NEST_BG), 0);
    lv_obj_set_style_bg_opa(sf, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sf, 1, 0);
    lv_obj_set_style_border_color(sf, Theme::c(Theme::LINE), 0);
    lv_obj_t *mag = Theme::label(sf, LV_SYMBOL_EDIT, &my_font_14, Theme::MUTED);
    lv_obj_align(mag, LV_ALIGN_LEFT_MID, 8, 0);
    s_searchLbl = Theme::label(sf, "", &my_font_14, Theme::MUTED);
    lv_label_set_long_mode(s_searchLbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_searchLbl, 132);
    lv_obj_align(s_searchLbl, LV_ALIGN_LEFT_MID, 30, 0);
    static uint32_t searchLast = 0;
    lv_obj_add_event_cb(sf, [](lv_event_t *) {
        if (!Theme::debounce(searchLast, DEBOUNCE_MS) || s_input) return;
        s_input = TextInput::open(lv_screen_active(), 0, 0, W, H, tr(Str::KARA_SEARCH_TITLE),
                                  s_mode == ListMode::SEARCH ? s_query.c_str() : "", 40, false, searchDone, nullptr);
    }, LV_EVENT_CLICKED, nullptr);

    static uint32_t rescanLast = 0;
    s_rescanBtn = Theme::button(scr, tr(Str::KARA_RESCAN), 120, 36, Theme::GOLD);
    lv_obj_set_pos(s_rescanBtn, 186, 50);
    lv_obj_add_event_cb(s_rescanBtn, [](lv_event_t *) {
        if (!Theme::debounce(rescanLast, 2000) || Karaoke::scanning()) return;
        Karaoke::rescanAsync();
        updateInfo();
    }, LV_EVENT_CLICKED, nullptr);

    static uint32_t fontLast = 0;
    lv_obj_t *fb = Theme::button(scr, tr(Str::KARA_BTN_FONT), 120, 36, Theme::TEAL);
    lv_obj_set_pos(fb, 312, 50);
    lv_obj_add_event_cb(fb, [](lv_event_t *) {
        if (Theme::debounce(fontLast, DEBOUNCE_MS)) showFonts(false);
    }, LV_EVENT_CLICKED, nullptr);

    // Row 2: where we are / count (left) and the sound bank state (right)
    s_info = Theme::label(scr, "", &my_font_14, Theme::MUTED);
    lv_label_set_long_mode(s_info, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_info, 236);
    lv_obj_set_pos(s_info, 10, 89);
    s_fontLbl = Theme::label(scr, "", &my_font_14, Theme::MUTED);
    lv_obj_set_width(s_fontLbl, LIST_W - 252);
    lv_label_set_long_mode(s_fontLbl, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(s_fontLbl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_fontLbl, 250, 89);
    // Load the default font only when none is loaded; a song font from the last song stays until a
    // song wants another one (saves a few seconds of loading).
    const Karaoke::FontState fs = Karaoke::fontState();
    if (fs == Karaoke::FontState::NONE || fs == Karaoke::FontState::FAILED) Karaoke::startLoadFont();
    updateFontLabel();

    s_list = lv_obj_create(scr);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_size(s_list, LIST_W, H - LIST_Y);
    lv_obj_set_pos(s_list, 0, LIST_Y);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_ROW_WRAP);   // Two song columns in landscape
    lv_obj_set_style_pad_row(s_list, 6, 0);
    lv_obj_set_style_pad_column(s_list, 8, 0);
    lv_obj_set_style_pad_hor(s_list, 10, 0);
    lv_obj_set_style_pad_bottom(s_list, 10, 0);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

    // A-Ö jump column along the right edge
    s_jumpCol = lv_obj_create(scr);
    lv_obj_remove_style_all(s_jumpCol);
    lv_obj_set_size(s_jumpCol, JUMP_W, H - 50);
    lv_obj_set_pos(s_jumpCol, W - JUMP_W - 2, 50);
    lv_obj_set_scrollable(s_jumpCol, false);
    const int32_t kh = (H - 54) / JUMP_N;
    for (int i = 0; i < JUMP_N; i++) {
        lv_obj_t *k = lv_button_create(s_jumpCol);
        lv_obj_remove_style_all(k);
        lv_obj_set_size(k, JUMP_W, kh - 2);
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

// v0.13.6: countdown in long pauses. dots = total (0 hides the row), elapsed = lit dots (left to
// right, sung-syllable colour); the rest are white. Only redraws on change.
constexpr int32_t CD_DOT = 13, CD_GAP = 8, CD_Y = 163;
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
    lv_obj_align(P.cdRow, LV_ALIGN_TOP_MID, 0, CD_Y);
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
        msg(tr(Str::KARA_WAIT_FONT), true);
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
    const Karaoke::SongFile f = Karaoke::current();
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
    // v0.13.6 countdown dots: centred under the current line (bottom of its 3-line area)
    P.cdRow = lv_obj_create(scr);
    lv_obj_remove_style_all(P.cdRow);
    lv_obj_set_size(P.cdRow, CD_DOT, CD_DOT);
    lv_obj_align(P.cdRow, LV_ALIGN_TOP_MID, 0, CD_Y);
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
    // v0.13.7: always start in the folder view. The index makes this quick; without a valid index
    // the scan runs in the background (the list shows "scanning" and fills in when done).
    s_mode = ListMode::ROOT;
    s_dir.clear();
    s_query.clear();
    s_page = 0;
    s_lastRel.clear();
    s_extSeen = Karaoke::externalOpenGen();
    if (!Karaoke::loadIndex()) Karaoke::rescanAsync();
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
