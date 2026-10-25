// ui_adventure.cpp - OnTheRoad Engine test world (see header).
#include "ui_adventure.h"
#include "adv_live.h"

#include <errno.h>
#include <math.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_config.h"
#include "app_players.h"
#include "board_audio.h"
#include "board_rgb.h"
#include "board_sd.h"
#include "lang.h"
#include "my_lvgl.h"
#include "my_storage.h"
#include "my_theme.h"
#include "otr_joystick.h"
#include "otr_maps.h"
#include "otr_stories.h"
#include "otr_story.h"
#include "otr_world.h"
#include "ui_yatzy.h"

namespace UiAdventure {

static const char *TAG = "adv";

using Otr::Dir;
using Otr::Obj;
using Otr::Tile;

// ----------------------------------------------------------------------------------------
// Layout
// ----------------------------------------------------------------------------------------
// v0.10.0: no top status bar. The viewport starts at y = 0 and the whole bottom strip
// (160 px) is the control panel: status + menu (left) | joystick (centre) | Slå, energy,
// gems, suit (right).
static constexpr int32_t VIEW_Y = 0, VIEW_W = 320, VIEW_H = 320;
static constexpr int32_t CTRL_Y = VIEW_Y + VIEW_H;               // 320
static constexpr int32_t CTRL_H = 480 - CTRL_Y;                  // 160
static constexpr uint32_t TICK_MS = 15;
static constexpr uint32_t WALK_MS_32 = 150, WALK_MS_16 = 115, BUMP_MS = 110, FX_MS = 220;
static constexpr uint32_t WALK_MS_MIN = 55;                      // Fastest walk animation (joystick)
static constexpr uint32_t WALK_MS_MAX = 260;                     // Slowest (stretched to the joystick pace)
// Control panel columns
static constexpr int32_t COL_L_W = 80;                           // Left: counters + menu/mute
static constexpr int32_t COL_R_X = 240, COL_R_W = 320 - COL_R_X - 4;   // Right: 76 px
// Joystick (centre column): pad centre (relative to the touch area), outer radius, stop zone, knob
static constexpr int32_t JOY_AREA_X = COL_L_W, JOY_AREA_W = COL_R_X - COL_L_W, JOY_AREA_H = CTRL_H;
static constexpr int32_t JOY_CX = JOY_AREA_W / 2, JOY_CY = JOY_AREA_H / 2, JOY_R = 74;
static constexpr int32_t JOY_STOP_R = JOY_R * 17 / 53, JOY_KNOB_R = JOY_R * 15 / 53;   // Same proportions as v0.9.0
static constexpr uint32_t TOAST_MS = 2200, ANIM_FRAME_MS = 420;
static constexpr int SPR = 32;                                   // Art is drawn at 32 px
static constexpr uint16_t KEY = 0xF81F;                          // Transparent (magenta)

// ----------------------------------------------------------------------------------------
// Sprites: generated once per visit (PSRAM), 32 px + 2x downscaled 16 px copies
// ----------------------------------------------------------------------------------------
enum Spr : uint8_t {
    S_GRASS0, S_GRASS1, S_SAND, S_FLOOR, S_RUBBLE, S_WALL, S_CRACK0, S_CRACK1, S_TREE,
    S_WATER0, S_WATER1, S_BRIDGE, S_LAVA0, S_LAVA1, S_BASALT, S_PLATE, S_GATE_C, S_GATE_O,
    S_ROCK, S_CRATE, S_KEY, S_DOOR, S_BOAT, S_GEM,
    S_PL_UP, S_PL_RIGHT, S_PL_DOWN, S_PL_LEFT, S_IMPACT,
    S_CHECK, S_CHECK_ON, S_EXIT, S_CRATER, S_FOG, S_FLAG, S_BUSH, S_POTION, S_HEART, S_SUIT, S_LIFE,
    S_MINE, S_NUM1, S_NUM2, S_NUM3, S_NUM4, S_NUM5, S_NUM6, S_NUM7, S_NUM8,
    S_NPC0,                                   // v0.11.0: people, NPC_LOOKS looks x 4 directions (Dir order)
    S_NPC_LAST = S_NPC0 + 3 * 4 - 1,
    S_COUNT
};

// HUD icons: 20 px copies of the world sprites, anti-aliased onto the panel colour so they can be
// shown with plain RGB565 canvases (no colour key needed in LVGL).
static constexpr int HUD_PX = 20;
enum HudIcon : uint8_t {
    H_LIFE, H_SUIT, H_KEY, H_GEM,   // Left column: counters
    H_BOAT,                         // Right column: carried items (on/off)
    H_COUNT
};
// Lives use the extra-life heart (green, +) so the counter matches the pickup that raises it.
static constexpr uint8_t HUD_SPRITE[H_COUNT] = {S_LIFE, S_SUIT, S_KEY, S_GEM, S_BOAT};

static constexpr int NPC_LOOKS = 3;           // Npc::look 0..2 (baker, shepherd, old man)
static constexpr int PORTRAIT_PX = 48;        // Dialogue portrait: head + shoulders of the sprite, 2x
struct Sprites {
    uint16_t big[S_COUNT][SPR * SPR];
    uint16_t small[S_COUNT][(SPR / 2) * (SPR / 2)];
    uint16_t hud[H_COUNT][HUD_PX * HUD_PX];
    uint16_t portrait[NPC_LOOKS][PORTRAIT_PX * PORTRAIT_PX];
};

static Sprites *s_spr = nullptr;
static Otr::World *s_world = nullptr;
static uint16_t *s_fb = nullptr;   // Canvas buffer VIEW_W x VIEW_H

static Otr::Planner *s_planner = nullptr;   // ~90 kB A* work area
static Otr::Path *s_path = nullptr;
static uint8_t *s_saveBuf = nullptr;         // Save file image
static constexpr size_t SAVE_CAP = 32 * 1024;   // >= World::saveSize() for 64 x 64

// Screen objects
static lv_obj_t *s_scr, *s_canvas, *s_toast, *s_dialog;
static lv_obj_t *s_activeScr;                // Screen that owns the PSRAM buffers (freed on its delete)
static lv_obj_t *s_bar, *s_barLbl, *s_muteBtn;
// HUD left column: one counter label per row (lives, suit, keys, gems)
enum HudCount : uint8_t { C_LIVES, C_SUIT, C_KEYS, C_GEMS, C_COUNT };
static lv_obj_t *s_count[C_COUNT];
static lv_obj_t *s_itemBox;                   // HUD right column: icons of carried items
static lv_obj_t *s_item[H_COUNT];             // Item icon objects (only item slots are used)
static uint32_t s_soundTapMs, s_zoomTapMs, s_menuTapMs, s_modeTapMs;   // Double-tap guards
static lv_timer_t *s_timer;

// View / animation state
static int s_ts = 32;                         // Tile size on screen (32 or 16)
static int s_fromX, s_fromY;                  // Player animation start cell
static uint32_t s_moveT0, s_moveMs;           // 0 ms = not moving
static bool s_rockAnim;                       // A pushed rock slides with the player
static int s_rockFromX, s_rockFromY, s_rockToX, s_rockToY;
static int s_bumpDx, s_bumpDy;
static uint32_t s_bumpT0;
static int s_fxX = -1, s_fxY = -1;            // Impact effect cell
static uint32_t s_fxT0;
static uint32_t s_toastUntil;
static Otr::JoyController s_joy;              // Joystick state (press / drag / release -> steps)
static lv_obj_t *s_joyKnob, *s_joyArrow[4];
static int s_joyLit = -1;                     // Highlighted arrow (Dir) or -1
static bool s_longDone;                       // Long press used (flag): ignore the release
static int s_pathPos = -1;                    // Tap-to-walk: next step in s_path, -1 = idle
static bool s_replanned;                      // NEED_BOAT walk already re-planned once
// Game session
static int s_worldIdx;                        // Index in Otr::WORLDS
static uint16_t s_playerId;                   // 0 = guest (never saved)
static bool s_saveWanted;                     // Save when idle (checkpoint)
static uint32_t s_lastSaveMs, s_savedRev;
static int s_savedSteps;
static char s_perfText[48];
static bool s_dirty = true;
static uint32_t s_lastRev, s_lastFrame;
static int32_t s_lastCamX = -1, s_lastCamY = -1;
// Statistics
static uint32_t s_renderUs, s_renderAccUs, s_renders, s_statT0;
// Story / dialogue (v0.11.0)
static const Otr::Story::Script *s_story;     // Script of the current world, nullptr = none
static Otr::Story::StoryState s_storyState;   // What the conversations changed (saved as .sts)
static Otr::Story::Dialog s_talk;
static int s_talkNum;                         // Person being talked to (map number), 0 = none
static int s_walkTalkNum;                     // Tap on a person: walk there, then talk to it
static int s_pendingChoice = -2;              // Picked answer, handled in the next tick (-1 = close)
static lv_obj_t *s_talkBox, *s_talkPortrait, *s_talkName, *s_talkText, *s_talkRef, *s_talkPanel;
static lv_obj_t *s_hitBtn;                    // Slå / Prata
static bool s_hitTalk;
static uint32_t s_hitTapMs, s_choiceTapMs;
static const char *s_twText;                  // Typewriter: full text, bytes shown, next char time
static int s_twLen, s_twPos, s_twChars;
static uint32_t s_twNext;
static char s_twBuf[4 * Otr::Story::MAX_TEXT_CHARS + 4];

static uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }

// ---- Pixel helpers on a 32 x 32 sprite ----------------------------------------------------
static uint16_t rgb(int r, int g, int b)
{
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    uint16_t c = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    return c == KEY ? (uint16_t)(KEY - 1) : c;
}

static uint32_t hash3(uint32_t x, uint32_t y, uint32_t seed)
{
    uint32_t h = x * 374761393u + y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void fillNoise(uint16_t *s, int r, int g, int b, int amp, uint32_t seed)
{
    for (int y = 0; y < SPR; y++)
        for (int x = 0; x < SPR; x++) {
            const int n = amp ? (int)(hash3(x, y, seed) % (uint32_t)(2 * amp + 1)) - amp : 0;
            s[y * SPR + x] = rgb(r + n, g + n, b + n / 2);
        }
}

static void fillKey(uint16_t *s)
{
    for (int i = 0; i < SPR * SPR; i++) s[i] = KEY;
}

static void px(uint16_t *s, int x, int y, uint16_t c)
{
    if (x >= 0 && y >= 0 && x < SPR && y < SPR) s[y * SPR + x] = c;
}

static void rect(uint16_t *s, int x0, int y0, int x1, int y1, uint16_t c)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) px(s, x, y, c);
}

static void disc(uint16_t *s, float cx, float cy, float r, uint16_t c)
{
    for (int y = 0; y < SPR; y++)
        for (int x = 0; x < SPR; x++) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            if (dx * dx + dy * dy <= r * r) s[y * SPR + x] = c;
        }
}

// Ball lit from the top left (rocks, tree crowns, gems' glow).
static void ball(uint16_t *s, float cx, float cy, float r, int cr, int cg, int cb, uint32_t seed, int amp)
{
    for (int y = 0; y < SPR; y++)
        for (int x = 0; x < SPR; x++) {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            if (dx * dx + dy * dy > r * r) continue;
            const float lx = x + 0.5f - (cx - r * 0.4f), ly = y + 0.5f - (cy - r * 0.4f);
            const float k = 1.25f - sqrtf(lx * lx + ly * ly) / (r * 1.6f);
            const int n = amp ? (int)(hash3(x, y, seed) % (uint32_t)(2 * amp + 1)) - amp : 0;
            s[y * SPR + x] = rgb((int)(cr * k) + n, (int)(cg * k) + n, (int)(cb * k) + n);
        }
}

static void line(uint16_t *s, int x0, int y0, int x1, int y1, uint16_t c, int thick = 1)
{
    const int steps = (abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0)) + 1;
    for (int i = 0; i <= steps; i++) {
        const int x = x0 + (x1 - x0) * i / steps, y = y0 + (y1 - y0) * i / steps;
        rect(s, x - thick / 2, y - thick / 2, x - thick / 2 + thick - 1, y - thick / 2 + thick - 1, c);
    }
}

// ---- Tile art ---------------------------------------------------------------------------
static void artGrass(uint16_t *s, uint32_t seed)
{
    fillNoise(s, 70, 150, 64, 10, seed);
    for (int i = 0; i < 14; i++) {   // Blades
        const int x = (int)(hash3(i, 1, seed) % 30) + 1, y = (int)(hash3(i, 2, seed) % 28) + 3;
        const uint16_t c = (i & 1) ? rgb(110, 196, 92) : rgb(44, 112, 46);
        px(s, x, y, c);
        px(s, x, y - 1, c);
        px(s, x + ((i & 2) ? 1 : -1), y - 2, c);
    }
}

static void artFloor(uint16_t *s, uint32_t seed)
{
    fillNoise(s, 118, 116, 124, 7, seed);
    const uint16_t seam = rgb(78, 76, 86), lite = rgb(146, 144, 152);
    for (int i = 0; i < SPR; i++) {
        px(s, i, 0, seam);
        px(s, i, 15, seam);
        px(s, 0, i, seam);
        px(s, (i < 16) ? 15 : 21, i, seam);
        px(s, i, 1, lite);
        px(s, i, 16, lite);
    }
}

static void artWall(uint16_t *s, int cracks)
{
    fillNoise(s, 74, 62, 58, 4, 7);
    for (int row = 0; row < 4; row++) {
        const int y0 = row * 8, off = (row & 1) ? 8 : 0;
        for (int bx = -16; bx < SPR; bx += 16) {
            const int x0 = bx + off;
            const int shade = (int)(hash3(row, bx + 40, 3) % 30);
            for (int y = y0 + 1; y < y0 + 7; y++)
                for (int x = x0 + 1; x < x0 + 15; x++) {
                    const int n = (int)(hash3(x, y, 11) % 13) - 6;
                    px(s, x, y, rgb(150 + shade + n, 78 + shade / 2 + n, 58 + n));
                }
            for (int x = x0 + 1; x < x0 + 15; x++) px(s, x, y0 + 1, rgb(184 + shade, 108, 84));
        }
    }
    if (cracks >= 1) {
        const uint16_t k = rgb(30, 22, 20);
        line(s, 16, 0, 13, 8, k, 2);
        line(s, 13, 8, 18, 15, k, 2);
        line(s, 18, 15, 12, 23, k, 2);
        line(s, 12, 23, 15, 31, k, 2);
        line(s, 13, 8, 6, 11, k, 1);
    }
    if (cracks >= 2) {
        const uint16_t k = rgb(22, 16, 14);
        line(s, 18, 15, 27, 12, k, 2);
        line(s, 12, 23, 4, 27, k, 2);
        line(s, 18, 15, 22, 26, k, 1);
        rect(s, 14, 12, 17, 18, rgb(40, 30, 26));
    }
}

static void artWater(uint16_t *s, int frame)
{
    for (int y = 0; y < SPR; y++)
        for (int x = 0; x < SPR; x++) {
            const float w = sinf((x + frame * 4) * 0.39f + y * 0.21f) + sinf(y * 0.55f - frame * 1.3f);
            const int n = (int)(w * 9);
            s[y * SPR + x] = rgb(36 + n / 2, 96 + n, 184 + n);
        }
    for (int i = 0; i < 4; i++) {   // Highlights
        const int x = (int)((hash3(i, frame, 5) % 24)), y = (int)(hash3(i, 9, 5) % 28) + 2;
        line(s, x, y, x + 5, y, rgb(150, 200, 250));
    }
}

static void artLava(uint16_t *s, int frame)
{
    for (int y = 0; y < SPR; y++)
        for (int x = 0; x < SPR; x++) {
            const float w = sinf(x * 0.31f + frame * 1.7f) * cosf(y * 0.27f - frame) + sinf((x + y) * 0.18f);
            const int n = (int)(w * 40);
            s[y * SPR + x] = rgb(214 + n / 2, 70 + n, 20);
        }
    for (int i = 0; i < 3; i++) {
        const float cx = (float)(hash3(i, frame, 8) % 26) + 3, cy = (float)(hash3(i, 3, 8) % 26) + 3;
        disc(s, cx, cy, 2.5f, rgb(255, 214, 90));
    }
}

// A person: the hero (look -1) or a villager (Npc::look 0..NPC_LOOKS-1). Same body, own colours
// and one detail each (baker's hat, shepherd's crook, old man's beard).
struct PersonLook {
    uint8_t tr, tg, tb;   // Tunic (shaded left -> right)
    uint8_t hr, hg, hb;   // Hair
    uint8_t extra;        // 0 none, 1 chef's hat, 2 crook, 3 beard
};
static constexpr PersonLook HERO_LOOK = {255, 150, 58, 92, 58, 34, 0};
static constexpr PersonLook NPC_LOOK[NPC_LOOKS] = {
    {240, 236, 226, 150, 96, 52, 1},    // Baker: white clothes, chef's hat
    {104, 176, 86, 214, 170, 86, 2},    // Shepherd: green tunic, fair hair, crook
    {136, 98, 160, 206, 206, 212, 3},   // Old man: purple robe, grey hair, beard
};

static void artPerson(uint16_t *s, Dir d, const PersonLook &lk)
{
    fillKey(s);
    // Shadow
    for (int y = 26; y < 31; y++)
        for (int x = 8; x < 24; x++) {
            const float dx = (x - 15.5f) / 8.f, dy = (y - 28.f) / 2.5f;
            if (dx * dx + dy * dy <= 1.f) px(s, x, y, rgb(20, 40, 24));
        }
    // Crook behind the body when seen from the front / back
    const uint16_t wood = rgb(120, 78, 40);
    const int crookX = d == Dir::LEFT ? 5 : 26;
    if (lk.extra == 2) {
        line(s, crookX, 6, crookX, 29, wood, 2);
        line(s, crookX, 6, crookX + (d == Dir::LEFT ? 3 : -3), 3, wood, 2);
        line(s, crookX + (d == Dir::LEFT ? 3 : -3), 3, crookX + (d == Dir::LEFT ? 5 : -5), 5, wood, 2);
    }
    // Body (tunic) + belt
    for (int y = 14; y < 28; y++)
        for (int x = 9; x < 23; x++) {
            const float dx = (x - 15.5f) / 7.f;
            if (y > 16 || dx * dx < 0.6f) px(s, x, y, rgb(lk.tr - (x - 9) * 4, lk.tg - (x - 9) * 3, lk.tb - (x - 9) * (lk.extra ? 2 : 0)));
        }
    rect(s, 9, 22, 22, 23, lk.extra == 1 ? rgb(200, 196, 186) : rgb(90, 56, 30));
    // Head
    disc(s, 15.5f, 10.f, 6.5f, rgb(244, 200, 160));
    // Hair
    const uint16_t hair = rgb(lk.hr, lk.hg, lk.hb);
    for (int y = 3; y < 17; y++)
        for (int x = 8; x < 24; x++) {
            const float dx = x + 0.5f - 15.5f, dy = y + 0.5f - 10.f;
            if (dx * dx + dy * dy > 6.5f * 6.5f) continue;
            const bool top = y < 8;
            bool on = top;
            if (d == Dir::UP) on = true;
            if (d == Dir::LEFT && x > 18) on = true;
            if (d == Dir::RIGHT && x < 13) on = true;
            if (on) px(s, x, y, hair);
        }
    // Beard (not seen from behind)
    if (lk.extra == 3 && d != Dir::UP) {
        const float bx = d == Dir::LEFT ? 13.5f : d == Dir::RIGHT ? 17.5f : 15.5f;
        for (int y = 12; y < 20; y++)
            for (int x = 9; x < 23; x++) {
                const float dx = (x + 0.5f - bx) / 4.5f, dy = (y + 0.5f - 13.f) / 6.5f;
                if (dx * dx + dy * dy <= 1.f && y >= 13) px(s, x, y, rgb(232, 232, 236));
            }
    }
    const uint16_t eye = rgb(30, 30, 46);
    switch (d) {
    case Dir::DOWN: px(s, 13, 11, eye); px(s, 18, 11, eye); px(s, 13, 10, eye); px(s, 18, 10, eye); break;
    case Dir::LEFT: px(s, 11, 10, eye); px(s, 11, 11, eye); break;
    case Dir::RIGHT: px(s, 20, 10, eye); px(s, 20, 11, eye); break;
    default: break;
    }
    // Chef's hat: puffy white top on a band
    if (lk.extra == 1) {
        const uint16_t w = rgb(250, 250, 248), sh = rgb(214, 214, 210);
        disc(s, 12.5f, 2.5f, 3.2f, w);
        disc(s, 18.5f, 2.5f, 3.2f, w);
        disc(s, 15.5f, 1.8f, 3.4f, w);
        rect(s, 10, 3, 21, 5, w);
        rect(s, 10, 5, 21, 5, sh);
    }
    // Arms hint at the facing direction
    const uint16_t skin = rgb(230, 186, 146);
    if (d == Dir::LEFT) rect(s, 6, 17, 9, 19, skin);
    else if (d == Dir::RIGHT) rect(s, 22, 17, 25, 19, skin);
    else { rect(s, 7, 17, 9, 21, skin); rect(s, 22, 17, 24, 21, skin); }
    if (lk.extra == 2 && d != Dir::UP) rect(s, crookX - 1, 17, crookX + 1, 19, skin);   // Hand on the crook
}

static void artPlayer(uint16_t *s, Dir d) { artPerson(s, d, HERO_LOOK); }

// Portrait: rows 0..23, columns 4..27 of the front sprite, 2x nearest (crisp pixel art) on a
// soft vertical gradient, so it can be an RGB565 canvas without colour key.
static void makePortrait(uint16_t *out, const uint16_t *front)
{
    for (int y = 0; y < PORTRAIT_PX; y++)
        for (int x = 0; x < PORTRAIT_PX; x++) {
            const uint16_t c = front[(y / 2) * SPR + 4 + x / 2];
            out[y * PORTRAIT_PX + x] = c != KEY ? c : rgb(44 + y / 3, 70 + y / 3, 100 + y / 4);
        }
}

static void makeArt(Sprites *sp)
{
    uint16_t(*b)[SPR * SPR] = sp->big;
    artGrass(b[S_GRASS0], 1);
    artGrass(b[S_GRASS1], 2);
    fillNoise(b[S_SAND], 216, 192, 132, 9, 3);
    for (int i = 0; i < 10; i++) px(b[S_SAND], (int)(hash3(i, 1, 4) % 32), (int)(hash3(i, 2, 4) % 32), rgb(170, 146, 96));
    artFloor(b[S_FLOOR], 5);
    artFloor(b[S_RUBBLE], 6);
    for (int i = 0; i < 7; i++) {
        const float cx = (float)(hash3(i, 1, 6) % 26) + 3, cy = (float)(hash3(i, 2, 6) % 26) + 3;
        ball(b[S_RUBBLE], cx, cy, 2.5f + (i % 3), 150, 82, 62, 0, 0);
    }
    artWall(b[S_WALL], 0);
    artWall(b[S_CRACK0], 1);
    artWall(b[S_CRACK1], 2);
    artGrass(b[S_TREE], 1);
    rect(b[S_TREE], 14, 20, 18, 29, rgb(104, 70, 40));
    disc(b[S_TREE], 16.f, 29.f, 2.f, rgb(30, 70, 30));
    ball(b[S_TREE], 16.f, 13.f, 12.f, 52, 140, 56, 12, 14);
    artWater(b[S_WATER0], 0);
    artWater(b[S_WATER1], 1);
    artWater(b[S_BRIDGE], 0);
    for (int p = 0; p < 4; p++) {   // Planks
        const int y0 = 2 + p * 7;
        rect(b[S_BRIDGE], 3, y0, 28, y0 + 5, rgb(160 - p * 6, 112 - p * 4, 64));
        line(b[S_BRIDGE], 3, y0 + 5, 28, y0 + 5, rgb(96, 64, 36));
        px(b[S_BRIDGE], 6, y0 + 2, rgb(70, 50, 30));
        px(b[S_BRIDGE], 25, y0 + 2, rgb(70, 50, 30));
    }
    artLava(b[S_LAVA0], 0);
    artLava(b[S_LAVA1], 1);
    fillNoise(b[S_BASALT], 64, 62, 70, 8, 9);
    line(b[S_BASALT], 0, 10, 12, 8, rgb(36, 34, 40));
    line(b[S_BASALT], 12, 8, 20, 18, rgb(36, 34, 40));
    line(b[S_BASALT], 20, 18, 31, 15, rgb(36, 34, 40));
    line(b[S_BASALT], 12, 8, 10, 0, rgb(36, 34, 40));
    line(b[S_BASALT], 20, 18, 16, 31, rgb(36, 34, 40));
    artGrass(b[S_PLATE], 2);
    rect(b[S_PLATE], 4, 4, 27, 27, rgb(96, 96, 108));
    rect(b[S_PLATE], 6, 6, 25, 25, rgb(170, 168, 180));
    rect(b[S_PLATE], 6, 6, 25, 7, rgb(214, 212, 222));
    disc(b[S_PLATE], 16.f, 16.f, 4.f, rgb(255, 190, 80));
    artFloor(b[S_GATE_C], 7);
    rect(b[S_GATE_C], 0, 2, 31, 4, rgb(60, 62, 72));
    for (int x = 3; x < 32; x += 6) rect(b[S_GATE_C], x, 2, x + 1, 31, rgb(70 + x, 74 + x, 90 + x));
    rect(b[S_GATE_C], 0, 16, 31, 17, rgb(60, 62, 72));
    artFloor(b[S_GATE_O], 7);
    rect(b[S_GATE_O], 0, 0, 31, 3, rgb(60, 62, 72));
    for (int x = 3; x < 32; x += 6) rect(b[S_GATE_O], x, 0, x + 1, 5, rgb(110, 114, 130));
    // Objects (transparent)
    fillKey(b[S_ROCK]);
    disc(b[S_ROCK], 16.f, 25.f, 10.f, rgb(30, 40, 30));
    ball(b[S_ROCK], 16.f, 16.f, 12.5f, 160, 156, 150, 21, 10);
    line(b[S_ROCK], 9, 18, 14, 22, rgb(90, 88, 84));
    fillKey(b[S_CRATE]);
    rect(b[S_CRATE], 4, 4, 27, 28, rgb(92, 58, 28));
    rect(b[S_CRATE], 6, 6, 25, 26, rgb(176, 120, 62));
    for (int y = 6; y <= 26; y += 5) line(b[S_CRATE], 6, y, 25, y, rgb(146, 96, 46));
    line(b[S_CRATE], 6, 6, 25, 26, rgb(110, 70, 34), 3);
    line(b[S_CRATE], 25, 6, 6, 26, rgb(110, 70, 34), 3);
    fillKey(b[S_KEY]);
    disc(b[S_KEY], 11.f, 16.f, 6.f, rgb(214, 160, 30));
    disc(b[S_KEY], 11.f, 16.f, 2.5f, KEY);
    rect(b[S_KEY], 16, 15, 28, 17, rgb(240, 196, 60));
    rect(b[S_KEY], 24, 18, 25, 21, rgb(240, 196, 60));
    rect(b[S_KEY], 27, 18, 28, 20, rgb(240, 196, 60));
    px(b[S_KEY], 9, 12, rgb(255, 240, 160));
    fillKey(b[S_DOOR]);
    rect(b[S_DOOR], 2, 0, 29, 31, rgb(70, 46, 24));
    rect(b[S_DOOR], 5, 3, 26, 31, rgb(150, 96, 48));
    for (int x = 10; x < 26; x += 5) line(b[S_DOOR], x, 3, x, 31, rgb(118, 74, 36));
    rect(b[S_DOOR], 5, 10, 26, 11, rgb(60, 60, 66));
    rect(b[S_DOOR], 5, 23, 26, 24, rgb(60, 60, 66));
    disc(b[S_DOOR], 21.f, 17.f, 2.5f, rgb(240, 196, 60));
    rect(b[S_DOOR], 20, 18, 21, 20, rgb(30, 20, 10));
    fillKey(b[S_BOAT]);
    for (int y = 14; y < 27; y++)
        for (int x = 1; x < 31; x++) {
            const float dx = (x - 15.5f) / 15.f, dy = (y - 14.f) / 12.f;
            if (dx * dx + dy * dy <= 1.f) px(b[S_BOAT], x, y, rgb(140 - (y - 14) * 4, 88 - (y - 14) * 3, 44));
        }
    rect(b[S_BOAT], 1, 14, 30, 16, rgb(196, 140, 80));
    rect(b[S_BOAT], 6, 18, 25, 19, rgb(100, 64, 30));
    fillKey(b[S_GEM]);
    for (int y = 6; y < 28; y++)
        for (int x = 4; x < 28; x++) {
            const int dx = abs(x - 16), top = y < 13;
            const bool in = top ? (dx <= 7 + (y - 6) && y >= 6 && dx <= 11) : (dx <= (27 - y) * 11 / 14);
            if (!in) continue;
            const bool lite = top ? (x < 16) : (x < 16 - (y - 13) / 3);
            px(b[S_GEM], x, y, lite ? rgb(150, 240, 255) : rgb(30, 160, 220));
        }
    line(b[S_GEM], 5, 13, 26, 13, rgb(220, 250, 255));
    rect(b[S_GEM], 10, 8, 11, 9, rgb(255, 255, 255));
    artPlayer(b[S_PL_UP], Dir::UP);
    artPlayer(b[S_PL_RIGHT], Dir::RIGHT);
    artPlayer(b[S_PL_DOWN], Dir::DOWN);
    artPlayer(b[S_PL_LEFT], Dir::LEFT);
    for (int l = 0; l < NPC_LOOKS; l++) {
        for (int d = 0; d < 4; d++) artPerson(b[S_NPC0 + l * 4 + d], (Dir)d, NPC_LOOK[l]);
        makePortrait(sp->portrait[l], b[S_NPC0 + l * 4 + (int)Dir::DOWN]);
    }
    fillKey(b[S_IMPACT]);
    for (int i = 0; i < 8; i++) {
        const float a = i * 0.785f;
        line(b[S_IMPACT], 16 + (int)(cosf(a) * 4), 16 + (int)(sinf(a) * 4), 16 + (int)(cosf(a) * 13), 16 + (int)(sinf(a) * 13),
             (i & 1) ? rgb(255, 220, 120) : rgb(255, 255, 240), 2);
    }
    disc(b[S_IMPACT], 16.f, 16.f, 3.5f, rgb(255, 255, 255));

    // ---- v0.8.0 tiles / items -----------------------------------------------------------
    for (int on = 0; on < 2; on++) {   // Checkpoint: stone ring with a flag (gold once active)
        uint16_t *s = b[on ? S_CHECK_ON : S_CHECK];
        artGrass(s, 3);
        disc(s, 16.f, 22.f, 9.f, rgb(120, 118, 112));
        disc(s, 16.f, 22.f, 6.5f, rgb(164, 160, 150));
        rect(s, 15, 4, 16, 22, rgb(60, 44, 30));
        const uint16_t f = on ? rgb(255, 210, 60) : rgb(200, 200, 210);
        for (int y = 4; y < 12; y++) rect(s, 17, y, 17 + (y < 8 ? y - 3 : 12 - y) * 2, y, f);
    }
    artGrass(b[S_EXIT], 4);   // Exit: stone arch with stairs going down
    rect(b[S_EXIT], 3, 2, 28, 30, rgb(96, 92, 100));
    rect(b[S_EXIT], 7, 6, 24, 30, rgb(20, 18, 28));
    for (int i = 0; i < 4; i++) rect(b[S_EXIT], 8 + i * 2, 12 + i * 5, 23 - i * 2, 13 + i * 5, rgb(110 - i * 20, 104 - i * 20, 90 - i * 18));
    rect(b[S_EXIT], 3, 2, 28, 4, rgb(150, 146, 156));
    artGrass(b[S_CRATER], 5);
    disc(b[S_CRATER], 16.f, 17.f, 12.f, rgb(70, 52, 34));
    disc(b[S_CRATER], 16.f, 17.f, 8.5f, rgb(40, 28, 20));
    disc(b[S_CRATER], 14.f, 15.f, 3.f, rgb(24, 18, 14));
    for (int i = 0; i < 6; i++) px(b[S_CRATER], (int)(hash3(i, 1, 9) % 30) + 1, (int)(hash3(i, 2, 9) % 30) + 1, rgb(90, 70, 50));
    // Fog: raised minesweeper block (light top / left, dark bottom / right)
    fillNoise(b[S_FOG], 112, 132, 112, 5, 13);
    rect(b[S_FOG], 0, 0, 31, 2, rgb(178, 196, 176));
    rect(b[S_FOG], 0, 0, 2, 31, rgb(160, 180, 158));
    rect(b[S_FOG], 0, 29, 31, 31, rgb(62, 76, 62));
    rect(b[S_FOG], 29, 0, 31, 31, rgb(70, 86, 70));
    fillKey(b[S_FLAG]);
    rect(b[S_FLAG], 13, 6, 14, 26, rgb(30, 30, 34));
    rect(b[S_FLAG], 9, 25, 19, 27, rgb(30, 30, 34));
    for (int y = 6; y < 17; y++) rect(b[S_FLAG], 15, y, 15 + (y < 11 ? (y - 5) * 2 : (16 - y) * 2), y, rgb(230, 40, 40));
    fillKey(b[S_BUSH]);
    disc(b[S_BUSH], 16.f, 26.f, 9.f, rgb(26, 60, 26));
    ball(b[S_BUSH], 10.f, 19.f, 8.f, 46, 130, 50, 31, 12);
    ball(b[S_BUSH], 22.f, 19.f, 8.f, 46, 130, 50, 32, 12);
    ball(b[S_BUSH], 16.f, 13.f, 9.f, 60, 150, 60, 33, 12);
    for (int i = 0; i < 5; i++) disc(b[S_BUSH], (float)(hash3(i, 1, 34) % 18) + 7, (float)(hash3(i, 2, 34) % 14) + 9, 1.3f, rgb(220, 60, 80));
    fillKey(b[S_POTION]);
    rect(b[S_POTION], 13, 4, 18, 7, rgb(150, 100, 60));
    rect(b[S_POTION], 13, 8, 18, 13, rgb(200, 220, 230));
    disc(b[S_POTION], 16.f, 20.f, 8.5f, rgb(200, 220, 230));
    disc(b[S_POTION], 16.f, 21.f, 7.f, rgb(60, 220, 120));
    rect(b[S_POTION], 9, 15, 23, 17, rgb(200, 220, 230));
    disc(b[S_POTION], 13.f, 18.f, 1.8f, rgb(220, 255, 230));
    for (int k = 0; k < 2; k++) {   // Heart (red) and extra life (green with a plus)
        uint16_t *s = b[k ? S_LIFE : S_HEART];
        const int r = k ? 70 : 230, g = k ? 210 : 40, bl = k ? 90 : 60;
        fillKey(s);
        ball(s, 11.f, 12.f, 6.5f, r, g, bl, 0, 0);
        ball(s, 21.f, 12.f, 6.5f, r, g, bl, 0, 0);
        for (int y = 12; y < 28; y++) {
            const int half = (27 - y) * 13 / 15;
            rect(s, 16 - half, y, 15 + half, y, rgb(r * 8 / 10, g * 8 / 10, bl * 8 / 10));
        }
        disc(s, 10.f, 10.f, 2.f, rgb(255, 230, 230));
        if (k) {
            rect(s, 14, 10, 17, 22, rgb(255, 255, 255));
            rect(s, 10, 14, 21, 17, rgb(255, 255, 255));
        }
    }
    fillKey(b[S_SUIT]);   // Silver suit with an orange helmet visor
    rect(b[S_SUIT], 9, 14, 22, 26, rgb(196, 200, 210));
    rect(b[S_SUIT], 5, 15, 8, 23, rgb(170, 174, 186));
    rect(b[S_SUIT], 23, 15, 26, 23, rgb(170, 174, 186));
    rect(b[S_SUIT], 10, 27, 14, 30, rgb(120, 124, 136));
    rect(b[S_SUIT], 17, 27, 21, 30, rgb(120, 124, 136));
    rect(b[S_SUIT], 9, 20, 22, 21, rgb(255, 140, 40));
    disc(b[S_SUIT], 16.f, 9.f, 7.f, rgb(214, 218, 226));
    rect(b[S_SUIT], 11, 7, 20, 11, rgb(255, 150, 30));
    rect(b[S_SUIT], 12, 7, 15, 8, rgb(255, 220, 150));
    fillKey(b[S_MINE]);
    for (int i = 0; i < 8; i++) {
        const float a = i * 0.785f;
        line(b[S_MINE], 16, 16, 16 + (int)(cosf(a) * 13), 16 + (int)(sinf(a) * 13), rgb(30, 30, 34), 2);
    }
    ball(b[S_MINE], 16.f, 16.f, 9.f, 70, 70, 80, 0, 0);
    disc(b[S_MINE], 13.f, 13.f, 2.f, rgb(230, 230, 240));
    disc(b[S_MINE], 19.f, 19.f, 1.8f, rgb(230, 40, 40));
    // Minesweeper digits 1..8 on a pale chip, classic colours, 5 x 7 glyphs drawn at 2x
    static const uint8_t GLYPH[8][7] = {
        {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
        {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}, {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
        {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    };
    static const uint8_t DIGIT_RGB[8][3] = {{25, 60, 220}, {20, 130, 40}, {210, 30, 30}, {20, 20, 130},
                                            {130, 20, 20}, {0, 128, 128}, {20, 20, 20}, {100, 100, 100}};
    for (int d = 0; d < 8; d++) {
        uint16_t *s = b[S_NUM1 + d];
        fillKey(s);
        rect(s, 9, 7, 22, 24, rgb(236, 232, 210));
        rect(s, 8, 8, 23, 23, rgb(236, 232, 210));
        const uint16_t c = rgb(DIGIT_RGB[d][0], DIGIT_RGB[d][1], DIGIT_RGB[d][2]);
        for (int y = 0; y < 7; y++)
            for (int x = 0; x < 5; x++)
                if (GLYPH[d][y] & (0x10 >> x)) rect(s, 11 + x * 2, 9 + y * 2, 12 + x * 2, 10 + y * 2, c);
    }

    // 16 px copies: 2x2 box filter; a pixel stays transparent when 2+ source pixels are.
    for (int i = 0; i < S_COUNT; i++)
        for (int y = 0; y < SPR / 2; y++)
            for (int x = 0; x < SPR / 2; x++) {
                int r = 0, g = 0, bl = 0, n = 0, keys = 0;
                for (int k = 0; k < 4; k++) {
                    const uint16_t c = b[i][(y * 2 + k / 2) * SPR + x * 2 + (k & 1)];
                    if (c == KEY) { keys++; continue; }
                    r += c >> 11;
                    g += (c >> 5) & 0x3F;
                    bl += c & 0x1F;
                    n++;
                }
                uint16_t out = KEY;
                if (keys < 2 && n) {
                    out = (uint16_t)(((r / n) << 11) | ((g / n) << 5) | (bl / n));
                    if (out == KEY) out--;
                }
                sp->small[i][y * (SPR / 2) + x] = out;
            }
}

// ----------------------------------------------------------------------------------------
// Renderer
// ----------------------------------------------------------------------------------------
static const uint16_t *spr(int id) { return s_ts == 32 ? s_spr->big[id] : s_spr->small[id]; }

// Copies a ts x ts sprite to the frame buffer at (sx, sy), clipped to the viewport.
static void blit(int id, int sx, int sy, bool keyed)
{
    const int ts = s_ts;
    int x0 = sx < 0 ? -sx : 0, y0 = sy < 0 ? -sy : 0;
    int x1 = sx + ts > VIEW_W ? VIEW_W - sx : ts, y1 = sy + ts > VIEW_H ? VIEW_H - sy : ts;
    if (x0 >= x1 || y0 >= y1) return;
    const uint16_t *src = spr(id);
    for (int y = y0; y < y1; y++) {
        const uint16_t *s = src + y * ts + x0;
        uint16_t *d = s_fb + (sy + y) * VIEW_W + sx + x0;
        if (!keyed) {
            memcpy(d, s, (size_t)(x1 - x0) * 2);
        } else {
            for (int x = x0; x < x1; x++, s++, d++)
                if (*s != KEY) *d = *s;
        }
    }
}

static int terrainSprite(int x, int y, int frame)
{
    switch (s_world->tile(x, y)) {
    case Tile::GRASS: return (hash3(x, y, 77) & 3) == 0 ? S_GRASS1 : S_GRASS0;
    case Tile::SAND: return S_SAND;
    case Tile::FLOOR: return S_FLOOR;
    case Tile::RUBBLE: return S_RUBBLE;
    case Tile::WALL: return S_WALL;
    case Tile::CRACKED: return s_world->damage(x, y) ? S_CRACK1 : S_CRACK0;
    case Tile::TREE: return S_TREE;
    case Tile::WATER: return frame ? S_WATER1 : S_WATER0;
    case Tile::BRIDGE: return S_BRIDGE;
    case Tile::LAVA: return frame ? S_LAVA1 : S_LAVA0;
    case Tile::BASALT: return S_BASALT;
    case Tile::PLATE: return S_PLATE;
    case Tile::GATE: return s_world->gatesOpen() ? S_GATE_O : S_GATE_C;
    case Tile::CHECKPOINT:
        return x == s_world->checkpointX() && y == s_world->checkpointY() ? S_CHECK_ON : S_CHECK;
    case Tile::EXIT: return S_EXIT;
    case Tile::CRATER: return S_CRATER;
    default: return S_WALL;
    }
}

static int objSprite(Obj o)
{
    switch (o) {
    case Obj::ROCK: return S_ROCK;
    case Obj::CRATE: return S_CRATE;
    case Obj::KEY: return S_KEY;
    case Obj::DOOR: return S_DOOR;
    case Obj::BOAT: return S_BOAT;
    case Obj::GEM: return S_GEM;
    case Obj::BUSH: return S_BUSH;
    case Obj::POTION: return S_POTION;
    case Obj::HEART: return S_HEART;
    case Obj::SUIT: return S_SUIT;
    case Obj::LIFE: return S_LIFE;
    default: return -1;
    }
}

// A person looks at the hero when they are close (dominant axis), otherwise it looks around now
// and then.
static int npcSprite(int x, int y, uint32_t now)
{
    const int num = s_world->npcAt(x, y);
    const Otr::Story::Npc *n = s_story ? s_story->npcByNum(num) : nullptr;
    const int look = (n ? n->look : num + NPC_LOOKS - 1) % NPC_LOOKS;
    const int dx = s_world->playerX() - x, dy = s_world->playerY() - y;
    Dir d;
    if (num == s_talkNum || (abs(dx) <= 3 && abs(dy) <= 3)) {
        d = abs(dx) >= abs(dy) ? (dx < 0 ? Dir::LEFT : Dir::RIGHT) : (dy < 0 ? Dir::UP : Dir::DOWN);
    } else {
        static constexpr Dir IDLE[4] = {Dir::DOWN, Dir::LEFT, Dir::DOWN, Dir::RIGHT};
        d = IDLE[hash3((uint32_t)num, now / 2600, 31) & 3];
    }
    return S_NPC0 + look * 4 + (int)d;
}

static float moveProgress(uint32_t now)
{
    if (!s_moveMs) return 1.f;
    const float t = (float)(now - s_moveT0) / (float)s_moveMs;
    return t >= 1.f ? 1.f : t;
}

// Player position in world pixels (interpolated while walking, nudged when bumping).
static void playerPixel(uint32_t now, int &x, int &y)
{
    const float t = moveProgress(now);
    const int ts = s_ts;
    x = (int)lroundf((s_fromX + (s_world->playerX() - s_fromX) * t) * ts);
    y = (int)lroundf((s_fromY + (s_world->playerY() - s_fromY) * t) * ts);
    if (s_bumpT0 && now - s_bumpT0 < BUMP_MS) {
        const float k = sinf((float)(now - s_bumpT0) / BUMP_MS * 3.14159f) * ts * 0.12f;
        x += (int)(s_bumpDx * k);
        y += (int)(s_bumpDy * k);
    }
}

static void camera(int ppx, int ppy, int32_t &cx, int32_t &cy)
{
    const int ww = s_world->width() * s_ts, wh = s_world->height() * s_ts;
    cx = ppx + s_ts / 2 - VIEW_W / 2;
    cy = ppy + s_ts / 2 - VIEW_H / 2;
    if (s_talkPanel) cy += VIEW_H / 4;   // Talking: hero in the upper half, above the dialogue box
    cx = cx < 0 ? 0 : cx > ww - VIEW_W ? ww - VIEW_W : cx;
    cy = cy < 0 ? 0 : cy > wh - VIEW_H ? wh - VIEW_H : cy;
}

static void currentCamera(int32_t &camX, int32_t &camY)
{
    int ppx, ppy;
    playerPixel(nowMs(), ppx, ppy);
    camera(ppx, ppy, camX, camY);
}

static void render(uint32_t now, int32_t camX, int32_t camY, int ppx, int ppy, int frame)
{
    const int64_t t0 = esp_timer_get_time();
    const Otr::World &w = *s_world;
    const int ts = s_ts;
    const bool fog = w.fog(), over = w.gameOver();
    const int tx0 = camX / ts, ty0 = camY / ts;
    const int tx1 = (camX + VIEW_W - 1) / ts, ty1 = (camY + VIEW_H - 1) / ts;
    const bool rockSliding = s_rockAnim && moveProgress(now) < 1.f;
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++) {
            const int sx = tx * ts - camX, sy = ty * ts - camY;
            if (over && w.rawObj(tx, ty) == Obj::MINE) {   // Game over: show where the mines were
                blit(terrainSprite(tx, ty, frame), sx, sy, false);
                blit(S_MINE, sx, sy, true);
                continue;
            }
            if (fog && !w.revealed(tx, ty)) {
                blit(S_FOG, sx, sy, false);
                if (w.flagged(tx, ty)) blit(S_FLAG, sx, sy, true);
                continue;
            }
            blit(terrainSprite(tx, ty, frame), sx, sy, false);
            if (fog) {
                const int n = w.mineHint(tx, ty);
                if (n > 0 && w.tile(tx, ty) != Tile::WALL && w.tile(tx, ty) != Tile::TREE) blit(S_NUM1 + n - 1, sx, sy, true);
            }
            if (rockSliding && tx == s_rockToX && ty == s_rockToY) continue;   // Drawn below
            const Obj ob = w.obj(tx, ty);
            const int o = ob == Obj::NPC ? npcSprite(tx, ty, now) : objSprite(ob);
            if (o >= 0) blit(o, sx, sy, o != S_DOOR);
        }
    if (rockSliding) {
        const float t = moveProgress(now);
        const int rx = (int)lroundf((s_rockFromX + (s_rockToX - s_rockFromX) * t) * ts);
        const int ry = (int)lroundf((s_rockFromY + (s_rockToY - s_rockFromY) * t) * ts);
        blit(S_ROCK, rx - camX, ry - camY, true);
    }
    if (w.sailing()) blit(S_BOAT, ppx - camX, ppy - camY + ts / 8, true);
    blit(S_PL_UP + (int)w.facing(), ppx - camX, ppy - camY - (w.sailing() ? ts / 8 : 0), true);
    if (s_fxX >= 0 && now - s_fxT0 < FX_MS) blit(S_IMPACT, s_fxX * ts - camX, s_fxY * ts - camY, true);
    s_renderUs = (uint32_t)(esp_timer_get_time() - t0);
}

// ----------------------------------------------------------------------------------------
// HUD
// ----------------------------------------------------------------------------------------
static void setCount(HudCount c, int value, uint32_t onColor)
{
    lv_label_set_text_fmt(s_count[c], "%d", value);
    lv_obj_set_style_text_color(s_count[c], Theme::c(value > 0 ? onColor : Theme::MUTED), 0);
}

static void updateStats()
{
    const Otr::World &w = *s_world;
    // Left column: symbol + count. Assist mode has unlimited lives: show lives used as "-N".
    if (w.livesLeft() == Otr::LIVES_INFINITE) {
        lv_label_set_text_fmt(s_count[C_LIVES], "\xE2\x80\x93%d", w.livesUsed());   // en dash
        lv_obj_set_style_text_color(s_count[C_LIVES], Theme::c(Theme::MUTED), 0);
    } else {
        setCount(C_LIVES, w.livesLeft(), Theme::TEXT);
    }
    setCount(C_SUIT, w.suitCharges(), Theme::ACCENT);
    setCount(C_KEYS, w.keys(), Theme::GOLD);
    lv_label_set_text_fmt(s_count[C_GEMS], "%d/%d", w.gems(), w.gemsTotal());
    lv_obj_set_style_text_color(s_count[C_GEMS], Theme::c(w.gems() > 0 ? Theme::TEAL : Theme::MUTED), 0);
    // Right column: carried items, shown only while owned (flex wrap fills up to two rows)
    lv_obj_set_hidden(s_item[H_BOAT], !w.hasBoat());
    // Energy bar: red fill on a light track (Kent's mock-up); the numbers sit in the bar
    const int e = w.energy(), m = w.maxEnergy();
    lv_bar_set_range(s_bar, 0, m);
    lv_bar_set_value(s_bar, e, LV_ANIM_OFF);
    lv_obj_set_style_border_color(s_bar, Theme::c(e * 4 <= m ? Theme::RED : Theme::LINE), LV_PART_MAIN);
    lv_label_set_text_fmt(s_barLbl, "%d/%d", e, m);
}

static void toast(const char *text, uint32_t color = Theme::TEXT, uint32_t ms = TOAST_MS)
{
    lv_label_set_text(s_toast, text);
    lv_obj_set_style_text_color(s_toast, Theme::c(color), 0);
    lv_obj_set_hidden(s_toast, false);
    s_toastUntil = nowMs() + ms;
}

static void closeDialog()
{
    if (s_dialog) lv_msgbox_close(s_dialog);
    s_dialog = nullptr;
}

// ----------------------------------------------------------------------------------------
// Save / load: World::save() image (versioned, CRC32) per player and world, file name
// adv_<player id>_<world id>.sav. Primary place is the microSD card
// (/sdcard/otr/adventure/saves/), fallback the internal Flash-FAT (/ffat/). Reading looks on
// the card first. A successful card save removes the Flash-FAT copy (the save "moves" to the
// card), so an old internal copy can never shadow newer progress.
// ----------------------------------------------------------------------------------------
static constexpr const char *SD_ADV_DIR = "/sdcard/otr/adventure";
static constexpr const char *SD_SAVE_DIR = "/sdcard/otr/adventure/saves";
static constexpr const char *SD_MAP_DIR = "/sdcard/otr/adventure/maps";   // Reserved (future maps)

// ext "sav": the world; "sts": the story state of the same game (v0.11.0, worlds with people).
static void savePath(char *out, size_t cap, uint16_t pid, int worldIdx, bool sd, const char *ext = "sav")
{
    char rel[32];
    snprintf(rel, sizeof(rel), "/adv_%u_%u.%s", (unsigned)pid, (unsigned)Otr::WORLDS[worldIdx].id, ext);
    if (sd) snprintf(out, cap, "%s%s", SD_SAVE_DIR, rel);
    else MyFat::path(out, cap, rel);
}

static bool fileExists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && !S_ISDIR(st.st_mode);
}

// Where the save of (pid, world) is: 1 = SD, 2 = Flash-FAT, 0 = none.
static int saveWhere(uint16_t pid, int worldIdx)
{
    if (!pid) return 0;
    char p[96];
    {
        BoardSd::Guard g;
        if (BoardSd::mounted()) {
            savePath(p, sizeof(p), pid, worldIdx, true);
            if (fileExists(p)) return 1;
        }
    }
    if (!MyFat::mounted()) return 0;
    savePath(p, sizeof(p), pid, worldIdx, false);
    return MyFat::exists(p) ? 2 : 0;
}

static bool hasSave(uint16_t pid, int worldIdx) { return saveWhere(pid, worldIdx) != 0; }

static size_t readFile(const char *p, bool sd)
{
    if (sd) BoardSd::lock();
    FILE *f = fopen(p, "rb");
    size_t n = 0;
    if (f) {
        n = fread(s_saveBuf, 1, SAVE_CAP, f);
        fclose(f);
    }
    if (sd) BoardSd::unlock();
    return n;
}

// Restores the current player's save into the (freshly loaded) world. False = world unchanged.
// A broken card copy falls back to the Flash-FAT copy if there is one.
static bool loadSave()
{
    const int where = saveWhere(s_playerId, s_worldIdx);
    if (!where) return false;
    char p[96];
    for (int place = where; place <= 2; place++) {
        const bool sd = place == 1;
        savePath(p, sizeof(p), s_playerId, s_worldIdx, sd);
        if (!sd && !MyFat::exists(p)) break;
        const size_t n = readFile(p, sd);
        if (n && s_world->restore(s_saveBuf, n)) {
            ESP_LOGI(TAG, "loaded %s", p);
            s_storyState.clear();
            if (s_story) {   // The story state lives next to the world save
                savePath(p, sizeof(p), s_playerId, s_worldIdx, sd, "sts");
                const size_t m = readFile(p, sd);
                if (!m || !s_storyState.restore(s_saveBuf, m)) {
                    ESP_LOGW(TAG, "%s: no valid story state (%u B), starting fresh", p, (unsigned)m);
                    s_storyState.clear();
                }
            }
            return true;
        }
        ESP_LOGW(TAG, "%s: invalid save (%u B)", p, (unsigned)n);
    }
    return false;
}

// tmp + rename on the card (FAT rename() does not replace: the old file is removed first; a
// crash in between leaves the .tmp, which is only a lost save point, never a corrupt one).
static bool writeSdAtomic(const char *p, const void *data, size_t n)
{
    BoardSd::Guard g;
    if (!BoardSd::mounted()) return false;
    BoardSd::ensureDir(SD_SAVE_DIR);
    BoardSd::ensureDir(SD_MAP_DIR);
    char tmp[104];
    snprintf(tmp, sizeof(tmp), "%s.tmp", p);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, n, f) == n;
    ok = (fflush(f) == 0) && ok;
    ok = (fsync(fileno(f)) == 0) && ok;
    ok = (fclose(f) == 0) && ok;
    if (ok) {
        unlink(p);
        ok = rename(tmp, p) == 0;
    }
    if (!ok) unlink(tmp);
    return ok;
}

static void markSaved()
{
    s_savedRev = s_world->revision();
    s_savedSteps = s_world->steps();
    s_lastSaveMs = nowMs();
    s_saveWanted = false;
}

// Guests and finished (game over) states are never written.
static bool saveNow()
{
    s_saveWanted = false;
    if (!s_playerId || s_world->gameOver()) return false;
    const size_t n = s_world->save(s_saveBuf, SAVE_CAP);
    if (!n) return false;
    char p[96];
    const int64_t t0 = esp_timer_get_time();
    savePath(p, sizeof(p), s_playerId, s_worldIdx, true);
    bool ok = writeSdAtomic(p, s_saveBuf, n);
    const bool sd = ok;
    if (ok) {
        char old[96];
        savePath(old, sizeof(old), s_playerId, s_worldIdx, false);
        if (MyFat::mounted() && MyFat::exists(old)) unlink(old);
    } else if (MyFat::mounted()) {
        savePath(p, sizeof(p), s_playerId, s_worldIdx, false);
        ok = MyFat::writeAtomic(p, nullptr, 0, s_saveBuf, n);
    }
    if (ok && s_story) {   // Story state in the same place as the world
        uint8_t st[Otr::Story::StoryState::SAVE_BYTES];
        const size_t sn = s_storyState.save(st, sizeof(st));
        char sp[96];
        savePath(sp, sizeof(sp), s_playerId, s_worldIdx, sd, "sts");
        ok = sd ? writeSdAtomic(sp, st, sn) : MyFat::writeAtomic(sp, nullptr, 0, st, sn);
        if (ok && sd) {
            savePath(sp, sizeof(sp), s_playerId, s_worldIdx, false, "sts");
            if (MyFat::mounted() && MyFat::exists(sp)) unlink(sp);
        }
    }
    ESP_LOGI(TAG, "save %s: %s (%u B, %d ms)", p, ok ? "ok" : "FAILED", (unsigned)n, (int)((esp_timer_get_time() - t0) / 1000));
    if (ok) markSaved();
    return ok;
}

// ----------------------------------------------------------------------------------------
// Actions
// ----------------------------------------------------------------------------------------
static bool busy(uint32_t now) { return s_moveMs && now - s_moveT0 < s_moveMs; }
static Otr::WalkMode walkMode() { return Config::get().advMode == 1 ? Otr::WalkMode::PRO : Otr::WalkMode::ASSIST; }

static void stopPath()
{
    s_pathPos = -1;
    s_walkTalkNum = 0;
}
static void showGameOver();

static void snapPlayer()
{
    s_fromX = s_world->playerX();
    s_fromY = s_world->playerY();
    s_moveMs = 0;
    s_rockAnim = false;
}

// Feedback for the item / treasure / checkpoint events of one move or hit.
static void eventToasts(uint32_t ev)
{
    const Otr::World &w = *s_world;
    char buf[80];
    if (ev & Otr::EV_ALL_GEMS) {
        toast(tr(Str::ADV_ALL_GEMS), Theme::GOLD);
        Rgb::effectFlash(255, 210, 60, 900);
    } else if (ev & Otr::EV_GEM) {
        snprintf(buf, sizeof(buf), tr(Str::ADV_GEM), w.gems(), w.gemsTotal());
        toast(buf, Theme::TEAL);
        Rgb::effectFlash(40, 200, 255, 250);
    } else if (ev & Otr::EV_SUIT) {
        snprintf(buf, sizeof(buf), tr(Str::ADV_SUIT), w.suitCharges());
        toast(buf, Theme::ACCENT);
        Rgb::effectFlash(255, 150, 40, 400);
    } else if (ev & Otr::EV_HEART) {
        snprintf(buf, sizeof(buf), tr(Str::ADV_HEART), w.maxEnergy());
        toast(buf, Theme::RED);
        Rgb::effectFlash(255, 40, 80, 400);
    } else if (ev & Otr::EV_LIFE) {
        toast(tr(Str::ADV_LIFE), Theme::GREEN);
        Rgb::effectFlash(60, 255, 100, 400);
    } else if (ev & Otr::EV_POTION) {
        snprintf(buf, sizeof(buf), tr(Str::ADV_POTION), w.energy(), w.maxEnergy());
        toast(buf, Theme::GREEN);
        Rgb::effectFlash(60, 255, 140, 250);
    } else if (ev & Otr::EV_KEY) {
        toast(tr(Str::ADV_KEY), Theme::GOLD);
    } else if (ev & Otr::EV_BOAT) {
        toast(tr(Str::ADV_BOAT), Theme::GOLD);
    } else if (ev & Otr::EV_EXIT) {
        toast(tr(Str::ADV_EXIT), Theme::GOLD, 3500);
        Rgb::effectFlash(255, 210, 60, 1200);
    } else if (ev & Otr::EV_CHECKPOINT) {
        toast(tr(Str::ADV_CHECKPOINT), Theme::GREEN);
    } else if (ev & Otr::EV_TREASURE) {
        toast(tr(Str::ADV_TREASURE), Theme::GOLD);
        Rgb::effectFlash(255, 230, 120, 200);
    } else if (ev & Otr::EV_DOOR) {
        toast(tr(Str::ADV_DOOR), Theme::GREEN);
    } else if (ev & Otr::EV_NEED_KEY) {
        toast(tr(Str::ADV_NEED_KEY), Theme::ACCENT);
    } else if (ev & Otr::EV_GATE_OPEN) {
        toast(tr(Str::ADV_GATE_OPEN), Theme::GREEN);
        Rgb::effectFlash(60, 255, 120, 300);
    } else if (ev & Otr::EV_GATE_CLOSE) {
        toast(tr(Str::ADV_GATE_CLOSE), Theme::ACCENT);
    } else if (ev & Otr::EV_SPLASH) {
        toast(tr(Str::ADV_SPLASH), Theme::BLUE);
    } else if (ev & Otr::EV_SIZZLE) {
        toast(tr(Str::ADV_SIZZLE), Theme::ACCENT);
    } else if (ev & Otr::EV_SUIT_USED) {
        snprintf(buf, sizeof(buf), tr(Str::ADV_SUIT_USED), w.suitCharges());
        toast(buf, Theme::ACCENT, 1200);
    }
    if (ev & Otr::EV_CHECKPOINT) s_saveWanted = true;
}

// One sound per move: the most important event wins.
static void playEvents(uint32_t ev)
{
    using Sound::Sfx;
    Sfx s;
    if (ev & Otr::EV_TALK) return;   // The conversation has its own sounds
    if (ev & Otr::EV_GAME_OVER) s = Sfx::GAME_OVER;
    else if (ev & Otr::EV_DIED) s = Sfx::LIFE_LOST;
    else if (ev & Otr::EV_MINE) s = Sfx::MINE;
    else if (ev & Otr::EV_BURNED) s = Sfx::HURT;
    else if (ev & Otr::EV_EXIT) s = Sfx::EXIT;
    else if (ev & Otr::EV_ALL_GEMS) s = Sfx::WIN;
    else if (ev & (Otr::EV_GEM | Otr::EV_TREASURE)) s = Sfx::GEM;
    else if (ev & (Otr::EV_KEY | Otr::EV_BOAT)) s = Sfx::KEY;
    else if (ev & Otr::EV_LIFE) s = Sfx::LIFE;
    else if (ev & Otr::EV_HEART) s = Sfx::HEART;
    else if (ev & (Otr::EV_POTION | Otr::EV_SUIT)) s = Sfx::POTION;
    else if (ev & Otr::EV_CHECKPOINT) s = Sfx::CHECKPOINT;
    else if (ev & (Otr::EV_DOOR | Otr::EV_GATE_OPEN)) s = Sfx::DOOR;
    else if (ev & Otr::EV_NEED_KEY) s = Sfx::LOCKED;
    else if (ev & (Otr::EV_SPLASH | Otr::EV_SIZZLE)) s = Sfx::SPLASH;
    else if (ev & Otr::EV_PUSHED) s = Sfx::PUSH;
    else if (ev & Otr::EV_MOVED) s = Sfx::STEP;
    else if (ev & Otr::EV_BLOCKED) s = Sfx::BUMP;
    else return;
    Audio::play(s);
}


// ----------------------------------------------------------------------------------------
// Dialogue (v0.11.0): box over the lower part of the map (portrait, name, typewriter text,
// reference line) and up to 4 answer buttons (36 px) in place of the control panel.
// ----------------------------------------------------------------------------------------
static constexpr int32_t TALK_W = VIEW_W - 12, TALK_PAD = 10, TALK_TEXT_X = PORTRAIT_PX + 10;
static constexpr int32_t CHOICE_H = 36, CHOICE_GAP = 4;
static constexpr uint32_t TW_CHAR_MS = 22, TW_COMMA_MS = 90, TW_STOP_MS = 180;

static void joyShow(bool down, int dx, int dy);
static bool talking() { return s_talkPanel != nullptr; }
static int langIdx() { return strcmp(Lang::currentLang(), "en") == 0 ? 1 : 0; }

static Dir dirToward(int fx, int fy, int tx, int ty)
{
    const int dx = tx - fx, dy = ty - fy;
    return abs(dx) >= abs(dy) ? (dx < 0 ? Dir::LEFT : Dir::RIGHT) : (dy < 0 ? Dir::UP : Dir::DOWN);
}

// Effects of the script that belong to the world and the speaker.
class StoryHost : public Otr::Story::Host {
public:
    void worldAction(const char *verb, int argc, const int *a) override
    {
        Otr::World &w = *s_world;
        bool ok = true;
        if (!strcmp(verb, "npc_move") && argc == 3) ok = w.moveNpc(a[0], a[1], a[2]);
        else if (!strcmp(verb, "npc_remove") && argc == 1) ok = w.removeNpc(a[0]);
        else if (!strcmp(verb, "open") && argc == 2) ok = w.clearObj(a[0], a[1]);
        else if (!strcmp(verb, "keys") && argc == 1) {
            w.addKeys(a[0]);
            if (a[0] > 0) toast(tr(Str::ADV_KEY), Theme::GOLD);
        } else ok = false;
        if (!ok) ESP_LOGW(TAG, "story: world %s (%d args) failed", verb, argc);
        s_dirty = true;
        updateStats();
    }
    void sound(const char *name) override
    {
        for (int i = 0; i < (int)Sound::Sfx::COUNT; i++)
            if (!strcmp(Sound::sfxName((Sound::Sfx)i), name)) {
                Audio::play((Sound::Sfx)i);
                return;
            }
        ESP_LOGW(TAG, "story: unknown sound '%s'", name);
    }
};
static StoryHost s_storyHost;

// Items / quests the last step changed, as one toast (shown at the top while talking).
static void noteToasts()
{
    using Otr::Story::NoteKind;
    if (!s_story || !s_talk.noteCount()) return;
    const int L = langIdx();
    char buf[200];
    size_t len = 0;
    buf[0] = 0;
    for (int i = 0; i < s_talk.noteCount() && len + 2 < sizeof(buf); i++) {
        const Otr::Story::Note &n = s_talk.note(i);
        const bool item = n.kind == NoteKind::ITEM_GOT || n.kind == NoteKind::ITEM_LOST;
        const char *name = item ? s_story->items[n.id].name.get(L) : s_story->quests[n.id].name.get(L);
        if (len) buf[len++] = '\n';
        const size_t cap = sizeof(buf) - len;
        int w;
        switch (n.kind) {
        case NoteKind::ITEM_GOT:
            w = n.amount > 1 ? snprintf(buf + len, cap, tr(Str::ADV_ITEM_GOT_N), name, (int)n.amount)
                             : snprintf(buf + len, cap, tr(Str::ADV_ITEM_GOT), name);
            break;
        case NoteKind::ITEM_LOST: w = snprintf(buf + len, cap, tr(Str::ADV_ITEM_LOST), name); break;
        case NoteKind::QUEST_STARTED: w = snprintf(buf + len, cap, tr(Str::ADV_QUEST_NEW), name); break;
        default: w = snprintf(buf + len, cap, tr(Str::ADV_QUEST_DONE), name); break;
        }
        if (w < 0) break;
        len += (size_t)w < cap ? (size_t)w : cap - 1;
    }
    toast(buf, Theme::GOLD, 3200);
    Rgb::effectFlash(255, 210, 60, 300);
}

static bool typing() { return s_twText && s_twPos < s_twLen; }

static void talkSkip()
{
    if (!typing()) return;
    s_twPos = s_twLen;   // Shown (and the answers built) in the next tick
}

static void choiceClicked(lv_event_t *e)
{
    if (!Theme::debounce(s_choiceTapMs)) return;
    if (typing()) {
        talkSkip();
        return;
    }
    const int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx >= 0 && !s_talk.enabled(idx)) {   // Locked: the hint is already on the button
        Audio::play(Sound::Sfx::LOCKED);
        return;
    }
    Audio::play(Sound::Sfx::CLICK);
    s_pendingChoice = idx;   // Handled in tick(): never delete the button inside its own event
}

// One answer: text, and for a locked answer the hint below it (greyed out).
static void addChoice(int slot, int idx, const char *text, const char *hint, bool enabled)
{
    lv_obj_t *b = Theme::button(s_talkPanel, "", VIEW_W - 12, CHOICE_H, enabled ? Theme::TEAL : Theme::LINE);
    lv_obj_set_pos(b, 6, 2 + slot * (CHOICE_H + CHOICE_GAP));
    lv_obj_t *l = lv_obj_get_child(b, 0);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, (hint || Otr::Story::utf8Len(text) > 26) ? &my_font_14 : &my_font_18, 0);
    if (hint) {
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 12, 1);
        lv_obj_set_style_text_color(l, Theme::c(Theme::MUTED), 0);
        lv_obj_t *h = Theme::label(b, hint, &my_font_14, Theme::ACCENT);
        lv_obj_align(h, LV_ALIGN_BOTTOM_LEFT, 12, -1);
    } else {
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);
    }
    if (!enabled) lv_obj_set_style_bg_color(b, Theme::c(Theme::PANEL), 0);
    lv_obj_add_event_cb(b, choiceClicked, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
}

static void talkShowChoices()
{
    lv_obj_clean(s_talkPanel);
    const int L = langIdx();
    const int n = s_talk.choiceCount();
    bool any = false;
    for (int i = 0; i < n; i++) {
        const Otr::Story::Choice &c = s_talk.choice(i);
        const bool en = s_talk.enabled(i);
        any = any || en;
        addChoice(i, i, c.text.get(L), en ? nullptr : c.hint.get(L), en);
    }
    if (!n) addChoice(0, -1, tr(Str::ADV_TALK_OK), nullptr, true);         // A line without answers
    else if (!any) addChoice(n, -1, tr(Str::ADV_TALK_BYE), nullptr, true);  // Every answer locked: a way out
}

// Lays out the box for the current node and starts the typewriter.
static void talkShowNode()
{
    const Otr::Story::Node *nd = s_talk.node();
    const Otr::Story::Npc *sp = s_talk.speaker();
    const int L = langIdx();
    const int32_t inner = TALK_W - 2 * TALK_PAD;
    int32_t textX = 0, textY = 0;
    if (sp) {
        lv_canvas_set_buffer(s_talkPortrait, s_spr->portrait[sp->look % NPC_LOOKS], PORTRAIT_PX, PORTRAIT_PX,
                             LV_COLOR_FORMAT_RGB565);
        lv_label_set_text(s_talkName, sp->name.get(L));
        textX = TALK_TEXT_X;
        textY = 20;
    }
    lv_obj_set_hidden(s_talkPortrait, !sp);
    lv_obj_set_hidden(s_talkName, !sp);
    // Narrator: full width, italic feel by colour
    lv_obj_set_style_text_color(s_talkText, Theme::c(sp ? Theme::TEXT : 0xC9D6E8), 0);
    lv_obj_set_pos(s_talkText, textX, textY);
    lv_obj_set_width(s_talkText, inner - textX);
    s_twText = nd->text.get(L);
    s_twLen = (int)strlen(s_twText);
    if (s_twLen > (int)sizeof(s_twBuf) - 1) s_twLen = (int)sizeof(s_twBuf) - 1;
    // Measure with the whole text, then fix the height so the box does not grow while typing
    lv_obj_set_height(s_talkText, LV_SIZE_CONTENT);
    lv_label_set_text(s_talkText, s_twText);
    lv_obj_update_layout(s_talkText);
    const int32_t th = lv_obj_get_height(s_talkText);
    lv_obj_set_height(s_talkText, th);
    int32_t h = textY + th;
    const char *ref = nd->ref ? nd->ref : nd->quote;   // Quote file: later (see the plan)
    if (ref) {
        char buf[80];
        snprintf(buf, sizeof(buf), tr(Str::ADV_TALK_READ), ref);
        lv_label_set_text(s_talkRef, buf);
        lv_obj_set_pos(s_talkRef, textX, h + 4);
        h += 4 + 18;
    }
    lv_obj_set_hidden(s_talkRef, !ref);
    if (sp && h < PORTRAIT_PX) h = PORTRAIT_PX;
    const int32_t boxH = h + 2 * TALK_PAD;
    lv_obj_set_size(s_talkBox, TALK_W, boxH);
    lv_obj_set_pos(s_talkBox, 6, VIEW_Y + VIEW_H - 6 - boxH);
    lv_label_set_text(s_talkText, "");
    s_twPos = 0;
    s_twChars = 0;
    s_twNext = nowMs();
    lv_obj_clean(s_talkPanel);   // The answers come when the text is out
}

static void talkBuild()
{
    s_talkBox = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_talkBox);
    lv_obj_set_style_bg_color(s_talkBox, Theme::c(Theme::PANEL), 0);
    lv_obj_set_style_bg_opa(s_talkBox, 242, 0);
    lv_obj_set_style_border_color(s_talkBox, Theme::c(Theme::GOLD), 0);
    lv_obj_set_style_border_width(s_talkBox, 2, 0);
    lv_obj_set_style_radius(s_talkBox, 12, 0);
    lv_obj_set_style_pad_all(s_talkBox, TALK_PAD, 0);
    lv_obj_set_style_shadow_width(s_talkBox, 16, 0);
    lv_obj_set_style_shadow_opa(s_talkBox, LV_OPA_50, 0);
    lv_obj_set_scrollable(s_talkBox, false);
    lv_obj_set_clickable(s_talkBox, true);
    lv_obj_add_event_cb(s_talkBox, [](lv_event_t *) { talkSkip(); }, LV_EVENT_PRESSED, nullptr);
    s_talkPortrait = lv_canvas_create(s_talkBox);
    lv_obj_set_pos(s_talkPortrait, 0, 0);
    lv_obj_set_clickable(s_talkPortrait, false);
    s_talkName = Theme::label(s_talkBox, "", &my_font_14, Theme::GOLD);
    lv_obj_set_pos(s_talkName, TALK_TEXT_X, 0);
    s_talkText = Theme::label(s_talkBox, "", &my_font_14, Theme::TEXT);
    lv_label_set_long_mode(s_talkText, LV_LABEL_LONG_MODE_WRAP);
    s_talkRef = Theme::label(s_talkBox, "", &my_font_14, Theme::TEAL);

    s_talkPanel = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_talkPanel);
    lv_obj_set_size(s_talkPanel, 320, CTRL_H);
    lv_obj_set_pos(s_talkPanel, 0, CTRL_Y);
    lv_obj_set_style_bg_color(s_talkPanel, Theme::c(Theme::PANEL), 0);
    lv_obj_set_style_bg_opa(s_talkPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(s_talkPanel, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(s_talkPanel, 1, 0);
    lv_obj_set_style_border_color(s_talkPanel, Theme::c(Theme::GOLD), 0);
    lv_obj_set_scrollable(s_talkPanel, false);
    lv_obj_set_clickable(s_talkPanel, true);
    lv_obj_add_event_cb(s_talkPanel, [](lv_event_t *) { talkSkip(); }, LV_EVENT_PRESSED, nullptr);

    lv_obj_move_foreground(s_toast);
    lv_obj_align(s_toast, LV_ALIGN_TOP_MID, 0, VIEW_Y + 8);   // Above the box while talking
}

static void endTalk()
{
    s_talk.end();
    s_talkNum = 0;
    s_twText = nullptr;
    s_pendingChoice = -2;
    if (s_talkBox) lv_obj_delete(s_talkBox);
    if (s_talkPanel) lv_obj_delete(s_talkPanel);
    s_talkBox = s_talkPanel = s_talkPortrait = s_talkName = s_talkText = s_talkRef = nullptr;
    lv_obj_align(s_toast, LV_ALIGN_TOP_MID, 0, VIEW_Y + VIEW_H - 64);
    s_saveWanted = s_playerId != 0;   // Conversations change the game: keep it
    s_dirty = true;
    updateStats();
}

static void startTalk(int num)
{
    stopPath();
    s_joy.cancel();
    joyShow(false, 0, 0);
    const Otr::Story::Npc *n = s_story ? s_story->npcByNum(num) : nullptr;
    if (!n || !s_talk.start(*s_story, s_storyState, n->id, &s_storyHost)) {
        toast(tr(Str::ADV_TALK_NONE), Theme::MUTED, 1500);
        return;
    }
    s_talkNum = num;
    talkBuild();
    talkShowNode();
    noteToasts();
    s_dirty = true;
}

// Typewriter + picked answers (from tick).
static void talkTick(uint32_t now)
{
    if (!talking()) return;
    if (s_pendingChoice != -2) {
        const int c = s_pendingChoice;
        s_pendingChoice = -2;
        if (c >= 0 && s_talk.choose(c) && s_talk.active()) {
            talkShowNode();
            noteToasts();
        } else {
            if (c >= 0) noteToasts();
            endTalk();
            return;
        }
    }
    if (!s_twText) return;
    if (s_twPos < s_twLen) {
        bool blip = false;
        while (s_twPos < s_twLen && (int32_t)(now - s_twNext) >= 0) {
            int n = 1;
            while (s_twPos + n < s_twLen && ((uint8_t)s_twText[s_twPos + n] & 0xC0) == 0x80) n++;
            const char ch = s_twText[s_twPos];
            s_twPos += n;
            s_twNext += ch == '.' || ch == '!' || ch == '?' ? TW_STOP_MS : ch == ',' ? TW_COMMA_MS : TW_CHAR_MS;
            if (ch != ' ' && s_twChars++ % 3 == 0) blip = true;
        }
        if (blip) Audio::play(Sound::Sfx::TALK);
        memcpy(s_twBuf, s_twText, (size_t)s_twPos);
        s_twBuf[s_twPos] = 0;
        lv_label_set_text(s_talkText, s_twBuf);
        if (s_twPos >= s_twLen) talkShowChoices();
    } else if (!lv_obj_get_child_count(s_talkPanel)) {   // Skipped: show it all
        lv_label_set_text(s_talkText, s_twText);
        talkShowChoices();
    }
}


// One step. Returns the world events (0 if the previous step is still animating).
static uint32_t doMove(Dir d)
{
    const uint32_t now = nowMs();
    if (busy(now) || s_world->gameOver()) return 0;
    const int fx = s_world->playerX(), fy = s_world->playerY();
    const int tx = fx + Otr::dirX(d), ty = fy + Otr::dirY(d);
    const bool wasRock = s_world->obj(tx, ty) == Obj::ROCK;
    const Tile targetTile = s_world->tile(tx, ty);
    const uint32_t ev = s_world->move(d);
    s_dirty = true;
    playEvents(ev);
    char buf[80];

    if (ev & Otr::EV_DIED) {   // Respawned (or game over): no walk animation
        snapPlayer();
        if (ev & Otr::EV_MINE) {
            s_fxX = tx;
            s_fxY = ty;
            s_fxT0 = now;
        }
        if (s_world->livesLeft() == Otr::LIVES_INFINITE || (ev & Otr::EV_GAME_OVER)) {
            toast(tr(Str::ADV_DIED), Theme::RED, 3000);
        } else {
            snprintf(buf, sizeof(buf), tr(Str::ADV_DIED_LEFT), s_world->livesLeft());
            toast(buf, Theme::RED, 3000);
        }
        Rgb::effectFlash(255, 30, 20, 900);
        updateStats();
        stopPath();
        if (ev & Otr::EV_GAME_OVER) showGameOver();
        return ev;
    }
    if (ev & Otr::EV_MOVED) {
        s_fromX = fx;
        s_fromY = fy;
        s_moveT0 = now;
        s_moveMs = s_ts == 32 ? WALK_MS_32 : WALK_MS_16;
        // Joystick: fit the walk animation to the step interval so the hero glides
        // continuously at the chosen pace instead of walk-pause-walk. Shorter intervals
        // shorten it (never below WALK_MS_MIN), longer ones stretch it (up to WALK_MS_MAX).
        if (const uint32_t iv = s_joy.intervalMs()) {
            const uint32_t fit = iv > TICK_MS ? iv - TICK_MS / 2 : iv;
            s_moveMs = fit < WALK_MS_MIN ? WALK_MS_MIN : fit > WALK_MS_MAX ? WALK_MS_MAX : fit;
        }
        s_rockAnim = wasRock && (ev & Otr::EV_PUSHED) && !(ev & (Otr::EV_SPLASH | Otr::EV_SIZZLE));
        if (s_rockAnim) {
            s_rockFromX = tx;
            s_rockFromY = ty;
            s_rockToX = tx + Otr::dirX(d);
            s_rockToY = ty + Otr::dirY(d);
            s_moveMs += 40;   // Pushing is a little slower
        }
    } else if (!(ev & Otr::EV_TALK)) {
        s_bumpDx = Otr::dirX(d);
        s_bumpDy = Otr::dirY(d);
        s_bumpT0 = now;
    }

    if (ev & Otr::EV_MINE) {
        snprintf(buf, sizeof(buf), tr(Str::ADV_MINE), s_world->rules().mineDamage);
        toast(buf, Theme::RED);
        Rgb::effectFlash(255, 120, 20, 600);
        s_fxX = tx;
        s_fxY = ty;
        s_fxT0 = now;
    } else if (ev & Otr::EV_BURNED) {
        snprintf(buf, sizeof(buf), tr(Str::ADV_BURNED), s_world->rules().lavaDamage);
        toast(buf, Theme::RED, 1400);
        Rgb::effectFlash(255, 60, 20, 400);
    } else if ((ev & Otr::EV_BLOCKED) && targetTile == Tile::WATER && !s_world->hasBoat()) {
        toast(tr(Str::ADV_NEED_BOAT), Theme::BLUE);
    } else {
        eventToasts(ev);
    }
    if (ev & (Otr::EV_MINE | Otr::EV_BURNED)) eventToasts(ev & (Otr::EV_CHECKPOINT));
    if (ev & ~(Otr::EV_MOVED | Otr::EV_BLOCKED | Otr::EV_TALK)) updateStats();
    if (ev & Otr::EV_TALK) startTalk(s_world->talkNpc());
    return ev;
}

static void doHit()
{
    const uint32_t now = nowMs();
    if (busy(now) || s_world->gameOver()) return;
    const int tx = s_world->playerX() + Otr::dirX(s_world->facing());
    const int ty = s_world->playerY() + Otr::dirY(s_world->facing());
    const uint32_t ev = s_world->hit();
    if (ev & Otr::EV_TALK) {   // Facing a person: Slå is Prata
        startTalk(s_world->talkNpc());
        return;
    }
    s_bumpDx = Otr::dirX(s_world->facing());
    s_bumpDy = Otr::dirY(s_world->facing());
    s_bumpT0 = now;
    Audio::play(ev & Otr::EV_BROKE ? Sound::Sfx::BREAK : ev & Otr::EV_HIT ? Sound::Sfx::HIT : Sound::Sfx::BUMP);
    if (ev & Otr::EV_HIT) {
        s_fxX = tx;
        s_fxY = ty;
        s_fxT0 = now;
        Rgb::effectFlash(255, 255, 255, ev & Otr::EV_BROKE ? 200 : 80);
        if (!(ev & Otr::EV_BROKE) && s_world->tile(tx, ty) == Tile::CRACKED) toast(tr(Str::ADV_CRACK), Theme::MUTED);
        if (ev & Otr::EV_TREASURE) eventToasts(Otr::EV_TREASURE);
    }
    s_dirty = true;
}

// Tap-to-walk: plan with A* and start walking. Messages for truncated paths come on arrival.
static void walkTo(int tx, int ty)
{
    stopPath();
    s_joy.cancel();
    const Otr::PlanResult r = s_planner->plan(*s_world, tx, ty, walkMode(), *s_path);
    switch (r) {
    case Otr::PlanResult::HERE: return;
    case Otr::PlanResult::INVALID: toast(tr(Str::ADV_CANT_GO), Theme::MUTED, 1500); return;
    case Otr::PlanResult::UNREACHABLE: toast(tr(Str::ADV_NO_WAY), Theme::MUTED, 1500); return;
    case Otr::PlanResult::SUIT: {
        char buf[80];
        snprintf(buf, sizeof(buf), tr(Str::ADV_SUIT_OK), s_world->suitCharges());
        toast(buf, Theme::ACCENT);
        break;
    }
    default: break;
    }
    s_pathPos = 0;
}

static void finishPath()
{
    const Otr::PlanResult r = s_path->result;
    const int gx = s_path->goalX, gy = s_path->goalY;
    const int talkNum = s_walkTalkNum;
    stopPath();
    int nx, ny;
    if (talkNum && (r == Otr::PlanResult::OK || r == Otr::PlanResult::SUIT) && s_world->findNpc(talkNum, nx, ny) &&
        abs(nx - s_world->playerX()) + abs(ny - s_world->playerY()) == 1) {
        doMove(dirToward(s_world->playerX(), s_world->playerY(), nx, ny));   // Face it and talk
        s_replanned = false;
        return;
    }
    switch (r) {
    case Otr::PlanResult::DANGER: toast(tr(Str::ADV_DANGER), Theme::RED); break;
    case Otr::PlanResult::UNKNOWN: toast(tr(Str::ADV_UNKNOWN), Theme::GOLD); break;
    case Otr::PlanResult::NEED_BOAT:
        if (s_world->hasBoat() && !s_replanned) {   // Picked up the boat on the way: go on
            s_replanned = true;
            walkTo(gx, gy);
            return;
        }
        toast(tr(Str::ADV_NEED_BOAT_GO), Theme::BLUE);
        break;
    default: break;
    }
    s_replanned = false;
}

// Tap on a person: walk to the nearest free side of it, then talk (finishPath).
static void walkToTalk(int num, int nx, int ny)
{
    stopPath();
    s_joy.cancel();
    const int px = s_world->playerX(), py = s_world->playerY();
    if (abs(nx - px) + abs(ny - py) == 1) {
        doMove(dirToward(px, py, nx, ny));   // Next to it: turn (bump) and talk
        return;
    }
    int best = -1, bestLen = 1 << 30;
    for (int d = 0; d < 4; d++) {
        const int x = nx + Otr::dirX((Dir)d), y = ny + Otr::dirY((Dir)d);
        if (!Otr::Planner::targetable(*s_world, x, y)) continue;
        const Otr::PlanResult r = s_planner->plan(*s_world, x, y, walkMode(), *s_path);
        if ((r == Otr::PlanResult::OK || r == Otr::PlanResult::SUIT) && s_path->len < bestLen) {
            best = d;
            bestLen = s_path->len;
        }
    }
    if (best < 0) {
        toast(tr(Str::ADV_NO_WAY), Theme::MUTED, 1500);
        return;
    }
    s_replanned = false;
    walkTo(nx + Otr::dirX((Dir)best), ny + Otr::dirY((Dir)best));
    if (s_pathPos >= 0) s_walkTalkNum = num;
}

static void resetWorld(bool showHelp = true)
{
    if (!s_world->load(Otr::WORLDS[s_worldIdx])) ESP_LOGE(TAG, "world %d failed to load", s_worldIdx);
    AdvLive::invalidateMap();
    if (talking()) endTalk();
    s_storyState.clear();
    snapPlayer();
    stopPath();
    s_joy.cancel();
    s_fxX = -1;
    s_bumpT0 = 0;
    s_dirty = true;
    updateStats();
    if (showHelp) toast(tr(s_world->fog() ? Str::ADV_HELP_MINES : Str::ADV_HELP), Theme::MUTED, 6000);
}

static lv_obj_t *dialog(const char *text)
{
    closeDialog();
    s_dialog = lv_msgbox_create(nullptr);
    if (text) lv_msgbox_add_text(s_dialog, text);
    return s_dialog;
}

static void showGameOver()
{
    lv_obj_t *mb = dialog(tr(Str::ADV_GAME_OVER_Q));
    lv_obj_t *cont = lv_msgbox_add_footer_button(mb, tr(Str::ADV_CONTINUE));
    lv_obj_t *restart = lv_msgbox_add_footer_button(mb, tr(Str::ADV_RESTART));
    lv_obj_add_event_cb(cont, [](lv_event_t *) {
        closeDialog();
        // Continue from the last save if there is one (it never holds a game over), else one
        // more life at the checkpoint.
        Otr::World &w = *s_world;
        if (!(s_playerId && loadSave() && !w.gameOver())) {
            if (w.gameOver()) w.revive();
        }
        AdvLive::invalidateMap();
        snapPlayer();
        s_dirty = true;
        updateStats();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(restart, [](lv_event_t *) {
        closeDialog();
        resetWorld();
        saveNow();
    }, LV_EVENT_CLICKED, nullptr);
    Theme::msgboxFonts(mb);
}

// ----------------------------------------------------------------------------------------
// v0.11.1: web live view (/adventure). Cheap change detection every tick; AdvLive::publish()
// only when something the page shows changed (the map itself is copied only on a new revision).
// ----------------------------------------------------------------------------------------
struct LiveSig {
    int px, py, facing, energy, maxEnergy, livesLeft, livesUsed, keys, gems, suit, cpX, cpY, steps;
    uint32_t rev, items;
    bool boat, gates, over, exit;
    const void *node;
    int talkNum, choices, lang;
};
static LiveSig s_liveSig;
static bool s_liveValid;

static const char *worldName()
{
    static const Str NAMES[Otr::WORLD_COUNT] = {Str::ADV_WORLD_TEST, Str::ADV_WORLD_MINES, Str::ADV_WORLD_VILLAGE};
    return s_worldIdx >= 0 && s_worldIdx < Otr::WORLD_COUNT ? tr(NAMES[s_worldIdx]) : "";
}

static void liveSync(bool force = false)
{
    if (!s_world) return;
    const Otr::World &w = *s_world;
    LiveSig g;
    memset(&g, 0, sizeof(g));   // Padding included: compared with memcmp
    g.px = w.playerX();
    g.py = w.playerY();
    g.facing = (int)w.facing();
    g.energy = w.energy();
    g.maxEnergy = w.maxEnergy();
    g.livesLeft = w.livesLeft();
    g.livesUsed = w.livesUsed();
    g.keys = w.keys();
    g.gems = w.gems();
    g.suit = w.suitCharges();
    g.cpX = w.checkpointX();
    g.cpY = w.checkpointY();
    g.steps = w.steps();
    g.rev = w.revision();
    for (int i = 0; i < Otr::Story::MAX_ITEMS; i++) g.items = g.items * 31u + s_storyState.items[i];
    g.boat = w.hasBoat();
    g.gates = w.gatesOpen();
    g.over = w.gameOver();
    g.exit = w.exitReached();
    g.node = s_talk.active() ? (const void *)s_talk.node() : nullptr;
    g.talkNum = s_talkNum;
    g.choices = s_talk.active() ? s_talk.choiceCount() : 0;
    g.lang = langIdx();
    if (!force && s_liveValid && memcmp(&g, &s_liveSig, sizeof(g)) == 0) return;
    s_liveSig = g;
    s_liveValid = true;
    const AdvLive::Source src = {&w, worldName(), s_story, &s_storyState, s_talk.active() ? &s_talk : nullptr,
                                 langIdx(), tr(Str::ADV_TALK_READ), tr(Str::ADV_TALK_OK), tr(Str::ADV_TALK_BYE),
                                 s_talkNum};
    AdvLive::publish(src);
}

static void liveStop()
{
    AdvLive::setInactive();
    s_liveValid = false;
}

// ----------------------------------------------------------------------------------------
// Frame tick
// ----------------------------------------------------------------------------------------
static void tick(lv_timer_t *)
{
    const uint32_t now = nowMs();
    talkTick(now);
    if (!s_dialog && !talking()) {
        Dir jd;
        if (s_joy.poll(now, !busy(now) && !s_world->gameOver(), jd)) {
            stopPath();
            doMove(jd);
        } else if (s_joy.held()) {
            // Finger on the joystick (stop zone or waiting for the next step): no path walking
        } else if (s_pathPos >= 0 && !busy(now)) {
            if (s_pathPos >= s_path->len) {
                finishPath();
            } else {
                const uint32_t ev = doMove((Dir)s_path->dirs[s_pathPos++]);
                if (!(ev & Otr::EV_MOVED) || (ev & (Otr::EV_DIED | Otr::EV_MINE))) stopPath();
            }
        }
    }

    int ppx, ppy;
    playerPixel(now, ppx, ppy);
    int32_t camX, camY;
    camera(ppx, ppy, camX, camY);
    const uint32_t frame = (now / ANIM_FRAME_MS) & 1;
    const bool anim = busy(now) || (s_bumpT0 && now - s_bumpT0 < BUMP_MS + TICK_MS) ||
                      (s_fxX >= 0 && now - s_fxT0 < FX_MS + TICK_MS);
    if (anim || s_dirty || camX != s_lastCamX || camY != s_lastCamY || frame != s_lastFrame ||
        s_world->revision() != s_lastRev) {
        render(now, camX, camY, ppx, ppy, (int)frame);
        lv_obj_invalidate(s_canvas);
        s_dirty = false;
        s_lastCamX = camX;
        s_lastCamY = camY;
        s_lastFrame = frame;
        s_lastRev = s_world->revision();
        s_renders++;
        s_renderAccUs += s_renderUs;
    }
    if (s_moveMs && !busy(now)) {   // Walk finished
        s_moveMs = 0;
        s_rockAnim = false;
        s_fromX = s_world->playerX();
        s_fromY = s_world->playerY();
    }

    // Autosave when idle: after a checkpoint, and every minute if anything changed
    const bool idle = !busy(now) && s_pathPos < 0 && !s_joy.held() && !s_dialog && !talking();
    // Slå becomes Prata when the hero faces a person
    const bool faceTalk = s_world->facingNpc() != 0;
    if (s_hitBtn && faceTalk != s_hitTalk) {
        s_hitTalk = faceTalk;
        lv_label_set_text(lv_obj_get_child(s_hitBtn, 0), tr(faceTalk ? Str::ADV_TALK : Str::ADV_HIT));
        lv_obj_set_style_bg_color(s_hitBtn, Theme::c(faceTalk ? Theme::TEAL : Theme::ACCENT), 0);
        lv_obj_set_style_border_color(s_hitBtn, Theme::c(faceTalk ? Theme::TEAL : Theme::ACCENT), 0);
    }
    if (idle && s_playerId) {
        const bool changed = s_world->revision() != s_savedRev || s_world->steps() != s_savedSteps;
        if (s_saveWanted || (changed && now - s_lastSaveMs >= 60000)) saveNow();
    }

    if (s_toastUntil && now > s_toastUntil) {
        s_toastUntil = 0;
        lv_obj_set_hidden(s_toast, true);
    }
    if (now - s_statT0 >= 1000) {   // Our frames, our blit time, LVGL's flushed frames (shown in the menu)
        const MyLvgl::Stats st = MyLvgl::stats();
        snprintf(s_perfText, sizeof(s_perfText), "%u/s  %u.%u ms  lcd %d fps", (unsigned)s_renders,
                 (unsigned)(s_renders ? s_renderAccUs / s_renders / 1000 : 0),
                 (unsigned)(s_renders ? s_renderAccUs / s_renders / 100 % 10 : 0), (int)(st.fps + 0.5f));
        s_renders = s_renderAccUs = 0;
        s_statT0 = now;
    }
    liveSync();
}

// ----------------------------------------------------------------------------------------
// Input
// ----------------------------------------------------------------------------------------
// Joystick: knob follows the finger (clamped to the pad), the active direction arrow lights up.
static void joyShow(bool down, int dx, int dy)
{
    if (!s_joyKnob) return;
    int lit = -1;
    if (down) {
        const float d = sqrtf((float)(dx * dx + dy * dy));
        const float lim = (float)(JOY_R - JOY_KNOB_R / 2);
        if (d > lim) {
            dx = (int)((float)dx * lim / d);
            dy = (int)((float)dy * lim / d);
        }
        lv_obj_set_pos(s_joyKnob, JOY_CX + dx - JOY_KNOB_R, JOY_CY + dy - JOY_KNOB_R);
        const Otr::JoySample js = s_joy.sample();
        if (js.moving) lit = (int)js.dir;
    }
    lv_obj_set_hidden(s_joyKnob, !down);
    if (lit != s_joyLit) {
        for (int i = 0; i < 4; i++)
            lv_obj_set_style_text_color(s_joyArrow[i], Theme::c(i == lit ? Theme::ACCENT : Theme::MUTED), 0);
        s_joyLit = lit;
    }
}

static void joyEvent(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED &&
        code != LV_EVENT_PRESS_LOST)
        return;
    lv_obj_t *area = (lv_obj_t *)lv_event_get_current_target(e);
    lv_point_t pt;
    lv_indev_get_point(lv_indev_active(), &pt);
    lv_area_t a;
    lv_obj_get_coords(area, &a);
    const int dx = pt.x - (a.x1 + JOY_CX), dy = pt.y - (a.y1 + JOY_CY);
    const uint32_t now = nowMs();
    if (code == LV_EVENT_PRESSED) {
        stopPath();   // Any touch on the joystick also stops tap-to-walk (the old STOP button)
        s_joy.press(dx, dy, now);
    } else if (code == LV_EVENT_PRESSING) {
        s_joy.drag(dx, dy, now);
    } else {
        s_joy.release(now);
        joyShow(false, 0, 0);
        return;
    }
    joyShow(true, dx, dy);
}

static void configureJoy()
{
    const AppConfig &c = Config::get();
    Otr::JoyConfig jc;
    jc.zones = c.joyZones;
    jc.slowMs = c.joySlowMs;
    jc.fastMs = c.joyFastMs;
    jc.holdMs = c.joyHoldMs;
    jc.diag = c.joyDiag;
    s_joy.configure(jc, JOY_R, JOY_STOP_R);
}

static lv_obj_t *circle(lv_obj_t *parent, int32_t cx, int32_t cy, int32_t r, uint32_t bg, lv_opa_t opa, uint32_t border)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, 2 * r, 2 * r);
    lv_obj_set_pos(o, cx - r, cy - r);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, Theme::c(bg), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_border_color(o, Theme::c(border), 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
    return o;
}

// The whole centre column of the control panel is the touch area (touches outside the ring count as the
// outer zone); the drawing is a ring with four arrows, a red stop zone and the finger knob.
static void buildJoystick(lv_obj_t *scr)
{
    lv_obj_t *area = lv_obj_create(scr);
    lv_obj_remove_style_all(area);
    lv_obj_set_size(area, JOY_AREA_W, JOY_AREA_H);
    lv_obj_set_pos(area, JOY_AREA_X, CTRL_Y);
    lv_obj_set_clickable(area, true);
    lv_obj_set_scrollable(area, false);
    lv_obj_set_gesture_bubble(area, false);
    lv_obj_add_event_cb(area, joyEvent, LV_EVENT_ALL, nullptr);

    circle(area, JOY_CX, JOY_CY, JOY_R, Theme::PANEL_RAISED, LV_OPA_COVER, Theme::LINE);
    // Speed zone guide ring halfway out (subtle)
    lv_obj_t *mid = circle(area, JOY_CX, JOY_CY, (JOY_R + JOY_STOP_R) / 2 + 4, Theme::PANEL, LV_OPA_COVER, Theme::LINE);
    lv_obj_set_style_border_width(mid, 1, 0);
    lv_obj_set_style_border_opa(mid, LV_OPA_50, 0);
    circle(area, JOY_CX, JOY_CY, JOY_STOP_R, 0x5A2328, LV_OPA_COVER, Theme::RED);

    static const char *const SYM[4] = {LV_SYMBOL_UP, LV_SYMBOL_RIGHT, LV_SYMBOL_DOWN, LV_SYMBOL_LEFT};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *l = Theme::label(area, SYM[i], &my_font_18, Theme::MUTED);
        const int off = JOY_R - 14;
        const int ax = Otr::dirX((Dir)i) * off, ay = Otr::dirY((Dir)i) * off;
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_update_layout(l);
        lv_obj_set_pos(l, JOY_CX + ax - lv_obj_get_width(l) / 2, JOY_CY + ay - lv_obj_get_height(l) / 2);
        lv_obj_set_clickable(l, false);
        s_joyArrow[i] = l;
    }
    s_joyKnob = circle(area, JOY_CX, JOY_CY, JOY_KNOB_R, Theme::ACCENT, LV_OPA_80, Theme::TEXT);
    lv_obj_set_hidden(s_joyKnob, true);
    s_joyLit = -1;
    configureJoy();
}

// Viewport: tap = walk there (A*), tap on the hero = hit, long press (minefield) = flag.
static void viewEvent(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_longDone = false;
        return;
    }
    if (code != LV_EVENT_LONG_PRESSED && code != LV_EVENT_RELEASED) return;
    if (s_dialog || talking() || s_world->gameOver()) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    int32_t camX, camY;
    currentCamera(camX, camY);
    const float fx = (float)(p.x + camX) / s_ts, fy = (float)(p.y - VIEW_Y + camY) / s_ts;
    if (code == LV_EVENT_LONG_PRESSED) {
        if (!s_world->fog()) return;
        s_longDone = true;
        if (s_world->toggleFlag((int)floorf(fx), (int)floorf(fy))) {
            s_dirty = true;
            Rgb::effectFlash(255, 40, 40, 120);
        }
        return;
    }
    if (s_longDone) return;
    // On the hero (a bit generous): hit
    const float hx = fx - (s_world->playerX() + 0.5f), hy = fy - (s_world->playerY() + 0.5f);
    if (fabsf(hx) < 0.6f && fabsf(hy) < 0.6f) {
        stopPath();
        doHit();
        return;
    }
    // On a person: walk there and talk
    const int cx = (int)floorf(fx), cy = (int)floorf(fy);
    if (const int num = s_world->npcAt(cx, cy)) {
        if (!s_world->fog() || s_world->revealed(cx, cy)) {
            walkToTalk(num, cx, cy);
            return;
        }
    }
    int tx, ty;
    if (!Otr::Planner::pickTarget(*s_world, fx, fy, tx, ty)) {
        toast(tr(Str::ADV_CANT_GO), Theme::MUTED, 1500);
        return;
    }
    s_replanned = false;
    walkTo(tx, ty);
}

static void freeAll()
{
    liveStop();   // Before the world memory goes
    closeDialog();
    if (s_timer) lv_timer_delete(s_timer);
    s_timer = nullptr;
    heap_caps_free(s_fb);
    heap_caps_free(s_spr);
    heap_caps_free(s_world);
    heap_caps_free(s_planner);
    heap_caps_free(s_path);
    heap_caps_free(s_saveBuf);
    s_fb = nullptr;
    s_spr = nullptr;
    s_world = nullptr;
    s_planner = nullptr;
    s_path = nullptr;
    s_saveBuf = nullptr;
    s_activeScr = nullptr;
    s_scr = nullptr;
    s_joyKnob = nullptr;
    s_muteBtn = nullptr;
    s_hitBtn = nullptr;
    s_hitTalk = false;
    s_talk.end();   // The dialogue objects went with the screen
    s_talkBox = s_talkPanel = s_talkPortrait = s_talkName = s_talkText = s_talkRef = nullptr;
    s_talkNum = s_walkTalkNum = 0;
    s_twText = nullptr;
    s_pendingChoice = -2;
    s_joy.cancel();
    s_pathPos = -1;
}

// The buffers belong to the screen that is shown; they are freed when that screen is deleted
// (after the fade-out, so the canvas never draws from freed memory).
static void screenDeleted(lv_event_t *e)
{
    if (lv_event_get_current_target(e) == s_activeScr) freeAll();
}

static void leave()
{
    stopPath();
    closeDialog();
    if (s_timer) lv_timer_pause(s_timer);
    s_joy.cancel();
    saveNow();
    liveStop();
    UiYatzy::showMenu();   // Switches the music back to the dice loop
}

static const char *modeText() { return tr(Config::get().advMode == 1 ? Str::ADV_MODE_PRO : Str::ADV_MODE_ASSIST); }

static void toggleMode(lv_event_t *e)
{
    if (!Theme::debounce(s_modeTapMs)) return;
    AppConfig &c = Config::get();
    c.advMode = c.advMode == 1 ? 0 : 1;
    Config::save();
    lv_obj_t *b = (lv_obj_t *)lv_event_get_target(e);
    lv_label_set_text(lv_obj_get_child(b, 0), modeText());
}

static lv_obj_t *menuButton(lv_obj_t *parent, const char *text, uint32_t color, lv_event_cb_t cb)
{
    lv_obj_t *b = Theme::button(parent, text, 250, 42, color);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

static const char *soundText()
{
    return Config::get().soundOn ? trp(LV_SYMBOL_VOLUME_MAX "  ", Str::ADV_SOUND_ON)
                                 : trp(LV_SYMBOL_MUTE "  ", Str::ADV_SOUND_OFF);
}

static const char *zoomText()
{
    // 32 px tiles -> offer "see more" (16 px); 16 px -> offer zooming back in
    return s_ts == 32 ? trp(LV_SYMBOL_MINUS "  ", Str::ADV_ZOOM_OUT) : trp(LV_SYMBOL_PLUS "  ", Str::ADV_ZOOM_IN);
}

static void muteIcon()
{
    if (s_muteBtn) lv_label_set_text(lv_obj_get_child(s_muteBtn, 0), Config::get().soundOn ? LV_SYMBOL_VOLUME_MAX : LV_SYMBOL_MUTE);
}

// Master sound on/off (same setting as Settings > Sound), from the quick button or the menu.
// Returns false when swallowed by the double-tap guard.
static bool toggleSound()
{
    if (!Theme::debounce(s_soundTapMs)) return false;
    AppConfig &c = Config::get();
    c.soundOn = !c.soundOn;
    Config::save();
    Audio::setVolumes(c.soundOn, c.sfxVol, c.musicOn, c.musicVol);
    muteIcon();
    return true;
}

static void toggleZoom()
{
    if (!Theme::debounce(s_zoomTapMs)) return;
    s_ts = s_ts == 32 ? 16 : 32;
    snapPlayer();
    s_dirty = true;
}

static void showMenu()
{
    if (s_dialog || talking()) return;   // Already open (double tap on the hamburger) / talking
    stopPath();
    s_joy.cancel();
    joyShow(false, 0, 0);
    lv_obj_t *mb = dialog(nullptr);
    lv_msgbox_add_title(mb, tr(Str::ADV_MENU_TITLE));
    lv_msgbox_add_close_button(mb);
    lv_obj_t *c = lv_msgbox_get_content(mb);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c, 8, 0);
    menuButton(c, soundText(), Theme::LINE, [](lv_event_t *e) {
        if (toggleSound()) lv_label_set_text(lv_obj_get_child(lv_event_get_target_obj(e), 0), soundText());
    });
    menuButton(c, zoomText(), Theme::LINE, [](lv_event_t *e) {
        toggleZoom();
        lv_label_set_text(lv_obj_get_child(lv_event_get_target_obj(e), 0), zoomText());
    });
    menuButton(c, modeText(), Theme::TEAL, toggleMode);
    lv_obj_t *help = lv_label_create(c);
    lv_label_set_text(help, tr(Str::ADV_MODE_HELP));
    lv_label_set_long_mode(help, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(help, 250);
    if (s_playerId) {
        menuButton(c, tr(Str::ADV_SAVE), Theme::GREEN, [](lv_event_t *) {
            const bool ok = saveNow();
            closeDialog();
            toast(tr(ok ? Str::ADV_SAVED : Str::ADV_SAVE_FAIL), ok ? Theme::GREEN : Theme::RED, 1500);
        });
    }
    menuButton(c, tr(Str::ADV_RESTART), Theme::ACCENT, [](lv_event_t *) {
        lv_obj_t *q = dialog(tr(Str::ADV_RESTART_Q));
        lv_obj_t *yes = lv_msgbox_add_footer_button(q, tr(Str::ADV_RESTART));
        lv_obj_t *no = lv_msgbox_add_footer_button(q, tr(Str::BTN_CANCEL));
        lv_obj_add_event_cb(yes, [](lv_event_t *) {
            closeDialog();
            resetWorld();
            saveNow();
        }, LV_EVENT_CLICKED, nullptr);
        lv_obj_add_event_cb(no, [](lv_event_t *) { closeDialog(); }, LV_EVENT_CLICKED, nullptr);
        Theme::msgboxFonts(q);
    });
    menuButton(c, tr(Str::ADV_LEAVE), Theme::RED, [](lv_event_t *) { leave(); });
    lv_obj_t *perf = lv_label_create(c);
    lv_label_set_text(perf, s_perfText);
    Theme::msgboxFonts(mb);
    lv_obj_set_style_text_font(help, &my_font_14, 0);
    lv_obj_set_style_text_font(perf, &my_font_14, 0);
    lv_obj_set_style_text_color(perf, Theme::c(Theme::MUTED), 0);
    // The close button (x) only deletes the box: forget it
    lv_obj_add_event_cb(mb, [](lv_event_t *e) {
        if (lv_event_get_current_target(e) == s_dialog) s_dialog = nullptr;
    }, LV_EVENT_DELETE, nullptr);
}

// 32 px sprite -> HUD_PX icon: box-filtered, colour-key pixels count as panel colour, so the edges
// are anti-aliased against the panel background.
static void makeHudIcons()
{
    const lv_color_t pc = Theme::c(Theme::PANEL);
    const int pr = pc.red, pg = pc.green, pb = pc.blue;
    for (int i = 0; i < H_COUNT; i++) {
        const uint16_t *src = s_spr->big[HUD_SPRITE[i]];
        for (int y = 0; y < HUD_PX; y++)
            for (int x = 0; x < HUD_PX; x++) {
                const int sx0 = x * SPR / HUD_PX, sx1 = (x + 1) * SPR / HUD_PX;
                const int sy0 = y * SPR / HUD_PX, sy1 = (y + 1) * SPR / HUD_PX;
                int r = 0, g = 0, b = 0, n = 0;
                for (int sy = sy0; sy < sy1; sy++)
                    for (int sx = sx0; sx < sx1; sx++, n++) {
                        const uint16_t c = src[sy * SPR + sx];
                        if (c == KEY) { r += pr; g += pg; b += pb; continue; }
                        r += ((c >> 11) & 0x1F) * 255 / 31;
                        g += ((c >> 5) & 0x3F) * 255 / 63;
                        b += (c & 0x1F) * 255 / 31;
                    }
                s_spr->hud[i][y * HUD_PX + x] =
                    (uint16_t)((((r / n) >> 3) << 11) | (((g / n) >> 2) << 5) | ((b / n) >> 3));
            }
    }
}

static lv_obj_t *hudIcon(lv_obj_t *parent, HudIcon i)
{
    lv_obj_t *cv = lv_canvas_create(parent);
    lv_canvas_set_buffer(cv, s_spr->hud[i], HUD_PX, HUD_PX, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_clickable(cv, false);
    return cv;
}

// Bottom control panel (Kent's v0.10.x sketch):
//   left   symbol + count rows (lives, suit, keys, gems) above the menu (hamburger) and mute buttons
//   centre large joystick (its red centre also stops tap-to-walk)
//   right  Slå box, red energy bar, icons of carried items (boat, later knife/sword ...; 2 rows)
static void buildControls()
{
    lv_obj_t *pan = lv_obj_create(s_scr);
    lv_obj_remove_style_all(pan);
    lv_obj_set_size(pan, 320, CTRL_H);
    lv_obj_set_pos(pan, 0, CTRL_Y);
    lv_obj_set_style_bg_color(pan, Theme::c(Theme::PANEL), 0);
    lv_obj_set_style_bg_opa(pan, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(pan, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(pan, 1, 0);
    lv_obj_set_style_border_color(pan, Theme::c(Theme::LINE), 0);
    lv_obj_set_scrollable(pan, false);
    makeHudIcons();

    // Left column: icon + count per row, top-aligned (a 5th row still fits above the buttons)
    lv_obj_t *info = lv_obj_create(pan);
    lv_obj_remove_style_all(info);
    lv_obj_set_pos(info, 6, 6);
    lv_obj_set_size(info, COL_L_W - 8, CTRL_H - 54);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(info, 2, 0);
    lv_obj_set_scrollable(info, false);
    lv_obj_set_clickable(info, false);
    static constexpr HudIcon rowIcon[C_COUNT] = {H_LIFE, H_SUIT, H_KEY, H_GEM};
    for (int r = 0; r < C_COUNT; r++) {
        lv_obj_t *row = lv_obj_create(info);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, COL_L_W - 8, HUD_PX);
        lv_obj_set_scrollable(row, false);
        lv_obj_set_clickable(row, false);
        hudIcon(row, rowIcon[r]);
        s_count[r] = Theme::label(row, "", &my_font_14, Theme::TEXT);
        lv_obj_align(s_count[r], LV_ALIGN_LEFT_MID, HUD_PX + 6, 0);
    }
    lv_obj_t *menu = Theme::button(pan, LV_SYMBOL_LIST, 36, 40, Theme::ACCENT);
    lv_obj_set_pos(menu, 4, CTRL_H - 48);
    lv_obj_add_event_cb(menu, [](lv_event_t *) {
        if (Theme::debounce(s_menuTapMs)) showMenu();
    }, LV_EVENT_CLICKED, nullptr);
    s_muteBtn = Theme::button(pan, LV_SYMBOL_VOLUME_MAX, 34, 40, Theme::TEAL);
    lv_obj_set_pos(s_muteBtn, 43, CTRL_H - 48);
    lv_obj_add_event_cb(s_muteBtn, [](lv_event_t *) { toggleSound(); }, LV_EVENT_CLICKED, nullptr);
    muteIcon();

    // Right column: the box on top is the Slå (hit) button
    // (Prata when facing a person, see tick())
    s_hitBtn = Theme::button(pan, tr(Str::ADV_HIT), COL_R_W, 56, Theme::ACCENT, true);
    s_hitTalk = false;
    lv_obj_set_pos(s_hitBtn, COL_R_X, 8);
    lv_obj_add_event_cb(s_hitBtn, [](lv_event_t *) {
        if (!Theme::debounce(s_hitTapMs)) return;
        stopPath();
        doHit();
    }, LV_EVENT_PRESSED, nullptr);
    s_bar = lv_bar_create(pan);
    lv_obj_set_pos(s_bar, COL_R_X, 74);
    lv_obj_set_size(s_bar, COL_R_W, 18);
    lv_obj_set_style_bg_color(s_bar, Theme::c(0xEEF2F7), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_bar, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_bar, Theme::c(0xE53935), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    s_barLbl = Theme::label(s_bar, "", &my_font_14, Theme::BG);
    lv_obj_center(s_barLbl);
    // Carried items: icons in a wrapping row, two rows of three fit under the energy bar
    s_itemBox = lv_obj_create(pan);
    lv_obj_remove_style_all(s_itemBox);
    lv_obj_set_pos(s_itemBox, COL_R_X, 100);
    lv_obj_set_size(s_itemBox, COL_R_W, CTRL_H - 104);
    lv_obj_set_flex_flow(s_itemBox, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(s_itemBox, 6, 0);
    lv_obj_set_style_pad_row(s_itemBox, 6, 0);
    lv_obj_set_scrollable(s_itemBox, false);
    lv_obj_set_clickable(s_itemBox, false);
    for (int i = 0; i < H_COUNT; i++) s_item[i] = nullptr;
    s_item[H_BOAT] = hudIcon(s_itemBox, H_BOAT);

    buildJoystick(s_scr);
}

// ----------------------------------------------------------------------------------------
// Game screen
// ----------------------------------------------------------------------------------------
static void buildGame(bool loadFailed)
{
    s_scr = Theme::screen();
    s_activeScr = s_scr;
    lv_obj_add_event_cb(s_scr, screenDeleted, LV_EVENT_DELETE, nullptr);

    // Viewport canvas
    s_canvas = lv_canvas_create(s_scr);
    lv_canvas_set_buffer(s_canvas, s_fb, VIEW_W, VIEW_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas, 0, VIEW_Y);
    lv_obj_set_clickable(s_canvas, true);
    lv_obj_add_event_cb(s_canvas, viewEvent, LV_EVENT_ALL, nullptr);

    s_toast = Theme::label(s_scr, "", &my_font_14);
    lv_label_set_long_mode(s_toast, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(s_toast, VIEW_W - 24);
    lv_obj_set_style_text_align(s_toast, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(s_toast, Theme::c(0x000000), 0);
    lv_obj_set_style_bg_opa(s_toast, 190, 0);
    lv_obj_set_style_radius(s_toast, 10, 0);
    lv_obj_set_style_pad_all(s_toast, 8, 0);
    lv_obj_align(s_toast, LV_ALIGN_TOP_MID, 0, VIEW_Y + VIEW_H - 64);
    lv_obj_set_clickable(s_toast, false);
    lv_obj_set_hidden(s_toast, true);

    buildControls();
    snapPlayer();
    s_fxX = -1;
    s_bumpT0 = 0;
    s_dirty = true;
    updateStats();
    toast(tr(loadFailed ? Str::ADV_LOAD_FAIL : s_world->fog() ? Str::ADV_HELP_MINES : Str::ADV_HELP),
          loadFailed ? Theme::RED : Theme::MUTED, 6000);
    markSaved();
    s_lastCamX = s_lastCamY = -1;
    s_statT0 = nowMs();
    s_renders = s_renderAccUs = 0;
    s_perfText[0] = 0;
    AdvLive::invalidateMap();
    s_liveValid = false;
    tick(nullptr);   // Also publishes the first live snapshot
    s_timer = lv_timer_create(tick, TICK_MS, nullptr);
    Theme::loadScreen(s_scr);
    Audio::music(Sound::Music::ADVENTURE);
    if (s_world->gameOver()) showGameOver();
}

static void startGame(bool cont)
{
    closeDialog();
    s_story = Otr::WORLDS[s_worldIdx].id == Otr::WORLD_VILLAGE ? &Otr::Story::VILLAGE_STORY : nullptr;
    s_storyState.clear();
    if (!s_world->load(Otr::WORLDS[s_worldIdx])) {
        ESP_LOGE(TAG, "world %d failed to load", s_worldIdx);
        return;
    }
    bool failed = false;
    if (cont && !loadSave()) {
        failed = true;
        s_world->load(Otr::WORLDS[s_worldIdx]);   // restore() leaves it untouched, but be explicit
        s_storyState.clear();
    }
    stopPath();
    buildGame(failed);
}

// ----------------------------------------------------------------------------------------
// Setup screen: world chooser + player (registry or guest) + continue / new game
// ----------------------------------------------------------------------------------------
static lv_obj_t *s_worldBtn[Otr::WORLD_COUNT];
static lv_obj_t *s_playerList;

static void styleWorldButtons()
{
    for (int i = 0; i < Otr::WORLD_COUNT; i++) {
        const bool sel = i == s_worldIdx;
        lv_obj_set_style_border_color(s_worldBtn[i], Theme::c(sel ? Theme::GOLD : Theme::LINE), 0);
        lv_obj_set_style_border_width(s_worldBtn[i], sel ? 3 : 2, 0);
        lv_obj_set_style_bg_color(s_worldBtn[i], Theme::c(sel ? 0x2A3F5C : Theme::PANEL_RAISED), 0);
    }
}

static void playerChosen(lv_event_t *e)
{
    s_playerId = (uint16_t)(uintptr_t)lv_event_get_user_data(e);
    if (!hasSave(s_playerId, s_worldIdx)) {
        startGame(false);
        return;
    }
    lv_obj_t *mb = dialog(tr(Str::ADV_CONTINUE_Q));
    lv_obj_t *cont = lv_msgbox_add_footer_button(mb, tr(Str::ADV_CONTINUE));
    lv_obj_t *fresh = lv_msgbox_add_footer_button(mb, tr(Str::ADV_NEW_GAME));
    lv_obj_t *cancel = lv_msgbox_add_footer_button(mb, tr(Str::BTN_CANCEL));
    lv_obj_add_event_cb(cont, [](lv_event_t *) { startGame(true); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(fresh, [](lv_event_t *) { startGame(false); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(cancel, [](lv_event_t *) { closeDialog(); }, LV_EVENT_CLICKED, nullptr);
    Theme::msgboxFonts(mb);
}

static void addPlayerRow(const char *name, uint16_t id)
{
    char text[64];
    snprintf(text, sizeof(text), "%s%s", name, hasSave(id, s_worldIdx) ? tr(Str::ADV_HAS_SAVE) : "");
    lv_obj_t *b = Theme::button(s_playerList, text, 296, 42, id ? Theme::TEAL : Theme::LINE);
    lv_obj_align(lv_obj_get_child(b, 0), LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_add_event_cb(b, playerChosen, LV_EVENT_CLICKED, (void *)(uintptr_t)id);
}

static void fillPlayerList()
{
    lv_obj_clean(s_playerList);
    addPlayerRow(tr(Str::ADV_GUEST), 0);
    PlayerDb *db = (PlayerDb *)heap_caps_malloc(sizeof(PlayerDb), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *idx = db ? (uint8_t *)heap_caps_malloc(PLAYER_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr;
    if (db && idx && Players::ok()) {
        Players::snapshot(*db);
        const int n = db->sortedIndices(idx);
        for (int i = 0; i < n; i++) addPlayerRow(db->items[idx[i]].name, db->items[idx[i]].id);
    }
    heap_caps_free(idx);
    heap_caps_free(db);
}

static void buildSetup()
{
    lv_obj_t *scr = Theme::screen();
    s_activeScr = scr;
    lv_obj_add_event_cb(scr, screenDeleted, LV_EVENT_DELETE, nullptr);
    lv_obj_t *h = Theme::header(scr, tr(Str::ADV_SETUP_TITLE));
    lv_obj_t *back = Theme::button(h, LV_SYMBOL_LEFT, 44, 36, Theme::MUTED);
    lv_obj_align(back, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_add_event_cb(back, [](lv_event_t *) {
        closeDialog();
        UiYatzy::showMenu();   // Buffers are freed when this screen is deleted
    }, LV_EVENT_CLICKED, nullptr);

    static const Str NAMES[Otr::WORLD_COUNT] = {Str::ADV_WORLD_TEST, Str::ADV_WORLD_MINES, Str::ADV_WORLD_VILLAGE};
    static const Str DESCS[Otr::WORLD_COUNT] = {Str::ADV_WORLD_TEST_DESC, Str::ADV_WORLD_MINES_DESC,
                                                Str::ADV_WORLD_VILLAGE_DESC};
    static constexpr int32_t WB_W = (308 - (Otr::WORLD_COUNT - 1) * 4) / Otr::WORLD_COUNT;   // 100 px for 3
    for (int i = 0; i < Otr::WORLD_COUNT; i++) {
        lv_obj_t *b = Theme::button(scr, tr(NAMES[i]), WB_W, 82, Theme::LINE);
        lv_obj_set_pos(b, 6 + i * (WB_W + 4), 48);
        lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &my_font_14, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(b, 0), Theme::c(Theme::GOLD), 0);
        lv_obj_align(lv_obj_get_child(b, 0), LV_ALIGN_TOP_MID, 0, 5);
        lv_obj_t *d = Theme::label(b, tr(DESCS[i]), &my_font_14, Theme::MUTED);
        lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_width(d, WB_W - 8);
        lv_obj_set_style_text_align(d, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(d, LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_obj_add_event_cb(b, [](lv_event_t *e) {
            static uint32_t s_worldTapMs;
            if (!Theme::debounce(s_worldTapMs, 250)) return;
            s_worldIdx = (int)(intptr_t)lv_event_get_user_data(e);
            styleWorldButtons();
            fillPlayerList();
        }, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        s_worldBtn[i] = b;
    }
    styleWorldButtons();
    lv_obj_t *mode = Theme::button(scr, modeText(), 308, 38, Theme::TEAL);
    lv_obj_set_pos(mode, 6, 138);
    lv_obj_add_event_cb(mode, toggleMode, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *who = Theme::label(scr, tr(Str::ADV_WHO), &my_font_18, Theme::GOLD);
    lv_obj_set_pos(who, 10, 184);
    s_playerList = lv_obj_create(scr);
    lv_obj_remove_style_all(s_playerList);
    lv_obj_set_pos(s_playerList, 6, 210);
    lv_obj_set_size(s_playerList, 308, 480 - 210 - 4);
    lv_obj_set_flex_flow(s_playerList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_playerList, 6, 0);
    lv_obj_set_scroll_dir(s_playerList, LV_DIR_VER);
    fillPlayerList();
    Theme::loadScreen(scr);
}

void show()
{
    freeAll();
    s_fb = (uint16_t *)heap_caps_aligned_alloc(64, VIEW_W * VIEW_H * 2, MALLOC_CAP_SPIRAM);
    s_spr = (Sprites *)heap_caps_malloc(sizeof(Sprites), MALLOC_CAP_SPIRAM);
    void *wmem = heap_caps_malloc(sizeof(Otr::World), MALLOC_CAP_SPIRAM);
    void *pmem = heap_caps_malloc(sizeof(Otr::Planner), MALLOC_CAP_SPIRAM);
    s_path = (Otr::Path *)heap_caps_malloc(sizeof(Otr::Path), MALLOC_CAP_SPIRAM);
    s_saveBuf = (uint8_t *)heap_caps_malloc(SAVE_CAP, MALLOC_CAP_SPIRAM);
    if (!s_fb || !s_spr || !wmem || !pmem || !s_path || !s_saveBuf) {
        ESP_LOGE(TAG, "out of PSRAM");
        heap_caps_free(wmem);
        heap_caps_free(pmem);
        freeAll();
        UiYatzy::showMenu();
        return;
    }
    s_world = new (wmem) Otr::World;
    s_planner = new (pmem) Otr::Planner;
    memset(s_path, 0, sizeof(*s_path));
    const int64_t t0 = esp_timer_get_time();
    makeArt(s_spr);
    ESP_LOGI(TAG, "tile art %d ms (%u B sprites, %u B frame, world %u B, planner %u B)",
             (int)((esp_timer_get_time() - t0) / 1000), (unsigned)sizeof(Sprites), (unsigned)(VIEW_W * VIEW_H * 2),
             (unsigned)sizeof(Otr::World), (unsigned)sizeof(Otr::Planner));
    if (s_worldIdx < 0 || s_worldIdx >= Otr::WORLD_COUNT) s_worldIdx = 0;
    buildSetup();
}

}  // namespace UiAdventure
