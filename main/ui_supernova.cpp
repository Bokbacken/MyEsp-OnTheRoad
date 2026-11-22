#include "ui_supernova.h"

#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <string.h>
#include <string>
#include <vector>

#include "board_display.h"
#include "board_sd.h"
#include "hw_profile.h"
#include "lang.h"
#include "my_storage.h"
#include "my_textinput.h"
#include "my_theme.h"
#include "supernova_lyrics.h"
#include "supernova_library.h"
#include "supernova_player.h"
#include "ui_metrics.h"
#include "ui_yatzy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace UiSupernova {
namespace {

constexpr size_t MAX_LYRICS_BYTES = SupernovaLyrics::MAX_FILE_BYTES;

using Song = SupernovaLibrary::Media;

Song s_song;
std::string s_songFolder;   // Folder name of the song being played
SupernovaLyrics::Document s_lyrics;
lv_obj_t *s_playerScreen = nullptr;
lv_obj_t *s_playerHeader = nullptr;
lv_obj_t *s_pauseButton = nullptr;
// Lyrics are rendered off-screen (never-loaded screen) and blended into the video frames by the player.
lv_obj_t *s_ovlScreen = nullptr;
lv_obj_t *s_ovlBox = nullptr;
lv_obj_t *s_curBox = nullptr;
lv_obj_t *s_curLine = nullptr;
lv_obj_t *s_nextLine = nullptr;
lv_obj_t *s_curSung = nullptr;   // Gold copy of the current line holding only the sung syllables
lv_obj_t *s_sizeLabel = nullptr;
lv_obj_t *s_posLabel = nullptr;
int s_ovlW = 0;
bool s_ovlDirty = false;
uint8_t s_ovlSize = 1;   // 0 small, 1 medium, 2 large
uint8_t s_ovlPos = 2;    // 0 top, 1 middle, 2 bottom
constexpr const char *SETTINGS_NS = "supernova";
uint32_t s_lastNote = UINT32_MAX;
lv_obj_t *s_timeLabel = nullptr;
lv_obj_t *s_playLabel = nullptr;
lv_timer_t *s_startTimer = nullptr;
lv_timer_t *s_playerTimer = nullptr;
bool s_songReady = false;
uint32_t s_lastPhrase = UINT32_MAX;
bool s_playerPaused = false;
bool s_closeRequested = false;
bool s_playerVisible = false;
bool s_startTaskFailed = false;
bool s_stopTaskFailed = false;
enum class MediaState : uint8_t { IDLE, STARTING, PLAYING, STOPPING, FAILED };
std::atomic<MediaState> s_mediaState{MediaState::IDLE};

bool readLyrics(const char *path, SupernovaLyrics::Document &out)
{
    BoardSd::Guard guard;
    if (!BoardSd::mounted()) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool ok = false;
    if (fseek(f, 0, SEEK_END) == 0) {
        const long size = ftell(f);
        if (size > 0 && (size_t)size <= MAX_LYRICS_BYTES && fseek(f, 0, SEEK_SET) == 0) {
            std::vector<char> data((size_t)size);
            if (fread(data.data(), 1, data.size(), f) == data.size())
                ok = SupernovaLyrics::parse(data.data(), data.size(), out) == SupernovaLyrics::Err::OK;
        }
    }
    fclose(f);
    return ok;
}

void showError(const char *message)
{
    lv_obj_t *box = lv_msgbox_create(nullptr);
    lv_msgbox_add_text(box, message);
    lv_msgbox_add_close_button(box);
    Theme::msgboxFonts(box);
}

lv_obj_t *makeText(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    return label;
}

void loadOverlaySettings()
{
    MyNvs::Handle p(SETTINGS_NS, true);
    if (!p) return;
    s_ovlSize = std::min<uint8_t>(p.getU8("ovl_size", 1), 2);
    s_ovlPos = std::min<uint8_t>(p.getU8("ovl_pos", 2), 2);
}

void saveOverlaySettings()
{
    MyNvs::Handle p(SETTINGS_NS, false);
    if (!p) return;
    p.setU8("ovl_size", s_ovlSize);
    p.setU8("ovl_pos", s_ovlPos);
}

const char *sizeText()
{
    static const char *const names[] = {"S", "M", "L"};
    return names[s_ovlSize];
}

const char *posText()
{
    static const char *const symbols[] = {LV_SYMBOL_UP, LV_SYMBOL_MINUS, LV_SYMBOL_DOWN};
    return symbols[s_ovlPos];
}

// Largest font from the given step (36/24/18/14) downward that keeps the label within avail pixels.
void fitFont(lv_obj_t *label, int firstStep, int32_t avail)
{
    const lv_font_t *steps[] = {&my_font_36, &my_font_24, &my_font_18, &my_font_14};
    for (int i = firstStep; i < 4; i++) {
        lv_obj_set_style_text_font(label, steps[i], 0);
        lv_obj_update_layout(label);
        if (lv_obj_get_width(label) <= avail) break;
    }
}

void deleteOverlay()
{
    if (s_ovlScreen) lv_obj_delete(s_ovlScreen);
    s_ovlScreen = s_ovlBox = s_curBox = s_curLine = s_nextLine = s_curSung = nullptr;
    s_ovlDirty = false;
}

bool ensureOverlay()
{
    if (s_ovlScreen) return true;
    int rx, ry, rw, rh;
    if (!SupernovaPlayer::videoRect(rx, ry, rw, rh)) return false;
    s_ovlW = std::min(rw, 800);
    s_ovlScreen = lv_obj_create(nullptr);
    s_ovlBox = lv_obj_create(s_ovlScreen);
    lv_obj_remove_style_all(s_ovlBox);
    lv_obj_set_size(s_ovlBox, s_ovlW, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(s_ovlBox, lv_color_hex(0x303030), 0);
    lv_obj_set_style_bg_opa(s_ovlBox, 150, 0);
    lv_obj_set_style_radius(s_ovlBox, 10, 0);
    lv_obj_set_style_pad_all(s_ovlBox, 6, 0);
    lv_obj_set_style_pad_row(s_ovlBox, 2, 0);
    lv_obj_remove_flag(s_ovlBox, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_ovlBox, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_ovlBox, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_curBox = lv_obj_create(s_ovlBox);
    lv_obj_remove_style_all(s_curBox);
    lv_obj_remove_flag(s_curBox, LV_OBJ_FLAG_SCROLLABLE);
    s_curLine = makeText(s_curBox, " ", &my_font_24, lv_color_white());
    lv_label_set_long_mode(s_curLine, LV_LABEL_LONG_MODE_CLIP);
    s_curSung = makeText(s_curBox, "", &my_font_24, Theme::c(Theme::GOLD));
    lv_label_set_long_mode(s_curSung, LV_LABEL_LONG_MODE_CLIP);
    s_nextLine = makeText(s_ovlBox, " ", &my_font_18, lv_color_hex(0xC8C8C8));
    lv_label_set_long_mode(s_nextLine, LV_LABEL_LONG_MODE_CLIP);
    s_lastPhrase = UINT32_MAX;
    s_lastNote = UINT32_MAX;
    return true;
}

void layoutPhrase(const std::string &current, const std::string &next)
{
    const int32_t avail = s_ovlW - 24;
    lv_label_set_text(s_curLine, current.empty() ? " " : current.c_str());
    lv_label_set_text(s_curSung, "");
    fitFont(s_curLine, 2 - s_ovlSize, avail);
    lv_obj_set_style_text_font(s_curSung, lv_obj_get_style_text_font(s_curLine, LV_PART_MAIN), 0);
    lv_obj_update_layout(s_curLine);
    lv_obj_set_size(s_curBox, lv_obj_get_width(s_curLine), lv_obj_get_height(s_curLine));
    lv_obj_set_pos(s_curLine, 0, 0);
    lv_obj_set_pos(s_curSung, 0, 0);
    lv_label_set_text(s_nextLine, next.empty() ? " " : next.c_str());
    fitFont(s_nextLine, 3 - s_ovlSize, avail);
}

void renderOverlay()
{
#if CONFIG_OTR_HW_PROFILE_P4_43
    int rx, ry, rw, rh;
    if (!SupernovaPlayer::videoRect(rx, ry, rw, rh)) return;
    lv_obj_update_layout(s_ovlBox);
    const int w = lv_obj_get_width(s_ovlBox), h = lv_obj_get_height(s_ovlBox);
    uint32_t capacity = 0;
    uint8_t *buffer = SupernovaPlayer::overlayBack(capacity);
    if (!buffer || w <= 0 || h <= 0 || w > 800 || h > 160) return;
    lv_draw_buf_t draw;
    if (lv_draw_buf_init(&draw, w, h, LV_COLOR_FORMAT_ARGB8888, 0, buffer, capacity) != LV_RESULT_OK ||
        lv_snapshot_take_to_draw_buf(s_ovlBox, LV_COLOR_FORMAT_ARGB8888, &draw) != LV_RESULT_OK)
        return;
    int dy = s_ovlPos == 0 ? 8 : s_ovlPos == 1 ? (rh - h) / 2 : rh - h - 8;
    if (dy < 0) dy = 0;
    SupernovaPlayer::overlayPublish(draw.header.w, draw.header.h, draw.header.stride, (rw - w) / 2, dy);
#endif
}

void updateLyrics(uint32_t pos)
{
    if (!s_playerScreen || s_lyrics.phrases.empty() || !ensureOverlay()) return;
    const uint32_t phraseIndex = SupernovaLyrics::phraseAt(s_lyrics, pos);
    if (phraseIndex != s_lastPhrase) {
        const std::string next = phraseIndex + 1 < s_lyrics.phrases.size()
                                     ? SupernovaLyrics::toDisplayText(s_lyrics.phrases[phraseIndex + 1].text) : "";
        layoutPhrase(SupernovaLyrics::toDisplayText(s_lyrics.phrases[phraseIndex].text), next);
        s_lastPhrase = phraseIndex;
        s_lastNote = UINT32_MAX;
        s_ovlDirty = true;
    }
    // Syllables light up when they start and stay lit until the phrase changes.
    const SupernovaLyrics::Phrase &phrase = s_lyrics.phrases[phraseIndex];
    uint32_t sungCount = 0;
    while (sungCount < phrase.notes.size() && phrase.notes[sungCount].startMs <= pos) sungCount++;
    if (sungCount != s_lastNote) {
        s_lastNote = sungCount;
        std::string sung;
        for (uint32_t i = 0; i < sungCount; i++) sung += phrase.notes[i].text;
        lv_label_set_text(s_curSung, SupernovaLyrics::toDisplayText(sung).c_str());
        s_ovlDirty = true;
    }
    if (s_ovlDirty) {
        renderOverlay();
        s_ovlDirty = false;
    }
}

void closePlayer();

void finishBackToSongs()
{
    closePlayer();
    Display::setLandscape(false);
    show();
}

void closePlayer()
{
    if (s_startTimer) {
        lv_timer_delete(s_startTimer);
        s_startTimer = nullptr;
    }
    if (s_playerTimer) {
        lv_timer_delete(s_playerTimer);
        s_playerTimer = nullptr;
    }
    s_playerScreen = nullptr;
    deleteOverlay();
    s_playerHeader = s_pauseButton = nullptr;
    s_sizeLabel = s_posLabel = s_timeLabel = s_playLabel = nullptr;
    s_lastPhrase = UINT32_MAX;
    s_lastNote = UINT32_MAX;
    s_playerPaused = false;
    s_closeRequested = false;
    s_playerVisible = false;
    s_startTaskFailed = false;
    s_stopTaskFailed = false;
    s_mediaState.store(MediaState::IDLE, std::memory_order_release);
}

void backToSongs()
{
    if (!s_playerScreen) return;
    s_closeRequested = true;
    if (s_mediaState.load(std::memory_order_acquire) == MediaState::IDLE) {
        if (s_startTimer) {
            lv_timer_delete(s_startTimer);
            s_startTimer = nullptr;
        }
        finishBackToSongs();
    }
}

void mediaStartTask(void *)
{
    const Song &song = s_song;
    const char *audioPath = song.audio.empty() ? nullptr : song.audio.c_str();
    const bool ok = SupernovaPlayer::start(audioPath, song.video.c_str());
    s_mediaState.store(ok ? MediaState::PLAYING : MediaState::FAILED, std::memory_order_release);
    vTaskDelete(nullptr);
}

void mediaStopTask(void *)
{
    SupernovaPlayer::stop();
    s_mediaState.store(MediaState::IDLE, std::memory_order_release);
    vTaskDelete(nullptr);
}

void startPendingPlayer(lv_timer_t *timer)
{
    lv_timer_delete(timer);
    s_startTimer = nullptr;
    if (!s_playerScreen || !s_songReady) return;
    s_mediaState.store(MediaState::STARTING, std::memory_order_release);
    if (xTaskCreate(mediaStartTask, "supernova_start", 8192, nullptr, 5, nullptr) != pdPASS) {
        s_startTaskFailed = true;
        s_mediaState.store(MediaState::FAILED, std::memory_order_release);
    }
}

void togglePause()
{
    if (s_playerPaused) {
        if (SupernovaPlayer::resume()) {
            s_playerPaused = false;
            lv_label_set_text(s_playLabel, LV_SYMBOL_PAUSE);
        } else {
            showError(SupernovaPlayer::error());
        }
    } else if (SupernovaPlayer::pause()) {
        s_playerPaused = true;
        lv_label_set_text(s_playLabel, LV_SYMBOL_PLAY);
    } else {
        showError(SupernovaPlayer::error());
    }
}

void playerTick(lv_timer_t *)
{
    if (!s_playerScreen) return;
    MediaState state = s_mediaState.load(std::memory_order_acquire);
    if (s_closeRequested && state == MediaState::PLAYING) {
        s_mediaState.store(MediaState::STOPPING, std::memory_order_release);
        if (xTaskCreate(mediaStopTask, "supernova_stop", 4096, nullptr, 5, nullptr) != pdPASS) {
            if (!s_stopTaskFailed) showError(tr(Str::SUPERNOVA_STOP_FAILED));
            s_stopTaskFailed = true;
            s_mediaState.store(MediaState::PLAYING, std::memory_order_release);
        } else {
            state = MediaState::STOPPING;
        }
    }
    if (state == MediaState::FAILED) {
        s_mediaState.store(MediaState::IDLE, std::memory_order_release);
        if (s_closeRequested) {
            finishBackToSongs();
        } else {
            showError(s_startTaskFailed ? tr(Str::SUPERNOVA_START_FAILED) : SupernovaPlayer::error());
        }
        return;
    }
    if (state == MediaState::IDLE && s_closeRequested) {
        finishBackToSongs();
        return;
    }
    if (state != MediaState::PLAYING) return;
    if (!s_playerVisible) {
        lv_obj_move_foreground(s_playerHeader);
        lv_obj_clear_state(s_pauseButton, LV_STATE_DISABLED);
        s_playerVisible = true;
    }
    const uint32_t pos = SupernovaPlayer::positionMs();
    static uint32_t shownSeconds = UINT32_MAX;
    if (pos / 1000 != shownSeconds) {
        shownSeconds = pos / 1000;
        lv_label_set_text_fmt(s_timeLabel, "%02u:%02u", (unsigned)(pos / 60000), (unsigned)(shownSeconds % 60));
    }
    updateLyrics(pos);
}

void playSong(const std::string &folderName, const std::string &title)
{
    s_songReady = false;
    s_song = Song();
    bool complete;
    {
        BoardSd::Guard guard;
        complete = SupernovaLibrary::findMedia(SupernovaLibrary::folderPath(folderName.c_str()), s_song);
    }
    if (!complete) {
        showError(tr(Str::SUPERNOVA_CATALOG_ERROR));
        return;
    }
    s_songReady = true;
    s_songFolder = folderName;
    if (!readLyrics(s_song.lyrics.c_str(), s_lyrics)) {
        showError(tr(Str::SUPERNOVA_BAD_LYRICS));
        return;
    }
    if (!Display::setLandscape(true)) {
        showError(tr(Str::SUPERNOVA_ROTATION_FAILED));
        return;
    }
    // Only the thin header is opaque; lyrics are blended into the video frame itself.
    const int headerH = UiMetrics::buttonH() + 8;
    loadOverlaySettings();
    SupernovaPlayer::setVideoArea(0, headerH, UiMetrics::screenW(), UiMetrics::screenH() - headerH);
    lv_obj_t *screen = Theme::screen();
    s_playerScreen = screen;
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *top = lv_obj_create(screen);
    s_playerHeader = top;
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, UiMetrics::screenW(), headerH);
    lv_obj_set_style_bg_color(top, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_t *back = lv_button_create(top);
    lv_obj_set_size(back, UiMetrics::buttonWMin(), UiMetrics::buttonH());
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *backLabel = lv_label_create(back);
    lv_label_set_text(backLabel, tr(Str::BTN_BACK));
    lv_obj_center(backLabel);
    lv_obj_add_event_cb(back, [](lv_event_t *) { backToSongs(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *name = makeText(top, title.c_str(), &my_font_18, lv_color_white());
    lv_obj_set_width(name, UiMetrics::screenW() / 3);
    lv_obj_align(name, LV_ALIGN_CENTER, -UiMetrics::buttonWMin(), 0);
    s_timeLabel = makeText(top, "00:00", &my_font_14, lv_color_white());
    lv_obj_align(s_timeLabel, LV_ALIGN_RIGHT_MID, -8, 0);

    lv_obj_t *pause = lv_button_create(top);
    s_pauseButton = pause;
    lv_obj_add_state(pause, LV_STATE_DISABLED);
    lv_obj_set_size(pause, UiMetrics::buttonWMin(), UiMetrics::buttonH());
    lv_obj_align(pause, LV_ALIGN_RIGHT_MID, -64, 0);
    s_playLabel = lv_label_create(pause);
    lv_label_set_text(s_playLabel, LV_SYMBOL_PAUSE);
    lv_obj_center(s_playLabel);
    lv_obj_add_event_cb(pause, [](lv_event_t *) { togglePause(); }, LV_EVENT_CLICKED, nullptr);
    auto makeSetting = [&](lv_obj_t *&label, const char *text, int x, lv_event_cb_t cb) {
        lv_obj_t *button = lv_button_create(top);
        lv_obj_set_size(button, UiMetrics::buttonWMin(), UiMetrics::buttonH());
        lv_obj_align(button, LV_ALIGN_RIGHT_MID, x, 0);
        label = lv_label_create(button);
        lv_label_set_text(label, text);
        lv_obj_center(label);
        lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, nullptr);
    };
    const int step = UiMetrics::buttonWMin() + 6;
    makeSetting(s_posLabel, posText(), -64 - step, [](lv_event_t *) {
        s_ovlPos = (s_ovlPos + 1) % 3;
        lv_label_set_text(s_posLabel, posText());
        saveOverlaySettings();
        s_ovlDirty = true;
    });
    makeSetting(s_sizeLabel, sizeText(), -64 - 2 * step, [](lv_event_t *) {
        s_ovlSize = (s_ovlSize + 1) % 3;
        lv_label_set_text(s_sizeLabel, sizeText());
        saveOverlaySettings();
        s_lastPhrase = UINT32_MAX;
    });
    Theme::loadScreen(screen);
    s_mediaState.store(MediaState::IDLE, std::memory_order_release);
    s_closeRequested = false;
    s_playerVisible = false;
    s_startTaskFailed = false;
    s_stopTaskFailed = false;
    s_playerTimer = lv_timer_create(playerTick, 33, nullptr);
    s_startTimer = lv_timer_create(startPendingPlayer, 230, nullptr);
    lv_timer_set_repeat_count(s_startTimer, 1);
}

void home()
{
    closePlayer();
    Display::setLandscape(false);
    UiYatzy::showMenu();
}

#if CONFIG_OTR_HW_PROFILE_P4_43
// ---- Song browser: search, sort, filter, A-Ö jump and pages over the library ---------------------
constexpr size_t PAGE = 20;
constexpr uint32_t DEBOUNCE_MS = 400;
struct JumpKey {
    const char *text;
    uint16_t rank;
};
const JumpKey JUMP[] = {{"#", 0},  {"A", 30}, {"D", 33}, {"G", 36}, {"J", 39}, {"M", 42},
                        {"P", 45}, {"S", 48}, {"V", 51}, {"\xc3\x85", 56}, {"\xc3\x84", 57}, {"\xc3\x96", 58}};
constexpr int JUMP_N = (int)(sizeof(JUMP) / sizeof(JUMP[0]));
constexpr size_t MAX_FILTER_VALUES = 40;

// Kept while the player is open so the list is where the user left it
SupernovaIndex::Query s_query;
size_t s_page = 0;
std::vector<uint16_t> s_view;
uint32_t s_shownGen = UINT32_MAX;
bool s_shownScanning = false;
size_t s_shownDone = SIZE_MAX;
lv_obj_t *s_list = nullptr, *s_info = nullptr, *s_pageLabel = nullptr, *s_searchBtn = nullptr;
lv_obj_t *s_sortLabel = nullptr, *s_filterLabel = nullptr, *s_jumpCol = nullptr, *s_input = nullptr, *s_sheet = nullptr;
lv_timer_t *s_browseTimer = nullptr;

size_t pageCount() { return s_view.empty() ? 1 : (s_view.size() + PAGE - 1) / PAGE; }

const char *sortText()
{
    switch (s_query.sort) {
    case SupernovaIndex::Sort::TITLE: return tr(Str::SUPERNOVA_SORT_TITLE);
    case SupernovaIndex::Sort::YEAR: return tr(Str::SUPERNOVA_SORT_YEAR);
    default: return tr(Str::SUPERNOVA_SORT_ARTIST);
    }
}

std::string filterText()
{
    if (s_query.kind == SupernovaIndex::FilterKind::NONE) return tr(Str::SUPERNOVA_FILTER);
    return s_query.kind == SupernovaIndex::FilterKind::DECADE ? s_query.value + "s" : s_query.value;
}

void rebuildView()
{
    SupernovaLibrary::withLibrary([](const SupernovaIndex::Library &lib) {
        s_view = lib.view(s_query);
        s_shownGen = SupernovaLibrary::generation();
    });
    if (s_page >= pageCount()) s_page = pageCount() - 1;
}

void updateInfo()
{
    if (!s_info) return;
    size_t done = 0, total = 0;
    SupernovaLibrary::progress(done, total);
    s_shownScanning = SupernovaLibrary::scanning();
    s_shownDone = done;
    if (s_shownScanning) lv_label_set_text_fmt(s_info, tr(Str::SUPERNOVA_SCANNING), (unsigned)done, (unsigned)total);
    else lv_label_set_text_fmt(s_info, tr(Str::KARA_N_SONGS), (unsigned)s_view.size());
    lv_label_set_text_fmt(s_pageLabel, tr(Str::KARA_PAGE), (unsigned)(s_page + 1), (unsigned)pageCount());
    lv_label_set_text(s_sortLabel, sortText());
    lv_label_set_text(s_filterLabel, filterText().c_str());
    lv_obj_t *searchLabel = lv_obj_get_child(s_searchBtn, 0);
    lv_label_set_text(searchLabel, s_query.text.empty() ? tr(Str::SUPERNOVA_SEARCH_TITLE) : s_query.text.c_str());
    const bool jump = s_query.sort != SupernovaIndex::Sort::YEAR && s_view.size() > PAGE;
    if (jump) lv_obj_clear_flag(s_jumpCol, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_jumpCol, LV_OBJ_FLAG_HIDDEN);
}

void playFromView(size_t pos)
{
    std::string folder, title;
    SupernovaLibrary::withLibrary([&](const SupernovaIndex::Library &lib) {
        if (SupernovaLibrary::generation() != s_shownGen || pos >= s_view.size()) return;
        folder = lib.folder(s_view[pos]);
        title = lib.title(s_view[pos]);
    });
    if (!folder.empty()) playSong(folder, title);
}

void renderList()
{
    lv_obj_clean(s_list);
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);
    if (s_view.empty()) {
        const bool filtered = !s_query.text.empty() || s_query.kind != SupernovaIndex::FilterKind::NONE;
        if (!(SupernovaLibrary::scanning() && !filtered)) {
            lv_obj_t *empty = Theme::label(s_list, filtered ? tr(Str::KARA_NO_MATCH) : tr(Str::SUPERNOVA_EMPTY),
                                           &my_font_18, Theme::MUTED);
            lv_obj_set_width(empty, LV_PCT(100));
        }
    } else {
        const size_t first = s_page * PAGE, last = std::min(first + PAGE, s_view.size());
        SupernovaLibrary::withLibrary([&](const SupernovaIndex::Library &lib) {
            for (size_t i = first; i < last; i++) {
                const uint16_t idx = s_view[i];
                std::string desc = lib.artist(idx);
                if (lib.year(idx)) desc += (desc.empty() ? "" : "  ") + std::to_string(lib.year(idx));
                lv_obj_t *button = Theme::menuCard(s_list, LV_PCT(100), lib.title(idx), desc.c_str(), nullptr,
                                                   LV_SYMBOL_AUDIO, Theme::BLUE);
                lv_obj_add_event_cb(button, [](lv_event_t *event) {
                    static uint32_t last = 0;
                    if (!Theme::debounce(last, DEBOUNCE_MS)) return;
                    playFromView((size_t)(uintptr_t)lv_event_get_user_data(event));
                }, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
            }
        });
    }
    updateInfo();
}

void requery()
{
    s_page = 0;
    rebuildView();
    renderList();
}

void gotoPage(int delta)
{
    const int next = (int)s_page + delta;
    if (next < 0 || next >= (int)pageCount()) return;
    s_page = (size_t)next;
    renderList();
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
    s_query.text = q;
    requery();
}

void closeSheet()
{
    if (s_sheet) lv_obj_delete(s_sheet);
    s_sheet = nullptr;
}

void applyFilter(SupernovaIndex::FilterKind kind, const std::string &value)
{
    s_query.kind = kind;
    s_query.value = value;
    closeSheet();
    requery();
}

struct FilterPick {
    SupernovaIndex::FilterKind kind;
    std::string value;
};

void openSheet()
{
    if (s_sheet) return;
    s_sheet = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_sheet);
    lv_obj_set_size(s_sheet, UiMetrics::screenW(), UiMetrics::screenH() - UiMetrics::headerH());
    lv_obj_set_pos(s_sheet, 0, UiMetrics::headerH());
    lv_obj_set_style_bg_color(s_sheet, Theme::c(Theme::BG), 0);
    lv_obj_set_style_bg_opa(s_sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(s_sheet, UiMetrics::marginNormal(), 0);
    lv_obj_set_flex_flow(s_sheet, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_sheet, 6, 0);
    lv_obj_set_scroll_dir(s_sheet, LV_DIR_VER);
    const int32_t w = UiMetrics::screenW() - 2 * UiMetrics::marginNormal();
    const int32_t h = UiMetrics::buttonH();
    auto addButton = [&](const std::string &text, uint32_t color, SupernovaIndex::FilterKind kind, const std::string &value) {
        lv_obj_t *b = Theme::button(s_sheet, text.c_str(), w, h, color);
        auto *pick = new FilterPick{kind, value};
        lv_obj_add_event_cb(b, [](lv_event_t *e) {
            static uint32_t last = 0;
            if (!Theme::debounce(last, DEBOUNCE_MS)) return;
            const FilterPick p = *(FilterPick *)lv_event_get_user_data(e);
            lv_async_call([](void *arg) {
                FilterPick *q = (FilterPick *)arg;
                applyFilter(q->kind, q->value);
                delete q;
            }, new FilterPick(p));
        }, LV_EVENT_CLICKED, pick);
        lv_obj_add_event_cb(b, [](lv_event_t *e) { delete (FilterPick *)lv_event_get_user_data(e); }, LV_EVENT_DELETE, pick);
    };
    addButton(tr(Str::SUPERNOVA_FILTER_ALL), Theme::GOLD, SupernovaIndex::FilterKind::NONE, "");
    struct Section {
        SupernovaIndex::FilterKind kind;
        Str title;
    };
    const Section sections[] = {{SupernovaIndex::FilterKind::GENRE, Str::SUPERNOVA_FILTER_GENRE},
                                {SupernovaIndex::FilterKind::LANGUAGE, Str::SUPERNOVA_FILTER_LANGUAGE},
                                {SupernovaIndex::FilterKind::DECADE, Str::SUPERNOVA_FILTER_DECADE}};
    SupernovaLibrary::withLibrary([&](const SupernovaIndex::Library &lib) {
        for (const Section &s : sections) {
            const auto values = lib.values(s.kind);
            if (values.empty()) continue;
            lv_obj_t *title = Theme::label(s_sheet, tr(s.title), &my_font_18, Theme::MUTED);
            lv_obj_set_width(title, w);
            size_t n = 0;
            for (const auto &v : values) {
                if (++n > MAX_FILTER_VALUES) break;
                addButton(v.name + " (" + std::to_string(v.count) + ")", Theme::BLUE, s.kind, v.key);
            }
        }
    });
}

void browserDeleted(lv_event_t *)
{
    if (s_browseTimer) lv_timer_delete(s_browseTimer);
    s_browseTimer = nullptr;
    s_list = s_info = s_pageLabel = s_searchBtn = s_sortLabel = s_filterLabel = s_jumpCol = s_input = s_sheet = nullptr;
}

void browseTick(lv_timer_t *)
{
    if (s_input) return;
    const bool scanningNow = SupernovaLibrary::scanning();
    if (SupernovaLibrary::generation() != s_shownGen && !s_sheet) {
        rebuildView();
        renderList();
        return;
    }
    size_t done = 0, total = 0;
    SupernovaLibrary::progress(done, total);
    if (scanningNow != s_shownScanning || (scanningNow && done != s_shownDone)) updateInfo();
}

void buildBrowser()
{
    lv_obj_t *screen = Theme::screen();
    lv_obj_t *header = Theme::header(screen, tr(Str::MENU_SUPERNOVA));
    lv_obj_t *back = Theme::button(header, LV_SYMBOL_LEFT, 44, 36, Theme::MUTED);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -Theme::batteryStatusWidth() - 8, 0);
    lv_obj_add_event_cb(back, [](lv_event_t *) { home(); }, LV_EVENT_CLICKED, nullptr);

    const int32_t margin = UiMetrics::marginNormal();
    const int32_t gap = 6;
    const int32_t w = UiMetrics::screenW();
    const int32_t barH = UiMetrics::buttonH();
    const int32_t y0 = UiMetrics::headerH() + gap;
    const int32_t btnW = (w - 2 * margin) / 4;
    const int32_t searchW = w - 2 * margin - 2 * btnW - 2 * gap;

    s_searchBtn = Theme::button(screen, tr(Str::SUPERNOVA_SEARCH_TITLE), searchW, barH, Theme::BLUE);
    lv_obj_set_pos(s_searchBtn, margin, y0);
    lv_obj_t *searchLabel = lv_obj_get_child(s_searchBtn, 0);
    lv_label_set_long_mode(searchLabel, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(searchLabel, searchW - 16);
    lv_obj_add_event_cb(s_searchBtn, [](lv_event_t *) {
        static uint32_t last = 0;
        if (!Theme::debounce(last, DEBOUNCE_MS) || s_input) return;
        s_input = TextInput::open(lv_screen_active(), 0, 0, UiMetrics::screenW(), UiMetrics::screenH(),
                                  tr(Str::SUPERNOVA_SEARCH_TITLE), s_query.text.c_str(), 40, false, searchDone, nullptr);
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *sort = Theme::button(screen, sortText(), btnW, barH, Theme::GOLD);
    lv_obj_set_pos(sort, margin + searchW + gap, y0);
    s_sortLabel = lv_obj_get_child(sort, 0);
    lv_obj_add_event_cb(sort, [](lv_event_t *) {
        static uint32_t last = 0;
        if (!Theme::debounce(last, DEBOUNCE_MS)) return;
        using S = SupernovaIndex::Sort;
        s_query.sort = s_query.sort == S::ARTIST ? S::TITLE : s_query.sort == S::TITLE ? S::YEAR : S::ARTIST;
        requery();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *filter = Theme::button(screen, filterText().c_str(), btnW, barH, Theme::BLUE);
    lv_obj_set_pos(filter, margin + searchW + btnW + 2 * gap, y0);
    s_filterLabel = lv_obj_get_child(filter, 0);
    lv_label_set_long_mode(s_filterLabel, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_filterLabel, btnW - 12);
    lv_obj_add_event_cb(filter, [](lv_event_t *) {
        static uint32_t last = 0;
        if (Theme::debounce(last, DEBOUNCE_MS)) openSheet();
    }, LV_EVENT_CLICKED, nullptr);

    const int32_t infoY = y0 + barH + gap;
    s_info = Theme::label(screen, "", &my_font_14, Theme::MUTED);
    lv_obj_set_pos(s_info, margin, infoY);
    lv_obj_set_width(s_info, w - 2 * margin);
    lv_label_set_long_mode(s_info, LV_LABEL_LONG_MODE_DOTS);

    const int32_t listY = infoY + 24;
    const int32_t footerY = UiMetrics::screenH() - barH - gap;
    const int32_t jumpW = 36;
    s_list = lv_obj_create(screen);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_pos(s_list, margin, listY);
    lv_obj_set_size(s_list, w - 2 * margin - jumpW - gap, footerY - listY - gap);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_list, UiMetrics::cardSpacing(), 0);
    lv_obj_set_scrollable(s_list, true);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);

    s_jumpCol = lv_obj_create(screen);
    lv_obj_remove_style_all(s_jumpCol);
    lv_obj_set_size(s_jumpCol, jumpW, footerY - listY - gap);
    lv_obj_set_pos(s_jumpCol, w - margin - jumpW, listY);
    const int32_t kh = (footerY - listY - gap) / JUMP_N;
    for (int i = 0; i < JUMP_N; i++) {
        lv_obj_t *k = lv_button_create(s_jumpCol);
        lv_obj_remove_style_all(k);
        lv_obj_set_size(k, jumpW, kh - 2);
        lv_obj_set_pos(k, 0, i * kh);
        lv_obj_set_style_radius(k, 6, 0);
        lv_obj_set_style_bg_color(k, Theme::c(Theme::PANEL), 0);
        lv_obj_set_style_bg_opa(k, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(k, Theme::c(Theme::ACCENT), LV_STATE_PRESSED);
        lv_obj_center(Theme::label(k, JUMP[i].text, &my_font_14, Theme::TEXT));
        lv_obj_add_event_cb(k, [](lv_event_t *e) {
            static uint32_t last = 0;
            if (!Theme::debounce(last, 250)) return;
            const uint16_t rank = JUMP[(int)(intptr_t)lv_event_get_user_data(e)].rank;
            size_t pos = 0;
            SupernovaLibrary::withLibrary([&](const SupernovaIndex::Library &lib) {
                if (SupernovaLibrary::generation() == s_shownGen)
                    pos = SupernovaIndex::jumpIndex(lib, s_view, s_query.sort, rank);
            });
            if (pos >= s_view.size()) pos = s_view.empty() ? 0 : s_view.size() - 1;
            s_page = pos / PAGE;
            renderList();
        }, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    const int32_t navW = btnW;
    lv_obj_t *prev = Theme::button(screen, LV_SYMBOL_LEFT, navW, barH, Theme::BLUE);
    lv_obj_set_pos(prev, margin, footerY);
    lv_obj_add_event_cb(prev, [](lv_event_t *) {
        static uint32_t last = 0;
        if (Theme::debounce(last, 250)) gotoPage(-1);
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *next = Theme::button(screen, LV_SYMBOL_RIGHT, navW, barH, Theme::BLUE);
    lv_obj_set_pos(next, margin + navW + gap, footerY);
    lv_obj_add_event_cb(next, [](lv_event_t *) {
        static uint32_t last = 0;
        if (Theme::debounce(last, 250)) gotoPage(1);
    }, LV_EVENT_CLICKED, nullptr);
    s_pageLabel = Theme::label(screen, "", &my_font_14, Theme::TEXT);
    lv_obj_set_pos(s_pageLabel, margin + 2 * (navW + gap) + 4, footerY + (barH - 18) / 2);
    lv_obj_t *rescan = Theme::button(screen, tr(Str::SUPERNOVA_RESCAN), navW + 20, barH, Theme::GOLD);
    lv_obj_set_pos(rescan, w - margin - navW - 20, footerY);
    lv_obj_add_event_cb(rescan, [](lv_event_t *) {
        static uint32_t last = 0;
        if (!Theme::debounce(last, DEBOUNCE_MS)) return;
        SupernovaLibrary::start(true);
        updateInfo();
    }, LV_EVENT_CLICKED, nullptr);

    rebuildView();
    renderList();
    s_browseTimer = lv_timer_create(browseTick, 300, nullptr);
    lv_obj_add_event_cb(screen, browserDeleted, LV_EVENT_DELETE, nullptr);
    Theme::loadScreen(screen);
}
#endif

}  // namespace

void show()
{
#if !CONFIG_OTR_HW_PROFILE_P4_43
    showError(tr(Str::SUPERNOVA_P4_ONLY));
    return;
#else
    if (!BoardSd::mounted()) {
        showError(tr(Str::SUPERNOVA_NO_SD));
        return;
    }
    SupernovaLibrary::start(false);
    buildBrowser();
#endif
}

}  // namespace UiSupernova
