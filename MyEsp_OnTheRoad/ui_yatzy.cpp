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
        s_game.clearPlayers();   // reset() keeps the roster; rebuild it from the player list
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
//
// One screen, two phases. The objects are kept and the layout animates between them:
//   ROLL    (rolls 1-3) big felt dice area (~68 % of the height incl. header) with a subtle,
//           darker framed nest strip at its bottom edge (no label), a translucent floating
//           card with the top-5 suggestions for the current dice, [Kasta] [Välj] at the bottom.
//           The score table is hidden.
//   RESULT  (after the last roll, "Välj" or a tapped suggestion) all dice sit in one row in
//           the upper half (the whole felt becomes the nest), action bar, and the full
//           scrollable score table in the lower half.
// ========================================================================================
static constexpr lv_coord_t SCR_W = 320, SCR_H = 480;
static constexpr lv_coord_t FELT_X = 4, FELT_Y = 48, FELT_W = SCR_W - 8;
static constexpr lv_coord_t FELT_H_ROLL = 276;                       // 48..324
static constexpr lv_coord_t NEST_H_ROLL = 74;
static constexpr lv_coord_t NEST_Y_ROLL = FELT_Y + FELT_H_ROLL - NEST_H_ROLL - 6;
static constexpr lv_coord_t THROW_TOP = FELT_Y + 8;
static constexpr lv_coord_t THROW_H = NEST_Y_ROLL - 6 - THROW_TOP;   // Free throw zone
static constexpr lv_coord_t FELT_H_RESULT = 142;                     // 48..190
static constexpr lv_coord_t SUG_X = 8, SUG_Y = 330, SUG_W = SCR_W - 16, SUG_H = 96;
static constexpr lv_opa_t SUG_OPA = 180;
static constexpr lv_coord_t BTN_Y = 432, BTN_H = 44;                 // Roll-phase buttons
static constexpr lv_coord_t BAR_Y = 194, BAR_H = 42;                 // Result-phase action bar
static constexpr lv_coord_t TABLE_Y = 240;                           // Lower half
static constexpr lv_coord_t CAT_COL_W = 112;
static constexpr uint32_t MOVE_MS = 260, PHASE_MS = 320, BOUNCE_MS = 220, AUTO_RESULT_MS = 650;
static constexpr uint32_t FAST_PHASE_MS = 400;                       // ~15 fps face changes
static constexpr uint32_t FAST_INTERVAL_MS = 66, SLOW_EXTRA_MS = 190;
static constexpr lv_coord_t ZOOM_100 = 256, ZOOM_110 = 282;           // LVGL zoom: 256 = 100 %
static constexpr int SUG_MAX = 5;

enum class Phase : uint8_t { ROLL, RESULT };

enum RowKind : uint8_t { ROW_HEADER, ROW_CAT, ROW_SUM, ROW_BONUS, ROW_TOTAL };
struct RowInfo {
    RowKind kind;
    YatzyCategory cat;
};

struct DieUi {
    lv_obj_t *obj;
    lv_obj_t *pips[9];
    lv_coord_t jx, jy;       // Random scatter in the throw zone (changes every roll)
    lv_coord_t totX, totY;   // Accumulated drag vector
    bool dragging;
    bool rolling;
    uint32_t nextChange;     // Roll animation: next face change (ms since roll start)
    uint32_t finishMs;       // Roll animation: landing time (ms since roll start)
};

static DieUi s_dice[YATZY_MAX_DICE];
static lv_coord_t s_dieSize = 52;
static Phase s_phase = Phase::ROLL;
static uint32_t s_gen = 0;   // Bumped when the game screen is left (guards timers)
static lv_obj_t *s_playerLabel, *s_roundLabel, *s_table;
static lv_obj_t *s_felt, *s_nest;
static lv_obj_t *s_rollBtn, *s_rollLabel, *s_chooseBtn;
static lv_obj_t *s_backBtn, *s_backLabel, *s_writeBtn, *s_writeLabel;
static lv_obj_t *s_sugPanel, *s_sugHint, *s_sugChip[SUG_MAX], *s_sugName[SUG_MAX], *s_sugScore[SUG_MAX];
static YatzySuggestion s_sug[SUG_MAX];
static int s_sugCount = 0;
static RowInfo s_rows[1 + YATZY_CATEGORY_COUNT + 3];
static int s_rowCount = 0;
static int s_rolling = 0;
static YatzyCategory s_selected = YatzyCategory::COUNT;
static YatzyCategory s_best = YatzyCategory::COUNT;

static void refreshTable();
static void refreshActions();
static void refreshSuggestions(bool animateIn);
static void setPhase(Phase p, bool animate);

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

// --- Animation helpers ------------------------------------------------------------------
static void zoomExec(void *obj, int32_t v) { lv_obj_set_style_transform_zoom((lv_obj_t *)obj, (lv_coord_t)v, 0); }
static void opaExec(void *obj, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0); }
static void xExec(void *obj, int32_t v) { lv_obj_set_x((lv_obj_t *)obj, (lv_coord_t)v); }
static void yExec(void *obj, int32_t v) { lv_obj_set_y((lv_obj_t *)obj, (lv_coord_t)v); }
static void hExec(void *obj, int32_t v) { lv_obj_set_height((lv_obj_t *)obj, (lv_coord_t)v); }

static void animInt(lv_obj_t *obj, lv_anim_exec_xcb_t exec, int32_t from, int32_t to, uint32_t ms,
                    lv_anim_path_cb_t path = lv_anim_path_ease_out, lv_anim_ready_cb_t ready = nullptr)
{
    lv_anim_del(obj, exec);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, exec);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, ms);
    lv_anim_set_path_cb(&a, path);
    if (ready) lv_anim_set_ready_cb(&a, ready);
    lv_anim_start(&a);
}

static void animXY(lv_obj_t *obj, lv_coord_t x, lv_coord_t y, uint32_t ms)
{
    animInt(obj, xExec, lv_obj_get_x(obj), x, ms, lv_anim_path_ease_in_out);
    animInt(obj, yExec, lv_obj_get_y(obj), y, ms, lv_anim_path_ease_in_out);
}

static void stopDieAnims(DieUi &d)
{
    lv_anim_del(d.obj, xExec);
    lv_anim_del(d.obj, yExec);
    lv_anim_del(d.obj, zoomExec);
    lv_obj_set_style_transform_zoom(d.obj, ZOOM_100, 0);
    lv_obj_set_style_transform_angle(d.obj, 0, 0);
}

// --- Dice placement ---------------------------------------------------------------------
static lv_coord_t slotX(int i)
{
    int n = s_game.diceCount();
    return (lv_coord_t)((2 * i + 1) * SCR_W / (2 * n)) - s_dieSize / 2;
}

static lv_coord_t scatterX() { return (lv_coord_t)LV_MAX(0, (SCR_W / s_game.diceCount() - s_dieSize) / 2 - 1); }
static lv_coord_t scatterY() { return (lv_coord_t)LV_MAX(0, (THROW_H - s_dieSize) / 2 - 2); }

static void targetPos(int i, lv_coord_t &x, lv_coord_t &y)
{
    x = slotX(i);
    if (s_phase == Phase::RESULT) {
        y = FELT_Y + (FELT_H_RESULT - s_dieSize) / 2;   // Everything in the nest, one row
    } else if (s_game.held(i)) {
        y = NEST_Y_ROLL + (NEST_H_ROLL - s_dieSize) / 2;
    } else {
        x += s_dice[i].jx;
        y = THROW_TOP + (THROW_H - s_dieSize) / 2 + s_dice[i].jy;
    }
}

// Held dice get a gold rim and look "parked"; free dice glow. In RESULT all dice are final.
static void styleDie(int i)
{
    lv_obj_t *o = s_dice[i].obj;
    bool rolled = s_game.hasRolled();
    bool result = s_phase == Phase::RESULT;
    bool held = !result && s_game.held(i);
    lv_obj_set_style_border_width(o, held ? 3 : (result ? 1 : 0), 0);
    lv_obj_set_style_border_color(o, Theme::c(held ? Theme::GOLD : 0x8C7A3A), 0);
    lv_obj_set_style_shadow_width(o, held ? 6 : (result ? 10 : 18), 0);
    lv_obj_set_style_shadow_color(o, Theme::c(held || result ? 0x000000 : Theme::TEAL), 0);
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

static int rowOfCategory(YatzyCategory c)
{
    for (int r = 0; r < s_rowCount; r++) if (s_rows[r].kind == ROW_CAT && s_rows[r].cat == c) return r;
    return -1;
}

// --- Drag / tap: throw zone <-> nest (roll phase only) -----------------------------------
static void dieEvent(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    DieUi &d = s_dice[i];
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (code == LV_EVENT_PRESSED) {
        d.dragging = s_phase == Phase::ROLL && !s_rolling && s_game.hasRolled() && !s_game.finished();
        d.totX = d.totY = 0;
        if (d.dragging) {
            lv_anim_del(d.obj, xExec);
            lv_anim_del(d.obj, yExec);
            lv_obj_move_foreground(d.obj);
        }
    } else if (code == LV_EVENT_PRESSING && d.dragging && indev) {
        lv_point_t v;
        lv_indev_get_vect(indev, &v);
        d.totX += v.x;
        d.totY += v.y;
        // The die follows the finger inside the felt.
        lv_coord_t x = LV_CLAMP(0, lv_obj_get_x(d.obj) + v.x, SCR_W - s_dieSize);
        lv_coord_t y = LV_CLAMP(THROW_TOP, lv_obj_get_y(d.obj) + v.y, NEST_Y_ROLL + NEST_H_ROLL - s_dieSize);
        lv_obj_set_pos(d.obj, x, y);
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && d.dragging) {
        d.dragging = false;
        bool held = s_game.held(i);
        bool tap = LV_ABS(d.totX) < 10 && LV_ABS(d.totY) < 10;
        lv_coord_t centerY = lv_obj_get_y(d.obj) + s_dieSize / 2;
        bool wantHold = held;
        if (tap) wantHold = !held;                                               // Tap toggles
        else if (!held && (d.totY > 25 || centerY >= NEST_Y_ROLL)) wantHold = true;   // Swipe down
        else if (held && (d.totY < -25 || centerY < NEST_Y_ROLL)) wantHold = false;   // Swipe up
        if (wantHold != held) {
            s_game.setHold(i, wantHold);
            if (!wantHold) {   // Back to the throw zone: new random spot, rolled on next Kasta
                s_dice[i].jx = (lv_coord_t)rnd(-scatterX(), scatterX());
                s_dice[i].jy = (lv_coord_t)rnd(-scatterY(), scatterY());
            }
        }
        moveDie(i, true);   // Glides to the nest / back up
        refreshActions();
    }
}

static void createDie(lv_obj_t *parent, int i)
{
    DieUi &d = s_dice[i];
    d.obj = lv_obj_create(parent);
    lv_obj_t *o = d.obj;
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, s_dieSize, s_dieSize);
    lv_obj_set_style_radius(o, s_dieSize / 5, 0);
    lv_obj_set_style_bg_color(o, Theme::c(Theme::DIE_FACE), 0);
    lv_obj_set_style_bg_grad_color(o, Theme::c(0xD9D4C7), 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_spread(o, 1, 0);
    lv_obj_set_style_transform_pivot_x(o, s_dieSize / 2, 0);
    lv_obj_set_style_transform_pivot_y(o, s_dieSize / 2, 0);
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
    d.dragging = d.rolling = false;
    setFace(d, 0);
    lv_obj_add_event_cb(o, dieEvent, LV_EVENT_PRESSED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(o, dieEvent, LV_EVENT_PRESSING, (void *)(intptr_t)i);
    lv_obj_add_event_cb(o, dieEvent, LV_EVENT_RELEASED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(o, dieEvent, LV_EVENT_PRESS_LOST, (void *)(intptr_t)i);
}

// --- Banner ("YATZY!") ----------------------------------------------------------------
static void showBanner(const char *text, uint32_t color)
{
    lv_obj_t *l = Theme::label(s_felt, text, &otr_font_36, color);
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
// One controller animation (var = felt, value = elapsed ms, linear) drives all rolling dice:
//  * 0..400 ms     faces change every ~66 ms (~15 fps), strong shake + rotation
//  * 400 ms..land  the interval grows to ~250 ms and shake/rotation fade out (ease-out)
//  * landing       final face locked, zoom 110 % -> 100 % bounce
// Dice are staggered by 35 ms so the last one lands exactly at the configured time (800 ms).
// Held dice (in the nest) are never touched.
static void autoResultTimer(lv_timer_t *t)
{
    uint32_t gen = (uint32_t)(uintptr_t)t->user_data;
    if (gen != s_gen || s_phase != Phase::ROLL || s_rolling || !s_game.hasRolled() || s_game.canRoll()) return;
    setPhase(Phase::RESULT, true);
}

static void afterRoll()
{
    s_sugCount = s_game.getSuggestedCategories(s_sug, SUG_MAX);
    s_best = s_sugCount > 0 ? s_sug[0].category : YatzyCategory::COUNT;
    s_selected = s_best;
    if (s_game.potentialScore(YatzyCategory::YATZY) > 0) {
        showBanner(s_game.variant() == YatzyVariant::MAXI ? "MAXI YATZY!" : "YATZY!", Theme::GOLD);
        Rgb::effectFlash(255, 190, 40, 1500);
    }
    refreshSuggestions(true);
    refreshTable();
    refreshActions();
    if (!s_game.canRoll()) {   // Last roll: move on to the result phase automatically
        lv_timer_t *t = lv_timer_create(autoResultTimer, AUTO_RESULT_MS, (void *)(uintptr_t)s_gen);
        lv_timer_set_repeat_count(t, 1);
    }
}

static void landDie(int i)
{
    DieUi &d = s_dice[i];
    if (!d.rolling) return;
    d.rolling = false;
    setFace(d, s_game.die(i));
    lv_obj_set_style_transform_angle(d.obj, 0, 0);
    moveDie(i, false);
    lv_obj_set_style_transform_zoom(d.obj, ZOOM_110, 0);
    animInt(d.obj, zoomExec, ZOOM_110, ZOOM_100, BOUNCE_MS, lv_anim_path_ease_out);
}

static void rollExec(void *, int32_t v)
{
    uint32_t t = (uint32_t)v;
    for (int i = 0; i < s_game.diceCount(); i++) {
        DieUi &d = s_dice[i];
        if (!d.rolling) continue;
        if (t >= d.finishMs) {
            landDie(i);
            continue;
        }
        if (t < d.nextChange) continue;
        uint32_t remain = d.finishMs - t;                        // > 0
        uint32_t interval = FAST_INTERVAL_MS;
        if (t > FAST_PHASE_MS && d.finishMs > FAST_PHASE_MS)
            interval += SLOW_EXTRA_MS * (t - FAST_PHASE_MS) / (d.finishMs - FAST_PHASE_MS);
        d.nextChange = t + interval;
        setFace(d, rnd(1, 6));
        lv_coord_t x, y;
        targetPos(i, x, y);
        lv_coord_t amp = (lv_coord_t)(2 + 14 * remain / d.finishMs);
        lv_coord_t angle = (lv_coord_t)(450 * remain / d.finishMs);   // 0.1 degree units
        lv_obj_set_pos(d.obj, LV_CLAMP(0, x + rnd(-amp, amp), SCR_W - s_dieSize), y + rnd(-amp, amp));
        lv_obj_set_style_transform_angle(d.obj, (lv_coord_t)rnd(-angle, angle), 0);
    }
}

static void rollDone(lv_anim_t *)
{
    for (int i = 0; i < s_game.diceCount(); i++) landDie(i);
    s_rolling = 0;
    afterRoll();
}

static void onRoll(lv_event_t *)
{
    if (s_rolling || s_phase != Phase::ROLL || !s_game.canRoll()) return;
    bool first = !s_game.hasRolled();
    bool rollingMask[YATZY_MAX_DICE];
    int count = 0;
    for (int i = 0; i < s_game.diceCount(); i++) {
        rollingMask[i] = first || !s_game.held(i);
        if (rollingMask[i]) count++;
    }
    if (!s_game.roll()) return;
    s_selected = s_best = YatzyCategory::COUNT;
    s_sugCount = 0;
    uint32_t ms = Config::get().rollAnimMs;   // Default 800 ms
    if (count == 0) {                          // Everything held: nothing moves
        afterRoll();
        return;
    }
    Rgb::effectDiceRoll(ms + 100);
    int k = 0;
    for (int i = 0; i < s_game.diceCount(); i++) {
        if (!rollingMask[i]) continue;
        DieUi &d = s_dice[i];
        stopDieAnims(d);
        d.jx = (lv_coord_t)rnd(-scatterX(), scatterX());
        d.jy = (lv_coord_t)rnd(-scatterY(), scatterY());
        d.rolling = true;
        d.nextChange = 0;
        d.finishMs = ms - (uint32_t)(count - 1 - k) * 35;
        k++;
        lv_obj_move_foreground(d.obj);
        styleDie(i);
    }
    s_rolling = 1;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_felt);   // Deleted with the screen -> animation dies with it
    lv_anim_set_exec_cb(&a, rollExec);
    lv_anim_set_values(&a, 0, (int32_t)ms);
    lv_anim_set_time(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_ready_cb(&a, rollDone);
    lv_anim_start(&a);
    refreshSuggestions(false);
    refreshTable();
    refreshActions();
}

// --- Suggestion card (roll phase) -------------------------------------------------------
static void sugChipEvent(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_rolling || k >= s_sugCount) return;
    s_selected = s_sug[k].category;
    setPhase(Phase::RESULT, true);
}

static void refreshSuggestions(bool animateIn)
{
    const char *hint = nullptr;
    if (!s_game.hasRolled()) hint = "Tryck Kasta f\xC3\xB6r att sl\xC3\xA5 t\xC3\xA4rningarna";
    else if (s_rolling) hint = "T\xC3\xA4rningarna rullar\xE2\x80\xA6";
    else if (s_sugCount == 0) hint = "Inga rutor kvar";
    int shown = hint && (s_rolling || !s_game.hasRolled()) ? 0 : s_sugCount;
    for (int k = 0; k < SUG_MAX; k++) {
        if (k >= shown) {
            lv_obj_add_flag(s_sugChip[k], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(s_sugChip[k], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_sugName[k], Yatzy::categoryName(s_sug[k].category, s_game.variant()));
        lv_label_set_text_fmt(s_sugScore[k], "%d", s_sug[k].score);
        bool best = k == 0;
        lv_obj_set_style_bg_color(s_sugChip[k], Theme::c(best ? 0x4D3F10 : Theme::PANEL), 0);
        lv_obj_set_style_border_color(s_sugChip[k], Theme::c(best ? Theme::GOLD : Theme::LINE), 0);
        lv_obj_set_style_text_color(s_sugName[k], Theme::c(best ? Theme::GOLD : Theme::TEXT), 0);
        lv_obj_set_style_text_color(s_sugScore[k], Theme::c(best ? Theme::GOLD : 0x6FB7C6), 0);
    }
    if (hint) {
        lv_label_set_text(s_sugHint, hint);
        lv_obj_clear_flag(s_sugHint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_sugHint, LV_OBJ_FLAG_HIDDEN);
    }
    if (animateIn && s_phase == Phase::ROLL) {   // "Floats" in: slides up and fades in
        animInt(s_sugPanel, yExec, SUG_Y + 12, SUG_Y, 260);
        animInt(s_sugPanel, opaExec, LV_OPA_TRANSP, LV_OPA_COVER, 260);
    }
}

// --- Phases -----------------------------------------------------------------------------
static void hideWhenDone(lv_anim_t *a)
{
    lv_obj_t *o = (lv_obj_t *)a->var;
    bool keep = (o == s_table) ? s_phase == Phase::RESULT : s_phase == Phase::ROLL;
    if (!keep) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void setVisible(lv_obj_t *o, bool on)
{
    if (on) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void setPhase(Phase p, bool animate)
{
    s_phase = p;
    bool res = p == Phase::RESULT;
    uint32_t ms = animate ? PHASE_MS : 1;
    lv_coord_t feltH = res ? FELT_H_RESULT : FELT_H_ROLL;
    lv_coord_t nestY = res ? FELT_Y : NEST_Y_ROLL;
    lv_coord_t nestH = res ? FELT_H_RESULT : NEST_H_ROLL;
    animInt(s_felt, hExec, lv_obj_get_height(s_felt), feltH, ms, lv_anim_path_ease_in_out);
    animInt(s_nest, yExec, lv_obj_get_y(s_nest), nestY, ms, lv_anim_path_ease_in_out);
    animInt(s_nest, hExec, lv_obj_get_height(s_nest), nestH, ms, lv_anim_path_ease_in_out);
    // Roll-phase widgets
    setVisible(s_rollBtn, !res);
    setVisible(s_chooseBtn, !res);
    if (res) {
        animInt(s_sugPanel, opaExec, lv_obj_get_style_opa(s_sugPanel, 0), LV_OPA_TRANSP, ms / 2 + 1,
                lv_anim_path_ease_out, hideWhenDone);
    } else {
        lv_obj_clear_flag(s_sugPanel, LV_OBJ_FLAG_HIDDEN);
        animInt(s_sugPanel, opaExec, lv_obj_get_style_opa(s_sugPanel, 0), LV_OPA_COVER, ms);
    }
    // Result-phase widgets
    setVisible(s_writeBtn, res);
    setVisible(s_backBtn, res && s_game.canRoll());
    lv_obj_clear_flag(s_table, LV_OBJ_FLAG_HIDDEN);
    animInt(s_table, yExec, lv_obj_get_y(s_table), res ? TABLE_Y : SCR_H, ms, lv_anim_path_ease_out,
            res ? nullptr : hideWhenDone);
    for (int i = 0; i < s_game.diceCount(); i++) {
        lv_anim_del(s_dice[i].obj, zoomExec);
        lv_obj_set_style_transform_zoom(s_dice[i].obj, ZOOM_100, 0);
        moveDie(i, animate);
    }
    if (res && s_selected == YatzyCategory::COUNT) s_selected = s_best;
    refreshTable();
    refreshActions();
    if (res) {
        int r = rowOfCategory(s_selected);
        lv_obj_scroll_to_y(s_table, r > 2 ? (r - 2) * 26 : 0, LV_ANIM_OFF);
    }
}

// --- Writing a score --------------------------------------------------------------------
static void newTurnUi()
{
    for (int i = 0; i < s_game.diceCount(); i++) {
        stopDieAnims(s_dice[i]);
        s_dice[i].rolling = false;
        s_dice[i].jx = s_dice[i].jy = 0;
        setFace(s_dice[i], 0);
    }
    s_selected = s_best = YatzyCategory::COUNT;
    s_sugCount = 0;
    const YatzyPlayer &p = s_game.player(s_game.currentPlayer());
    lv_label_set_text(s_playerLabel, p.name);
    lv_label_set_text_fmt(s_roundLabel, "Runda %d/%d", s_game.round(), s_game.totalRounds());
    setPhase(Phase::ROLL, true);
    refreshSuggestions(true);
    // Prepare the table for the next result phase: current player's column in view, top row.
    lv_coord_t colW = lv_table_get_col_width(s_table, 1);
    lv_coord_t x = CAT_COL_W + s_game.currentPlayer() * colW;
    lv_coord_t visible = lv_obj_get_content_width(s_table);
    lv_coord_t target = x + colW > visible ? x + colW - visible : 0;
    lv_obj_scroll_to(s_table, target, 0, LV_ANIM_OFF);
}

static void writeSelected()
{
    if (s_rolling || s_phase != Phase::RESULT || s_selected == YatzyCategory::COUNT || !s_game.canScore(s_selected))
        return;
    int pts = s_game.potentialScore(s_selected);
    s_game.score(s_selected);
    if (pts > 0) Rgb::effectFlash(40, 255, 90, 250);
    else Rgb::effectFlash(255, 60, 40, 250);
    if (s_game.finished()) {
        s_gen++;
        showResults();
        return;
    }
    newTurnUi();
}

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
    if (s_phase != Phase::RESULT || s_rolling) return;
    if (row == LV_TABLE_CELL_NONE || row >= s_rowCount || s_rows[row].kind != ROW_CAT) return;
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

// --- Buttons ----------------------------------------------------------------------------
static void refreshActions()
{
    int left = s_game.rollsLeft();
    bool canRoll = !s_rolling && s_game.canRoll();
    int base = YATZY_ROLLS_PER_TURN - LV_MIN(s_game.rollsUsed(), YATZY_ROLLS_PER_TURN);
    if (s_game.variant() == YatzyVariant::MAXI && s_game.savedRollsRule() && left > base)
        lv_label_set_text_fmt(s_rollLabel, LV_SYMBOL_REFRESH " Kasta  %d (+%d)", base, left - base);
    else
        lv_label_set_text_fmt(s_rollLabel, LV_SYMBOL_REFRESH " Kasta  %d", left);
    if (canRoll) lv_obj_clear_state(s_rollBtn, LV_STATE_DISABLED);
    else lv_obj_add_state(s_rollBtn, LV_STATE_DISABLED);
    if (!s_rolling && s_game.hasRolled()) lv_obj_clear_state(s_chooseBtn, LV_STATE_DISABLED);
    else lv_obj_add_state(s_chooseBtn, LV_STATE_DISABLED);

    // Result phase: "back to rolling" only while rolls remain; Skriv takes the full width otherwise.
    bool back = s_phase == Phase::RESULT && canRoll;
    setVisible(s_backBtn, back);
    lv_label_set_text_fmt(s_backLabel, LV_SYMBOL_LEFT " Kasta %d", left);
    lv_obj_set_x(s_writeBtn, back ? 122 : 6);
    lv_obj_set_width(s_writeBtn, back ? 192 : SCR_W - 12);
    bool canWrite = !s_rolling && s_selected != YatzyCategory::COUNT && s_game.canScore(s_selected);
    if (canWrite) {
        lv_label_set_text_fmt(s_writeLabel, "Skriv %d p\n%s", s_game.potentialScore(s_selected),
                              Yatzy::categoryName(s_selected, s_game.variant()));
        lv_obj_clear_state(s_writeBtn, LV_STATE_DISABLED);
    } else {
        lv_label_set_text(s_writeLabel, "V\xC3\xA4lj ruta\ni tabellen");
        lv_obj_add_state(s_writeBtn, LV_STATE_DISABLED);
    }
}

static void quitEvent(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_current_target(e);
    const char *txt = lv_msgbox_get_active_btn_text(mbox);
    bool quit = txt && strcmp(txt, "Avsluta") == 0;
    lv_msgbox_close(mbox);
    if (quit) {
        s_gen++;
        showMenu();
    }
}

static void onMenu(lv_event_t *)
{
    static const char *btns[] = {"Avsluta", "Forts\xC3\xA4tt", ""};
    lv_obj_t *m = lv_msgbox_create(nullptr, "Avsluta spelet?", "Po\xC3\xA4ngen f\xC3\xB6r den h\xC3\xA4r omg\xC3\xA5ngen f\xC3\xB6rsvinner.", btns, false);
    lv_obj_set_width(m, 280);
    lv_obj_set_style_bg_color(m, Theme::c(Theme::PANEL_RAISED), 0);
    lv_obj_set_style_text_color(m, Theme::c(Theme::TEXT), 0);
    lv_obj_set_style_border_color(m, Theme::c(Theme::ACCENT), 0);
    lv_obj_set_style_border_width(m, 2, 0);
    lv_obj_center(m);
    lv_obj_add_event_cb(m, quitEvent, LV_EVENT_VALUE_CHANGED, nullptr);
}

static lv_obj_t *plainBox(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static void showGame()
{
    s_gen++;
    s_rolling = 0;
    s_phase = Phase::ROLL;
    s_sugCount = 0;
    s_selected = s_best = YatzyCategory::COUNT;
    s_dieSize = s_game.diceCount() == 6 ? 46 : 52;

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

    // Felt dice area
    s_felt = plainBox(scr, FELT_X, FELT_Y, FELT_W, FELT_H_ROLL);
    lv_obj_set_style_radius(s_felt, 14, 0);
    lv_obj_set_style_bg_color(s_felt, Theme::c(0x123A3F), 0);
    lv_obj_set_style_bg_grad_color(s_felt, Theme::c(0x0E2730), 0);
    lv_obj_set_style_bg_grad_dir(s_felt, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(s_felt, LV_OPA_COVER, 0);

    // Nest: subtle darker, framed strip at the bottom of the felt (no label)
    s_nest = plainBox(scr, FELT_X + 6, NEST_Y_ROLL, FELT_W - 12, NEST_H_ROLL);
    lv_obj_set_style_radius(s_nest, 12, 0);
    lv_obj_set_style_bg_color(s_nest, Theme::c(0x000000), 0);
    lv_obj_set_style_bg_opa(s_nest, LV_OPA_30, 0);
    lv_obj_set_style_border_width(s_nest, 1, 0);
    lv_obj_set_style_border_color(s_nest, Theme::c(0x6B5418), 0);
    lv_obj_set_style_border_opa(s_nest, LV_OPA_70, 0);

    // Floating, translucent suggestion card
    s_sugPanel = plainBox(scr, SUG_X, SUG_Y, SUG_W, SUG_H);
    lv_obj_set_style_radius(s_sugPanel, 14, 0);
    lv_obj_set_style_bg_color(s_sugPanel, Theme::c(Theme::PANEL_RAISED), 0);
    lv_obj_set_style_bg_opa(s_sugPanel, SUG_OPA, 0);
    lv_obj_set_style_border_width(s_sugPanel, 1, 0);
    lv_obj_set_style_border_color(s_sugPanel, Theme::c(Theme::LINE), 0);
    lv_obj_set_style_shadow_width(s_sugPanel, 16, 0);
    lv_obj_set_style_shadow_color(s_sugPanel, Theme::c(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_sugPanel, LV_OPA_50, 0);
    lv_obj_set_style_shadow_ofs_y(s_sugPanel, 4, 0);
    lv_obj_t *sugTitle = Theme::label(s_sugPanel, "F\xC3\xB6rslag", &otr_font_14, Theme::MUTED);
    lv_obj_set_pos(sugTitle, 10, 3);
    s_sugHint = Theme::label(s_sugPanel, "", &otr_font_14, Theme::MUTED);
    lv_obj_align(s_sugHint, LV_ALIGN_CENTER, 0, 8);
    for (int k = 0; k < SUG_MAX; k++) {
        lv_obj_t *chip = lv_btn_create(s_sugPanel);
        lv_obj_remove_style_all(chip);
        lv_obj_set_size(chip, 140, 22);
        lv_obj_set_pos(chip, 8 + (k % 2) * 148, 22 + (k / 2) * 24);
        lv_obj_set_style_radius(chip, 8, 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(chip, 1, 0);
        lv_obj_set_style_bg_color(chip, Theme::c(Theme::ACCENT_DARK), LV_STATE_PRESSED);
        lv_obj_set_style_outline_width(chip, 2, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_color(chip, Theme::c(Theme::TEAL), LV_STATE_FOCUS_KEY);
        lv_obj_set_ext_click_area(chip, 2);
        s_sugName[k] = Theme::label(chip, "", &otr_font_14);
        lv_label_set_long_mode(s_sugName[k], LV_LABEL_LONG_DOT);
        lv_obj_set_width(s_sugName[k], 100);
        lv_obj_align(s_sugName[k], LV_ALIGN_LEFT_MID, 8, 0);
        s_sugScore[k] = Theme::label(chip, "", &otr_font_14);
        lv_obj_align(s_sugScore[k], LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_add_event_cb(chip, sugChipEvent, LV_EVENT_CLICKED, (void *)(intptr_t)k);
        lv_obj_add_flag(chip, LV_OBJ_FLAG_HIDDEN);
        s_sugChip[k] = chip;
    }

    // Roll phase buttons
    s_rollBtn = Theme::button(scr, "", 150, BTN_H, Theme::ACCENT, true);
    lv_obj_set_pos(s_rollBtn, 6, BTN_Y);
    s_rollLabel = lv_obj_get_child(s_rollBtn, 0);
    lv_obj_add_event_cb(s_rollBtn, onRoll, LV_EVENT_CLICKED, nullptr);
    s_chooseBtn = Theme::button(scr, "V\xC3\xA4lj ruta " LV_SYMBOL_RIGHT, 152, BTN_H, Theme::TEAL);
    lv_obj_set_pos(s_chooseBtn, 162, BTN_Y);
    lv_obj_add_event_cb(s_chooseBtn, [](lv_event_t *) {
        if (!s_rolling && s_game.hasRolled()) setPhase(Phase::RESULT, true);
    }, LV_EVENT_CLICKED, nullptr);

    // Result phase action bar
    s_backBtn = Theme::button(scr, "", 110, BAR_H, Theme::MUTED);
    lv_obj_set_pos(s_backBtn, 6, BAR_Y);
    s_backLabel = lv_obj_get_child(s_backBtn, 0);
    lv_obj_set_style_text_font(s_backLabel, &otr_font_14, 0);
    lv_obj_add_event_cb(s_backBtn, [](lv_event_t *) {
        if (!s_rolling && s_game.canRoll()) setPhase(Phase::ROLL, true);
    }, LV_EVENT_CLICKED, nullptr);
    s_writeBtn = Theme::button(scr, "", 192, BAR_H, Theme::ACCENT, true);
    lv_obj_set_pos(s_writeBtn, 122, BAR_Y);
    s_writeLabel = lv_obj_get_child(s_writeBtn, 0);
    lv_obj_set_style_text_font(s_writeLabel, &otr_font_14, 0);
    lv_obj_set_style_text_align(s_writeLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(s_writeBtn, [](lv_event_t *) { writeSelected(); }, LV_EVENT_CLICKED, nullptr);

    lv_group_add_obj(g, s_rollBtn);
    lv_group_add_obj(g, s_chooseBtn);
    for (int k = 0; k < SUG_MAX; k++) lv_group_add_obj(g, s_sugChip[k]);
    lv_group_add_obj(g, s_writeBtn);
    lv_group_add_obj(g, s_backBtn);
    lv_group_add_obj(g, menu);

    // Score table (lower half in the result phase; parked below the screen while rolling)
    buildRows();
    int players = s_game.playerCount();
    s_table = lv_table_create(scr);
    lv_obj_set_pos(s_table, 0, SCR_H);
    lv_obj_set_size(s_table, SCR_W, SCR_H - TABLE_Y);
    lv_table_set_col_cnt(s_table, players + 1);
    lv_table_set_row_cnt(s_table, s_rowCount);
    lv_coord_t colW = LV_MAX(52, (SCR_W - CAT_COL_W - 4) / players);
    if (colW > 100) colW = 100;
    lv_table_set_col_width(s_table, 0, CAT_COL_W);
    for (int p = 0; p < players; p++) lv_table_set_col_width(s_table, p + 1, colW);
    lv_obj_set_style_bg_color(s_table, Theme::c(Theme::BG), 0);
    lv_obj_set_style_border_width(s_table, 2, 0);
    lv_obj_set_style_border_side(s_table, LV_BORDER_SIDE_TOP, 0);   // Clear divider to the dice
    lv_obj_set_style_border_color(s_table, Theme::c(Theme::ACCENT), 0);
    lv_obj_set_style_radius(s_table, 0, 0);
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
    lv_obj_add_flag(s_table, LV_OBJ_FLAG_HIDDEN);

    // Dice last: children of the screen, on top of felt/nest/table so they can travel freely
    for (int i = 0; i < YATZY_MAX_DICE; i++) s_dice[i].obj = nullptr;
    for (int i = 0; i < s_game.diceCount(); i++) {
        createDie(scr, i);
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
