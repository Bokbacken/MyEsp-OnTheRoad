#include "ui_sudoku.h"

#include <atomic>
#include <stdio.h>
#include <string.h>
#include "board_backlight.h"
#include "board_sd.h"
#include "app_config.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lang.h"
#include "my_storage.h"
#include "my_sys.h"
#include "my_theme.h"
#include "sudoku_game.h"
#include "sudoku_sd.h"
#include "ui_metrics.h"
#include "ui_yatzy.h"

LV_FONT_DECLARE(my_font_raw_12);
LV_FONT_DECLARE(my_font_raw_24);
LV_FONT_DECLARE(my_font_raw_32);

namespace UiSudoku {
namespace {
Sudoku::Game s_game;
std::atomic<bool> s_running{false};
bool s_ready = false, s_paused = false, s_notes = false, s_erase = false, s_checked = false;
bool s_dirty = false, s_completedSaved = false;
bool s_saveFailed = false;
uint8_t s_digit = 1;
uint32_t s_elapsed = 0, s_tick = 0, s_savedAt = 0;
uint32_t s_next[4]{}, s_sdCounts[4]{};
uint32_t s_sdGeneration = UINT32_MAX;
std::atomic<bool> s_sdScanRunning{false}, s_sdScanReady{false};
char s_sdBlock[Sudoku::BLOCK_SIZE][Sudoku::CELL_COUNT + 1]{};
uint32_t s_sdBlockDifficulty = UINT32_MAX, s_sdBlockNumber = UINT32_MAX, s_sdBlockCount = 0;
lv_obj_t *s_board = nullptr, *s_info = nullptr, *s_help = nullptr;
lv_obj_t *s_gameScreen = nullptr, *s_listScreen = nullptr;
lv_timer_t *s_helpTimer = nullptr;
lv_obj_t *s_digits[9]{}, *s_noteButton = nullptr, *s_eraseButton = nullptr, *s_pauseButton = nullptr;
lv_obj_t *s_replaceDialog = nullptr, *s_cheer = nullptr, *s_winPrompt = nullptr, *s_winNext = nullptr;
unsigned s_replaceCell = 0;
uint8_t s_replaceDigit = 0;

struct Save {
    uint32_t version = 2;
    uint32_t elapsed = 0;
    uint32_t next[4]{};
    uint16_t notes[81]{};
    uint32_t puzzle = 0;
    uint8_t difficulty = 0, values[81]{};
    uint8_t reserved = 0;
};

struct SaveV1 {
    uint32_t version = 1;
    uint32_t elapsed = 0;
    uint16_t next[4]{};
    uint16_t notes[81]{};
    uint16_t puzzle = 0;
    uint8_t difficulty = 0, values[81]{};
    uint8_t reserved = 0;
};

Str difficultyLabel(unsigned difficulty)
{
    const Str names[] = {Str::SUDOKU_EASY, Str::SUDOKU_MEDIUM, Str::SUDOKU_HARD, Str::SUDOKU_EXTREME};
    return names[difficulty < 4 ? difficulty : 0];
}

constexpr const char *SUDOKU_SYMBOL_UNDO = "\xef\x83\xa2";
constexpr const char *SUDOKU_SYMBOL_REDO = "\xef\x8b\xb9";

void startNew(unsigned difficulty);
void showWinPrompt();

void hideHelp(lv_timer_t *timer)
{
    auto *label = static_cast<lv_obj_t *>(lv_timer_get_user_data(timer));
    if (label && lv_obj_is_valid(label)) lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(timer);
}

void showHelp(Str message, bool force = false)
{
    if (!s_help) return;
    if (!force && !Config::get().sudokuHelpHints) {
        lv_obj_add_flag(s_help, LV_OBJ_FLAG_HIDDEN);
        if (s_helpTimer) lv_timer_pause(s_helpTimer);
        return;
    }
    lv_label_set_text(s_help, tr(message));
    lv_obj_clear_flag(s_help, LV_OBJ_FLAG_HIDDEN);
    if (!s_helpTimer) s_helpTimer = lv_timer_create(hideHelp, 2200, s_help);
    if (s_helpTimer) {
        lv_timer_reset(s_helpTimer);
        lv_timer_resume(s_helpTimer);
    }
}

uint32_t available(unsigned difficulty)
{
    return Sudoku::BLOCK_SIZE +
           (s_sdScanReady.load(std::memory_order_acquire) ? s_sdCounts[difficulty] : 0);
}

uint32_t blockProgress(unsigned difficulty)
{
    const uint32_t completed = s_next[difficulty] % Sudoku::BLOCK_SIZE;
    return completed || !s_next[difficulty] ? completed : Sudoku::BLOCK_SIZE;
}

bool startPuzzle(Sudoku::Game &game, unsigned difficulty, uint32_t index)
{
    const auto level = static_cast<Sudoku::Difficulty>(difficulty);
    if (index < Sudoku::BLOCK_SIZE) return game.start(level, index);
    if (!s_sdScanReady.load(std::memory_order_acquire) || index >= available(difficulty)) return false;
    const uint32_t externalIndex = index - Sudoku::BLOCK_SIZE;
    const uint32_t block = externalIndex / Sudoku::BLOCK_SIZE;
    if (s_sdBlockDifficulty != difficulty || s_sdBlockNumber != block) {
        uint32_t count = 0;
        if (!SudokuSd::loadBlock(level, block, s_sdBlock, count)) return false;
        s_sdBlockDifficulty = difficulty;
        s_sdBlockNumber = block;
        s_sdBlockCount = count;
    }
    const uint32_t position = externalIndex % Sudoku::BLOCK_SIZE;
    if (position >= s_sdBlockCount) return false;
    return game.startFromGivens(level, index, s_sdBlock[position]);
}

void scanDone(void *)
{
    s_sdScanRunning.store(false, std::memory_order_release);
    if (s_listScreen && lv_screen_active() == s_listScreen) show();
    if (s_winNext && lv_obj_is_valid(s_winNext)) {
        const unsigned difficulty = static_cast<unsigned>(s_game.difficulty());
        if (s_next[difficulty] < available(difficulty))
            lv_obj_remove_state(s_winNext, LV_STATE_DISABLED);
        else
            lv_obj_add_state(s_winNext, LV_STATE_DISABLED);
    }
}

void scanTask(void *)
{
    uint32_t counts[4]{};
    if (SudokuSd::scan(counts))
        for (unsigned i = 0; i < 4; ++i) s_sdCounts[i] = counts[i];
    s_sdScanReady.store(true, std::memory_order_release);
    lv_async_call(scanDone, nullptr);
    vTaskDelete(nullptr);
}

void scanSdIfNeeded()
{
    const BoardSd::Info info = BoardSd::info();
    if (!s_sdScanRunning.load(std::memory_order_acquire) &&
        (info.generation != s_sdGeneration || (info.mounted && SudokuSd::changed()))) {
        s_sdGeneration = info.generation;
        for (auto &count : s_sdCounts) count = 0;
        s_sdBlockDifficulty = s_sdBlockNumber = UINT32_MAX;
        s_sdBlockCount = 0;
        s_sdScanReady.store(false, std::memory_order_release);
    }
    if (!BoardSd::mounted()) {
        for (auto &count : s_sdCounts) count = 0;
        s_sdBlockDifficulty = s_sdBlockNumber = UINT32_MAX;
        s_sdBlockCount = 0;
        s_sdScanReady.store(true, std::memory_order_release);
        return;
    }
    if (s_sdScanReady.load(std::memory_order_acquire) ||
        s_sdScanRunning.exchange(true, std::memory_order_acq_rel)) return;
    if (xTaskCreate(scanTask, "sudoku_sd", 4096, nullptr, 1, nullptr) != pdPASS) {
        s_sdScanRunning.store(false, std::memory_order_release);
        s_sdScanReady.store(true, std::memory_order_release);
        lv_async_call(scanDone, nullptr);
    }
}

void cheerAnimation(void *object, int32_t rise)
{
    auto *label = static_cast<lv_obj_t *>(object);
    if (!s_board || !lv_obj_is_valid(s_board)) return;
    lv_obj_align_to(label, s_board, LV_ALIGN_CENTER, 0, -rise);
    lv_obj_set_style_opa(label, static_cast<lv_opa_t>(LV_OPA_COVER - rise * LV_OPA_COVER / 44), 0);
}

void cheerDone(lv_anim_t *animation)
{
    auto *label = static_cast<lv_obj_t *>(animation->var);
    if (s_cheer == label) s_cheer = nullptr;
    lv_obj_delete_async(label);
    showWinPrompt();
}

void showCheer()
{
    static constexpr Str messages[] = {
        Str::SUDOKU_CHEER_1, Str::SUDOKU_CHEER_2, Str::SUDOKU_CHEER_3,
        Str::SUDOKU_CHEER_4, Str::SUDOKU_CHEER_5, Str::SUDOKU_CHEER_6
    };
    s_cheer = Theme::label(s_gameScreen, tr(messages[esp_random() % (sizeof(messages) / sizeof(messages[0]))]),
                           &my_font_24, Theme::GOLD);
    lv_obj_set_width(s_cheer, lv_obj_get_width(s_board));
    lv_label_set_long_mode(s_cheer, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(s_cheer, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(s_cheer, Theme::c(Theme::PANEL), 0);
    lv_obj_set_style_bg_opa(s_cheer, LV_OPA_80, 0);
    lv_obj_set_style_radius(s_cheer, 12, 0);
    lv_obj_set_style_pad_hor(s_cheer, 10, 0);
    lv_obj_set_style_pad_ver(s_cheer, 6, 0);
    lv_obj_add_flag(s_cheer, LV_OBJ_FLAG_FLOATING);
    lv_obj_align_to(s_cheer, s_board, LV_ALIGN_CENTER, 0, 0);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, s_cheer);
    lv_anim_set_exec_cb(&animation, cheerAnimation);
    lv_anim_set_values(&animation, 0, 44);
    lv_anim_set_duration(&animation, 3700);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&animation, cheerDone);
    lv_anim_start(&animation);
}

bool showErrorCross(unsigned cell)
{
    return s_game.incorrect(cell) || (s_checked && !s_game.matchesSolution(cell));
}

void closeWinPrompt()
{
    if (!s_winPrompt) return;
    lv_obj_delete(s_winPrompt);
    s_winPrompt = nullptr;
    s_winNext = nullptr;
}

void nextPuzzle(lv_event_t *event)
{
    const unsigned difficulty = static_cast<unsigned>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    closeWinPrompt();
    startNew(difficulty);
}

void backToPuzzleList(lv_event_t *)
{
    closeWinPrompt();
    show();
}

void showWinPrompt()
{
    if (!s_gameScreen || lv_screen_active() != s_gameScreen || !s_game.complete() || s_winPrompt) return;
    s_winPrompt = lv_obj_create(s_gameScreen);
    lv_obj_set_size(s_winPrompt, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_winPrompt, LV_OPA_80, 0);
    lv_obj_add_flag(s_winPrompt, LV_OBJ_FLAG_CLICKABLE);
    auto *box = Theme::card(s_winPrompt, UiMetrics::screenW() - 40, 174);
    lv_obj_center(box);
    auto *message = Theme::label(box, tr(Str::SUDOKU_NEXT_PROMPT), &my_font_14);
    lv_obj_set_width(message, LV_PCT(100));
    lv_label_set_long_mode(message, LV_LABEL_LONG_MODE_WRAP);
    auto *back = Theme::button(box, tr(Str::BTN_BACK), 110, 40, Theme::MUTED);
    lv_obj_align(back, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(back, backToPuzzleList, LV_EVENT_CLICKED, nullptr);
    auto *next = Theme::button(box, tr(Str::SUDOKU_NEXT), 126, 40, Theme::ACCENT);
    s_winNext = next;
    lv_obj_align(next, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(next, nextPuzzle, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(uintptr_t(s_game.difficulty())));
    const unsigned difficulty = static_cast<unsigned>(s_game.difficulty());
    if ((!s_sdScanReady.load(std::memory_order_acquire) &&
         s_next[difficulty] >= Sudoku::BLOCK_SIZE) ||
        s_next[difficulty] >= available(difficulty))
        lv_obj_add_state(next, LV_STATE_DISABLED);
}

void save()
{
    if (!s_ready || !s_dirty) return;
    Save data{};
    data.elapsed = s_elapsed;
    data.puzzle = s_game.puzzleIndex();
    data.difficulty = static_cast<uint8_t>(s_game.difficulty());
    for (unsigned i = 0; i < 4; ++i) data.next[i] = s_next[i];
    for (unsigned i = 0; i < 81; ++i) {
        data.values[i] = s_game.value(i);
        data.notes[i] = s_game.notes(i);
    }
    MyNvs::Handle h("sudoku", false);
    if (!h || !h.setBlob("game", &data, sizeof(data)) || !h.commit()) {
        ESP_LOGE("sudoku", "cannot save game");
        s_saveFailed = true;
        showHelp(Str::SUDOKU_SAVE_FAILED, true);
        return;
    }
    s_saveFailed = false;
    s_dirty = false;
}

bool restore()
{
    MyNvs::Handle h("sudoku", true);
    if (!h || h.blobSize("game") == 0) return true;
    Save data{};
    const size_t size = h.blobSize("game");
    if (size == sizeof(SaveV1)) {
        SaveV1 old{};
        if (!h.getBlob("game", &old, sizeof(old)) || old.version != 1) {
            ESP_LOGE("sudoku", "invalid saved game");
            return true;
        }
        data.elapsed = old.elapsed;
        for (unsigned i = 0; i < 4; ++i) data.next[i] = old.next[i];
        memcpy(data.notes, old.notes, sizeof(data.notes));
        data.puzzle = old.puzzle;
        data.difficulty = old.difficulty;
        memcpy(data.values, old.values, sizeof(data.values));
    } else if (size != sizeof(Save) || !h.getBlob("game", &data, sizeof(data)) ||
               data.version != 2) {
        ESP_LOGE("sudoku", "invalid saved game");
        return true;
    }
    if (data.difficulty >= 4) {
        ESP_LOGE("sudoku", "invalid saved game");
        return true;
    }
    for (unsigned i = 0; i < 4; ++i) {
        if (data.next[i] > available(i) &&
            !(data.next[i] >= Sudoku::BLOCK_SIZE &&
              !s_sdScanReady.load(std::memory_order_acquire))) {
            if (data.next[i] >= Sudoku::BLOCK_SIZE && !BoardSd::mounted()) return false;
            ESP_LOGE("sudoku", "invalid saved progress");
            return true;
        }
    }
    for (unsigned i = 0; i < 81; ++i) {
        if (data.values[i] > 9 || data.notes[i] > 0x1ff || (data.values[i] && data.notes[i])) {
            ESP_LOGE("sudoku", "invalid saved cell");
            return true;
        }
    }
    Sudoku::Game restored;
    if (!startPuzzle(restored, data.difficulty, data.puzzle)) {
        ESP_LOGE("sudoku", "cannot restore puzzle");
        if (data.puzzle >= Sudoku::BLOCK_SIZE &&
            (!BoardSd::mounted() || !s_sdScanReady.load(std::memory_order_acquire)))
            return false;
        return true;
    }
    for (unsigned i = 0; i < 81; ++i) {
        if (restored.fixed(i)) {
            if (data.values[i] != restored.value(i) || data.notes[i]) {
                ESP_LOGE("sudoku", "saved puzzle does not match bundled givens");
                return true;
            }
            continue;
        }
        if (data.values[i]) restored.put(i, data.values[i], false);
        else for (unsigned digit = 1; digit <= 9; ++digit)
            if (data.notes[i] & (1u << (digit - 1))) restored.put(i, digit, true);
    }
    s_game = restored;
    for (unsigned i = 0; i < 4; ++i) s_next[i] = data.next[i];
    s_elapsed = data.elapsed;
    s_game.clearHistory();
    s_completedSaved = s_game.complete();
    s_ready = true;
    return true;
}

void fill(lv_layer_t *layer, int x, int y, int w, int h, uint32_t color)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = Theme::c(color);
    d.bg_opa = LV_OPA_COVER;
    lv_area_t a{x, y, x + w - 1, y + h - 1};
    lv_draw_rect(layer, &d, &a);
}

// Compact, fixed-position candidate digits stay legible even in a 32-pixel cell.
void note(lv_layer_t *layer, int x, int y, unsigned digit, int scale, uint32_t color)
{
    constexpr uint16_t glyphs[] = {
        0, 0x2492, 0x73e7, 0x73cf, 0x5bc9, 0x79cf, 0x79ef, 0x7249, 0x7bef, 0x7bcf
    };
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < 3; ++col)
            if (glyphs[digit] & (1u << (14 - row * 3 - col)))
                fill(layer, x + col * scale, y + row * scale, scale, scale, color);
}

void draw(lv_event_t *event)
{
    lv_obj_t *board = lv_event_get_current_target_obj(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(board, &area);
    const int size = lv_obj_get_width(board), cell = size / 9;
    fill(layer, area.x1, area.y1, size, size, 0x243044);
    if (s_paused) return;
    for (unsigned i = 0; i < 81; ++i) {
        const int row = i / 9, col = i % 9;
        const int x = area.x1 + col * cell, y = area.y1 + row * cell;
        const uint8_t value = s_game.value(i);
        const bool highlight = value && value == s_digit;
        const uint32_t bg = highlight ? 0xFFCE55 : ((row / 3 + col / 3) % 2 ? 0xE2E7EE : 0xFAFAF5);
        fill(layer, x + 1, y + 1, cell - 1, cell - 1, bg);
        if (value) {
            char text[2] = {static_cast<char>('0' + value), 0};
            lv_draw_label_dsc_t d;
            lv_draw_label_dsc_init(&d);
            d.text = text;
            d.text_local = true;
            d.font = cell >= 42 ? &my_font_raw_32 : &my_font_raw_24;
            d.color = Theme::c(s_game.fixed(i) ? 0x172339 : 0x1468A0);
            d.align = LV_TEXT_ALIGN_CENTER;
            lv_area_t a{x, y + (cell - d.font->line_height) / 2, x + cell - 1, y + cell - 1};
            lv_draw_label(layer, &d, &a);
            if (showErrorCross(i)) {
                for (int k = 3; k < cell - 3; ++k) {
                    fill(layer, x + k, y + k, 2, 2, Theme::RED);
                    fill(layer, x + cell - k - 2, y + k, 2, 2, Theme::RED);
                }
            }
        } else {
            for (unsigned digit = 1; digit <= 9; ++digit) {
                if (!(s_game.notes(i) & (1u << (digit - 1)))) continue;
                const int scale = cell >= 42 ? 2 : 1;
                const int nx = x + (digit - 1) % 3 * cell / 3;
                const int ny = y + (digit - 1) / 3 * cell / 3;
                if (digit == s_digit) fill(layer, nx + 1, ny + 1, cell / 3 - 1, cell / 3 - 1, 0xFFCE55);
                note(layer, nx + (cell / 3 - 3 * scale) / 2,
                     ny + (cell / 3 - 5 * scale) / 2, digit, scale, 0x243044);
            }
        }
    }
    for (int i = 0; i <= 9; i += 3) {
        const int p = i == 9 ? size - 3 : i * cell;
        fill(layer, area.x1 + p, area.y1, 3, size, 0x243044);
        fill(layer, area.x1, area.y1 + p, size, 3, 0x243044);
    }
}

void refresh()
{
    for (unsigned i = 0; i < 9; ++i) {
        unsigned count = 0;
        for (unsigned cell = 0; cell < Sudoku::CELL_COUNT; ++cell)
            if (s_game.value(cell) == i + 1) ++count;
        const bool complete = count >= 9;
        const bool selected = i + 1 == s_digit;
        const uint32_t background = complete ? Theme::PANEL : selected ? Theme::GOLD : Theme::PANEL_RAISED;
        const uint32_t foreground = complete ? Theme::MUTED : selected ? Theme::BG : Theme::TEXT;
        lv_obj_set_style_bg_color(s_digits[i], Theme::c(background), 0);
        lv_obj_set_style_text_color(s_digits[i], Theme::c(foreground), 0);
        if (complete) lv_obj_add_state(s_digits[i], LV_STATE_DISABLED);
        else lv_obj_remove_state(s_digits[i], LV_STATE_DISABLED);
    }
    lv_obj_set_style_bg_color(s_noteButton, Theme::c(s_notes ? Theme::ACCENT : Theme::PANEL_RAISED), 0);
    lv_obj_set_style_bg_color(s_eraseButton, Theme::c(s_erase ? Theme::RED : Theme::PANEL_RAISED), 0);
    lv_label_set_text(lv_obj_get_child(s_pauseButton, 0), s_paused ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE);
    const bool complete = s_game.complete();
    s_running = !s_paused && !complete;
    if (complete && !s_completedSaved) {
        s_completedSaved = true;
        showCheer();
        const unsigned difficulty = static_cast<unsigned>(s_game.difficulty());
        if (s_next[difficulty] <= s_game.puzzleIndex()) s_next[difficulty] = s_game.puzzleIndex() + 1;
        s_dirty = true;
        save();
    }
    char text[48];
    if ((MySys::millis() / 2500) % 2 == 0) {
        snprintf(text, sizeof(text), "%s", tr(Str::SUDOKU_TITLE));
    } else {
        snprintf(text, sizeof(text), "%s #%u %lu:%02lu",
                 tr(difficultyLabel(static_cast<unsigned>(s_game.difficulty()))),
                 s_game.puzzleIndex() + 1,
                 (unsigned long)(s_elapsed / 60000), (unsigned long)(s_elapsed / 1000 % 60));
    }
    lv_label_set_text(s_info, text);
    if (s_saveFailed) showHelp(Str::SUDOKU_SAVE_FAILED, true);
    lv_obj_invalidate(s_board);
}

void play();

void leave()
{
    save();
    if (!s_saveFailed) show();
}

void placeDigit(unsigned index, uint8_t digit)
{
    if (s_game.put(index, digit, false)) {
        s_dirty = true;
        s_checked = false;
        const uint16_t bit = static_cast<uint16_t>(1u << (digit - 1));
        const unsigned row = index / 9, column = index % 9;
        const unsigned box = row / 3 * 3 + column / 3;
        for (unsigned peer = 0; peer < Sudoku::CELL_COUNT; ++peer) {
            if (peer == index || !(s_game.notes(peer) & bit)) continue;
            const unsigned peerRow = peer / 9, peerColumn = peer % 9;
            if (peerRow == row || peerColumn == column ||
                peerRow / 3 * 3 + peerColumn / 3 == box) {
                s_game.put(peer, digit, true);
                s_dirty = true;
            }
        }
    }
    refresh();
}

void dismissReplacement(lv_event_t *)
{
    if (!s_replaceDialog) return;
    lv_obj_delete(s_replaceDialog);
    s_replaceDialog = nullptr;
}

void confirmReplacement(lv_event_t *)
{
    const unsigned index = s_replaceCell;
    const uint8_t digit = s_replaceDigit;
    dismissReplacement(nullptr);
    if (s_paused || s_game.complete() || s_game.fixed(index)) return;
    placeDigit(index, digit);
}

void askReplaceDigit(unsigned index, uint8_t digit)
{
    s_replaceCell = index;
    s_replaceDigit = digit;
    s_replaceDialog = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_replaceDialog, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_replaceDialog, LV_OPA_80, 0);
    lv_obj_add_flag(s_replaceDialog, LV_OBJ_FLAG_CLICKABLE);
    auto *box = Theme::card(s_replaceDialog, UiMetrics::screenW() - 40, 190);
    lv_obj_center(box);
    auto *message = Theme::label(box, tr(Str::SUDOKU_REPLACE_VALUE), &my_font_14);
    lv_obj_set_width(message, LV_PCT(100));
    lv_label_set_long_mode(message, LV_LABEL_LONG_MODE_WRAP);
    auto *cancel = Theme::button(box, tr(Str::BTN_CANCEL), 110, 40, Theme::MUTED);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(cancel, dismissReplacement, LV_EVENT_CLICKED, nullptr);
    auto *replace = Theme::button(box, tr(Str::SUDOKU_CONFIRM_REPLACE), 110, 40, Theme::ACCENT);
    lv_obj_align(replace, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(replace, confirmReplacement, LV_EVENT_CLICKED, nullptr);
}

void chooseCell(unsigned index)
{
    if (s_paused || s_game.complete() || s_game.fixed(index)) return;
    if (!s_erase && !s_notes && s_game.value(index) == s_digit) {
        if (s_game.erase(index)) {
            s_dirty = true;
            s_checked = false;
        }
        refresh();
        return;
    }
    if (!s_erase && !s_notes && s_game.value(index) && s_game.value(index) != s_digit) {
        askReplaceDigit(index, s_digit);
        return;
    }
    if (!s_erase && !s_notes) {
        placeDigit(index, s_digit);
        return;
    }
    const bool changed = s_erase ? s_game.erase(index) : s_game.put(index, s_digit, s_notes);
    if (changed) {
        s_dirty = true;
        s_checked = false;
    }
    refresh();
}

int cellAtPoint(int x, int y, int size, int deadZone)
{
    const int cell = size / 9;
    if (cell <= 8 || x < 0 || y < 0 || x >= cell * 9 || y >= cell * 9) return -1;
    const int column = x / cell, row = y / cell;
    if (x % cell < deadZone || x % cell >= cell - deadZone ||
        y % cell < deadZone || y % cell >= cell - deadZone) return -1;
    return row * 9 + column;
}

void boardClick(lv_event_t *event)
{
    lv_indev_t *input = lv_indev_active();
    if (!input) return;
    lv_point_t p;
    lv_indev_get_point(input, &p);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_current_target_obj(event), &a);
    const int index = cellAtPoint(p.x - a.x1, p.y - a.y1, lv_obj_get_width(s_board),
                                  Config::get().sudokuDeadZone);
    if (index >= 0) chooseCell(static_cast<unsigned>(index));
}

void tool(lv_event_t *event)
{
    const auto action = static_cast<unsigned>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    Str hint = Str::SUDOKU_MODE_DIGIT;
    if (action == 5) {
        const uint32_t now = MySys::millis();
        if (!s_paused && !s_game.complete() && !Backlight::sleeping())
            s_elapsed += now - s_tick;
        s_paused = !s_paused;
        hint = s_paused ? Str::SUDOKU_MODE_PAUSE : Str::SUDOKU_MODE_DIGIT;
        s_tick = now;
        s_dirty = true;
        save();
    } else if (!s_paused && !s_game.complete()) {
        if (action == 0) { if (s_game.undo()) s_dirty = true; s_checked = false; hint = Str::SUDOKU_MODE_UNDO; }
        if (action == 1) { if (s_game.redo()) s_dirty = true; s_checked = false; hint = Str::SUDOKU_MODE_REDO; }
        if (action == 2) { s_checked = true; hint = Str::SUDOKU_MODE_CHECK; }
        if (action == 3) {
            s_notes = !s_notes; s_erase = false;
            hint = s_notes ? Str::SUDOKU_MODE_NOTES : Str::SUDOKU_MODE_DIGIT;
        }
        if (action == 4) {
            s_erase = !s_erase;
            hint = s_erase ? Str::SUDOKU_MODE_ERASE :
                   s_notes ? Str::SUDOKU_MODE_NOTES : Str::SUDOKU_MODE_DIGIT;
        }
    }
    showHelp(hint);
    refresh();
}

void digitClick(lv_event_t *event)
{
    s_digit = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    s_erase = false;
    refresh();
    showHelp(s_notes ? Str::SUDOKU_MODE_NOTES : Str::SUDOKU_MODE_DIGIT);
}

void tick(lv_timer_t *timer)
{
    auto *screen = static_cast<lv_obj_t *>(lv_timer_get_user_data(timer));
    if (lv_screen_active() != screen) return;
    const uint32_t now = MySys::millis();
    if (!s_paused && !s_game.complete() && !Backlight::sleeping()) {
        s_elapsed += now - s_tick;
        s_dirty = true;
    }
    s_tick = now;
    if (now - s_savedAt >= 30000) { s_savedAt = now; save(); }
    refresh();
}

void play()
{
    Theme::setStatusShareAvailable(false);
    lv_obj_t *screen = Theme::screen();
    s_gameScreen = screen;
    lv_obj_t *header = Theme::header(screen, tr(Str::SUDOKU_TITLE));
    s_info = lv_obj_get_child(header, 0);
    const int w = UiMetrics::screenW(), h = UiMetrics::screenH();
    const int margin = 4, gap = 3;
    const bool landscape = w > h;
    const int top = UiMetrics::titleH() + 4;
    const int buttonH = landscape ? 42 : (h <= 500 ? 30 : 40);
    const int controlsH = 3 * buttonH + 2 * gap;
    int side = landscape ? h - top - 8 : h - top - controlsH - gap - margin;
    const int widthSide = (w - 2) / 9 * 9;
    if (!landscape && side >= widthSide) side = widthSide;
    else side = side / 9 * 9;
    const int controlsX = landscape ? margin + side + 10 : margin;
    const int controlsY = landscape ? top : top + side + gap;
    const int controlsW = landscape ? w - controlsX - margin : w - 2 * margin;
    const int buttonW = (controlsW - 4 * gap) / 5;

    auto *back = Theme::button(header, LV_SYMBOL_LEFT, 32, 32, Theme::MUTED);
    lv_obj_set_pos(back, 6, 6);
    lv_obj_add_event_cb(back, [](lv_event_t *) { leave(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_pos(s_info, 44, 6);
    lv_obj_set_size(s_info, LV_MAX(1, w - Theme::STATUS_BAR_RESERVED_WIDTH - 50), 32);
    lv_obj_set_style_text_font(s_info, &my_font_raw_12, 0);
    lv_label_set_long_mode(s_info, LV_LABEL_LONG_MODE_DOTS);
    s_board = lv_obj_create(screen);
    lv_obj_remove_style_all(s_board);
    lv_obj_set_pos(s_board, landscape ? margin : (w - side) / 2, top);
    lv_obj_set_size(s_board, side, side);
    lv_obj_add_flag(s_board, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_board, draw, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(s_board, boardClick, LV_EVENT_CLICKED, nullptr);
    const char *tools[] = {SUDOKU_SYMBOL_UNDO, SUDOKU_SYMBOL_REDO, LV_SYMBOL_OK,
                           LV_SYMBOL_EDIT, LV_SYMBOL_CLOSE, LV_SYMBOL_PAUSE};
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned col = 0; col < 5; ++col) {
            const bool digit = col >= 1 && col <= 3;
            const unsigned id = digit ? row * 3 + col : row + (col == 4 ? 3 : 0);
            char label[2] = {static_cast<char>('0' + id), 0};
            auto *b = Theme::button(screen, digit ? label : tools[id], buttonW, buttonH, Theme::LINE);
            lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &my_font_raw_24, 0);
            lv_obj_set_pos(b, controlsX + col * (buttonW + gap), controlsY + row * (buttonH + gap));
            lv_obj_add_event_cb(b, digit ? digitClick : tool, LV_EVENT_CLICKED, reinterpret_cast<void *>(uintptr_t(id)));
            if (digit) s_digits[id - 1] = b;
            else if (id == 3) s_noteButton = b;
            else if (id == 4) s_eraseButton = b;
            else if (id == 5) s_pauseButton = b;
        }
    }
    s_help = Theme::label(screen, "", &my_font_raw_12, Theme::MUTED);
    lv_obj_set_width(s_help, w - 16);
    lv_obj_set_style_text_align(s_help, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(s_help, Theme::c(Theme::PANEL), 0);
    lv_obj_set_style_bg_opa(s_help, LV_OPA_90, 0);
    lv_obj_set_style_radius(s_help, 8, 0);
    lv_obj_set_style_pad_hor(s_help, 6, 0);
    lv_obj_set_style_pad_ver(s_help, 4, 0);
    lv_obj_align(s_help, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_add_flag(s_help, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(s_help, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_long_mode(s_help, LV_LABEL_LONG_MODE_WRAP);
    s_tick = s_savedAt = MySys::millis();
    s_paused = false;
    auto *timer = lv_timer_create(tick, 1000, screen);
    lv_obj_add_event_cb(screen, [](lv_event_t *event) {
        if (lv_event_get_target(event) != lv_event_get_current_target(event)) return;
        auto *timer = static_cast<lv_timer_t *>(lv_event_get_user_data(event));
        if (lv_event_get_code(event) == LV_EVENT_SCREEN_UNLOAD_START) {
            if (lv_event_get_current_target_obj(event) == s_gameScreen) {
                s_running = false;
                save();
                s_board = s_help = s_gameScreen = nullptr;
                s_cheer = s_winPrompt = nullptr;
                s_winNext = nullptr;
                s_info = nullptr;
                if (s_helpTimer) lv_timer_pause(s_helpTimer);
            }
            lv_timer_pause(timer);
        } else if (lv_event_get_code(event) == LV_EVENT_DELETE) {
            lv_timer_delete(timer);
            if (s_helpTimer) {
                lv_timer_delete(s_helpTimer);
                s_helpTimer = nullptr;
            }
        }
    }, LV_EVENT_ALL, timer);
    refresh();
    showHelp(s_paused ? Str::SUDOKU_MODE_PAUSE : Str::SUDOKU_MODE_DIGIT);
    Theme::loadScreen(screen);
}

void startNew(unsigned difficulty)
{
    if (difficulty >= 4 || s_next[difficulty] >= available(difficulty)) return;
    if (!startPuzzle(s_game, difficulty, s_next[difficulty])) {
        ESP_LOGE("sudoku", "cannot start puzzle");
        return;
    }
    s_ready = s_dirty = true;
    s_completedSaved = s_checked = s_notes = s_erase = false;
    s_digit = 1;
    s_elapsed = 0;
    save();
    play();
}

void start(lv_event_t *event)
{
    const unsigned difficulty = static_cast<unsigned>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    if (!s_ready || s_game.complete()) { startNew(difficulty); return; }
    if (static_cast<unsigned>(s_game.difficulty()) == difficulty &&
        s_game.puzzleIndex() == s_next[difficulty]) { play(); return; }
    auto *shade = lv_obj_create(lv_screen_active());
    lv_obj_set_size(shade, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(shade, LV_OPA_80, 0);
    auto *box = Theme::card(shade, UiMetrics::screenW() - 40, 190);
    lv_obj_center(box);
    auto *message = Theme::label(box, tr(Str::SUDOKU_REPLACE), &my_font_14);
    lv_obj_set_width(message, LV_PCT(100));
    auto *cancel = Theme::button(box, tr(Str::BTN_CANCEL), 110, 40, Theme::MUTED);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_event_cb(cancel, [](lv_event_t *e) {
        lv_obj_delete(static_cast<lv_obj_t *>(lv_event_get_user_data(e)));
    }, LV_EVENT_CLICKED, shade);
    auto *ok = Theme::button(box, tr(Str::SUDOKU_NEW), 110, 40, Theme::ACCENT);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(ok, [](lv_event_t *e) {
        startNew(static_cast<unsigned>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));
    }, LV_EVENT_CLICKED, reinterpret_cast<void *>(uintptr_t(difficulty)));
}
}  // namespace

bool running() { return s_running.load(); }

void show()
{
    static bool restoreChecked = false;
    if (!restoreChecked) restoreChecked = restore();
    scanSdIfNeeded();
    Theme::setStatusShareAvailable(false);
    auto *screen = Theme::screen();
    s_listScreen = screen;
    lv_obj_add_event_cb(screen, [](lv_event_t *event) {
        if (lv_event_get_code(event) == LV_EVENT_DELETE &&
            lv_event_get_current_target_obj(event) == s_listScreen) s_listScreen = nullptr;
    }, LV_EVENT_ALL, nullptr);
    Theme::header(screen, tr(Str::SUDOKU_TITLE));
    auto *list = lv_obj_create(screen);
    lv_obj_remove_style_all(list);
    lv_obj_set_pos(list, 8, UiMetrics::headerH());
    lv_obj_set_size(list, UiMetrics::screenW() - 16, UiMetrics::screenH() - UiMetrics::headerH() - 48);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, UiMetrics::cardSpacing(), 0);
    if (s_ready && !s_game.complete()) {
        auto *resume = Theme::button(list, tr(Str::SUDOKU_RESUME), LV_PCT(100), UiMetrics::buttonH(), Theme::GREEN);
        lv_obj_add_event_cb(resume, [](lv_event_t *) { play(); }, LV_EVENT_CLICKED, nullptr);
    }
    for (unsigned i = 0; i < 4; ++i) {
        char description[96];
        if (s_sdScanReady.load(std::memory_order_acquire)) {
            snprintf(description, sizeof(description), tr(Str::SUDOKU_PROGRESS),
                     blockProgress(i), Sudoku::BLOCK_SIZE, available(i));
        } else {
            snprintf(description, sizeof(description), "%s", tr(Str::SUDOKU_SD_SCANNING));
        }
        auto *b = Theme::menuCard(list, LV_PCT(100), tr(difficultyLabel(i)), description,
                                 nullptr, LV_SYMBOL_LIST, Theme::GOLD);
        if (s_next[i] >= available(i) ||
            (!s_sdScanReady.load(std::memory_order_acquire) &&
             s_next[i] >= Sudoku::BLOCK_SIZE))
            lv_obj_add_state(b, LV_STATE_DISABLED);
        lv_obj_add_event_cb(b, start, LV_EVENT_CLICKED, reinterpret_cast<void *>(uintptr_t(i)));
    }
    auto *back = Theme::button(screen, tr(Str::BTN_BACK), 100, 34, Theme::MUTED);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_add_event_cb(back, [](lv_event_t *) { UiYatzy::showMenu(); }, LV_EVENT_CLICKED, nullptr);
    Theme::loadScreen(screen);
}
}  // namespace UiSudoku
