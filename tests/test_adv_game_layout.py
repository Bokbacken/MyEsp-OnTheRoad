#!/usr/bin/env python3
"""Exercise production Adventure root, joystick drawing/input and talk overlays with host LVGL.
Run: python3 tests/test_adv_game_layout.py [external build directory].
Requires managed_components/lvgl__lvgl, like the existing setup/dialog layout tests.
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
WORK = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(tempfile.mkdtemp(prefix="adv-game-"))
WORK.mkdir(parents=True, exist_ok=True)
source = (ROOT / "main/ui_adventure.cpp").read_text()


def section(start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


geometry = section("static Game::Layout s_layout", "// Sprites:").rsplit("// -----", 1)[0]
joy = section("static void joyShow(bool down, int dx, int dy)\n{", "static void configureJoy()")
pad = section("static lv_obj_t *circle(", "// Viewport: tap")
talk = section("static void talkBuild()", "static void endTalk()")
# Keep all construction/parenting statements from buildGame, stopping before world/timer work.
game = section("static void buildGame(bool loadFailed)", "    snapPlayer();") + "\n}\n"
talk_constants = section("static constexpr int32_t TALK_PAD", "static bool talking()")

code = r'''
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <vector>
#include "adv_game_ui.h"
#include "ui_metrics.h"
#include "my_theme.h"
#include "otr_joystick.h"

namespace Game = AdvUi::Game;
using Otr::Dir;
/*GEOMETRY*/
static lv_obj_t *s_scr, *s_activeScr, *s_gameRoot, *s_canvas, *s_toast, *s_stageLabel;
static lv_obj_t *s_joyKnob, *s_joyArrow[4];
static int s_joyLit = -1;
static Otr::JoyController s_joy;
static uint16_t *s_fb;
static constexpr int PORTRAIT_PX = 48;
static lv_obj_t *s_talkBox, *s_talkPanel, *s_talkPortrait, *s_talkName, *s_talkText, *s_talkRef;
/*TALK_CONSTANTS*/
static int stops, deletions;
static uint32_t nowMs() { return lv_tick_get(); }
static void stopPath() { ++stops; }
static void talkSkip() {}
static void screenDeleted(lv_event_t *) { ++deletions; }
/*JOY*/
static void configureJoy() { s_joy.configure(Otr::JoyConfig{}, JOY_R, JOY_STOP_R); }
/*PAD*/
/*TALK*/
// World/HUD rendering is outside this layout test; the real buildGame parenting is retained.
static void buildControls() { buildJoystick(s_gameRoot); }
/*GAME*/

static lv_point_t pointer;
static lv_indev_state_t state = LV_INDEV_STATE_RELEASED;
static void touch(lv_indev_t *indev, int x, int y, bool down) {
    pointer = {x, y};
    state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    lv_tick_inc(20);
    lv_indev_read(indev);
}
static void checkPad(lv_indev_t *indev) {
    auto *area = lv_obj_get_parent(s_joyKnob);
    auto a = AdvUi::gameBounds(area);
    assert(a.w == 160 && a.h == 160);
    assert(a.x == s_layout.joystick.x && a.y == s_layout.controls.y);
    auto *ring = lv_obj_get_child(area, 0);
    auto *stop = lv_obj_get_child(area, 2);
    const auto r = AdvUi::gameBounds(ring), s = AdvUi::gameBounds(stop);
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    assert(r.w == 148 && r.h == 148 && s.w == 46 && s.h == 46);
    assert(s.x + s.w / 2 == cx && s.y + s.h / 2 == cy);
    auto d = Game::joyDelta(a, {cx, cy});
    assert(d.x == 0 && d.y == 0);
    const int before = stops;
    touch(indev, cx, cy, true);
    assert(stops == before + 1 && s_joy.held() && !s_joy.sample().moving);
    lv_obj_update_layout(s_scr);
    auto knob = AdvUi::gameBounds(s_joyKnob);
    assert(knob.w == 40 && knob.h == 40 && knob.x + 20 == cx && knob.y + 20 == cy);
    touch(indev, cx + 23, cy, true);
    assert(s_joy.sample().moving && s_joy.sample().zone == 1);
    touch(indev, cx + 49, cy, true);
    assert(s_joy.sample().zone == 2 && s_joy.sample().dir == Dir::RIGHT);
    touch(indev, cx + 79, cy + 79, true);
    assert(s_joy.sample().zone == 2);  // Corners outside the drawn ring still accept input.
    lv_obj_update_layout(s_scr);
    knob = AdvUi::gameBounds(s_joyKnob);
    assert(knob.x + 20 - cx == 45 && knob.y + 20 - cy == 45);  // Existing knob clamp.
    touch(indev, cx + 79, cy + 79, false);
    assert(!s_joy.held() && lv_obj_has_flag(s_joyKnob, LV_OBJ_FLAG_HIDDEN));
    touch(indev, cx, cy - 60, true);
    assert(s_joy.held());
    // Let LVGL generate PRESS_LOST while its pointer is active.
    lv_obj_remove_flag(area, LV_OBJ_FLAG_PRESS_LOCK);
    touch(indev, a.x - 10, a.y - 10, true);
    assert(!s_joy.held() && lv_obj_has_flag(s_joyKnob, LV_OBJ_FLAG_HIDDEN));
    touch(indev, a.x - 10, a.y - 10, false);
}
int main() {
    lv_init();
    auto *display = lv_display_create(320, 480);
    static uint8_t pixels[320 * 20 * 2];
    lv_display_set_buffers(display, pixels, nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, [](lv_display_t *d, const lv_area_t *, uint8_t *) { lv_display_flush_ready(d); });
    auto *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, display);
    lv_indev_set_read_cb(indev, [](lv_indev_t *, lv_indev_data_t *data) { data->point = pointer; data->state = state; });
    for (auto size : {lv_point_t{320, 480}, lv_point_t{480, 800}, lv_point_t{481, 801}}) {
        lv_display_set_resolution(display, size.x, size.y);
        for (int visit = 0; visit < 2; ++visit) {
            s_layout = Game::layout(UiMetrics::screenW(), UiMetrics::screenH());
            const auto stride = lv_draw_buf_width_to_stride(s_layout.view.w, LV_COLOR_FORMAT_RGB565);
            s_fbStride = stride / sizeof(uint16_t);
            std::vector<uint16_t> frame(Game::frameBytes(stride, s_layout.view.h) / sizeof(uint16_t));
            s_fb = frame.data();
            buildGame(false);
            lv_screen_load(s_scr);
            lv_obj_update_layout(s_scr);
            auto root = AdvUi::gameBounds(s_gameRoot), canvas = AdvUi::gameBounds(s_canvas);
            assert(root.x == 0 && root.y == 0 && root.w == size.x && root.h == size.y);
            assert(canvas.x == 0 && canvas.y == 0 && canvas.w == size.x && canvas.h == size.y - 160);
            const auto *drawBuffer = lv_canvas_get_draw_buf(s_canvas);
            assert(drawBuffer->header.stride == stride && drawBuffer->data == reinterpret_cast<uint8_t *>(s_fb));
            auto stage = AdvUi::gameBounds(s_stageLabel);
            assert(stage.x == 4 && stage.y == 4);
            assert(lv_obj_get_parent(s_toast) == s_gameRoot);
            assert(lv_obj_get_y(s_toast) == canvas.h - 64);
            assert(lv_obj_get_width(s_toast) == canvas.w - 24);
            checkPad(indev);
            talkBuild();
            lv_obj_update_layout(s_scr);
            const auto panel = AdvUi::gameBounds(s_talkPanel);
            assert(lv_obj_get_parent(s_talkBox) == s_gameRoot);
            assert(panel.x == 0 && panel.y == canvas.h && panel.w == size.x && panel.h == 160);
            assert(lv_obj_get_y(s_toast) == 8);
            assert(lv_obj_has_flag(s_talkPanel, LV_OBJ_FLAG_CLICKABLE));
            const int before = stops;
            touch(indev, size.x / 2, canvas.h + 80, true);
            touch(indev, size.x / 2, canvas.h + 80, false);
            assert(stops == before && !s_joy.held());  // Panel covers the translated joystick.
            lv_obj_delete(s_talkBox);
            lv_obj_delete(s_talkPanel);
            // Actual bounds remain authoritative even after an additional parent translation.
            lv_obj_set_pos(s_gameRoot, 7, 9);
            lv_obj_update_layout(s_scr);
            float fx, fy;
            canvas = AdvUi::gameBounds(s_canvas);
            assert(Game::worldPoint(canvas, {canvas.x + 16, canvas.y + 32}, 37, 91, 16, fx, fy));
            assert(fx == 3.3125f && fy == 7.6875f);
            auto *blank = Theme::screen();
            lv_screen_load(blank);
            lv_obj_delete(s_scr);
            lv_obj_delete(blank);
        }
    }
    assert(deletions == 6);
    lv_indev_delete(indev);
    lv_display_delete(display);
    lv_deinit();
    puts("Adventure production viewport/pad/talk: 320x480, 480x800, 481x801; repeated entry/exit");
}
'''
# buildGame's pre-controls section references viewEvent; its transform is tested without LVGL.
code = code.replace("/*GEOMETRY*/", geometry).replace("/*TALK_CONSTANTS*/", talk_constants)
code = code.replace("/*JOY*/", joy).replace("/*PAD*/", pad).replace("/*TALK*/", talk)
code = code.replace("/*GAME*/", "static void viewEvent(lv_event_t *) {}\n" + game)
(WORK / "test.cpp").write_text(code)
(WORK / "sdkconfig.h").write_text("")
(WORK / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.22)
project(adv_game_layout LANGUAGES C CXX)
set(LV_CONF_SKIP ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_compile_definitions(LV_CONF_SKIP LV_COLOR_FORMAT_DEFAULT=LV_COLOR_FORMAT_RGB565 LV_DPI_DEF=130)
add_subdirectory("{ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(layout test.cpp "{ROOT}/components/my_ui/fonts/my_font_18.c" "{ROOT}/components/my_ui/fonts/my_font_14.c"
 "{ROOT}/main/ui_metrics.cpp" "{ROOT}/main/hw_profile.cpp")
target_include_directories(layout PRIVATE "{WORK}" "{ROOT}/components/my_ui/include" "{ROOT}/main"
 "{ROOT}/components/otr_engine/include")
target_link_libraries(layout PRIVATE lvgl m)
''')
subprocess.run(["cmake", "-S", str(WORK), "-B", str(WORK / "build"), "-G", "Ninja", "-DCMAKE_CXX_FLAGS="], check=True)
subprocess.run(["cmake", "--build", str(WORK / "build"), "--target", "layout", "-j", "2"], check=True)
subprocess.run([str(WORK / "build/layout")], check=True)
