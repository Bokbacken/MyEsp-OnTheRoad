#!/usr/bin/env python3
"""Host-check production Adventure art, framebuffer, actions, movement and sound dispatch.

No LVGL/ESP-IDF needed: only LVGL labels/hardware are stubbed. Uses the real World.
Run: python3 tests/test_adv_s2_ui.py [repository-relative build directory]
"""
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
WORK = ROOT / (sys.argv[1] if len(sys.argv) > 1 else "build-adv-s2-ui")
WORK.mkdir(parents=True, exist_ok=True)
source = (ROOT / "main/ui_adventure.cpp").read_text()


def section(start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


code = r'''
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#include "adv_game_geometry.h"
#include "otr_world.h"
#include "otr_joystick.h"
#include "otr_story.h"
#include "sound_bank.h"
#include "lang/lang.h"
using Otr::Dir;
using Otr::Obj;
using Otr::Tile;
namespace Game = AdvUi::Game;
struct lv_obj_t { std::string text; bool hidden = false; uint32_t color = 0; };
static void lv_label_set_text(lv_obj_t *o, const char *s) { o->text = s; }
static void lv_label_set_text_fmt(lv_obj_t *o, const char *fmt, ...) {
    char text[256]; va_list ap; va_start(ap, fmt); vsnprintf(text, sizeof text, fmt, ap); va_end(ap);
    o->text = text;
}
static void lv_obj_set_style_text_color(lv_obj_t *, uint32_t, int) {}
static void lv_obj_set_style_bg_color(lv_obj_t *, uint32_t, int) {}
static void lv_obj_set_style_border_color(lv_obj_t *, uint32_t, int) {}
static void lv_obj_set_state(lv_obj_t *, int, bool) {}
static void lv_obj_set_hidden(lv_obj_t *o, bool hide) { o->hidden = hide; }
static lv_obj_t *lv_obj_get_child(lv_obj_t *o, int) { return o; }
static void lv_bar_set_range(lv_obj_t *, int, int) {}
static void lv_bar_set_value(lv_obj_t *, int, int) {}
enum { LV_STATE_DISABLED, LV_ANIM_OFF, LV_PART_MAIN };
namespace Theme {
constexpr uint32_t TEXT=1, MUTED=2, ACCENT=3, GOLD=4, TEAL=5, GREEN=6, RED=7, BLUE=8, LINE=9;
static uint32_t c(uint32_t n) { return n; }
static lv_obj_t inventory[6];
static int inventoryCount;
static lv_obj_t *label(lv_obj_t *, const char *, const int *, uint32_t color) {
    assert(inventoryCount<6);
    auto *o=&inventory[inventoryCount++]; o->color=color; return o;
}
}
static int my_font_14;
namespace Rgb { static void effectFlash(int, int, int, int) {} }
static uint32_t clockMs=1000;
static uint32_t nowMs() { return clockMs; }
static int64_t esp_timer_get_time() { return clockMs * 1000; }
/*GEOMETRY*/
/*SPRITES*/
static Sprites sprites, *s_spr=&sprites;
static Otr::World world, *s_world=&world;
static uint16_t *s_fb;
static int s_ts=32, s_fromX, s_fromY;
static uint32_t s_moveMs, s_moveT0, s_bumpT0, s_fxT0, s_renderUs;
static bool s_rockAnim, s_dirty;
static int s_rockFromX, s_rockFromY, s_rockToX, s_rockToY, s_bumpDx, s_bumpDy, s_fxX=-1, s_fxY=-1;
static int s_bombSize, s_bombX=-1, s_bombY=-1, s_talkNum, s_pathPos=-1, s_walkTalkNum;
static Otr::JoyController s_joy;
static const Otr::Story::Script *s_story;
static lv_obj_t *s_talkPanel;
static lv_obj_t labelObjects[32];
enum HudCount { C_LIVES, C_SUIT, C_KEYS, C_GEMS, C_COUNT };
static lv_obj_t *s_count[C_COUNT]={labelObjects,labelObjects+1,labelObjects+2,labelObjects+3};
static lv_obj_t *s_stageLabel=labelObjects+4, *s_progressLabel=labelObjects+5;
static lv_obj_t *s_item[H_COUNT]={labelObjects+6,labelObjects+7,labelObjects+8,labelObjects+9,
                                labelObjects+10,labelObjects+11,labelObjects+12};
static lv_obj_t *s_flashCell=labelObjects+13, *s_pogoCell=labelObjects+14;
static lv_obj_t *s_flashLvl=labelObjects+15, *s_pogoLvl=labelObjects+16;
static lv_obj_t *s_bar=labelObjects+17, *s_barLbl=labelObjects+18;
static lv_obj_t *s_hitBtn=labelObjects+19, *s_swapBtn=labelObjects+20;
static int s_prevFlash, s_prevPogo, s_actSel, s_actShown=-1;
static int langIdx() { return strcmp(Lang::currentLang(),"en")==0; }
static const char *advMapTitle() { return "A very long title does not contain any critical statistics"; }
static int results, deaths, talks;
static void stopPath() { s_pathPos=-1; s_walkTalkNum=0; }
static void joyShow(bool, int, int) {}
static void showGameOver() { ++deaths; }
static void startTalk(int) { ++talks; }
static void adventureResult() { ++results; }
static bool busy(uint32_t now) { return s_moveMs && now-s_moveT0<s_moveMs; }
/*SNAP*/
/*ART*/
/*RENDER*/
/*HUD*/
static void toast(const char *, uint32_t=Theme::TEXT, uint32_t=2200) {}
static void eventToasts(uint32_t) {}
static void bombAction() {}
namespace Audio {
static int s_sfxGain=256;
static constexpr int CMD_SFX=1;
static std::vector<Sound::Sfx> heard;
static void post(int command, uint8_t sound) {
    assert(command==CMD_SFX); heard.push_back(Sound::Sfx(sound));
}
/*AUDIO_PLAY*/
}
/*SOUND*/
static struct SdStub { bool active() const { return false; } } s_sd;
static void sdExitAdvance() { assert(false); }
/*MOVE*/
/*ACTION*/
/*PERFORMANCE*/
static void inventoryMenu() {
    lv_obj_t *c=nullptr; Theme::inventoryCount=0;
/*INVENTORY*/
}

static std::vector<std::string> rows;
static void load(int par=0) {
    rows.assign(10,std::string(14,'.'));
    for (int x=0;x<14;++x) rows[0][x]=rows[9][x]='#';
    for (int y=0;y<10;++y) rows[y][0]=rows[y][13]='#';
    rows[1][1]='S';
    const char *ptr[10];
    for (int y=0;y<10;++y) ptr[y]=rows[y].c_str();
    assert(world.load(Otr::MapDef{90,ptr,10,Otr::CLASSIC_RULES,nullptr,0,nullptr,0,nullptr,0,par}));
    snapPlayer(); s_pathPos=-1; s_joy.cancel(); Audio::heard.clear(); results=deaths=talks=0;
}
static void reload(int par=0) {
    const char *ptr[10]; for (int y=0;y<10;++y) ptr[y]=rows[y].c_str();
    assert(world.load(Otr::MapDef{90,ptr,10,Otr::CLASSIC_RULES,nullptr,0,nullptr,0,nullptr,0,par}));
    snapPlayer(); Audio::heard.clear();
}
static void soundTests() {
    using Sound::Sfx;
    for (auto mask : {uint32_t(Otr::EV_LEVER), uint32_t(Otr::EV_GATE_CLOSE),
                      uint32_t(Otr::EV_LEVER|Otr::EV_GATE_OPEN|Otr::EV_GATE_CLOSE|Otr::EV_MOVED),
                      uint32_t(Otr::EV_EXIT|Otr::EV_KEY|Otr::EV_GATE_CLOSE|Otr::EV_MOVED),
                      uint32_t(Otr::EV_GAME_OVER|Otr::EV_DIED|Otr::EV_LEVER)}) {
        Audio::heard.clear(); Audio::s_sfxGain=256; playEvents(mask);
        assert(Audio::heard.size()==1);
        if (mask&Otr::EV_GAME_OVER) assert(Audio::heard[0]==Sfx::GAME_OVER);
        else if (mask&Otr::EV_EXIT) assert(Audio::heard[0]==Sfx::EXIT);
        else if (mask&(Otr::EV_GATE_CLOSE|Otr::EV_GATE_OPEN)) assert(Audio::heard[0]==Sfx::DOOR);
        else assert(Audio::heard[0]==Sfx::CLICK);
        Audio::heard.clear(); Audio::s_sfxGain=0; playEvents(mask); assert(Audio::heard.empty());
    }
    Audio::s_sfxGain=256; playEvents(0); playEvents(Otr::EV_TALK); assert(Audio::heard.empty());
}
static void actionTests() {
    load(); rows[2][1]=char(Otr::GROUPED_LEVER_GLYPH); reload();
    assert(!world.leverOnAt(1,2)); // Initial facing is DOWN.
    s_actSel=ACT_HIT; doSwap();
    assert(s_actSel==ACT_LEVER && !world.leverOnAt(1,2) && Audio::heard.empty());
    const int steps=world.steps();
    doHit(); assert(world.leverOnAt(1,2) && world.steps()==steps && Audio::heard.size()==1);
    Audio::heard.clear(); doHit(); assert(!world.leverOnAt(1,2) && Audio::heard.size()==1);
    Audio::s_sfxGain=0; Audio::heard.clear(); doHit();
    assert(world.leverOnAt(1,2) && Audio::heard.empty()); Audio::s_sfxGain=256;
    // Holding/turning the joystick does not use the selected action.
    doMove(Dir::DOWN); assert(world.leverOnAt(1,2));
}
static void movementTests() {
    load(4);
    rows[1][2]=char(Otr::BELT_UP_GLYPH+1); // right, down, left: final cell diagonally from origin
    rows[1][3]=char(Otr::BELT_UP_GLYPH+2);
    rows[2][3]=char(Otr::BELT_UP_GLYPH+3); reload(4);
    auto ev=doMove(Dir::RIGHT);
    assert((ev&Otr::EV_MOVED) && world.steps()==4 && world.playerX()==2 && world.playerY()==2);
    assert(s_moveMs==0 && Audio::heard.size()==1);
    assert(s_progressLabel->text.find("4")!=std::string::npos);
    char text[96]; performanceText(text,sizeof text); assert(strstr(text,"Equal")||strstr(text,"Lika"));
    // Check an ordinary single step keeps animation; forced chains never shortcut through walls.
    load(); doMove(Dir::DOWN); assert(s_moveMs>0);
    load(); rows[1][2]=char(Otr::ICE_GLYPH); rows[1][3]='X'; reload();
    s_pathPos=3; s_joy.press(clockMs,70,0);
    ev=doMove(Dir::RIGHT);
    assert(ev&Otr::EV_EXIT); assert(s_pathPos==-1 && !s_joy.held() && results==1 && s_moveMs==0);
    const int steps=world.steps(); doMove(Dir::LEFT); doHit(); assert(world.steps()==steps);
    load(); rows[1][2]=char(Otr::BELT_UP_GLYPH+1); rows[1][3]='^'; reload();
    s_pathPos=2; s_joy.press(clockMs,70,0);
    ev=doMove(Dir::RIGHT);
    assert(ev&Otr::EV_DIED); assert(s_pathPos==-1 && !s_joy.held() && s_moveMs==0);
    load(5); doMove(Dir::DOWN); performanceText(text,sizeof text); assert(strstr(text,"Under"));
    load(1); world.move(Dir::DOWN); world.move(Dir::DOWN);
    performanceText(text,sizeof text); assert(strstr(text,"Over")||strstr(text,"Över"));
    // Exceeding par is an assessment, never a win gate.
    load(1); rows[1][3]='X'; reload(1);
    world.move(Dir::RIGHT); ev=doMove(Dir::RIGHT);
    assert((ev&Otr::EV_EXIT) && results==1 && !world.parMet());
    load(); performanceText(text,sizeof text); assert(strstr(text,"Par: -"));
    for (int c=0;c<6;++c) world.setKeys(Otr::KeyColor(c),10+c);
    inventoryMenu(); assert(Theme::inventoryCount==6);
    for (int c=0;c<6;++c) {
        char expected[32]; snprintf(expected,sizeof expected,"): %d",10+c);
        assert(Theme::inventory[c].text.find(expected)!=std::string::npos);
        snprintf(expected,sizeof expected," %d (",c+1);
        assert(Theme::inventory[c].text.find(expected)!=std::string::npos);
        assert(Theme::inventory[c].color==KEY_COLORS[c]);
    }
}
static void rendererTests() {
    makeArt(s_spr);
    for (auto size : {Game::Point{320,480},Game::Point{480,800}}) for (int ts : {16,32}) {
        s_layout=Game::layout(size.x,size.y); s_ts=ts; s_fbStride=size.x+3;
        const size_t count=s_fbStride*s_layout.view.h;
        std::vector<uint16_t> fb(count+2,0xbeef); s_fb=fb.data()+1;
        auto draw=[&] {
            std::fill(s_fb,s_fb+count,0xbeef);
            render(clockMs,0,0,world.playerX()*ts,world.playerY()*ts,0);
            assert(fb.front()==0xbeef && fb.back()==0xbeef);
            for (int y=0;y<s_layout.view.h;++y)
                for (size_t x=size.x;x<s_fbStride;++x) assert(s_fb[y*s_fbStride+x]==0xbeef);
        };
        auto cell=[&](int x,int y) {
            std::vector<uint16_t> out;
            for(int j=0;j<ts;++j) for(int i=0;i<ts;++i) out.push_back(s_fb[(y*ts+j)*s_fbStride+x*ts+i]);
            return out;
        };
        load(); rows[3][2]=char(Otr::ICE_GLYPH); rows[3][3]=char(Otr::GROUPED_LEVER_GLYPH);
        for(int d=0;d<4;++d) rows[3][4+d]=char(Otr::BELT_UP_GLYPH+d);
        for(int c=0;c<6;++c) {
            rows[5][2+c]=char(Otr::COLORED_KEY_GLYPH+c);
            rows[7][2+c]=char(Otr::COLORED_DOOR_GLYPH+c);
        }
        reload(); draw();
        std::set<std::vector<uint16_t>> terrain, keys, doors, badges;
        for(int x=2;x<8;++x) { terrain.insert(cell(x,3)); keys.insert(cell(x,5)); doors.insert(cell(x,7)); }
        if (terrain.size()!=6 || keys.size()!=6 || doors.size()!=6)
            fprintf(stderr,"renderer %dx%d zoom%d: terrain=%zu keys=%zu doors=%zu\n",
                    size.x,size.y,ts,terrain.size(),keys.size(),doors.size());
        assert(terrain.size()==6 && keys.size()==6 && doors.size()==6);
        for(int c=0;c<6;++c) {
            assert(objSprite(Obj(int(Obj::KEY_RED)+c))==S_KEY);
            assert(objSprite(Obj(int(Obj::DOOR_RED)+c))==S_DOOR);
            assert(keyColorId(Obj(int(Obj::KEY_RED)+c))==c);
            assert(keyColorId(Obj(int(Obj::DOOR_RED)+c))==c);
            auto k=cell(2+c,5), d=cell(2+c,7);
            assert(k!=d);
            // Same high-contrast numeric ID on key and door; every glyph is distinct.
            const int scale=ts/16;
            std::vector<uint16_t> badge;
            for(int y=0;y<7*scale;++y) for(int x=11*scale;x<ts;++x)
                { assert(k[y*ts+x]==d[y*ts+x]); badge.push_back(k[y*ts+x]); }
            badges.insert(badge);
        }
        assert(badges.size()==6); // IDs remain distinct even with colour ignored.
        auto off=cell(3,3);
        world.move(Dir::DOWN); world.move(Dir::DOWN); world.move(Dir::RIGHT);
        world.activate(); draw(); assert(off!=cell(3,3));
        // Exercise the real framebuffer overlay at all clipping boundaries, with stride guards.
        for(int sx : {-ts,-ts+1,size.x-1,size.x})
            for(int sy : {-ts,-ts+1,s_layout.view.h-1,s_layout.view.h}) {
                coloredObject(S_KEY,5,sx,sy); terrainOverlay(Tile::ICE,2,3,sx,sy);
                assert(fb.front()==0xbeef && fb.back()==0xbeef);
            }
    }
}
int main() {
    soundTests(); actionTests(); movementTests(); rendererTests();
    puts("S2 production UI: renderer (2 sizes/2 zooms), action, forced movement, HUD/par, muted SFX OK");
}
'''
replacements = {
    "GEOMETRY": section("static Game::Layout s_layout", "// Sprites:").rsplit("// -----", 1)[0],
    "SPRITES": section("enum Spr", "static Sprites *s_spr"),
    "ART": section("static uint16_t rgb(", "// Renderer").rsplit("// -----", 1)[0],
    "RENDER": section("static const uint16_t *spr(", "// HUD").rsplit("// -----", 1)[0],
    "HUD": section("static void setCount(", "static void toast("),
    "SNAP": section("static void snapPlayer()", "// Feedback for"),
    "SOUND": section("static void playEvents(", "// Dialogue").rsplit("// -----", 1)[0],
    "MOVE": section("static uint32_t doMove(", "// v0.18.4: the split"),
    "ACTION": section("enum { ACT_HIT", "// Tap-to-walk"),
    "PERFORMANCE": section("static void performanceText(", "static void addPerformance("),
    "INVENTORY": section("    static const char *namesEn[6]", "    menuButton(c, soundText()"),
}
audio = (ROOT / "main/board_audio.cpp").read_text()
replacements["AUDIO_PLAY"] = audio[audio.index("void play(Sound::Sfx s)"):audio.index("void music(Sound::Music m)")]
for marker, replacement in replacements.items():
    code = code.replace(f"/*{marker}*/", replacement)
(WORK / "test.cpp").write_text(code)
subprocess.run([
    "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
    "-Wno-unused-variable", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
    "-Imain", "-Icomponents/otr_engine/include", "-Icomponents/otr_story/include",
    str(WORK / "test.cpp"), "components/otr_engine/otr_world.cpp", "components/otr_story/otr_story.cpp",
    "main/lang/lang.cpp", "main/lang/lang_sv.cpp", "main/lang/lang_en.cpp",
    "-o", str(WORK / "test")
], cwd=ROOT, check=True)
subprocess.run([str(WORK / "test")], cwd=ROOT, check=True)
