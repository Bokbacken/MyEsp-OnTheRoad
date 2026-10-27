#!/usr/bin/env python3
"""Riktat LVGL-layouttest: verklig dialogkod/font, utan ESP32 eller bildskärm.
Kör: python3 tests/test_mine_dialog.py [extern byggkatalog]
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
WORK = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(tempfile.mkdtemp(prefix="mine-dialog-"))
WORK.mkdir(parents=True, exist_ok=True)
source = (ROOT / "main/ui_adventure.cpp").read_text()
body = source.split("static void mineResult()\n{", 1)[1].split("static void showGameOver()", 1)[0]
body = body[body.index("    auto *mb=dialog("):].rsplit("}", 1)[0]
body, callbacks = re.subn(r"lv_obj_add_event_cb\(.*?},LV_EVENT_CLICKED,nullptr\);", "", body, flags=re.S)
assert callbacks == 3, "Förväntade tre oförändrade callbackblock"
body = body.replace("s_world->mineLevel()", "level")
assert "s_world" not in body and "s_session" not in body
resume = source.split("static void playerChosen(lv_event_t *e)\n{",1)[1].split("static void addPlayerRow",1)[0]
resume = resume[resume.index("    lv_obj_t *mb = dialog("):].rsplit("}",1)[0]
resume, count = re.subn(r"lv_obj_add_event_cb\(.*?},\s*LV_EVENT_CLICKED,\s*nullptr\);", "", resume, flags=re.S)
assert count == 3
profile = source.split("static AdvUi::NameForm s_nameForm;",1)[1].split("static void playerChosen",1)[0]
code = r'''
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "lvgl.h"
#include "my_theme.h"
#include "lang.h"
#include "adv_setup_ui.h"
#include "app_players.h"
#include "lang/lang_game.h"
#include <string>
#include <unistd.h>
#include <cstdlib>
static bool storageMounted=true;
static std::string storageBase;
namespace MyFat {
bool mounted(){return storageMounted;}
void path(char*out,size_t n,const char*rel){snprintf(out,n,"%s%s",storageBase.c_str(),rel);}
}
static lv_obj_t *s_activeScr;
static uint16_t s_playerId;
static int refreshes;
static AdvUi::NameForm s_nameForm;
PROFILE
static void fillPlayerList() { ++refreshes; }
static lv_obj_t *dialog(const char *text) {
    auto *mb = lv_msgbox_create(nullptr);
    lv_msgbox_add_text(mb, text);
    return mb;
}
static lv_obj_t *makeDialog(bool won, int level) {
BODY
    return mb;
}
static lv_obj_t *makeResume() {
RESUME
    return mb;
}
static void inside(lv_obj_t *obj, const lv_area_t &limit) {
    lv_area_t a; lv_obj_get_coords(obj,&a);
    if (!(a.x1>=limit.x1 && a.y1>=limit.y1 && a.x2<=limit.x2 && a.y2<=limit.y2)) {
        printf("UTANFÖR objekt=%s coords=%d,%d,%d,%d ram=%d,%d,%d,%d\n",lv_obj_check_type(obj,&lv_label_class)?lv_label_get_text(obj):"kontroll",int(a.x1),int(a.y1),int(a.x2),int(a.y2),int(limit.x1),int(limit.y1),int(limit.x2),int(limit.y2)); fflush(stdout);
    }
    assert(a.x1>=limit.x1 && a.y1>=limit.y1 && a.x2<=limit.x2 && a.y2<=limit.y2);
}
static void dialogTree(lv_obj_t *obj, const lv_area_t &limit) {
    inside(obj,limit);
    lv_area_t area; lv_obj_get_coords(obj,&area);
    for(unsigned i=0;i<lv_obj_get_child_count(obj);++i) dialogTree(lv_obj_get_child(obj,i),area);
}
int main() {
    lv_init();
    auto *display = lv_display_create(320, 480);
    lv_display_set_dpi(display, 130);
    static uint8_t pixels[320*20*2];
    lv_display_set_buffers(display,pixels,nullptr,sizeof(pixels),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,[](lv_display_t*d,const lv_area_t*,uint8_t*){lv_display_flush_ready(d);});
    char dir[]="/tmp/player-ui-XXXXXX"; storageBase=mkdtemp(dir);
    assert(Players::begin());
    s_activeScr=lv_screen_active();
    int checked = 0;
    for (const lv_point_t size : {lv_point_t{320,480}, lv_point_t{480,800}}) {
        lv_display_set_resolution(display,size.x,size.y);
        lv_obj_set_size(s_activeScr,size.x,size.y);
        lv_area_t screen={0,0,size.x-1,size.y-1};
        for (int language=0; language<2; ++language) {
            Lang::setLang(language == 0 ? "sv" : "en");
            for (int scenario=0; scenario<4; ++scenario) {
                bool won = scenario != 2;
                int level = scenario == 1 ? 12 : 1;
                int count = won && level < 12 ? 3 : 2;
                auto *mb=scenario==3?makeResume():makeDialog(won,level);
                lv_obj_update_layout(mb);
                dialogTree(mb,screen);
                auto *footer=lv_msgbox_get_footer(mb);
                assert(lv_obj_get_child_count(footer)==unsigned(count));
                assert(lv_obj_get_width(mb)==(size.x==320?296:444));
                printf("portrait %dx%d språk=%d scenario=%d dialog=%dx%d\n",size.x,size.y,language,scenario,
                    int(lv_obj_get_width(mb)),int(lv_obj_get_height(mb)));
                for(int i=0;i<count;++i) {
                    auto *button=lv_obj_get_child(footer,i);
                    auto *label=lv_obj_get_child(button,0);
                    if(i) {
                        auto *prev=lv_obj_get_child(footer,i-1);
                        assert(lv_obj_get_y(prev)+lv_obj_get_height(prev)<=lv_obj_get_y(button));
                    }
                    lv_point_t textSize;
                    lv_text_get_size(&textSize,lv_label_get_text(label),lv_obj_get_style_text_font(label,LV_PART_MAIN),
                        lv_obj_get_style_text_letter_space(label,LV_PART_MAIN),lv_obj_get_style_text_line_space(label,LV_PART_MAIN),
                        LV_COORD_MAX,LV_TEXT_FLAG_NONE);
                    printf("  %s text=%d knapp=%dx%d\n",lv_label_get_text(label),int(textSize.x),
                        int(lv_obj_get_width(button)),int(lv_obj_get_height(button)));
                    assert(textSize.x<=lv_obj_get_content_width(button) && textSize.x<=lv_obj_get_width(label));
                    assert(textSize.y<=lv_obj_get_content_height(button));
                    assert(lv_obj_get_width(button)>=(size.x==320?144:200) && lv_obj_get_height(button)>=(size.x==320?44:66));
                    ++checked;
                }
                lv_msgbox_close(mb);
            }
            AdvUi::NameForm f;
            AdvUi::nameForm(f,lv_screen_active(),[](lv_event_t*){},[](lv_event_t*){});
            lv_label_set_text(f.error,tr(Str::PERR_STORAGE));
            lv_obj_update_layout(f.root);
            dialogTree(f.root,screen);
            auto *saveButton=lv_obj_get_child(f.root,3);
            auto *cancelButton=lv_obj_get_child(f.root,4);
            inside(saveButton,screen); inside(cancelButton,screen);
            assert(lv_obj_get_y(saveButton)+lv_obj_get_height(saveButton)<=lv_obj_get_y(f.keyboard));
            assert(lv_obj_get_y(cancelButton)+lv_obj_get_height(cancelButton)<=lv_obj_get_y(f.keyboard));
            for(const char *name:{"", "Anna", "ABCDEFGHIJKLMNOPQRSTUVW"}) {
                lv_textarea_set_text(f.input,name);
                lv_obj_update_layout(f.root);
                inside(f.input,screen); inside(f.keyboard,screen);
                ++checked;
            }
            assert(lv_obj_get_y(f.keyboard)==AdvUi::scale(240));
            assert(lv_obj_get_height(f.keyboard)==size.y-AdvUi::scale(240));
            printf("Namnruta/tangentbord och feltext inom portrait %dx%d språk=%d\n",size.x,size.y,language);
            lv_obj_delete(f.root);

            std::string longText;
            for(int i=0;i<140;++i) longText += language==0
                ? "En lång svensk dialogtext som ska kunna läsas genom att rulla innehållet. "
                : "A long English dialog message that must remain readable by scrolling its content. ";
            auto *longDialog=lv_msgbox_create(nullptr);
            lv_msgbox_add_text(longDialog,longText.c_str());
            auto *action=lv_msgbox_add_footer_button(longDialog,tr(Str::BTN_CANCEL));
            AdvUi::dialogLayout(longDialog);
            lv_obj_update_layout(longDialog);
            inside(longDialog,screen);
            auto *footer=lv_msgbox_get_footer(longDialog);
            inside(footer,screen);
            lv_area_t footerArea; lv_obj_get_coords(footer,&footerArea);
            inside(action,footerArea);
            assert(lv_obj_get_width(longDialog)<=AdvUi::dialogWidth());
            assert(lv_obj_get_scroll_bottom(lv_msgbox_get_content(longDialog))>0);
            lv_obj_scroll_to_y(lv_msgbox_get_content(longDialog),LV_COORD_MAX,LV_ANIM_OFF);
            assert(lv_obj_get_scroll_y(lv_msgbox_get_content(longDialog))>0);
            ++checked;
            lv_msgbox_close(longDialog);
        }
    }
    // Exercise the actual adventure handlers and Players API, not a duplicated save flow.
    lv_display_set_resolution(display,320,480);
    lv_obj_set_size(s_activeScr,320,480);
    auto clickSave=[](){ lv_tick_inc(501); lv_obj_send_event(lv_obj_get_child(s_nameForm.root,3),LV_EVENT_CLICKED,nullptr); };
    lv_tick_inc(501); newPlayer(nullptr);
    clickSave(); assert(s_nameForm.root && s_playerId==0 && Players::count()==0);
    lv_textarea_set_text(s_nameForm.input,"Anna"); clickSave();
    assert(!s_nameForm.root && s_playerId==1 && refreshes==1 && Players::findByName("Anna")==1);
    lv_timer_handler();
    lv_tick_inc(501); newPlayer(nullptr);
    lv_textarea_set_text(s_nameForm.input," anna "); clickSave();
    assert(s_nameForm.root && Players::count()==1 && refreshes==1);
    lv_textarea_set_text(s_nameForm.input,"Bob"); storageMounted=false; clickSave();
    assert(s_nameForm.root && s_playerId==1 && Players::count()==1 && refreshes==1);
    storageMounted=true; clickSave();
    assert(!s_nameForm.root && s_playerId==2 && Players::count()==2 && refreshes==2);
    lv_timer_handler();
    lv_tick_inc(501); newPlayer(nullptr);
    lv_textarea_set_text(s_nameForm.input,"Cancelled");
    lv_tick_inc(501); lv_obj_send_event(lv_obj_get_child(s_nameForm.root,4),LV_EVENT_CLICKED,nullptr);
    assert(!s_nameForm.root && Players::count()==2 && s_playerId==2);
    lv_timer_handler();
    for(auto path:{"/players.bin","/players.bin.bak"}) remove((storageBase+path).c_str());
    rmdir(storageBase.c_str());
    printf("Spelar-UI: tomt/dubblett/fel/lyckat/avbryt och valt ID/listuppdatering godkända\n");
    printf("Godkända text-/tryckytekontroller: %d\n",checked);
    lv_display_delete(display);
    lv_deinit();
}
'''.replace("BODY", body).replace("PROFILE", profile).replace("RESUME", resume)
(WORK / "test.cpp").write_text(code)
(WORK / "sdkconfig.h").write_text("")
(WORK / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.22)
project(mine_dialog_layout LANGUAGES C CXX)
set(LV_CONF_SKIP ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_compile_definitions(LV_CONF_SKIP LV_COLOR_FORMAT_DEFAULT=LV_COLOR_FORMAT_RGB565 LV_DPI_DEF=130)
add_subdirectory("{ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(layout test.cpp "{ROOT}/components/my_ui/fonts/my_font_18.c" "{ROOT}/components/my_ui/fonts/my_font_14.c"
 "{ROOT}/main/app_players.cpp" "{ROOT}/main/lang/lang.cpp" "{ROOT}/main/lang/lang_sv.cpp" "{ROOT}/main/lang/lang_en.cpp"
 "{ROOT}/main/ui_metrics.cpp" "{ROOT}/main/hw_profile.cpp")
target_include_directories(layout PRIVATE "{WORK}" "{ROOT}/tests/player_stubs" "{ROOT}/components/my_ui/include" "{ROOT}/main/lang" "{ROOT}/main")
target_link_libraries(layout PRIVATE lvgl m)
''')
subprocess.run(["cmake", "-S", str(WORK), "-B", str(WORK / "build"), "-G", "Ninja", "-DCMAKE_CXX_FLAGS="], check=True)
subprocess.run(["cmake", "--build", str(WORK / "build"), "--target", "layout", "-j", "2"], check=True)
subprocess.run([str(WORK / "build/layout")], check=True)
