#!/usr/bin/env python3
"""Build the real Sudoku screen with host LVGL and deterministic NVS/time doubles."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
WORK = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(tempfile.mkdtemp(prefix="sudoku-ui-"))
WORK.mkdir(parents=True, exist_ok=True)
(WORK / "my_sys.h").write_text("""
#pragma once
#include <stdint.h>
namespace MySys { inline uint32_t now=0; inline uint32_t millis() { return now; } }
""")
(WORK / "my_storage.h").write_text("""
#pragma once
#include <cstring>
#include <vector>
namespace MyNvs {
inline std::vector<unsigned char> data;
inline bool fail=false;
class Handle {
public:
 Handle(const char *, bool) {}
 explicit operator bool() const { return true; }
 size_t blobSize(const char *) const { return data.size(); }
 bool getBlob(const char *, void *out, size_t n) const {
  if(n!=data.size()) return false;
  memcpy(out,data.data(),n); return true;
 }
 bool setBlob(const char *, const void *value, size_t n) {
  if(fail) return false;
  auto *p=static_cast<const unsigned char *>(value); data.assign(p,p+n); return true;
 }
 bool commit() { return !fail; }
};
}
""")
(WORK / "sdkconfig.h").write_text("")
(WORK / "test.cpp").write_text(r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include "lvgl.h"
#include "my_fonts.h"
#include "lang.h"
#include "my_sys.h"
#include "ui_sudoku.cpp"
namespace Backlight { bool sleeping() { return false; } }
namespace UiYatzy { void showMenu() {} }
static void settle() {
    for (int i=0;i<30;++i) { lv_tick_inc(10); MySys::now+=10; lv_timer_handler(); }
    lv_obj_update_layout(lv_screen_active());
}
static void event(unsigned action) {
    auto *button=Theme::button(lv_screen_active(), "", 10, 10);
    lv_obj_add_event_cb(button, UiSudoku::tool, LV_EVENT_CLICKED, reinterpret_cast<void *>(uintptr_t(action)));
    lv_obj_send_event(button,LV_EVENT_CLICKED,nullptr);
    lv_obj_delete(button);
}
static void inside(lv_obj_t *object, int w, int h) {
    lv_area_t a; lv_obj_get_coords(object,&a);
    assert(a.x1>=0 && a.y1>=0 && a.x2<w && a.y2<h);
}
int main() {
    lv_init();
    auto *display=lv_display_create(320,480);
    static uint16_t pixels[800*30];
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,pixels,nullptr,sizeof(pixels),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *,uint8_t *) { lv_display_flush_ready(d); });
    for (auto size : {lv_point_t{320,480},lv_point_t{480,320},lv_point_t{480,800},lv_point_t{800,480}}) {
      lv_display_set_resolution(display,size.x,size.y);
      for (int language=0;language<2;++language) {
        Lang::setLang(language==0 ? "sv" : "en");
        for (auto textSize : {MyFonts::TextSize::NORMAL,MyFonts::TextSize::LARGE,MyFonts::TextSize::XLARGE}) {
        MyFonts::apply(textSize,size.x<size.y?size.x:size.y);
        UiSudoku::s_game.start(Sudoku::Difficulty::EASY,0);
        for(auto &next : UiSudoku::s_next) next=0;
        UiSudoku::s_ready=true;
        UiSudoku::s_completedSaved=false;
        UiSudoku::s_paused=false;
        UiSudoku::play(); settle();
        assert(UiSudoku::running());
        inside(UiSudoku::s_board,size.x,size.y);
        inside(UiSudoku::s_info,size.x,size.y);
        inside(UiSudoku::s_help,size.x,size.y);
        for(auto *digit : UiSudoku::s_digits) inside(digit,size.x,size.y);
        inside(UiSudoku::s_noteButton,size.x,size.y);
        inside(UiSudoku::s_pauseButton,size.x,size.y);
        unsigned empty=0;
        while(UiSudoku::s_game.fixed(empty)) ++empty;
        UiSudoku::s_digit=8;
        UiSudoku::s_notes=true;
        UiSudoku::chooseCell(empty);
        assert(UiSudoku::s_game.notes(empty)==128);
        event(0); assert(UiSudoku::s_game.notes(empty)==0);
        event(1); assert(UiSudoku::s_game.notes(empty)==128);
        event(2); assert(UiSudoku::s_checked && !UiSudoku::s_game.incorrect(empty));
        event(4); UiSudoku::chooseCell(empty);
        assert(UiSudoku::s_game.notes(empty)==0);
        event(0); assert(UiSudoku::s_game.notes(empty)==128);
        event(4);
        event(5); assert(!UiSudoku::running());
        UiSudoku::chooseCell(empty); assert(UiSudoku::s_game.notes(empty)==128);
        event(5); assert(UiSudoku::running());
        UiSudoku::s_dirty=true;
        UiSudoku::save(); assert(!MyNvs::data.empty());
        UiSudoku::s_game.start(Sudoku::Difficulty::MEDIUM,1);
        UiSudoku::restore();
        assert(UiSudoku::s_game.difficulty()==Sudoku::Difficulty::EASY);
        assert(UiSudoku::s_game.notes(empty)==128);
        assert(!UiSudoku::s_game.undo()); // Restoring is not an edit.
        UiSudoku::Save saved{};
        memcpy(&saved,MyNvs::data.data(),sizeof(saved));
        unsigned given=0;
        while(!UiSudoku::s_game.fixed(given)) ++given;
        saved.values[given]=0;
        memcpy(MyNvs::data.data(),&saved,sizeof(saved));
        UiSudoku::s_ready=false;
        UiSudoku::restore();
        assert(!UiSudoku::s_ready);
        UiSudoku::s_ready=true;
        MyNvs::fail=true;
        UiSudoku::s_dirty=true; UiSudoku::save();
        assert(UiSudoku::s_dirty && UiSudoku::s_saveFailed);
        MyNvs::fail=false; UiSudoku::save();
        assert(!UiSudoku::s_dirty && !UiSudoku::s_saveFailed);
        for(unsigned digit=1;digit<=9;++digit) {
            UiSudoku::s_game.put(empty,digit,false);
            if(UiSudoku::s_game.incorrect(empty)) break;
        }
        event(2); assert(UiSudoku::s_checked && UiSudoku::s_game.incorrect(empty));
        for(unsigned cell=0;cell<81;++cell) {
            if(UiSudoku::s_game.fixed(cell)) continue;
            for(unsigned digit=1;digit<=9;++digit) {
                UiSudoku::s_game.put(cell,digit,false);
                if(!UiSudoku::s_game.incorrect(cell)) break;
            }
        }
        UiSudoku::refresh();
        assert(UiSudoku::s_game.complete() && !UiSudoku::running());
        assert(UiSudoku::s_next[0]==1);
        UiSudoku::refresh(); assert(UiSudoku::s_next[0]==1);
        UiSudoku::show(); settle();
        assert(!UiSudoku::running() && UiSudoku::s_board==nullptr);
        auto *page=lv_screen_active();
        for(unsigned i=0;i<lv_obj_get_child_count(page);++i) inside(lv_obj_get_child(page,i),size.x,size.y);
        printf("Sudoku screen passed: %dx%d language %d text size %d\n",size.x,size.y,language,int(textSize));
        }
      }
    }
    lv_display_delete(display);
    lv_deinit();
}
''')
fonts = " ".join(f'"{p}"' for p in (ROOT / "components/my_ui/fonts").glob("my_font_raw_*.c"))
(WORK / "CMakeLists.txt").write_text(f"""
cmake_minimum_required(VERSION 3.22)
project(sudoku_ui LANGUAGES C CXX)
set(LV_CONF_SKIP ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_compile_definitions(LV_CONF_SKIP LV_COLOR_FORMAT_DEFAULT=LV_COLOR_FORMAT_RGB565 LV_MEM_SIZE=262144)
add_subdirectory("{ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(sudoku_ui test.cpp "{ROOT}/main/sudoku_game.cpp"
 "{ROOT}/main/ui_metrics.cpp" "{ROOT}/main/hw_profile.cpp"
 "{ROOT}/components/my_ui/my_fonts.cpp" {fonts}
 "{ROOT}/main/lang/lang.cpp" "{ROOT}/main/lang/lang_sv.cpp" "{ROOT}/main/lang/lang_en.cpp")
target_include_directories(sudoku_ui PRIVATE "{WORK}" "{ROOT}/tests/player_stubs"
 "{ROOT}/main" "{ROOT}/main/lang" "{ROOT}/components/my_ui/include")
target_compile_options(sudoku_ui PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Werror;-Wno-error=deprecated-declarations>)
target_link_libraries(sudoku_ui PRIVATE lvgl m)
""")
subprocess.run(["cmake", "-S", str(WORK), "-B", str(WORK / "build"), "-G", "Ninja"], check=True)
subprocess.run(["cmake", "--build", str(WORK / "build"), "--target", "sudoku_ui", "-j", "2"], check=True)
subprocess.run([str(WORK / "build/sudoku_ui")], check=True)
