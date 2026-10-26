#!/usr/bin/env python3
"""Riktat LVGL-layouttest för det nya äventyrsvalet (steg 1: VÄLJ ÄVENTYR).

Bygger de verkliga breda, staplade äventyrsknapparna (samma addWorldRow-kod och font som i
firmware) på en 320x480 porträttskärm och verifierar att knapparna är:
  * staplade under varandra (ingen sida-vid-sida-överfyllnad),
  * ca 296 px breda,
  * tillräckligt höga (>= 56 px),
  * och att varje etikett (namn + beskrivning) ryms inom knappens bredd på både sv och en.
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

# Extract the real addWorldRow body (minus its trailing "return b;") so the test exercises the
# exact firmware button-building code, not a copy.
addrow = source.split("static lv_obj_t *addWorldRow(lv_obj_t *list, const char *name, const char *desc, int idx)\n{", 1)[1]
addrow = addrow.split("    return b;", 1)[0]
assert "Theme::button(list, name, 296" in addrow, "addWorldRow shape changed unexpectedly"

code = r'''
#include <cassert>
#include <cstdio>
#include "lvgl.h"
#include "my_theme.h"
#include "lang.h"

// Stub for the click handler referenced by the extracted addWorldRow body.
static void worldChosen(lv_event_t *) {}

static lv_obj_t *makeWorldRow(lv_obj_t *list, const char *name, const char *desc, int idx) {
ADDROW
    return b;
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
static void tree(lv_obj_t *obj, const lv_area_t &limit) {
    inside(obj, limit);
    lv_area_t area; lv_obj_get_coords(obj, &area);
    for (unsigned i = 0; i < lv_obj_get_child_count(obj); ++i) tree(lv_obj_get_child(obj, i), area);
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
    const lv_area_t screen = {0, 0, 319, 479};
    const int N = int(sizeof(WORLDS) / sizeof(WORLDS[0]));
    int checked = 0;
    for (int language = 0; language < 2; ++language) {
        Lang::setLang(language == 0 ? "sv" : "en");
        auto *scr = lv_obj_create(nullptr);
        lv_obj_remove_style_all(scr);
        lv_obj_set_size(scr, 320, 480);
        lv_screen_load(scr);
        // Same scrollable column container as buildWorldPick().
        auto *list = lv_obj_create(scr);
        lv_obj_remove_style_all(list);
        lv_obj_set_pos(list, 6, 50);
        lv_obj_set_size(list, 308, 424);
        lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(list, 8, 0);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);
        lv_obj_t *rows[8];
        for (int i = 0; i < N; ++i) rows[i] = makeWorldRow(list, tr(WORLDS[i].name), tr(WORLDS[i].desc), i);
        lv_obj_update_layout(scr);
        for (int i = 0; i < N; ++i) {
            lv_obj_t *b = rows[i];
            const int w = int(lv_obj_get_width(b)), h = int(lv_obj_get_height(b));
            // Wide stacked button near 296 px, tall enough to read.
            assert(w >= 290 && w <= 308);
            assert(h >= 56);
            // Stacked: each button starts at or below the previous one's bottom edge.
            if (i) assert(lv_obj_get_y(rows[i - 1]) + lv_obj_get_height(rows[i - 1]) <= lv_obj_get_y(b));
            // Both labels (name = child 0, description = child 1) stay within the button bounds.
            assert(lv_obj_get_child_count(b) >= 2);
            auto *nm = lv_obj_get_child(b, 0);
            auto *desc = lv_obj_get_child(b, 1);
            const int cw = int(lv_obj_get_content_width(b));
            assert(labelTextWidth(nm) <= cw);
            assert(int(lv_obj_get_width(nm)) <= w);
            assert(int(lv_obj_get_width(desc)) <= w);
            printf("språk=%d %-14s knapp=%dx%d namn_text=%d beskr_bredd=%d\n", language, tr(WORLDS[i].name),
                   w, h, labelTextWidth(nm), int(lv_obj_get_width(desc)));
            ++checked;
        }
        // Every pixel of every row sits inside the physical portrait screen.
        tree(list, screen);
        lv_obj_delete(scr);
    }
    printf("Äventyrsval: %d breda staplade knappar kontrollerade (sv+en), etiketter ryms\n", checked);
    lv_display_delete(display);
    lv_deinit();
}
'''.replace("ADDROW", addrow)

(WORK / "test.cpp").write_text(code)
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
 "{ROOT}/main/lang/lang.cpp" "{ROOT}/main/lang/lang_sv.cpp" "{ROOT}/main/lang/lang_en.cpp")
target_include_directories(layout PRIVATE "{ROOT}/components/my_ui/include" "{ROOT}/main/lang" "{ROOT}/main")
target_link_libraries(layout PRIVATE lvgl m)
''')
subprocess.run(["cmake", "-S", str(WORK), "-B", str(WORK / "build"), "-G", "Ninja", "-DCMAKE_CXX_FLAGS="], check=True)
subprocess.run(["cmake", "--build", str(WORK / "build"), "--target", "layout", "-j", "2"], check=True)
subprocess.run([str(WORK / "build/layout")], check=True)
