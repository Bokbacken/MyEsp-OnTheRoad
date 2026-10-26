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
code = r'''
#include <cassert>
#include <cstdio>
#include "lvgl.h"
#include "my_theme.h"
#include "lang.h"
static lv_obj_t *dialog(const char *text) {
    auto *mb = lv_msgbox_create(nullptr);
    lv_msgbox_add_text(mb, text);
    return mb;
}
static lv_obj_t *makeDialog(bool won, int level) {
BODY
    return mb;
}
int main() {
    lv_init();
    auto *display = lv_display_create(480, 320);
    lv_display_set_dpi(display, 130);
    int checked = 0;
    const char *oldLabels[2][3]={{"Nästa bana","Börja om","Lämna"},{"Next stage","Restart","Leave"}};
    for (int language=0; language<2; ++language) {
        Lang::setLang(language == 0 ? "sv" : "en");
        for (int scenario=0; scenario<3; ++scenario) {
            bool won = scenario != 2;
            int level = scenario == 1 ? 12 : 1;
            int count = won && level < 12 ? 3 : 2;
            auto *baseline = dialog("Test");
            for(int i=0;i<count;++i) lv_msgbox_add_footer_button(baseline,oldLabels[language][i+(count==2)]);
            Theme::msgboxFonts(baseline);
            lv_obj_update_layout(baseline);
            int oldWidths[3], oldHeights[3];
            for(int i=0;i<count;++i) {
                auto *oldButton=lv_obj_get_child(lv_msgbox_get_footer(baseline),i);
                oldWidths[i]=lv_obj_get_width(oldButton); oldHeights[i]=lv_obj_get_height(oldButton);
            }
            lv_msgbox_close(baseline);
            auto *mb=makeDialog(won,level);
            lv_obj_update_layout(mb);
            assert(lv_obj_get_x(mb)>=0 && lv_obj_get_x(mb)+lv_obj_get_width(mb)<=480);
            auto *footer=lv_msgbox_get_footer(mb);
            assert(lv_obj_get_child_count(footer)==unsigned(count));
            for(int i=0;i<count;++i) {
                auto *button=lv_obj_get_child(footer,i);
                auto *label=lv_obj_get_child(button,0);
                int oldWidth=oldWidths[i], oldHeight=oldHeights[i];
                assert(lv_obj_get_x(button)>=0);
                assert(lv_obj_get_x(button)+lv_obj_get_width(button)<=lv_obj_get_content_width(footer));
                if(i) {
                    auto *prev=lv_obj_get_child(footer,i-1);
                    assert(lv_obj_get_x(prev)+lv_obj_get_width(prev)<=lv_obj_get_x(button));
                }
                lv_point_t size;
                lv_text_get_size(&size,lv_label_get_text(label),lv_obj_get_style_text_font(label,LV_PART_MAIN),
                    lv_obj_get_style_text_letter_space(label,LV_PART_MAIN),lv_obj_get_style_text_line_space(label,LV_PART_MAIN),
                    LV_COORD_MAX,LV_TEXT_FLAG_NONE);
                int available=lv_obj_get_content_width(button);
                printf("språk=%d scenario=%d %s: text=%d px, utrymme=%d px, knapp=%dx%d (tidigare %dx%d)\n",
                    language,scenario,lv_label_get_text(label),int(size.x),available,
                    int(lv_obj_get_width(button)),int(lv_obj_get_height(button)),oldWidth,oldHeight);
                fflush(stdout);
                assert(size.x<=available && size.x<=lv_obj_get_width(label));
                assert(size.y<=lv_obj_get_content_height(button));
                assert(lv_obj_get_width(button)>=oldWidth && lv_obj_get_height(button)>=oldHeight);
                ++checked;
            }
            lv_msgbox_close(mb);
        }
    }
    printf("Godkända text-/tryckytekontroller: %d\n",checked);
    lv_display_delete(display);
    lv_deinit();
}
'''.replace("BODY", body)
(WORK / "test.cpp").write_text(code)
(WORK / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.22)
project(mine_dialog_layout LANGUAGES C CXX)
set(LV_CONF_SKIP ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_compile_definitions(LV_CONF_SKIP LV_COLOR_DEPTH=16 LV_DPI_DEF=130)
add_subdirectory("{ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(layout test.cpp "{ROOT}/components/my_ui/fonts/my_font_18.c"
 "{ROOT}/main/lang/lang.cpp" "{ROOT}/main/lang/lang_sv.cpp" "{ROOT}/main/lang/lang_en.cpp")
target_include_directories(layout PRIVATE "{ROOT}/components/my_ui/include" "{ROOT}/main/lang")
target_link_libraries(layout PRIVATE lvgl m)
''')
subprocess.run(["cmake", "-S", str(WORK), "-B", str(WORK / "build"), "-G", "Ninja", "-DCMAKE_CXX_FLAGS="], check=True)
subprocess.run(["cmake", "--build", str(WORK / "build"), "--target", "layout", "-j", "2"], check=True)
subprocess.run([str(WORK / "build/layout")], check=True)
