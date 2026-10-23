// ui_yatzy.cpp - Yatzy / Maxi-Yatzy screens (see header).
#include "ui_yatzy.h"

#include "my_sys.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_players.h"
#include "board_rgb.h"
#include "game_live.h"
#include "game_yatzy.h"
#include "my_charwheel.h"
#include "ui_clock.h"
#include "ui_settings.h"
#include "my_theme.h"
#include "ui_wifi.h"

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
    Theme::header(scr, "OnTheRoad");

    lv_obj_t *gear = Theme::button(scr, LV_SYMBOL_SETTINGS, 52, 36, Theme::LINE);
    lv_obj_align(gear, LV_ALIGN_TOP_RIGHT, -6, 4);
    lv_obj_add_event_cb(gear, [](lv_event_t *) { UiSettings::show(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *wifi = Theme::button(scr, LV_SYMBOL_WIFI, 52, 36, Theme::LINE);
    lv_obj_align(wifi, LV_ALIGN_TOP_RIGHT, -64, 4);
    lv_obj_add_event_cb(wifi, [](lv_event_t *) { UiWifi::showNetworks(); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *hint = Theme::label(scr, "Välj spel", &my_font_14, Theme::MUTED);
    lv_obj_set_pos(hint, 14, 52);
    lv_obj_t *clk = Theme::label(scr, "", &my_font_14, Theme::MUTED);
    lv_obj_align(clk, LV_ALIGN_TOP_RIGHT, -14, 52);
    UiClock::attachLabel(clk, true);

    int def = Config::get().defaultVariant;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = lv_button_create(scr);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 300, 86);
        lv_obj_set_pos(b, 10, 76 + i * 96);
        lv_obj_set_style_radius(b, 14, 0);
        lv_obj_set_style_bg_color(b, Theme::c(Theme::PANEL), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(b, i == def ? 2 : 1, 0);
        lv_obj_set_style_border_color(b, Theme::c(i == def ? Theme::ACCENT : Theme::LINE), 0);
        lv_obj_set_style_bg_color(b, Theme::c(Theme::PANEL_RAISED), LV_STATE_PRESSED);
        // Accent stripe on the left
        lv_obj_t *stripe = lv_obj_create(b);
        lv_obj_remove_style_all(stripe);
        lv_obj_set_size(stripe, 6, 62);
        lv_obj_align(stripe, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_set_style_radius(stripe, 3, 0);
        lv_obj_set_style_bg_color(stripe, Theme::c(CHOICES[i].color), 0);
        lv_obj_set_style_bg_opa(stripe, LV_OPA_COVER, 0);
        lv_obj_remove_flag(stripe, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *t = Theme::label(b, CHOICES[i].title, &my_font_24);
        lv_obj_set_pos(t, 28, 14);
        lv_obj_t *s = Theme::label(b, CHOICES[i].sub, &my_font_14, Theme::MUTED);
        lv_obj_set_pos(s, 28, 50);
        if (i == def) {
            lv_obj_t *star = Theme::label(b, LV_SYMBOL_OK, &my_font_18, Theme::ACCENT);
            lv_obj_align(star, LV_ALIGN_TOP_RIGHT, -12, 12);
        }
        lv_obj_add_event_cb(b, menuChoiceEvent, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    lv_obj_t *foot = Theme::label(scr, FW_NAME " v" FW_VERSION, &my_font_14, Theme::LINE);
    lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -6);
    Theme::loadScreen(scr);
}

// ========================================================================================
// Players
//
// The roster (up to YATZY_MAX_PLAYERS) is picked from the player registry (board_players,
// up to 200 unique names). "Ny spelare" adds to the registry; "Gäst" plays without saving.
// s_pids keeps the registry id per seat (0 = guest) for the game history (Del B).
// ========================================================================================
static uint16_t s_pids[YATZY_MAX_PLAYERS];
static lv_obj_t *s_playerList = nullptr;
static lv_obj_t *s_addBtn = nullptr;
static lv_obj_t *s_startBtn = nullptr;
static int s_pickSlot = -1;   // Seat the picker fills; -1 = append
static char s_pickFilter[PLAYER_NAME_CAP];   // Picker search text

static void rebuildPlayerList();
static void showPicker(int slot);

static void trimInPlace(char *s)
{
    size_t n = strlen(s);
    while (n && s[n - 1] == ' ') s[--n] = '\0';
    size_t lead = strspn(s, " ");
    if (lead) memmove(s, s + lead, n - lead + 1);
}

// Puts a player into the picker's seat (or appends). Returns false if the roster is full.
static bool setSeat(const char *name, uint16_t id)
{
    int i = s_pickSlot;
    if (i < 0 || i >= s_nameCount) {
        if (s_nameCount >= YATZY_MAX_PLAYERS) return false;
        i = s_nameCount++;
    }
    snprintf(s_names[i], YATZY_NAME_CAP, "%s", name);
    s_pids[i] = id;
    return true;
}

// Registry names may have changed on the web page meanwhile: refresh them by id.
static void refreshSeatNames()
{
    for (int i = 0; i < s_nameCount; i++) {
        if (!s_pids[i]) continue;
        char n[PLAYER_NAME_CAP];
        if (Players::name(s_pids[i], n, sizeof(n))) snprintf(s_names[i], YATZY_NAME_CAP, "%s", n);
        else s_pids[i] = 0;   // Removed from the registry: keep playing as a guest
    }
}

static void playerRowEvent(lv_event_t *e)
{
    showPicker((int)(intptr_t)lv_event_get_user_data(e));
}

static void playerDeleteEvent(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= s_nameCount) return;
    for (int k = i; k < s_nameCount - 1; k++) {
        memcpy(s_names[k], s_names[k + 1], YATZY_NAME_CAP);
        s_pids[k] = s_pids[k + 1];
    }
    s_nameCount--;
    rebuildPlayerList();
}

static void rebuildPlayerList()
{
    lv_obj_clean(s_playerList);
    for (int i = 0; i < s_nameCount; i++) {
        lv_obj_t *row = lv_button_create(s_playerList);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, 240, 34);
        lv_obj_set_pos(row, 0, i * 38);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_bg_color(row, Theme::c(Theme::PANEL_RAISED), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, Theme::c(Theme::ACCENT_DARK), LV_STATE_PRESSED);
        char num[8];
        snprintf(num, sizeof(num), "%d", i + 1);
        lv_obj_t *n = Theme::label(row, num, &my_font_18, Theme::ACCENT);
        lv_obj_align(n, LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_t *name = Theme::label(row, s_names[i], &my_font_18);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(name, 150);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 36, 0);
        if (!s_pids[i]) {   // Guests are not saved in the registry
            lv_obj_t *tag = Theme::label(row, "gäst", &my_font_14, Theme::GOLD);
            lv_obj_align(tag, LV_ALIGN_RIGHT_MID, -8, 0);
        }
        lv_obj_add_event_cb(row, playerRowEvent, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *del = Theme::button(s_playerList, LV_SYMBOL_CLOSE, 44, 34, Theme::RED);
        lv_obj_set_pos(del, 248, i * 38);
        lv_obj_add_event_cb(del, playerDeleteEvent, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    if (s_nameCount == 0) {
        lv_obj_t *empty = Theme::label(s_playerList, "Lägg till minst en spelare", &my_font_14, Theme::MUTED);
        lv_obj_align(empty, LV_ALIGN_TOP_MID, 0, 20);
    }
    if (s_nameCount >= YATZY_MAX_PLAYERS) lv_obj_add_state(s_addBtn, LV_STATE_DISABLED);
    else lv_obj_remove_state(s_addBtn, LV_STATE_DISABLED);
    if (s_nameCount > 0) lv_obj_remove_state(s_startBtn, LV_STATE_DISABLED);
    else lv_obj_add_state(s_startBtn, LV_STATE_DISABLED);
}

static void showPlayers()
{
    s_pickFilter[0] = 0;
    refreshSeatNames();
    if (s_nameCount == 0 && Players::count() == 0) {   // Empty registry: two guests to get going
        snprintf(s_names[0], YATZY_NAME_CAP, "Spelare 1");
        snprintf(s_names[1], YATZY_NAME_CAP, "Spelare 2");
        s_pids[0] = s_pids[1] = 0;
        s_nameCount = 2;
    }
    lv_obj_t *scr = Theme::screen();
    Theme::header(scr, "Spelare");
    lv_obj_t *var = Theme::label(scr, Yatzy::variantName(s_variant, s_mode), &my_font_14, Theme::TEAL);
    lv_obj_align(var, LV_ALIGN_TOP_RIGHT, -12, 14);

    lv_obj_t *card = Theme::card(scr, 310, 326);
    lv_obj_set_pos(card, 5, 52);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    s_playerList = lv_obj_create(card);
    lv_obj_remove_style_all(s_playerList);
    lv_obj_set_size(s_playerList, 292, 306);
    lv_obj_remove_flag(s_playerList, LV_OBJ_FLAG_SCROLLABLE);

    s_addBtn = Theme::button(scr, LV_SYMBOL_PLUS " Lägg till spelare", 300, 42, Theme::TEAL);
    lv_obj_set_pos(s_addBtn, 10, 384);
    lv_obj_add_event_cb(s_addBtn, [](lv_event_t *) { showPicker(-1); }, LV_EVENT_CLICKED, nullptr);

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

    rebuildPlayerList();
    Theme::loadScreen(scr);
}

// ----------------------------------------------------------------------------------------
// Player picker: sorted registry list (one lv_table, light on RAM even with 200 names),
// optional search filter, "Ny spelare" (saved) and "Gäst" (not saved).
// ----------------------------------------------------------------------------------------
static PlayerDb *s_pickDb = nullptr;        // Snapshot, PSRAM
static uint16_t s_pickIds[PLAYER_MAX];      // Table row -> player id
static lv_obj_t *s_wheel = nullptr;

// Case-insensitive substring match (ASCII + Latin-1 via PlayerDb::foldChar).
static bool nameMatches(const char *name, const char *filter)
{
    if (!filter[0]) return true;
    for (const char *s = name; *s;) {
        const char *a = s, *b = filter;
        while (*a && *b) {
            uint32_t ca, cb;
            int la = PlayerDb::foldChar(a, ca), lb = PlayerDb::foldChar(b, cb);
            if (ca != cb) break;
            a += la;
            b += lb;
        }
        if (!*b) return true;
        uint32_t dummy;
        s += PlayerDb::foldChar(s, dummy);
    }
    return false;
}

static bool seated(uint16_t id, int exceptSlot)
{
    for (int i = 0; i < s_nameCount; i++)
        if (i != exceptSlot && s_pids[i] == id) return true;
    return false;
}

static void pickTableEvent(lv_event_t *e)
{
    lv_obj_t *t = lv_event_get_target_obj(e);
    uint32_t row, col;
    lv_table_get_selected_cell(t, &row, &col);
    if (row == LV_TABLE_CELL_NONE || row >= lv_table_get_row_count(t) || !s_pickIds[row]) return;
    char n[PLAYER_NAME_CAP];
    if (!Players::name(s_pickIds[row], n, sizeof(n))) return;
    setSeat(n, s_pickIds[row]);
    showPlayers();
}

static void newPlayerEntered(const char *text, bool confirmed, void *)
{
    if (!confirmed) return;   // Wheel closes itself on BACK
    int r = Players::add(text);
    if (r < 0) {
        CharWheel::setHint(s_wheel, playerErrText(r));
        return;
    }
    char n[PLAYER_NAME_CAP];
    if (!Players::name((uint16_t)r, n, sizeof(n))) snprintf(n, sizeof(n), "%s", text);
    setSeat(n, (uint16_t)r);
    CharWheel::close(s_wheel);
    showPlayers();
}

static void guestEntered(const char *text, bool confirmed, void *)
{
    if (!confirmed) return;
    char name[YATZY_NAME_CAP];
    snprintf(name, sizeof(name), "%s", text);
    trimInPlace(name);
    if (!name[0]) {
        CharWheel::setHint(s_wheel, "Namnet får inte vara tomt");
        return;
    }
    setSeat(name, 0);
    CharWheel::close(s_wheel);
    showPlayers();
}

static void filterEntered(const char *text, bool confirmed, void *)
{
    if (!confirmed) return;
    snprintf(s_pickFilter, sizeof(s_pickFilter), "%s", text);
    trimInPlace(s_pickFilter);
    CharWheel::close(s_wheel);
    showPicker(s_pickSlot);
}

static void openWheel(const char *label, const char *initial, CharWheel::Callback cb)
{
    s_wheel = CharWheel::create(lv_screen_active(), 0, 0, 320, 480, label, initial, PLAYER_NAME_CAP - 1, false, cb, nullptr);
}

static void showPicker(int slot)
{
    s_pickSlot = slot;
    if (!s_pickDb) {
        s_pickDb = (PlayerDb *)heap_caps_malloc(sizeof(PlayerDb), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_pickDb) s_pickDb = (PlayerDb *)malloc(sizeof(PlayerDb));
        if (!s_pickDb) return;
    }
    Players::snapshot(*s_pickDb);
    static uint8_t idx[PLAYER_MAX];
    int total = s_pickDb->sortedIndices(idx);

    lv_obj_t *scr = Theme::screen();
    Theme::header(scr, slot < 0 ? "Välj spelare" : "Byt spelare");
    char buf[48];
    snprintf(buf, sizeof(buf), "%d av %d", total, PLAYER_MAX);
    lv_obj_t *cnt = Theme::label(scr, buf, &my_font_14, Theme::MUTED);
    lv_obj_align(cnt, LV_ALIGN_TOP_RIGHT, -12, 14);

    lv_obj_t *add = Theme::button(scr, LV_SYMBOL_PLUS " Ny spelare", 170, 42, Theme::TEAL, true);
    lv_obj_set_pos(add, 10, 52);
    lv_obj_add_event_cb(add, [](lv_event_t *) { openWheel("Ny spelare (sparas)", "", newPlayerEntered); },
                        LV_EVENT_CLICKED, nullptr);
    if (total >= PLAYER_MAX) lv_obj_add_state(add, LV_STATE_DISABLED);
    lv_obj_t *search = Theme::button(scr, s_pickFilter[0] ? LV_SYMBOL_CLOSE " Sök" : "Sök...", 120, 42,
                                     s_pickFilter[0] ? Theme::GOLD : Theme::LINE);
    lv_obj_set_pos(search, 190, 52);
    lv_obj_add_event_cb(search, [](lv_event_t *) {
        if (s_pickFilter[0]) {   // Clear the active filter
            s_pickFilter[0] = 0;
            showPicker(s_pickSlot);
        } else {
            openWheel("Sök spelare", "", filterEntered);
        }
    }, LV_EVENT_CLICKED, nullptr);

    // Rows: registry players not already seated (the seat being replaced is allowed)
    int rows = 0;
    for (int k = 0; k < total; k++) {
        const PlayerRec &r = s_pickDb->items[idx[k]];
        if (seated(r.id, slot) || !nameMatches(r.name, s_pickFilter)) continue;
        s_pickIds[rows++] = r.id;
    }

    lv_obj_t *t = lv_table_create(scr);
    lv_obj_set_pos(t, 10, 102);
    lv_obj_set_size(t, 300, 322);
    lv_table_set_column_count(t, 1);
    lv_table_set_column_width(t, 0, 284);
    lv_obj_set_style_bg_color(t, Theme::c(Theme::PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(t, Theme::c(Theme::LINE), LV_PART_MAIN);
    lv_obj_set_style_border_width(t, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(t, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_all(t, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(t, Theme::c(Theme::PANEL), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(t, Theme::c(Theme::ACCENT_DARK), (lv_style_selector_t)LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(t, Theme::c(Theme::TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(t, &my_font_18, LV_PART_ITEMS);
    lv_obj_set_style_border_side(t, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    lv_obj_set_style_border_color(t, Theme::c(Theme::LINE), LV_PART_ITEMS);
    lv_obj_set_style_border_width(t, 1, LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(t, 9, LV_PART_ITEMS);
    lv_obj_set_style_pad_hor(t, 10, LV_PART_ITEMS);
    if (rows == 0) {
        lv_table_set_row_count(t, 1);
        s_pickIds[0] = 0;
        lv_table_set_cell_value(t, 0, 0,
                                total == 0 ? "Registret är tomt - tryck Ny spelare"
                                : s_pickFilter[0] ? "Ingen träff" : "Alla spelare är redan valda");
        lv_obj_set_style_text_color(t, Theme::c(Theme::MUTED), LV_PART_ITEMS);
    } else {
        lv_table_set_row_count(t, rows);
        for (int r = 0; r < rows; r++) {
            const PlayerRec *p = s_pickDb->get(s_pickIds[r]);
            lv_table_set_cell_value(t, r, 0, p ? p->name : "?");
        }
        lv_obj_add_event_cb(t, pickTableEvent, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    lv_obj_t *back = Theme::button(scr, LV_SYMBOL_LEFT " Tillbaka", 140, 42, Theme::MUTED);
    lv_obj_set_pos(back, 10, 432);
    lv_obj_add_event_cb(back, [](lv_event_t *) { s_pickFilter[0] = 0; showPlayers(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *guest = Theme::button(scr, "Gäst (sparas ej)", 160, 42, Theme::GOLD);
    lv_obj_set_pos(guest, 150, 432);
    lv_obj_add_event_cb(guest, [](lv_event_t *) {
        char initial[YATZY_NAME_CAP];
        snprintf(initial, sizeof(initial), "Gäst %d", s_nameCount + 1);
        openWheel("Gästens namn", initial, guestEntered);
    }, LV_EVENT_CLICKED, nullptr);

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
static constexpr int32_t SCR_W = 320, SCR_H = 480;
static constexpr int32_t FELT_X = 4, FELT_Y = 48, FELT_W = SCR_W - 8;
static constexpr int32_t FELT_H_ROLL = 276;                       // 48..324
static constexpr int32_t NEST_H_ROLL = 74;
static constexpr int32_t NEST_Y_ROLL = FELT_Y + FELT_H_ROLL - NEST_H_ROLL - 6;
static constexpr int32_t THROW_TOP = FELT_Y + 8;
static constexpr int32_t THROW_H = NEST_Y_ROLL - 6 - THROW_TOP;   // Free throw zone
static constexpr int32_t FELT_H_RESULT = 142;                     // 48..190
static constexpr int32_t SUG_X = 8, SUG_Y = 330, SUG_W = SCR_W - 16, SUG_H = 96;
static constexpr lv_opa_t SUG_OPA = 180;
static constexpr int32_t BTN_Y = 432, BTN_H = 44;                 // Roll-phase buttons
static constexpr int32_t BAR_Y = 194, BAR_H = 42;                 // Result-phase action bar
static constexpr int32_t TABLE_Y = 240;                           // Lower half
static constexpr int32_t CAT_COL_W = 112;
static constexpr uint32_t MOVE_MS = 260, PHASE_MS = 320, BOUNCE_MS = 160, AUTO_RESULT_MS = 650;
static constexpr uint32_t ROLL_STAGGER_MS = 45;
static constexpr int32_t ZOOM_100 = 256, ZOOM_LAND = 272;          // LVGL scale: 256 = 100 %
static constexpr int SUG_MAX = 5;

enum class Phase : uint8_t { ROLL, RESULT };

enum RowKind : uint8_t { ROW_HEADER, ROW_CAT, ROW_SUM, ROW_BONUS, ROW_TOTAL };
struct RowInfo {
    RowKind kind;
    YatzyCategory cat;
};

struct DieUi {
    lv_obj_t *obj;           // lv_image showing one of the pre-rendered face sprites
    int face;                // Face currently shown (0 = blank)
    int32_t jx, jy;          // Random scatter in the throw zone (changes every roll)
    int32_t totX, totY;      // Accumulated drag vector
    bool dragging;
    bool rolling;
    // Roll animation (ms since roll start)
    uint32_t startMs, finishMs;
    int32_t sx, sy, tx, ty;  // Start / landing position
    int32_t spin;            // Total rotation in 0.1 degree units (signed), unwinds to 0 at landing
    int32_t lastSeg;         // Quarter turn index of the last face change
};

static DieUi s_dice[YATZY_MAX_DICE];
static int32_t s_dieSize = 52;
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

// --- Face sprites -----------------------------------------------------------------------
// The seven faces (blank + 1..6) are rendered once per die size into ARGB8888 draw buffers in
// PSRAM (anti-aliased rounded body, soft top gloss, inset pips). A die is a single lv_image;
// rotation and scale are done by the image transform, which is far cheaper than transforming
// an object tree (LVGL 9 renders transformed objects through an intermediate layer).
struct FaceSet {
    int32_t size;
    uint8_t *mem;
    lv_draw_buf_t buf[7];
};
static FaceSet s_faceSets[2];
static const FaceSet *s_faces = nullptr;   // Set of the current die size

static void drawFace(lv_layer_t *layer, int32_t S, int face)
{
    const int32_t r = S / 5;
    lv_area_t a = {0, 0, S - 1, S - 1};
    // Body: warm ivory with a vertical gradient and a thin darker rim
    lv_draw_rect_dsc_t body;
    lv_draw_rect_dsc_init(&body);
    body.radius = r;
    body.bg_opa = LV_OPA_COVER;
    const lv_color_t bodyCols[2] = {Theme::c(0xFFFDF6), Theme::c(0xD3CCBC)};
    lv_grad_init_stops(&body.bg_grad, bodyCols, nullptr, nullptr, 2);
    lv_grad_vertical_init(&body.bg_grad);
    body.bg_color = bodyCols[0];
    body.border_width = 1;
    body.border_color = Theme::c(0xA89F8C);
    body.border_opa = LV_OPA_80;
    lv_draw_rect(layer, &body, &a);
    // Gloss: white fading out over the upper part
    lv_draw_rect_dsc_t gloss;
    lv_draw_rect_dsc_init(&gloss);
    gloss.radius = r - 2;
    gloss.bg_opa = LV_OPA_COVER;
    const lv_color_t glossCols[2] = {lv_color_white(), lv_color_white()};
    const lv_opa_t glossOpa[2] = {120, 0};
    lv_grad_init_stops(&gloss.bg_grad, glossCols, glossOpa, nullptr, 2);
    lv_grad_vertical_init(&gloss.bg_grad);
    gloss.bg_color = glossCols[0];
    lv_area_t g = {3, 2, S - 4, S * 45 / 100};
    lv_draw_rect(layer, &gloss, &g);
    // Pips: light lower rim (inset look) + dark pip with a slightly lighter centre
    uint16_t mask = FACE_PIPS[face >= 1 && face <= 6 ? face : 0];
    const int32_t pip = S / 5 - 1, off = S * 27 / 100, c = S / 2;
    for (int k = 0; k < 9; k++) {
        if (!(mask & (1 << k))) continue;
        int32_t px = c + (k % 3 - 1) * off - pip / 2, py = c + (k / 3 - 1) * off - pip / 2;
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.radius = LV_RADIUS_CIRCLE;
        d.bg_opa = LV_OPA_70;
        d.bg_color = lv_color_white();
        lv_area_t rim = {px, py + 1, px + pip - 1, py + pip};
        lv_draw_rect(layer, &d, &rim);
        d.bg_opa = LV_OPA_COVER;
        const lv_color_t pipCols[2] = {Theme::c(0x0B1220), Theme::c(Theme::DIE_PIP)};
        lv_grad_init_stops(&d.bg_grad, pipCols, nullptr, nullptr, 2);
        lv_grad_vertical_init(&d.bg_grad);
        d.bg_color = pipCols[0];
        lv_area_t p = {px, py, px + pip - 1, py + pip - 1};
        lv_draw_rect(layer, &d, &p);
    }
}

// Returns the sprite set for die size S, rendering it on first use (LVGL context).
static const FaceSet *facesFor(int32_t S)
{
    FaceSet *freeSet = nullptr;
    for (FaceSet &fs : s_faceSets) {
        if (fs.mem && fs.size == S) return &fs;
        if (!fs.mem && !freeSet) freeSet = &fs;
    }
    if (!freeSet) return nullptr;
    const uint32_t stride = lv_draw_buf_width_to_stride((uint32_t)S, LV_COLOR_FORMAT_ARGB8888);
    const size_t one = (size_t)stride * (size_t)S;
    uint8_t *mem = (uint8_t *)heap_caps_aligned_alloc(64, one * 7, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) return nullptr;
    lv_obj_t *canvas = lv_canvas_create(lv_layer_top());
    for (int f = 0; f < 7; f++) {
        lv_draw_buf_init(&freeSet->buf[f], (uint32_t)S, (uint32_t)S, LV_COLOR_FORMAT_ARGB8888, stride, mem + one * f,
                         (uint32_t)one);
        lv_canvas_set_draw_buf(canvas, &freeSet->buf[f]);
        lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_TRANSP);
        lv_layer_t layer;
        lv_canvas_init_layer(canvas, &layer);
        drawFace(&layer, S, f);
        lv_canvas_finish_layer(canvas, &layer);
    }
    lv_obj_delete(canvas);
    freeSet->mem = mem;
    freeSet->size = S;
    return freeSet;
}

static void setFace(DieUi &d, int face)
{
    if (face < 0 || face > 6) face = 0;
    if (d.face == face) return;
    d.face = face;
    if (s_faces) lv_image_set_src(d.obj, &s_faces->buf[face]);
}

// --- Animation helpers ------------------------------------------------------------------
static void zoomExec(void *obj, int32_t v) { lv_image_set_scale((lv_obj_t *)obj, (uint32_t)v); }
static void opaExec(void *obj, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0); }
static void xExec(void *obj, int32_t v) { lv_obj_set_x((lv_obj_t *)obj, (int32_t)v); }
static void yExec(void *obj, int32_t v) { lv_obj_set_y((lv_obj_t *)obj, (int32_t)v); }
static void hExec(void *obj, int32_t v) { lv_obj_set_height((lv_obj_t *)obj, (int32_t)v); }

static void animInt(lv_obj_t *obj, lv_anim_exec_xcb_t exec, int32_t from, int32_t to, uint32_t ms,
                    lv_anim_path_cb_t path = lv_anim_path_ease_out, lv_anim_ready_cb_t ready = nullptr)
{
    lv_anim_delete(obj, exec);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, exec);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, path);
    if (ready) lv_anim_set_completed_cb(&a, ready);
    lv_anim_start(&a);
}

static void animXY(lv_obj_t *obj, int32_t x, int32_t y, uint32_t ms)
{
    animInt(obj, xExec, lv_obj_get_x(obj), x, ms, lv_anim_path_ease_in_out);
    animInt(obj, yExec, lv_obj_get_y(obj), y, ms, lv_anim_path_ease_in_out);
}

static void stopDieAnims(DieUi &d)
{
    lv_anim_delete(d.obj, xExec);
    lv_anim_delete(d.obj, yExec);
    lv_anim_delete(d.obj, zoomExec);
    lv_image_set_scale(d.obj, ZOOM_100);
    lv_image_set_rotation(d.obj, 0);
}

// --- Dice placement ---------------------------------------------------------------------
static int32_t slotX(int i)
{
    int n = s_game.diceCount();
    return (int32_t)((2 * i + 1) * SCR_W / (2 * n)) - s_dieSize / 2;
}

static int32_t scatterX() { return (int32_t)LV_MAX(0, (SCR_W / s_game.diceCount() - s_dieSize) / 2 - 1); }
static int32_t scatterY() { return (int32_t)LV_MAX(0, (THROW_H - s_dieSize) / 2 - 2); }

static void targetPos(int i, int32_t &x, int32_t &y)
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
    bool rolling = s_dice[i].rolling;   // Spinning sprite: no box border/shadow (they would not rotate)
    lv_obj_set_style_border_width(o, rolling ? 0 : (held ? 3 : 0), 0);
    lv_obj_set_style_border_color(o, Theme::c(Theme::GOLD), 0);
    lv_obj_set_style_shadow_width(o, held ? 6 : (result ? 10 : 16), 0);
    lv_obj_set_style_shadow_offset_y(o, held ? 1 : 3, 0);
    lv_obj_set_style_shadow_color(o, Theme::c(held || result ? 0x000000 : 0x05343A), 0);
    lv_obj_set_style_shadow_opa(o, rolled && !rolling ? (held ? LV_OPA_60 : LV_OPA_70) : LV_OPA_0, 0);
    lv_obj_set_style_image_opa(o, rolled ? (held ? 225 : LV_OPA_COVER) : LV_OPA_30, 0);
}

static void moveDie(int i, bool animate)
{
    int32_t x, y;
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
    lv_indev_t *indev = lv_indev_active();
    if (code == LV_EVENT_PRESSED) {
        d.dragging = s_phase == Phase::ROLL && !s_rolling && s_game.hasRolled() && !s_game.finished();
        d.totX = d.totY = 0;
        if (d.dragging) {
            lv_anim_delete(d.obj, xExec);
            lv_anim_delete(d.obj, yExec);
            lv_obj_move_foreground(d.obj);
        }
    } else if (code == LV_EVENT_PRESSING && d.dragging && indev) {
        lv_point_t v;
        lv_indev_get_vect(indev, &v);
        d.totX += v.x;
        d.totY += v.y;
        // The die follows the finger inside the felt.
        int32_t x = LV_CLAMP(0, lv_obj_get_x(d.obj) + v.x, SCR_W - s_dieSize);
        int32_t y = LV_CLAMP(THROW_TOP, lv_obj_get_y(d.obj) + v.y, NEST_Y_ROLL + NEST_H_ROLL - s_dieSize);
        lv_obj_set_pos(d.obj, x, y);
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && d.dragging) {
        d.dragging = false;
        bool held = s_game.held(i);
        bool tap = LV_ABS(d.totX) < 10 && LV_ABS(d.totY) < 10;
        int32_t centerY = lv_obj_get_y(d.obj) + s_dieSize / 2;
        bool wantHold = held;
        if (tap) wantHold = !held;                                               // Tap toggles
        else if (!held && (d.totY > 25 || centerY >= NEST_Y_ROLL)) wantHold = true;   // Swipe down
        else if (held && (d.totY < -25 || centerY < NEST_Y_ROLL)) wantHold = false;   // Swipe up
        if (wantHold != held) {
            s_game.setHold(i, wantHold);
            if (!wantHold) {   // Back to the throw zone: new random spot, rolled on next Kasta
                s_dice[i].jx = (int32_t)rnd(-scatterX(), scatterX());
                s_dice[i].jy = (int32_t)rnd(-scatterY(), scatterY());
            }
        }
        moveDie(i, true);   // Glides to the nest / back up
        refreshActions();
    }
}

static void createDie(lv_obj_t *parent, int i)
{
    DieUi &d = s_dice[i];
    d.obj = lv_image_create(parent);
    lv_obj_t *o = d.obj;
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, s_dieSize, s_dieSize);
    lv_obj_set_style_radius(o, s_dieSize / 5, 0);   // For the held rim and the shadow
    lv_obj_set_style_shadow_spread(o, 0, 0);
    lv_image_set_pivot(o, s_dieSize / 2, s_dieSize / 2);
    lv_image_set_antialias(o, true);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE));
    lv_obj_set_ext_click_area(o, 4);
    d.jx = d.jy = 0;
    d.dragging = d.rolling = false;
    d.face = -1;
    setFace(d, 0);
    lv_obj_add_event_cb(o, dieEvent, LV_EVENT_PRESSED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(o, dieEvent, LV_EVENT_PRESSING, (void *)(intptr_t)i);
    lv_obj_add_event_cb(o, dieEvent, LV_EVENT_RELEASED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(o, dieEvent, LV_EVENT_PRESS_LOST, (void *)(intptr_t)i);
}

// --- Banner ("YATZY!") ----------------------------------------------------------------
static void showBanner(const char *text, uint32_t color)
{
    lv_obj_t *l = Theme::label(s_felt, text, &my_font_36, color);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 4);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, l);
    lv_anim_set_exec_cb(&a, opaExec);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_delay(&a, 1200);
    lv_anim_set_duration(&a, 600);
    lv_anim_set_completed_cb(&a, [](lv_anim_t *an) { lv_obj_delete_async((lv_obj_t *)an->var); });
    lv_anim_start(&a);
}

// --- Rolling ----------------------------------------------------------------------------
// One controller animation (var = felt, value = elapsed ms, linear) drives all rolling dice and
// updates them on every LVGL frame (refresh period 16 ms):
//  * the dice are thrown in from one side and travel to their landing spot (ease-out cubic)
//  * three decaying hops (scale up + lift) make them bounce on the felt
//  * the sprite spins 1.25-2.25 turns and unwinds to 0; each quarter turn shows a new face,
//    the last quarter shows the real result, so the die "rolls onto" its value
//  * landing: short 106 % -> 100 % settle
// Starts are staggered by 45 ms; the last die lands at the configured time (default 800 ms).
// Held dice (in the nest) are never touched.
static void autoResultTimer(lv_timer_t *t)
{
    uint32_t gen = (uint32_t)(uintptr_t)lv_timer_get_user_data(t);
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
    lv_image_set_rotation(d.obj, 0);
    moveDie(i, false);
    // Small "thud": the die settles from 106 % to 100 %
    animInt(d.obj, zoomExec, ZOOM_LAND, ZOOM_100, BOUNCE_MS, lv_anim_path_ease_out);
}

// Per-frame update of one rolling die. p = 0..1 progress of this die.
static void rollFrame(int i, float p)
{
    DieUi &d = s_dice[i];
    const float q = 1.0f - p;
    const float e = 1.0f - q * q * q;                          // Ease-out cubic: fast throw, soft stop
    // Hops: three decaying bounces (|sin| arcs) -> scale up (towards the viewer) and lift
    float hop = sinf(3.0f * (float)M_PI * p);
    if (hop < 0) hop = -hop;
    hop *= q * sqrtf(q);
    int32_t x = d.sx + (int32_t)((float)(d.tx - d.sx) * e);
    int32_t y = d.sy + (int32_t)((float)(d.ty - d.sy) * e) - (int32_t)(hop * 10.0f);
    lv_obj_set_pos(d.obj, LV_CLAMP(0, x, SCR_W - s_dieSize), y);
    lv_image_set_scale(d.obj, (uint32_t)(ZOOM_100 + (int32_t)(hop * 64.0f)));
    // Spin unwinds to 0; a new random face shows every quarter turn, the final face on the last one
    int32_t angle = (int32_t)((float)d.spin * (1.0f - e));
    lv_image_set_rotation(d.obj, angle % 3600);
    int32_t seg = LV_ABS(angle) / 900;
    if (seg != d.lastSeg) {
        d.lastSeg = seg;
        if (seg == 0) {
            setFace(d, s_game.die(i));
        } else {
            int f = rnd(1, 5);
            if (f >= d.face) f++;                                  // Always a different face
            setFace(d, f);
        }
    }
}

static void rollExec(void *, int32_t v)
{
    uint32_t t = (uint32_t)v;
    for (int i = 0; i < s_game.diceCount(); i++) {
        DieUi &d = s_dice[i];
        if (!d.rolling || t < d.startMs) continue;
        if (t >= d.finishMs) {
            landDie(i);
            continue;
        }
        rollFrame(i, (float)(t - d.startMs) / (float)(d.finishMs - d.startMs));
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
    if (ms < 300) ms = 300;
    if (count == 0) {                          // Everything held: nothing moves
        afterRoll();
        return;
    }
    Rgb::effectDiceRoll(ms + 100);
    // All dice are thrown in from the same side (random left/right) and roll across the felt.
    const bool fromLeft = rnd(0, 1) == 0;
    const int32_t dir = fromLeft ? 1 : -1;
    int k = 0;
    for (int i = 0; i < s_game.diceCount(); i++) {
        if (!rollingMask[i]) continue;
        DieUi &d = s_dice[i];
        stopDieAnims(d);
        d.jx = (int32_t)rnd(-scatterX(), scatterX());
        d.jy = (int32_t)rnd(-scatterY(), scatterY());
        d.rolling = true;
        targetPos(i, d.tx, d.ty);
        d.sx = fromLeft ? rnd(0, 24) : SCR_W - s_dieSize - rnd(0, 24);
        d.sy = THROW_TOP + rnd(0, LV_MAX(0, THROW_H - s_dieSize));
        // Roll direction follows the travel; 1.25 .. 2.25 turns
        d.spin = dir * (4500 + rnd(0, 3600));
        d.lastSeg = -1;
        d.startMs = (uint32_t)k * ROLL_STAGGER_MS;
        d.finishMs = ms - (uint32_t)(count - 1 - k) * ROLL_STAGGER_MS / 2;
        if (d.finishMs < d.startMs + 200) d.finishMs = d.startMs + 200;
        k++;
        lv_obj_move_foreground(d.obj);
        styleDie(i);
        rollFrame(i, 0.0f);
    }
    s_rolling = 1;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_felt);   // Deleted with the screen -> animation dies with it
    lv_anim_set_exec_cb(&a, rollExec);
    lv_anim_set_values(&a, 0, (int32_t)ms);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_completed_cb(&a, rollDone);
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
    else if (s_rolling) hint = "T\xC3\xA4rningarna rullar...";
    else if (s_sugCount == 0) hint = "Inga rutor kvar";
    int shown = hint && (s_rolling || !s_game.hasRolled()) ? 0 : s_sugCount;
    for (int k = 0; k < SUG_MAX; k++) {
        if (k >= shown) {
            lv_obj_add_flag(s_sugChip[k], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_sugChip[k], LV_OBJ_FLAG_HIDDEN);
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
        lv_obj_remove_flag(s_sugHint, LV_OBJ_FLAG_HIDDEN);
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
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void setPhase(Phase p, bool animate)
{
    s_phase = p;
    bool res = p == Phase::RESULT;
    uint32_t ms = animate ? PHASE_MS : 1;
    int32_t feltH = res ? FELT_H_RESULT : FELT_H_ROLL;
    int32_t nestY = res ? FELT_Y : NEST_Y_ROLL;
    int32_t nestH = res ? FELT_H_RESULT : NEST_H_ROLL;
    animInt(s_felt, hExec, lv_obj_get_height(s_felt), feltH, ms, lv_anim_path_ease_in_out);
    animInt(s_nest, yExec, lv_obj_get_y(s_nest), nestY, ms, lv_anim_path_ease_in_out);
    animInt(s_nest, hExec, lv_obj_get_height(s_nest), nestH, ms, lv_anim_path_ease_in_out);
    // Roll-phase widgets
    setVisible(s_rollBtn, !res);
    setVisible(s_chooseBtn, !res);
    if (res) {
        animInt(s_sugPanel, opaExec, lv_obj_get_style_opa(s_sugPanel, LV_PART_MAIN), LV_OPA_TRANSP, ms / 2 + 1,
                lv_anim_path_ease_out, hideWhenDone);
    } else {
        lv_obj_remove_flag(s_sugPanel, LV_OBJ_FLAG_HIDDEN);
        animInt(s_sugPanel, opaExec, lv_obj_get_style_opa(s_sugPanel, LV_PART_MAIN), LV_OPA_COVER, ms);
    }
    // Result-phase widgets
    setVisible(s_writeBtn, res);
    setVisible(s_backBtn, res && s_game.canRoll());
    lv_obj_remove_flag(s_table, LV_OBJ_FLAG_HIDDEN);
    animInt(s_table, yExec, lv_obj_get_y(s_table), res ? TABLE_Y : SCR_H, ms, lv_anim_path_ease_out,
            res ? nullptr : hideWhenDone);
    for (int i = 0; i < s_game.diceCount(); i++) {
        lv_anim_delete(s_dice[i].obj, zoomExec);
        lv_image_set_scale(s_dice[i].obj, ZOOM_100);
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
    int32_t colW = lv_table_get_column_width(s_table, 1);
    int32_t x = CAT_COL_W + s_game.currentPlayer() * colW;
    int32_t visible = lv_obj_get_content_width(s_table);
    int32_t target = x + colW > visible ? x + colW - visible : 0;
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
    // LVGL 9: recolour the fill / label draw tasks of each table cell (id1 = row, id2 = column).
    lv_draw_task_t *task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(task);
    if (!base || base->part != LV_PART_ITEMS) return;
    lv_draw_fill_dsc_t *fill = lv_draw_task_get_fill_dsc(task);
    lv_draw_label_dsc_t *text = lv_draw_task_get_label_dsc(task);
    if (!fill && !text) return;
    uint32_t row = base->id1, col = base->id2;
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
    if (fill) {
        fill->color = Theme::c(bg);
        fill->opa = LV_OPA_COVER;
    }
    if (text) text->color = Theme::c(fg);
}

static void tableClickEvent(lv_event_t *)
{
    uint32_t row, col;
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
    if (canRoll) lv_obj_remove_state(s_rollBtn, LV_STATE_DISABLED);
    else lv_obj_add_state(s_rollBtn, LV_STATE_DISABLED);
    if (!s_rolling && s_game.hasRolled()) lv_obj_remove_state(s_chooseBtn, LV_STATE_DISABLED);
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
        lv_obj_remove_state(s_writeBtn, LV_STATE_DISABLED);
    } else {
        lv_label_set_text(s_writeLabel, "V\xC3\xA4lj ruta\ni tabellen");
        lv_obj_add_state(s_writeBtn, LV_STATE_DISABLED);
    }
    GameLive::publish(s_game, true, s_rolling != 0, s_phase == Phase::RESULT);   // Web live view
}

static void quitEvent(lv_event_t *e)
{
    lv_msgbox_close((lv_obj_t *)lv_event_get_user_data(e));
    s_gen++;
    GameLive::setActive(false);
    showMenu();
}

static void onMenu(lv_event_t *)
{
    lv_obj_t *m = lv_msgbox_create(nullptr);
    lv_msgbox_add_title(m, "Avsluta spelet?");
    lv_msgbox_add_text(m, "Po\xC3\xA4ngen f\xC3\xB6r den h\xC3\xA4r omg\xC3\xA5ngen f\xC3\xB6rsvinner.");
    lv_obj_set_width(m, 280);
    lv_obj_set_style_bg_color(m, Theme::c(Theme::PANEL_RAISED), 0);
    lv_obj_set_style_text_color(m, Theme::c(Theme::TEXT), 0);
    lv_obj_set_style_border_color(m, Theme::c(Theme::ACCENT), 0);
    lv_obj_set_style_border_width(m, 2, 0);
    lv_obj_center(m);
    lv_obj_t *bQuit = lv_msgbox_add_footer_button(m, "Avsluta");
    lv_obj_t *bCont = lv_msgbox_add_footer_button(m, "Forts\xC3\xA4tt");
    lv_obj_add_event_cb(bQuit, quitEvent, LV_EVENT_CLICKED, m);
    lv_obj_add_event_cb(bCont, [](lv_event_t *e) { lv_msgbox_close((lv_obj_t *)lv_event_get_user_data(e)); },
                        LV_EVENT_CLICKED, m);
}

static lv_obj_t *plainBox(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
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
    s_faces = facesFor(s_dieSize);
    if (!s_faces) ESP_LOGE("yatzy", "die sprites: out of PSRAM");

    lv_obj_t *scr = Theme::screen();

    // Header: player, round, menu
    lv_obj_t *h = Theme::header(scr, "");
    lv_obj_clean(h);
    s_playerLabel = Theme::label(h, "", &my_font_24, Theme::ACCENT);
    lv_label_set_long_mode(s_playerLabel, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(s_playerLabel, 170);
    lv_obj_align(s_playerLabel, LV_ALIGN_LEFT_MID, 10, 0);
    s_roundLabel = Theme::label(h, "", &my_font_14, Theme::MUTED);
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
    lv_obj_set_style_shadow_offset_y(s_sugPanel, 4, 0);
    lv_obj_t *sugTitle = Theme::label(s_sugPanel, "F\xC3\xB6rslag", &my_font_14, Theme::MUTED);
    lv_obj_set_pos(sugTitle, 10, 3);
    s_sugHint = Theme::label(s_sugPanel, "", &my_font_14, Theme::MUTED);
    lv_obj_align(s_sugHint, LV_ALIGN_CENTER, 0, 8);
    for (int k = 0; k < SUG_MAX; k++) {
        lv_obj_t *chip = lv_button_create(s_sugPanel);
        lv_obj_remove_style_all(chip);
        lv_obj_set_size(chip, 140, 22);
        lv_obj_set_pos(chip, 8 + (k % 2) * 148, 22 + (k / 2) * 24);
        lv_obj_set_style_radius(chip, 8, 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(chip, 1, 0);
        lv_obj_set_style_bg_color(chip, Theme::c(Theme::ACCENT_DARK), LV_STATE_PRESSED);
        lv_obj_set_ext_click_area(chip, 2);
        s_sugName[k] = Theme::label(chip, "", &my_font_14);
        lv_label_set_long_mode(s_sugName[k], LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(s_sugName[k], 100);
        lv_obj_align(s_sugName[k], LV_ALIGN_LEFT_MID, 8, 0);
        s_sugScore[k] = Theme::label(chip, "", &my_font_14);
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
    lv_obj_set_style_text_font(s_backLabel, &my_font_14, 0);
    lv_obj_add_event_cb(s_backBtn, [](lv_event_t *) {
        if (!s_rolling && s_game.canRoll()) setPhase(Phase::ROLL, true);
    }, LV_EVENT_CLICKED, nullptr);
    s_writeBtn = Theme::button(scr, "", 192, BAR_H, Theme::ACCENT, true);
    lv_obj_set_pos(s_writeBtn, 122, BAR_Y);
    s_writeLabel = lv_obj_get_child(s_writeBtn, 0);
    lv_obj_set_style_text_font(s_writeLabel, &my_font_14, 0);
    lv_obj_set_style_text_align(s_writeLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(s_writeBtn, [](lv_event_t *) { writeSelected(); }, LV_EVENT_CLICKED, nullptr);


    // Score table (lower half in the result phase; parked below the screen while rolling)
    buildRows();
    int players = s_game.playerCount();
    s_table = lv_table_create(scr);
    lv_obj_set_pos(s_table, 0, SCR_H);
    lv_obj_set_size(s_table, SCR_W, SCR_H - TABLE_Y);
    lv_table_set_column_count(s_table, players + 1);
    lv_table_set_row_count(s_table, s_rowCount);
    int32_t colW = LV_MAX(52, (SCR_W - CAT_COL_W - 4) / players);
    if (colW > 100) colW = 100;
    lv_table_set_column_width(s_table, 0, CAT_COL_W);
    for (int p = 0; p < players; p++) lv_table_set_column_width(s_table, p + 1, colW);
    lv_obj_set_style_bg_color(s_table, Theme::c(Theme::BG), 0);
    lv_obj_set_style_border_width(s_table, 2, 0);
    lv_obj_set_style_border_side(s_table, LV_BORDER_SIDE_TOP, 0);   // Clear divider to the dice
    lv_obj_set_style_border_color(s_table, Theme::c(Theme::ACCENT), 0);
    lv_obj_set_style_radius(s_table, 0, 0);
    lv_obj_set_style_pad_all(s_table, 0, 0);
    lv_obj_set_style_text_font(s_table, &my_font_14, LV_PART_ITEMS);
    lv_obj_set_style_pad_top(s_table, 4, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(s_table, 4, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(s_table, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_right(s_table, 4, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_table, Theme::c(Theme::LINE), LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_table, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_side(s_table, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS);
    for (int c = 0; c <= players; c++) lv_table_set_cell_ctrl(s_table, 0, c, LV_TABLE_CELL_CTRL_TEXT_CROP);
    lv_obj_add_event_cb(s_table, tableDrawEvent, LV_EVENT_DRAW_TASK_ADDED, nullptr);
    lv_obj_add_flag(s_table, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
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
    Theme::header(scr, "Resultat");
    lv_obj_t *var = Theme::label(scr, Yatzy::variantName(s_game.variant(), s_game.mode()), &my_font_14, Theme::TEAL);
    lv_obj_align(var, LV_ALIGN_TOP_RIGHT, -12, 14);

    int order[YATZY_MAX_PLAYERS], places[YATZY_MAX_PLAYERS];
    int n = s_game.ranking(order, places);
    lv_obj_t *card = Theme::card(scr, 310, 360);
    lv_obj_set_pos(card, 5, 54);
    static const uint32_t MEDAL[3] = {Theme::GOLD, 0xC9D3DE, 0xD08A4A};
    for (int i = 0; i < n; i++) {
        const YatzyPlayer &p = s_game.player(order[i]);
        int32_t y = i * 42;
        lv_obj_t *badge = lv_obj_create(card);
        lv_obj_remove_style_all(badge);
        lv_obj_set_size(badge, 32, 32);
        lv_obj_set_pos(badge, 0, y);
        lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(badge, Theme::c(places[i] <= 3 ? MEDAL[places[i] - 1] : Theme::PANEL_RAISED), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        char num[6];
        snprintf(num, sizeof(num), "%d", places[i]);
        lv_obj_t *nl = Theme::label(badge, num, &my_font_18, places[i] <= 3 ? Theme::BG : Theme::TEXT);
        lv_obj_center(nl);
        lv_obj_t *name = Theme::label(card, p.name, &my_font_18, places[i] == 1 ? Theme::GOLD : Theme::TEXT);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_width(name, 170);
        lv_obj_set_pos(name, 42, y + 5);
        char pts[12];
        snprintf(pts, sizeof(pts), "%d", s_game.total(order[i]));
        lv_obj_t *pl = Theme::label(card, pts, &my_font_24);
        lv_obj_align(pl, LV_ALIGN_TOP_RIGHT, 0, y + 2);
    }

    lv_obj_t *again = Theme::button(scr, LV_SYMBOL_REFRESH " Spela igen", 150, 46, Theme::ACCENT, true);
    lv_obj_set_pos(again, 160, 424);
    lv_obj_add_event_cb(again, [](lv_event_t *) { s_game.restartSamePlayers(); showGame(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *menu = Theme::button(scr, LV_SYMBOL_HOME " Meny", 140, 46, Theme::MUTED);
    lv_obj_set_pos(menu, 10, 424);
    lv_obj_add_event_cb(menu, [](lv_event_t *) { showMenu(); }, LV_EVENT_CLICKED, nullptr);

    Rgb::effectFlash(255, 200, 60, 1200);
    GameLive::publish(s_game, true, false, true);   // Final scores stay on the live page
    Theme::loadScreen(scr);
}

}  // namespace UiYatzy
