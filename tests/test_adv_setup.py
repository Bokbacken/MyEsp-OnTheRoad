#!/usr/bin/env python3
"""Riktat LVGL-layouttest för det nya äventyrsvalet (steg 1: VÄLJ ÄVENTYR).

Bygger verkliga äventyrs-/spelarrader och scrollramar från firmware på 320x480 och 480x800.
Verifierar bredd, textradbrytning och att sista raden kan scrollas till i både sv och en.
Ingen ESP32 eller bildskärm behövs. Kör: python3 tests/test_adv_setup.py [extern byggkatalog]
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
WORK = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(tempfile.mkdtemp(prefix="adv-setup-"))
WORK.mkdir(parents=True, exist_ok=True)
source = (ROOT / "main/ui_adventure.cpp").read_text()

# Extract the real row builders so the test exercises the exact firmware button-building code.
addrow = source.split("static lv_obj_t *addWorldRow(lv_obj_t *list, const char *name, const char *desc, int idx)\n{", 1)[1]
addrow = addrow.split("    return b;", 1)[0]
playerrow = source.split("static void addPlayerRow(const char *name, uint16_t id)\n{", 1)[1]
playerrow = playerrow.split("\n}\n\nstatic void fillPlayerList", 1)[0]
assert "AdvUi::rowWidth()" in addrow, "addWorldRow must use the responsive row width"
assert "AdvUi::rowWidth()" in playerrow, "addPlayerRow must use the responsive row width"

code = r'''
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "lvgl.h"
#include "my_theme.h"
#include "lang.h"
#include "adv_setup_ui.h"

// Stub for the click handler referenced by the extracted addWorldRow body.
static void worldChosen(lv_event_t *) {}
static void playerChosen(lv_event_t *) {}
static lv_obj_t *s_playerList;
static uint16_t s_playerId;
static const char *curFolder() { return "test"; }
static bool hasSave(uint16_t, const char *) { return true; }

static lv_obj_t *makeWorldRow(lv_obj_t *list, const char *name, const char *desc, int idx) {
ADDROW
    return b;
}
static void makePlayerRow(const char *name, uint16_t id) {
 PLAYERROW
}

static void inside(lv_obj_t *obj, const lv_area_t &limit) {
    lv_area_t a; lv_obj_get_coords(obj, &a);
    if (!(a.x1 >= limit.x1 && a.y1 >= limit.y1 && a.x2 <= limit.x2 && a.y2 <= limit.y2)) {
        printf("UTANFÖR coords=%d,%d,%d,%d ram=%d,%d,%d,%d\n", int(a.x1), int(a.y1), int(a.x2), int(a.y2),
               int(limit.x1), int(limit.y1), int(limit.x2), int(limit.y2));
        fflush(stdout);
    }
    assert(a.x1 >= limit.x1 && a.y1 >= limit.y1 && a.x2 <= limit.x2 && a.y2 <= limit.y2);
}
static int labelTextWidth(lv_obj_t *label) {
    lv_point_t size;
    lv_text_get_size(&size, lv_label_get_text(label), lv_obj_get_style_text_font(label, LV_PART_MAIN),
                     lv_obj_get_style_text_letter_space(label, LV_PART_MAIN),
                     lv_obj_get_style_text_line_space(label, LV_PART_MAIN),
                     lv_obj_get_width(label), LV_TEXT_FLAG_NONE);
    return int(size.x);
}

struct Pair { Str name; Str desc; };
static const Pair WORLDS[] = {
    {Str::ADV_WORLD_TEST,    Str::ADV_WORLD_TEST_DESC},
    {Str::ADV_WORLD_MINES,   Str::ADV_WORLD_MINES_DESC},
    {Str::ADV_WORLD_VILLAGE, Str::ADV_WORLD_VILLAGE_DESC},
    {Str::ADV_WORLD_JOSEPH,  Str::ADV_WORLD_JOSEPH_DESC},
};

int main() {
    lv_init();
    auto *display = lv_display_create(320, 480);
    lv_display_set_dpi(display, 130);
    static uint8_t pixels[320 * 20 * 2];
    lv_display_set_buffers(display, pixels, nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, [](lv_display_t *d, const lv_area_t *, uint8_t *) { lv_display_flush_ready(d); });
    const int N = int(sizeof(WORLDS) / sizeof(WORLDS[0]));
    int checked = 0;
    for (const lv_point_t size : {lv_point_t{320, 480}, lv_point_t{480, 800}}) {
        lv_display_set_resolution(display, size.x, size.y);
        for (int language = 0; language < 2; ++language) {
            Lang::setLang(language == 0 ? "sv" : "en");
            auto *scr = lv_obj_create(nullptr);
            lv_obj_remove_style_all(scr);
            lv_obj_set_size(scr, size.x, size.y);
            lv_screen_load(scr);
            auto *list = AdvUi::scrollColumn(scr, 50, 6, 8);
            assert(AdvUi::rowWidth() == (size.x == 320 ? 296 : 444));
            assert(lv_obj_get_x(list) == AdvUi::scale(6));
            assert(lv_obj_get_y(list) == AdvUi::scale(50));
            assert(lv_obj_get_width(list) == size.x - 2 * AdvUi::scale(6));
            assert(lv_obj_get_height(list) == size.y - AdvUi::scale(56));
            lv_obj_t *rows[8];
            for (int i = 0; i < N; ++i)
                rows[i] = makeWorldRow(list, tr(WORLDS[i].name), tr(WORLDS[i].desc), i);
            const char *longName = language == 0
                ? "Detta är en ovanligt lång äventyrstitel med svenska tecken ÅÄÖ"
                : "This is an unusually long adventure title that must wrap without clipping";
            const char *longDesc = language == 0
                ? "En lång beskrivning som fortsätter med ytterligare information om världen, uppdraget och hur spelaren börjar sitt äventyr."
                : "A long description with additional information about this world, its objective, and how the player can begin the adventure.";
            for (int i = N; i < 8; ++i)
                rows[i] = makeWorldRow(list, longName, longDesc, i);
            lv_obj_update_layout(scr);
            for (int i = 0; i < 8; ++i) {
                lv_obj_t *b = rows[i];
                const int w = int(lv_obj_get_width(b)), h = int(lv_obj_get_height(b));
                assert(w == (size.x == 320 ? 296 : 444));
                assert(h >= (size.x == 320 ? 62 : 93));
                if (i) assert(lv_obj_get_y(rows[i - 1]) + lv_obj_get_height(rows[i - 1]) <= lv_obj_get_y(b));
                assert(lv_obj_get_child_count(b) >= 2);
                auto *nm = lv_obj_get_child(b, 0);
                auto *desc = lv_obj_get_child(b, 1);
                assert(labelTextWidth(nm) <= lv_obj_get_width(nm));
                assert(int(lv_obj_get_width(nm)) <= w);
                assert(int(lv_obj_get_width(desc)) <= w);
                lv_area_t buttonArea; lv_obj_get_coords(b, &buttonArea);
                inside(nm, buttonArea);
                if (lv_obj_get_height(desc)) inside(desc, buttonArea);
                ++checked;
            }
            assert(lv_obj_get_scroll_bottom(list) > 0);
            lv_obj_scroll_to_view(rows[7], LV_ANIM_OFF);
            lv_obj_update_layout(scr);
            lv_area_t listArea, lastArea;
            lv_obj_get_coords(list, &listArea);
            lv_obj_get_coords(rows[7], &lastArea);
            assert(lastArea.y1 >= listArea.y1 && lastArea.y2 <= listArea.y2);
            lv_obj_delete(list);

            s_playerList = AdvUi::scrollColumn(scr, 128, 64, 6);
            lv_obj_t *players[8];
            for (int i = 0; i < 8; ++i) {
                char name[96];
                snprintf(name, sizeof(name), "%s %d", longName, i);
                makePlayerRow(name, static_cast<uint16_t>(i + 1));
                players[i] = lv_obj_get_child(s_playerList, i);
            }
            auto *newPlayer = Theme::button(scr, tr(Str::PICK_NEW),
                size.x - 2 * AdvUi::scale(12), AdvUi::scale(48), Theme::GREEN);
            AdvUi::bottomButton(newPlayer, 48, 12, 8);
            lv_obj_update_layout(scr);
            assert(lv_obj_get_y(s_playerList) == AdvUi::scale(128));
            assert(lv_obj_get_height(s_playerList) == size.y - AdvUi::scale(192));
            assert(lv_obj_get_y(newPlayer) + lv_obj_get_height(newPlayer) == size.y - AdvUi::scale(8));
            for (auto *player : players) {
                assert(lv_obj_get_width(player) == (size.x == 320 ? 296 : 444));
                lv_area_t playerArea; lv_obj_get_coords(player, &playerArea);
                inside(lv_obj_get_child(player, 0), playerArea);
            }
            assert(lv_obj_get_scroll_bottom(s_playerList) > 0);
            lv_obj_scroll_to_view(players[7], LV_ANIM_OFF);
            lv_obj_update_layout(scr);
            lv_area_t playerListArea, lastPlayerArea;
            lv_obj_get_coords(s_playerList, &playerListArea);
            lv_obj_get_coords(players[7], &lastPlayerArea);
            assert(lastPlayerArea.y1 >= playerListArea.y1 && lastPlayerArea.y2 <= playerListArea.y2);

            printf("äventyr %dx%d språk=%d: världar och spelare scrollar till sista raden\n",
                   size.x, size.y, language);
            lv_obj_delete(scr);
        }
    }
    printf("Äventyrsval: %d produktionsrader kontrollerade (320x480 + 480x800, sv+en)\n", checked);
    lv_display_delete(display);
    lv_deinit();
}
'''.replace("ADDROW", addrow).replace("PLAYERROW", playerrow)

(WORK / "test.cpp").write_text(code)
(WORK / "sdkconfig.h").write_text("")
(WORK / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.22)
project(adv_setup_layout LANGUAGES C CXX)
set(LV_CONF_SKIP ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_compile_definitions(LV_CONF_SKIP LV_COLOR_FORMAT_DEFAULT=LV_COLOR_FORMAT_RGB565 LV_DPI_DEF=130)
add_subdirectory("{ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(layout test.cpp "{ROOT}/components/my_ui/fonts/my_font_18.c" "{ROOT}/components/my_ui/fonts/my_font_14.c"
 "{ROOT}/main/lang/lang.cpp" "{ROOT}/main/lang/lang_sv.cpp" "{ROOT}/main/lang/lang_en.cpp"
 "{ROOT}/main/ui_metrics.cpp" "{ROOT}/main/hw_profile.cpp")
target_include_directories(layout PRIVATE "{WORK}" "{ROOT}/components/my_ui/include" "{ROOT}/main/lang" "{ROOT}/main")
target_link_libraries(layout PRIVATE lvgl m)
''')
subprocess.run(["cmake", "-S", str(WORK), "-B", str(WORK / "build"), "-G", "Ninja", "-DCMAKE_CXX_FLAGS="], check=True)
subprocess.run(["cmake", "--build", str(WORK / "build"), "--target", "layout", "-j", "2"], check=True)
subprocess.run([str(WORK / "build/layout")], check=True)
