// ui_yatzy.cpp - Yatzy / Maxi-Yatzy screens (see header).
#include "ui_yatzy.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "board_config.h"
#include "board_rgb.h"
#include "encoder_input.h"
#include "game_yatzy.h"
#include "ui_charwheel.h"
#include "ui_settings.h"
#include "ui_theme.h"

namespace UiYatzy {

// ----------------------------------------------------------------------------------------
// State shared by all screens
// ----------------------------------------------------------------------------------------
static YatzyGame s_game;
static YatzyVariant s_variant = YatzyVariant::STANDARD;
static YatzyMode s_mode = YatzyMode::NORMAL;
static char s_names[YATZY_MAX_PLAYERS][YATZY_NAME_CAP];
static int s_nameCount = 0;

static uint8_t rngDie(void *) { return (uint8_t)(esp_random() % 6) + 1; }
static int rnd(int lo, int hi) { return lo + (int)(esp_random() % (uint32_t)(hi - lo + 1)); }

static void showPlayers();
static void showGame();
static void showResults();

// ========================================================================================
// Menu: variant choice
// ========================================================================================
struct VariantChoice {
    YatzyVariant v;
    YatzyMode m;
    const char *title;
    const char *sub;
    uint32_t color;
};

static const VariantChoice CHOICES[4] = {
    {YatzyVariant::STANDARD, YatzyMode::NORMAL, "Yatzy", "5 tärningar \xE2\x80\xA2 15 rutor \xE2\x80\xA2 bonus 50", Theme::ACCENT},
    {YatzyVariant::MAXI, YatzyMode::NORMAL, "Maxi-Yatzy", "6 tärningar \xE2\x80\xA2 20 rutor \xE2\x80\xA2 bonus 100", Theme::TEAL},
    {YatzyVariant::STANDARD, YatzyMode::ORDERED, "Yatzy i ordning", "Rutorna fylls uppifrån och ned", Theme::ACCENT},
    {YatzyVariant::MAXI, YatzyMode::ORDERED, "Maxi-Yatzy i ordning", "6 tärningar, uppifrån och ned", Theme::TEAL},
};

static void menuChoiceEvent(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    s_variant = CHOICES[i].v;
    s_mode = CHOICES[i].m;
    showPlayers();
}

void showMenu()
{
    lv_obj_t *scr = Theme::screen();
    lv_group_t *g = Theme::newScreenGroup(Encoder::setGroup);
    Theme::header(scr, "OnTheRoad");

    lv_obj_t *gear = Theme::button(scr, LV_SYMBOL_SETTINGS, 52, 36, Theme::LINE);
    lv_obj_align(gear, LV_ALIGN_TOP_RIGHT, -6, 4);
    lv_obj_add_event_cb(gear, [](lv_event_t *) { UiSettings::show(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *hint = Theme::label(scr, "Välj spel", &otr_font_14, Theme::MUTED);
    lv_obj_set_pos(hint, 14, 52);

    int def = Config::get().defaultVariant;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = lv_btn_create(scr);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 300, 86);
        lv_obj_set_pos(b, 10, 76 + i * 96);
        lv_obj_set_style_radius(b, 14, 0);
        lv_obj_set_style_bg_color(b, Theme::c(Theme::PANEL), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(b, i == def ? 2 : 1, 0);
        lv_obj_set_style_border_color(b, Theme::c(i == def ? Theme::ACCENT : Theme::LINE), 0);
        lv_obj_set_style_bg_color(b, Theme::c(Theme::PANEL_RAISED), LV_STATE_PRESSED);
        lv_obj_set_style_outline_width(b, 2, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_color(b, Theme::c(Theme::TEAL), LV_STATE_FOCUS_KEY);
        // Accent stripe on the left
        lv_obj_t *stripe = lv_obj_create(b);
        lv_obj_remove_style_all(stripe);
        lv_obj_set_size(stripe, 6, 62);
        lv_obj_align(stripe, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_set_style_radius(stripe, 3, 0);
        lv_obj_set_style_bg_color(stripe, Theme::c(CHOICES[i].color), 0);
        lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);
        lv_obj_clear_flag(stripe, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *t = Theme::label(b, CHOICES[i].title, &otr_font_24);
        lv_obj_set_pos(t, 28, 14);
        lv_obj_t *s = Theme::label(b, CHOICES[i].sub, &otr_font_14, Theme::MUTED);
        lv_obj_set_pos(s, 28, 50);
        if (i == def) {
            lv_obj_t *star = Theme::label(b, LV_SYMBOL_OK, &otr_font_18, Theme::ACCENT);
            lv_obj_align(star, LV_ALIGN_TOP_RIGHT, -12, 12);
        }
        lv_obj_add_event_cb(b, menuChoiceEvent, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_group_add_obj(g, b);
    }
    lv_group_add_obj(g, gear);

    lv_obj_t *foot = Theme::label(scr, FW_NAME " v" FW_VERSION, &otr_font_14, Theme::LINE);
    lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -6);
    Theme::loadScreen(scr);
}

// ========================================================================================
// Players
// ========================================================================================
static lv_obj_t *s_playerList = nullptr;
static lv_obj_t *s_addBtn = nullptr;
static lv_obj_t *s_startBtn = nullptr;
static lv_group_t *s_playersGroup = nullptr;
static int s_editIndex = -1;   // -1 = new player

static void rebuildPlayerList();

static void trimInPlace(char *s)
{
    size_t n = strlen(s);
    while (n && s[n - 1] == ' ') s[--n] = '\0';
    size_t lead = strspn(s, " ");
    if (lead) memmove(s, s + lead, n - lead + 1);
}

static void nameEntered(const char *text, bool confirmed, void *user)
{
    lv_obj_t *wheel = (lv_obj_t *)user;
    if (!confirmed) {   // Wheel closes itself on BACK
        Encoder::setGroup(s_playersGroup);
        return;
    }
    char name[YATZY_NAME_CAP];
    snprintf(name, sizeof(name), "%s", text);
    trimInPlace(name);
    if (!name[0]) {
        CharWheel::setHint(wheel, "Namnet får inte vara tomt");
        return;
    }
    if (s_editIndex < 0 && s_nameCount < YATZY_MAX_PLAYERS) {
        snprintf(s_names[s_nameCount++], YATZY_NAME_CAP, "%s", name);
    } else if (s_editIndex >= 0 && s_editIndex < s_nameCount) {
        snprintf(s_names[s_editIndex], YATZY_NAME_CAP, "%s", name);
    }
    CharWheel::close(wheel);
    Encoder::setGroup(s_playersGroup);
    rebuildPlayerList();
}

static void openNameWheel(int index)
{
    s_editIndex = index;
    char label[40], initial[YATZY_NAME_CAP];
    if (index < 0) {
        snprintf(label, sizeof(label), "Namn på spelare %d", s_nameCount + 1);
        snprintf(initial, sizeof(initial), "Spelare %d", s_nameCount + 1);
    } else {
        snprintf(label, sizeof(label), "Byt namn på spelare %d", index + 1);
        snprintf(initial, sizeof(initial), "%s", s_names[index]);
    }
    // The callback needs the wheel object; create first, then patch the user pointer via a
    // static trampoline (the wheel passes `user` back unchanged).
    static lv_obj_t *s_wheel = nullptr;
    s_wheel = CharWheel::create(lv_scr_act(), 0, 0, 320, 480, label, initial, YATZY_NAME_CAP - 1, false,
                                [](const char *t, bool ok, void *) { nameEntered(t, ok, s_wheel); }, nullptr);
}

static void playerRowEvent(lv_event_t *e)
{
    openNameWheel((int)(intptr_t)lv_event_get_user_data(e));
}

static void playerDeleteEvent(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= s_nameCount) return;
    for (int k = i; k < s_nameCount - 1; k++) memcpy(s_names[k], s_names[k + 1], YATZY_NAME_CAP);
    s_nameCount--;
    rebuildPlayerList();
}

static void rebuildPlayerList()
{
    lv_obj_clean(s_playerList);
    for (int i = 0; i < s_nameCount; i++) {
        lv_obj_t *row = lv_btn_create(s_playerList);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 240, 34);
        lv_obj_set_pos(row, 0, i * 38);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_bg_color(row, Theme::c(Theme::PANEL_RAISED), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, Theme::c(Theme::ACCENT_DARK), LV_STATE_PRESSED);
        char num[8];
        snprintf(num, sizeof(num), "%d", i + 1);
        lv_obj_t *n = Theme::label(row, num, &otr_font_18, Theme::ACCENT);
        lv_obj_align(n, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_t *name = Theme::label(row, s_names[i], &otr_font_18);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, 170);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 36, 0);
        lv_obj_t *pen = Theme::label(row, LV_SYMBOL_EDIT, &otr_font_14, Theme::MUTED);
        lv_obj_align(pen, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_add_event_cb(row, playerRowEvent, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *del = Theme::button(s_playerList, LV_SYMBOL_TRASH, 44, 34, Theme::RED);
        lv_obj_set_pos(del, 248, i * 38);
        lv_obj_add_event_cb(del, playerDeleteEvent, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    if (s_nameCount == 0) {
        lv_obj_t *empty = Theme::label(s_playerList, "Lägg till minst en spelare", &otr_font_14, Theme::MUTED);
        lv_obj_align(empty, LV_ALIGN_TOP_MID, 0, 20);
    }
    if (s_nameCount >= YATZY_MAX_PLAYERS) lv_obj_add_state(s_addBtn, LV_STATE_DISABLED);
    else lv_obj_clear_state(s_addBtn, LV_STATE_DISABLED);
    if (s_nameCount > 0) lv_obj_clear_state(s_startBtn, LV_STATE_DISABLED);
    else lv_obj_add_state(s_startBtn, LV_STATE_DISABLED);
}

static void showPlayers()
{
    if (s_nameCount == 0) {
        snprintf(s_names[0], YATZY_NAME_CAP, "Spelare 1");
        snprintf(s_names[1], YATZY_NAME_CAP, "Spelare 2");
        s_nameCount = 2;
    }
    lv_obj_t *scr = Theme::screen();
    s_playersGroup = Theme::newScreenGroup(Encoder::setGroup);
    Theme::header(scr, "Spelare");
    lv_obj_t *var = Theme::label(scr, Yatzy::variantName(s_variant, s_mode), &otr_font_14, Theme::TEAL);
    lv_obj_align(var, LV_ALIGN_TOP_RIGHT, -12, 14);

    lv_obj_t *card = Theme::card(scr, 310, 326);
    lv_obj_set_pos(card, 5, 52);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    s_playerList = lv_obj_create(card);
    lv_obj_remove_style_all(s_playerList);
    lv_obj_set_size(s_playerList, 292, 306);
    lv_obj_clear_flag(s_playerList, LV_OBJ_FLAG_SCROLLABLE);

    s_addBtn = Theme::button(scr, LV_SYMBOL_PLUS " Lägg till spelare", 300, 42, Theme::TEAL);
    lv_obj_set_pos(s_addBtn, 10, 384);
    lv_obj_add_event_cb(s_addBtn, [](lv_event_t *) { openNameWheel(-1); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *back = Theme::button(scr, LV_SYMBOL_LEFT " Tillbaka", 140, 42, Theme::MUTED);
    lv_obj_set_pos(back, 10, 432);
    lv_obj_add_event_cb(back, [](lv_event_t *) { showMenu(); }, LV_EVENT_CLICKED, nullptr);

    s_startBtn = Theme::button(scr, "Starta " LV_SYMBOL_RIGHT, 150, 42, Theme::ACCENT, true);
    lv_obj_set_pos(s_startBtn, 160, 432);
    lv_obj_add_event_cb(s_startBtn, [](lv_event_t *) {
        const AppConfig &cfg = Config::get();
        s_game.reset(s_variant, s_mode, cfg.maxiSavedRolls);
        s_game.setRng(rngDie, nullptr);
        for (int i = 0; i < s_nameCount; i++) s_game.addPlayer(s_names[i]);
        s_game.start();
        showGame();
    }, LV_EVENT_CLICKED, nullptr);

    lv_group_add_obj(s_playersGroup, s_startBtn);
    lv_group_add_obj(s_playersGroup, s_addBtn);
    lv_group_add_obj(s_playersGroup, back);
    rebuildPlayerList();
    Theme::loadScreen(scr);
}

// ========================================================================================
// Game screen
// ========================================================================================
static constexpr lv_coord_t SCR_W = 320;
static constexpr lv_coord_t HEADER_H = 44;
static constexpr lv_coord_t THROW_Y = 48, THROW_H = 116;
static constexpr lv_coord_t NEST_Y = 168, NEST_H = 70;
static constexpr lv_coord_t ACTION_Y = 244, ACTION_H = 50;
static constexpr lv_coord_t TABLE_Y = 300;
static constexpr lv_coord_t CAT_COL_W = 112;
static constexpr uint32_t MOVE_MS = 260;

enum RowKind : uint8_t { ROW_HEADER, ROW_CAT, ROW_SUM, ROW_BONUS, ROW_TOTAL };
struct RowInfo {
    RowKind kind;
    YatzyCategory cat;
};

struct DieUi {
    lv_obj_t *obj;
    lv_obj_t *pips[9];
    lv_coord_t jx, jy;       // Random scatter in the throw area (changes every roll)
    int lastStep;
    lv_coord_t totX, totY;
    bool dragging;
};

static DieUi s_dice[YATZY_MAX_DICE];
static lv_coord_t s_dieSize = 50;
static lv_obj_t *s_playerLabel, *s_roundLabel, *s_rollBtn, *s_rollLabel, *s_writeBtn, *s_writeLabel, *s_table;
static lv_obj_t *s_throwArea;
static RowInfo s_rows[1 + YATZY_CATEGORY_COUNT + 3];
static int s_rowCount = 0;
static int s_rolling = 0;
static YatzyCategory s_selected = YatzyCategory::COUNT;
static YatzyCategory s_best = YatzyCategory::COUNT;

// Pip cells on a 3x3 grid (index = row * 3 + col) for faces 1..6.
static const uint16_t FACE_PIPS[7] = {
    0,
    1 << 4,
    (1 << 2) | (1 << 6),
    (1 << 2) | (1 << 4) | (1 << 6),
    (1 << 0) | (1 << 2) | (1 << 6) | (1 << 8),
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 8),
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8),
};

static void setFace(DieUi &d, int face)
{
    uint16_t mask = FACE_PIPS[face >= 1 && face <= 6 ? face : 0];
    for (int i = 0; i < 9; i++) {
        if (mask & (1 << i)) lv_obj_clear_flag(d.pips[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(d.pips[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_coord_t slotX(int i)
{
    int n = s_game.diceCount();
    return (lv_coord_t)((2 * i + 1) * SCR_W / (2 * n)) - s_dieSize / 2;
}

static void targetPos(int i, lv_coord_t &x, lv_coord_t &y)
{
    if (s_game.held(i)) {
        x = slotX(i);
        y = NEST_Y + (NEST_H - s_dieSize) / 2;
    } else {
        x = slotX(i) + s_dice[i].jx;
        y = THROW_Y + (THROW_H - s_dieSize) / 2 + s_dice[i].jy;
    }
}

static void animXY(lv_obj_t *obj, lv_coord_t x, lv_coord_t y, uint32_t ms)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_time(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_set_values(&a, lv_obj_get_x(obj), x);
    lv_anim_start(&a);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_values(&a, lv_obj_get_y(obj), y);
    lv_anim_start(&a);
}

// Visual state: held dice get a gold rim and sit in the darker nest; free dice glow brighter.
static void styleDie(int i)
{
    lv_obj_t *o = s_dice[i].obj;
    bool rolled = s_game.hasRolled();
    bool held = s_game.held(i);
    lv_obj_set_style_border_width(o, held ? 3 : 0, 0);
    lv_obj_set_style_border_color(o, Theme::c(Theme::GOLD), 0);
    lv_obj_set_style_shadow_width(o, held ? 6 : 18, 0);
    lv_obj_set_style_shadow_color(o, Theme::c(held ? 0x000000 : Theme::TEAL), 0);
    lv_obj_set_style_shadow_opa(o, rolled ? (held ? LV_OPA_60 : LV_OPA_50) : LV_OPA_0, 0);
    lv_obj_set_style_bg_opa(o, rolled ? (held ? LV_OPA_80 : LV_OPA_COVER) : LV_OPA_30, 0);
}

static void moveDie(int i, bool animate)
{
    lv_coord_t x, y;
    targetPos(i, x, y);
    styleDie(i);
    if (animate) animXY(s_dice[i].obj, x, y, MOVE_MS);
    else lv_obj_set_pos(s_dice[i].obj, x, y);
}

static void refreshTable();
static void refreshActions();

static int rowOfCategory(YatzyCategory c)
{
    for (int r = 0; r < s_rowCount; r++) if (s_rows[r].kind == ROW_CAT && s_rows[r].cat == c) return r;
    return -1;
}

static void dieEvent(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    DieUi &d = s_dice[i];
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (code == LV_EVENT_PRESSED) {
        d.dragging = !s_rolling && s_game.hasRolled() && !s_game.finished();
        d.totX = d.totY = 0;
        if (d.dragging) {
            lv_anim_del(d.obj, (lv_anim_exec_xcb_t)lv_obj_set_x);
            lv_anim_del(d.obj, (lv_anim_exec_xcb_t)lv_obj_set_y);
            lv_obj_move_foreground(d.obj);
        }
    } else if (code == LV_EVENT_PRESSING && d.dragging && indev) {
        lv_point_t v;
        lv_indev_get_vect(indev, &v);
        d.totX += v.x;
        d.totY += v.y;
        // The die follows the finger (light drag-and-drop).
        lv_coord_t x = LV_CLAMP(0, lv_obj_get_x(d.obj) + v.x, SCR_W - s_dieSize);
        lv_coord_t y = LV_CLAMP(THROW_Y, lv_obj_get_y(d.obj) + v.y, NEST_Y + NEST_H - s_dieSize);
        lv_obj_set_pos(d.obj, x, y);
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && d.dragging) {
        d.dragging = false;
        bool held = s_game.held(i);
        bool tap = LV_ABS(d.totX) < 10 && LV_ABS(d.totY) < 10;
        // Dropped inside the nest / throw area, or swiped towards it.
        lv_coord_t centerY = lv_obj_get_y(d.obj) + s_dieSize / 2;
        bool wantHold = held;
        if (tap) wantHold = !held;
        else if (!held && (d.totY > 25 || centerY >= NEST_Y)) wantHold = true;
        else if (held && (d.totY < -25 || centerY < NEST_Y)) wantHold = false;
        if (wantHold != held) s_game.setHold(i, wantHold);
        moveDie(i, true);
        refreshActions();
    }
}

static void createDie(int i)
{
    DieUi &d = s_dice[i];
    d.obj = lv_obj_create(lv_scr_act() == nullptr ? nullptr : lv_obj_get_parent(s_throwArea));
    lv_obj_t *o = d.obj;
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, s_dieSize, s_dieSize);
    lv_obj_set_style_radius(o, s_dieSize / 5, 0);
    lv_obj_set_style_bg_color(o, Theme::c(Theme::DIE_FACE), 0);
    lv_obj_set_style_bg_grad_color(o, Theme::c(0xD9D4C7), 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_spread(o, 1, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_ext_click_area(o, 4);
    lv_coord_t pip = s_dieSize / 5 - 1;
    lv_coord_t off = s_dieSize * 27 / 100;
    for (int k = 0; k < 9; k++) {
        lv_obj_t *p = lv_obj_create(o);
        lv_obj_remove_style_all(p);
        lv_obj_set_size(p, pip, pip);
        lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(p, Theme::c(Theme::DIE_PIP), 0);
        lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
        lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(p, LV_ALIGN_CENTER, (k % 3 - 1) * off, (k / 3 - 1) * off);
        d.pips[k] = p;
    }
    d.jx = d.jy = 0;
    d.dragging = false;
    setFace(d, 0);
    lv_obj_add_event_cb(o, dieEvent, LV_EVENT_PRESSED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(o, dieEvent, LV_EVENT_PRESSING, (void *)(intptr_t)i);
    lv_obj_add_event_cb(o, dieEvent, LV_EVENT_RELEASED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(o, dieEvent, LV_EVENT_PRESS_LOST, (void *)(intptr_t)i);
}

// --- Banner ("YATZY!") ----------------------------------------------------------------
static void opaExec(void *obj, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0); }

static void showBanner(const char *text, uint32_t color)
{
    lv_obj_t *l = Theme::label(s_throwArea, text, &otr_font_36, color);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 4);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, l);
    lv_anim_set_exec_cb(&a, opaExec);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_delay(&a, 1200);
    lv_anim_set_time(&a, 600);
    lv_anim_set_ready_cb(&a, [](lv_anim_t *an) { lv_obj_del_async((lv_obj_t *)an->var); });
    lv_anim_start(&a);
}

// --- Rolling ----------------------------------------------------------------------------
static void afterRoll()
{
    YatzySuggestion sug[YATZY_CATEGORY_COUNT];
    int n = s_game.getSuggestedCategories(sug, YATZY_CATEGORY_COUNT);
    s_best = n > 0 ? sug[0].category : YatzyCategory::COUNT;
    s_selected = s_best;
    if (s_game.potentialScore(YatzyCategory::YATZY) > 0) {
        showBanner(s_game.variant() == YatzyVariant::MAXI ? "MAXI YATZY!" : "YATZY!", Theme::GOLD);
        Rgb::effectFlash(255, 190, 40, 1500);
    }
    refreshTable();
    refreshActions();
    // Bring the suggested row into view.
    int r = rowOfCategory(s_best);
    if (r > 0) {
        lv_coord_t rowH = 26;
        lv_coord_t target = (r - 2) * rowH;
        lv_obj_scroll_to_y(s_table, LV_MAX(0, target), LV_ANIM_ON);
    }
}

static void rollExec(void *var, int32_t v)
{
    DieUi &d = *(DieUi *)var;
    int i = (int)(&d - s_dice);
    int step = v / 80;                          // ~13 face changes, spaced out by ease-out
    if (step != d.lastStep) {
        d.lastStep = step;
        setFace(d, rnd(1, 6));
        lv_coord_t x, y;
        targetPos(i, x, y);
        lv_coord_t amp = (lv_coord_t)(16 * (1024 - v) / 1024);
        lv_obj_set_pos(d.obj, x + rnd(-amp, amp), y + rnd(-amp, amp));
    }
}

static void rollReady(lv_anim_t *a)
{
    DieUi &d = *(DieUi *)a->var;
    int i = (int)(&d - s_dice);
    setFace(d, s_game.die(i));
    moveDie(i, false);
    if (--s_rolling <= 0) {
        s_rolling = 0;
        afterRoll();
    }
}

static void onRoll(lv_event_t *)
{
    if (s_rolling || !s_game.canRoll()) return;
    bool first = !s_game.hasRolled();
    bool rollingMask[YATZY_MAX_DICE];
    for (int i = 0; i < s_game.diceCount(); i++) rollingMask[i] = first || !s_game.held(i);
    if (!s_game.roll()) return;
    s_selected = s_best = YatzyCategory::COUNT;
    uint32_t ms = Config::get().rollAnimMs;
    Rgb::effectDiceRoll(ms + 200);
    for (int i = 0; i < s_game.diceCount(); i++) {
        if (!rollingMask[i]) continue;
        DieUi &d = s_dice[i];
        d.jx = (lv_coord_t)rnd(-8, 8);
        d.jy = (lv_coord_t)rnd(-22, 22);
        d.lastStep = -1;
        styleDie(i);
        lv_anim_del(d.obj, (lv_anim_exec_xcb_t)lv_obj_set_x);
        lv_anim_del(d.obj, (lv_anim_exec_xcb_t)lv_obj_set_y);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, &d);
        lv_anim_set_exec_cb(&a, rollExec);
        lv_anim_set_values(&a, 0, 1024);
        lv_anim_set_time(&a, ms + i * 70);    // Dice settle one after another
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_ready_cb(&a, rollReady);
        lv_anim_start(&a);
        s_rolling++;
    }
    refreshTable();
    refreshActions();
}

// --- Writing a score --------------------------------------------------------------------
static void newTurnUi()
{
    for (int i = 0; i < s_game.diceCount(); i++) {
        s_dice[i].jx = s_dice[i].jy = 0;
        setFace(s_dice[i], 0);
        moveDie(i, true);
    }
    s_selected = s_best = YatzyCategory::COUNT;
    const YatzyPlayer &p = s_game.player(s_game.currentPlayer());
    lv_label_set_text(s_playerLabel, p.name);
    lv_label_set_text_fmt(s_roundLabel, "Runda %d/%d", s_game.round(), s_game.totalRounds());
    refreshTable();
    refreshActions();
    // Scroll the current player's column into view.
    lv_coord_t colW = lv_table_get_col_width(s_table, 1);
    lv_coord_t x = CAT_COL_W + s_game.currentPlayer() * colW;
    lv_coord_t visible = lv_obj_get_content_width(s_table);
    lv_coord_t target = x + colW > visible ? x + colW - visible : 0;
    lv_obj_scroll_to_x(s_table, target, LV_ANIM_ON);
    lv_obj_scroll_to_y(s_table, 0, LV_ANIM_ON);
}

static void writeSelected()
{
    if (s_rolling || s_selected == YatzyCategory::COUNT || !s_game.canScore(s_selected)) return;
    int pts = s_game.potentialScore(s_selected);
    s_game.score(s_selected);
    if (pts > 0) Rgb::effectFlash(40, 255, 90, 250);
    else Rgb::effectFlash(255, 60, 40, 250);
    if (s_game.finished()) {
        showResults();
        return;
    }
    newTurnUi();
}

static void onWrite(lv_event_t *) { writeSelected(); }

// --- Score table ------------------------------------------------------------------------
static void buildRows()
{
    int n;
    const YatzyCategory *cats = Yatzy::categories(s_game.variant(), n);
    s_rowCount = 0;
    s_rows[s_rowCount++] = {ROW_HEADER, YatzyCategory::COUNT};
    bool upperDone = false;
    for (int i = 0; i < n; i++) {
        if (!Yatzy::isUpper(cats[i]) && !upperDone) {
            s_rows[s_rowCount++] = {ROW_SUM, YatzyCategory::COUNT};
            s_rows[s_rowCount++] = {ROW_BONUS, YatzyCategory::COUNT};
            upperDone = true;
        }
        s_rows[s_rowCount++] = {ROW_CAT, cats[i]};
    }
    s_rows[s_rowCount++] = {ROW_TOTAL, YatzyCategory::COUNT};
}

static bool upperComplete(int p)
{
    int n;
    const YatzyCategory *cats = Yatzy::categories(s_game.variant(), n);
    for (int i = 0; i < n; i++)
        if (Yatzy::isUpper(cats[i]) && s_game.player(p).scores[(int)cats[i]] == YATZY_EMPTY) return false;
    return true;
}

static void refreshTable()
{
    int players = s_game.playerCount();
    int cur = s_game.currentPlayer();
    char buf[16];
    for (int r = 0; r < s_rowCount; r++) {
        const RowInfo &ri = s_rows[r];
        switch (ri.kind) {
        case ROW_HEADER: lv_table_set_cell_value(s_table, r, 0, s_game.variant() == YatzyVariant::MAXI ? "Maxi" : "Yatzy"); break;
        case ROW_CAT: lv_table_set_cell_value(s_table, r, 0, Yatzy::categoryName(ri.cat, s_game.variant())); break;
        case ROW_SUM: lv_table_set_cell_value(s_table, r, 0, "Summa"); break;
        case ROW_BONUS: lv_table_set_cell_value(s_table, r, 0, "Bonus"); break;
        case ROW_TOTAL: lv_table_set_cell_value(s_table, r, 0, "Totalt"); break;
        }
        for (int p = 0; p < players; p++) {
            const YatzyPlayer &pl = s_game.player(p);
            buf[0] = '\0';
            switch (ri.kind) {
            case ROW_HEADER: snprintf(buf, sizeof(buf), "%.10s", pl.name); break;
            case ROW_CAT: {
                int16_t v = pl.scores[(int)ri.cat];
                if (v != YATZY_EMPTY) snprintf(buf, sizeof(buf), "%d", v);
                else if (p == cur && s_game.hasRolled() && !s_rolling && s_game.canScore(ri.cat))
                    snprintf(buf, sizeof(buf), "%d", s_game.potentialScore(ri.cat));
                break;
            }
            case ROW_SUM: snprintf(buf, sizeof(buf), "%d", s_game.upperSum(p)); break;
            case ROW_BONUS: {
                int b = s_game.bonus(p);
                if (b > 0 || upperComplete(p)) snprintf(buf, sizeof(buf), "%d", b);
                else snprintf(buf, sizeof(buf), "-%d", Yatzy::bonusThreshold(s_game.variant()) - s_game.upperSum(p));
                break;
            }
            case ROW_TOTAL: snprintf(buf, sizeof(buf), "%d", s_game.total(p)); break;
            }
            lv_table_set_cell_value(s_table, r, p + 1, buf);
        }
    }
    lv_obj_invalidate(s_table);
}

static void tableDrawEvent(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part != LV_PART_ITEMS || !dsc->rect_dsc || !dsc->label_dsc) return;
    uint16_t cols = lv_table_get_col_cnt(s_table);
    uint32_t row = dsc->id / cols, col = dsc->id % cols;
    if (row >= (uint32_t)s_rowCount) return;
    const RowInfo &ri = s_rows[row];
    int cur = s_game.currentPlayer();
    bool curCol = (int)col == cur + 1;
    uint32_t bg = curCol ? 0x1A2C44 : Theme::PANEL;
    uint32_t fg = col == 0 ? Theme::MUTED : Theme::TEXT;
    if (ri.kind == ROW_HEADER) {
        bg = Theme::PANEL_RAISED;
        fg = curCol ? Theme::ACCENT : Theme::MUTED;
    } else if (ri.kind != ROW_CAT) {
        bg = Theme::PANEL_RAISED;
        fg = ri.kind == ROW_TOTAL ? Theme::GOLD : Theme::TEAL;
    } else if (curCol && s_game.player(cur).scores[(int)ri.cat] == YATZY_EMPTY) {
        bool avail = s_game.hasRolled() && !s_rolling && s_game.canScore(ri.cat);
        if (avail) fg = 0x6FB7C6;                     // Potential score (not yet written)
        if (avail && ri.cat == s_best) { bg = 0x4D3F10; fg = Theme::GOLD; }
        if (avail && ri.cat == s_selected) { bg = Theme::ACCENT; fg = Theme::BG; }
    } else if (col == 0 && s_game.mode() == YatzyMode::ORDERED &&
               ri.cat == s_game.nextOrderedCategory(cur)) {
        fg = Theme::ACCENT;                           // Next row in "i ordning"
    }
    dsc->rect_dsc->bg_color = Theme::c(bg);
    dsc->rect_dsc->bg_opa = LV_OPA_COVER;
    dsc->label_dsc->color = Theme::c(fg);
}

static void tableClickEvent(lv_event_t *)
{
    uint16_t row, col;
    lv_table_get_selected_cell(s_table, &row, &col);
    if (row == LV_TABLE_CELL_NONE || row >= s_rowCount || s_rows[row].kind != ROW_CAT || s_rolling) return;
    YatzyCategory c = s_rows[row].cat;
    if (!s_game.hasRolled() || !s_game.canScore(c)) return;
    if (c == s_selected) {   // Second tap on the selected row writes it
        writeSelected();
        return;
    }
    s_selected = c;
    refreshActions();
    lv_obj_invalidate(s_table);
}

static void refreshActions()
{
    int left = s_game.rollsLeft();
    bool canRoll = !s_rolling && s_game.canRoll();
    if (s_game.variant() == YatzyVariant::MAXI && s_game.savedRollsRule() && left > YATZY_ROLLS_PER_TURN - s_game.rollsUsed())
        lv_label_set_text_fmt(s_rollLabel, LV_SYMBOL_REFRESH " Kasta  %d (+%d)", YATZY_ROLLS_PER_TURN - LV_MIN(s_game.rollsUsed(), YATZY_ROLLS_PER_TURN),
                              left - (YATZY_ROLLS_PER_TURN - LV_MIN(s_game.rollsUsed(), YATZY_ROLLS_PER_TURN)));
    else
        lv_label_set_text_fmt(s_rollLabel, LV_SYMBOL_REFRESH " Kasta  %d", left);
    if (canRoll) lv_obj_clear_state(s_rollBtn, LV_STATE_DISABLED);
    else lv_obj_add_state(s_rollBtn, LV_STATE_DISABLED);

    bool canWrite = !s_rolling && s_selected != YatzyCategory::COUNT && s_game.canScore(s_selected);
    if (canWrite) {
        lv_label_set_text_fmt(s_writeLabel, "Skriv %d p\n%s", s_game.potentialScore(s_selected),
                              Yatzy::categoryName(s_selected, s_game.variant()));
        lv_obj_clear_state(s_writeBtn, LV_STATE_DISABLED);
    } else {
        lv_label_set_text(s_writeLabel, s_game.hasRolled() ? "Välj ruta\ni tabellen" : "Skriv");
        lv_obj_add_state(s_writeBtn, LV_STATE_DISABLED);
    }
}

static void quitEvent(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_current_target(e);
    const char *txt = lv_msgbox_get_active_btn_text(mbox);
    bool quit = txt && strcmp(txt, "Avsluta") == 0;
    lv_msgbox_close(mbox);
    if (quit) showMenu();
}

static void onMenu(lv_event_t *)
{
    static const char *btns[] = {"Avsluta", "Fortsätt", ""};
    lv_obj_t *m = lv_msgbox_create(nullptr, "Avsluta spelet?", "Poängen för den här omgången försvinner.", btns, false);
    lv_obj_set_width(m, 280);
    lv_obj_set_style_bg_color(m, Theme::c(Theme::PANEL_RAISED), 0);
    lv_obj_set_style_text_color(m, Theme::c(Theme::TEXT), 0);
    lv_obj_set_style_border_color(m, Theme::c(Theme::ACCENT), 0);
    lv_obj_set_style_border_width(m, 2, 0);
    lv_obj_center(m);
    lv_obj_add_event_cb(m, quitEvent, LV_EVENT_VALUE_CHANGED, nullptr);
}

static void showGame()
{
    s_rolling = 0;
    s_selected = s_best = YatzyCategory::COUNT;
    s_dieSize = s_game.diceCount() == 6 ? 44 : 50;

    lv_obj_t *scr = Theme::screen();
    lv_group_t *g = Theme::newScreenGroup(Encoder::setGroup);

    // Header: player, round, menu
    lv_obj_t *h = Theme::header(scr, "");
    lv_obj_clean(h);
    s_playerLabel = Theme::label(h, "", &otr_font_24, Theme::ACCENT);
    lv_label_set_long_mode(s_playerLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_playerLabel, 170);
    lv_obj_align(s_playerLabel, LV_ALIGN_LEFT_MID, 10, 0);
    s_roundLabel = Theme::label(h, "", &otr_font_14, Theme::MUTED);
    lv_obj_align(s_roundLabel, LV_ALIGN_RIGHT_MID, -62, 0);
    lv_obj_t *menu = Theme::button(h, LV_SYMBOL_LIST, 48, 34, Theme::LINE);
    lv_obj_align(menu, LV_ALIGN_RIGHT_MID, -6, 0);
    lv_obj_add_event_cb(menu, onMenu, LV_EVENT_CLICKED, nullptr);

    // Throw area ("kastruta"): felt-like panel
    s_throwArea = lv_obj_create(scr);
    lv_obj_remove_style_all(s_throwArea);
    lv_obj_set_pos(s_throwArea, 4, THROW_Y);
    lv_obj_set_size(s_throwArea, SCR_W - 8, THROW_H);
    lv_obj_set_style_radius(s_throwArea, 14, 0);
    lv_obj_set_style_bg_color(s_throwArea, Theme::c(0x123A3F), 0);
    lv_obj_set_style_bg_grad_color(s_throwArea, Theme::c(0x0E2730), 0);
    lv_obj_set_style_bg_grad_dir(s_throwArea, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_throwArea, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_throwArea, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Nest ("boet"): darker, framed, inner shadow look
    lv_obj_t *nest = lv_obj_create(scr);
    lv_obj_remove_style_all(nest);
    lv_obj_set_pos(nest, 4, NEST_Y);
    lv_obj_set_size(nest, SCR_W - 8, NEST_H);
    lv_obj_set_style_radius(nest, 14, 0);
    lv_obj_set_style_bg_color(nest, Theme::c(Theme::NEST_BG), 0);
    lv_obj_set_style_bg_opa(nest, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(nest, 2, 0);
    lv_obj_set_style_border_color(nest, Theme::c(0x6B5418), 0);
    lv_obj_clear_flag(nest, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *nestLbl = Theme::label(nest, "BOET \xE2\x80\x93 sparade tärningar", &otr_font_14, 0x6B5418);
    lv_obj_align(nestLbl, LV_ALIGN_BOTTOM_MID, 0, -2);

    // Actions
    s_rollBtn = Theme::button(scr, "", 150, ACTION_H, Theme::ACCENT, true);
    lv_obj_set_pos(s_rollBtn, 6, ACTION_Y);
    s_rollLabel = lv_obj_get_child(s_rollBtn, 0);
    lv_obj_add_event_cb(s_rollBtn, onRoll, LV_EVENT_CLICKED, nullptr);
    s_writeBtn = Theme::button(scr, "", 152, ACTION_H, Theme::TEAL);
    lv_obj_set_pos(s_writeBtn, 162, ACTION_Y);
    s_writeLabel = lv_obj_get_child(s_writeBtn, 0);
    lv_obj_set_style_text_font(s_writeLabel, &otr_font_14, 0);
    lv_obj_set_style_text_align(s_writeLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(s_writeBtn, onWrite, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(g, s_rollBtn);
    lv_group_add_obj(g, s_writeBtn);
    lv_group_add_obj(g, menu);

    // Score table
    buildRows();
    int players = s_game.playerCount();
    s_table = lv_table_create(scr);
    lv_obj_set_pos(s_table, 0, TABLE_Y);
    lv_obj_set_size(s_table, SCR_W, 480 - TABLE_Y);
    lv_table_set_col_cnt(s_table, players + 1);
    lv_table_set_row_cnt(s_table, s_rowCount);
    lv_coord_t colW = LV_MAX(52, (SCR_W - CAT_COL_W - 4) / players);
    if (colW > 100) colW = 100;
    lv_table_set_col_width(s_table, 0, CAT_COL_W);
    for (int p = 0; p < players; p++) lv_table_set_col_width(s_table, p + 1, colW);
    lv_obj_set_style_bg_color(s_table, Theme::c(Theme::BG), 0);
    lv_obj_set_style_border_width(s_table, 0, 0);
    lv_obj_set_style_pad_all(s_table, 0, 0);
    lv_obj_set_style_text_font(s_table, &otr_font_14, LV_PART_ITEMS);
    lv_obj_set_style_pad_top(s_table, 4, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(s_table, 4, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(s_table, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(s_table, 4, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_table, Theme::c(Theme::LINE), LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_table, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_side(s_table, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    for (int c = 0; c <= players; c++) lv_table_add_cell_ctrl(s_table, 0, c, LV_TABLE_CELL_CTRL_TEXT_CROP);
    lv_obj_add_event_cb(s_table, tableDrawEvent, LV_EVENT_DRAW_PART_BEGIN, nullptr);
    lv_obj_add_event_cb(s_table, tableClickEvent, LV_EVENT_VALUE_CHANGED, nullptr);

    // Dice (children of the screen so they can travel between the areas)
    for (int i = 0; i < YATZY_MAX_DICE; i++) s_dice[i].obj = nullptr;
    for (int i = 0; i < s_game.diceCount(); i++) {
        createDie(i);
        moveDie(i, false);
    }

    Theme::loadScreen(scr);
    newTurnUi();
}

// ========================================================================================
// Results
// ========================================================================================
static void showResults()
{
    lv_obj_t *scr = Theme::screen();
    lv_group_t *g = Theme::newScreenGroup(Encoder::setGroup);
    Theme::header(scr, "Resultat");
    lv_obj_t *var = Theme::label(scr, Yatzy::variantName(s_game.variant(), s_game.mode()), &otr_font_14, Theme::TEAL);
    lv_obj_align(var, LV_ALIGN_TOP_RIGHT, -12, 14);

    int order[YATZY_MAX_PLAYERS], places[YATZY_MAX_PLAYERS];
    int n = s_game.ranking(order, places);
    lv_obj_t *card = Theme::card(scr, 310, 360);
    lv_obj_set_pos(card, 5, 54);
    static const uint32_t MEDAL[3] = {Theme::GOLD, 0xC9D3DE, 0xD08A4A};
    for (int i = 0; i < n; i++) {
        const YatzyPlayer &p = s_game.player(order[i]);
        lv_coord_t y = i * 42;
        lv_obj_t *badge = lv_obj_create(card);
        lv_obj_remove_style_all(badge);
        lv_obj_set_size(badge, 32, 32);
        lv_obj_set_pos(badge, 0, y);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(badge, Theme::c(places[i] <= 3 ? MEDAL[places[i] - 1] : Theme::PANEL_RAISED), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        char num[6];
        snprintf(num, sizeof(num), "%d", places[i]);
        lv_obj_t *nl = Theme::label(badge, num, &otr_font_18, places[i] <= 3 ? Theme::BG : Theme::TEXT);
        lv_obj_center(nl);
        lv_obj_t *name = Theme::label(card, p.name, &otr_font_18, places[i] == 1 ? Theme::GOLD : Theme::TEXT);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, 170);
        lv_obj_set_pos(name, 42, y + 5);
        char pts[12];
        snprintf(pts, sizeof(pts), "%d", s_game.total(order[i]));
        lv_obj_t *pl = Theme::label(card, pts, &otr_font_24);
        lv_obj_align(pl, LV_ALIGN_TOP_RIGHT, 0, y + 2);
    }

    lv_obj_t *again = Theme::button(scr, LV_SYMBOL_REFRESH " Spela igen", 150, 46, Theme::ACCENT, true);
    lv_obj_set_pos(again, 160, 424);
    lv_obj_add_event_cb(again, [](lv_event_t *) { s_game.restartSamePlayers(); showGame(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *menu = Theme::button(scr, LV_SYMBOL_HOME " Meny", 140, 46, Theme::MUTED);
    lv_obj_set_pos(menu, 10, 424);
    lv_obj_add_event_cb(menu, [](lv_event_t *) { showMenu(); }, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(g, again);
    lv_group_add_obj(g, menu);

    Rgb::effectFlash(255, 200, 60, 1200);
    Theme::loadScreen(scr);
}

}  // namespace UiYatzy
