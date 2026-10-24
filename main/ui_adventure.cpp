// ui_adventure.cpp - OnTheRoad Engine test world (see header).
#include "ui_adventure.h"

#include <math.h>
#include <new>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board_rgb.h"
#include "lang.h"
#include "my_lvgl.h"
#include "my_theme.h"
#include "otr_maps.h"
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
static constexpr int32_t VIEW_Y = 44, VIEW_W = 320, VIEW_H = 320;
static constexpr int32_t CTRL_Y = VIEW_Y + VIEW_H + 4;          // 368
static constexpr uint32_t TICK_MS = 15;
static constexpr uint32_t WALK_MS_32 = 150, WALK_MS_16 = 115, BUMP_MS = 110, FX_MS = 220;
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
    S_COUNT
};

struct Sprites {
    uint16_t big[S_COUNT][SPR * SPR];
    uint16_t small[S_COUNT][(SPR / 2) * (SPR / 2)];
};

static Sprites *s_spr = nullptr;
static Otr::World *s_world = nullptr;
static uint16_t *s_fb = nullptr;   // Canvas buffer VIEW_W x VIEW_H

// Screen objects
static lv_obj_t *s_scr, *s_canvas, *s_stats, *s_perf, *s_toast;
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
static int s_hold = -1;                       // Held direction (Dir) or -1
static bool s_tapPlayer;
static uint32_t s_pressT0;
static bool s_dirty = true;
static uint32_t s_lastRev, s_lastFrame;
static int32_t s_lastCamX = -1, s_lastCamY = -1;
// Statistics
static uint32_t s_renderUs, s_renderAccUs, s_renders, s_statT0;

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

static void artPlayer(uint16_t *s, Dir d)
{
    fillKey(s);
    // Shadow
    for (int y = 26; y < 31; y++)
        for (int x = 8; x < 24; x++) {
            const float dx = (x - 15.5f) / 8.f, dy = (y - 28.f) / 2.5f;
            if (dx * dx + dy * dy <= 1.f) px(s, x, y, rgb(20, 40, 24));
        }
    // Body (orange tunic) + belt
    for (int y = 14; y < 28; y++)
        for (int x = 9; x < 23; x++) {
            const float dx = (x - 15.5f) / 7.f;
            if (y > 16 || dx * dx < 0.6f) px(s, x, y, rgb(255 - (x - 9) * 4, 150 - (x - 9) * 3, 58));
        }
    rect(s, 9, 22, 22, 23, rgb(90, 56, 30));
    // Head
    disc(s, 15.5f, 10.f, 6.5f, rgb(244, 200, 160));
    // Hair
    const uint16_t hair = rgb(92, 58, 34);
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
    const uint16_t eye = rgb(30, 30, 46);
    switch (d) {
    case Dir::DOWN: px(s, 13, 11, eye); px(s, 18, 11, eye); px(s, 13, 10, eye); px(s, 18, 10, eye); break;
    case Dir::LEFT: px(s, 11, 10, eye); px(s, 11, 11, eye); break;
    case Dir::RIGHT: px(s, 20, 10, eye); px(s, 20, 11, eye); break;
    default: break;
    }
    // Arms hint at the facing direction
    const uint16_t skin = rgb(230, 186, 146);
    if (d == Dir::LEFT) rect(s, 6, 17, 9, 19, skin);
    else if (d == Dir::RIGHT) rect(s, 22, 17, 25, 19, skin);
    else { rect(s, 7, 17, 9, 21, skin); rect(s, 22, 17, 24, 21, skin); }
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
    fillKey(b[S_IMPACT]);
    for (int i = 0; i < 8; i++) {
        const float a = i * 0.785f;
        line(b[S_IMPACT], 16 + (int)(cosf(a) * 4), 16 + (int)(sinf(a) * 4), 16 + (int)(cosf(a) * 13), 16 + (int)(sinf(a) * 13),
             (i & 1) ? rgb(255, 220, 120) : rgb(255, 255, 240), 2);
    }
    disc(b[S_IMPACT], 16.f, 16.f, 3.5f, rgb(255, 255, 255));

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
    default: return -1;
    }
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
    cx = cx < 0 ? 0 : cx > ww - VIEW_W ? ww - VIEW_W : cx;
    cy = cy < 0 ? 0 : cy > wh - VIEW_H ? wh - VIEW_H : cy;
}

static void render(uint32_t now, int32_t camX, int32_t camY, int ppx, int ppy, int frame)
{
    const int64_t t0 = esp_timer_get_time();
    const int ts = s_ts;
    const int tx0 = camX / ts, ty0 = camY / ts;
    const int tx1 = (camX + VIEW_W - 1) / ts, ty1 = (camY + VIEW_H - 1) / ts;
    const bool rockSliding = s_rockAnim && moveProgress(now) < 1.f;
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++) {
            const int sx = tx * ts - camX, sy = ty * ts - camY;
            blit(terrainSprite(tx, ty, frame), sx, sy, false);
            if (rockSliding && tx == s_rockToX && ty == s_rockToY) continue;   // Drawn below
            const int o = objSprite(s_world->obj(tx, ty));
            if (o >= 0) blit(o, sx, sy, o != S_DOOR);
        }
    if (rockSliding) {
        const float t = moveProgress(now);
        const int rx = (int)lroundf((s_rockFromX + (s_rockToX - s_rockFromX) * t) * ts);
        const int ry = (int)lroundf((s_rockFromY + (s_rockToY - s_rockFromY) * t) * ts);
        blit(S_ROCK, rx - camX, ry - camY, true);
    }
    if (s_world->sailing()) blit(S_BOAT, ppx - camX, ppy - camY + ts / 8, true);
    blit(S_PL_UP + (int)s_world->facing(), ppx - camX, ppy - camY - (s_world->sailing() ? ts / 8 : 0), true);
    if (s_fxX >= 0 && now - s_fxT0 < FX_MS) blit(S_IMPACT, s_fxX * ts - camX, s_fxY * ts - camY, true);
    s_renderUs = (uint32_t)(esp_timer_get_time() - t0);
}

// ----------------------------------------------------------------------------------------
// HUD
// ----------------------------------------------------------------------------------------
static void updateStats()
{
    char buf[96];
    snprintf(buf, sizeof(buf), tr(Str::ADV_STATS), s_world->gems(), s_world->gemsTotal(), s_world->keys());
    if (s_world->hasBoat()) strncat(buf, tr(Str::ADV_BOAT_TAG), sizeof(buf) - strlen(buf) - 1);
    lv_label_set_text(s_stats, buf);
}

static void toast(const char *text, uint32_t color = Theme::TEXT)
{
    lv_label_set_text(s_toast, text);
    lv_obj_set_style_text_color(s_toast, Theme::c(color), 0);
    lv_obj_set_hidden(s_toast, false);
    s_toastUntil = nowMs() + TOAST_MS;
}

// ----------------------------------------------------------------------------------------
// Actions
// ----------------------------------------------------------------------------------------
static bool busy(uint32_t now) { return s_moveMs && now - s_moveT0 < s_moveMs; }

static void doMove(Dir d)
{
    const uint32_t now = nowMs();
    if (busy(now)) return;
    const int fx = s_world->playerX(), fy = s_world->playerY();
    const int tx = fx + Otr::dirX(d), ty = fy + Otr::dirY(d);
    const bool wasRock = s_world->obj(tx, ty) == Obj::ROCK;
    const Tile targetTile = s_world->tile(tx, ty);
    const uint32_t ev = s_world->move(d);
    s_dirty = true;

    if (ev & Otr::EV_BURNED) {
        s_fromX = s_world->playerX();
        s_fromY = s_world->playerY();
        s_moveMs = 0;
        toast(tr(Str::ADV_BURNED), Theme::RED);
        Rgb::effectFlash(255, 60, 20, 500);
        return;
    }
    if (ev & Otr::EV_MOVED) {
        s_fromX = fx;
        s_fromY = fy;
        s_moveT0 = now;
        s_moveMs = s_ts == 32 ? WALK_MS_32 : WALK_MS_16;
        s_rockAnim = wasRock && (ev & Otr::EV_PUSHED) && !(ev & (Otr::EV_SPLASH | Otr::EV_SIZZLE));
        if (s_rockAnim) {
            s_rockFromX = tx;
            s_rockFromY = ty;
            s_rockToX = tx + Otr::dirX(d);
            s_rockToY = ty + Otr::dirY(d);
            s_moveMs += 40;   // Pushing is a little slower
        }
    } else {
        s_bumpDx = Otr::dirX(d);
        s_bumpDy = Otr::dirY(d);
        s_bumpT0 = now;
    }

    char buf[64];
    if (ev & Otr::EV_ALL_GEMS) {
        toast(tr(Str::ADV_ALL_GEMS), Theme::GOLD);
        Rgb::effectFlash(255, 210, 60, 900);
    } else if (ev & Otr::EV_GEM) {
        snprintf(buf, sizeof(buf), tr(Str::ADV_GEM), s_world->gems(), s_world->gemsTotal());
        toast(buf, Theme::TEAL);
        Rgb::effectFlash(40, 200, 255, 250);
    } else if (ev & Otr::EV_KEY) {
        toast(tr(Str::ADV_KEY), Theme::GOLD);
    } else if (ev & Otr::EV_BOAT) {
        toast(tr(Str::ADV_BOAT), Theme::GOLD);
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
    } else if ((ev & Otr::EV_BLOCKED) && targetTile == Tile::WATER && !s_world->hasBoat()) {
        toast(tr(Str::ADV_NEED_BOAT), Theme::BLUE);
    }
    if (ev & (Otr::EV_GEM | Otr::EV_KEY | Otr::EV_BOAT | Otr::EV_DOOR)) updateStats();
}

static void doHit()
{
    const uint32_t now = nowMs();
    if (busy(now)) return;
    const int tx = s_world->playerX() + Otr::dirX(s_world->facing());
    const int ty = s_world->playerY() + Otr::dirY(s_world->facing());
    const uint32_t ev = s_world->hit();
    s_bumpDx = Otr::dirX(s_world->facing());
    s_bumpDy = Otr::dirY(s_world->facing());
    s_bumpT0 = now;
    if (ev & Otr::EV_HIT) {
        s_fxX = tx;
        s_fxY = ty;
        s_fxT0 = now;
        Rgb::effectFlash(255, 255, 255, ev & Otr::EV_BROKE ? 200 : 80);
        if (!(ev & Otr::EV_BROKE) && s_world->tile(tx, ty) == Tile::CRACKED) toast(tr(Str::ADV_CRACK), Theme::MUTED);
    }
    s_dirty = true;
}

static void resetWorld()
{
    if (!s_world->load(Otr::TEST_WORLD, Otr::TEST_WORLD_H)) ESP_LOGE(TAG, "test world failed to load");
    s_fromX = s_world->playerX();
    s_fromY = s_world->playerY();
    s_moveMs = 0;
    s_rockAnim = false;
    s_fxX = -1;
    s_bumpT0 = 0;
    s_dirty = true;
    updateStats();
    toast(tr(Str::ADV_HELP), Theme::MUTED);
    s_toastUntil = nowMs() + 6000;
}

// ----------------------------------------------------------------------------------------
// Frame tick
// ----------------------------------------------------------------------------------------
static void tick(lv_timer_t *)
{
    const uint32_t now = nowMs();
    if (s_hold >= 0 && !busy(now)) doMove((Dir)s_hold);

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

    if (s_toastUntil && now > s_toastUntil) {
        s_toastUntil = 0;
        lv_obj_set_hidden(s_toast, true);
    }
    if (now - s_statT0 >= 1000) {   // Our frames, our blit time, LVGL's flushed frames
        const MyLvgl::Stats st = MyLvgl::stats();
        lv_label_set_text_fmt(s_perf, "%u/s  %u.%u ms  lcd %d fps", (unsigned)s_renders,
                              (unsigned)(s_renders ? s_renderAccUs / s_renders / 1000 : 0),
                              (unsigned)(s_renders ? s_renderAccUs / s_renders / 100 % 10 : 0), (int)(st.fps + 0.5f));
        s_renders = s_renderAccUs = 0;
        s_statT0 = now;
    }
}

// ----------------------------------------------------------------------------------------
// Input
// ----------------------------------------------------------------------------------------
static void padEvent(lv_event_t *e)
{
    const int d = (int)(intptr_t)lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_hold = d;
        doMove((Dir)d);
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && s_hold == d) {
        s_hold = -1;
    }
}

// Finger in the viewport: walk towards it (dominant axis); a short tap on the player hits.
static void viewEvent(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (code == LV_EVENT_RELEASED && s_tapPlayer && nowMs() - s_pressT0 < 400) doHit();
        s_hold = -1;
        s_tapPlayer = false;
        return;
    }
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    int ppx, ppy;
    playerPixel(nowMs(), ppx, ppy);
    int32_t camX, camY;
    camera(ppx, ppy, camX, camY);
    const int dx = p.x - (ppx - camX + s_ts / 2);
    const int dy = (p.y - VIEW_Y) - (ppy - camY + s_ts / 2);
    const int dead = s_ts * 3 / 4;
    if (code == LV_EVENT_PRESSED) {
        s_pressT0 = nowMs();
        s_tapPlayer = abs(dx) < dead && abs(dy) < dead;
    }
    if (abs(dx) < dead && abs(dy) < dead) {
        s_hold = -1;
        return;
    }
    s_tapPlayer = false;
    s_hold = abs(dx) > abs(dy) ? (int)(dx > 0 ? Dir::RIGHT : Dir::LEFT) : (int)(dy > 0 ? Dir::DOWN : Dir::UP);
}

static void freeAll()
{
    if (s_timer) lv_timer_delete(s_timer);
    s_timer = nullptr;
    heap_caps_free(s_fb);
    heap_caps_free(s_spr);
    heap_caps_free(s_world);
    s_fb = nullptr;
    s_spr = nullptr;
    s_world = nullptr;
    s_hold = -1;
}

static lv_obj_t *padButton(lv_obj_t *scr, const char *sym, int32_t x, int32_t y, int32_t w, int32_t h, Dir d)
{
    lv_obj_t *b = Theme::button(scr, sym, w, h, Theme::LINE);
    lv_obj_set_pos(b, x, y);
    lv_obj_add_event_cb(b, padEvent, LV_EVENT_ALL, (void *)(intptr_t)d);
    return b;
}

void show()
{
    freeAll();
    s_fb = (uint16_t *)heap_caps_aligned_alloc(64, VIEW_W * VIEW_H * 2, MALLOC_CAP_SPIRAM);
    s_spr = (Sprites *)heap_caps_malloc(sizeof(Sprites), MALLOC_CAP_SPIRAM);
    void *wmem = heap_caps_malloc(sizeof(Otr::World), MALLOC_CAP_SPIRAM);
    if (!s_fb || !s_spr || !wmem) {
        ESP_LOGE(TAG, "out of PSRAM");
        heap_caps_free(wmem);
        freeAll();
        UiYatzy::showMenu();
        return;
    }
    s_world = new (wmem) Otr::World;
    const int64_t t0 = esp_timer_get_time();
    makeArt(s_spr);
    ESP_LOGI(TAG, "tile art generated in %d ms (%u B sprites, %u B frame)", (int)((esp_timer_get_time() - t0) / 1000),
             (unsigned)sizeof(Sprites), (unsigned)(VIEW_W * VIEW_H * 2));

    s_scr = Theme::screen();
    lv_obj_t *hdr = lv_obj_create(s_scr);
    lv_obj_remove_style_all(hdr);
    lv_obj_set_size(hdr, VIEW_W, VIEW_Y);
    lv_obj_set_style_bg_color(hdr, Theme::c(Theme::PANEL), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(hdr, false);
    lv_obj_t *back = Theme::button(hdr, LV_SYMBOL_LEFT, 44, 36, Theme::MUTED);
    lv_obj_set_pos(back, 4, 4);
    lv_obj_add_event_cb(back, [](lv_event_t *) {
        freeAll();
        UiYatzy::showMenu();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *title = Theme::label(hdr, tr(Str::ADV_TITLE), &my_font_18);
    lv_obj_set_pos(title, 56, 2);
    s_stats = Theme::label(hdr, "", &my_font_14, Theme::GOLD);
    lv_obj_set_pos(s_stats, 56, 24);
    s_perf = Theme::label(hdr, "", &my_font_14, Theme::MUTED);
    lv_obj_align(s_perf, LV_ALIGN_TOP_RIGHT, -54, 4);
    lv_obj_t *restart = Theme::button(hdr, LV_SYMBOL_REFRESH, 44, 36, Theme::LINE);
    lv_obj_align(restart, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_add_event_cb(restart, [](lv_event_t *) {
        static const char *btns[] = {nullptr};
        (void)btns;
        lv_obj_t *mb = lv_msgbox_create(nullptr);
        lv_msgbox_add_text(mb, tr(Str::ADV_RESTART_Q));
        lv_obj_t *yes = lv_msgbox_add_footer_button(mb, tr(Str::ADV_RESTART));
        lv_obj_t *no = lv_msgbox_add_footer_button(mb, tr(Str::BTN_BACK));
        lv_obj_add_event_cb(yes, [](lv_event_t *ev) {
            resetWorld();
            lv_msgbox_close((lv_obj_t *)lv_event_get_user_data(ev));
        }, LV_EVENT_CLICKED, mb);
        lv_obj_add_event_cb(no, [](lv_event_t *ev) { lv_msgbox_close((lv_obj_t *)lv_event_get_user_data(ev)); },
                            LV_EVENT_CLICKED, mb);
        Theme::msgboxFonts(mb);
    }, LV_EVENT_CLICKED, nullptr);

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

    // Controls: arrow pad (left), Slå + Zoom (right)
    constexpr int32_t PW = 62, PH = 36;
    padButton(s_scr, LV_SYMBOL_UP, 4 + PW + 4, CTRL_Y, PW, PH, Dir::UP);
    padButton(s_scr, LV_SYMBOL_LEFT, 4, CTRL_Y + PH + 2, PW, PH, Dir::LEFT);
    padButton(s_scr, LV_SYMBOL_RIGHT, 4 + 2 * (PW + 4), CTRL_Y + PH + 2, PW, PH, Dir::RIGHT);
    padButton(s_scr, LV_SYMBOL_DOWN, 4 + PW + 4, CTRL_Y + 2 * (PH + 2), PW, PH, Dir::DOWN);
    lv_obj_t *hit = Theme::button(s_scr, tr(Str::ADV_HIT), 110, 66, Theme::ACCENT, true);
    lv_obj_set_pos(hit, 206, CTRL_Y);
    lv_obj_add_event_cb(hit, [](lv_event_t *) { doHit(); }, LV_EVENT_PRESSED, nullptr);
    lv_obj_t *zoom = Theme::button(s_scr, tr(Str::ADV_ZOOM), 110, 40, Theme::TEAL);
    lv_obj_set_pos(zoom, 206, CTRL_Y + 70);
    lv_obj_add_event_cb(zoom, [](lv_event_t *) {
        s_ts = s_ts == 32 ? 16 : 32;
        s_moveMs = 0;
        s_rockAnim = false;
        s_fromX = s_world->playerX();
        s_fromY = s_world->playerY();
        s_dirty = true;
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_event_cb(s_scr, [](lv_event_t *) { freeAll(); }, LV_EVENT_DELETE, nullptr);

    resetWorld();
    s_lastCamX = s_lastCamY = -1;
    s_statT0 = nowMs();
    s_renders = s_renderAccUs = 0;
    tick(nullptr);
    s_timer = lv_timer_create(tick, TICK_MS, nullptr);
    Theme::loadScreen(s_scr);
}

}  // namespace UiAdventure
