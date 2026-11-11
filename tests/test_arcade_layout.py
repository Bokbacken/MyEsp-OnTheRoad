#!/usr/bin/env python3
"""Exercise the production Arcade UI, pointer delivery and screen cleanup with host LVGL."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
WORK = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(tempfile.mkdtemp(prefix="arcade-layout-"))
WORK.mkdir(parents=True, exist_ok=True)
(WORK / "sdkconfig.h").write_text("")
(WORK / "esp_log.h").write_text(
    '#pragma once\n#include <cstdio>\n#define ESP_LOGE(tag, ...) std::fprintf(stderr, __VA_ARGS__)\n'
)
code = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "ui_arcade.cpp"

static bool rotateOK = true;
static bool flipLandscape = false;
namespace Display {
bool landscape() { return lv_display_get_rotation(lv_display_get_default()) != LV_DISPLAY_ROTATION_0; }
bool setLandscape(bool on, bool flip) {
    if (!rotateOK) return false;
    lv_display_set_rotation(lv_display_get_default(), on
        ? (flip != flipLandscape ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90)
        : LV_DISPLAY_ROTATION_0);
    return true;
}
}
namespace UiScreenPower {
bool blocked = false;
bool inputBlocked() { return blocked; }
}
namespace Touch {
bool enabled = false, polling = true;
Contacts sample;
void setMultiTouch(bool on) { enabled = on; sample = {}; }
Contacts contacts() { return sample; }
}
static int menus = 0;
namespace UiYatzy {
void showMenu() { ++menus; Theme::loadScreen(Theme::screen()); }
}
using namespace UiArcade;

static void advance(uint32_t ms) {
    while (ms) {
        const uint32_t step = ms > 10 ? 10 : ms;
        lv_tick_inc(step);
        if (Touch::enabled && Touch::polling) Touch::sample.sampledAt = lv_tick_get();
        lv_timer_handler();
        ms -= step;
    }
}
static void inside(lv_obj_t *obj, lv_obj_t *parent) {
    lv_area_t a, b;
    lv_obj_get_coords(obj, &a); lv_obj_get_coords(parent, &b);
    assert(a.x1 >= b.x1 && a.y1 >= b.y1 && a.x2 <= b.x2 && a.y2 <= b.y2);
}
static lv_obj_t *button(lv_obj_t *obj, const char *text) {
    if (lv_obj_check_type(obj, &lv_button_class)) {
        auto *label = lv_obj_get_child(obj, 0);
        if (label && lv_obj_check_type(label, &lv_label_class) &&
            !strcmp(lv_label_get_text(label), text)) return obj;
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i)
        if (auto *found = button(lv_obj_get_child(obj, i), text)) return found;
    return nullptr;
}
int main() {
    lv_init();
    auto *display = lv_display_create(320, 480);
    static uint16_t pixels[800 * 16];
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, pixels, nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, [](lv_display_t *d, const lv_area_t *, uint8_t *) {
        lv_display_flush_ready(d);
    });
    auto *input = lv_indev_create();
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_data_t sample{};
    lv_indev_set_user_data(input, &sample);
    lv_indev_set_read_cb(input, [](lv_indev_t *i, lv_indev_data_t *data) {
        *data = *static_cast<lv_indev_data_t *>(lv_indev_get_user_data(i));
    });
    for (bool p4 : {false, true}) {
        for (bool flipped : {false, true}) {
        flipLandscape = flipped;
        for (auto zoom : {MyFonts::ScreenZoom::SMALL, MyFonts::ScreenZoom::NORMAL,
                          MyFonts::ScreenZoom::LARGE})
        for (auto textSize : {MyFonts::TextSize::SMALL, MyFonts::TextSize::NORMAL,
                              MyFonts::TextSize::LARGE, MyFonts::TextSize::XLARGE})
        for (const char *language : {"sv", "en"}) {
            Lang::setLang(language);
            Display::setLandscape(false, false);
            lv_display_set_resolution(display, p4 ? 480 : 320, p4 ? 800 : 480);
            MyFonts::apply(textSize, p4 ? 480 : 320, p4 ? 117 : 154, zoom);
            showMenu(); advance(250);
            showTouchTest(); advance(250);
            assert(Display::landscape() && s_timer && s_field);
            assert(Touch::enabled && s_controlsArmed);
            assert(s_fieldW == (p4 ? 800 : 480) - 2 *
                   (UiMetrics::marginNormal() + UiMetrics::buttonWMin() + UiMetrics::marginSmall()));
            assert(s_fieldH > 100);
            inside(s_field, lv_screen_active());
            inside(s_ship, s_field);
            inside(s_status, s_field);
            inside(s_heightControl, lv_screen_active());
            inside(s_speedControl, lv_screen_active());
            auto *back = button(lv_screen_active(), tr(Str::BTN_BACK));
            auto *reset = button(lv_screen_active(), tr(Str::ARCADE_RESET));
            assert(back && reset);
            inside(back, lv_screen_active()); inside(reset, lv_screen_active());

            lv_area_t field, left, right;
            lv_obj_get_coords(s_field, &field);
            lv_obj_get_coords(s_heightControl, &left);
            lv_obj_get_coords(s_speedControl, &right);
            assert(left.x2 < field.x1 && right.x1 > field.x2);
            auto touch = [&](std::initializer_list<lv_point_t> points) {
                Touch::sample = {};
                Touch::sample.sampledAt = lv_tick_get();
                for (auto point : points) {
                    if (flipped) point = {lv_display_get_vertical_resolution(display) - 1 - point.y, point.x};
                    else point = {point.y, lv_display_get_horizontal_resolution(display) - 1 - point.x};
                    Touch::sample.points[Touch::sample.count++] = {int16_t(point.x), int16_t(point.y)};
                    sample.point = point;
                }
                sample.state = points.size() ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
                lv_indev_read(input);
            };
            const int32_t oldX = s_shipX, oldY = s_shipY;
            const int32_t lx = (left.x1 + left.x2) / 2, rx = (right.x1 + right.x2) / 2;
            const int32_t idleGate = s_gates[0].x;
            advance(400);
            const int32_t idleScroll = idleGate - s_gates[0].x;
            touch({{lx, left.y1 + 1}, {rx, right.y1 + 1}});
            advance(40);
            assert(s_shipY < oldY && s_shipY > s_shipRadius + 2);
            assert(s_shipX > oldX); // Two simultaneous contacts control independent axes.
            const int32_t movingGate = s_gates[0].x;
            advance(400);
            assert(std::abs(movingGate - s_gates[0].x - idleScroll) <= 2);
            touch({{rx, right.y1 + 1}, {lx, left.y1 + 1}}); // Reordered report.
            advance(1600);
            assert(s_shipY <= s_shipRadius + 3);
            assert(s_shipX == s_fieldW - s_shipRadius - 3);
            touch({{rx, (right.y1 + right.y2) / 2}, {lx, left.y2 - 1}});
            advance(1600);
            assert(s_shipY >= s_fieldH - s_shipRadius - 4);
            const int32_t neutralX = s_shipX;
            advance(200);
            assert(s_shipX == neutralX && s_motion.vx == 0);
            touch({{rx, right.y2 - 1}});
            advance(400);
            assert(s_shipX < neutralX);
            touch({{lx, (left.y1 + left.y2) / 2}}); // Lift right finger; remaining point changes index.
            advance(1000);
            const int32_t releasedX = s_shipX;
            advance(200);
            assert(s_shipX == releasedX && s_motion.vx == 0);
            assert(std::abs(s_shipY - s_fieldH / 2) <= 3);
            touch({{field.x1 + 10, field.y1 + 10}}); // Field never teleports the ship.
            advance(200);
            assert(std::abs(s_shipY - s_fieldH / 2) <= 2 && s_shipX == releasedX);

            touch({{rx, right.y1 + 1}});
            advance(100);
            Touch::polling = false;
            advance(700); // A stalled sampler must not leave the throttle latched.
            assert(s_motion.vx == 0);
            Touch::polling = true;
            UiScreenPower::blocked = true;
            const int32_t sleepingX = s_shipX, sleepingGate = s_gates[0].x;
            advance(300);
            assert(s_shipX == sleepingX && s_gates[0].x == sleepingGate && !s_controlsArmed);
            UiScreenPower::blocked = false;
            advance(100);
            assert(!s_controlsArmed);
            touch({});
            advance(40);
            assert(s_controlsArmed);
            bool fired = false;
            for (const auto &bullet : s_bullets) fired |= bullet.active;
            assert(fired);
            resetCourse();
            const int32_t oldGateX = s_gates[0].x;
            advance(100);
            assert(s_gates[0].x < oldGateX);
            s_gates[0].x = s_shipX;
            s_motion = {float(s_shipX), float(s_shipRadius + 2), 0, 0};
            s_targetY = float(s_shipRadius + 2);
            s_shipY = s_shipRadius + 2;
            const int32_t impactX = s_shipX, impactY = s_shipY;
            advance(100);
            assert(s_impacts == 1 && s_shipX == impactX && s_shipY == impactY);
            s_gates[0].x = s_shipX + s_gateWidth;
            auto &shot = s_bullets[0];
            shot.active = true;
            shot.x = s_gates[0].x;
            shot.y = s_gates[0].gapCenter;
            advance(50);
            assert(s_targetsHit == 1 && !s_gates[0].targetVisible);
            lv_obj_send_event(reset, LV_EVENT_CLICKED, nullptr);
            assert(s_passed == 0 && s_impacts == 0 && s_targetsHit == 0);
            assert(s_shipX == s_fieldW / 7 && s_shipY == s_fieldH / 2);
            rotateOK = false;
            closeCourse();
            assert(s_timer && Display::landscape());
            rotateOK = true;
            lv_obj_send_event(back, LV_EVENT_CLICKED, nullptr);
            advance(250);
            assert(!s_timer && !s_field && !Display::landscape());
            assert(!Touch::enabled);
            showTouchTest(); advance(250); // Re-entry after deletion must get fresh objects.
            assert(s_timer && s_field);
            closeCourse(); advance(250);
            assert(!s_timer && !s_field);
        }
        }
    }
    lv_indev_delete(input);
    lv_display_delete(display);
    lv_deinit();
    puts("Arcade production UI: SV/EN, S3/P4, 90/270 rotation, two-thumb inertia, neutral/release/stale/wake and cleanup passed");
}
'''
(WORK / "test.cpp").write_text(code)
fonts = sorted((ROOT / "components/my_ui/fonts").glob("my_font_raw_*.c"))
sources = [
    ROOT / "components/my_ui/my_fonts.cpp",
    *fonts,
    ROOT / "main/ui_metrics.cpp",
    ROOT / "main/hw_profile.cpp",
    ROOT / "main/lang/lang.cpp",
    ROOT / "main/lang/lang_sv.cpp",
    ROOT / "main/lang/lang_en.cpp",
]
quoted = " ".join(f'"{p}"' for p in sources)
(WORK / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.22)
project(arcade_layout LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(LV_CONF_SKIP ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_compile_definitions(LV_CONF_SKIP LV_COLOR_FORMAT_DEFAULT=LV_COLOR_FORMAT_RGB565
 LV_DPI_DEF=130 LV_MEM_SIZE=262144 LV_USE_ASSERT_MALLOC=1)
add_subdirectory("{ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(layout test.cpp {quoted})
target_include_directories(layout PRIVATE "{WORK}" "{ROOT}/components/my_ui/include"
 "{ROOT}/components/my_lvgl/include" "{ROOT}/main/lang" "{ROOT}/main" "{ROOT}/tests/host_stubs")
target_link_libraries(layout PRIVATE lvgl m)
''')
subprocess.run(["cmake", "-S", str(WORK), "-B", str(WORK / "build"), "-G", "Ninja"], check=True)
subprocess.run(["cmake", "--build", str(WORK / "build"), "--target", "layout", "-j", "2"], check=True)
subprocess.run([str(WORK / "build/layout")], check=True, timeout=60)
