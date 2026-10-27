// sprite_export.cpp  --  HOST-ONLY tool (not part of firmware/CMake).
//
// Runs the EXACT procedural sprite builders copied verbatim from
// main/ui_adventure.cpp (makeArt + helpers) and dumps every 32x32 sprite as a
// raw RGBA8888 file, plus a manifest.  tools/sprite_png.py then packs the raw
// dumps into transparent PNGs (KEY colour -> alpha 0).
//
// This is a faithful export of what the device draws, NOT a hand-drawn set.
// Build:
//   g++ -std=c++17 -Wall -Wextra tools/sprite_export.cpp -o /tmp/sprite_export
// Run (writes *.rgba + manifest.txt into the given out dir):
//   /tmp/sprite_export <out_dir>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>

// ---- constants mirrored from ui_adventure.cpp -------------------------------
static constexpr int SPR = 32;
static constexpr uint16_t KEY = 0xF81F;        // transparent (magenta)
static constexpr int NPC_LOOKS = 3;
static constexpr int PORTRAIT_PX = 48;

enum class Dir : uint8_t { UP, RIGHT, DOWN, LEFT };

enum {
    S_GRASS0, S_GRASS1, S_SAND, S_FLOOR, S_RUBBLE, S_WALL, S_CRACK0, S_CRACK1, S_TREE,
    S_WATER0, S_WATER1, S_BRIDGE, S_LAVA0, S_LAVA1, S_BASALT, S_PLATE, S_GATE_C, S_GATE_O,
    S_ROCK, S_CRATE, S_KEY, S_DOOR, S_BOAT, S_GEM,
    S_PL_UP, S_PL_RIGHT, S_PL_DOWN, S_PL_LEFT, S_IMPACT,
    S_CHECK, S_CHECK_ON, S_EXIT, S_CRATER, S_FOG, S_FLAG, S_BUSH, S_POTION, S_HEART, S_SUIT, S_LIFE,
    S_MINE, S_NUM1, S_NUM2, S_NUM3, S_NUM4, S_NUM5, S_NUM6, S_NUM7, S_NUM8,
    S_NPC0,
    S_NPC_LAST = S_NPC0 + 3 * 4 - 1,
    S_COUNT
};

struct Sprites {
    uint16_t big[S_COUNT][SPR * SPR];
    uint16_t small[S_COUNT][(SPR / 2) * (SPR / 2)];
    uint16_t portrait[NPC_LOOKS][PORTRAIT_PX * PORTRAIT_PX];
};

// ================= BEGIN verbatim copy from ui_adventure.cpp ==================
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

static void artGrass(uint16_t *s, uint32_t seed)
{
    fillNoise(s, 70, 150, 64, 10, seed);
    for (int i = 0; i < 14; i++) {
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
    for (int i = 0; i < 4; i++) {
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

struct PersonLook {
    uint8_t tr, tg, tb;
    uint8_t hr, hg, hb;
    uint8_t extra;
};
static constexpr PersonLook HERO_LOOK = {255, 150, 58, 92, 58, 34, 0};
static constexpr PersonLook NPC_LOOK[NPC_LOOKS] = {
    {240, 236, 226, 150, 96, 52, 1},
    {104, 176, 86, 214, 170, 86, 2},
    {136, 98, 160, 206, 206, 212, 3},
};

static void artPerson(uint16_t *s, Dir d, const PersonLook &lk)
{
    fillKey(s);
    for (int y = 26; y < 31; y++)
        for (int x = 8; x < 24; x++) {
            const float dx = (x - 15.5f) / 8.f, dy = (y - 28.f) / 2.5f;
            if (dx * dx + dy * dy <= 1.f) px(s, x, y, rgb(20, 40, 24));
        }
    const uint16_t wood = rgb(120, 78, 40);
    const int crookX = d == Dir::LEFT ? 5 : 26;
    if (lk.extra == 2) {
        line(s, crookX, 6, crookX, 29, wood, 2);
        line(s, crookX, 6, crookX + (d == Dir::LEFT ? 3 : -3), 3, wood, 2);
        line(s, crookX + (d == Dir::LEFT ? 3 : -3), 3, crookX + (d == Dir::LEFT ? 5 : -5), 5, wood, 2);
    }
    for (int y = 14; y < 28; y++)
        for (int x = 9; x < 23; x++) {
            const float dx = (x - 15.5f) / 7.f;
            if (y > 16 || dx * dx < 0.6f) px(s, x, y, rgb(lk.tr - (x - 9) * 4, lk.tg - (x - 9) * 3, lk.tb - (x - 9) * (lk.extra ? 2 : 0)));
        }
    rect(s, 9, 22, 22, 23, lk.extra == 1 ? rgb(200, 196, 186) : rgb(90, 56, 30));
    disc(s, 15.5f, 10.f, 6.5f, rgb(244, 200, 160));
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
    if (lk.extra == 1) {
        const uint16_t w = rgb(250, 250, 248), sh = rgb(214, 214, 210);
        disc(s, 12.5f, 2.5f, 3.2f, w);
        disc(s, 18.5f, 2.5f, 3.2f, w);
        disc(s, 15.5f, 1.8f, 3.4f, w);
        rect(s, 10, 3, 21, 5, w);
        rect(s, 10, 5, 21, 5, sh);
    }
    const uint16_t skin = rgb(230, 186, 146);
    if (d == Dir::LEFT) rect(s, 6, 17, 9, 19, skin);
    else if (d == Dir::RIGHT) rect(s, 22, 17, 25, 19, skin);
    else { rect(s, 7, 17, 9, 21, skin); rect(s, 22, 17, 24, 21, skin); }
    if (lk.extra == 2 && d != Dir::UP) rect(s, crookX - 1, 17, crookX + 1, 19, skin);
}

static void artPlayer(uint16_t *s, Dir d) { artPerson(s, d, HERO_LOOK); }

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
    for (int p = 0; p < 4; p++) {
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

    for (int on = 0; on < 2; on++) {
        uint16_t *s = b[on ? S_CHECK_ON : S_CHECK];
        artGrass(s, 3);
        disc(s, 16.f, 22.f, 9.f, rgb(120, 118, 112));
        disc(s, 16.f, 22.f, 6.5f, rgb(164, 160, 150));
        rect(s, 15, 4, 16, 22, rgb(60, 44, 30));
        const uint16_t f = on ? rgb(255, 210, 60) : rgb(200, 200, 210);
        for (int y = 4; y < 12; y++) rect(s, 17, y, 17 + (y < 8 ? y - 3 : 12 - y) * 2, y, f);
    }
    artGrass(b[S_EXIT], 4);
    rect(b[S_EXIT], 3, 2, 28, 30, rgb(96, 92, 100));
    rect(b[S_EXIT], 7, 6, 24, 30, rgb(20, 18, 28));
    for (int i = 0; i < 4; i++) rect(b[S_EXIT], 8 + i * 2, 12 + i * 5, 23 - i * 2, 13 + i * 5, rgb(110 - i * 20, 104 - i * 20, 90 - i * 18));
    rect(b[S_EXIT], 3, 2, 28, 4, rgb(150, 146, 156));
    artGrass(b[S_CRATER], 5);
    disc(b[S_CRATER], 16.f, 17.f, 12.f, rgb(70, 52, 34));
    disc(b[S_CRATER], 16.f, 17.f, 8.5f, rgb(40, 28, 20));
    disc(b[S_CRATER], 14.f, 15.f, 3.f, rgb(24, 18, 14));
    for (int i = 0; i < 6; i++) px(b[S_CRATER], (int)(hash3(i, 1, 9) % 30) + 1, (int)(hash3(i, 2, 9) % 30) + 1, rgb(90, 70, 50));
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
    for (int k = 0; k < 2; k++) {
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
    fillKey(b[S_SUIT]);
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
// ================= END verbatim copy =========================================

// Name table: index must match the S_* enum order above.
static const char *NAMES[S_COUNT] = {
    "grass0", "grass1", "sand", "floor", "rubble", "wall", "crack0", "crack1", "tree",
    "water0", "water1", "bridge", "lava0", "lava1", "basalt", "plate", "gate_closed", "gate_open",
    "rock", "crate", "key", "door", "boat", "gem",
    "player_up", "player_right", "player_down", "player_left", "impact",
    "checkpoint", "checkpoint_on", "exit", "crater", "fog", "flag", "bush", "potion", "heart", "suit", "life",
    "mine", "num1", "num2", "num3", "num4", "num5", "num6", "num7", "num8",
    "npc0_up", "npc0_right", "npc0_down", "npc0_left",
    "npc1_up", "npc1_right", "npc1_down", "npc1_left",
    "npc2_up", "npc2_right", "npc2_down", "npc2_left",
};

static void dumpRGBA(const std::string &path, const uint16_t *src, int w, int h, bool keyed)
{
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path.c_str()); return; }
    for (int i = 0; i < w * h; i++) {
        const uint16_t c = src[i];
        uint8_t a = 255, r, g, b;
        if (keyed && c == KEY) { r = g = b = 0; a = 0; }
        else {
            r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
            g = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
            b = (uint8_t)((c & 0x1F) * 255 / 31);
        }
        uint8_t px[4] = {r, g, b, a};
        fwrite(px, 1, 4, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s <out_dir>\n", argv[0]); return 1; }
    std::string out = argv[1];
    static Sprites sp;
    makeArt(&sp);

    FILE *man = fopen((out + "/manifest.txt").c_str(), "w");
    for (int i = 0; i < S_COUNT; i++) {
        std::string base = NAMES[i];
        dumpRGBA(out + "/" + base + ".rgba", sp.big[i], SPR, SPR, true);
        if (man) fprintf(man, "%s %d %d\n", base.c_str(), SPR, SPR);
    }
    if (man) fclose(man);
    fprintf(stderr, "wrote %d sprites (32x32 RGBA) to %s\n", (int)S_COUNT, out.c_str());

    // ---- Joseph override set (optional 2nd arg) -----------------------------
    // Brown-robe hero drawn with the SAME artPerson builder, so it matches the
    // device look.  These are a starter set Kent can refine by hand.
    if (argc >= 3) {
        std::string jout = argv[2];
        // Brown robe, dark hair, no extra prop; a humble traveller look for Joseph.
        const PersonLook JOSEPH = {150, 96, 48, 70, 46, 24, 0};
        static const char *JNAMES[4] = {"player_up", "player_right", "player_down", "player_left"};
        FILE *jman = fopen((jout + "/manifest.txt").c_str(), "w");
        uint16_t buf[SPR * SPR];
        for (int d = 0; d < 4; d++) {
            artPerson(buf, (Dir)d, JOSEPH);
            dumpRGBA(jout + "/" + JNAMES[d] + ".rgba", buf, SPR, SPR, true);
            if (jman) fprintf(jman, "%s %d %d\n", JNAMES[d], SPR, SPR);
        }
        if (jman) fclose(jman);
        fprintf(stderr, "wrote 4 Joseph override sprites to %s\n", jout.c_str());
    }
    return 0;
}
