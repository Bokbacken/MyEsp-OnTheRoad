#!/usr/bin/env python3
"""Exercise Yatzy selection/modal layouts and the real numpad with host LVGL (no ESP32/display).
Run: python3 tests/test_yatzy_layout.py [external build directory]
Requires the project's managed_components/lvgl__lvgl dependency, like test_mine_dialog.py.
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
WORK = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(tempfile.mkdtemp(prefix="yatzy-layout-"))
WORK.mkdir(parents=True, exist_ok=True)
source = (ROOT / "main/ui_yatzy.cpp").read_text()


def section(start, end):
    return source[source.index(start):source.index(end)]


code = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <unistd.h>
#include "lvgl.h"
#include "my_theme.h"
#include "ui_metrics.h"
#include "hw_profile.h"
#include "app_config.h"
#include "app_players.h"
#include "game_base.h"
#include "game_pig.h"
#include "game_yatzy.h"
#include "lang_game.h"
#include "my_charwheel.h"
#include "ui_numpad.h"
#include "esp_heap_caps.h"
static std::string storageBase;
namespace MyFat {
bool mounted() { return true; }
void path(char *out, size_t n, const char *rel) { snprintf(out, n, "%s%s", storageBase.c_str(), rel); }
}
namespace Config { AppConfig &get() { static AppConfig cfg{}; return cfg; } }
namespace CharWheel {
lv_obj_t *create(lv_obj_t *, int32_t, int32_t, int32_t w, int32_t h,
                 const char *, const char *, size_t, bool, Callback, void *) {
    assert(w == UiMetrics::screenW() && h == UiMetrics::screenH());
    return nullptr;
}
void close(lv_obj_t *) {}
void setHint(lv_obj_t *, const char *) {}
}
static YatzyVariant s_variant = YatzyVariant::STANDARD;
static YatzyMode s_mode = YatzyMode::NORMAL;
static char s_names[YATZY_MAX_PLAYERS][YATZY_NAME_CAP];
static int s_nameCount;
static GameType s_gameType = GameType::YATZY;
static PigVariant s_pigVariant = PigVariant::ONE_DIE;
static bool s_midi;
static int starts, menus;
static void showPlayers();
static void showMenu() { ++menus; }
static void startNewGame() { ++starts; }
@CHOICES@
@LAYOUTS@
@PLAYERS@
@PICKER@
static int quits, localClicks, clearCmds;
static void quitEvent(lv_event_t *e) {
    ++quits; lv_msgbox_close((lv_obj_t *)lv_event_get_user_data(e));
}
static void webLocalEvent(lv_event_t *) { ++localClicks; }
static void webTimer(lv_timer_t *t) { lv_timer_delete(t); }
static bool remoteTurn, connected;
static bool webTurn() { return remoteTurn; }
static int curPlayer() { return 0; }
static const char *curName() { return "Long player name with Swedish letters Åäö"; }
namespace MyWifi { const char *ipStr() { return "192.168.123.123"; } }
namespace GameLive {
int connections(int) { return connected; }
void clearCmds() { ++::clearCmds; }
}
static constexpr int32_t FELT_Y = 48;
static constexpr uint32_t WEB_TICK_MS = 50;
static uint32_t s_gen;
@OVERLAY_STATE@
@MODALS@
static lv_obj_t *child(lv_obj_t *o, int i) { return lv_obj_get_child(o, i); }
static void settle() {
    lv_tick_inc(200);
    lv_timer_handler();
    lv_obj_update_layout(lv_screen_active());
}
static void inside(lv_obj_t *obj, lv_obj_t *parent) {
    lv_area_t a, p; lv_obj_get_coords(obj, &a); lv_obj_get_coords(parent, &p);
    if (!(a.x1 >= p.x1 && a.y1 >= p.y1 && a.x2 <= p.x2 && a.y2 <= p.y2))
        fprintf(stderr, "outside: %d,%d..%d,%d parent %d,%d..%d,%d\n",
                int(a.x1), int(a.y1), int(a.x2), int(a.y2), int(p.x1), int(p.y1), int(p.x2), int(p.y2));
    assert(a.x1 >= p.x1 && a.y1 >= p.y1 && a.x2 <= p.x2 && a.y2 <= p.y2);
}
static void noOverlap(lv_obj_t *box, bool horizontal, bool contained = true) {
    for (unsigned i = 0; i < lv_obj_get_child_count(box); ++i) {
        auto *o = child(box, i);
        if (contained) inside(o, box);
        if (i) {
            auto *prev = child(box, i - 1);
            assert(horizontal ? lv_obj_get_x(prev) + lv_obj_get_width(prev) <= lv_obj_get_x(o)
                              : lv_obj_get_y(prev) + lv_obj_get_height(prev) <= lv_obj_get_y(o));
        }
    }
}
static lv_obj_t *body() {
    auto *scr = lv_screen_active();
    auto *box = child(scr, -1);
    inside(box, scr); noOverlap(box, false); noOverlap(child(scr, 0), true);
    return box;
}
static void click(lv_obj_t *o) { lv_obj_send_event(o, LV_EVENT_CLICKED, nullptr); settle(); }
static lv_obj_t *messageBox() { return child(child(lv_layer_top(), -1), 0); }
static void checkDialog(lv_obj_t *m) {
    inside(m, lv_screen_active());
    inside(lv_msgbox_get_header(m), m);
    inside(lv_msgbox_get_title(m), lv_msgbox_get_header(m));
    inside(lv_msgbox_get_content(m), m);
    inside(lv_msgbox_get_footer(m), m);
    noOverlap(lv_msgbox_get_footer(m), true);
    assert(lv_obj_get_width(m) == (UiMetrics::screenW() == 320 ? 280 : 420));
}
static void key(lv_obj_t *matrix, int id) {
    lv_buttonmatrix_set_selected_button(matrix, id);
    lv_obj_send_event(matrix, LV_EVENT_VALUE_CHANGED, nullptr); settle();
}
static void swipeText(lv_obj_t *area) {
    auto *input = lv_indev_create();
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_data_t sample{};
    lv_indev_set_user_data(input, &sample);
    lv_indev_set_read_cb(input, [](lv_indev_t *i, lv_indev_data_t *data) {
        *data = *(lv_indev_data_t *)lv_indev_get_user_data(i);
    });
    lv_area_t bounds; lv_obj_get_coords(area, &bounds);
    sample.point = {bounds.x1 + 8, bounds.y2 - 8};
    sample.state = LV_INDEV_STATE_PRESSED;
    lv_indev_read(input);
    for (int step = 0; step < 5; ++step) {
        sample.point.y -= 8; lv_tick_inc(20); lv_indev_read(input);
    }
    assert(lv_indev_get_scroll_obj(input) == area);
    sample.state = LV_INDEV_STATE_RELEASED; lv_indev_read(input); settle();
    assert(lv_obj_get_scroll_y(area) > 0);
    lv_anim_delete(input, nullptr);
    lv_indev_delete(input);
}
static void checkModals() {
    // Opening from the picker/web-seat flow must also produce a contained modal.
    while (lv_obj_get_child_count(lv_layer_top())) lv_obj_delete(child(lv_layer_top(), 0));
    infoBox(tr(Str::PIN_WEB_TITLE), tr(Str::PIN_GUEST_NO_WEB)); settle();
    checkDialog(messageBox());
    click(child(lv_msgbox_get_footer(messageBox()), 0));
    assert(lv_obj_get_child_count(lv_layer_top()) == 0);
    std::string longText;
    for (int i = 0; i < 80; ++i) longText += "Long information Åäö. ";
    infoBox(longText.c_str(), longText.c_str()); settle();
    auto *m = messageBox(); checkDialog(m);
    assert(lv_obj_get_scroll_bottom(lv_msgbox_get_content(m)) > 0);
    lv_obj_scroll_to_y(lv_msgbox_get_content(m), LV_COORD_MAX, LV_ANIM_OFF); settle();
    checkDialog(m); click(child(lv_msgbox_get_footer(m), 0));
    onMenu(nullptr); settle(); checkDialog(messageBox());
    click(child(lv_msgbox_get_footer(messageBox()), 1)); assert(quits == 0);
    onMenu(nullptr); settle(); click(child(lv_msgbox_get_footer(messageBox()), 0));
    assert(quits == 1); quits = 0;

    auto *scr = lv_screen_active();
    int oldClears = clearCmds;
    buildWebOverlay(scr); settle();
    assert(clearCmds == oldClears + 1);
    assert(lv_obj_has_flag(s_webBlock, LV_OBJ_FLAG_HIDDEN));
    for (bool conn : {false, true}) {
        remoteTurn = true; connected = conn; updateWebOverlay(); settle();
        inside(s_webBlock, scr); inside(s_webPanel, scr);
        auto *textArea = lv_obj_get_parent(s_webText);
        inside(textArea, s_webPanel); inside(s_webLocal, s_webPanel);
        assert(lv_obj_get_y(textArea) + lv_obj_get_height(textArea) <= lv_obj_get_y(s_webLocal));
        assert(lv_obj_has_flag(s_webLocal, LV_OBJ_FLAG_HIDDEN) == conn);
        assert(lv_obj_has_flag(s_webBlock, LV_OBJ_FLAG_CLICKABLE));
        assert(lv_obj_get_width(s_webBlock) == UiMetrics::screenW());
    }
    connected = false; updateWebOverlay(); settle();
    int oldClicks = localClicks; click(s_webLocal); assert(localClicks == oldClicks + 1);
    lv_label_set_text(s_webText, longText.c_str()); settle();
    auto *area = lv_obj_get_parent(s_webText);
    assert(lv_obj_get_scroll_bottom(area) > 0);
    swipeText(area);
    lv_obj_scroll_to_y(area, LV_COORD_MAX, LV_ANIM_OFF); settle(); inside(area, s_webPanel);
    remoteTurn = false; updateWebOverlay(); settle();
    assert(lv_obj_has_flag(s_webBlock, LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(s_webPanel, LV_OBJ_FLAG_HIDDEN));
    lv_obj_delete(s_webBlock); lv_obj_delete(s_webPanel);

    struct Result { int calls = 0; bool ok = false, accept = false; char pin[PLAYER_PIN_CAP] = {}; } result;
    auto done = [](const char *pin, bool ok, void *u) {
        auto *r = (Result *)u; ++r->calls; r->ok = ok;
        snprintf(r->pin, sizeof(r->pin), "%s", pin);
        return !ok || r->accept; // Rejection keeps the pad open for retry.
    };
    for (const char *info : {tr(Str::PIN_INFO_NEW), tr(Str::PIN_INFO_WEB), "", longText.c_str()}) {
        auto *pad = UiNumpad::open(longText.c_str(), info, done, &result);
        settle(); inside(pad, scr);
        lv_obj_t *matrix = nullptr;
        for (unsigned i = 0; i < lv_obj_get_child_count(pad); ++i) {
            auto *o = child(pad, i); inside(o, pad);
            if (lv_obj_check_type(o, &lv_buttonmatrix_class)) matrix = o;
        }
        assert(matrix && lv_obj_get_height(matrix) >= 4 * 36 + 6 * UiMetrics::marginSmall());
        int firstCell = info[0] ? 2 : 1;
        auto *cell = child(pad, firstCell);
        assert(lv_obj_get_x(cell) ==
               (UiMetrics::screenW() - PLAYER_PIN_LEN * UiMetrics::buttonWMin() -
                (PLAYER_PIN_LEN - 1) * 2 * UiMetrics::marginSmall()) / 2);
        for (int i = 0; i < PLAYER_PIN_LEN; ++i) {
            auto *next = child(pad, firstCell + i);
            assert(lv_obj_get_y(next) == lv_obj_get_y(cell));
            if (i) assert(lv_obj_get_x(child(pad, firstCell + i - 1)) + UiMetrics::buttonWMin() <= lv_obj_get_x(next));
        }
        auto *cancel = child(pad, -2), *ok = child(pad, -1);
        assert(lv_obj_get_y(matrix) + lv_obj_get_height(matrix) <= lv_obj_get_y(ok));
        assert(lv_obj_has_state(ok, LV_STATE_DISABLED));
        int calls = result.calls;
        click(ok); assert(result.calls == calls);
        for (int id : {0, 1, 2, 3, 4}) key(matrix, id);
        click(ok); assert(result.ok && strcmp(result.pin, "1234") == 0);
        key(matrix, 9); assert(lv_obj_has_state(ok, LV_STATE_DISABLED)); // Backspace.
        key(matrix, 10); click(ok); assert(strcmp(result.pin, "1230") == 0);
        UiNumpad::setHint(pad, longText.c_str(), true); settle();
        assert(lv_obj_has_state(ok, LV_STATE_DISABLED));
        for (int id : {4, 5, 6, 7}) key(matrix, id);
        click(ok); assert(strcmp(result.pin, "5678") == 0);
        UiNumpad::setHint(pad, "Try again", false);
        assert(!lv_obj_has_state(ok, LV_STATE_DISABLED));
        click(cancel); assert(!result.ok && !result.pin[0]);
        assert(lv_obj_get_child_count(lv_layer_top()) == 0);
    }
    result.accept = true;
    auto *pad = UiNumpad::open(tr(Str::PIN_WEB_TITLE), nullptr, done, &result);
    settle();
    for (int id : {0, 1, 2, 3}) key(child(pad, -3), id);
    click(child(pad, -1));
    assert(result.ok && strcmp(result.pin, "1234") == 0);
    assert(lv_obj_get_child_count(lv_layer_top()) == 0);
}
int main() {
    lv_init();
    auto *display = lv_display_create(320, 480);
    lv_display_set_dpi(display, 130);
    static uint8_t pixels[800 * 20 * 2];
    lv_display_set_buffers(display, pixels, nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, [](lv_display_t *d, const lv_area_t *, uint8_t *) { lv_display_flush_ready(d); });
    char dir[] = "/tmp/yatzy-players-XXXXXX"; storageBase = mkdtemp(dir);
    assert(Players::begin());
    // No registry: preserve the two default guests.
    showPlayers(); settle();
    assert(s_nameCount == 2 && !s_pids[0] && !s_pids[1]);
    showPicker(-1); settle();
    assert(lv_table_get_row_count(child(body(), 1)) == 1 && s_pickIds[0] == 0);
    assert(!lv_obj_has_state(child(child(body(), 0), 0), LV_STATE_DISABLED));
    click(child(child(body(), 0), 0)); // New saved player uses live display size.
    for (int i = 0; i < PLAYER_MAX; ++i) {
        char name[PLAYER_NAME_CAP]; snprintf(name, sizeof(name), "Player %03d long name", i);
        assert(Players::add(name) > 0);
    }
    for (int language = 0; language < 2; ++language) {
        Lang::setLang(language ? "en" : "sv");
        for (auto size : {lv_point_t{320,480}, lv_point_t{480,800}, lv_point_t{800,480}}) {
            lv_display_set_resolution(display, size.x, size.y);
            for (auto game : {GameType::YATZY, GameType::PIG, GameType::FARKLE, GameType::SCC}) {
                s_gameType = game; showVariants(); settle();
                auto *box = body(); auto *list = child(box, 0);
                noOverlap(list, false, false); noOverlap(child(box, 1), true);
                if (size.x == 320 && size.y == 480) {
                    assert(lv_obj_get_y(box) == 74 && lv_obj_get_y(child(box, 1)) + lv_obj_get_y(box) == 432);
                    assert(lv_obj_get_scroll_bottom(list) <= 0);
                }
                unsigned count = game == GameType::YATZY ? 5 : game == GameType::PIG ? 2 : 1;
                assert(lv_obj_get_child_count(list) == count);
                for (unsigned i = 0; i < count; ++i) {
                    lv_obj_scroll_to_view(child(list, i), LV_ANIM_OFF); settle();
                    inside(child(list, i), list);
                }
                click(child(list, count - 1));
                assert(s_gameType == game);
                if (game == GameType::YATZY) assert(s_variant == YatzyVariant::MAXI && s_mode == YatzyMode::ORDERED);
                if (game == GameType::PIG) assert(s_pigVariant == PigVariant::TWO_DICE);
                body();
            }
            for (int count : {0, 1, YATZY_MAX_PLAYERS}) {
                s_nameCount = count;
                for (int i = 0; i < count; ++i) {
                    snprintf(s_names[i], YATZY_NAME_CAP, "Long player name %d", i);
                    s_pids[i] = i % 2 ? i + 1 : 0; s_web[i] = false;
                }
                showPlayers(); settle(); auto *box = body();
                noOverlap(child(box, -1), true);
                if (size.x == 320 && size.y == 480) {
                    assert(lv_obj_get_y(s_addBtn) + lv_obj_get_y(box) == 384);
                    assert(lv_obj_get_y(child(box, -1)) + lv_obj_get_y(box) == 432);
                }
                assert(lv_obj_has_state(s_startBtn, LV_STATE_DISABLED) == (count == 0));
                assert(lv_obj_has_state(s_addBtn, LV_STATE_DISABLED) == (count == YATZY_MAX_PLAYERS));
                for (int i = 0; i < count; ++i) {
                    auto *line = child(s_playerList, i);
                    lv_obj_scroll_to_view(line, LV_ANIM_OFF); settle();
                    inside(line, s_playerList); noOverlap(line, true); noOverlap(child(line, 0), true);
                }
                if (count) {
                    int oldStarts = starts; click(s_startBtn); assert(starts == oldStarts + 1);
                    click(child(child(s_playerList, 0), 2)); assert(s_nameCount == count - 1);
                }
            }
            s_nameCount = 1; s_pids[0] = 1;
            showPlayers(); settle();
            click(child(child(s_playerList, 0), 1)); assert(s_web[0] == false); // No PIN: opens PIN prompt.
            assert(Players::setPin(1, "1234") == 0);
            click(child(child(s_playerList, 0), 1)); assert(s_web[0]);
            click(child(child(s_playerList, 0), 1)); assert(!s_web[0]);
            Players::setPin(1, "");
            click(child(child(s_playerList, 0), 0)); assert(s_pickSlot == 0);
            auto *box = body(); noOverlap(child(box, 0), true); noOverlap(child(box, -1), true);
            auto *table = child(box, 1);
            assert(lv_table_get_row_count(table) == PLAYER_MAX);
            assert(lv_table_get_column_width(table, 0) == lv_obj_get_content_width(table));
            assert(lv_obj_get_scroll_dir(table) == LV_DIR_VER);
            assert(!lv_obj_has_flag(table, LV_OBJ_FLAG_SCROLL_ELASTIC));
            assert(lv_obj_has_state(child(child(box, 0), 0), LV_STATE_DISABLED));
            lv_obj_scroll_to_y(table, LV_COORD_MAX, LV_ANIM_OFF); settle(); body();
            lv_table_set_selected_cell(table, 0, 0); settle();
            assert(s_pids[0] == 1 && s_nameCount == 1);
            showPicker(-1); settle(); assert(lv_table_get_row_count(child(body(), 1)) == PLAYER_MAX - 1);
            snprintf(s_pickFilter, sizeof(s_pickFilter), "no match");
            showPicker(-1); settle();
            assert(lv_table_get_row_count(child(body(), 1)) == 1 && s_pickIds[0] == 0);
            click(child(child(body(), 0), 1)); assert(!s_pickFilter[0]);
            click(child(child(body(), -1), 1)); // Guest name wheel uses live display size.
            click(child(child(body(), -1), 0)); // Back to players.
            assert(!s_pickFilter[0]);
            click(s_addBtn);
            table = child(body(), 1);
            lv_table_set_selected_cell(table, 0, 0); settle();
            assert(s_nameCount == 2 && s_pids[1] == 2);
            checkModals();
            printf("selection layout/interactions OK: %dx%d language=%d\n", int(size.x), int(size.y), language);
        }
    }
    for (auto path : {"/players.bin", "/players.bin.bak"}) remove((storageBase + path).c_str());
    rmdir(storageBase.c_str());
    free(s_pickDb);
    lv_display_delete(display); lv_deinit();
}
'''
code = code.replace("@CHOICES@", section("struct VariantChoice {", "static void showVariants();"))
code = code.replace("@LAYOUTS@", section("// Selection screens keep", "// ========================================================================================\n// Players"))
code = code.replace("@PLAYERS@", section("static uint16_t s_pids", "static void startYatzy()"))
code = code.replace("@PICKER@", section("static PlayerDb *s_pickDb", "// ========================================================================================\n// Game screen"))
code = code.replace("@OVERLAY_STATE@", section("static lv_obj_t *s_webBlock", "static void updateWebOverlay()\n{"))
code = code.replace("@MODALS@", section("static void onMenu(lv_event_t *)\n{", "// --- Web play") +
                    section("static void updateWebOverlay()\n{", "static void webWriteTimer") +
                    section("static void buildWebOverlay(lv_obj_t *scr)\n{", "static void showGame()\n{"))
(WORK / "test.cpp").write_text(code)
(WORK / "sdkconfig.h").write_text("")
(WORK / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.22)
project(yatzy_selection_layout LANGUAGES C CXX)
set(LV_CONF_SKIP ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_compile_definitions(LV_CONF_SKIP LV_COLOR_FORMAT_DEFAULT=LV_COLOR_FORMAT_RGB565 LV_DPI_DEF=130)
add_subdirectory("{ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(layout test.cpp "{ROOT}/main/ui_metrics.cpp" "{ROOT}/main/ui_numpad.cpp" "{ROOT}/main/hw_profile.cpp"
 "{ROOT}/components/my_ui/fonts/my_font_14.c" "{ROOT}/components/my_ui/fonts/my_font_18.c"
 "{ROOT}/components/my_ui/fonts/my_font_24.c" "{ROOT}/main/app_players.cpp"
 "{ROOT}/main/lang/lang.cpp" "{ROOT}/main/lang/lang_sv.cpp" "{ROOT}/main/lang/lang_en.cpp")
target_include_directories(layout PRIVATE "{WORK}" "{ROOT}/tests/player_stubs"
 "{ROOT}/components/my_ui/include" "{ROOT}/components/my_wifi/include" "{ROOT}/main/lang" "{ROOT}/main")
target_link_libraries(layout PRIVATE lvgl m)
''')
subprocess.run(["cmake", "-S", str(WORK), "-B", str(WORK / "build"), "-G", "Ninja", "-DCMAKE_CXX_FLAGS="], check=True)
subprocess.run(["cmake", "--build", str(WORK / "build"), "--target", "layout", "-j", "2"], check=True)
subprocess.run([str(WORK / "build/layout")], check=True)
