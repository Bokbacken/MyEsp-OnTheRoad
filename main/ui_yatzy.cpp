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
#include "dice_cup.h"
#include "game_live.h"
#include "game_yatzy.h"
#include "my_charwheel.h"
#include "my_wifi.h"
#include "ui_clock.h"
#include "ui_settings.h"
#include "my_theme.h"
#include "ui_wifi.h"
#include "lang_game.h"

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
    Str sub;
    uint32_t color;
};

static const VariantChoice CHOICES[4] = {
    {YatzyVariant::STANDARD, YatzyMode::NORMAL, Str::MENU_SUB_YATZY, Theme::ACCENT},
    {YatzyVariant::MAXI, YatzyMode::NORMAL, Str::MENU_SUB_MAXI, Theme::TEAL},
    {YatzyVariant::STANDARD, YatzyMode::ORDERED, Str::MENU_SUB_YATZY_ORDER, Theme::ACCENT},
    {YatzyVariant::MAXI, YatzyMode::ORDERED, Str::MENU_SUB_MAXI_ORDER, Theme::TEAL},
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

    lv_obj_t *hint = Theme::label(scr, tr(Str::MENU_CHOOSE_GAME), &my_font_14, Theme::MUTED);
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
        lv_obj_set_clickable(stripe, false);
        lv_obj_t *t = Theme::label(b, trVariant(CHOICES[i].v, CHOICES[i].m), &my_font_24);
        lv_obj_set_pos(t, 28, 14);
        lv_obj_t *s = Theme::label(b, tr(CHOICES[i].sub), &my_font_14, Theme::MUTED);
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
static bool s_web[YATZY_MAX_PLAYERS];   // Seat is played from a phone via /play (v0.5.2)
static uint16_t s_code = 0;             // 4-digit game code of the current game
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
        s_web[i] = false;
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
        s_web[k] = s_web[k + 1];
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
        lv_obj_set_size(row, 192, 34);
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
        lv_obj_set_width(name, s_pids[i] ? 150 : 112);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 36, 0);
        if (!s_pids[i]) {   // Guests are not saved in the registry
            lv_obj_t *tag = Theme::label(row, tr(Str::PL_GUEST_TAG), &my_font_14, Theme::GOLD);
            lv_obj_align(tag, LV_ALIGN_RIGHT_MID, -6, 0);
        }
        lv_obj_add_event_cb(row, playerRowEvent, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        // Web toggle: this seat is played from a phone (/play?code=...)
        lv_obj_t *web = Theme::button(s_playerList, LV_SYMBOL_WIFI, 44, 34, s_web[i] ? Theme::TEAL : Theme::LINE);
        lv_obj_set_pos(web, 198, i * 38);
        lv_obj_add_event_cb(web, [](lv_event_t *e) {
            int k = (int)(intptr_t)lv_event_get_user_data(e);
            if (k < 0 || k >= s_nameCount) return;
            s_web[k] = !s_web[k];
            rebuildPlayerList();
        }, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *del = Theme::button(s_playerList, LV_SYMBOL_CLOSE, 44, 34, Theme::RED);
        lv_obj_set_pos(del, 248, i * 38);
        lv_obj_add_event_cb(del, playerDeleteEvent, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    if (s_nameCount > 0 && s_nameCount < YATZY_MAX_PLAYERS) {
        lv_obj_t *hint = Theme::label(s_playerList, trp(LV_SYMBOL_WIFI " ", Str::PL_WEB_HINT), &my_font_14, Theme::MUTED);
        lv_obj_set_pos(hint, 200, s_nameCount * 38 + 6);
        lv_obj_set_width(hint, 92);
        lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_WRAP);
    }
    if (s_nameCount == 0) {
        lv_obj_t *empty = Theme::label(s_playerList, tr(Str::PL_ADD_AT_LEAST_ONE), &my_font_14, Theme::MUTED);
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
        snprintf(s_names[0], YATZY_NAME_CAP, tr(Str::PL_DEFAULT_NAME), 1);
        snprintf(s_names[1], YATZY_NAME_CAP, tr(Str::PL_DEFAULT_NAME), 2);
        s_pids[0] = s_pids[1] = 0;
        s_web[0] = s_web[1] = false;
        s_nameCount = 2;
    }
    lv_obj_t *scr = Theme::screen();
    Theme::header(scr, tr(Str::PL_TITLE));
    lv_obj_t *var = Theme::label(scr, trVariant(s_variant, s_mode), &my_font_14, Theme::TEAL);
    lv_obj_align(var, LV_ALIGN_TOP_RIGHT, -12, 14);

    lv_obj_t *card = Theme::card(scr, 310, 326);
    lv_obj_set_pos(card, 5, 52);
    lv_obj_set_scrollable(card, false);
    s_playerList = lv_obj_create(card);
    lv_obj_remove_style_all(s_playerList);
    lv_obj_set_size(s_playerList, 292, 306);
    lv_obj_set_scrollable(s_playerList, false);

    s_addBtn = Theme::button(scr, trp(LV_SYMBOL_PLUS " ", Str::PL_ADD), 300, 42, Theme::TEAL);
    lv_obj_set_pos(s_addBtn, 10, 384);
    lv_obj_add_event_cb(s_addBtn, [](lv_event_t *) { showPicker(-1); }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *back = Theme::button(scr, trp(LV_SYMBOL_LEFT " ", Str::BTN_BACK), 140, 42, Theme::MUTED);
    lv_obj_set_pos(back, 10, 432);
    lv_obj_add_event_cb(back, [](lv_event_t *) { showMenu(); }, LV_EVENT_CLICKED, nullptr);

    s_startBtn = Theme::button(scr, trp("", Str::PL_START, " " LV_SYMBOL_RIGHT), 150, 42, Theme::ACCENT, true);
    lv_obj_set_pos(s_startBtn, 160, 432);
    lv_obj_add_event_cb(s_startBtn, [](lv_event_t *) {
        const AppConfig &cfg = Config::get();
        s_game.reset(s_variant, s_mode, cfg.maxiSavedRolls);
        s_game.clearPlayers();   // reset() keeps the roster; rebuild it from the player list
        s_game.setRng(rngDie, nullptr);
        for (int i = 0; i < s_nameCount; i++) s_game.addPlayer(s_names[i]);
        s_game.start();
        s_code = (uint16_t)rnd(1000, 9999);   // Web play code (/play?code=XXXX), kept for "Spela igen"
        GameLive::setSeats(s_code, s_web, s_pids, s_nameCount);
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
        CharWheel::setHint(s_wheel, trPlayerErr(r));
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
        CharWheel::setHint(s_wheel, tr(Str::PL_NAME_EMPTY));
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
    Theme::header(scr, tr(slot < 0 ? Str::PICK_CHOOSE : Str::PICK_CHANGE));
    char buf[48];
    snprintf(buf, sizeof(buf), tr(Str::PICK_COUNT), total, PLAYER_MAX);
    lv_obj_t *cnt = Theme::label(scr, buf, &my_font_14, Theme::MUTED);
    lv_obj_align(cnt, LV_ALIGN_TOP_RIGHT, -12, 14);

    lv_obj_t *add = Theme::button(scr, trp(LV_SYMBOL_PLUS " ", Str::PICK_NEW), 170, 42, Theme::TEAL, true);
    lv_obj_set_pos(add, 10, 52);
    lv_obj_add_event_cb(add, [](lv_event_t *) { openWheel(tr(Str::PICK_NEW_SAVED), "", newPlayerEntered); },
                        LV_EVENT_CLICKED, nullptr);
    if (total >= PLAYER_MAX) lv_obj_add_state(add, LV_STATE_DISABLED);
    lv_obj_t *search = Theme::button(scr, s_pickFilter[0] ? trp(LV_SYMBOL_CLOSE " ", Str::PICK_SEARCH) : tr(Str::PICK_SEARCH_DOTS), 120, 42,
                                     s_pickFilter[0] ? Theme::GOLD : Theme::LINE);
    lv_obj_set_pos(search, 190, 52);
    lv_obj_add_event_cb(search, [](lv_event_t *) {
        if (s_pickFilter[0]) {   // Clear the active filter
            s_pickFilter[0] = 0;
            showPicker(s_pickSlot);
        } else {
            openWheel(tr(Str::PICK_SEARCH_PLAYER), "", filterEntered);
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
                                tr(total == 0          ? Str::PICK_EMPTY
                                   : s_pickFilter[0] ? Str::PICK_NO_MATCH
                                                     : Str::PICK_ALL_SEATED));
        lv_obj_set_style_text_color(t, Theme::c(Theme::MUTED), LV_PART_ITEMS);
    } else {
        lv_table_set_row_count(t, rows);
        for (int r = 0; r < rows; r++) {
            const PlayerRec *p = s_pickDb->get(s_pickIds[r]);
            lv_table_set_cell_value(t, r, 0, p ? p->name : "?");
        }
        lv_obj_add_event_cb(t, pickTableEvent, LV_EVENT_VALUE_CHANGED, nullptr);
    }

    lv_obj_t *back = Theme::button(scr, trp(LV_SYMBOL_LEFT " ", Str::BTN_BACK), 140, 42, Theme::MUTED);
    lv_obj_set_pos(back, 10, 432);
    lv_obj_add_event_cb(back, [](lv_event_t *) { s_pickFilter[0] = 0; showPlayers(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *guest = Theme::button(scr, tr(Str::PICK_GUEST), 160, 42, Theme::GOLD);
    lv_obj_set_pos(guest, 150, 432);
    lv_obj_add_event_cb(guest, [](lv_event_t *) {
        char initial[YATZY_NAME_CAP];
        snprintf(initial, sizeof(initial), tr(Str::PICK_GUEST_NAME_FMT), s_nameCount + 1);
        openWheel(tr(Str::PICK_GUEST_NAME), initial, guestEntered);
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
    int32_t cx, cy;          // Quadratic Bezier control point of the flight path
    int hops;                // Bounces on the felt
    float hopPx;             // Bounce height
    int32_t scale0;          // Scale at the start of the flight (dice leave the cup at ~70 %)
    // Dice cup
    bool inCup;              // Rattling in the cup (position driven by the cup physics)
    int32_t gx, gy, gScale;  // "Gather" start (glides from here into the cup)
    uint32_t gStartMs;
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

// Current seat is played from the web: local dice / cup / table input is blocked.
static bool webTurn()
{
    const int p = s_game.currentPlayer();
    return p >= 0 && p < YATZY_MAX_PLAYERS && s_web[p] && s_game.started() && !s_game.finished();
}

static void refreshTable();
static void refreshActions();
static void updateWebOverlay();
static void refreshSuggestions(bool animateIn);
static void setPhase(Phase p, bool animate);
static void showCup(bool animate);
static void hideCup(bool moveBack, bool animate);
static void cupAdopt(int i, uint32_t gatherStartMs);
static void cupKill();
static void setVisible(lv_obj_t *o, bool on);
static lv_obj_t *plainBox(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);

// --- Dice cup ("bägaren") state ----------------------------------------------------------
// Roll phase with rolls left: the lower part of the screen (where the suggestion card and the
// buttons sit) becomes a leather dice cup seen from above. The free dice rattle inside it;
// shake with the finger and swipe up (or tap) to throw. Gesture/physics maths: dice_cup.h.
static constexpr int32_t CUP_AREA_Y = 326, CUP_AREA_H = SCR_H - CUP_AREA_Y;
static constexpr int32_t CUP_D = 146, CUP_R = CUP_D / 2;
static constexpr int32_t CUP_CX = SCR_W / 2, CUP_CY = CUP_AREA_Y + 4 + CUP_R;   // (160, 403)
static constexpr int32_t CUP_LIP_D = 124, CUP_BOTTOM_D = 102;
static constexpr float CUP_FOLLOW = 0.35f;        // Cup offset = 35 % of the finger offset ...
static constexpr float CUP_FOLLOW_MAX = 18.0f;    // ... clamped to +-18 px
static constexpr float CUP_INERTIA = 1.6f;        // Dice velocity kick per cup velocity change
static constexpr int32_t CUP_DIE_SCALE = 180;     // Dice in the cup: ~70 %
static constexpr int32_t CUP_PRESS_SLACK = 14;    // Press must start within the cup (+14 px)
static constexpr uint32_t CUP_TICK_MS = 16, CUP_SHOW_MS = 260, CUP_GATHER_MS = 300;
static constexpr uint32_t CUP_GATHER_STAGGER_MS = 35, CUP_FLASH_GAP_MS = 70;

static lv_obj_t *s_cupArea, *s_cupRim, *s_cupLip, *s_cupBottom;
static lv_obj_t *s_cupCount, *s_cupHint, *s_cupChoose, *s_cupHide;
static lv_timer_t *s_cupTimer = nullptr;
static bool s_cupActive = false, s_cupPressed = false, s_cupShaking = false, s_cupHintDone = false;
static DiceCup::ShakeTracker s_shake;
static DiceCup::CupParams s_cupParams;
static DiceCup::Body s_body[YATZY_MAX_DICE];
static int s_bodyDie[YATZY_MAX_DICE];
static int s_bodyCount = 0;
static float s_cupOffX = 0, s_cupOffY = 0, s_cupVx = 0, s_cupVy = 0;
static uint32_t s_cupLastMs = 0, s_cupFlashMs = 0;

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
        d.dragging = s_phase == Phase::ROLL && !s_rolling && s_game.hasRolled() && !s_game.finished() && !webTurn();
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
                if (s_cupActive) {   // The cup is out: the die drops into it instead
                    cupAdopt(i, lv_tick_get());
                    refreshActions();
                    return;
                }
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
    lv_obj_set_clickable(o, true);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_scroll_chain(o, false);
    lv_obj_set_gesture_bubble(o, false);
    lv_obj_set_ext_click_area(o, 4);
    d.jx = d.jy = 0;
    d.dragging = d.rolling = d.inCup = false;
    d.cx = d.cy = 0;
    d.hops = 3;
    d.hopPx = 10.0f;
    d.scale0 = ZOOM_100;
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
    // Hops: decaying bounces (|sin| arcs) -> scale up (towards the viewer) and lift
    float hop = sinf((float)d.hops * (float)M_PI * p);
    if (hop < 0) hop = -hop;
    hop *= q * sqrtf(q);
    // Quadratic Bezier start -> control -> landing spot (control = midpoint for a straight path)
    const float u = 1.0f - e;
    const float bx = u * u * (float)d.sx + 2.0f * u * e * (float)d.cx + e * e * (float)d.tx;
    const float by = u * u * (float)d.sy + 2.0f * u * e * (float)d.cy + e * e * (float)d.ty - hop * d.hopPx;
    lv_obj_set_pos(d.obj, LV_CLAMP(0, (int32_t)bx, SCR_W - s_dieSize), LV_CLAMP(0, (int32_t)by, SCR_H - s_dieSize));
    const float grow = p * 3.0f > 1.0f ? 1.0f : p * 3.0f;      // Leaves the cup small, full size quickly
    const int32_t base = d.scale0 + (int32_t)((float)(ZOOM_100 - d.scale0) * grow);
    lv_image_set_scale(d.obj, (uint32_t)(base + (int32_t)(hop * 64.0f)));
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

static uint32_t rollBaseMs()
{
    uint32_t ms = Config::get().rollAnimMs;   // Default 800 ms
    return ms < 300 ? 300 : ms;
}

// Rolls the free dice. tp == nullptr: classic throw from the side of the felt ([Kasta] button
// mode). With tp the dice fly out of the cup along a path shaped by the gesture.
static void startRoll(const DiceCup::ThrowParams *tp)
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
    const uint32_t ms = tp ? tp->durationMs : rollBaseMs();
    if (count == 0) {                          // Everything held: nothing moves
        if (s_cupActive) hideCup(false, true);
        afterRoll();
        return;
    }
    Rgb::effectDiceRoll(ms + 100);
    // Classic: all dice are thrown in from the same side (random left/right). Cup: the spin
    // direction of the gesture decides the roll direction.
    const int32_t dir = tp ? (tp->spinDir < 0 ? -1 : 1) : (rnd(0, 1) == 0 ? 1 : -1);
    const bool fromLeft = dir > 0;
    int k = 0;
    for (int i = 0; i < s_game.diceCount(); i++) {
        if (!rollingMask[i]) continue;
        DieUi &d = s_dice[i];
        const bool cup = tp && d.inCup;
        const int32_t curX = lv_obj_get_x(d.obj), curY = lv_obj_get_y(d.obj);
        const int32_t curRot = lv_image_get_rotation(d.obj);
        const int32_t curScale = lv_image_get_scale(d.obj);
        stopDieAnims(d);
        d.jx = (int32_t)rnd(-scatterX(), scatterX());
        d.jy = (int32_t)rnd(-scatterY(), scatterY());
        d.rolling = true;
        targetPos(i, d.tx, d.ty);
        d.lastSeg = -1;
        uint32_t stagger;
        if (tp) {
            d.sx = cup ? curX : CUP_CX - s_dieSize / 2 + rnd(-30, 30);
            d.sy = cup ? curY : CUP_CY - s_dieSize / 2;
            d.scale0 = cup ? curScale : CUP_DIE_SCALE;
            // Launch direction +-0.25 rad * spread, reach and curve vary a little per die
            const float a = tp->angle + tp->spread * (float)rnd(-100, 100) / 400.0f;
            const float reach = tp->reachPx * (0.85f + 0.3f * (float)rnd(0, 100) / 100.0f);
            const float curve = tp->curve * (0.7f + 0.6f * (float)rnd(0, 100) / 100.0f);
            d.cx = d.sx + (int32_t)(sinf(a) * reach + cosf(a) * curve);
            d.cy = d.sy + (int32_t)(-cosf(a) * reach + sinf(a) * curve);
            d.hops = tp->hops;
            d.hopPx = tp->hopPx;
            // Total spin follows the gesture; it ends at 0 deg and starts at the current angle
            const int32_t spinBase = dir * (int32_t)(tp->turns * 3600.0f + tp->spread * (float)rnd(0, 900));
            const int32_t a0 = curRot % 3600;
            d.spin = spinBase - ((spinBase - a0) % 3600 + 3600) % 3600;
            stagger = (uint32_t)((float)(k * ROLL_STAGGER_MS) * (0.5f + tp->spread));
        } else {
            d.sx = fromLeft ? rnd(0, 24) : SCR_W - s_dieSize - rnd(0, 24);
            d.sy = THROW_TOP + rnd(0, LV_MAX(0, THROW_H - s_dieSize));
            d.cx = (d.sx + d.tx) / 2;
            d.cy = (d.sy + d.ty) / 2;
            d.hops = 3;
            d.hopPx = 10.0f;
            d.scale0 = ZOOM_100;
            d.spin = dir * (4500 + rnd(0, 3600));   // Roll direction follows the travel; 1.25 .. 2.25 turns
            stagger = (uint32_t)k * ROLL_STAGGER_MS;
        }
        d.startMs = stagger;
        d.finishMs = ms - (uint32_t)(count - 1 - k) * ROLL_STAGGER_MS / 2;
        if (d.finishMs < d.startMs + 200) d.finishMs = d.startMs + 200;
        k++;
        lv_obj_move_foreground(d.obj);
        styleDie(i);
        rollFrame(i, 0.0f);
    }
    s_rolling = 1;
    if (s_cupActive) hideCup(false, true);   // The cup slides away while the dice fly
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

// --- Dice cup -----------------------------------------------------------------------------
static bool cupMode() { return Config::get().throwMode == 0; }

static bool cupWanted()
{
    return cupMode() && s_phase == Phase::ROLL && !s_rolling && s_game.canRoll() && !s_game.finished() && !webTurn();
}

static void cupSetShaking(bool on)
{
    if (s_cupShaking == on) return;
    s_cupShaking = on;
    GameLive::setShaking(on);   // Web live view: "skakar bägaren..."
}

static void styleCupDie(int i)
{
    lv_obj_t *o = s_dice[i].obj;
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_shadow_opa(o, LV_OPA_0, 0);   // The box shadow would not rotate with the sprite
    lv_obj_set_style_image_opa(o, LV_OPA_COVER, 0);
}

// Die i joins the cup: it glides from where it is into the cup starting at gatherStartMs.
static void cupAdopt(int i, uint32_t gatherStartMs)
{
    DieUi &d = s_dice[i];
    if (d.inCup || s_bodyCount >= YATZY_MAX_DICE) return;
    lv_anim_delete(d.obj, xExec);
    lv_anim_delete(d.obj, yExec);
    lv_anim_delete(d.obj, zoomExec);
    d.inCup = true;
    d.dragging = false;
    d.gx = lv_obj_get_x(d.obj);
    d.gy = lv_obj_get_y(d.obj);
    d.gScale = lv_image_get_scale(d.obj);
    d.gStartMs = gatherStartMs;
    lv_obj_set_clickable(d.obj, false);   // Presses go through to the cup
    if (d.face <= 0) setFace(d, rnd(1, 6));
    styleCupDie(i);
    s_body[s_bodyCount] = DiceCup::Body{(float)rnd(-10, 10), (float)rnd(-10, 10), (float)rnd(-150, 150),
                                        (float)rnd(-150, 150), (float)rnd(0, 359), (float)rnd(-240, 240)};
    s_bodyDie[s_bodyCount++] = i;
}

// Positions the cup visuals and the dice inside it (no allocations).
static void cupPlace(uint32_t now)
{
    const int32_t ox = (int32_t)s_cupOffX, oy = (int32_t)s_cupOffY;
    lv_obj_set_pos(s_cupRim, CUP_CX - CUP_R + ox, CUP_CY - CUP_AREA_Y - CUP_R + oy);
    // Parallax of the inner lip / bottom against the rim reads as a slight tilt towards the finger
    lv_obj_set_pos(s_cupLip, (CUP_D - CUP_LIP_D) / 2 - ox / 6, (CUP_D - CUP_LIP_D) / 2 - oy / 6);
    lv_obj_set_pos(s_cupBottom, (CUP_LIP_D - CUP_BOTTOM_D) / 2 - ox / 3, (CUP_LIP_D - CUP_BOTTOM_D) / 2 - oy / 3);
    const int32_t areaDy = lv_obj_get_y(s_cupArea) - CUP_AREA_Y;   // Cup sliding in / out
    const float cx = (float)CUP_CX + s_cupOffX - (float)ox / 2.0f;   // Centre of the felt bottom
    const float cy = (float)(CUP_CY + areaDy) + s_cupOffY - (float)oy / 2.0f;
    for (int k = 0; k < s_bodyCount; k++) {
        DieUi &d = s_dice[s_bodyDie[k]];
        const DiceCup::Body &b = s_body[k];
        int32_t x = (int32_t)(cx + b.x) - s_dieSize / 2;
        int32_t y = (int32_t)(cy + b.y) - s_dieSize / 2;
        int32_t scale = CUP_DIE_SCALE;
        int32_t rot = (int32_t)(b.ang * 10.0f);
        const int32_t el = (int32_t)(now - d.gStartMs);
        if (el < (int32_t)CUP_GATHER_MS) {
            float g = el <= 0 ? 0.0f : (float)el / (float)CUP_GATHER_MS;
            g = 1.0f - (1.0f - g) * (1.0f - g);   // Ease-out
            x = d.gx + (int32_t)((float)(x - d.gx) * g);
            y = d.gy + (int32_t)((float)(y - d.gy) * g);
            scale = d.gScale + (int32_t)((float)(scale - d.gScale) * g);
            rot = (int32_t)((float)rot * g);
        }
        lv_obj_set_pos(d.obj, x, y);
        lv_image_set_scale(d.obj, (uint32_t)scale);
        lv_image_set_rotation(d.obj, rot);
    }
}

static void cupTick(lv_timer_t *t)
{
    const uint32_t gen = (uint32_t)(uintptr_t)lv_timer_get_user_data(t);
    if (gen != s_gen || !s_cupActive) {
        if (s_cupTimer == t) s_cupTimer = nullptr;
        lv_timer_delete(t);
        return;
    }
    const uint32_t now = lv_tick_get();
    float dt = (float)(now - s_cupLastMs) / 1000.0f;
    s_cupLastMs = now;
    if (dt <= 0.0f) return;
    if (dt > 0.05f) dt = 0.05f;
    // The cup follows the finger a little (smoothed); its acceleration throws the dice around
    float tx = 0.0f, ty = 0.0f;
    if (s_cupPressed) {
        tx = LV_CLAMP(-CUP_FOLLOW_MAX, (s_shake.lastX() - (float)CUP_CX) * CUP_FOLLOW, CUP_FOLLOW_MAX);
        ty = LV_CLAMP(-CUP_FOLLOW_MAX, (s_shake.lastY() - (float)CUP_CY) * CUP_FOLLOW, CUP_FOLLOW_MAX);
    }
    const float k = dt * 16.0f > 1.0f ? 1.0f : dt * 16.0f;
    const float nx = s_cupOffX + (tx - s_cupOffX) * k, ny = s_cupOffY + (ty - s_cupOffY) * k;
    const float vx = (nx - s_cupOffX) / dt, vy = (ny - s_cupOffY) / dt;
    const float dvx = vx - s_cupVx, dvy = vy - s_cupVy;
    s_cupOffX = nx;
    s_cupOffY = ny;
    s_cupVx = vx;
    s_cupVy = vy;
    const DiceCup::StepResult r =
        DiceCup::step(s_body, s_bodyCount, dt, -dvx * CUP_INERTIA, -dvy * CUP_INERTIA, s_cupParams);
    for (int b = 0; b < s_bodyCount; b++) {
        if (!(r.hitMask & (1u << b))) continue;
        DieUi &d = s_dice[s_bodyDie[b]];
        int f = rnd(1, 5);
        if (f >= d.face) f++;   // A knock shows a new face
        setFace(d, f);
    }
    cupPlace(now);
    // LED: short warm flash per knock, brighter for harder impacts
    if (r.maxImpact >= s_cupParams.impactSpeed && now - s_cupFlashMs >= CUP_FLASH_GAP_MS) {
        s_cupFlashMs = now;
        float i = r.maxImpact / 700.0f;
        i = i < 0.25f ? 0.25f : (i > 1.0f ? 1.0f : i);
        Rgb::effectFlash((uint8_t)(255.0f * i), (uint8_t)(140.0f * i), (uint8_t)(30.0f * i), 60);
    }
}

static void cupThrow(const DiceCup::ThrowParams &tp)
{
    if (!s_cupActive || s_rolling || s_phase != Phase::ROLL || !s_game.canRoll()) return;
    s_cupHintDone = true;
    startRoll(&tp);
}

static void cupEvent(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    const uint32_t now = lv_tick_get();
    if (code == LV_EVENT_PRESSED) {
        if (!s_cupActive || s_rolling) return;
        const float dx = (float)p.x - ((float)CUP_CX + s_cupOffX), dy = (float)p.y - ((float)CUP_CY + s_cupOffY);
        const float rr = (float)(CUP_R + CUP_PRESS_SLACK);
        if (dx * dx + dy * dy > rr * rr) return;   // Next to the cup: ignore
        s_cupPressed = true;
        s_shake.begin((float)p.x, (float)p.y, now);
    } else if (code == LV_EVENT_PRESSING) {
        if (!s_cupPressed) return;
        s_shake.move((float)p.x, (float)p.y, now);
        if (s_shake.energy() > 30.0f) cupSetShaking(true);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (!s_cupPressed) return;
        s_cupPressed = false;
        // No extra sample at release: the velocity is that of the last real movement
        const DiceCup::ReleaseInfo r = DiceCup::decideRelease(s_shake, s_shake.lastY(), (float)(CUP_CY - CUP_R));
        s_shake.end();
        cupSetShaking(false);
        const int dir = rnd(0, 1) ? 1 : -1;
        if (r.kind == DiceCup::Release::TAP) cupThrow(DiceCup::defaultThrow(rollBaseMs(), dir));
        else if (r.kind == DiceCup::Release::THROW) cupThrow(DiceCup::mapThrow(r, &s_shake, rollBaseMs(), dir));
        // Release::NONE: the dice stay in the cup
    }
}

static void cupHiddenDone(lv_anim_t *a)
{
    if (!s_cupActive) lv_obj_set_hidden((lv_obj_t *)a->var, true);
}

static void showCup(bool animate)
{
    if (s_cupActive || !cupWanted()) return;
    s_cupActive = true;
    s_cupPressed = false;
    s_cupOffX = s_cupOffY = s_cupVx = s_cupVy = 0.0f;
    const float dieScaled = (float)s_dieSize * (float)CUP_DIE_SCALE / 256.0f;
    s_cupParams.wallR = (float)CUP_LIP_D / 2.0f - dieScaled * 0.55f;
    s_cupParams.dieR = dieScaled * 0.48f;
    // The dice to be thrown are collected into the cup (first roll: all of them)
    s_bodyCount = 0;
    const uint32_t now = lv_tick_get();
    const bool first = !s_game.hasRolled();
    int k = 0;
    for (int i = 0; i < s_game.diceCount(); i++) {
        if (first || !s_game.held(i)) cupAdopt(i, now + (uint32_t)(k++) * CUP_GATHER_STAGGER_MS);
    }
    DiceCup::layout(s_body, s_bodyCount, s_cupParams, (float)rnd(0, 628) / 100.0f);
    for (int b = 0; b < s_bodyCount; b++) {   // A little life from the start
        s_body[b].vx = (float)rnd(-120, 120);
        s_body[b].vy = (float)rnd(-120, 120);
        s_body[b].av = (float)rnd(-200, 200);
    }
    setVisible(s_rollBtn, false);
    setVisible(s_chooseBtn, false);
    lv_obj_set_hidden(s_sugPanel, true);
    const bool wasHidden = lv_obj_is_hidden(s_cupArea);
    lv_obj_set_hidden(s_cupArea, false);
    if (animate) {
        animInt(s_cupArea, yExec, wasHidden ? SCR_H - 40 : lv_obj_get_y(s_cupArea), CUP_AREA_Y, CUP_SHOW_MS);
        animInt(s_cupArea, opaExec, wasHidden ? (int32_t)LV_OPA_TRANSP : (int32_t)lv_obj_get_style_opa(s_cupArea, LV_PART_MAIN),
                LV_OPA_COVER, CUP_SHOW_MS);
    } else {
        lv_anim_delete(s_cupArea, yExec);
        lv_anim_delete(s_cupArea, opaExec);
        lv_obj_set_y(s_cupArea, CUP_AREA_Y);
        lv_obj_set_style_opa(s_cupArea, LV_OPA_COVER, 0);
    }
    s_cupLastMs = now;
    if (s_cupTimer) lv_timer_delete(s_cupTimer);
    s_cupTimer = lv_timer_create(cupTick, CUP_TICK_MS, (void *)(uintptr_t)s_gen);
    cupPlace(now);
    refreshActions();
}

// moveBack: free dice glide back to the throw zone (not when they are being thrown).
static void hideCup(bool moveBack, bool animate)
{
    if (s_cupTimer) {
        lv_timer_delete(s_cupTimer);
        s_cupTimer = nullptr;
    }
    s_cupPressed = false;
    s_shake.end();
    cupSetShaking(false);
    const bool was = s_cupActive;
    s_cupActive = false;
    for (int k = 0; k < s_bodyCount; k++) {
        const int i = s_bodyDie[k];
        DieUi &d = s_dice[i];
        d.inCup = false;
        lv_obj_set_clickable(d.obj, true);
        if (moveBack) {
            const int32_t sc = lv_image_get_scale(d.obj);
            lv_image_set_rotation(d.obj, 0);
            moveDie(i, animate);
            if (animate) animInt(d.obj, zoomExec, sc, ZOOM_100, MOVE_MS);
            else lv_image_set_scale(d.obj, ZOOM_100);
        }
    }
    s_bodyCount = 0;
    s_cupOffX = s_cupOffY = s_cupVx = s_cupVy = 0.0f;
    if (!was) return;
    if (animate) {
        animInt(s_cupArea, yExec, lv_obj_get_y(s_cupArea), SCR_H - 40, CUP_SHOW_MS, lv_anim_path_ease_in, cupHiddenDone);
        animInt(s_cupArea, opaExec, lv_obj_get_style_opa(s_cupArea, LV_PART_MAIN), LV_OPA_TRANSP, CUP_SHOW_MS);
    } else {
        lv_anim_delete(s_cupArea, yExec);
        lv_anim_delete(s_cupArea, opaExec);
        lv_obj_set_hidden(s_cupArea, true);
    }
    if (s_phase == Phase::ROLL) {   // Suggestions + [Kasta n] [Välj ruta] come back
        setVisible(s_rollBtn, true);
        setVisible(s_chooseBtn, true);
        lv_obj_set_hidden(s_sugPanel, false);
        refreshSuggestions(animate);
        refreshActions();
    }
}

// Leaving the game screen: stop the timer, no object access.
static void cupKill()
{
    if (s_cupTimer) {
        lv_timer_delete(s_cupTimer);
        s_cupTimer = nullptr;
    }
    s_cupActive = s_cupPressed = false;
    s_bodyCount = 0;
    s_shake.end();
    cupSetShaking(false);
}

static lv_obj_t *cupCircle(lv_obj_t *parent, int32_t d, uint32_t top, uint32_t bottom)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, Theme::c(top), 0);
    lv_obj_set_style_bg_grad_color(o, Theme::c(bottom), 0);
    lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t *cupButton(const char *text, int32_t x, int32_t y, uint32_t color, bool filled, lv_event_cb_t cb)
{
    lv_obj_t *b = Theme::button(s_cupArea, text, 78, 38, color, filled);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &my_font_14, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

// Leather cup seen from above: brown rim with a soft drop shadow and a gloss streak, a dark
// inner lip and a felt bottom. Plus the roll counter, the first-game hint and small buttons.
static void buildCup(lv_obj_t *scr)
{
    s_cupArea = plainBox(scr, 0, CUP_AREA_Y, SCR_W, CUP_AREA_H);
    lv_obj_set_clickable(s_cupArea, true);
    lv_obj_set_scroll_chain(s_cupArea, false);
    lv_obj_set_gesture_bubble(s_cupArea, false);
    lv_obj_add_event_cb(s_cupArea, cupEvent, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(s_cupArea, cupEvent, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_event_cb(s_cupArea, cupEvent, LV_EVENT_RELEASED, nullptr);
    lv_obj_add_event_cb(s_cupArea, cupEvent, LV_EVENT_PRESS_LOST, nullptr);

    s_cupRim = cupCircle(s_cupArea, CUP_D, 0x8A5A34, 0x3B2213);
    lv_obj_set_style_outline_width(s_cupRim, 2, 0);
    lv_obj_set_style_outline_color(s_cupRim, Theme::c(0x24140A), 0);
    lv_obj_set_style_outline_opa(s_cupRim, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(s_cupRim, 22, 0);
    lv_obj_set_style_shadow_offset_y(s_cupRim, 7, 0);
    lv_obj_set_style_shadow_color(s_cupRim, Theme::c(0x000000), 0);
    lv_obj_set_style_shadow_opa(s_cupRim, LV_OPA_60, 0);
    lv_obj_t *gloss = lv_obj_create(s_cupRim);
    lv_obj_remove_style_all(gloss);
    lv_obj_set_size(gloss, 74, 12);
    lv_obj_set_pos(gloss, (CUP_D - 74) / 2, 2);
    lv_obj_set_style_radius(gloss, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(gloss, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(gloss, 40, 0);
    lv_obj_set_clickable(gloss, false);
    s_cupLip = cupCircle(s_cupRim, CUP_LIP_D, 0x0C0603, 0x2A180C);
    s_cupBottom = cupCircle(s_cupLip, CUP_BOTTOM_D, 0x0B2629, 0x14403F);
    lv_obj_set_style_border_width(s_cupBottom, 3, 0);
    lv_obj_set_style_border_color(s_cupBottom, Theme::c(0x000000), 0);
    lv_obj_set_style_border_opa(s_cupBottom, LV_OPA_50, 0);

    s_cupCount = Theme::label(s_cupArea, "", &my_font_18, Theme::GOLD);
    lv_obj_set_width(s_cupCount, 80);
    lv_obj_set_pos(s_cupCount, 6, 8);
    s_cupHint = Theme::label(s_cupArea, tr(Str::CUP_HINT), &my_font_14, Theme::MUTED);
    lv_obj_set_width(s_cupHint, 80);
    lv_obj_set_pos(s_cupHint, 6, 56);
    cupButton(trp(LV_SYMBOL_REFRESH " ", Str::GAME_ROLL), SCR_W - 82, 8, Theme::ACCENT, true, [](lv_event_t *) {
        if (s_cupActive && !s_rolling) cupThrow(DiceCup::defaultThrow(rollBaseMs(), rnd(0, 1) ? 1 : -1));
    });
    s_cupChoose = cupButton(trp("", Str::GAME_CHOOSE, " " LV_SYMBOL_RIGHT), SCR_W - 82, CUP_AREA_H - 44, Theme::TEAL, false,
                            [](lv_event_t *) {
                                if (!s_rolling && s_game.hasRolled()) setPhase(Phase::RESULT, true);
                            });
    s_cupHide = cupButton(trp(LV_SYMBOL_DOWN " ", Str::GAME_TIDY), 4, CUP_AREA_H - 44, Theme::MUTED, false, [](lv_event_t *) {
        if (s_cupActive && !s_rolling) hideCup(true, true);   // Put the cup away to move dice
    });
    lv_obj_set_hidden(s_cupArea, true);
}

static void onRoll(lv_event_t *)
{
    if (webTurn()) return;
    if (s_rolling || s_phase != Phase::ROLL || !s_game.canRoll()) return;
    if (cupMode()) showCup(true);   // Cup mode: [Kasta n] brings the cup back
    else startRoll(nullptr);
}

// --- Suggestion card (roll phase) -------------------------------------------------------
static void sugChipEvent(lv_event_t *e)
{
    int k = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_rolling || k >= s_sugCount || webTurn()) return;
    s_selected = s_sug[k].category;
    setPhase(Phase::RESULT, true);
}

static void refreshSuggestions(bool animateIn)
{
    const char *hint = nullptr;
    if (!s_game.hasRolled()) hint = tr(Str::GAME_HINT_ROLL);
    else if (s_rolling) hint = tr(Str::GAME_HINT_ROLLING);
    else if (s_sugCount == 0) hint = tr(Str::GAME_HINT_NO_BOXES);
    int shown = hint && (s_rolling || !s_game.hasRolled()) ? 0 : s_sugCount;
    for (int k = 0; k < SUG_MAX; k++) {
        if (k >= shown) {
            lv_obj_set_hidden(s_sugChip[k], true);
            continue;
        }
        lv_obj_set_hidden(s_sugChip[k], false);
        lv_label_set_text(s_sugName[k], trCategory(s_sug[k].category, s_game.variant()));
        lv_label_set_text_fmt(s_sugScore[k], "%d", s_sug[k].score);
        bool best = k == 0;
        lv_obj_set_style_bg_color(s_sugChip[k], Theme::c(best ? 0x4D3F10 : Theme::PANEL), 0);
        lv_obj_set_style_border_color(s_sugChip[k], Theme::c(best ? Theme::GOLD : Theme::LINE), 0);
        lv_obj_set_style_text_color(s_sugName[k], Theme::c(best ? Theme::GOLD : Theme::TEXT), 0);
        lv_obj_set_style_text_color(s_sugScore[k], Theme::c(best ? Theme::GOLD : 0x6FB7C6), 0);
    }
    if (hint) {
        lv_label_set_text(s_sugHint, hint);
        lv_obj_set_hidden(s_sugHint, false);
    } else {
        lv_obj_set_hidden(s_sugHint, true);
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
    if (!keep) lv_obj_set_hidden(o, true);
}

static void setVisible(lv_obj_t *o, bool on)
{
    if (on) lv_obj_set_hidden(o, false);
    else lv_obj_set_hidden(o, true);
}

static void setPhase(Phase p, bool animate)
{
    s_phase = p;
    bool res = p == Phase::RESULT;
    uint32_t ms = animate ? PHASE_MS : 1;
    if (res && s_cupActive) hideCup(false, animate);   // The dice are moved below
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
        lv_obj_set_hidden(s_sugPanel, false);
        animInt(s_sugPanel, opaExec, lv_obj_get_style_opa(s_sugPanel, LV_PART_MAIN), LV_OPA_COVER, ms);
    }
    // Result-phase widgets
    setVisible(s_writeBtn, res);
    setVisible(s_backBtn, res && s_game.canRoll());
    lv_obj_set_hidden(s_table, false);
    animInt(s_table, yExec, lv_obj_get_y(s_table), res ? TABLE_Y : SCR_H, ms, lv_anim_path_ease_out,
            res ? nullptr : hideWhenDone);
    for (int i = 0; i < s_game.diceCount(); i++) {
        lv_obj_t *o = s_dice[i].obj;
        const int32_t sc = lv_image_get_scale(o);   // Dice coming out of the cup are smaller
        lv_anim_delete(o, zoomExec);
        lv_image_set_rotation(o, 0);
        if (animate && sc != ZOOM_100) animInt(o, zoomExec, sc, ZOOM_100, ms);
        else lv_image_set_scale(o, ZOOM_100);
        moveDie(i, animate);
    }
    if (res && s_selected == YatzyCategory::COUNT) s_selected = s_best;
    refreshTable();
    refreshActions();
    if (res) {
        int r = rowOfCategory(s_selected);
        lv_obj_scroll_to_y(s_table, r > 2 ? (r - 2) * 26 : 0, LV_ANIM_OFF);
    } else {
        showCup(animate);   // Cup mode: ready to shake (also after "< Kasta n")
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
    bool anyWeb = false;
    for (int i = 0; i < s_game.playerCount() && i < YATZY_MAX_PLAYERS; i++) anyWeb |= s_web[i];
    char rl[48];
    int n = snprintf(rl, sizeof(rl), tr(Str::GAME_ROUND), s_game.round(), s_game.totalRounds());
    if (anyWeb && n > 0 && n < (int)sizeof(rl)) {   // Web play: the code under the round
        rl[n++] = '\n';
        snprintf(rl + n, sizeof(rl) - n, tr(Str::GAME_CODE), (unsigned)s_code);
    }
    lv_label_set_text(s_roundLabel, rl);
    setPhase(Phase::ROLL, true);
    updateWebOverlay();
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
        cupKill();
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
        case ROW_CAT: lv_table_set_cell_value(s_table, r, 0, trCategory(ri.cat, s_game.variant())); break;
        case ROW_SUM: lv_table_set_cell_value(s_table, r, 0, tr(Str::GAME_SUM)); break;
        case ROW_BONUS: lv_table_set_cell_value(s_table, r, 0, tr(Str::GAME_BONUS)); break;
        case ROW_TOTAL: lv_table_set_cell_value(s_table, r, 0, tr(Str::GAME_TOTAL)); break;
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
    if (s_phase != Phase::RESULT || s_rolling || webTurn()) return;
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
        lv_label_set_text_fmt(s_rollLabel, trp(LV_SYMBOL_REFRESH " ", Str::GAME_ROLL_N_EXTRA), base, left - base);
    else
        lv_label_set_text_fmt(s_rollLabel, trp(LV_SYMBOL_REFRESH " ", Str::GAME_ROLL_N), left);
    if (canRoll) lv_obj_remove_state(s_rollBtn, LV_STATE_DISABLED);
    else lv_obj_add_state(s_rollBtn, LV_STATE_DISABLED);
    if (!s_rolling && s_game.hasRolled()) lv_obj_remove_state(s_chooseBtn, LV_STATE_DISABLED);
    else lv_obj_add_state(s_chooseBtn, LV_STATE_DISABLED);

    // Result phase: "back to rolling" only while rolls remain; Skriv takes the full width otherwise.
    bool back = s_phase == Phase::RESULT && canRoll;
    setVisible(s_backBtn, back);
    lv_label_set_text_fmt(s_backLabel, trp(LV_SYMBOL_LEFT " ", Str::GAME_BACK_ROLL_N), left);
    lv_obj_set_x(s_writeBtn, back ? 122 : 6);
    lv_obj_set_width(s_writeBtn, back ? 192 : SCR_W - 12);
    bool canWrite = !s_rolling && s_selected != YatzyCategory::COUNT && s_game.canScore(s_selected);
    if (canWrite) {
        lv_label_set_text_fmt(s_writeLabel, tr(Str::GAME_WRITE), s_game.potentialScore(s_selected),
                              trCategory(s_selected, s_game.variant()));
        lv_obj_remove_state(s_writeBtn, LV_STATE_DISABLED);
    } else {
        lv_label_set_text(s_writeLabel, tr(Str::GAME_PICK_BOX));
        lv_obj_add_state(s_writeBtn, LV_STATE_DISABLED);
    }
    if (s_cupArea) {
        if (s_game.variant() == YatzyVariant::MAXI && s_game.savedRollsRule() && left > base)
            lv_label_set_text_fmt(s_cupCount, tr(Str::GAME_ROLLS_LEFT_EXTRA), base, left - base);
        else
            lv_label_set_text_fmt(s_cupCount, tr(Str::GAME_ROLLS_LEFT), left);
        setVisible(s_cupChoose, s_game.hasRolled());
        setVisible(s_cupHide, s_game.hasRolled());
        setVisible(s_cupHint, !s_cupHintDone);
    }
    GameLive::publish(s_game, true, s_rolling != 0, s_phase == Phase::RESULT);   // Web live view
}

static void quitEvent(lv_event_t *e)
{
    lv_msgbox_close((lv_obj_t *)lv_event_get_user_data(e));
    s_gen++;
    cupKill();
    GameLive::setActive(false);
    showMenu();
}

static void onMenu(lv_event_t *)
{
    lv_obj_t *m = lv_msgbox_create(nullptr);
    lv_obj_set_style_text_font(m, &my_font_18, 0);
    lv_msgbox_add_title(m, tr(Str::GAME_QUIT_Q));
    lv_msgbox_add_text(m, tr(Str::GAME_QUIT_TEXT));
    lv_obj_set_width(m, 280);
    lv_obj_set_style_bg_color(m, Theme::c(Theme::PANEL_RAISED), 0);
    lv_obj_set_style_text_color(m, Theme::c(Theme::TEXT), 0);
    lv_obj_set_style_border_color(m, Theme::c(Theme::ACCENT), 0);
    lv_obj_set_style_border_width(m, 2, 0);
    lv_obj_center(m);
    lv_obj_t *bQuit = lv_msgbox_add_footer_button(m, tr(Str::GAME_QUIT));
    lv_obj_t *bCont = lv_msgbox_add_footer_button(m, tr(Str::BTN_CONTINUE));
    Theme::msgboxFonts(m);
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
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

// --- Web play (v0.5.2) -------------------------------------------------------------------
// While a web seat has the turn a transparent blocker swallows all touches below the header and
// a panel at the bottom says who we are waiting for (plus code + URL until a phone is bound).
// Commands from /ws/game arrive through GameLive's queue and are executed here, on the LVGL task.
static constexpr uint32_t WEB_TICK_MS = 50, WEB_WRITE_DELAY_MS = 700;
static lv_obj_t *s_webBlock = nullptr, *s_webPanel = nullptr, *s_webText = nullptr;
static int s_webShownConn = -1;   // Connection state the panel text was built for
static YatzyCategory s_webPending = YatzyCategory::COUNT;

static void updateWebOverlay()
{
    if (!s_webBlock) return;
    const bool on = webTurn();
    lv_obj_set_hidden(s_webBlock, !on);
    lv_obj_set_hidden(s_webPanel, !on);
    if (!on) {
        s_webShownConn = -1;
        return;
    }
    const int cur = s_game.currentPlayer();
    const char *name = s_game.player(cur).name;
    const int conn = GameLive::connections(cur) > 0 ? 1 : 0;
    s_webShownConn = conn;
    char line1[80], line2[96], all[180];
    snprintf(line1, sizeof(line1), tr(Str::GAME_WEB_WAIT), name);
    if (conn) snprintf(line2, sizeof(line2), tr(Str::GAME_WEB_CONNECTED), name);
    else snprintf(line2, sizeof(line2), tr(Str::GAME_WEB_CONNECT), MyWifi::ipStr(), (unsigned)s_code);
    snprintf(all, sizeof(all), "%s\n%s", line1, line2);
    lv_label_set_text(s_webText, all);
    lv_obj_move_foreground(s_webBlock);
    lv_obj_move_foreground(s_webPanel);
}

static void webWriteTimer(lv_timer_t *t)
{
    uint32_t gen = (uint32_t)(uintptr_t)lv_timer_get_user_data(t);
    YatzyCategory c = s_webPending;
    s_webPending = YatzyCategory::COUNT;
    if (gen != s_gen || !webTurn() || s_rolling || c == YatzyCategory::COUNT) return;
    s_selected = c;
    if (s_phase != Phase::RESULT) setPhase(Phase::RESULT, false);
    writeSelected();
}

static void webCommand(const GameLive::Cmd &c)
{
    if (!webTurn() || c.player != s_game.currentPlayer() || s_rolling || s_webPending != YatzyCategory::COUNT) return;
    switch (c.type) {
    case GameLive::CmdType::HOLD: {
        const int i = c.arg;
        if (s_phase != Phase::ROLL || !s_game.hasRolled() || i < 0 || i >= s_game.diceCount()) return;
        const bool hold = !s_game.held(i);
        s_game.setHold(i, hold);
        if (!hold) {   // Back to the throw zone at a new random spot
            s_dice[i].jx = (int32_t)rnd(-scatterX(), scatterX());
            s_dice[i].jy = (int32_t)rnd(-scatterY(), scatterY());
        }
        moveDie(i, true);
        refreshActions();
        break;
    }
    case GameLive::CmdType::ROLL:
        if (!s_game.canRoll()) return;
        if (s_phase == Phase::RESULT) setPhase(Phase::ROLL, false);
        startRoll(nullptr);   // Classic throw; the console shows the animation
        break;
    case GameLive::CmdType::CHOOSE: {
        const YatzyCategory cat = (YatzyCategory)c.arg;
        if (!s_game.hasRolled() || !s_game.canScore(cat)) return;
        // Show the table with the chosen box for a moment, then write it
        s_selected = cat;
        if (s_phase != Phase::RESULT) setPhase(Phase::RESULT, true);
        else refreshActions();
        lv_obj_invalidate(s_table);
        s_webPending = cat;
        lv_timer_t *t = lv_timer_create(webWriteTimer, WEB_WRITE_DELAY_MS, (void *)(uintptr_t)s_gen);
        lv_timer_set_repeat_count(t, 1);
        break;
    }
    }
}

static void webTimer(lv_timer_t *t)
{
    uint32_t gen = (uint32_t)(uintptr_t)lv_timer_get_user_data(t);
    if (gen != s_gen) {   // Game screen left
        lv_timer_delete(t);
        return;
    }
    GameLive::Cmd c;
    for (int n = 0; n < 4 && GameLive::popCmd(c); n++) {
        webCommand(c);
        if (gen != s_gen) return;   // The game ended (results screen)
    }
    if (webTurn()) {
        const int conn = GameLive::connections(s_game.currentPlayer()) > 0 ? 1 : 0;
        if (conn != s_webShownConn) updateWebOverlay();
    }
}

static void buildWebOverlay(lv_obj_t *scr)
{
    s_webBlock = plainBox(scr, 0, FELT_Y, SCR_W, SCR_H - FELT_Y);
    lv_obj_set_clickable(s_webBlock, true);   // Swallows touches (dice, cup, buttons, table)
    lv_obj_set_scroll_chain(s_webBlock, false);
    lv_obj_set_gesture_bubble(s_webBlock, false);
    s_webPanel = plainBox(scr, 8, SCR_H - 112, SCR_W - 16, 104);
    lv_obj_set_style_radius(s_webPanel, 14, 0);
    lv_obj_set_style_bg_color(s_webPanel, Theme::c(Theme::PANEL_RAISED), 0);
    lv_obj_set_style_bg_opa(s_webPanel, 235, 0);
    lv_obj_set_style_border_width(s_webPanel, 2, 0);
    lv_obj_set_style_border_color(s_webPanel, Theme::c(Theme::TEAL), 0);
    lv_obj_t *icon = Theme::label(s_webPanel, LV_SYMBOL_WIFI, &my_font_24, Theme::TEAL);
    lv_obj_set_pos(icon, 10, 10);
    s_webText = Theme::label(s_webPanel, "", &my_font_14);
    lv_obj_set_width(s_webText, SCR_W - 16 - 50);
    lv_label_set_long_mode(s_webText, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_pos(s_webText, 44, 8);
    lv_obj_set_hidden(s_webBlock, true);
    lv_obj_set_hidden(s_webPanel, true);
    s_webShownConn = -1;
    s_webPending = YatzyCategory::COUNT;
    GameLive::clearCmds();
    lv_timer_create(webTimer, WEB_TICK_MS, (void *)(uintptr_t)s_gen);
}

static void showGame()
{
    s_gen++;
    cupKill();
    s_cupArea = nullptr;
    s_webBlock = s_webPanel = s_webText = nullptr;
    s_cupHintDone = false;   // "Skaka och svep uppåt" once per game
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
    lv_obj_t *sugTitle = Theme::label(s_sugPanel, tr(Str::GAME_SUGGESTIONS), &my_font_14, Theme::MUTED);
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
        lv_obj_set_hidden(chip, true);
        s_sugChip[k] = chip;
    }

    // Roll phase buttons
    s_rollBtn = Theme::button(scr, "", 150, BTN_H, Theme::ACCENT, true);
    lv_obj_set_pos(s_rollBtn, 6, BTN_Y);
    s_rollLabel = lv_obj_get_child(s_rollBtn, 0);
    lv_obj_add_event_cb(s_rollBtn, onRoll, LV_EVENT_CLICKED, nullptr);
    s_chooseBtn = Theme::button(scr, trp("", Str::GAME_PICK_BOX_BTN, " " LV_SYMBOL_RIGHT), 152, BTN_H, Theme::TEAL);
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
    lv_obj_set_send_draw_task_events(s_table, true);
    lv_obj_add_event_cb(s_table, tableClickEvent, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_set_hidden(s_table, true);

    // Dice cup (roll phase, cup mode), below the dice
    buildCup(scr);

    // Dice last: children of the screen, on top of felt/nest/table so they can travel freely
    for (int i = 0; i < YATZY_MAX_DICE; i++) s_dice[i].obj = nullptr;
    for (int i = 0; i < s_game.diceCount(); i++) {
        createDie(scr, i);
        moveDie(i, false);
    }
    buildWebOverlay(scr);   // Above the dice: blocks local input on web turns

    Theme::loadScreen(scr);
    newTurnUi();
}

// ========================================================================================
// Results
// ========================================================================================
static void showResults()
{
    lv_obj_t *scr = Theme::screen();
    Theme::header(scr, tr(Str::RES_TITLE));
    lv_obj_t *var = Theme::label(scr, trVariant(s_game.variant(), s_game.mode()), &my_font_14, Theme::TEAL);
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

    lv_obj_t *again = Theme::button(scr, trp(LV_SYMBOL_REFRESH " ", Str::RES_AGAIN), 150, 46, Theme::ACCENT, true);
    lv_obj_set_pos(again, 160, 424);
    lv_obj_add_event_cb(again, [](lv_event_t *) { s_game.restartSamePlayers(); showGame(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *menu = Theme::button(scr, trp(LV_SYMBOL_HOME " ", Str::RES_MENU), 140, 46, Theme::MUTED);
    lv_obj_set_pos(menu, 10, 424);
    lv_obj_add_event_cb(menu, [](lv_event_t *) { showMenu(); }, LV_EVENT_CLICKED, nullptr);

    Rgb::effectFlash(255, 200, 60, 1200);
    GameLive::publish(s_game, true, false, true);   // Final scores stay on the live page
    Theme::loadScreen(scr);
}

}  // namespace UiYatzy
