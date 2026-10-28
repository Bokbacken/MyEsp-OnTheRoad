#!/usr/bin/env python3
"""Host-check production Adventure art, framebuffer, actions, movement and sound dispatch.

No LVGL/ESP-IDF needed: only LVGL labels/hardware are stubbed. Uses the real World.
Run: python3 tests/test_adv_s2_ui.py [existing workspace parent directory]
Set ADV_UI_SANITIZE=0 for a non-sanitized build (sanitizers are enabled by default).
"""
import atexit
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
WORK = pathlib.Path(tempfile.mkdtemp(prefix="adv-s2-ui-", dir=sys.argv[1] if len(sys.argv) > 1 else None))
atexit.register(shutil.rmtree, WORK)
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
constexpr uint32_t PANEL=0x17263A;
static uint32_t c(uint32_t n) { return n; }
}
struct lv_event_t { lv_obj_t *target; };
static lv_obj_t *lv_event_get_target_obj(lv_event_t *e) { return e->target; }
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
/*MOVE_STATE*/
static bool s_rockAnim, s_dirty;
static int s_rockFromX, s_rockFromY, s_rockToX, s_rockToY, s_bumpDx, s_bumpDy, s_fxX=-1, s_fxY=-1;
static int s_bombSize, s_bombX=-1, s_bombY=-1, s_talkNum, s_pathPos=-1, s_walkTalkNum;
static Otr::JoyController s_joy;
static const Otr::Story::Script *s_story;
static lv_obj_t *s_talkPanel;
static lv_obj_t *s_dialog;
static void closeDialog() { s_dialog=nullptr; }
static lv_obj_t *lv_msgbox_create(lv_obj_t *) { static lv_obj_t popup; return &popup; }
static void lv_msgbox_add_text(lv_obj_t *o, const char *text) { o->text=text; }
namespace AdvUi { static void dialogLayout(lv_obj_t *) {} }
static lv_obj_t labelObjects[32];
enum HudCount { C_LIVES, C_SUIT, C_KEYS, C_GEMS, C_COUNT };
static lv_obj_t *s_count[C_COUNT]={labelObjects,labelObjects+1,labelObjects+2,labelObjects+3};
static lv_obj_t *s_stageLabel=labelObjects+4, *s_progressLabel=labelObjects+5;
static lv_obj_t *s_timerLabel=labelObjects+21;
/*EXTRA_STATE*/
static lv_obj_t *s_keyBox=labelObjects+22;
static lv_obj_t *s_keyIcons[6]={labelObjects+23,labelObjects+24,labelObjects+25,
                              labelObjects+26,labelObjects+27,labelObjects+28};
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
/*EXTRA_TOGGLE*/
/*KEY_ART*/
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
static struct SdStub { bool enabled=false; bool active() const { return enabled; } } s_sd;
static int travels;
static void sdExitAdvance() { ++travels; }
/*MOVE*/
static bool talking() { return false; }
static void menuPause() {
/*MENU_PAUSE*/
}
/*DIALOG*/
/*ACTION*/
/*PERFORMANCE*/
static std::vector<std::string> rows;
static void load(int par=0) {
    rows.assign(10,std::string(14,'.'));
    for (int x=0;x<14;++x) rows[0][x]=rows[9][x]='#';
    for (int y=0;y<10;++y) rows[y][0]=rows[y][13]='#';
    rows[1][1]='S';
    const char *ptr[10];
    for (int y=0;y<10;++y) ptr[y]=rows[y].c_str();
    assert(world.load(Otr::MapDef{90,ptr,10,Otr::CLASSIC_RULES,nullptr,0,nullptr,0,nullptr,0,par}));
    snapPlayer(); s_bumpT0=0; s_pathPos=-1; s_joy.cancel(); Audio::heard.clear(); results=deaths=talks=0;
    s_actSel=ACT_HIT; s_actShown=-1;
}
static void reload(int par=0) {
    const char *ptr[10]; for (int y=0;y<10;++y) ptr[y]=rows[y].c_str();
    assert(world.load(Otr::MapDef{90,ptr,10,Otr::CLASSIC_RULES,nullptr,0,nullptr,0,nullptr,0,par}));
    snapPlayer(); s_bumpT0=0; Audio::heard.clear();
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
    rows[2][3]=char(Otr::BELT_UP_GLYPH+3);
    rows[2][1]='#'; // The inside corner cannot be cut diagonally.
    reload(4);
    auto ev=doMove(Dir::RIGHT);
    assert((ev&Otr::EV_MOVED) && world.steps()==4 && world.playerX()==2 && world.playerY()==2);
    assert(s_moveMs==520 && Audio::heard.size()==1);
    assert(results==0);
    const uint32_t start=clockMs;
    for (int ts : {16,32}) {
        s_ts=ts;
        const int expected[][2]={{24,16},{40,16},{48,24},{40,32}};
        for (int i=0;i<4;++i) {
            int x,y; playerPixel(start+65+130*i,x,y);
            assert(x==expected[i][0]*ts/16 && y==expected[i][1]*ts/16);
            assert(busy(start+65+130*i));
        }
    }
    // Busy is the production joystick poll gate, including while held.
    s_joy.press(clockMs,70,0);
    for (int elapsed=0;elapsed<520;elapsed+=20) {
        clockMs=start+elapsed;
        Dir dir;
        assert(!s_joy.poll(start+elapsed,!busy(start+elapsed),dir));
        assert(doMove(Dir::DOWN)==0 && world.steps()==4 && Audio::heard.size()==1);
    }
    s_joy.release(clockMs); clockMs=start+520; finishMovement(clockMs);
    assert(!busy(clockMs) && s_moveTrace.count==0);
    assert(s_progressLabel->text==(langIdx() ? "Steps:4  Par:4" : "Steg:4  Par:4"));
    char text[96]; performanceText(text,sizeof text); assert(strstr(text,"Equal")||strstr(text,"Lika"));
    // Straight ice visits each cell; visual ticks never advance the synchronous model.
    load(); rows[1][2]=rows[1][3]=rows[1][4]=char(Otr::ICE_GLYPH); reload();
    doMove(Dir::RIGHT);
    const int iceSteps=world.steps();
    for (int ts : {16,32}) for (int elapsed=0;elapsed<=520;elapsed+=65) {
        s_ts=ts; int x,y; playerPixel(clockMs+elapsed,x,y);
        assert(x==int(lround((1.f+elapsed/130.f)*ts)) && y==ts);
        assert(world.steps()==iceSteps && Audio::heard.size()==1);
    }
    // Release/STOP do not discard the remaining route or lock subsequent input.
    s_joy.cancel(); stopPath(); assert(busy(clockMs+200));
    clockMs+=520; finishMovement(clockMs); doMove(Dir::DOWN);
    assert(world.steps()==iceSteps+1);
    // Menu pause snaps to the model endpoint and cancels the visual queue.
    menuPause(); assert(!busy(clockMs) && s_moveTrace.count==0);
    clockMs+=1000; assert(doMove(Dir::DOWN)&Otr::EV_MOVED);
    menuPause();
    // Check an ordinary single step keeps animation.
    load(); doMove(Dir::DOWN); assert(s_moveMs>0);
    assert(s_progressLabel->text==(langIdx() ? "Steps:1  Par:-" : "Steg:1  Par:-"));
    clockMs+=1000; doMove(Dir::DOWN);
    assert(s_progressLabel->text==(langIdx() ? "Steps:2  Par:-" : "Steg:2  Par:-"));
    load(); rows[1][2]=char(Otr::ICE_GLYPH); rows[1][3]='X'; reload();
    s_pathPos=3; s_joy.press(clockMs,70,0);
    ev=doMove(Dir::RIGHT);
    assert(ev&Otr::EV_EXIT); assert(s_pathPos==-1 && !s_joy.held() && results==0 && s_moveMs==260);
    finishMovement(clockMs+259); assert(results==0);
    clockMs+=260; finishMovement(clockMs); assert(results==1 && s_moveMs==0);
    const int steps=world.steps(); doMove(Dir::LEFT); doHit(); assert(world.steps()==steps);
    load(); rows[1][2]=char(Otr::BELT_UP_GLYPH+1); rows[1][3]='^'; reload();
    s_pathPos=2; s_joy.press(clockMs,70,0);
    ev=doMove(Dir::RIGHT);
    assert(ev&Otr::EV_DIED); assert(s_pathPos==-1 && !s_joy.held() && s_moveMs==0);
    assert(s_moveTrace.count==0 && !s_moveEvents);
    // A surviving danger landing still animates its real route without replaying damage.
    load(); rows[1][2]=char(Otr::BELT_UP_GLYPH+1); rows[1][3]='m'; reload();
    ev=doMove(Dir::RIGHT); assert((ev&Otr::EV_MINE) && !(ev&Otr::EV_DIED));
    const int energy=world.energy(); clockMs+=260; finishMovement(clockMs);
    assert(world.energy()==energy && Audio::heard.size()==1);
    // SD travel is deferred too; opening a menu safely snaps/delivers terminal events once.
    load(); rows[1][2]=char(Otr::ICE_GLYPH); rows[1][3]='X'; reload(); s_sd.enabled=true;
    travels=0; doMove(Dir::RIGHT); assert(travels==0);
    finishMovement(clockMs+259); assert(travels==0);
    menuPause(); assert(travels==1 && !s_moveEvents && !s_moveMs);
    finishMovement(clockMs+1000); assert(travels==1); s_sd.enabled=false;
    // Generic modal cancellation preserves pending terminal delivery even without a visual queue.
    for (bool sd : {false,true}) {
        load(); rows[1][2]=char(Otr::ICE_GLYPH); rows[1][3]='X'; reload();
        s_sd.enabled=sd; travels=0;
        doMove(Dir::RIGHT); dialog("Pause");
        assert(!s_moveMs && (s_moveEvents&Otr::EV_EXIT) && s_dialog);
        assert(results==0 && travels==0);
        finishMovement(clockMs);
        assert(!s_moveEvents && !s_dialog && results==int(!sd) && travels==int(sd));
        finishMovement(clockMs+1000);
        assert(results==int(!sd) && travels==int(sd));
    }
    s_sd.enabled=false;
    load(5); doMove(Dir::DOWN); performanceText(text,sizeof text); assert(strstr(text,"Under"));
    load(1); world.move(Dir::DOWN); world.move(Dir::DOWN);
    performanceText(text,sizeof text); assert(strstr(text,"Over")||strstr(text,"Över"));
    // Exceeding par is an assessment, never a win gate.
    load(1); rows[1][3]='X'; reload(1);
    world.move(Dir::RIGHT); ev=doMove(Dir::RIGHT);
    assert((ev&Otr::EV_EXIT) && results==0 && !world.parMet());
    clockMs+=s_moveMs; finishMovement(clockMs); assert(results==1);
    // Built-in true-minesweeper exits belong to tick()/mineResult(), not the generic result.
    const char *mineRows[]={"#####","#SX.#","#####"};
    auto mineRules=Otr::CLASSIC_RULES; mineRules.mineLevel=1;
    assert(world.load(mineRows,3,mineRules)); snapPlayer(); results=0;
    ev=doMove(Dir::RIGHT); assert((ev&Otr::EV_EXIT) && results==0);
    load(); performanceText(text,sizeof text); assert(strstr(text,"Par: -"));
}
static void inventoryTests() {
    makeArt(s_spr); makeColoredKeyIcons();
    std::set<std::vector<uint16_t>> images, badges;
    for (int i=0;i<6;++i) {
        images.emplace(s_spr->coloredKeys[i],s_spr->coloredKeys[i]+256);
        std::vector<uint16_t> badge;
        for (int y=0;y<7;++y) for (int x=11;x<16;++x) badge.push_back(s_spr->coloredKeys[i][y*16+x]);
        badges.insert(badge);
    }
    assert(images.size()==6 && badges.size()==6);
    load(); updateStats();
    assert(s_keyBox->hidden);
    for (auto *icon : s_keyIcons) assert(icon->hidden);
    // Quantity is not rendered: one owned key and 42 owned keys have identical indicators.
    for (int count : {1,42}) {
        world.setKeys(Otr::KeyColor::RED,count); updateStats();
        makeColoredKeyIcons();
        for (int i=0;i<6;++i)
            assert(images.count(std::vector<uint16_t>(s_spr->coloredKeys[i],s_spr->coloredKeys[i]+256))==1);
        assert(!s_keyBox->hidden && !s_keyIcons[0]->hidden);
        for (int i=1;i<6;++i) assert(s_keyIcons[i]->hidden);
    }
    for (int c : {2,5}) world.setKeys(Otr::KeyColor(c),3);
    updateStats();
    for (int i=0;i<6;++i) assert(s_keyIcons[i]->hidden != (i==0||i==2||i==5));
    for (int c=0;c<6;++c) world.setKeys(Otr::KeyColor(c),10+c);
    world.setBoat(true); world.setFlashlight(3); world.setPogo(3);
    updateStats(); for (auto *icon : s_keyIcons) assert(!icon->hidden && icon->text.empty());
    assert(!s_item[H_BOAT]->hidden && !s_flashCell->hidden && !s_pogoCell->hidden);
    assert(s_flashLvl->text=="3" && s_pogoLvl->text=="3");
    // Consuming the last key at a real matching door removes the icon immediately.
    load(); rows[1][2]=rows[1][3]=char(Otr::COLORED_DOOR_GLYPH); reload();
    world.setKeys(Otr::KeyColor::RED,2); updateStats();
    assert(!s_keyIcons[0]->hidden);
    doMove(Dir::RIGHT); menuPause(); assert(world.keys(Otr::KeyColor::RED)==1);
    assert(!s_keyIcons[0]->hidden);
    doMove(Dir::RIGHT); menuPause(); // Enter the now-open first door's cell.
    doMove(Dir::RIGHT); menuPause();
    assert(world.keys(Otr::KeyColor::RED)==0 && s_keyIcons[0]->hidden && s_keyBox->hidden);
}
static void timerTests() {
    load();
    s_extraHud=false; updateStats();
    assert(s_progressLabel->hidden && s_timerLabel->hidden);
    lv_obj_t button; lv_event_t click{&button};
    assert(std::string(extraHudText())==(langIdx() ? "Extra HUD info: OFF" : "Extra HUD-info: AV"));
    toggleExtraHud(&click);
    assert(s_extraHud && !s_progressLabel->hidden && s_timerLabel->hidden);
    assert(button.text==(langIdx() ? "Extra HUD info: ON" : "Extra HUD-info: PÅ"));
    toggleExtraHud(&click);
    assert(!s_extraHud && s_progressLabel->hidden && s_timerLabel->hidden);
    rows[2][1]=char(Otr::GROUPED_LEVER_GLYPH+10);
    rows[1][2]=rows[1][3]=char(Otr::ICE_GLYPH);
    const char *ptr[10]; for (int y=0;y<10;++y) ptr[y]=rows[y].c_str();
    Otr::GroupControl controls[]={{10,5},{0,65535},{35,9}};
    Otr::MapDef def{90,ptr,10,Otr::CLASSIC_RULES,nullptr,0,nullptr,0,controls,3,0};
    assert(world.load(def)); snapPlayer(); updateStats();
    const bool en=langIdx();
    assert(s_progressLabel->hidden && s_timerLabel->hidden);
    toggleExtraHud(&click);
    assert(!s_progressLabel->hidden && !s_timerLabel->hidden);
    toggleExtraHud(&click);
    assert(s_progressLabel->hidden && s_timerLabel->hidden);
    toggleExtraHud(&click);
    assert(s_timerLabel->text==(en ? "Gate steps: 0:0 A:0 Z:0" : "Portsteg: 0:0 A:0 Z:0"));
    doSwap(); assert(s_actSel==ACT_LEVER); doHit();
    assert(world.timerRemaining(10)==5 && s_timerLabel->text.find("A:5")!=std::string::npos);
    doMove(Dir::RIGHT); assert(world.steps()==3 && world.timerRemaining(10)==2);
    const auto text=s_timerLabel->text;
    for (int t=0;t<390;t+=20) {
        int x,y; playerPixel(clockMs+t,x,y); updateStats();
        assert(world.timerRemaining(10)==2 && s_timerLabel->text==text);
    }
    menuPause(); assert(world.timerRemaining(10)==2);
    doMove(Dir::DOWN); menuPause(); doMove(Dir::DOWN); menuPause();
    assert(world.timerRemaining(10)==0 && s_timerLabel->text.find("A:0")!=std::string::npos);
    // Configuration, not a nonzero countdown, determines visibility (including expired groups).
    for (int g=0;g<Otr::PLATE_GROUPS;++g)
        assert(world.isTimedGroup(g)==(g==0||g==10||g==35));
    assert(world.spawnAt(1,1)); snapPlayer(); updateStats();
    refreshActBtn();
    if (s_actSel!=ACT_LEVER) doSwap();
    doHit(); // Re-arm the expired lever from its original facing.
    assert(world.timerRemaining(10)==5 && s_timerLabel->text.find("A:5")!=std::string::npos);

    // Maximum configured groups and counter width remain bounded, sorted and save-restorable.
    std::string plates(38,'.'); plates[0]='S';
    Otr::GroupControl all[Otr::PLATE_GROUPS];
    for (int g=0;g<Otr::PLATE_GROUPS;++g) {
        plates[g+1]=char(Otr::GROUPED_PLATE_GLYPH+g);
        all[g]={(uint8_t)g,65535};
    }
    const char *plateRows[]={plates.c_str()};
    Otr::MapDef many{91,plateRows,1,Otr::CLASSIC_RULES,nullptr,0,nullptr,0,all,Otr::PLATE_GROUPS,0};
    assert(world.load(many));
    for (int g=0;g<Otr::PLATE_GROUPS;++g) assert(world.move(Dir::RIGHT)&Otr::EV_MOVED);
    snapPlayer(); updateStats();
    const auto active=s_timerLabel->text;
    assert(active.size()<384 && active.find("0:65500")!=std::string::npos &&
           active.find("Z:65535")!=std::string::npos);
    std::vector<uint8_t> saved(world.saveSize());
    assert(world.save(saved.data(),saved.size())==saved.size());
    assert(world.load(many) && world.restore(saved.data(),saved.size()));
    snapPlayer(); updateStats(); assert(s_timerLabel->text==active);
    load(); updateStats(); assert(s_timerLabel->hidden && !s_progressLabel->hidden && s_extraHud);
    toggleExtraHud(&click);
    assert(s_progressLabel->hidden && s_timerLabel->hidden);
    }
static void rendererTests() {
    makeArt(s_spr);
    for (auto size : {Game::Point{320,480},Game::Point{480,800},Game::Point{481,801}}) for (int ts : {16,32}) {
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
    soundTests();
    assert(!s_extraHud);
    for (const char *language : {"sv","en"}) {
        assert(Lang::setLang(language));
        actionTests(); movementTests(); inventoryTests(); timerTests();
    }
    rendererTests();
    puts("S2 production UI: renderer (3 sizes/2 zooms), action, forced movement, optional HUD, owned-key icons, muted SFX OK");
}
'''
replacements = {
    "GEOMETRY": section("static Game::Layout s_layout", "// Sprites:").rsplit("// -----", 1)[0],
    "SPRITES": section("enum Spr", "static Sprites *s_spr"),
    "ART": section("static uint16_t rgb(", "// Renderer").rsplit("// -----", 1)[0],
    "RENDER": section("static const uint16_t *spr(", "// HUD").rsplit("// -----", 1)[0],
    "HUD": section("static void setCount(", "static void toast("),
    "SNAP": section("static void snapPlayer()", "// Feedback for"),
    "MOVE_STATE": section("static Otr::MoveTrace s_moveTrace", "static bool s_rockAnim"),
    "SOUND": section("static void playEvents(", "// Dialogue").rsplit("// -----", 1)[0],
    "MOVE": section("static void finishMovement(", "// v0.18.4: the split"),
    "MENU_PAUSE": section("    if (s_dialog || talking()) return;   // Already open", "    lv_obj_t *mb = dialog(nullptr);"),
    "DIALOG": section("static lv_obj_t *dialog(const char *text)", "static void performanceText("),
    "ACTION": section("enum { ACT_HIT", "// Tap-to-walk"),
    "PERFORMANCE": section("static void performanceText(", "static void addPerformance("),
    "EXTRA_STATE": section("static bool s_extraHud", "static lv_obj_t *s_keyBox"),
    "EXTRA_TOGGLE": section("static const char *extraHudText()", "static void showMenu()"),
    "KEY_ART": section("static void makeColoredKeyIcons()", "static void buildKeyIcons()"),
}
menu = section("static void showMenu()", "// 32 px sprite")
assert "menuButton(c, extraHudText(), Theme::LINE, toggleExtraHud)" in menu
assert "namesEn" not in menu and "namesSv" not in menu
assert "s_world->keys(" not in menu and "KEY_COLORS" not in menu
audio = (ROOT / "main/board_audio.cpp").read_text()
replacements["AUDIO_PLAY"] = audio[audio.index("void play(Sound::Sfx s)"):audio.index("void music(Sound::Music m)")]
for marker, replacement in replacements.items():
    code = code.replace(f"/*{marker}*/", replacement)
(WORK / "test.cpp").write_text(code)
subprocess.run([
    "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
    "-Wno-unused-variable", "-g", "-fno-omit-frame-pointer",
    *(["-fsanitize=address,undefined"] if os.environ.get("ADV_UI_SANITIZE", "1") != "0" else []),
    "-Imain", "-Icomponents/otr_engine/include", "-Icomponents/otr_story/include",
    str(WORK / "test.cpp"), "components/otr_engine/otr_world.cpp", "components/otr_story/otr_story.cpp",
    "main/lang/lang.cpp", "main/lang/lang_sv.cpp", "main/lang/lang_en.cpp",
    "-o", str(WORK / "test")
], cwd=ROOT, check=True)
subprocess.run([str(WORK / "test")], cwd=ROOT, check=True)
