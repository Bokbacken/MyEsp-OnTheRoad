#!/usr/bin/env python3
"""Exercise production Adventure root, joystick drawing/input and talk overlays with host LVGL.
Run: python3 tests/test_adv_game_layout.py [external build directory].
Requires managed_components/lvgl__lvgl, like the existing setup/dialog layout tests.
Set ADV_UI_SANITIZE=1 to enable address/undefined sanitizers.
"""
import pathlib
import os
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
WORK = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(tempfile.mkdtemp(prefix="adv-game-"))
WORK = WORK.resolve()
WORK.mkdir(parents=True, exist_ok=True)
source = (ROOT / "main/ui_adventure.cpp").read_text()


def section(start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


geometry = section("static Game::Layout s_layout", "// Sprites:").rsplit("// -----", 1)[0]
joy = section("static void joyShow(bool down, int dx, int dy)\n{", "static void configureJoy()")
pad = section("static lv_obj_t *circle(", "// Viewport: tap")
talk = section("static void talkBuild()", "static void endTalk()")
# Keep all construction/parenting statements from buildGame, stopping before world/timer work.
game = section("static void buildGame(bool loadFailed)", "    snapPlayer();") + "\n}\n"
talk_constants = section("static constexpr int32_t TALK_PAD", "static bool talking()")
controls = section("static void buildControls()", "// Game screen").rsplit("// -----", 1)[0]
choice = section("static void addChoice(", "static void talkShowChoices()")

code = r'''
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <set>
#include <string>
#include <vector>
#include <fstream>
#include <iterator>
#include "adv_resource_art.h"
#include "adv_game_ui.h"
#include "ui_metrics.h"
#include "my_theme.h"
#include "otr_joystick.h"
#include "otr_world.h"
#include "lang/lang.h"

namespace Game = AdvUi::Game;
using Otr::Dir;
/*GEOMETRY*/
/*SPRITES*/
static Sprites sprites, *s_spr = &sprites;
/*ART*/
/*KEY_COLORS*/
/*COLORED_SPRITE*/
static lv_obj_t *s_scr, *s_activeScr, *s_gameRoot, *s_canvas, *s_toast, *s_stageLabel, *s_progressLabel, *s_timerLabel;
static lv_obj_t *s_joyKnob, *s_joyArrow[4];
static int s_joyLit = -1;
static Otr::JoyController s_joy;
static uint16_t *s_fb;
static lv_obj_t *s_talkBox, *s_talkPanel, *s_talkPortrait, *s_talkName, *s_talkText, *s_talkRef;
/*TALK_CONSTANTS*/
namespace Otr { namespace Story { static int utf8Len(const char *) { return 0; } } }
static void choiceClicked(lv_event_t *) {}
/*CHOICE*/
static int stops, deletions;
static uint32_t nowMs() { return lv_tick_get(); }
static void stopPath() { ++stops; }
static void talkSkip() {}
static void screenDeleted(lv_event_t *) { ++deletions; }
/*JOY*/
static void configureJoy() {
    const auto j = Game::joyGeometry(s_layout.joystick);
    s_joy.configure(Otr::JoyConfig{}, j.radius, j.stopRadius);
}
/*PAD*/
/*TALK*/
// Hardware/game actions are stubbed; art, HUD and control construction are production code.
enum HudCount { C_LIVES, C_SUIT, C_KEYS, C_GEMS, C_COUNT };
static lv_obj_t *s_count[C_COUNT], *s_muteBtn, *s_hitBtn, *s_swapBtn, *s_bar, *s_barLbl, *s_itemBox;
static lv_obj_t *s_item[H_COUNT], *s_flashCell, *s_pogoCell, *s_flashLvl, *s_pogoLvl, *s_bombCancel;
static uint32_t s_menuTapMs, s_hitTapMs, s_actionTapMs;
static int s_actSel, s_actShown;
static constexpr int ACT_HIT = 0;
static Otr::World world, *s_world = &world;
/*EXTRA_STATE*/
static lv_obj_t *s_keyBox, *s_keyIcons[6];
static int s_bombSize;
static int s_prevFlash, s_prevPogo;
static int langIdx() { return !strcmp(Lang::currentLang(), "en"); }
static const char *advMapTitle() { return "A long map title"; }
/*HUD*/
/*EXTRA_TOGGLE*/
/*HUD_ICONS*/
static void showMenu() {}
static void toggleSound() {}
static void muteIcon() {}
static void doSwap() {}
static void doHit() {}
static void cancelBomb() {}
/*CONTROLS*/
/*GAME*/

static bool hidden(lv_obj_t *obj) { return lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN); }
static bool contains(Game::Rect outer, Game::Rect inner) {
    return inner.x>=outer.x && inner.y>=outer.y &&
           inner.x+inner.w<=outer.x+outer.w && inner.y+inner.h<=outer.y+outer.h;
}
static bool separate(Game::Rect a, Game::Rect b) {
    return a.x+a.w<=b.x || b.x+b.w<=a.x || a.y+a.h<=b.y || b.y+b.h<=a.y;
}
static lv_obj_t *hudToggle() {
    auto *button = Theme::button(s_scr, extraHudText(), 250, 40, Theme::LINE);
    lv_obj_add_event_cb(button, toggleExtraHud, LV_EVENT_CLICKED, nullptr);
    return button;
}
static void checkHud() {
    auto *button = hudToggle();
    assert(!s_extraHud && hidden(s_progressLabel) && hidden(s_timerLabel));
    assert(!hidden(s_stageLabel) && !strcmp(lv_label_get_text(s_stageLabel), advMapTitle()));
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(button, 0)),
                   langIdx() ? "Extra HUD info: OFF" : "Extra HUD-info: AV"));
    lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
    assert(s_extraHud && !hidden(s_progressLabel) && hidden(s_timerLabel));
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(button, 0)),
                   langIdx() ? "Extra HUD info: ON" : "Extra HUD-info: PÅ"));
    const std::string progress = lv_label_get_text(s_progressLabel);
    assert(progress == (langIdx() ? "Steps:0  Par:7" : "Steg:0  Par:7"));
    lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
    assert(!s_extraHud && hidden(s_progressLabel) && hidden(s_timerLabel));
    const char *map[] = {"S."};
    Otr::GroupControl group{10, 5};
    Otr::MapDef timed{90,map,1,Otr::CLASSIC_RULES,nullptr,0,nullptr,0,&group,1,7};
    assert(world.load(timed)); updateStats();
    assert(hidden(s_progressLabel) && hidden(s_timerLabel));
    lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
    assert(!hidden(s_progressLabel) && !hidden(s_timerLabel));
    assert(!strcmp(lv_label_get_text(s_timerLabel), langIdx() ? "Gate steps: A:0" : "Portsteg: A:0"));
    // Map travel must not reset the session preference, even when the new map has no timers.
    Otr::MapDef plain{91,map,1,Otr::CLASSIC_RULES,nullptr,0,nullptr,0,nullptr,0,0};
    assert(world.load(plain)); updateStats();
    assert(s_extraHud && !hidden(s_progressLabel) && hidden(s_timerLabel));
    lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
    assert(hidden(s_progressLabel) && hidden(s_timerLabel));
    lv_obj_delete(button);
}
static void checkKeys() {
    updateStats();
    assert(hidden(s_keyBox));
    for (auto *icon : s_keyIcons) assert(hidden(icon));
    std::set<std::vector<uint16_t>> images, badges;
    for (int i=0;i<6;++i) {
        images.emplace(s_spr->coloredKeys[i], s_spr->coloredKeys[i]+256);
        std::vector<uint16_t> badge;
        for (int y=0;y<7;++y) for (int x=11;x<16;++x) badge.push_back(s_spr->coloredKeys[i][y*16+x]);
        badges.insert(badge);
        assert(lv_obj_get_child_count(s_keyIcons[i])==0); // No quantity labels/badges.
    }
    assert(images.size()==6 && badges.size()==6);
    for (auto owned : {std::vector<int>{4}, {0,2,5}, {0,1,2,3,4,5}}) {
        for (int i=0;i<6;++i) world.setKeys(Otr::KeyColor(i),0);
        for (int i : owned) world.setKeys(Otr::KeyColor(i),42);
        updateStats(); lv_obj_update_layout(s_scr);
        assert(!hidden(s_keyBox));
        auto strip=AdvUi::gameBounds(s_keyBox);
        assert(lv_obj_get_parent(s_keyBox)==lv_obj_get_parent(s_hitBtn));
        assert(contains(AdvUi::gameBounds(lv_obj_get_parent(s_hitBtn)),strip));
        assert(strip.x+strip.w==s_layout.width-s_layout.controlPx(4));
        assert(lv_obj_get_y(s_keyBox)+strip.h==s_layout.controls.h-s_layout.controlPx(4));
        assert(strip.w==56 && strip.h==36);
        for (int i=0;i<6;++i) {
            const bool has=std::find(owned.begin(),owned.end(),i)!=owned.end();
            assert(hidden(s_keyIcons[i])!=has);
            if (!has) continue;
            auto bounds=AdvUi::gameBounds(s_keyIcons[i]);
            assert(lv_obj_get_parent(s_keyIcons[i])==s_keyBox);
            assert(bounds.x==strip.x+(i%3)*20 && bounds.y==strip.y+(i/3)*20);
            assert(bounds.x+bounds.w<=strip.x+strip.w && bounds.y+bounds.h<=strip.y+strip.h);
            assert(bounds.w==16 && bounds.h==16);
            const std::vector<uint16_t> before(s_spr->coloredKeys[i],s_spr->coloredKeys[i]+256);
            world.setKeys(Otr::KeyColor(i),1); updateStats();
            assert(!hidden(s_keyIcons[i]) && before==
                   std::vector<uint16_t>(s_spr->coloredKeys[i],s_spr->coloredKeys[i]+256));
        }
    }
    // All six keys coexist with every carried item in the control panel's right column.
    world.setBoat(true); world.setFlashlight(3); world.setPogo(3); world.setSuction(true);
    updateStats(); lv_obj_update_layout(s_scr);
    const auto strip=AdvUi::gameBounds(s_keyBox), items=AdvUi::gameBounds(s_itemBox);
    assert(contains(s_layout.controls,items) && separate(strip,items));
    assert(separate(items,AdvUi::gameBounds(s_bar)));
    std::vector<Game::Rect> cells;
    for (auto *cell : {s_item[H_BOAT],s_flashCell,s_pogoCell}) {
        assert(!hidden(cell));
        auto bounds=AdvUi::gameBounds(cell);
        assert(contains(items,bounds));
        for (auto other : cells) assert(separate(bounds,other));
        cells.push_back(bounds);
    }
    assert(!strcmp(lv_label_get_text(s_flashLvl),"3") && !strcmp(lv_label_get_text(s_pogoLvl),"3"));
    const auto suction=AdvUi::gameBounds(s_item[H_SUCTION]);
    assert(!hidden(s_item[H_SUCTION]) && contains(s_layout.controls,suction));
    assert(separate(strip,suction) && separate(items,suction));
    for (auto *level : {s_flashLvl,s_pogoLvl}) {
        assert(lv_obj_get_style_text_font(level,LV_PART_MAIN)==&my_font_14);
        assert(lv_obj_get_style_bg_opa(level,LV_PART_MAIN)==LV_OPA_COVER);
        assert(lv_color_eq(lv_obj_get_style_bg_color(level,LV_PART_MAIN),Theme::c(Theme::PANEL)));
        assert(!lv_color_eq(lv_obj_get_style_text_color(level,LV_PART_MAIN),Theme::c(Theme::PANEL)));
        assert(lv_obj_get_width(level)>=10 && lv_obj_get_height(level)>=my_font_14.line_height);
    }
    for (auto *cell : {s_flashCell,s_pogoCell})
        for (uint32_t i=0;i<lv_obj_get_child_count(cell);++i)
            assert(contains(AdvUi::gameBounds(cell),AdvUi::gameBounds(lv_obj_get_child(cell,i))));
    for (auto *icon : s_keyIcons) assert(!hidden(icon));
    for (auto *control : {s_itemBox,s_bar,s_hitBtn,s_swapBtn,s_muteBtn})
        if (control) assert(separate(strip,AdvUi::gameBounds(control)));
    assert(separate(strip,s_layout.joystick));
    // Bomb selection temporarily replaces the keys, without moving the cancel control.
    s_bombSize=3; lv_obj_set_hidden(s_bombCancel,false); updateStats();
    assert(hidden(s_keyBox));
    s_bombSize=0; lv_obj_set_hidden(s_bombCancel,true); updateStats();
    assert(!hidden(s_keyBox));
    const char *doors[]={"S.."};
    std::string row=doors[0]; row[1]=row[2]=char(Otr::COLORED_DOOR_GLYPH);
    const char *map[]={row.c_str()};
    assert(world.load(map,1,Otr::CLASSIC_RULES)); world.setKeys(Otr::KeyColor::RED,2); updateStats();
    assert(!hidden(s_keyIcons[0]));
    world.move(Dir::RIGHT); updateStats(); assert(world.keys(Otr::KeyColor::RED)==1 && !hidden(s_keyIcons[0]));
    world.move(Dir::RIGHT); world.move(Dir::RIGHT); updateStats();
    assert(!world.keys(Otr::KeyColor::RED) && hidden(s_keyIcons[0]) && hidden(s_keyBox));
}

static lv_point_t pointer;
static lv_indev_state_t state = LV_INDEV_STATE_RELEASED;
static void touch(lv_indev_t *indev, int x, int y, bool down) {
    pointer = {x, y};
    state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    lv_tick_inc(20);
    lv_indev_read(indev);
}
static void checkPad(lv_indev_t *indev) {
    auto *area = lv_obj_get_parent(s_joyKnob);
    auto a = AdvUi::gameBounds(area);
    const auto j = Game::joyGeometry(a);
    assert(a.w == s_layout.controls.h && a.h == s_layout.controls.h);
    assert(a.x == s_layout.joystick.x && a.y == s_layout.controls.y);
    auto *ring = lv_obj_get_child(area, 0);
    auto *stop = lv_obj_get_child(area, 2);
    const auto r = AdvUi::gameBounds(ring), s = AdvUi::gameBounds(stop);
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    assert(r.w == 2 * j.radius && r.h == 2 * j.radius && s.w == 2 * j.stopRadius && s.h == 2 * j.stopRadius);
    assert(s.x + s.w / 2 == cx && s.y + s.h / 2 == cy);
    auto d = Game::joyDelta(a, {cx, cy});
    assert(d.x == 0 && d.y == 0);
    const int before = stops;
    touch(indev, cx, cy, true);
    assert(stops == before + 1 && s_joy.held() && !s_joy.sample().moving);
    lv_obj_update_layout(s_scr);
    auto knob = AdvUi::gameBounds(s_joyKnob);
    assert(knob.w == 2 * j.knobRadius && knob.h == 2 * j.knobRadius);
    assert(knob.x + j.knobRadius == cx && knob.y + j.knobRadius == cy);
    touch(indev, cx + j.stopRadius, cy, true);
    assert(s_joy.sample().moving && s_joy.sample().zone == 1);
    const int boundary = j.stopRadius + (j.radius - j.stopRadius + 1) / 2;
    touch(indev, cx + boundary, cy, true);
    assert(s_joy.sample().zone == 2 && s_joy.sample().dir == Dir::RIGHT);
    const int corner = a.w - j.cx - 1;
    touch(indev, cx + corner, cy + corner, true);
    assert(s_joy.sample().zone == 2);  // Corners outside the drawn ring still accept input.
    lv_obj_update_layout(s_scr);
    knob = AdvUi::gameBounds(s_joyKnob);
    const int clamped = int((j.radius - j.knobRadius / 2) / sqrtf(2.f));
    assert(knob.x + j.knobRadius - cx == clamped && knob.y + j.knobRadius - cy == clamped);
    touch(indev, cx + corner, cy + corner, false);
    assert(!s_joy.held() && lv_obj_has_flag(s_joyKnob, LV_OBJ_FLAG_HIDDEN));
    touch(indev, cx, cy - 60, true);
    assert(s_joy.held());
    // Let LVGL generate PRESS_LOST while its pointer is active.
    lv_obj_remove_flag(area, LV_OBJ_FLAG_PRESS_LOCK);
    touch(indev, a.x - 10, a.y - 10, true);
    assert(!s_joy.held() && lv_obj_has_flag(s_joyKnob, LV_OBJ_FLAG_HIDDEN));
    touch(indev, a.x - 10, a.y - 10, false);
}
int main() {
    lv_init();
    makeArt(s_spr);
    {
        std::ifstream file("docs/adventures/z_resource_test/bitmaps/stone.png", std::ios::binary);
        std::vector<uint8_t> png{std::istreambuf_iterator<char>(file), {}};
        assert(!png.empty());
        Adv::ResourceArt art{};
        for (int repeat = 0; repeat < 32; ++repeat)
            assert(Adv::decodeResourcePng(png.data(), png.size(), art) && art.ready);
        int opaque = 0, transparent = 0;
        for (int p = 0; p < 1024; ++p) {
            opaque += art.bgra[p * 4 + 3] == 255;
            transparent += art.bgra[p * 4 + 3] == 0;
        }
        assert(opaque && transparent);
        assert(!Adv::decodeResourcePng(png.data(), 16, art) && !art.ready);
        auto wrongSize = png; wrongSize[19] = 64;
        assert(!Adv::decodeResourcePng(wrongSize.data(), wrongSize.size(), art));
        auto corrupt = png; corrupt[corrupt.size()/2] ^= 255;
        assert(!Adv::decodeResourcePng(corrupt.data(), corrupt.size(), art));
    }
    assert(!s_extraHud);
    auto *display = lv_display_create(320, 480);
    static uint8_t pixels[320 * 20 * 2];
    lv_display_set_buffers(display, pixels, nullptr, sizeof(pixels), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, [](lv_display_t *d, const lv_area_t *, uint8_t *) { lv_display_flush_ready(d); });
    auto *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, display);
    lv_indev_set_read_cb(indev, [](lv_indev_t *, lv_indev_data_t *data) { data->point = pointer; data->state = state; });
    for (auto size : {lv_point_t{320, 480}, lv_point_t{480, 800}, lv_point_t{481, 801}}) {
        lv_display_set_resolution(display, size.x, size.y);
        for (const char *language : {"sv","en"}) for (int visit = 0; visit < 2; ++visit) {
            assert(Lang::setLang(language));
            const bool mine = visit != 0;
            const char *map[]={"S."};
            auto rules=Otr::CLASSIC_RULES; rules.mineLevel=mine ? 1 : 0;
            assert(world.load(Otr::MapDef{90,map,1,rules,nullptr,0,nullptr,0,nullptr,0,7}));
            s_layout = Game::layout(UiMetrics::screenW(), UiMetrics::screenH());
            const auto stride = lv_draw_buf_width_to_stride(s_layout.view.w, LV_COLOR_FORMAT_RGB565);
            s_fbStride = stride / sizeof(uint16_t);
            std::vector<uint16_t> frame(Game::frameBytes(stride, s_layout.view.h) / sizeof(uint16_t));
            s_fb = frame.data();
            buildGame(false);
            updateStats();
            lv_screen_load(s_scr);
            lv_obj_update_layout(s_scr);
            auto root = AdvUi::gameBounds(s_gameRoot), canvas = AdvUi::gameBounds(s_canvas);
            assert(root.x == 0 && root.y == 0 && root.w == size.x && root.h == size.y);
            assert(canvas.x == 0 && canvas.y == Game::HUD_HEIGHT && canvas.w == size.x &&
                   canvas.h == size.y - size.x / 2 - Game::HUD_HEIGHT);
            const auto *drawBuffer = lv_canvas_get_draw_buf(s_canvas);
            assert(drawBuffer->header.stride == stride && drawBuffer->data == reinterpret_cast<uint8_t *>(s_fb));
            auto stage = AdvUi::gameBounds(s_stageLabel);
            assert(stage.x == 4 && stage.y == 4);
            const auto hud = AdvUi::gameBounds(lv_obj_get_parent(s_stageLabel));
            assert(contains(s_layout.hud, hud) && separate(hud, canvas));
            assert(contains(hud, stage));
            assert(stage.w < canvas.w - 8); // The base title is compact, not a full-width HUD bar.
            assert(hidden(s_progressLabel) && hidden(s_timerLabel));
            if (!mine) checkHud();
            checkKeys();
            lv_label_set_text(s_stageLabel, "A long map title must never cover the steps and par");
            lv_label_set_text(s_progressLabel, "Steps:2147483647  Par:2147483647");
            lv_obj_update_layout(s_scr);
            auto progress = AdvUi::gameBounds(s_progressLabel);
            assert(progress.x == 4 && progress.y == 24);
            assert(contains(hud, progress) && separate(progress, canvas));
            assert(lv_obj_get_y(s_stageLabel) + lv_obj_get_height(s_stageLabel) <= progress.y);
            lv_obj_set_hidden(s_timerLabel, false);
            lv_label_set_text(s_timerLabel, "Gate steps: 0:65535 A:0 Z:65535");
            lv_obj_update_layout(s_scr);
            const auto timer = AdvUi::gameBounds(s_timerLabel);
            assert(timer.x == size.x / 2 + 2 && timer.y == 24);
            assert(contains(hud, timer) && separate(timer, canvas) && separate(progress, timer));
            assert(lv_label_get_long_mode(s_timerLabel) == LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
            assert(lv_obj_get_parent(s_toast) == s_gameRoot);
            assert(lv_obj_get_y(s_toast) == canvas.y + canvas.h - 64);
            assert(lv_obj_get_width(s_toast) == canvas.w - 24);
            auto *controlPanel = lv_obj_get_parent(s_hitBtn);
            const auto controlBounds = AdvUi::gameBounds(controlPanel);
            assert(controlBounds.x == 0 && controlBounds.y == canvas.y + canvas.h);
            assert(controlBounds.w == size.x && controlBounds.h == size.x / 2);
            const auto px = [](int v) { return s_layout.controlPx(v); };
            const auto action = AdvUi::gameBounds(s_hitBtn);
            lv_area_t content;
            lv_obj_get_content_coords(controlPanel, &content);
            const int rightX = s_layout.joystick.x + s_layout.joystick.w;
            assert(action.x == rightX && action.w == size.x - rightX - px(4));
            assert(action.y == content.y1 + px(mine ? 8 : 28));
            assert(action.h == px(mine ? 56 : 36));
            auto *menu = lv_obj_get_child(controlPanel, 1);
            assert(lv_obj_get_x(menu) == px(4) && lv_obj_get_y(menu) == controlBounds.h - px(48));
            assert(lv_obj_get_width(menu) == px(36) && lv_obj_get_height(menu) == px(40));
            assert(lv_obj_get_x(s_muteBtn) == px(43) && lv_obj_get_height(s_muteBtn) == px(40));
            assert(lv_obj_get_y(s_bar) == px(74) && lv_obj_get_height(s_bar) == px(18));
            assert(lv_obj_get_parent(s_bombCancel) == controlPanel);
            assert(lv_obj_get_x(s_bombCancel) == rightX && lv_obj_get_y(s_bombCancel) == px(106));
            assert(lv_obj_get_height(s_bombCancel) == px(36));
            assert(!s_swapBtn || lv_obj_get_height(s_swapBtn) == px(18));
            checkPad(indev);
            talkBuild();
            lv_obj_update_layout(s_scr);
            const auto panel = AdvUi::gameBounds(s_talkPanel);
            assert(lv_obj_get_parent(s_talkBox) == s_gameRoot);
            assert(panel.x == 0 && panel.y == canvas.y + canvas.h && panel.w == size.x && panel.h == size.x / 2);
            assert(lv_obj_get_y(s_toast) == canvas.y + 8);
            assert(lv_obj_has_flag(s_talkPanel, LV_OBJ_FLAG_CLICKABLE));
            addChoice(0, 0, "Answer", nullptr, true);
            addChoice(1, 1, "Locked answer", "Hint", false);
            lv_obj_update_layout(s_scr);
            for (int i = 0; i < 2; ++i) {
                auto *answer = lv_obj_get_child(s_talkPanel, i);
                assert(lv_obj_get_height(answer) == px(36));
                assert(lv_obj_get_x(answer) == px(6));
                assert(lv_obj_get_y(answer) == px(2 + i * 40));
                assert(lv_obj_get_width(answer) == size.x - 2 * px(6));
            }
            const int before = stops;
            touch(indev, size.x / 2, panel.y + panel.h / 2, true);
            touch(indev, size.x / 2, panel.y + panel.h / 2, false);
            assert(stops == before && !s_joy.held());  // Panel covers the translated joystick.
            lv_obj_delete(s_talkBox);
            lv_obj_delete(s_talkPanel);
            // Actual bounds remain authoritative even after an additional parent translation.
            lv_obj_set_pos(s_gameRoot, 7, 9);
            lv_obj_update_layout(s_scr);
            float fx, fy;
            canvas = AdvUi::gameBounds(s_canvas);
            assert(Game::worldPoint(canvas, {canvas.x + 16, canvas.y + 32}, 37, 91, 16, fx, fy));
            assert(fx == 3.3125f && fy == 7.6875f);
            auto *blank = Theme::screen();
            lv_screen_load(blank);
            lv_obj_delete(s_scr);
            lv_obj_delete(blank);
        }
    }
    assert(deletions == 12);
    lv_indev_delete(indev);
    lv_display_delete(display);
    lv_deinit();
    puts("Adventure production viewport/pad/talk: 320x480, 480x800, 481x801; repeated entry/exit");
}
'''
# buildGame's pre-controls section references viewEvent; its transform is tested without LVGL.
code = code.replace("/*GEOMETRY*/", geometry).replace("/*TALK_CONSTANTS*/", talk_constants)
code = code.replace("/*JOY*/", joy).replace("/*PAD*/", pad).replace("/*TALK*/", talk)
code = code.replace("/*CONTROLS*/", controls)
code = code.replace("/*SPRITES*/", section("enum Spr", "static Sprites *s_spr"))
code = code.replace("/*ART*/", section("static uint16_t rgb(", "// Renderer").rsplit("// -----", 1)[0])
code = code.replace("/*KEY_COLORS*/", section("static constexpr uint32_t KEY_COLORS", "static int keyColorId"))
code = code.replace("/*COLORED_SPRITE*/", section("template<typename Pixel>", "static void coloredObject("))
code = code.replace("/*HUD_ICONS*/", section("static void makeHudIcons()", "// Bottom control panel"))
code = code.replace("/*HUD*/", section("static void setCount(", "static void toast("))
code = code.replace("/*EXTRA_STATE*/", section("static bool s_extraHud", "static lv_obj_t *s_keyBox"))
code = code.replace("/*EXTRA_TOGGLE*/", section("static const char *extraHudText()", "static void showMenu()"))
code = code.replace("/*CHOICE*/", choice)
code = code.replace("/*GAME*/", "static void viewEvent(lv_event_t *) {}\n" + game)
(WORK / "test.cpp").write_text(code)
(WORK / "sdkconfig.h").write_text("")
(WORK / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.22)
project(adv_game_layout LANGUAGES C CXX)
set(LV_CONF_SKIP ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_compile_definitions(LV_CONF_SKIP LV_COLOR_FORMAT_DEFAULT=LV_COLOR_FORMAT_RGB565 LV_DPI_DEF=130 LV_USE_LODEPNG=1)
add_subdirectory("{ROOT}/managed_components/lvgl__lvgl" lvgl)
add_executable(layout test.cpp "{ROOT}/components/my_ui/my_fonts.cpp" "{ROOT}/components/my_ui/fonts/my_font_raw_12.c" "{ROOT}/components/my_ui/fonts/my_font_raw_14.c" "{ROOT}/components/my_ui/fonts/my_font_raw_16.c" "{ROOT}/components/my_ui/fonts/my_font_raw_18.c" "{ROOT}/components/my_ui/fonts/my_font_raw_20.c" "{ROOT}/components/my_ui/fonts/my_font_raw_22.c" "{ROOT}/components/my_ui/fonts/my_font_raw_24.c" "{ROOT}/components/my_ui/fonts/my_font_raw_28.c" "{ROOT}/components/my_ui/fonts/my_font_raw_32.c" "{ROOT}/components/my_ui/fonts/my_font_raw_36.c" "{ROOT}/components/my_ui/fonts/my_font_raw_40.c" "{ROOT}/components/my_ui/fonts/my_font_raw_44.c" 
 "{ROOT}/main/ui_metrics.cpp" "{ROOT}/main/hw_profile.cpp" "{ROOT}/main/adv_resource_art.cpp"
 "{ROOT}/components/otr_engine/otr_world.cpp" "{ROOT}/main/lang/lang.cpp"
 "{ROOT}/main/lang/lang_sv.cpp" "{ROOT}/main/lang/lang_en.cpp")
target_include_directories(layout PRIVATE "{WORK}" "{ROOT}/components/my_ui/include" "{ROOT}/main"
 "{ROOT}/components/otr_engine/include")
target_link_libraries(layout PRIVATE lvgl m)
''')
flags = "-fsanitize=address,undefined -fno-omit-frame-pointer" if os.environ.get("ADV_UI_SANITIZE") == "1" else ""
subprocess.run(["cmake", "-S", str(WORK), "-B", str(WORK / "build"), "-G", "Ninja",
                f"-DCMAKE_CXX_FLAGS={flags}", f"-DCMAKE_C_FLAGS={flags}"], check=True)
subprocess.run(["cmake", "--build", str(WORK / "build"), "--target", "layout", "-j", "2"], check=True)
subprocess.run([str(WORK / "build/layout")], check=True)
