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
WORK = pathlib.Path(tempfile.mkdtemp(prefix="adv-s2-ui-", dir=sys.argv[1] if len(sys.argv) > 1 else ROOT))
atexit.register(shutil.rmtree, WORK)
FIXTURE = pathlib.Path(tempfile.mkdtemp(prefix="adv-ui-portal-"))
atexit.register(shutil.rmtree, FIXTURE)
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
#include <deque>
#include "adv_game_geometry.h"
#include "otr_world.h"
#include "otr_joystick.h"
#include "otr_story.h"
#include "adv_loader.h"
#include "adv_sd_session.h"
#include "adv_resource_art.h"
#include "sound_bank.h"
#include "lang/lang.h"
using Otr::Dir;
using Otr::Obj;
using Otr::Tile;
namespace Game = AdvUi::Game;
struct lv_obj_t {
    std::string text;
    bool hidden = false;
    uint32_t color = 0, background = 0;
    int x = 0, y = 0, w = 0, h = 0, gap = 0, opacity = 0;
    bool row = false, bottomRight = false, clickable = true, scrollable = true;
    lv_obj_t *parent = nullptr;
    std::vector<lv_obj_t *> children;
    const uint16_t *pixels = nullptr;
};
static std::deque<lv_obj_t> uiObjects;
static lv_obj_t *lv_obj_create(lv_obj_t *parent) {
    uiObjects.emplace_back();
    auto *obj=&uiObjects.back(); obj->parent=parent;
    if (parent) parent->children.push_back(obj);
    return obj;
}
static void lv_obj_remove_style_all(lv_obj_t *) {}
static void lv_obj_set_size(lv_obj_t *o, int w, int h) { o->w=w; o->h=h; }
static void lv_obj_set_pos(lv_obj_t *o, int x, int y) { o->x=x; o->y=y; }
static void lv_obj_set_scrollable(lv_obj_t *o, bool on) { o->scrollable=on; }
static void lv_obj_set_clickable(lv_obj_t *o, bool on) { o->clickable=on; }
static void lv_obj_set_flex_flow(lv_obj_t *o, int) { o->row=true; }
static void lv_obj_set_style_pad_column(lv_obj_t *o, int gap, int) { o->gap=gap; }
static void lv_obj_set_style_bg_opa(lv_obj_t *o, int opacity, int) { o->opacity=opacity; }
static void lv_obj_set_style_min_width(lv_obj_t *o, int w, int) { o->w=std::max(o->w,w); }
static void lv_obj_set_style_text_align(lv_obj_t *, int, int) {}
static void lv_obj_align(lv_obj_t *o, int, int, int) { o->bottomRight=true; }
static lv_obj_t *lv_canvas_create(lv_obj_t *parent) { return lv_obj_create(parent); }
static void lv_canvas_set_buffer(lv_obj_t *o, const uint16_t *pixels, int w, int h, int) {
    o->pixels=pixels; o->w=w; o->h=h;
}
enum { LV_OPA_COVER=255, LV_COLOR_FORMAT_RGB565, LV_FLEX_FLOW_ROW, LV_TEXT_ALIGN_CENTER,
       LV_ALIGN_BOTTOM_RIGHT };
static constexpr int my_font_14=18; // Production font's line_height, not the glyph point size.
static void lv_label_set_text(lv_obj_t *o, const char *s) { o->text = s; }
static void lv_label_set_text_fmt(lv_obj_t *o, const char *fmt, ...) {
    char text[256]; va_list ap; va_start(ap, fmt); vsnprintf(text, sizeof text, fmt, ap); va_end(ap);
    o->text = text;
}
static void lv_obj_set_style_text_color(lv_obj_t *, uint32_t, int) {}
static void lv_obj_set_style_bg_color(lv_obj_t *o, uint32_t color, int) { o->background=color; }
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
static void msgboxFonts(lv_obj_t *) {}
static lv_obj_t *label(lv_obj_t *parent, const char *text, const int *font, uint32_t color) {
    auto *obj=lv_obj_create(parent); obj->text=text; obj->h=*font; obj->color=color;
    obj->w=9; return obj;
}
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
static Otr::Story::StoryState s_storyState;
static Adv::Loaded activeLoaded;
static Adv::Loaded *s_adv = &activeLoaded;
static struct SdStub : Adv::SdSession {
    bool enabled=false;
    bool active() const { return enabled || Adv::SdSession::active(); }
} s_sd;
static Adv::ResourceArt *s_resourceArt;
static lv_obj_t *s_talkPanel;
static lv_obj_t *s_dialog;
static void closeDialog() { s_dialog=nullptr; }
static lv_obj_t *lv_msgbox_create(lv_obj_t *) { static lv_obj_t popup; return &popup; }
static std::vector<std::string> inventoryLines;
static void lv_msgbox_add_text(lv_obj_t *o, const char *text) { o->text=text; inventoryLines.emplace_back(text); }
static void lv_msgbox_add_title(lv_obj_t *, const char *) {}
static void lv_msgbox_add_close_button(lv_obj_t *) {}
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
                                labelObjects+10,labelObjects+11,labelObjects+12,labelObjects+29};
static lv_obj_t *s_flashCell=labelObjects+13, *s_pogoCell=labelObjects+14;
static lv_obj_t *s_flashLvl=labelObjects+15, *s_pogoLvl=labelObjects+16;
static lv_obj_t *s_itemBox;
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
/*KEY_CONSTRUCTION*/
static void buildInventory(lv_obj_t *pan, int32_t COL_R_X, int32_t COL_R_W) {
    const auto px=[](int32_t baseline) { return s_layout.controlPx(baseline); };
/*ITEM_CONSTRUCTION*/
}
static std::string lastToast;
static void toast(const char *text, uint32_t=Theme::TEXT, uint32_t=2200) { lastToast=text; }
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
static bool s_saveWanted;
static int imageRefreshes, mapInvalidations;
static int32_t s_lastCamX, s_lastCamY;
static void loadResourceImages() { ++imageRefreshes; }
namespace AdvLive { static void invalidateMap() { ++mapInvalidations; } }
#define ESP_LOGW(...) ((void)0)
static int travels;
static void sdExitAdvance() { ++travels; }
/*SD_ACTION*/
/*MOVE*/
static bool talking() { return false; }
static void menuPause() {
/*MENU_PAUSE*/
}
/*DIALOG*/
/*INVENTORY*/
/*ACTION*/
/*PERFORMANCE*/
static std::vector<std::string> rows;
static void load(int par=0) {
    s_adv = &activeLoaded;
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
static void portalUiTests() {
    s_adv = &activeLoaded;
    Adv::CatalogEntry entry{};
    strcpy(entry.folder, "portal_ui");
    strcpy(entry.maps[0], "01_test.adv");
    entry.mapCount = 1;
    char err[160];
    const bool started = s_sd.start(ADV_TEST_ROOT, entry, *s_adv, world, s_storyState, s_story, err, sizeof err);
    if (!started) fprintf(stderr, "portal UI fixture: %s\n", err);
    assert(started);
    snapPlayer();
    s_joy.cancel();
    s_bombSize = 0;
    const int images = imageRefreshes, invalidations = mapInvalidations;
    doMove(Dir::RIGHT);
    finishMovement(clockMs + 1000);
    doMove(Dir::RIGHT);
    assert(s_sd.currentMapId() == 2 && world.playerX() == 2 && world.playerY() == 1);
    assert(!s_moveMs && !s_moveEvents && s_pathPos < 0 && !s_joy.held());
    assert(s_lastCamX == -1 && s_lastCamY == -1);
    assert(imageRefreshes == images + 1 && mapInvalidations == invalidations + 1 && s_saveWanted);
    // Arriving on the return portal must not immediately bounce back.
    finishMovement(clockMs + 1000);
    assert(s_sd.currentMapId() == 2);
    doMove(Dir::RIGHT);
    finishMovement(clockMs + 1000);
    doMove(Dir::LEFT);
    assert(s_sd.currentMapId() == 1 && world.playerX() == 2 && world.playerY() == 1);
    assert(imageRefreshes == images + 2 && mapInvalidations == invalidations + 2);
    s_sd.clear();
    s_story = nullptr;
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
static void pullTests() {
    load(); rows[1][1]='.'; rows[2][2]='S'; rows[3][2]='R'; reload();
    assert(!actAvail(ACT_PULL));
    world.setSuction(true);
    assert(actAvail(ACT_PULL));
    const int steps=world.steps(), energy=world.energy();
    doSwap();
    assert(s_actSel==ACT_PULL && world.steps()==steps && world.energy()==energy);
    assert(world.playerX()==2 && world.playerY()==2 && world.obj(2,3)==Obj::ROCK);
    assert(s_hitBtn->text==(langIdx()?"Pull":"Dra"));
    doHit();
    assert(world.playerX()==2 && world.playerY()==1 && world.obj(2,2)==Obj::ROCK);
    assert(s_moveTrace.count==1 && s_moveMs>0 && !s_rockAnim);
    assert(!actAvail(ACT_PULL)); // Backward landing is a wall.
    menuPause();
    world.setSuction(false); refreshActBtn();
    assert(s_actSel==ACT_HIT);
    load(); rows[1][2]=char(Otr::SUCTION_GLYPH); reload();
    doMove(Dir::RIGHT); menuPause();
    assert(world.hasSuction() && objSprite(Obj::SUCTION)==S_SUCTION && keyColorId(Obj::SUCTION)==-1);
    load(); rows[2][1]=char(Otr::GROUPED_LEVER_GLYPH);
    const char *ptr[10]; for(int y=0;y<10;++y) ptr[y]=rows[y].c_str();
    Otr::EnemyDef foe{Otr::EnemyKind::CHASER,2,1};
    Otr::MapDef def{90,ptr,10,Otr::CLASSIC_RULES,nullptr,0,nullptr,0};
    def.enemies=&foe; def.enemyCount=1;
    assert(world.load(def)); snapPlayer(); world.setEnergy(1); world.setLives(3,0);
    s_moveMs=10; s_moveT0=clockMs-11; s_moveTrace.count=1;
    s_moveEvents=Otr::EV_EXIT|Otr::EV_TALK; s_rockAnim=true;
    s_fromX=9; s_fromY=8; s_pathPos=2; s_joy.press(clockMs,70,0);
    s_actSel=ACT_LEVER; doHit();
    assert(!world.gameOver() && world.livesLeft()==2 && deaths==0);
    assert(!s_moveMs && !s_moveTrace.count && !s_moveEvents && !s_rockAnim);
    assert(s_fromX==world.playerX() && s_fromY==world.playerY() && s_pathPos==-1 && !s_joy.held());
    int playerX,playerY; playerPixel(clockMs,playerX,playerY);
    assert(playerX==world.playerX()*s_ts && playerY==world.playerY()*s_ts);
    finishMovement(clockMs); assert(results==0 && talks==0);
    world.setEnergy(1); world.setLives(1,0);
    s_actSel=ACT_LEVER; doHit();
    assert(world.gameOver() && deaths==1 && !s_moveMs);
}
static void movementTests() {
    // Both held joystick and tap-planned steps may stop and turn on skated ice.
    load(); rows[1][2]=char(Otr::SKATES_GLYPH);
    rows[1][3]=rows[1][4]=char(Otr::ICE_GLYPH); reload();
    doMove(Dir::RIGHT); menuPause();
    assert(world.hasSkates() && !s_item[H_SKATES]->hidden);
    s_joy.press(clockMs,70,0);
    doMove(Dir::RIGHT); menuPause();
    assert(world.playerX()==3 && world.steps()==2);
    doMove(Dir::DOWN); menuPause(); assert(world.playerX()==3 && world.playerY()==2);
    Otr::Planner planner; Otr::Path path;
    int tapX=-1,tapY=-1;
    assert(Otr::Planner::pickTarget(world,4.5f,1.5f,tapX,tapY) && tapX==4 && tapY==1);
    assert(planner.plan(world,4,1,Otr::WalkMode::ASSIST,path)==Otr::PlanResult::OK);
    for (int i=0;i<path.len;++i) { doMove(Dir(path.dirs[i])); menuPause(); }
    assert(world.playerX()==4 && world.playerY()==1);
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
static Game::Rect bounds(lv_obj_t *obj) {
    Game::Rect rect{obj->x,obj->y,obj->w,obj->h};
    for (auto *parent=obj->parent; parent; parent=parent->parent) {
        rect.x+=parent->x; rect.y+=parent->y;
    }
    return rect;
}
static bool contains(Game::Rect outer, Game::Rect inner) {
    return inner.x>=outer.x && inner.y>=outer.y &&
           inner.x+inner.w<=outer.x+outer.w && inner.y+inner.h<=outer.y+outer.h;
}
static bool separate(Game::Rect a, Game::Rect b) {
    return a.x+a.w<=b.x || b.x+b.w<=a.x || a.y+a.h<=b.y || b.y+b.h<=a.y;
}
// Only row placement and label alignment are simulated; sizes/parents/styles come from production.
static void inventoryLayout(lv_obj_t *obj) {
    int x=0;
    for (auto *child : obj->children) {
        if (child->hidden) continue;
        if (obj->row) { child->x=x; child->y=0; x+=child->w+obj->gap; }
        if (child->bottomRight) { child->x=obj->w-child->w; child->y=obj->h-child->h; }
        inventoryLayout(child);
    }
}
static void inventoryConstructionTests() {
    for (auto size : {Game::Point{320,480}, {480,800}, {481,801}}) {
        s_layout=Game::layout(size.x,size.y);
        const auto px=[](int v) { return s_layout.controlPx(v); };
        auto *panel=lv_obj_create(nullptr);
        lv_obj_set_pos(panel,0,s_layout.controls.y);
        lv_obj_set_size(panel,s_layout.controls.w,s_layout.controls.h);
        const int rightX=s_layout.joystick.x+s_layout.joystick.w;
        const int rightW=s_layout.controls.w-rightX-px(4);
        buildInventory(panel,rightX,rightW);
        assert(s_keyBox->parent==panel && s_keyBox->w==56 && s_keyBox->h==36);
        assert(s_keyBox->hidden && !s_keyBox->clickable && !s_keyBox->scrollable);
        assert(s_keyBox->children.size()==6);
        load(); world.setBoat(true); world.setFlashlight(3); world.setPogo(3); world.setSkates(true); world.setSuction(true);
        for (auto owned : {std::vector<int>{}, {4}, {0,2,5}, {0,1,2,3,4,5}}) {
            for (int i=0;i<6;++i) world.setKeys(Otr::KeyColor(i),0);
            for (int i : owned) world.setKeys(Otr::KeyColor(i),42);
            updateStats(); inventoryLayout(panel);
            const auto grid=bounds(s_keyBox), items=bounds(s_itemBox);
            const auto skates=bounds(s_item[H_SKATES]);
            const auto suction=bounds(s_item[H_SUCTION]);
            assert(!s_item[H_SUCTION]->hidden && contains(bounds(panel),suction));
            assert(separate(suction,grid) && separate(suction,items) && separate(suction,skates));
            assert(s_item[H_SUCTION]->pixels==s_spr->hud[H_SUCTION]);
            assert(!s_item[H_SKATES]->hidden && contains(bounds(panel),skates));
            assert(separate(skates,grid) && separate(skates,items) && separate(skates,s_layout.joystick));
            assert(s_item[H_SKATES]->pixels==s_spr->hud[H_SKATES]);
            assert(s_keyBox->hidden==owned.empty());
            assert(contains(bounds(panel),grid) && contains(bounds(panel),items));
            assert(grid.x>=rightX && grid.x+grid.w==size.x-px(4));
            assert(grid.y+grid.h==size.y-px(4));
            assert(separate(grid,items) && separate(grid,s_layout.joystick));
            for (auto area : {grid,items}) {
                assert(separate(area,{rightX,s_layout.controls.y+px(74),rightW,px(18)}));
                assert(separate(area,{rightX,s_layout.controls.y+px(8),rightW,px(56)}));
            }
            for (int i=0;i<6;++i) {
                auto *icon=s_keyIcons[i];
                const bool has=std::find(owned.begin(),owned.end(),i)!=owned.end();
                assert(icon->hidden!=has && icon->parent==s_keyBox);
                assert(icon->x==(i%3)*20 && icon->y==(i/3)*20);
                assert(icon->w==16 && icon->h==16 && contains(grid,bounds(icon)));
                assert(icon->pixels==s_spr->coloredKeys[i] && icon->children.empty());
                if (has) { world.setKeys(Otr::KeyColor(i),1); updateStats(); assert(!icon->hidden); }
            }
            std::vector<Game::Rect> cells;
            for (auto *cell : {s_item[H_BOAT],s_flashCell,s_pogoCell}) {
                assert(!cell->hidden && contains(items,bounds(cell)));
                for (auto previous : cells) assert(separate(previous,bounds(cell)));
                cells.push_back(bounds(cell));
                for (auto *child : cell->children) assert(contains(bounds(cell),bounds(child)));
            }
            for (auto *level : {s_flashLvl,s_pogoLvl}) {
                assert(level->text=="3" && level->h==18 && level->w>=10);
                assert(level->opacity==LV_OPA_COVER && level->background==Theme::PANEL);
                assert(level->color!=level->background);
            }
            assert(s_flashLvl->color==Theme::GOLD && s_pogoLvl->color==Theme::TEAL);
        }
        s_bombSize=3; updateStats(); assert(s_keyBox->hidden);
        s_bombSize=0; updateStats(); assert(!s_keyBox->hidden);
    }
}
static void inventoryTests() {
    static Otr::Story::Named named{"stone",{{"Sten","Stone"}},2};
    static Otr::Story::Script script{};
    script.items=&named; script.itemCount=1;
    s_story=&script; s_storyState.clear();
    inventoryLines.clear(); showInventory();
    assert(inventoryLines.back()==(langIdx()?"Empty":"Tomt"));
    s_storyState.items[0]=2; inventoryLines.clear(); showInventory();
    assert(inventoryLines.back()==(langIdx()?"Stone: 2 / 2":"Sten: 2 / 2"));
    s_story=nullptr; s_storyState.clear(); closeDialog();
    makeArt(s_spr); makeColoredKeyIcons();
    inventoryConstructionTests();
    std::set<std::vector<uint16_t>> images, badges;
    for (int i=0;i<6;++i) {
        images.emplace(s_spr->coloredKeys[i],s_spr->coloredKeys[i]+256);
        std::vector<uint16_t> badge;
        for (int y=0;y<7;++y) for (int x=11;x<16;++x) badge.push_back(s_spr->coloredKeys[i][y*16+x]);
        badges.insert(badge);
    }
    assert(images.size()==6 && badges.size()==6);
    load(); updateStats();
    assert(s_item[H_SKATES]->hidden);
    world.setSkates(true); updateStats(); assert(!s_item[H_SKATES]->hidden);
    world.setSkates(false); updateStats(); assert(s_item[H_SKATES]->hidden);
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
            int playerX,playerY; playerPixel(clockMs,playerX,playerY);
            render(clockMs,0,0,playerX,playerY,0);
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
        {
            static Adv::Loaded adventure;
            adventure.resourceCount=1;
            adventure.resources[0].id="stone";
            static Otr::Story::Named named{"stone",{{"Sten","Stone"}},2};
            static Otr::Story::Script script{};
            script.items=&named; script.itemCount=1;
            s_adv=&adventure; s_story=&script;
            std::fill(s_fb,s_fb+count,0);
            resourceSprite(0,0,0);
            const auto fallback=cell(0,0);
            assert(std::count(fallback.begin(),fallback.end(),uint16_t(0))<int(fallback.size()));
            static Adv::ResourceArt custom;
            custom.ready=true;
            for(int p=0;p<1024;++p) {
                custom.bgra[p*4]=0; custom.bgra[p*4+1]=0;
                custom.bgra[p*4+2]=255; custom.bgra[p*4+3]=255;
            }
            s_resourceArt=&custom;
            resourceSprite(0,0,0);
            assert(s_fb[0]==rgb(255,0,0) && cell(0,0)!=fallback);
            custom.bgra[3]=0; s_fb[0]=rgb(0,255,0);
            resourceSprite(0,0,0); assert(s_fb[0]==rgb(0,255,0));
            s_resourceArt=nullptr; s_adv=nullptr; s_story=nullptr;
            draw();
        }
        assert(objSprite(Obj::SUCTION)==S_SUCTION && keyColorId(Obj::SUCTION)==-1);
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
        load();
        rows[3][2]=char(Otr::POISON_GLYPH); rows[3][3]=char(Otr::SWAMP_GLYPH);
        rows[3][4]=char(Otr::SHARP_ROCKS_GLYPH); rows[3][5]=char(Otr::SKATES_GLYPH);
        for (int c=0;c<6;++c) {
            rows[5][2+c]=char(Otr::BOX_GLYPH+c);
            rows[7][2+c]=char(Otr::COLORED_PLATE_GLYPH+c);
        }
        reload(); draw();
        assert(terrainSprite(2,3,0)==S_POISON && terrainSprite(3,3,0)==S_SWAMP &&
               terrainSprite(4,3,0)==S_SHARP && objSprite(Obj::SKATES)==S_SKATES);
        std::set<std::vector<uint8_t>> shapes;
        for (int x=2;x<5;++x) {
            std::vector<uint8_t> shape;
            for (auto p : cell(x,3)) {
                const int brightness=((p>>11)*255/31+((p>>5)&63)*255/63+(p&31)*255/31)/3;
                shape.push_back(brightness>140);
            }
            assert(std::count(shape.begin(),shape.end(),1)>0);
            shapes.insert(shape);
        }
        assert(shapes.size()==3); // Skull, reeds and spikes survive monochrome at both zooms.
        assert(cell(5,3)!=cell(6,3));
        std::set<std::vector<uint16_t>> boxes,plates,boxBadges;
        for (int c=0;c<6;++c) {
            assert(objSprite(Obj(int(Obj::BOX_RED)+c))==S_BOX);
            assert(keyColorId(Obj(int(Obj::BOX_RED)+c))==c);
            auto box=cell(2+c,5), plate=cell(2+c,7);
            boxes.insert(box); plates.insert(plate); assert(box!=plate);
            std::vector<uint16_t> badge;
            const int scale=ts/16;
            for (int y=0;y<7*scale;++y) for (int x=11*scale;x<ts;++x) {
                assert(box[y*ts+x]==plate[y*ts+x]); badge.push_back(box[y*ts+x]);
            }
            boxBadges.insert(badge);
        }
        assert(boxes.size()==6 && plates.size()==6 && boxBadges==badges);
        load(); rows[3][2]=char(Otr::COLORED_PLATE_GLYPH+4); reload(); draw();
        const auto emptyPatrol=cell(2,3), emptyChaser=cell(3,3);
        Otr::EnemyDef foes[]={{Otr::EnemyKind::PATROL,2,3},{Otr::EnemyKind::CHASER,3,3}};
        foes[0].routeCount=1; foes[0].route[0]={2,3};
        const char *actorRows[10]; for(int y=0;y<10;++y) actorRows[y]=rows[y].c_str();
        Otr::MapDef actors{90,actorRows,10,Otr::CLASSIC_RULES,nullptr,0,nullptr,0};
        actors.enemies=foes; actors.enemyCount=2;
        assert(world.load(actors)); snapPlayer(); draw();
        assert(cell(2,3)!=emptyPatrol && cell(3,3)!=emptyChaser && cell(2,3)!=cell(3,3));
        Otr::DarkRect dark{2,3,3,3}; actors.darks=&dark; actors.darkCount=1;
        assert(world.load(actors)); snapPlayer(); draw();
        assert(cell(2,3)==cell(3,3));
        for(auto p : cell(2,3)) assert(p==rgb(6,6,12));
        rows[2][2]='m';
        actors.darks=nullptr; actors.darkCount=0; actors.rules.fog=true;
        assert(world.load(actors)); snapPlayer(); draw();
        assert(!world.revealed(2,3) && !world.revealed(3,3));
        assert(cell(2,3)==cell(3,3));
        for (int c=0;c<6;++c) {
            load(); rows[1][2]=char(Otr::BOX_GLYPH+c); reload(); draw();
            const auto before=cell(2,1);
            const auto ev=doMove(Dir::RIGHT);
            assert((ev&Otr::EV_PUSHED) && s_rockAnim && s_moveTrace.count==1);
            const auto duration=s_moveMs;
            // Box starts at its old cell, not prematurely at its engine destination.
            draw(); assert(cell(2,1)==before);
            const auto empty=cell(3,1);
            clockMs+=duration/2; draw(); const auto middle=cell(3,1); assert(middle!=empty);
            clockMs+=duration-duration/2; finishMovement(clockMs); draw();
            assert(!s_rockAnim && cell(3,1)!=middle && world.obj(3,1)==Obj(int(Obj::BOX_RED)+c));
        }
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
        actionTests(); pullTests(); movementTests(); inventoryTests(); timerTests();
    }
    portalUiTests();
    rendererTests();
    puts("S2/v0.21 production UI: hazard shapes, skates pickup/HUD/ice controls, six box/plate badges and push animations, renderer (3 sizes/2 zooms), inventory (2 languages), actions/HUD/SFX OK");
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
    "SD_ACTION": section("static void refreshInternalMap()", "static void pendingStoryTravel()"),
    "MENU_PAUSE": section("    if (s_dialog || talking()) return;   // Already open", "    lv_obj_t *mb = dialog(nullptr);"),
    "DIALOG": section("static lv_obj_t *dialog(const char *text)", "static void performanceText("),
    "INVENTORY": section("static void showInventory()", "static const char *extraHudText()"),
    "ACTION": section("enum { ACT_HIT", "// Tap-to-walk"),
    "PERFORMANCE": section("static void performanceText(", "static void addPerformance("),
    "EXTRA_STATE": section("static bool s_extraHud", "static lv_obj_t *s_keyBox"),
    "EXTRA_TOGGLE": section("static const char *extraHudText()", "static void showMenu()"),
    "KEY_ART": section("static void makeColoredKeyIcons()", "static void buildKeyIcons("),
    "KEY_CONSTRUCTION": section("static lv_obj_t *hudIcon(", "static void makeColoredKeyIcons()") +
                        section("static void buildKeyIcons(", "// Bottom control panel"),
    "ITEM_CONSTRUCTION": section("    // Compact level badges", "    s_bombCancel=Theme::button"),
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
(FIXTURE / "portal_ui").mkdir()
(FIXTURE / "portal_ui/01_test.adv").write_text("""@meta
id: portal_ui
@map
########
#S..X..#
#......#
########
@map 2
########
#S....X#
#......#
########
@destinations
home 2,1
map=2: cave 2,1
@portals
3,1 -> cave
map=2: 2,1 -> home
""")
subprocess.run([
    "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
    "-Wno-unused-variable", "-g", "-fno-omit-frame-pointer",
    *(["-fsanitize=address,undefined"] if os.environ.get("ADV_UI_SANITIZE", "1") != "0" else []),
    "-Imain", "-Icomponents/otr_engine/include", "-Icomponents/otr_story/include",
    f'-DADV_TEST_ROOT="{FIXTURE}"',
    str(WORK / "test.cpp"), "components/otr_engine/otr_world.cpp", "components/otr_story/otr_story.cpp",
    "main/adv_loader.cpp", "main/adv_sd_session.cpp",
    "main/lang/lang.cpp", "main/lang/lang_sv.cpp", "main/lang/lang_en.cpp",
    "-o", str(WORK / "test")
], cwd=ROOT, check=True)
subprocess.run([str(WORK / "test")], cwd=ROOT, check=True)
