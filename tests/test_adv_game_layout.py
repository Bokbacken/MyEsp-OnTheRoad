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
controls = section("static void buildControls()", "// Game screen").rsplit("// -----", 1)[0]
choice = section("static void addChoice(", "static void talkShowChoices()")

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
static lv_obj_t *s_scr, *s_activeScr, *s_gameRoot, *s_canvas, *s_toast, *s_stageLabel, *s_progressLabel;
static lv_obj_t *s_joyKnob, *s_joyArrow[4];
static int s_joyLit = -1;
static Otr::JoyController s_joy;
static uint16_t *s_fb;
static constexpr int PORTRAIT_PX = 48;
static lv_obj_t *s_talkBox, *s_talkPanel, *s_talkPortrait, *s_talkName, *s_talkText, *s_talkRef;
/*TALK_CONSTANTS*/
namespace Otr { namespace Story { static int utf8Len(const char *) { return 0; } } }
static void choiceClicked(lv_event_t *) {}
/*CHOICE*/
static int stops, deletions;
static uint32_t nowMs() { return lv_tick_get(); }
static void stopPath() { ++stops; }
static void talkSkip() {}
static void screenDeleted(lv_event_t *) { ++deletions; }
/*JOY*/
static void configureJoy() {
    const auto j = Game::joyGeometry(s_layout.joystick);
    s_joy.configure(Otr::JoyConfig{}, j.radius, j.stopRadius);
}
/*PAD*/
/*TALK*/
// Hardware, sprite art and game actions are stubbed; control construction itself is production code.
enum class Str { ADV_BOMB, ADV_SWAP, ADV_HIT, BTN_CANCEL };
static const char *tr(Str) { return "Action"; }
static constexpr int HUD_PX = 20, C_COUNT = 4;
enum HudIcon { H_LIFE, H_SUIT, H_KEY, H_GEM, H_BOAT, H_FLASH, H_POGO, H_COUNT };
static lv_obj_t *s_count[C_COUNT], *s_muteBtn, *s_hitBtn, *s_swapBtn, *s_bar, *s_barLbl, *s_itemBox;
static lv_obj_t *s_item[H_COUNT], *s_flashCell, *s_pogoCell, *s_flashLvl, *s_pogoLvl, *s_bombCancel;
static uint32_t s_menuTapMs, s_hitTapMs, s_actionTapMs;
static int s_actSel, s_actShown;
static constexpr int ACT_HIT = 0;
static struct WorldStub { bool mine; bool mineLevel() const { return mine; } } world, *s_world = &world;
static void makeHudIcons() {}
static lv_obj_t *hudIcon(lv_obj_t *parent, HudIcon) {
    auto *icon = lv_obj_create(parent);
    lv_obj_remove_style_all(icon);
    lv_obj_set_size(icon, HUD_PX, HUD_PX);
    lv_obj_set_clickable(icon, false);
    return icon;
}
static void showMenu() {}
static void toggleSound() {}
static void muteIcon() {}
static void doSwap() {}
static void doHit() {}
static void cancelBomb() {}
/*CONTROLS*/
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
    const auto j = Game::joyGeometry(a);
    assert(a.w == s_layout.controls.h && a.h == s_layout.controls.h);
    assert(a.x == s_layout.joystick.x && a.y == s_layout.controls.y);
    auto *ring = lv_obj_get_child(area, 0);
    auto *stop = lv_obj_get_child(area, 2);
    const auto r = AdvUi::gameBounds(ring), s = AdvUi::gameBounds(stop);
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    assert(r.w == 2 * j.radius && r.h == 2 * j.radius && s.w == 2 * j.stopRadius && s.h == 2 * j.stopRadius);
    assert(s.x + s.w / 2 == cx && s.y + s.h / 2 == cy);
    auto d = Game::joyDelta(a, {cx, cy});
    assert(d.x == 0 && d.y == 0);
    const int before = stops;
    touch(indev, cx, cy, true);
    assert(stops == before + 1 && s_joy.held() && !s_joy.sample().moving);
    lv_obj_update_layout(s_scr);
    auto knob = AdvUi::gameBounds(s_joyKnob);
    assert(knob.w == 2 * j.knobRadius && knob.h == 2 * j.knobRadius);
    assert(knob.x + j.knobRadius == cx && knob.y + j.knobRadius == cy);
    touch(indev, cx + j.stopRadius, cy, true);
    assert(s_joy.sample().moving && s_joy.sample().zone == 1);
    const int boundary = j.stopRadius + (j.radius - j.stopRadius + 1) / 2;
    touch(indev, cx + boundary, cy, true);
    assert(s_joy.sample().zone == 2 && s_joy.sample().dir == Dir::RIGHT);
    const int corner = a.w - j.cx - 1;
    touch(indev, cx + corner, cy + corner, true);
    assert(s_joy.sample().zone == 2);  // Corners outside the drawn ring still accept input.
    lv_obj_update_layout(s_scr);
    knob = AdvUi::gameBounds(s_joyKnob);
    const int clamped = int((j.radius - j.knobRadius / 2) / sqrtf(2.f));
    assert(knob.x + j.knobRadius - cx == clamped && knob.y + j.knobRadius - cy == clamped);
    touch(indev, cx + corner, cy + corner, false);
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
            world.mine = visit != 0;
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
            assert(canvas.x == 0 && canvas.y == 0 && canvas.w == size.x && canvas.h == size.y - size.x / 2);
            const auto *drawBuffer = lv_canvas_get_draw_buf(s_canvas);
            assert(drawBuffer->header.stride == stride && drawBuffer->data == reinterpret_cast<uint8_t *>(s_fb));
            auto stage = AdvUi::gameBounds(s_stageLabel);
            assert(stage.x == 4 && stage.y == 4);
            lv_label_set_text(s_stageLabel, "A long map title must never cover the steps and par");
            lv_label_set_text(s_progressLabel, "Steps:2147483647  Par:2147483647");
            lv_obj_update_layout(s_scr);
            auto progress = AdvUi::gameBounds(s_progressLabel);
            assert(progress.x == 4 && progress.y == 24);
            assert(progress.x + progress.w <= size.x && progress.y + progress.h < canvas.h);
            assert(lv_obj_get_y(s_stageLabel) + lv_obj_get_height(s_stageLabel) <= progress.y);
            assert(lv_obj_get_parent(s_toast) == s_gameRoot);
            assert(lv_obj_get_y(s_toast) == canvas.h - 64);
            assert(lv_obj_get_width(s_toast) == canvas.w - 24);
            auto *controlPanel = lv_obj_get_parent(s_hitBtn);
            const auto controlBounds = AdvUi::gameBounds(controlPanel);
            assert(controlBounds.x == 0 && controlBounds.y == canvas.h);
            assert(controlBounds.w == size.x && controlBounds.h == size.x / 2);
            const auto px = [](int v) { return s_layout.controlPx(v); };
            const auto action = AdvUi::gameBounds(s_hitBtn);
            lv_area_t content;
            lv_obj_get_content_coords(controlPanel, &content);
            const int rightX = s_layout.joystick.x + s_layout.joystick.w;
            assert(action.x == rightX && action.w == size.x - rightX - px(4));
            assert(action.y == content.y1 + px(world.mine ? 8 : 28));
            assert(action.h == px(world.mine ? 56 : 36));
            auto *menu = lv_obj_get_child(controlPanel, 1);
            assert(lv_obj_get_x(menu) == px(4) && lv_obj_get_y(menu) == controlBounds.h - px(48));
            assert(lv_obj_get_width(menu) == px(36) && lv_obj_get_height(menu) == px(40));
            assert(lv_obj_get_x(s_muteBtn) == px(43) && lv_obj_get_height(s_muteBtn) == px(40));
            assert(lv_obj_get_y(s_bar) == px(74) && lv_obj_get_height(s_bar) == px(18));
            assert(lv_obj_get_x(s_bombCancel) == rightX && lv_obj_get_y(s_bombCancel) == px(106));
            assert(lv_obj_get_height(s_bombCancel) == px(36));
            assert(!s_swapBtn || lv_obj_get_height(s_swapBtn) == px(18));
            checkPad(indev);
            talkBuild();
            lv_obj_update_layout(s_scr);
            const auto panel = AdvUi::gameBounds(s_talkPanel);
            assert(lv_obj_get_parent(s_talkBox) == s_gameRoot);
            assert(panel.x == 0 && panel.y == canvas.h && panel.w == size.x && panel.h == size.x / 2);
            assert(lv_obj_get_y(s_toast) == 8);
            assert(lv_obj_has_flag(s_talkPanel, LV_OBJ_FLAG_CLICKABLE));
            addChoice(0, 0, "Answer", nullptr, true);
            addChoice(1, 1, "Locked answer", "Hint", false);
            lv_obj_update_layout(s_scr);
            for (int i = 0; i < 2; ++i) {
                auto *answer = lv_obj_get_child(s_talkPanel, i);
                assert(lv_obj_get_height(answer) == px(36));
                assert(lv_obj_get_x(answer) == px(6));
                assert(lv_obj_get_y(answer) == px(2 + i * 40));
                assert(lv_obj_get_width(answer) == size.x - 2 * px(6));
            }
            const int before = stops;
            touch(indev, size.x / 2, canvas.h + panel.h / 2, true);
            touch(indev, size.x / 2, canvas.h + panel.h / 2, false);
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
code = code.replace("/*CONTROLS*/", controls)
code = code.replace("/*CHOICE*/", choice)
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
