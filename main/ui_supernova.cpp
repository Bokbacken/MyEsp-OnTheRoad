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
#include "my_theme.h"
#include "supernova_lyrics.h"
#include "supernova_player.h"
#include "ui_metrics.h"
#include "ui_yatzy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace UiSupernova {
namespace {

constexpr const char *SONG_ROOT = "/sdcard/otr/supernova";
constexpr size_t MAX_LYRICS_BYTES = SupernovaLyrics::MAX_FILE_BYTES;

struct Song {
    std::string folder;
    std::string audio;
    std::string video;
    std::string lyrics;
};

std::vector<Song> s_songs;
SupernovaLyrics::Document s_lyrics;
lv_obj_t *s_playerScreen = nullptr;
lv_obj_t *s_playerHeader = nullptr;
lv_obj_t *s_lyricsPanel = nullptr;
lv_obj_t *s_pauseButton = nullptr;
lv_obj_t *s_prevLine = nullptr;
lv_obj_t *s_curLine = nullptr;
lv_obj_t *s_nextLine = nullptr;
lv_obj_t *s_curSyllable = nullptr;
lv_obj_t *s_timeLabel = nullptr;
lv_obj_t *s_playLabel = nullptr;
lv_timer_t *s_startTimer = nullptr;
lv_timer_t *s_playerTimer = nullptr;
size_t s_pendingSong = 0;
uint32_t s_lastPos = UINT32_MAX;
uint32_t s_lastPhrase = UINT32_MAX;
bool s_playerPaused = false;
bool s_closeRequested = false;
bool s_playerVisible = false;
bool s_startTaskFailed = false;
bool s_stopTaskFailed = false;
enum class MediaState : uint8_t { IDLE, STARTING, PLAYING, STOPPING, FAILED };
std::atomic<MediaState> s_mediaState{MediaState::IDLE};

bool endsWithExt(const std::string &name, const char *ext)
{
    const size_t extLen = strlen(ext);
    if (name.size() <= extLen) return false;
    const size_t start = name.size() - extLen;
    for (size_t i = 0; i < extLen; i++) {
        char a = name[start + i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return false;
    }
    return true;
}

bool isDirectory(const std::string &path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool findMedia(const std::string &folder, Song &song)
{
    DIR *dir = opendir(folder.c_str());
    if (!dir) return false;
    std::vector<std::string> files;
    while (dirent *entry = readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        const std::string path = folder + "/" + entry->d_name;
        struct stat st {};
        if (stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode))
            files.emplace_back(entry->d_name);
    }
    closedir(dir);
    std::sort(files.begin(), files.end());
    std::string avi;
    for (const std::string &file : files) {
        const std::string path = folder + "/" + file;
        if (song.audio.empty() && endsWithExt(file, ".mp3")) song.audio = path;
        else if (song.video.empty() && endsWithExt(file, ".mp4")) song.video = path;
        else if (avi.empty() && endsWithExt(file, ".avi")) avi = path;
        else if (song.lyrics.empty() && endsWithExt(file, ".txt")) song.lyrics = path;
    }
    if (!avi.empty()) song.video = avi;
    return !song.video.empty() && !song.lyrics.empty();
}

std::vector<Song> scanSongs(bool &sdReady, bool &catalogReady)
{
    std::vector<Song> songs;
    catalogReady = false;
    BoardSd::Guard guard;
    sdReady = BoardSd::mounted();
    if (!sdReady) return songs;
    if (!BoardSd::ensureDir(SONG_ROOT)) return songs;
    DIR *dir = opendir(SONG_ROOT);
    if (!dir) return songs;
    catalogReady = true;
    while (dirent *entry = readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        const std::string name(entry->d_name);
        const std::string path = std::string(SONG_ROOT) + "/" + name;
        if (!isDirectory(path)) continue;
        Song song;
        song.folder = name;
        if (findMedia(path, song)) songs.push_back(std::move(song));
    }
    closedir(dir);
    std::sort(songs.begin(), songs.end(), [](const Song &a, const Song &b) { return a.folder < b.folder; });
    return songs;
}

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

void updateLyrics(uint32_t pos)
{
    if (!s_playerScreen || s_lyrics.phrases.empty()) return;
    const uint32_t phraseIndex = SupernovaLyrics::phraseAt(s_lyrics, pos);
    if (phraseIndex != s_lastPhrase) {
        const std::string previous = phraseIndex
                                         ? SupernovaLyrics::toDisplayText(s_lyrics.phrases[phraseIndex - 1].text)
                                         : "";
        const std::string current = SupernovaLyrics::toDisplayText(s_lyrics.phrases[phraseIndex].text);
        const std::string next = phraseIndex + 1 < s_lyrics.phrases.size()
                                     ? SupernovaLyrics::toDisplayText(s_lyrics.phrases[phraseIndex + 1].text) : "";
        lv_label_set_text(s_prevLine, previous.c_str());
        lv_label_set_text(s_curLine, current.c_str());
        lv_label_set_text(s_nextLine, next.c_str());
        s_lastPhrase = phraseIndex;
        s_lastPos = UINT32_MAX;
    }
    if (pos == s_lastPos) return;
    s_lastPos = pos;
    const SupernovaLyrics::Phrase &phrase = s_lyrics.phrases[phraseIndex];
    const uint32_t noteIndex = SupernovaLyrics::noteAt(phrase, pos);
    if (noteIndex < phrase.notes.size()) {
        const std::string syllable = SupernovaLyrics::toDisplayText(phrase.notes[noteIndex].text);
        lv_label_set_text(s_curSyllable, syllable.c_str());
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
    s_playerHeader = s_lyricsPanel = s_pauseButton = nullptr;
    s_prevLine = s_curLine = s_nextLine = s_curSyllable = s_timeLabel = s_playLabel = nullptr;
    s_lastPhrase = UINT32_MAX;
    s_lastPos = UINT32_MAX;
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
    const Song &song = s_songs[s_pendingSong];
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
    if (!s_playerScreen || s_pendingSong >= s_songs.size()) return;
    s_mediaState.store(MediaState::STARTING, std::memory_order_release);
    if (xTaskCreate(mediaStartTask, "supernova_start", 8192, nullptr, 5, nullptr) != pdPASS) {
        s_startTaskFailed = true;
        s_mediaState.store(MediaState::FAILED, std::memory_order_release);
    }
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
        lv_obj_move_foreground(s_lyricsPanel);
        lv_obj_clear_state(s_pauseButton, LV_STATE_DISABLED);
        s_playerVisible = true;
    }
    const uint32_t pos = SupernovaPlayer::positionMs();
    lv_label_set_text_fmt(s_timeLabel, "%02u:%02u",
                          (unsigned)(pos / 60000), (unsigned)((pos / 1000) % 60));
    updateLyrics(pos);
}

void playSong(size_t index)
{
    if (index >= s_songs.size()) return;
    s_pendingSong = index;
    if (!readLyrics(s_songs[index].lyrics.c_str(), s_lyrics)) {
        showError(tr(Str::SUPERNOVA_BAD_LYRICS));
        return;
    }
    if (!Display::setLandscape(true)) {
        showError(tr(Str::SUPERNOVA_ROTATION_FAILED));
        return;
    }
    constexpr int LYRICS_H = 116;
    SupernovaPlayer::setVideoArea(0, UiMetrics::headerH(), UiMetrics::screenW(),
                                  UiMetrics::screenH() - UiMetrics::headerH() - LYRICS_H);
    lv_obj_t *screen = Theme::screen();
    s_playerScreen = screen;
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *top = lv_obj_create(screen);
    s_playerHeader = top;
    lv_obj_remove_style_all(top);
    lv_obj_set_size(top, UiMetrics::screenW(), UiMetrics::headerH());
    lv_obj_set_style_bg_color(top, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_t *back = lv_button_create(top);
    lv_obj_set_size(back, UiMetrics::buttonWMin(), UiMetrics::buttonH());
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *backLabel = lv_label_create(back);
    lv_label_set_text(backLabel, tr(Str::BTN_BACK));
    lv_obj_center(backLabel);
    lv_obj_add_event_cb(back, [](lv_event_t *) { backToSongs(); }, LV_EVENT_CLICKED, nullptr);

    const Song &song = s_songs[index];
    const size_t dash = song.folder.find(" - ");
    const std::string title = dash == std::string::npos ? song.folder : song.folder.substr(dash + 3);
    lv_obj_t *name = makeText(top, title.c_str(), &my_font_18, lv_color_white());
    lv_obj_align(name, LV_ALIGN_CENTER, 0, 0);
    s_timeLabel = makeText(top, "00:00", &my_font_14, lv_color_white());
    lv_obj_align(s_timeLabel, LV_ALIGN_RIGHT_MID, -8, 0);

    lv_obj_t *lyrics = lv_obj_create(screen);
    s_lyricsPanel = lyrics;
    lv_obj_remove_style_all(lyrics);
    lv_obj_set_size(lyrics, UiMetrics::screenW(), 116);
    lv_obj_align(lyrics, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(lyrics, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(lyrics, LV_OPA_COVER, 0);
    s_prevLine = makeText(lyrics, "", &my_font_14, Theme::c(Theme::LINE));
    lv_obj_set_width(s_prevLine, UiMetrics::screenW() - 24);
    lv_obj_align(s_prevLine, LV_ALIGN_TOP_MID, 0, 4);
    s_curLine = makeText(lyrics, "", &my_font_24, lv_color_white());
    lv_obj_set_width(s_curLine, UiMetrics::screenW() - 24);
    lv_obj_align(s_curLine, LV_ALIGN_TOP_MID, 0, 25);
    s_nextLine = makeText(lyrics, "", &my_font_14, Theme::c(Theme::LINE));
    lv_obj_set_width(s_nextLine, UiMetrics::screenW() - 24);
    lv_obj_align(s_nextLine, LV_ALIGN_TOP_MID, 0, 60);
    s_curSyllable = makeText(lyrics, "", &my_font_18, Theme::c(Theme::GOLD));
    lv_obj_align(s_curSyllable, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_t *pause = lv_button_create(top);
    s_pauseButton = pause;
    lv_obj_add_state(pause, LV_STATE_DISABLED);
    lv_obj_set_size(pause, UiMetrics::buttonWMin(), UiMetrics::buttonH());
    lv_obj_align(pause, LV_ALIGN_RIGHT_MID, -64, 0);
    s_playLabel = lv_label_create(pause);
    lv_label_set_text(s_playLabel, LV_SYMBOL_PAUSE);
    lv_obj_center(s_playLabel);
    lv_obj_add_event_cb(pause, [](lv_event_t *) { togglePause(); }, LV_EVENT_CLICKED, nullptr);
    Theme::loadScreen(screen);
    s_mediaState.store(MediaState::IDLE, std::memory_order_release);
    s_closeRequested = false;
    s_playerVisible = false;
    s_startTaskFailed = false;
    s_stopTaskFailed = false;
    s_playerTimer = lv_timer_create(playerTick, 100, nullptr);
    s_startTimer = lv_timer_create(startPendingPlayer, 230, nullptr);
    lv_timer_set_repeat_count(s_startTimer, 1);
}

void home()
{
    closePlayer();
    Display::setLandscape(false);
    UiYatzy::showMenu();
}

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
    bool sdReady = false, catalogReady = false;
    s_songs = scanSongs(sdReady, catalogReady);
    if (!sdReady) {
        showError(tr(Str::SUPERNOVA_NO_SD));
        return;
    }
    if (!catalogReady) {
        showError(tr(Str::SUPERNOVA_CATALOG_ERROR));
        return;
    }
    lv_obj_t *screen = Theme::screen();
    lv_obj_t *header = Theme::header(screen, tr(Str::MENU_SUPERNOVA));
    lv_obj_t *back = Theme::button(header, LV_SYMBOL_LEFT, 44, 36, Theme::MUTED);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -Theme::batteryStatusWidth() - 8, 0);
    lv_obj_add_event_cb(back, [](lv_event_t *) { home(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *list = lv_obj_create(screen);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, UiMetrics::marginNormal(), UiMetrics::headerH());
    lv_obj_set_size(list, UiMetrics::screenW() - 2 * UiMetrics::marginNormal(),
                    UiMetrics::screenH() - UiMetrics::headerH() - UiMetrics::footerH());
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, UiMetrics::cardSpacing(), 0);
    lv_obj_set_scrollable(list, true);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    if (s_songs.empty()) {
        lv_obj_t *empty = Theme::label(screen, tr(Str::SUPERNOVA_EMPTY), &my_font_18, Theme::MUTED);
        lv_obj_set_width(empty, UiMetrics::screenW() - 2 * UiMetrics::marginNormal());
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, 0);
    } else {
        for (size_t i = 0; i < s_songs.size(); i++) {
            const Song &song = s_songs[i];
            const size_t dash = song.folder.find(" - ");
            const std::string artist = dash == std::string::npos ? "" : song.folder.substr(0, dash);
            const std::string title = dash == std::string::npos ? song.folder : song.folder.substr(dash + 3);
            lv_obj_t *button = Theme::menuCard(list, LV_PCT(100), title.c_str(), artist.c_str(),
                                               nullptr, LV_SYMBOL_AUDIO, Theme::BLUE);
            lv_obj_add_event_cb(button, [](lv_event_t *event) {
                playSong((size_t)(uintptr_t)lv_event_get_user_data(event));
            }, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        }
    }
    Theme::loadScreen(screen);
#endif
}

}  // namespace UiSupernova
