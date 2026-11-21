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
#include "my_theme.h"
#include "supernova_convert.h"
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
size_t s_pendingSong = 0;
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

    const Song &song = s_songs[index];
    const size_t dash = song.folder.find(" - ");
    const std::string title = dash == std::string::npos ? song.folder : song.folder.substr(dash + 3);
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

// On-device MP4 -> AVI conversion of every song that only has an MP4.
std::vector<size_t> s_convQueue;
size_t s_convPos = 0;
int s_convDone = 0, s_convFailed = 0;
char s_convError[64] = "";
lv_timer_t *s_convTimer = nullptr;
lv_obj_t *s_convLabel = nullptr;
lv_obj_t *s_convBar = nullptr;
lv_obj_t *s_convButton = nullptr;
lv_obj_t *s_convButtonLabel = nullptr;

bool isMp4(const Song &song) { return endsWithExt(song.video, ".mp4"); }

std::string aviPathFor(const std::string &mp4) { return mp4.substr(0, mp4.size() - 4) + ".avi"; }

std::string songName(const Song &song)
{
    const size_t slash = song.folder.find_last_of('/');
    return slash == std::string::npos ? song.folder : song.folder.substr(slash + 1);
}

void showConvert(size_t only);

bool convStartNext()
{
    while (s_convPos < s_convQueue.size()) {
        const Song &song = s_songs[s_convQueue[s_convPos]];
        if (SupernovaConvert::start(song.video.c_str(), aviPathFor(song.video).c_str())) return true;
        snprintf(s_convError, sizeof(s_convError), "%s", SupernovaConvert::error());
        s_convFailed++;
        s_convPos++;
    }
    return false;
}

void convFinish()
{
    if (s_convTimer) { lv_timer_delete(s_convTimer); s_convTimer = nullptr; }
    lv_label_set_text_fmt(s_convLabel, tr(Str::SUPERNOVA_CONVERT_DONE), s_convDone, s_convFailed);
    if (s_convFailed > 0 && s_convError[0]) {
        lv_obj_t *err = Theme::label(lv_obj_get_parent(s_convLabel), "", &my_font_14, Theme::MUTED);
        lv_label_set_text_fmt(err, tr(Str::SUPERNOVA_CONVERT_FAILED), s_convError);
        lv_obj_align_to(err, s_convLabel, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
    }
    lv_bar_set_value(s_convBar, s_convFailed == 0 ? 100 : lv_bar_get_value(s_convBar), LV_ANIM_OFF);
    lv_label_set_text(s_convButtonLabel, tr(Str::BTN_BACK));
}

void convTick(lv_timer_t *)
{
    using SupernovaConvert::State;
    const State st = SupernovaConvert::state();
    if (st == State::Running) {
        const uint32_t sec = SupernovaConvert::elapsedMs() / 1000;
        const std::string name = songName(s_songs[s_convQueue[s_convPos]]);
        lv_label_set_text_fmt(s_convLabel, tr(Str::SUPERNOVA_CONVERT_PROGRESS), (int)s_convPos + 1,
                              (int)s_convQueue.size(), name.c_str(), SupernovaConvert::percent(),
                              (unsigned)(sec / 60), (unsigned)(sec % 60));
        lv_bar_set_value(s_convBar, SupernovaConvert::percent(), LV_ANIM_OFF);
        return;
    }
    if (st == State::Cancelled) {
        SupernovaConvert::reset();
        convFinish();
        return;
    }
    if (st == State::Done || st == State::Failed) {
        if (st == State::Done) s_convDone++;
        else {
            s_convFailed++;
            snprintf(s_convError, sizeof(s_convError), "%s", SupernovaConvert::error());
        }
        s_convPos++;
        SupernovaConvert::reset();
        if (!convStartNext()) convFinish();
    }
}

void showConvert(size_t only)
{
    s_convQueue.clear();
    for (size_t i = 0; i < s_songs.size(); i++)
        if (isMp4(s_songs[i]) && (only == SIZE_MAX || only == i)) s_convQueue.push_back(i);
    if (s_convQueue.empty()) return;
    s_convPos = 0;
    s_convDone = s_convFailed = 0;
    s_convError[0] = 0;
    lv_obj_t *screen = Theme::screen();
    Theme::header(screen, tr(Str::SUPERNOVA_CONVERT_TITLE));
    const int32_t w = UiMetrics::screenW();
    s_convLabel = Theme::label(screen, "", &my_font_18, Theme::TEXT);
    lv_obj_set_width(s_convLabel, w - 2 * UiMetrics::marginNormal());
    lv_label_set_long_mode(s_convLabel, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_convLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_convLabel, LV_ALIGN_CENTER, 0, -50);
    s_convBar = lv_bar_create(screen);
    lv_obj_set_size(s_convBar, w - 4 * UiMetrics::marginNormal(), 18);
    lv_bar_set_range(s_convBar, 0, 100);
    lv_obj_align(s_convBar, LV_ALIGN_CENTER, 0, 40);
    s_convButton = Theme::button(screen, tr(Str::BTN_CANCEL), 200, 48, Theme::MUTED);
    s_convButtonLabel = lv_obj_get_child(s_convButton, 0);
    lv_obj_align(s_convButton, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_event_cb(s_convButton, [](lv_event_t *) {
        if (SupernovaConvert::state() == SupernovaConvert::State::Running) {
            SupernovaConvert::cancel();
            lv_label_set_text(s_convLabel, tr(Str::SUPERNOVA_CONVERT_STOPPING));
        } else {
            if (s_convTimer) { lv_timer_delete(s_convTimer); s_convTimer = nullptr; }
            show();
        }
    }, LV_EVENT_CLICKED, nullptr);
    Theme::loadScreen(screen);
    if (!convStartNext()) {
        convFinish();
        return;
    }
    s_convTimer = lv_timer_create(convTick, 500, nullptr);
}

void showConvertPicker()
{
    lv_obj_t *screen = Theme::screen();
    lv_obj_t *header = Theme::header(screen, tr(Str::SUPERNOVA_CONVERT_TITLE));
    lv_obj_t *back = Theme::button(header, LV_SYMBOL_LEFT, 44, 36, Theme::MUTED);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -Theme::batteryStatusWidth() - 8, 0);
    lv_obj_add_event_cb(back, [](lv_event_t *) { show(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *list = lv_obj_create(screen);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, UiMetrics::marginNormal(), UiMetrics::headerH());
    lv_obj_set_size(list, UiMetrics::screenW() - 2 * UiMetrics::marginNormal(),
                    UiMetrics::screenH() - UiMetrics::headerH() - UiMetrics::footerH());
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, UiMetrics::cardSpacing(), 0);
    lv_obj_set_scrollable(list, true);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_t *all = Theme::menuCard(list, LV_PCT(100), tr(Str::SUPERNOVA_CONVERT_ALL), "",
                                    nullptr, LV_SYMBOL_DOWNLOAD, Theme::ACCENT);
    lv_obj_add_event_cb(all, [](lv_event_t *) { showConvert(SIZE_MAX); }, LV_EVENT_CLICKED, nullptr);
    for (size_t i = 0; i < s_songs.size(); i++) {
        if (!isMp4(s_songs[i])) continue;
        const std::string name = songName(s_songs[i]);
        lv_obj_t *card = Theme::menuCard(list, LV_PCT(100), name.c_str(), "", nullptr, LV_SYMBOL_VIDEO, Theme::BLUE);
        lv_obj_add_event_cb(card, [](lv_event_t *e) {
            showConvert((size_t)(uintptr_t)lv_event_get_user_data(e));
        }, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
    Theme::loadScreen(screen);
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
    if (std::any_of(s_songs.begin(), s_songs.end(), isMp4)) {
        lv_obj_t *convert = Theme::button(header, tr(Str::SUPERNOVA_CONVERT), 130, 36, Theme::BLUE);
        lv_obj_align_to(convert, back, LV_ALIGN_OUT_LEFT_MID, -8, 0);
        lv_obj_add_event_cb(convert, [](lv_event_t *) { showConvertPicker(); }, LV_EVENT_CLICKED, nullptr);
    }
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
