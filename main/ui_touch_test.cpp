#include "ui_touch_test.h"

#include <math.h>
#include <inttypes.h>
#include <initializer_list>
#include <stdio.h>
#include <string.h>

#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "board_sd.h"
#include "app_config.h"
#include "esp_random.h"
#include "esp_log.h"
#include "hw_profile.h"
#include "lang.h"
#include "my_theme.h"
#include "my_sys.h"
#include "touch_test_math.h"
#include "ui_metrics.h"
#include "ui_yatzy.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

namespace UiTouchTest {
namespace {

constexpr const char *TAG = "touch-test";
constexpr const char *EXPORT_DIR = "/sdcard/otr/diagnostics";
constexpr const char *UNDO_SYMBOL = "\xef\x83\xa2";
enum class Page : uint8_t { MENU, TEST, RESULTS };
Page s_page = Page::MENU;
lv_obj_t *s_screen = nullptr;
lv_obj_t *s_canvas = nullptr;
lv_obj_t *s_status = nullptr;
lv_obj_t *s_summary = nullptr;
lv_obj_t *s_prompt = nullptr;
bool s_keypad = false;
bool s_pressed = false;
bool s_pending = false;
int16_t s_downX = 0, s_downY = 0;
int16_t s_pointerX = -1, s_pointerY = -1;
constexpr uint32_t POINTER_HOLD_MS = 300;
constexpr size_t TRAIL_CAPACITY = 64;
lv_point_t s_trail[TRAIL_CAPACITY]{};
size_t s_trailStart = 0, s_trailCount = 0;
bool s_pointerVisible = false;
lv_timer_t *s_feedbackTimer = nullptr;
float s_maxMovePx = 0;
uint32_t s_pressTick = 0, s_releaseTick = 0;
int s_canvasW = 0, s_canvasH = 0;
int s_buttonH = 0, s_gap = 3;
int s_controlsX = 0, s_controlsY = 0, s_controlsW = 0, s_controlsH = 0;
unsigned s_position = 0;
unsigned s_targets[81]{};
unsigned s_targetCount = 0;
unsigned s_target = 0;
int16_t s_expectedX = 0, s_expectedY = 0;
TouchTest::Sample s_samples[2][TouchTest::MAX_SAMPLES]{};
size_t s_sampleCount[2]{};
uint32_t s_exportSequence = 0;

unsigned modeIndex() { return s_keypad ? 1u : 0u; }
unsigned gridSize() { return s_keypad ? 3u : 9u; }
unsigned sampleCount() { return static_cast<unsigned>(s_sampleCount[modeIndex()]); }
int screenW() { return UiMetrics::screenW(); }
int screenH() { return UiMetrics::screenH(); }

struct Layout {
    bool landscape;
    int margin, gap, top, buttonH, boardSize, controlsX, controlsY, controlsW, controlsH;
};

Layout sudokuLayout()
{
    const int w = screenW(), h = screenH();
    const bool landscape = w > h;
    const int margin = 4, gap = 3;
    const int top = LV_MAX(UiMetrics::titleH(), Theme::headerHeight()) + 4;
    const int buttonH = LV_MAX(landscape ? 48 : (h <= 500 ? 36 : 50),
                               my_font_24.line_height + 4);
    const int controlsH = 3 * buttonH + 2 * gap;
    int boardSize = landscape ? h - top - 8 : h - top - controlsH - gap - margin;
    const int widthSide = (w - 2) / 9 * 9;
    if (!landscape && boardSize >= widthSide) boardSize = widthSide;
    else boardSize = boardSize / 9 * 9;
    const int controlsX = landscape ? margin + boardSize + 10 : margin;
    const int controlsY = landscape ? top : top + boardSize + gap;
    const int controlsW = landscape ? w - controlsX - margin : w - 2 * margin;
    return {landscape, margin, gap, top, buttonH, boardSize,
            controlsX, controlsY, controlsW, controlsH};
}

void drawFill(lv_layer_t *layer, int x, int y, int w, int h, uint32_t color)
{
    if (w <= 0 || h <= 0) return;
    lv_draw_rect_dsc_t desc;
    lv_draw_rect_dsc_init(&desc);
    desc.bg_color = Theme::c(color);
    desc.bg_opa = LV_OPA_COVER;
    lv_area_t area{x, y, x + w - 1, y + h - 1};
    lv_draw_rect(layer, &desc, &area);
}

void drawGrid(lv_event_t *event)
{
    auto *canvas = lv_event_get_current_target_obj(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(canvas, &area);
    const int grid = static_cast<int>(gridSize());
    const bool results = s_page == Page::RESULTS;
    const unsigned mode = modeIndex();
    const TouchTest::Sample *samples = s_samples[mode];
    const size_t count = s_sampleCount[mode];

    drawFill(layer, area.x1, area.y1, s_canvasW, s_canvasH, Theme::PANEL);
    const int digitCellW = s_keypad && !results ? (s_canvasW - 4 * s_gap) / 5 : 0;
    const int digitCellH = s_keypad && !results ? s_buttonH : 0;
    const int cellW = s_keypad && !results ? digitCellW : s_canvasW / grid;
    const int cellH = s_keypad && !results ? digitCellH : s_canvasH / grid;
    for (int target = 0; target < grid * grid; ++target) {
        const int col = s_keypad && !results ? target % 3 + 1 : target % grid;
        const int row = s_keypad && !results ? target / 3 : target / grid;
        const int x = area.x1 + col * (cellW + (s_keypad && !results ? s_gap : 0));
        const int y = area.y1 + row * (cellH + (s_keypad && !results ? s_gap : 0));
        if (results) {
            unsigned attempts = 0, correct = 0;
            for (size_t i = 0; i < count; ++i) {
                if (samples[i].target != target) continue;
                ++attempts;
                if (!samples[i].unregistered && !samples[i].cancelled && samples[i].actual == target) ++correct;
            }
            if (attempts) {
                const uint32_t color = correct == attempts ? Theme::GREEN :
                                       correct == 0 ? Theme::RED : Theme::GOLD;
                drawFill(layer, x + 1, y + 1, cellW - 2, cellH - 2, color);
            }
        } else if (static_cast<unsigned>(target) == s_target) {
            drawFill(layer, x + 1, y + 1, cellW - 2, cellH - 2, Theme::ACCENT);
        }
    }
    if (s_keypad && !results) {
        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (col >= 1 && col <= 3) continue;
                const int x = area.x1 + col * (digitCellW + s_gap);
                const int y = area.y1 + row * (digitCellH + s_gap);
                drawFill(layer, x, y, digitCellW, digitCellH, Theme::PANEL_RAISED);
            }
        }
    } else {
        for (int line = 0; line <= grid; ++line) {
            const int x = line == grid ? s_canvasW - 1 : line * cellW;
            const int y = line == grid ? s_canvasH - 1 : line * cellH;
            const int thickness = !s_keypad && line % 3 == 0 ? 3 : 1;
            drawFill(layer, area.x1 + x, area.y1, thickness, s_canvasH, Theme::LINE);
            drawFill(layer, area.x1, area.y1 + y, s_canvasW, thickness, Theme::LINE);
        }
    }
    if (!results && s_pointerVisible) {
        lv_draw_line_dsc_t line;
        lv_draw_line_dsc_init(&line);
        line.color = Theme::c(Theme::GOLD);
        line.width = 2;
        for (size_t i = 1; i < s_trailCount; ++i) {
            const auto &from = s_trail[(s_trailStart + i - 1) % TRAIL_CAPACITY];
            const auto &to = s_trail[(s_trailStart + i) % TRAIL_CAPACITY];
            line.p1.x = area.x1 + from.x;
            line.p1.y = area.y1 + from.y;
            line.p2.x = area.x1 + to.x;
            line.p2.y = area.y1 + to.y;
            lv_draw_line(layer, &line);
        }
        const int x = area.x1 + s_pointerX;
        const int y = area.y1 + s_pointerY;
        for (int degree = 0; degree < 360; degree += 6) {
            const double radians = degree * 0.017453292519943295;
            drawFill(layer, x + static_cast<int>(8 * cos(radians)), y + static_cast<int>(8 * sin(radians)),
                     2, 2, Theme::RED);
        }
        drawFill(layer, x - 9, y - 1, 19, 2, Theme::RED);
        drawFill(layer, x - 1, y - 9, 2, 19, Theme::RED);
        drawFill(layer, x - 2, y - 2, 5, 5, Theme::TEXT);
    }
}

void setStatus()
{
    if (!s_status) return;
    if (s_page == Page::TEST) {
        char text[64];
        if (s_keypad) {
            snprintf(text, sizeof(text), tr(Str::TOUCH_TEST_KEY_PROMPT),
                     s_target + 1, s_position + 1, s_targetCount, sampleCount());
        } else {
            snprintf(text, sizeof(text), tr(Str::TOUCH_TEST_PROGRESS),
                     s_position + 1, s_targetCount, sampleCount());
        }
        lv_label_set_text(s_status, text);
    } else {
        lv_label_set_text(s_status, tr(Str::TOUCH_TEST_INSTRUCTION));
    }
}

void makeTargetPoint()
{
    const int grid = static_cast<int>(gridSize());
    const bool keypad = s_keypad && s_page == Page::TEST;
    const int cellW = keypad ? (s_canvasW - 4 * s_gap) / 5 : s_canvasW / grid;
    const int cellH = keypad ? s_buttonH : s_canvasH / grid;
    const int col = keypad ? static_cast<int>(s_target % 3) + 1 : static_cast<int>(s_target % grid);
    const int row = keypad ? static_cast<int>(s_target / 3) : static_cast<int>(s_target / grid);
    s_expectedX = static_cast<int16_t>(col * (cellW + (keypad ? s_gap : 0)) +
                                       cellW / 2);
    s_expectedY = static_cast<int16_t>(row * (cellH + (keypad ? s_gap : 0)) +
                                       cellH / 2);
}

void finishPass()
{
    s_page = Page::RESULTS;
    s_pressed = s_pending = false;
    s_pointerVisible = false;
    s_pointerX = s_pointerY = -1;
    UiTouchTest::show();
}

void nextTarget()
{
    if (s_position >= s_targetCount) {
        if (!s_pointerVisible) finishPass();
        return;
    }
    s_target = s_targets[s_position];
    makeTargetPoint();
    setStatus();
    lv_obj_invalidate(s_canvas);
}

void startPass(bool keypad)
{
    s_keypad = keypad;
    if (s_sampleCount[modeIndex()] >= TouchTest::MAX_SAMPLES) {
        s_page = Page::RESULTS;
        UiTouchTest::show();
        return;
    }
    s_position = 0;
    s_targetCount = gridSize() * gridSize();
    for (unsigned i = 0; i < s_targetCount; ++i) s_targets[i] = i;
    for (unsigned i = s_targetCount; i > 1; --i) {
        const unsigned j = esp_random() % i;
        const unsigned tmp = s_targets[i - 1];
        s_targets[i - 1] = s_targets[j];
        s_targets[j] = tmp;
    }
    s_page = Page::TEST;
    s_pressed = s_pending = false;
    s_pointerVisible = false;
    s_pointerX = s_pointerY = -1;
    UiTouchTest::show();
    nextTarget();
}

void erasePrompt()
{
    if (s_prompt) {
        lv_obj_delete(s_prompt);
        s_prompt = nullptr;
    }
}

void advance()
{
    s_pending = false;
    ++s_position;
    nextTarget();
}

void promptForMiss()
{
    if (s_prompt) return;
    s_pending = true;
    s_prompt = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_prompt, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_prompt, Theme::c(Theme::BG), 0);
    lv_obj_set_style_bg_opa(s_prompt, LV_OPA_80, 0);
    lv_obj_add_flag(s_prompt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_prompt, LV_OBJ_FLAG_SCROLLABLE);
    auto *box = Theme::card(s_prompt, screenW() - 36, 166);
    lv_obj_center(box);
    auto *message = Theme::label(box, tr(Str::TOUCH_TEST_MISS_PROMPT), &my_font_14);
    lv_obj_set_width(message, LV_PCT(100));
    lv_label_set_long_mode(message, LV_LABEL_LONG_MODE_WRAP);
    auto *keep = Theme::button(box, tr(Str::TOUCH_TEST_KEEP), LV_PCT(100), 38, Theme::ACCENT);
    lv_obj_set_width(keep, LV_PCT(47));
    lv_obj_align(keep, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(keep, [](lv_event_t *) { erasePrompt(); advance(); }, LV_EVENT_CLICKED, nullptr);
    auto *retry = Theme::button(box, tr(Str::TOUCH_TEST_RETRY), LV_PCT(100), 38, Theme::MUTED);
    lv_obj_set_width(retry, LV_PCT(47));
    lv_obj_align(retry, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(retry, [](lv_event_t *) {
        if (s_sampleCount[modeIndex()]) --s_sampleCount[modeIndex()];
        erasePrompt();
        s_pending = false;
        setStatus();
        lv_obj_invalidate(s_canvas);
    }, LV_EVENT_CLICKED, nullptr);
}

TouchTest::Sample *appendSample()
{
    if (s_page != Page::TEST || s_pending || s_position >= s_targetCount) return nullptr;
    if (s_sampleCount[modeIndex()] >= TouchTest::MAX_SAMPLES) {
        finishPass();
        return nullptr;
    }
    lv_area_t area;
    lv_obj_get_coords(s_canvas, &area);
    auto &sample = s_samples[modeIndex()][s_sampleCount[modeIndex()]++];
    sample = {};
    sample.grid = static_cast<uint8_t>(gridSize());
    sample.target = static_cast<uint8_t>(s_target);
    sample.actual = UINT8_MAX;
    sample.deadZone = Config::get().sudokuDeadZone;
    sample.size = static_cast<uint16_t>(s_canvasW);
    sample.areaH = static_cast<uint16_t>(s_canvasH);
    sample.expectedX = s_expectedX;
    sample.expectedY = s_expectedY;
    sample.originX = static_cast<int16_t>(area.x1);
    sample.originY = static_cast<int16_t>(area.y1);
    sample.screenW = static_cast<uint16_t>(screenW());
    sample.screenH = static_cast<uint16_t>(screenH());
    sample.landscape = screenW() > screenH();
    return &sample;
}

void reportUnregistered()
{
    auto *sample = appendSample();
    if (!sample) return;
    sample->unregistered = true;
    setStatus();
    lv_obj_invalidate(s_canvas);
}

void recordTap(int pressX, int pressY, int releaseX, int releaseY, bool cancelled)
{
    auto *entry = appendSample();
    if (!entry) return;
    auto &sample = *entry;
    lv_area_t area;
    lv_obj_get_coords(s_canvas, &area);
    const int localX = pressX - area.x1, localY = pressY - area.y1;
    const int releasedX = releaseX - area.x1, releasedY = releaseY - area.y1;
    int actual = -1;
    if (!s_keypad) {
        actual = TouchTest::cellAtPoint(releasedX, releasedY, s_canvasW, 9, 0);
    } else {
        const int cellW = (s_canvasW - 4 * s_gap) / 5;
        const int cellH = s_buttonH;
        const int stepX = cellW + s_gap, stepY = cellH + s_gap;
        const int col = releasedX >= 0 ? releasedX / stepX : -1;
        const int row = releasedY >= 0 ? releasedY / stepY : -1;
        if (col >= 1 && col <= 3 && row >= 0 && row < 3 &&
            releasedX % stepX < cellW && releasedY % stepY < cellH)
            actual = row * 3 + col - 1;
    }
    sample.actual = actual < 0 ? UINT8_MAX : static_cast<uint8_t>(actual);
    sample.x = static_cast<int16_t>(localX);
    sample.y = static_cast<int16_t>(localY);
    sample.releaseX = static_cast<int16_t>(releasedX);
    sample.releaseY = static_cast<int16_t>(releasedY);
    sample.cancelled = cancelled;
    sample.maxMovePx = s_maxMovePx;
    sample.durationMs = lv_tick_elaps(s_pressTick);
    s_pointerX = static_cast<int16_t>(releasedX);
    s_pointerY = static_cast<int16_t>(releasedY);
    setStatus();
    lv_obj_invalidate(s_canvas);
    if (cancelled || actual != static_cast<int>(s_target)) promptForMiss();
    else advance();
}

void trackPointer(const lv_point_t &point)
{
    lv_area_t area;
    lv_obj_get_coords(s_canvas, &area);
    s_pointerX = static_cast<int16_t>(point.x - area.x1);
    s_pointerY = static_cast<int16_t>(point.y - area.y1);
    s_maxMovePx = fmaxf(s_maxMovePx, hypotf(point.x - s_downX, point.y - s_downY));
    if (s_trailCount) {
        const auto &last = s_trail[(s_trailStart + s_trailCount - 1) % TRAIL_CAPACITY];
        if (last.x == s_pointerX && last.y == s_pointerY) return;
    }
    if (s_trailCount == TRAIL_CAPACITY) {
        s_trailStart = (s_trailStart + 1) % TRAIL_CAPACITY;
        --s_trailCount;
    }
    s_trail[(s_trailStart + s_trailCount++) % TRAIL_CAPACITY] = {s_pointerX, s_pointerY};
    lv_obj_invalidate(s_canvas);
}

void screenInput(lv_event_t *event)
{
    if (s_page != Page::TEST || s_pending || s_position >= s_targetCount) return;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING &&
        code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) return;
    lv_indev_t *input = lv_indev_active();
    if (!input) return;
    lv_point_t point;
    lv_indev_get_point(input, &point);
    if (code == LV_EVENT_PRESSED) {
        s_pressed = true;
        s_downX = point.x;
        s_downY = point.y;
        s_pressTick = lv_tick_get();
        s_pointerVisible = true;
        lv_timer_pause(s_feedbackTimer);
        s_maxMovePx = 0;
        s_trailStart = s_trailCount = 0;
        trackPointer(point);
        return;
    }
    if (code == LV_EVENT_PRESSING) {
        if (s_pressed) trackPointer(point);
        return;
    }
    if (!s_pressed) return;
    trackPointer(point);
    s_releaseTick = lv_tick_get();
    s_pressed = false;
    lv_timer_reset(s_feedbackTimer);
    lv_timer_resume(s_feedbackTimer);
    recordTap(s_downX, s_downY, point.x, point.y, code == LV_EVENT_PRESS_LOST);
}

void discardSession()
{
    memset(s_sampleCount, 0, sizeof(s_sampleCount));
    s_page = Page::MENU;
    UiTouchTest::show();
}

void askDiscard()
{
    if (s_prompt) return;
    s_prompt = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_prompt, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_prompt, Theme::c(Theme::BG), 0);
    lv_obj_set_style_bg_opa(s_prompt, LV_OPA_80, 0);
    lv_obj_add_flag(s_prompt, LV_OBJ_FLAG_CLICKABLE);
    auto *box = Theme::card(s_prompt, screenW() - 36, 150);
    lv_obj_center(box);
    auto *message = Theme::label(box, tr(Str::TOUCH_TEST_DISCARD_CONFIRM), &my_font_14);
    lv_obj_set_width(message, LV_PCT(100));
    lv_label_set_long_mode(message, LV_LABEL_LONG_MODE_WRAP);
    auto *cancel = Theme::button(box, tr(Str::BTN_CANCEL), LV_PCT(47), 40, Theme::MUTED);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(cancel, [](lv_event_t *) { erasePrompt(); }, LV_EVENT_CLICKED, nullptr);
    auto *discard = Theme::button(box, tr(Str::TOUCH_TEST_DISCARD), LV_PCT(47), 40, Theme::RED);
    lv_obj_align(discard, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(discard, [](lv_event_t *) { erasePrompt(); discardSession(); },
                        LV_EVENT_CLICKED, nullptr);
}

void undo()
{
    const unsigned mode = modeIndex();
    if (!s_sampleCount[mode]) return;
    const TouchTest::Sample last = s_samples[mode][--s_sampleCount[mode]];
    if (s_page == Page::TEST) {
        if (s_pending) {
            s_pending = false;
        } else if (s_position && !last.unregistered) {
            --s_position;
        }
        s_target = last.target;
        s_targets[s_position] = s_target;
        s_expectedX = last.expectedX;
        s_expectedY = last.expectedY;
        setStatus();
        lv_obj_invalidate(s_canvas);
    } else if (s_page == Page::RESULTS) {
        startPass(s_keypad);
    }
}

bool writeSampleCsv(FILE *file, unsigned mode, const TouchTest::Sample &sample)
{
    if (sample.unregistered) {
        return fprintf(file, "%s,%u,,%d,%d,,,,,%d,%d,,,,,unregistered,%s,%u,%u,%u,%u,%u,%u,%d,%d,,\n",
            mode == 0 ? "board" : "keypad", sample.target,
            sample.originX + sample.expectedX, sample.originY + sample.expectedY,
            sample.expectedX, sample.expectedY,
            sample.landscape ? "landscape" : "portrait",
            sample.screenW, sample.screenH, sample.grid, sample.deadZone, sample.size, sample.areaH,
            sample.originX, sample.originY) > 0;
    }
    char actual[12];
    if (sample.actual == UINT8_MAX) snprintf(actual, sizeof(actual), "outside");
    else snprintf(actual, sizeof(actual), "%u", sample.actual);
    return fprintf(file, "%s,%u,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%s,%s,%u,%u,%u,%u,%u,%u,%d,%d,%.2f,%" PRIu32 "\n",
        mode == 0 ? "board" : "keypad", sample.target, actual,
        sample.originX + sample.expectedX, sample.originY + sample.expectedY,
        sample.originX + sample.x, sample.originY + sample.y,
        sample.originX + sample.releaseX, sample.originY + sample.releaseY,
        sample.expectedX, sample.expectedY, sample.x, sample.y,
        sample.releaseX, sample.releaseY,
        sample.cancelled ? "cancelled" : "released",
        sample.landscape ? "landscape" : "portrait",
        sample.screenW, sample.screenH, sample.grid, sample.deadZone, sample.size, sample.areaH,
        sample.originX, sample.originY, static_cast<double>(sample.maxMovePx), sample.durationMs) > 0;
}

bool exportCsv(char *path, size_t pathCapacity)
{
    if (!BoardSd::mounted()) {
        errno = ENODEV;
        return false;
    }
    BoardSd::Transfer transfer;
    if (!transfer.active) {
        errno = EBUSY;
        return false;
    }
    if (!BoardSd::ensureDir(EXPORT_DIR)) return false;

    char timestamp[24];
    const time_t now = time(nullptr);
    struct tm local{};
    if (now > 1600000000 && localtime_r(&now, &local) &&
        strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", &local)) {
    } else {
        snprintf(timestamp, sizeof(timestamp), "uptime-%lu",
                 static_cast<unsigned long>(MySys::millis()));
    }

    struct stat st{};
    bool available = false;
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        const int length = snprintf(path, pathCapacity, "%s/touch_%s_%" PRIu32 ".csv",
                                   EXPORT_DIR, timestamp, s_exportSequence++);
        if (length < 0 || static_cast<size_t>(length) >= pathCapacity) {
            errno = ENAMETOOLONG;
            return false;
        }
        if (stat(path, &st) != 0) {
            if (errno == ENOENT) { available = true; break; }
            return false;
        }
    }
    if (!available) {
        errno = EEXIST;
        return false;
    }

    char temporary[160];
    const int temporaryLength = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    if (temporaryLength < 0 || static_cast<size_t>(temporaryLength) >= sizeof(temporary)) {
        errno = ENAMETOOLONG;
        return false;
    }
    FILE *file = fopen(temporary, "wb");
    if (!file) return false;
    bool good = fprintf(file,
        "# firmware=%s\n# profile=%s\n# indices=zero-based; outside=255\n"
        "# expected_position=cell-center reference, not a precision target\n"
        "mode,target,actual,expected_screen_x,expected_screen_y,pressed_screen_x,pressed_screen_y,"
        "released_screen_x,released_screen_y,expected_local_x,expected_local_y,pressed_local_x,"
        "pressed_local_y,released_local_x,released_local_y,status,orientation,"
        "screen_w,screen_h,grid_size,dead_zone,area_w,area_h,origin_x,origin_y,max_move_px,duration_ms\n",
        FW_VERSION, HwProfile::active().name) > 0;
    for (unsigned mode = 0; good && mode < 2; ++mode) {
        for (size_t i = 0; good && i < s_sampleCount[mode]; ++i) {
            good = writeSampleCsv(file, mode, s_samples[mode][i]);
        }
    }
    if (fflush(file) != 0) good = false;
    if (fsync(fileno(file)) != 0) good = false;
    if (fclose(file) != 0) good = false;
    if (good && rename(temporary, path) == 0) return true;
    const int reason = errno;
    remove(temporary);
    errno = reason;
    return false;
}

void showSaveResult(bool saved, const char *path, int error)
{
    s_prompt = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_prompt, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_prompt, Theme::c(Theme::BG), 0);
    lv_obj_set_style_bg_opa(s_prompt, LV_OPA_80, 0);
    lv_obj_add_flag(s_prompt, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_prompt, LV_OBJ_FLAG_SCROLLABLE);
    auto *box = Theme::card(s_prompt, screenW() - 36, 166);
    lv_obj_center(box);
    char message[192];
    if (saved) {
        snprintf(message, sizeof(message), tr(Str::TOUCH_TEST_SAVE_OK), path);
    } else {
        snprintf(message, sizeof(message), tr(Str::TOUCH_TEST_SAVE_FAILED), error);
    }
    auto *label = Theme::label(box, message, &my_font_14);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    auto *ok = Theme::button(box, tr(Str::ADV_TALK_OK), LV_PCT(100), 38, Theme::ACCENT);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(ok, [](lv_event_t *) { erasePrompt(); }, LV_EVENT_CLICKED, nullptr);
}

void saveResults()
{
    char path[128];
    if (exportCsv(path, sizeof(path))) {
        ESP_LOGI(TAG, "touch test CSV saved: %s", path);
        showSaveResult(true, path, 0);
    } else {
        const int error = errno;
        ESP_LOGE(TAG, "touch test CSV export failed errno=%d", error);
        showSaveResult(false, nullptr, error);
    }
}

void resultSummary()
{
    if (!s_summary) return;
    const unsigned mode = modeIndex();
    char text[384];
    if (!s_keypad) {
        char zones[180] = "";
        for (int deadZone = 0; deadZone <= 8; ++deadZone) {
            const auto stats = TouchTest::analyzeZone(s_samples[mode], s_sampleCount[mode],
                                                       9, s_canvasW, deadZone);
            char part[24];
            snprintf(part, sizeof(part), "%s%d:%u/%u/%u",
                     deadZone ? " " : "", deadZone, stats.correct,
                     stats.rejected, stats.misassigned);
            strncat(zones, part, sizeof(zones) - strlen(zones) - 1);
        }
        const auto zero = TouchTest::analyzeZone(s_samples[mode], s_sampleCount[mode], 9, s_canvasW, 0);
        const bool enough = zero.edgeSamples >= 20 && zero.samples >= 81;
        if (enough) {
            const int candidate = TouchTest::recommendDeadZone(s_samples[mode], s_sampleCount[mode], s_canvasW);
            char candidateText[16];
            if (candidate == -2) snprintf(candidateText, sizeof(candidateText), "%s", tr(Str::TOUCH_TEST_NO_ZONE));
            else snprintf(candidateText, sizeof(candidateText), "%d", candidate);
            snprintf(text, sizeof(text), tr(Str::TOUCH_TEST_ZONE_FMT),
                     static_cast<unsigned>(s_sampleCount[mode]), candidateText, zones);
        } else {
            snprintf(text, sizeof(text), tr(Str::TOUCH_TEST_ZONE_INSUFFICIENT),
                     static_cast<unsigned>(s_sampleCount[mode]), zones);
        }
    } else {
        unsigned correct = 0;
        for (size_t i = 0; i < s_sampleCount[mode]; ++i)
            if (!s_samples[mode][i].unregistered && !s_samples[mode][i].cancelled &&
                s_samples[mode][i].actual == s_samples[mode][i].target) ++correct;
        snprintf(text, sizeof(text), tr(Str::TOUCH_TEST_KEY_RESULT),
                 static_cast<unsigned>(s_sampleCount[mode]), correct);
    }
    unsigned unregistered = 0;
    for (size_t i = 0; i < s_sampleCount[mode]; ++i)
        if (s_samples[mode][i].unregistered) ++unregistered;
    char counts[96], summary[512];
    snprintf(counts, sizeof(counts), tr(Str::TOUCH_TEST_COUNTS),
             static_cast<unsigned>(s_sampleCount[mode]) - unregistered, unregistered);
    snprintf(summary, sizeof(summary), "%s\n%s", counts, text);
    lv_label_set_text(s_summary, summary);
}

void makeScreen()
{
    auto *screen = Theme::screen();
    s_screen = screen;
    s_canvas = s_status = s_summary = nullptr;
    s_prompt = nullptr;
    const char *title = tr(s_page == Page::MENU ? Str::TOUCH_TEST_TITLE :
                           s_page == Page::RESULTS ? Str::TOUCH_TEST_RESULT :
                           s_keypad ? Str::TOUCH_TEST_KEYS : Str::TOUCH_TEST_BOARD);
    auto *header = Theme::header(screen, title);
    auto *headerInfo = lv_obj_get_child(header, 0);
    auto *back = Theme::button(header, LV_SYMBOL_LEFT, 32, 32, Theme::MUTED);
    lv_obj_set_pos(back, 6, 6);
    lv_obj_add_event_cb(back, [](lv_event_t *) {
        if (s_page == Page::MENU) UiYatzy::showMenu();
        else if (s_page == Page::TEST) finishPass();
        else { s_page = Page::MENU; UiTouchTest::show(); }
    }, LV_EVENT_CLICKED, nullptr);
    if (s_page == Page::TEST) {
        s_status = headerInfo;
        lv_obj_set_pos(s_status, 44, 6);
        lv_obj_set_size(s_status, LV_MAX(1, screenW() - Theme::statusReservedWidth() - 50),
                        my_font_14.line_height);
        lv_obj_set_style_text_font(s_status, &my_font_14, 0);
        lv_label_set_long_mode(s_status, LV_LABEL_LONG_MODE_DOTS);
    }

    if (s_page == Page::MENU) {
        auto *board = Theme::button(screen, tr(Str::TOUCH_TEST_BOARD), screenW() - 28,
                                    UiMetrics::buttonH(), Theme::ACCENT);
        lv_obj_align(board, LV_ALIGN_BOTTOM_MID, 0, -UiMetrics::footerH() - UiMetrics::buttonH() - 14);
        lv_obj_add_event_cb(board, [](lv_event_t *) { startPass(false); }, LV_EVENT_CLICKED, nullptr);
        auto *keys = Theme::button(screen, tr(Str::TOUCH_TEST_KEYS), screenW() - 28,
                                   UiMetrics::buttonH(), Theme::TEAL);
        lv_obj_align(keys, LV_ALIGN_BOTTOM_MID, 0, -UiMetrics::footerH() - 6);
        lv_obj_add_event_cb(keys, [](lv_event_t *) { startPass(true); }, LV_EVENT_CLICKED, nullptr);
        auto *hint = Theme::label(screen, tr(Str::TOUCH_TEST_INSTRUCTION), &my_font_14, Theme::MUTED);
        lv_obj_set_width(hint, screenW() - 24);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_pos(hint, 12, LV_MAX(UiMetrics::titleH(), Theme::headerHeight()) + 6);
    } else {
        const Layout layout = sudokuLayout();
        s_gap = layout.gap;
        s_buttonH = layout.buttonH;
        s_controlsX = layout.controlsX;
        s_controlsY = layout.controlsY;
        s_controlsW = layout.controlsW;
        s_controlsH = layout.controlsH;
        const int footer = layout.buttonH + 12;
        int canvasX = 0, canvasY = 0;
        if (s_page == Page::TEST && s_keypad) {
            s_canvasW = s_controlsW;
            s_canvasH = s_controlsH;
            canvasX = s_controlsX;
            canvasY = s_controlsY;
        } else if (s_page == Page::TEST) {
            s_canvasW = s_canvasH = layout.boardSize;
            canvasX = layout.landscape ? layout.margin : (screenW() - layout.boardSize) / 2;
            canvasY = layout.top;
        } else {
            const int summaryWidth = screenW() > screenH() ? screenW() / 2 : screenW() - 16;
            int size = screenW() > screenH()
                ? LV_MIN(layout.boardSize, summaryWidth - 16)
                : LV_MIN(screenW() - 16, screenH() - layout.top - footer - 88);
            size = LV_MAX(static_cast<int>(gridSize()) * 9, size);
            size = size / static_cast<int>(gridSize()) * static_cast<int>(gridSize());
            s_canvasW = s_canvasH = size;
            canvasX = screenW() > screenH() ? 8 : (screenW() - size) / 2;
            canvasY = layout.top;
        }
        s_canvas = lv_obj_create(screen);
        lv_obj_remove_style_all(s_canvas);
        lv_obj_set_size(s_canvas, s_canvasW, s_canvasH);
        lv_obj_set_pos(s_canvas, canvasX, canvasY);
        lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(s_canvas, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_remove_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(s_canvas, drawGrid, LV_EVENT_DRAW_MAIN, nullptr);

        if (s_page == Page::TEST) {
            auto *feedbackTimer = lv_timer_create([](lv_timer_t *timer) {
                lv_timer_pause(timer);
                if (lv_timer_get_user_data(timer) != s_canvas || s_page != Page::TEST ||
                    s_pressed || !s_pointerVisible || lv_tick_elaps(s_releaseTick) < POINTER_HOLD_MS) return;
                s_pointerVisible = false;
                if (s_position >= s_targetCount && !s_pending) finishPass();
                else if (s_canvas) lv_obj_invalidate(s_canvas);
            }, POINTER_HOLD_MS, s_canvas);
            s_feedbackTimer = feedbackTimer;
            lv_timer_pause(feedbackTimer);
            lv_obj_add_event_cb(s_canvas, [](lv_event_t *event) {
                auto *timer = static_cast<lv_timer_t *>(lv_event_get_user_data(event));
                if (s_feedbackTimer == timer) s_feedbackTimer = nullptr;
                lv_timer_delete(timer);
            }, LV_EVENT_DELETE, feedbackTimer);
            lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
            if (s_keypad) {
                const int width = (s_controlsW - 4 * s_gap) / 5;
                auto *missed = Theme::button(screen, tr(Str::TOUCH_TEST_UNREGISTERED), width, s_buttonH, Theme::GOLD);
                lv_obj_set_style_text_font(lv_obj_get_child(missed, 0), &my_font_14, 0);
                lv_obj_set_pos(missed, s_controlsX + 4 * (width + s_gap), s_controlsY);
                lv_obj_add_event_cb(missed, [](lv_event_t *) { reportUnregistered(); }, LV_EVENT_CLICKED, nullptr);
                const char *icons[] = {UNDO_SYMBOL, LV_SYMBOL_CLOSE, LV_SYMBOL_OK};
                for (int row = 0; row < 3; ++row) {
                    auto *button = Theme::button(screen, icons[row], width, s_buttonH,
                                                 row == 1 ? Theme::RED : Theme::MUTED);
                    lv_obj_set_pos(button, s_controlsX, s_controlsY + row * (s_buttonH + s_gap));
                    if (row == 0) lv_obj_add_event_cb(button, [](lv_event_t *) { undo(); }, LV_EVENT_CLICKED, nullptr);
                    if (row == 1) lv_obj_add_event_cb(button, [](lv_event_t *) { askDiscard(); }, LV_EVENT_CLICKED, nullptr);
                    if (row == 2) lv_obj_add_event_cb(button, [](lv_event_t *) { finishPass(); },
                                                     LV_EVENT_CLICKED, nullptr);
                }
            } else if (layout.landscape) {
                const int y = s_controlsY + my_font_14.line_height + 8;
                auto *missed = Theme::button(screen, tr(Str::TOUCH_TEST_UNREGISTERED), s_controlsW, s_buttonH, Theme::GOLD);
                lv_obj_set_pos(missed, s_controlsX, y);
                lv_obj_add_event_cb(missed, [](lv_event_t *) { reportUnregistered(); }, LV_EVENT_CLICKED, nullptr);
                auto *undoButton = Theme::button(screen, tr(Str::TOUCH_TEST_UNDO), s_controlsW,
                                                  s_buttonH, Theme::MUTED);
                lv_obj_set_pos(undoButton, s_controlsX, y + s_buttonH + s_gap);
                lv_obj_add_event_cb(undoButton, [](lv_event_t *) { undo(); }, LV_EVENT_CLICKED, nullptr);
                auto *discard = Theme::button(screen, tr(Str::TOUCH_TEST_DISCARD), s_controlsW,
                                               s_buttonH, Theme::RED);
                lv_obj_set_pos(discard, s_controlsX, y + 2 * (s_buttonH + s_gap));
                lv_obj_add_event_cb(discard, [](lv_event_t *) { askDiscard(); }, LV_EVENT_CLICKED, nullptr);
                auto *backButton = Theme::button(screen, tr(Str::TOUCH_TEST_DONE), s_controlsW,
                                                  s_buttonH, Theme::MUTED);
                lv_obj_set_pos(backButton, s_controlsX, y + 3 * (s_buttonH + s_gap));
                lv_obj_add_event_cb(backButton, [](lv_event_t *) { finishPass(); },
                                    LV_EVENT_CLICKED, nullptr);
            } else {
                const int gap = 4;
                const int width = (screenW() - 16 - gap * 3) / 4;
                auto *missed = Theme::button(screen, tr(Str::TOUCH_TEST_UNREGISTERED), width, s_buttonH, Theme::GOLD);
                lv_obj_align(missed, LV_ALIGN_BOTTOM_LEFT, 8, -6);
                lv_obj_add_event_cb(missed, [](lv_event_t *) { reportUnregistered(); }, LV_EVENT_CLICKED, nullptr);
                auto *undoButton = Theme::button(screen, tr(Str::TOUCH_TEST_UNDO), width,
                                                  s_buttonH, Theme::MUTED);
                lv_obj_align(undoButton, LV_ALIGN_BOTTOM_LEFT, 8 + width + gap, -6);
                lv_obj_add_event_cb(undoButton, [](lv_event_t *) { undo(); }, LV_EVENT_CLICKED, nullptr);
                auto *discard = Theme::button(screen, tr(Str::TOUCH_TEST_DISCARD), width,
                                               s_buttonH, Theme::RED);
                lv_obj_align(discard, LV_ALIGN_BOTTOM_LEFT, 8 + 2 * (width + gap), -6);
                lv_obj_add_event_cb(discard, [](lv_event_t *) { askDiscard(); }, LV_EVENT_CLICKED, nullptr);
                auto *backButton = Theme::button(screen, tr(Str::TOUCH_TEST_DONE), width,
                                                  s_buttonH, Theme::MUTED);
                lv_obj_align(backButton, LV_ALIGN_BOTTOM_RIGHT, -8, -6);
                lv_obj_add_event_cb(backButton, [](lv_event_t *) { finishPass(); },
                                    LV_EVENT_CLICKED, nullptr);
                for (auto *button : {missed, undoButton, discard, backButton})
                    lv_obj_set_style_text_font(lv_obj_get_child(button, 0), &my_font_14, 0);
            }
            setStatus();
        } else {
            s_summary = Theme::label(screen, "", &my_font_14, Theme::MUTED);
            if (screenW() > screenH()) {
                lv_obj_set_width(s_summary, screenW() - s_canvasW - 20);
                lv_obj_set_height(s_summary, screenH() - layout.top - footer);
                lv_obj_set_pos(s_summary, s_canvasW + 16, layout.top);
            } else {
                lv_obj_set_width(s_summary, screenW() - 16);
                lv_obj_set_height(s_summary, screenH() - layout.top - s_canvasH - footer - 8);
                lv_obj_set_pos(s_summary, 8, layout.top + s_canvasH + 4);
            }
            resultSummary();
            const int gap = 4;
            const int width = (screenW() - 20 - gap * 3) / 4;
            const char *labels[] = {tr(Str::TOUCH_TEST_SAVE), tr(Str::TOUCH_TEST_AGAIN),
                                    tr(Str::TOUCH_TEST_UNDO), tr(Str::TOUCH_TEST_DISCARD)};
            const uint32_t colors[] = {Theme::TEAL, Theme::ACCENT, Theme::MUTED, Theme::RED};
            for (int i = 0; i < 4; ++i) {
                auto *button = Theme::button(screen, labels[i], width, layout.buttonH, colors[i]);
                lv_obj_set_style_text_font(lv_obj_get_child(button, 0), &my_font_14, 0);
                lv_obj_set_pos(button, 8 + i * (width + gap), screenH() - layout.buttonH - 6);
                if (i == 0) lv_obj_add_event_cb(button, [](lv_event_t *) { saveResults(); }, LV_EVENT_CLICKED, nullptr);
                if (i == 1) lv_obj_add_event_cb(button, [](lv_event_t *) { startPass(s_keypad); }, LV_EVENT_CLICKED, nullptr);
                if (i == 2) lv_obj_add_event_cb(button, [](lv_event_t *) { undo(); }, LV_EVENT_CLICKED, nullptr);
                if (i == 3) lv_obj_add_event_cb(button, [](lv_event_t *) { askDiscard(); }, LV_EVENT_CLICKED, nullptr);
            }
        }
        lv_obj_add_event_cb(screen, screenInput, LV_EVENT_ALL, nullptr);
    }
    lv_obj_add_event_cb(screen, [](lv_event_t *event) {
        if (lv_event_get_code(event) == LV_EVENT_DELETE &&
            lv_event_get_current_target_obj(event) == s_screen) {
            s_screen = s_canvas = s_status = s_summary = nullptr;
            s_prompt = nullptr;
        }
    }, LV_EVENT_DELETE, nullptr);
    Theme::loadScreen(screen);
}

}  // namespace

void show()
{
    makeScreen();
}

}  // namespace UiTouchTest
