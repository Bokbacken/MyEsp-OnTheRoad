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
(WORK / "esp_random.h").write_text("""
#pragma once
#include <stdint.h>
inline uint32_t esp_random() { static uint32_t value=0; return value++; }
""")
(WORK / "test.cpp").write_text(r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include "lvgl.h"
#include "my_fonts.h"
#include "lang.h"
#include "my_sys.h"
#include "app_config.h"
static AppConfig testConfig{};
namespace Config { AppConfig &get() { return testConfig; } }
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
static bool solveBoard(uint8_t board[81]) {
    unsigned cell=81, best=10;
    uint16_t options=0;
    for(unsigned i=0;i<81;++i) {
        if(board[i]) continue;
        uint16_t candidates=0;
        const unsigned row=i/9, col=i%9, boxRow=row/3*3, boxCol=col/3*3;
        for(unsigned digit=1;digit<=9;++digit) {
            bool valid=true;
            for(unsigned k=0;k<9;++k) {
                if(board[row*9+k]==digit || board[k*9+col]==digit ||
                   board[(boxRow+k/3)*9+boxCol+k%3]==digit) valid=false;
            }
            if(valid) candidates|=static_cast<uint16_t>(1u<<(digit-1));
        }
        unsigned count=0;
        for(uint16_t bits=candidates;bits;bits&=static_cast<uint16_t>(bits-1)) ++count;
        if(count<best) { cell=i; best=count; options=candidates; }
        if(!count) return false;
    }
    if(cell==81) return true;
    for(unsigned digit=1;digit<=9;++digit) {
        if(!(options&(1u<<(digit-1)))) continue;
        board[cell]=digit;
        if(solveBoard(board)) return true;
        board[cell]=0;
    }
    return false;
}
int main() {
    lv_init();
    Config::get().sudokuDeadZone=4;
    auto *display=lv_display_create(320,480);
    static uint16_t pixels[800*30];
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,pixels,nullptr,sizeof(pixels),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,[](lv_display_t *d,const lv_area_t *,uint8_t *) { lv_display_flush_ready(d); });
    std::string previousCheer;
    for (auto size : {lv_point_t{320,480},lv_point_t{480,320},lv_point_t{480,800},lv_point_t{800,480}}) {
      lv_display_set_resolution(display,size.x,size.y);
      for (int language=0;language<2;++language) {
        Lang::setLang(language==0 ? "sv" : "en");
        for (auto textSize : {MyFonts::TextSize::NORMAL,MyFonts::TextSize::LARGE,MyFonts::TextSize::XLARGE}) {
        MyFonts::apply(textSize,size.x<size.y?size.x:size.y);
        UiSudoku::s_game.start(Sudoku::Difficulty::EASY,0);
        for(auto &next : UiSudoku::s_next) next=0;
        UiSudoku::s_ready=true;
        UiSudoku::s_elapsed=0;
        UiSudoku::s_dirty=false;
        UiSudoku::s_saveFailed=false;
        UiSudoku::s_completedSaved=false;
        UiSudoku::s_paused=false;
        UiSudoku::play(); settle();
        assert(UiSudoku::running());
        unsigned fixedCell=0;
        while(!UiSudoku::s_game.fixed(fixedCell)) ++fixedCell;
        const uint8_t givenValue=UiSudoku::s_game.value(fixedCell);
        UiSudoku::s_digit=static_cast<uint8_t>(givenValue%9+1);
        UiSudoku::s_notes=false;
        UiSudoku::s_erase=false;
        UiSudoku::chooseCell(fixedCell);
        assert(UiSudoku::s_game.value(fixedCell)==givenValue && !UiSudoku::s_replaceDialog);
        assert(UiSudoku::cellAtPoint(4,4,432,4)==0);
        assert(UiSudoku::cellAtPoint(43,43,432,4)==0);
        assert(UiSudoku::cellAtPoint(3,24,432,4)==-1);
        assert(UiSudoku::cellAtPoint(44,24,432,4)==-1);
        assert(UiSudoku::cellAtPoint(24,3,432,4)==-1);
        assert(UiSudoku::cellAtPoint(24,44,432,4)==-1);
        assert(UiSudoku::cellAtPoint(52,24,432,4)==1);
        assert(UiSudoku::cellAtPoint(432,24,432,4)==-1);
        assert(UiSudoku::cellAtPoint(0,0,432,0)==0);
        assert(UiSudoku::cellAtPoint(7,24,432,8)==-1);
        assert(UiSudoku::cellAtPoint(8,24,432,8)==0);
        assert(UiSudoku::cellAtPoint(39,24,432,8)==0);
        assert(UiSudoku::cellAtPoint(40,24,432,8)==-1);
        inside(UiSudoku::s_board,size.x,size.y);
        inside(UiSudoku::s_info,size.x,size.y);
        inside(UiSudoku::s_help,size.x,size.y);
        lv_area_t infoArea, boardArea;
        lv_obj_get_coords(UiSudoku::s_info,&infoArea);
        lv_obj_get_coords(UiSudoku::s_board,&boardArea);
        assert(infoArea.y2 < boardArea.y1);
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
        unsigned replacement=0;
        while(replacement<Sudoku::CELL_COUNT &&
              (replacement==empty || UiSudoku::s_game.fixed(replacement))) ++replacement;
        assert(replacement<Sudoku::CELL_COUNT);
        UiSudoku::s_notes=false;
        UiSudoku::s_digit=8;
        UiSudoku::chooseCell(replacement);
        assert(UiSudoku::s_game.value(replacement)==8);
        UiSudoku::s_digit=7;
        UiSudoku::chooseCell(replacement);
        assert(UiSudoku::s_game.value(replacement)==8 && UiSudoku::s_replaceDialog);
        auto *replaceBox=lv_obj_get_child(UiSudoku::s_replaceDialog,0);
        lv_obj_send_event(lv_obj_get_child(replaceBox,1),LV_EVENT_CLICKED,nullptr);
        assert(UiSudoku::s_game.value(replacement)==8);
        UiSudoku::chooseCell(replacement);
        replaceBox=lv_obj_get_child(UiSudoku::s_replaceDialog,0);
        lv_obj_send_event(lv_obj_get_child(replaceBox,2),LV_EVENT_CLICKED,nullptr);
        assert(UiSudoku::s_game.value(replacement)==7 && !UiSudoku::s_replaceDialog);
        UiSudoku::chooseCell(replacement);
        assert(UiSudoku::s_game.value(replacement)==0);
        UiSudoku::chooseCell(replacement);
        assert(UiSudoku::s_game.value(replacement)==7);
        UiSudoku::chooseCell(replacement);
        assert(UiSudoku::s_game.value(replacement)==0);
        unsigned conflictCell=0, conflictPeer=0;
        for(;conflictCell<Sudoku::CELL_COUNT;++conflictCell) {
            if(UiSudoku::s_game.fixed(conflictCell)) continue;
            for(conflictPeer=0;conflictPeer<Sudoku::CELL_COUNT;++conflictPeer) {
                if(!UiSudoku::s_game.fixed(conflictPeer)) continue;
                const unsigned r=conflictCell/9, c=conflictCell%9;
                const unsigned pr=conflictPeer/9, pc=conflictPeer%9;
                if(r==pr || c==pc || (r/3==pr/3 && c/3==pc/3)) break;
            }
            if(conflictPeer<Sudoku::CELL_COUNT) break;
        }
        assert(conflictCell<Sudoku::CELL_COUNT && conflictPeer<Sudoku::CELL_COUNT);
        UiSudoku::s_digit=UiSudoku::s_game.value(conflictPeer);
        UiSudoku::s_notes=false;
        UiSudoku::chooseCell(conflictCell);
        assert(UiSudoku::s_game.incorrect(conflictCell));
        assert(UiSudoku::showErrorCross(conflictCell));
        assert(UiSudoku::showErrorCross(conflictPeer));
        UiSudoku::chooseCell(conflictCell);
        assert(UiSudoku::s_game.value(conflictCell)==0);
        UiSudoku::s_digit=8;
        UiSudoku::s_notes=true;
        if (!(UiSudoku::s_game.notes(empty)&128)) UiSudoku::chooseCell(empty);
        assert(UiSudoku::s_game.notes(empty)==128);
        UiSudoku::s_tick=MySys::now;
        MySys::now+=750;
        event(5); assert(!UiSudoku::running());
        assert(UiSudoku::s_elapsed==750);
        UiSudoku::chooseCell(empty); assert(UiSudoku::s_game.notes(empty)==128);
        MySys::now+=400;
        event(5); assert(UiSudoku::running());
        assert(UiSudoku::s_elapsed==750);
        MySys::now+=650;
        event(5); assert(!UiSudoku::running() && UiSudoku::s_elapsed==1400);
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
        unsigned target=0;
        while(target<Sudoku::CELL_COUNT &&
              (UiSudoku::s_game.fixed(target) || target==empty ||
               target/9==empty/9 || target%9==empty%9 ||
               (target/27==empty/27 && target%9/3==empty%9/3))) ++target;
        assert(target<Sudoku::CELL_COUNT);
        const unsigned targetRow=target/9, targetCol=target%9, targetBox=targetRow/3*3+targetCol/3;
        unsigned peers[3]={Sudoku::CELL_COUNT,Sudoku::CELL_COUNT,Sudoku::CELL_COUNT};
        for(unsigned peer=0;peer<Sudoku::CELL_COUNT;++peer) {
            if(peer==target || UiSudoku::s_game.fixed(peer)) continue;
            const unsigned row=peer/9, col=peer%9, box=row/3*3+col/3;
            if(row==targetRow && col/3!=targetCol/3) peers[0]=peer;
            if(col==targetCol && row/3!=targetRow/3) peers[1]=peer;
            if(box==targetBox && row!=targetRow && col!=targetCol) peers[2]=peer;
        }
        for(unsigned peer:peers) {
            assert(peer<Sudoku::CELL_COUNT);
            assert(UiSudoku::s_game.put(peer,8,true));
            assert(UiSudoku::s_game.notes(peer)&128);
        }
        UiSudoku::s_notes=false;
        UiSudoku::s_digit=8;
        UiSudoku::chooseCell(target);
        assert(UiSudoku::s_game.value(target)==8);
        for(unsigned peer:peers) assert(!(UiSudoku::s_game.notes(peer)&128));
        assert(UiSudoku::s_game.notes(empty)&128);
        uint8_t solution[81];
        for(unsigned cell=0;cell<81;++cell) solution[cell]=UiSudoku::s_game.givens()[cell]-'0';
        assert(solveBoard(solution));
        uint8_t wrong=0;
        for(unsigned digit=1;digit<=9;++digit) {
            if(digit==solution[empty]) continue;
            assert(UiSudoku::s_game.put(empty,digit,false));
            if(!UiSudoku::s_game.incorrect(empty)) { wrong=digit; break; }
            assert(UiSudoku::s_game.erase(empty));
        }
        assert(wrong);
        event(2);
        assert(UiSudoku::s_checked && !UiSudoku::s_game.incorrect(empty));
        assert(!UiSudoku::s_game.matchesSolution(empty));
        for(unsigned cell=0;cell<81;++cell)
            if(!UiSudoku::s_game.fixed(cell)) UiSudoku::s_game.put(cell,solution[cell],false);
        UiSudoku::refresh();
        assert(UiSudoku::s_game.complete() && !UiSudoku::running());
        assert(UiSudoku::s_cheer);
        const std::string cheer=lv_label_get_text(UiSudoku::s_cheer);
        assert(!cheer.empty() && cheer!=previousCheer);
        previousCheer=cheer;
        for(int frame=0;frame<240;++frame) {
            lv_tick_inc(10); MySys::now+=10; lv_timer_handler();
        }
        assert(UiSudoku::s_cheer && !UiSudoku::s_winPrompt);
        for(int frame=0;frame<150;++frame) {
            lv_tick_inc(10); MySys::now+=10; lv_timer_handler();
        }
        assert(UiSudoku::s_winPrompt);
        auto *winBox=lv_obj_get_child(UiSudoku::s_winPrompt,0);
        assert(lv_obj_get_child_count(winBox)==3);
        auto *backButton=lv_obj_get_child(winBox,1);
        auto *nextButton=lv_obj_get_child(winBox,2);
        lv_area_t backArea, nextArea;
        lv_obj_get_coords(backButton,&backArea);
        lv_obj_get_coords(nextButton,&nextArea);
        assert(backArea.x1 < nextArea.x1);
        for(auto *digit : UiSudoku::s_digits) assert(lv_obj_has_state(digit,LV_STATE_DISABLED));
        assert(UiSudoku::s_next[0]==1);
        UiSudoku::refresh(); assert(UiSudoku::s_next[0]==1);
        const bool testBack = size.x==320 && size.y==480 && language==0 &&
                              textSize==MyFonts::TextSize::NORMAL;
        lv_obj_send_event(lv_obj_get_child(winBox,testBack ? 1 : 2),LV_EVENT_CLICKED,nullptr);
        settle();
        if(testBack) {
            assert(!UiSudoku::s_board && !UiSudoku::running());
            continue;
        }
        assert(UiSudoku::s_game.puzzleIndex()==1 && UiSudoku::s_board && UiSudoku::running());
        UiSudoku::s_dirty=true;
        MyNvs::fail=true;
        auto *gameScreen=UiSudoku::s_gameScreen;
        UiSudoku::leave();
        assert(UiSudoku::s_gameScreen==gameScreen && UiSudoku::s_board && UiSudoku::s_saveFailed);
        MyNvs::fail=false;
        UiSudoku::leave();
        settle();
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
