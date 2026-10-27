#!/usr/bin/env python3
"""Exercise the actual Yatzy selection layouts with host LVGL (no ESP32/display).
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
namespace UiNumpad {
lv_obj_t *open(const char *, const char *, DoneCb, void *, const char *) { return nullptr; }
}
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
add_executable(layout test.cpp "{ROOT}/main/ui_metrics.cpp" "{ROOT}/main/hw_profile.cpp"
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
