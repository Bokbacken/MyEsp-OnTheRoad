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
 bool erase(const char *) {
  if(fail) return false;
  data.clear(); return true;
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
(WORK / "freertos").mkdir(exist_ok=True)
(WORK / "freertos" / "FreeRTOS.h").write_text("""
#pragma once
using BaseType_t = int;
using UBaseType_t = unsigned;
using TaskHandle_t = void *;
#define pdPASS 1
""")
(WORK / "freertos" / "task.h").write_text("""
#pragma once
#include "FreeRTOS.h"
using TaskFunction_t = void (*)(void *);
BaseType_t xTaskCreate(TaskFunction_t, const char *, unsigned, void *, UBaseType_t, TaskHandle_t *);
void vTaskDelete(void *);
""")
(WORK / "esp_random.h").write_text("""
#pragma once
#include <stdint.h>
inline uint32_t esp_random() { static uint32_t value=0; return value++; }
""")
(WORK / "esp_heap_caps.h").write_text("""
#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline void *heap_caps_calloc(size_t count, size_t size, int) { return std::calloc(count,size); }
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
#include "ui_touch_test.h"
static AppConfig testConfig{};
namespace Config { AppConfig &get() { return testConfig; } }
#include "ui_sudoku.cpp"
#include "ui_touch_test.cpp"
namespace Backlight { bool sleeping() { return false; } }
namespace UiYatzy { void showMenu() {} }
namespace BoardSd {
bool begin() { return false; }
bool mounted() { return false; }
bool transferHealthy() { return false; }
bool beginTransfer() { return false; }
void endTransfer() {}
bool ensureDir(const char *) { return false; }
Info info() { return {}; }
}
BaseType_t xTaskCreate(TaskFunction_t, const char *, unsigned, void *, UBaseType_t, TaskHandle_t *) {
    return 0;
}
void vTaskDelete(void *) {}
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
    Config::get().sudokuHelpHints=true;
    lv_font_glyph_dsc_t undoGlyph{}, redoGlyph{};
    assert(lv_font_get_glyph_dsc(&my_font_raw_24, &undoGlyph, 0xF0E2, 0));
    assert(lv_font_get_glyph_dsc(&my_font_raw_24, &redoGlyph, 0xF2F9, 0));
    assert(undoGlyph.box_w && undoGlyph.box_h && redoGlyph.box_w && redoGlyph.box_h);
    assert(strcmp(UiSudoku::SUDOKU_SYMBOL_REDO, "\xef\x8b\xb9") == 0);
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
        for (int zoom=0;zoom<3;++zoom)
        for (auto textSize : {MyFonts::TextSize::SMALL,MyFonts::TextSize::NORMAL,MyFonts::TextSize::LARGE,MyFonts::TextSize::XLARGE}) {
        const int shortSide = size.x<size.y?size.x:size.y;
        MyFonts::apply(textSize,shortSide,shortSide>=480 ? 117 : 154,(MyFonts::ScreenZoom)zoom);
        for (const auto *font : {&my_font_14, &my_font_18, &my_font_24, &my_font_36}) {
            for (uint32_t symbol : {0xF0E2u, 0xF2F9u}) {
                lv_font_glyph_dsc_t glyph{};
                assert(lv_font_get_glyph_dsc(font, &glyph, symbol, 0));
                assert(glyph.box_w && glyph.box_h);
            }
        }
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
        assert(!UiSudoku::s_saveDialog);
        lv_area_t infoArea, boardArea;
        lv_obj_get_coords(UiSudoku::s_info,&infoArea);
        lv_obj_get_coords(UiSudoku::s_board,&boardArea);
        assert(infoArea.y2 < boardArea.y1);
        assert(lv_obj_get_width(UiSudoku::s_board) >= 9 * 20);
        assert(lv_obj_get_width(UiSudoku::s_board) == lv_obj_get_height(UiSudoku::s_board));
        const auto *cellFont = MyFonts::fit(&my_font_24,lv_obj_get_width(UiSudoku::s_board)/9-4);
        assert(cellFont->line_height <= lv_obj_get_width(UiSudoku::s_board)/9-4);
        for(auto *digit : UiSudoku::s_digits) {
            inside(digit,size.x,size.y);
            assert(lv_obj_get_style_text_font(lv_obj_get_child(digit,0),LV_PART_MAIN)==&my_font_24);
            assert(lv_obj_get_height(digit)>=my_font_24.line_height+4);
            assert(lv_obj_get_height(digit)>=(size.x>size.y ? 48 : (size.y<=500 ? 36 : 50)));
            lv_area_t digitArea;
            lv_obj_get_coords(digit,&digitArea);
            assert(size.x>size.y ? digitArea.x1>boardArea.x2 : digitArea.y1>boardArea.y2);
            for(auto *other : UiSudoku::s_digits) {
                if(other==digit) continue;
                lv_area_t otherArea;
                lv_obj_get_coords(other,&otherArea);
                assert(digitArea.x2<otherArea.x1 || otherArea.x2<digitArea.x1 ||
                       digitArea.y2<otherArea.y1 || otherArea.y2<digitArea.y1);
            }
        }
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
        assert(!UiSudoku::s_saveDialog);
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
        auto *saveDialog=UiSudoku::s_saveDialog;
        assert(saveDialog && lv_obj_get_parent(saveDialog)==UiSudoku::s_gameScreen);
        UiSudoku::save(); UiSudoku::refresh(); settle();
        assert(UiSudoku::s_saveDialog==saveDialog); // Repeated failures do not stack popups.
        auto *saveBox=lv_obj_get_child(saveDialog,0);
        auto *saveMessage=lv_obj_get_child(saveBox,0);
        auto *saveOk=lv_obj_get_child(saveBox,1);
        inside(saveDialog,size.x,size.y);
        inside(saveBox,size.x,size.y);
        inside(saveMessage,size.x,size.y);
        inside(saveOk,size.x,size.y);
        lv_area_t messageArea, okArea;
        lv_obj_get_coords(saveMessage,&messageArea);
        lv_obj_get_coords(saveOk,&okArea);
        assert(messageArea.y2<okArea.y1);
        assert(strcmp(lv_label_get_text(saveMessage),tr(Str::SUDOKU_SAVE_FAILED))==0);
        lv_obj_send_event(saveOk,LV_EVENT_CLICKED,nullptr);
        UiSudoku::refresh();
        assert(!UiSudoku::s_saveDialog && UiSudoku::s_saveFailed);
        UiSudoku::save();
        assert(UiSudoku::s_saveDialog);
        MyNvs::fail=false; UiSudoku::save();
        assert(!UiSudoku::s_dirty && !UiSudoku::s_saveFailed && !UiSudoku::s_saveDialog);
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
            assert(UiSudoku::s_listScreen && !MyNvs::data.empty());
            UiSudoku::confirmResetProgress();
            auto *resetBox=lv_obj_get_parent(UiSudoku::s_resetMessage);
            assert(resetBox && lv_obj_get_child_count(resetBox)==3);
            lv_obj_send_event(lv_obj_get_child(resetBox,2),LV_EVENT_CLICKED,nullptr);
            settle();
            assert(MyNvs::data.empty() && !UiSudoku::s_ready && UiSudoku::s_next[0]==0);
            continue;
        }
        assert(UiSudoku::s_game.puzzleIndex()==1 && UiSudoku::s_board && UiSudoku::running());
        UiSudoku::s_dirty=true;
        MyNvs::fail=true;
        auto *gameScreen=UiSudoku::s_gameScreen;
        UiSudoku::leave();
        assert(UiSudoku::s_gameScreen==gameScreen && UiSudoku::s_board && UiSudoku::s_saveFailed);
        assert(UiSudoku::s_saveDialog);
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
    Lang::setLang("en");
    MyFonts::apply(MyFonts::TextSize::NORMAL, 480);
    UiSudoku::s_sdScanNoticeRequired=true;
    UiSudoku::show(); settle();
    assert(UiSudoku::s_sdScanNoticeRequired);
    UiSudoku::show(); settle();
    assert(UiSudoku::s_sdScanNoticeRequired);
    auto *screen=lv_screen_active();
    auto *shade=lv_obj_get_child(screen,lv_obj_get_child_count(screen)-1);
    auto *box=lv_obj_get_child(shade,0);
    assert(lv_obj_get_child_count(box)==2);
    lv_obj_send_event(lv_obj_get_child(box,1),LV_EVENT_CLICKED,nullptr);
    assert(!UiSudoku::s_sdScanNoticeRequired);
    for (auto size : {lv_point_t{320,480},lv_point_t{480,320},lv_point_t{480,800},lv_point_t{800,480}}) {
        lv_display_set_resolution(display,size.x,size.y);
        const int shortSide = size.x<size.y?size.x:size.y;
        MyFonts::apply(MyFonts::TextSize::NORMAL,shortSide,shortSide>=480?117:154,MyFonts::ScreenZoom::NORMAL);
        UiTouchTest::show(); settle();
        auto *menu=lv_screen_active();
        for(unsigned i=0;i<lv_obj_get_child_count(menu);++i) inside(lv_obj_get_child(menu,i),size.x,size.y);
        lv_area_t hintArea, menuButtonArea;
        lv_obj_get_coords(lv_obj_get_child(menu,4),&hintArea);
        lv_obj_get_coords(lv_obj_get_child(menu,1),&menuButtonArea);
        assert(hintArea.y2<menuButtonArea.y1);
        lv_obj_send_event(lv_obj_get_child(menu,1),LV_EVENT_CLICKED,nullptr);
        settle();
        auto *boardTest=lv_screen_active();
        for(unsigned i=0;i<lv_obj_get_child_count(boardTest);++i) inside(lv_obj_get_child(boardTest,i),size.x,size.y);
        const auto beforeMiss=UiTouchTest::s_sampleCount[0];
        const auto originalTarget=UiTouchTest::s_target;
        const auto originalPosition=UiTouchTest::s_position;
        auto *missed=lv_obj_get_child(boardTest,2);
        assert(strcmp(lv_label_get_text(lv_obj_get_child(missed,0)),tr(Str::TOUCH_TEST_UNREGISTERED))==0);
        lv_obj_send_event(missed,LV_EVENT_CLICKED,nullptr);
        assert(UiTouchTest::s_sampleCount[0]==beforeMiss+1);
        assert(UiTouchTest::s_samples[0][beforeMiss].unregistered);
        {
            FILE *csv=tmpfile();
            assert(csv && UiTouchTest::writeSampleCsv(csv,0,UiTouchTest::s_samples[0][beforeMiss]));
            rewind(csv);
            char row[512];
            assert(fgets(row,sizeof(row),csv));
            assert(fclose(csv)==0);
            std::vector<std::string> columns;
            std::string value;
            for(const char *p=row;*p;++p) {
                if(*p==',' || *p=='\n') { columns.push_back(value); value.clear(); }
                else value+=*p;
            }
            assert(columns.size()==27 && columns[15]=="unregistered");
            for(unsigned index : {2u,5u,6u,7u,8u,11u,12u,13u,14u,25u,26u})
                assert(columns[index].empty());
            assert(columns[0]=="board");
            assert(columns[1]==std::to_string(originalTarget));
        }
        assert(UiTouchTest::s_target==originalTarget && UiTouchTest::s_position==originalPosition);
        UiTouchTest::undo();
        assert(UiTouchTest::s_sampleCount[0]==beforeMiss);
        assert(UiTouchTest::s_target==originalTarget && UiTouchTest::s_position==originalPosition);
        lv_obj_send_event(missed,LV_EVENT_CLICKED,nullptr);
        auto *input=lv_indev_create();
        lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(input,display);
        lv_indev_data_t pointer{};
        lv_indev_set_user_data(input,&pointer);
        lv_indev_set_read_cb(input,[](lv_indev_t *in,lv_indev_data_t *data) {
            *data=*static_cast<lv_indev_data_t *>(lv_indev_get_user_data(in));
        });
        lv_area_t boardArea;
        lv_obj_get_coords(UiTouchTest::s_canvas,&boardArea);
        for(unsigned col=0;col<9;++col) {
            UiTouchTest::s_target=col;
            const auto before=UiTouchTest::s_sampleCount[0];
            pointer.point={boardArea.x1+int(col)*UiTouchTest::s_canvasW/9+4,boardArea.y1+4};
            pointer.state=LV_INDEV_STATE_PRESSED;
            lv_tick_inc(30); lv_indev_read(input);
            if(col==0) {
                pointer.point.x+=6; pointer.point.y+=8;
                lv_tick_inc(30); lv_indev_read(input);
                pointer.point.x-=6; pointer.point.y-=8;
            }
            pointer.state=LV_INDEV_STATE_RELEASED;
            lv_tick_inc(30); lv_indev_read(input);
            assert(UiTouchTest::s_sampleCount[0]==before+1);
            assert(UiTouchTest::s_samples[0][before].actual==col);
            assert(!UiTouchTest::s_pending);
            assert(UiTouchTest::s_pointerVisible);
            if(col==0) {
                assert(UiTouchTest::s_trailCount==3);
                assert(UiTouchTest::s_samples[0][before].maxMovePx==10);
                assert(UiTouchTest::s_samples[0][before].durationMs==60);
                lv_tick_inc(299); lv_timer_handler();
                assert(UiTouchTest::s_pointerVisible);
                lv_tick_inc(1); lv_timer_handler();
                assert(!UiTouchTest::s_pointerVisible);
            }
        }
        lv_indev_delete(input);
        const auto beforeLaterMiss=UiTouchTest::s_sampleCount[0];
        const auto laterTarget=UiTouchTest::s_target;
        const auto laterPosition=UiTouchTest::s_position;
        lv_obj_send_event(missed,LV_EVENT_CLICKED,nullptr);
        UiTouchTest::undo();
        assert(UiTouchTest::s_sampleCount[0]==beforeLaterMiss);
        assert(UiTouchTest::s_position==laterPosition && UiTouchTest::s_target==laterTarget);
        const auto partialCount=UiTouchTest::s_sampleCount[0];
        auto *header=lv_obj_get_child(boardTest,0);
        bool returned=false;
        for(unsigned i=0;i<lv_obj_get_child_count(header);++i) {
            auto *child=lv_obj_get_child(header,i);
            if(lv_obj_has_flag(child,LV_OBJ_FLAG_CLICKABLE) && lv_obj_get_width(child)==32) {
                lv_obj_send_event(child,LV_EVENT_CLICKED,nullptr);
                returned=true;
                break;
            }
        }
        assert(returned); settle();
        assert(UiTouchTest::s_page==UiTouchTest::Page::RESULTS);
        assert(UiTouchTest::s_sampleCount[0]==partialCount);
        auto *results=lv_screen_active();
        for(unsigned i=0;i<lv_obj_get_child_count(results);++i) inside(lv_obj_get_child(results,i),size.x,size.y);
        auto *save=lv_obj_get_child(results,3);
        assert(strcmp(lv_label_get_text(lv_obj_get_child(save,0)),tr(Str::TOUCH_TEST_SAVE))==0);
        lv_obj_send_event(save,LV_EVENT_CLICKED,nullptr); settle();
        assert(UiTouchTest::s_prompt);
        assert(UiTouchTest::s_sampleCount[0]==partialCount);
        UiTouchTest::erasePrompt();
        UiTouchTest::s_page=UiTouchTest::Page::MENU;
        UiTouchTest::show(); settle();
        menu=lv_screen_active();
        lv_obj_send_event(lv_obj_get_child(menu,2),LV_EVENT_CLICKED,nullptr);
        settle();
        auto *keyTest=lv_screen_active();
        for(unsigned i=0;i<lv_obj_get_child_count(keyTest);++i) inside(lv_obj_get_child(keyTest,i),size.x,size.y);
        missed=lv_obj_get_child(keyTest,2);
        const auto beforeKeyMiss=UiTouchTest::s_sampleCount[1];
        lv_obj_send_event(missed,LV_EVENT_CLICKED,nullptr);
        assert(UiTouchTest::s_sampleCount[1]==beforeKeyMiss+1);
        assert(UiTouchTest::s_samples[1][beforeKeyMiss].unregistered);
        assert(UiTouchTest::s_position==0);
        auto *done=lv_obj_get_child(keyTest,lv_obj_get_child_count(keyTest)-1);
        assert(strcmp(lv_label_get_text(lv_obj_get_child(done,0)),LV_SYMBOL_OK)==0);
        lv_obj_send_event(done,LV_EVENT_CLICKED,nullptr); settle();
        assert(UiTouchTest::s_page==UiTouchTest::Page::RESULTS);
        results=lv_screen_active();
        for(unsigned i=0;i<lv_obj_get_child_count(results);++i) inside(lv_obj_get_child(results,i),size.x,size.y);
        UiTouchTest::startPass(false); settle();
        UiTouchTest::s_targetCount=1;
        UiTouchTest::s_targets[0]=0;
        UiTouchTest::nextTarget();
        input=lv_indev_create();
        lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(input,display);
        lv_indev_set_user_data(input,&pointer);
        lv_indev_set_read_cb(input,[](lv_indev_t *in,lv_indev_data_t *data) {
            *data=*static_cast<lv_indev_data_t *>(lv_indev_get_user_data(in));
        });
        lv_obj_get_coords(UiTouchTest::s_canvas,&boardArea);
        pointer.point={boardArea.x1+UiTouchTest::s_expectedX,boardArea.y1+UiTouchTest::s_expectedY};
        pointer.state=LV_INDEV_STATE_PRESSED;
        lv_tick_inc(30); lv_indev_read(input);
        pointer.state=LV_INDEV_STATE_RELEASED;
        lv_tick_inc(30); lv_indev_read(input);
        assert(UiTouchTest::s_page==UiTouchTest::Page::TEST && UiTouchTest::s_position==1);
        lv_tick_inc(299); lv_timer_handler();
        assert(UiTouchTest::s_page==UiTouchTest::Page::TEST && UiTouchTest::s_pointerVisible);
        lv_tick_inc(1); lv_timer_handler();
        assert(UiTouchTest::s_page==UiTouchTest::Page::RESULTS && !UiTouchTest::s_pointerVisible);
        settle();
        assert(!UiTouchTest::s_feedbackTimer);
        lv_indev_delete(input);
        UiTouchTest::s_page=UiTouchTest::Page::MENU;
        UiTouchTest::show(); settle();
        menu=lv_screen_active();
        lv_obj_send_event(lv_obj_get_child(menu,3),LV_EVENT_CLICKED,nullptr); settle();
        assert(UiTouchTest::s_fullScreen && UiTouchTest::modeIndex()==2);
        assert(lv_obj_get_child_count(lv_screen_active())==1);
        lv_obj_get_coords(UiTouchTest::s_canvas,&boardArea);
        assert(boardArea.x1==0 && boardArea.y1==0 && boardArea.x2==size.x-1 && boardArea.y2==size.y-1);
        input=lv_indev_create();
        lv_indev_set_type(input,LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(input,display);
        lv_indev_set_user_data(input,&pointer);
        lv_indev_set_read_cb(input,[](lv_indev_t *in,lv_indev_data_t *data) {
            *data=*static_cast<lv_indev_data_t *>(lv_indev_get_user_data(in));
        });
        for(auto corner : {lv_point_t{0,0},lv_point_t{size.x-1,0},
                            lv_point_t{0,size.y-1},lv_point_t{size.x-1,size.y-1}}) {
            const int target=TouchTest::screenCellAtPoint(corner.x,corner.y,size.x,size.y);
            UiTouchTest::s_target=target;
            const auto before=UiTouchTest::s_sampleCount[2];
            pointer.point=corner;
            pointer.state=LV_INDEV_STATE_PRESSED;
            lv_tick_inc(30); lv_indev_read(input);
            if(corner.x==0 && corner.y==0) {
                lv_tick_inc(600); lv_indev_read(input);
                assert(!UiTouchTest::s_prompt);
            }
            pointer.state=LV_INDEV_STATE_RELEASED;
            lv_tick_inc(30); lv_indev_read(input);
            assert(UiTouchTest::s_sampleCount[2]==before+1);
            assert(UiTouchTest::s_samples[2][before].actual==target);
            assert(UiTouchTest::s_samples[2][before].fullScreen);
        }
        const auto beforeMenu=UiTouchTest::s_sampleCount[2];
        const auto positionBeforeMenu=UiTouchTest::s_position;
        pointer.point={size.x/2,size.y/2};
        pointer.state=LV_INDEV_STATE_PRESSED;
        lv_tick_inc(30); lv_indev_read(input);
        lv_tick_inc(600); lv_indev_read(input);
        assert(UiTouchTest::s_prompt && UiTouchTest::s_pending);
        pointer.state=LV_INDEV_STATE_RELEASED;
        lv_tick_inc(30); lv_indev_read(input);
        assert(UiTouchTest::s_sampleCount[2]==beforeMenu);
        auto *controls=lv_obj_get_child(UiTouchTest::s_prompt,0);
        for(unsigned i=0;i<lv_obj_get_child_count(controls);++i) inside(lv_obj_get_child(controls,i),size.x,size.y);
        lv_obj_send_event(lv_obj_get_child(controls,1),LV_EVENT_CLICKED,nullptr);
        assert(UiTouchTest::s_sampleCount[2]==beforeMenu+1);
        assert(UiTouchTest::s_samples[2][beforeMenu].unregistered);
        assert(UiTouchTest::s_position==positionBeforeMenu);
        pointer.state=LV_INDEV_STATE_PRESSED;
        lv_tick_inc(30); lv_indev_read(input);
        lv_tick_inc(600); lv_indev_read(input);
        assert(UiTouchTest::s_prompt);
        pointer.state=LV_INDEV_STATE_RELEASED;
        lv_tick_inc(30); lv_indev_read(input);
        controls=lv_obj_get_child(UiTouchTest::s_prompt,0);
        lv_obj_send_event(lv_obj_get_child(controls,3),LV_EVENT_CLICKED,nullptr); settle();
        assert(UiTouchTest::s_page==UiTouchTest::Page::RESULTS);
        lv_indev_delete(input);
        UiTouchTest::s_page=UiTouchTest::Page::MENU;
        UiTouchTest::show(); settle();
        printf("Touch diagnostic layout passed: %dx%d\n",size.x,size.y);
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
add_executable(sudoku_ui test.cpp "{ROOT}/main/sudoku_game.cpp" "{ROOT}/main/sudoku_sd.cpp"
 "{ROOT}/main/ui_metrics.cpp" "{ROOT}/main/hw_profile.cpp"
 "{ROOT}/components/my_ui/my_fonts.cpp" {fonts}
 "{ROOT}/main/lang/lang.cpp" "{ROOT}/main/lang/lang_sv.cpp" "{ROOT}/main/lang/lang_en.cpp")
target_include_directories(sudoku_ui PRIVATE "{WORK}" "{ROOT}/tests/player_stubs"
 "{ROOT}/main" "{ROOT}/main/lang" "{ROOT}/components/my_ui/include"
 "{ROOT}/components/my_wifi/include" "{WORK}")
target_compile_options(sudoku_ui PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Werror;-Wno-error=deprecated-declarations>)
target_link_libraries(sudoku_ui PRIVATE lvgl m)
""")
subprocess.run(["cmake", "-S", str(WORK), "-B", str(WORK / "build"), "-G", "Ninja"], check=True)
subprocess.run(["cmake", "--build", str(WORK / "build"), "--target", "sudoku_ui", "-j", "2"], check=True)
subprocess.run([str(WORK / "build/sudoku_ui")], check=True)
