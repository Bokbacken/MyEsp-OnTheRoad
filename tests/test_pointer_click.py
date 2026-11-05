#!/usr/bin/env python3
"""Exercise the shared pointer callbacks against real LVGL, not event mocks."""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
WORK = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(tempfile.mkdtemp(prefix="pointer-click-"))
WORK.mkdir(parents=True, exist_ok=True)
port = (ROOT / "components/my_lvgl/my_lvgl.cpp").read_text()
callbacks = port.split("bool g_pointerPressed = false;", 1)[1].split("\nvoid lvglTask", 1)[0]
registration = port.split("lv_indev_t *addPointer(PointerFn fn)\n{", 1)[1].split("\n}\n", 1)[0]
(WORK / "test.cpp").write_text(r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include "lvgl.h"
#define ESP_LOGE(...) ((void)0)
using PointerFn = bool (*)(int16_t *, int16_t *);
static PointerFn g_pointer;
static bool (*g_pointerFilter)(bool);
static lv_display_t *g_disp;
static bool g_pointerPressed = false;
''' + callbacks + '\nlv_indev_t *addPointer(PointerFn fn) {\n' + registration + r'''
}
static bool down;
static int16_t px, py;
static unsigned clicks, shortClicks, releases, scrolls;
static unsigned observed;
static bool readPoint(int16_t *x, int16_t *y) { *x=px; *y=py; return down; }
static void events(lv_event_t *e) {
    switch(lv_event_get_code(e)) {
    case LV_EVENT_CLICKED: ++clicks; break;
    case LV_EVENT_SHORT_CLICKED: ++shortClicks; break;
    case LV_EVENT_RELEASED: ++releases; break;
    case LV_EVENT_SCROLL_BEGIN: ++scrolls; break;
    default: break;
    }
}
int main() {
    lv_init();
    g_disp=lv_display_create(480,800);
    auto *in=addPointer(readPoint);
    assert(in);
    lv_indev_set_mode(in,LV_INDEV_MODE_EVENT);
    auto *button=lv_button_create(lv_screen_active());
    lv_obj_set_pos(button,20,20);
    lv_obj_set_size(button,300,300);
    lv_obj_set_scrollable(button,false);
    lv_obj_set_scrollable(lv_screen_active(),false);
    lv_obj_add_event_cb(button,events,LV_EVENT_ALL,nullptr);
    lv_obj_update_layout(lv_screen_active());
    auto sample=[&](bool pressed,int x,int y) {
        down=pressed; px=x; py=y; lv_tick_inc(30); lv_indev_read(in);
    };
    for (auto size : {lv_point_t{320,480},lv_point_t{480,800},lv_point_t{800,480}}) {
        lv_display_set_resolution(g_disp,size.x,size.y);
        for(int distance : {0,20,21,80}) {
            const auto before=clicks, beforeShort=shortClicks, beforeRelease=releases;
            sample(true,100,100);
            sample(true,100+distance,100);
            sample(false,100+distance,100);
            assert(clicks==before+(distance<=20));
            assert(shortClicks==beforeShort+(distance<=20));
            assert(releases==beforeRelease+1);
        }
    }
    auto before=clicks;
    sample(true,100,100); sample(false,180,160); // Release-only movement.
    assert(clicks==before);
    sample(true,100,100); sample(true,180,100); sample(true,100,100);
    sample(false,100,100); // Returning to origin must not re-enable clicks.
    assert(clicks==before);
    sample(true,100,100); sample(false,100,100);
    assert(clicks==before+1); // New tap clears the veto.
    for(auto code : {LV_EVENT_CLICKED,LV_EVENT_SHORT_CLICKED,LV_EVENT_SINGLE_CLICKED,
                     LV_EVENT_DOUBLE_CLICKED,LV_EVENT_TRIPLE_CLICKED}) {
        sample(true,100,100); sample(true,180,100);
        assert(lv_indev_add_event_cb(in,[](lv_event_t *) { ++observed; },code,nullptr));
        const auto count=observed;
        lv_indev_send_event(in,code,nullptr);
        assert(observed==count);
        lv_indev_remove_event(in,lv_indev_get_event_count(in)-1);
        sample(false,180,100);
    }
    lv_obj_delete(button);
    auto *container=lv_obj_create(lv_screen_active());
    lv_obj_set_pos(container,0,0); lv_obj_set_size(container,300,300);
    auto *content=lv_obj_create(container);
    lv_obj_set_size(content,200,1000);
    lv_obj_add_event_cb(container,events,LV_EVENT_ALL,nullptr);
    lv_obj_update_layout(lv_screen_active());
    sample(true,100,200); sample(true,100,140); sample(true,100,80);
    sample(false,100,80);
    assert(scrolls>0);
    puts("Shared pointer: clean taps, slop, drag/release veto, click variants and scrolling passed");
}
''')
(WORK / "CMakeLists.txt").write_text(f'''
cmake_minimum_required(VERSION 3.22)
project(pointer_click LANGUAGES C CXX)
set(LV_CONF_SKIP ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_compile_definitions(LV_CONF_SKIP LV_COLOR_FORMAT_DEFAULT=LV_COLOR_FORMAT_RGB565 LV_MEM_SIZE=262144)
add_subdirectory("{ROOT / 'managed_components/lvgl__lvgl'}" lvgl)
add_executable(pointer_click test.cpp)
target_compile_options(pointer_click PRIVATE -Wall -Wextra -Werror)
target_link_libraries(pointer_click PRIVATE lvgl m)
''')
subprocess.run(["cmake", "-S", str(WORK), "-B", str(WORK / "build")], check=True, stdout=subprocess.DEVNULL)
subprocess.run(["cmake", "--build", str(WORK / "build"), "-j", "2"], check=True, stdout=subprocess.DEVNULL)
subprocess.run([str(WORK / "build/pointer_click")], check=True)
