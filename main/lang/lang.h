// lang.h - user interface strings (i18n) for the on-device UI and the web API messages.
//
// Usage:   lv_label_set_text(l, tr(Str::BTN_SAVE));
//          trp(LV_SYMBOL_SAVE " ", Str::BTN_SAVE)   -> prefix + text (LVGL symbols, icons)
//
// Every visible string has an ID in LANG_STR_IDS below; each language is a table of
// {Str::ID, "text"} pairs in its own file (lang_sv.cpp, lang_en.cpp, ...). Missing entries fall
// back to Swedish (the base language, always complete), so a partial translation still works.
// Pure C++ (no ESP-IDF): the language code is persisted by AppConfig (NVS key "lang") and the
// module is host-tested by tests/test_lang.cpp.
//
// Contributing a new language (e.g. German, code "de"):
//   1. Copy main/lang/lang_en.cpp to main/lang/lang_de.cpp (English is the easiest template).
//   2. Rename LANG_EN / DEF_EN to LANG_DE / DEF_DE, set the code "de" and the native name
//      "Deutsch", and translate the strings. Keep printf placeholders (%s, %d, %u, %lu, %04u)
//      in the same order and keep "\n" line breaks - the test checks the placeholders.
//      The fonts cover ASCII + Latin-1 (U+00A0-U+00FF) and a few symbols: no emoji, use "...".
//   3. Add `extern const LangDef DEF_DE;` and `&DEF_DE` to the LANGS table in lang.cpp.
//   4. Add "lang/lang_de.cpp" to main/CMakeLists.txt and to the test_lang build line.
//   5. Run tests/test_lang (see README) and open a pull request.
// New strings: add the ID to LANG_STR_IDS and a line to EVERY language file (at least lang_sv).
#pragma once

#include <stddef.h>
#include <stdint.h>

// clang-format off
#define LANG_STR_IDS(X) \
    X(BTN_CANCEL) \
    X(BTN_SAVE) \
    X(BTN_BACK) \
    X(SCREEN_TIMEOUT) \
    X(SCREEN_TIMEOUT_OPTIONS) \
    X(SCREEN_SLEEP_NOW) \
    X(SCREEN_SLEEP_HELP) \
    X(SET_SAVE_FAILED) \
    X(BOOT_I2C) \
    X(BOOT_CLOCK) \
    X(BOOT_STORAGE) \
    X(BOOT_WEB) \
    X(BOOT_WAITING) \
    X(BOOT_STARTING) \
    X(BOOT_TESTING) \
    X(BOOT_FAIL) \
    X(BOOT_SKIPPED) \
    X(WDAY_SUN) \
    X(WDAY_MON) \
    X(WDAY_TUE) \
    X(WDAY_WED) \
    X(WDAY_THU) \
    X(WDAY_FRI) \
    X(WDAY_SAT) \
    X(MON_JAN) \
    X(MON_FEB) \
    X(MON_MAR) \
    X(MON_APR) \
    X(MON_MAY) \
    X(MON_JUN) \
    X(MON_JUL) \
    X(MON_AUG) \
    X(MON_SEP) \
    X(MON_OCT) \
    X(MON_NOV) \
    X(MON_DEC) \
    X(CLOCK_TITLE) \
    X(CLOCK_INFO) \
    X(CLOCK_DATE) \
    X(CLOCK_TIME) \
    X(OTA_TITLE) \
    X(OTA_DONT_POWER_OFF) \
    X(OTA_DONE) \
    X(OTA_RESTARTING) \
    X(OTA_FAILED) \
    X(OTA_KEPT) \
    X(SET_NET_INFO_CONNECTED) \
    X(SET_NET_INFO) \
    X(SET_TOUCH_INFO) \
    X(SET_MISSING) \
    X(SET_TITLE) \
    X(SET_LANGUAGE) \
    X(SET_BRIGHTNESS) \
    X(SET_RGB_LED) \
    X(SET_LED_BRIGHTNESS) \
    X(SET_ROLL_ANIM) \
    X(SET_DEFAULT_VARIANT) \
    X(SET_MAXI_SAVED) \
    X(SET_THROW_MODE) \
    X(SET_THROW_CUP) \
    X(SET_THROW_BUTTON) \
    X(SET_CLOCK) \
    X(SET_BATTERY) \
    X(BATTERY_HELP) \
    X(BATTERY_UNSUPPORTED) \
    X(BATTERY_ADC_FAILED) \
    X(BATTERY_WAIT) \
    X(BATTERY_ADC_CAL_MISSING) \
    X(BATTERY_RAW_FMT) \
    X(BATTERY_ADC_MV_FMT) \
    X(BATTERY_VOLTAGE_FMT) \
    X(BATTERY_UNKNOWN) \
    X(BATTERY_CALIBRATE) \
    X(BATTERY_RESET) \
    X(BATTERY_ENTER_MV) \
    X(BATTERY_CALIBRATION_FAILED) \
    X(BATTERY_SAVE_FAILED) \
    X(SET_NETWORK) \
    X(SET_CHANGE_WIFI) \
    X(VAR_YATZY) \
    X(VAR_MAXI) \
    X(VAR_YATZY_ORDER) \
    X(VAR_MAXI_ORDER) \
    X(WIFI_CONNECTING_TO_FMT) \
    X(WIFI_SEARCHING_SAVED) \
    X(WIFI_NOT_CONNECTED_FORGET) \
    X(WIFI_NOT_CONNECTED) \
    X(WIFI_FORGET_Q) \
    X(WIFI_SAVED_NETWORK) \
    X(WIFI_FORGET) \
    X(WIFI_NONE_FOUND) \
    X(WIFI_OPEN) \
    X(WIFI_SCANNING) \
    X(WIFI_SCAN_FAILED) \
    X(WIFI_CHOOSE) \
    X(WIFI_REFRESH) \
    X(WIFI_ENTER_PASSWORD) \
    X(WIFI_MIN8) \
    X(WIFI_HEX64) \
    X(WIFI_MAX63) \
    X(WIFI_PASSWORD_FOR) \
    X(WIFI_SAVED_FILLED) \
    X(WIFI_CONNECTED) \
    X(WIFI_CONNECT_FAILED) \
    X(BTN_CONTINUE) \
    X(WIFI_SAVED_NETWORK_DOTS) \
    X(WIFI_SEARCHING) \
    X(WIFI_UNKNOWN_ERROR) \
    X(WIFI_CONNECTING_TO) \
    X(WIFI_INVALID) \
    X(WIFI_AUTO_CONNECT) \
    X(CAT_ONES) \
    X(CAT_TWOS) \
    X(CAT_THREES) \
    X(CAT_FOURS) \
    X(CAT_FIVES) \
    X(CAT_SIXES) \
    X(CAT_ONE_PAIR) \
    X(CAT_TWO_PAIRS) \
    X(CAT_THREE_PAIRS) \
    X(CAT_THREE_KIND) \
    X(CAT_FOUR_KIND) \
    X(CAT_FIVE_KIND) \
    X(CAT_SMALL_STRAIGHT) \
    X(CAT_LARGE_STRAIGHT) \
    X(CAT_FULL_STRAIGHT) \
    X(CAT_FULL_HOUSE) \
    X(CAT_VILLA) \
    X(CAT_TOWER) \
    X(CAT_CHANCE) \
    X(CAT_YATZY) \
    X(CAT_MAXI_YATZY) \
    X(PERR_INVALID) \
    X(PERR_DUPLICATE) \
    X(PERR_FULL) \
    X(PERR_NOT_FOUND) \
    X(PERR_BAD_PIN) \
    X(MENU_CHOOSE_GAME) \
    X(MENU_SUB_YATZY) \
    X(MENU_SUB_MAXI) \
    X(MENU_SUB_YATZY_ORDER) \
    X(MENU_SUB_MAXI_ORDER) \
    X(PL_GUEST_TAG) \
    X(PL_ADD_AT_LEAST_ONE) \
    X(PL_DEFAULT_NAME) \
    X(PL_TITLE) \
    X(PL_ADD) \
    X(PL_START) \
    X(PL_WEB_HINT) \
    X(PL_NAME_EMPTY) \
    X(PICK_CHOOSE) \
    X(PICK_CHANGE) \
    X(PICK_COUNT) \
    X(PICK_NEW) \
    X(PICK_NEW_SAVED) \
    X(PICK_SEARCH) \
    X(PICK_SEARCH_DOTS) \
    X(PICK_SEARCH_PLAYER) \
    X(PICK_EMPTY) \
    X(PICK_NO_MATCH) \
    X(PICK_ALL_SEATED) \
    X(PICK_GUEST) \
    X(PICK_GUEST_NAME_FMT) \
    X(PICK_GUEST_NAME) \
    X(CUP_HINT) \
    X(GAME_ROLL) \
    X(GAME_CHOOSE) \
    X(GAME_TIDY) \
    X(GAME_HINT_ROLL) \
    X(GAME_HINT_ROLLING) \
    X(GAME_HINT_NO_BOXES) \
    X(GAME_ROUND) \
    X(GAME_SUM) \
    X(GAME_BONUS) \
    X(GAME_TOTAL) \
    X(GAME_ROLL_N_EXTRA) \
    X(GAME_ROLL_N) \
    X(GAME_BACK_ROLL_N) \
    X(GAME_WRITE) \
    X(GAME_PICK_BOX) \
    X(GAME_ROLLS_LEFT_EXTRA) \
    X(GAME_ROLLS_LEFT) \
    X(GAME_QUIT_Q) \
    X(GAME_QUIT_TEXT) \
    X(GAME_QUIT) \
    X(GAME_SUGGESTIONS) \
    X(GAME_PICK_BOX_BTN) \
    X(GAME_WEB_WAIT) \
    X(GAME_WEB_CONNECT) \
    X(GAME_WEB_CONNECTED) \
    X(GAME_WEB_LOCAL) \
    X(RES_TITLE) \
    X(RES_AGAIN) \
    X(RES_MENU) \
    X(VAR_MIDI) \
    X(MENU_SUB_MIDI) \
    X(MENU_CHOOSE_VARIANT) \
    X(GAME_NAME_PIG) \
    X(GAME_NAME_FARKLE) \
    X(GAME_NAME_SCC) \
    X(MENU_SUB_GAME_YATZY) \
    X(MENU_SUB_GAME_PIG) \
    X(MENU_SUB_GAME_FARKLE) \
    X(MENU_SUB_GAME_SCC) \
    X(VAR_PIG_1) \
    X(VAR_PIG_2) \
    X(MENU_SUB_PIG_1) \
    X(MENU_SUB_PIG_2) \
    X(VAR_FARKLE) \
    X(MENU_SUB_FARKLE) \
    X(VAR_SCC_3) \
    X(MENU_SUB_SCC_3) \
    X(G_BANK) \
    X(G_BANK_N) \
    X(G_BUST) \
    X(G_LOST_ALL) \
    X(G_FARKLE) \
    X(G_HOT_DICE) \
    X(G_TURN_N) \
    X(G_HELD_N) \
    X(G_ROUND_N) \
    X(G_PIG_HINT) \
    X(G_PIG_FIRST) \
    X(G_HOLD_HINT) \
    X(G_INVALID_HOLD) \
    X(G_OPEN_HINT) \
    X(G_FINAL_ROUND) \
    X(G_SHIP) \
    X(G_CAPTAIN) \
    X(G_CREW) \
    X(G_CARGO_N) \
    X(G_SAVE) \
    X(G_SAVE_N) \
    X(G_NO_CARGO) \
    X(G_SCC_HINT) \
    X(G_SCC_CARGO_HINT) \
    X(GAME_NAME_ADV) \
    X(MENU_SUB_GAME_ADV) \
    X(ADV_TITLE) \
    X(ADV_HIT) \
    X(ADV_JUMP) \
    X(ADV_SWAP) \
    X(ADV_ZOOM) \
    X(ADV_ZOOM_IN) \
    X(ADV_ZOOM_OUT) \
    X(ADV_SOUND_ON) \
    X(ADV_SOUND_OFF) \
    X(ADV_STATS) \
    X(ADV_BOAT_TAG) \
    X(ADV_GEM) \
    X(ADV_ALL_GEMS) \
    X(ADV_KEY) \
    X(ADV_NEED_KEY) \
    X(ADV_DOOR) \
    X(ADV_BOAT) \
    X(ADV_NEED_BOAT) \
    X(ADV_SPLASH) \
    X(ADV_SIZZLE) \
    X(ADV_BURNED) \
    X(ADV_GATE_OPEN) \
    X(ADV_GATE_CLOSE) \
    X(ADV_CRACK) \
    X(ADV_HELP) \
    X(ADV_RESTART_Q) \
    X(ADV_RESTART) \
    X(ADV_SETUP_TITLE) \
    X(ADV_WORLD_TEST) \
    X(ADV_WORLD_MINES) \
    X(ADV_WORLD_TEST_DESC) \
    X(ADV_WORLD_MINES_DESC) \
    X(ADV_WHO) \
    X(ADV_PICK_WORLD) \
    X(ADV_PICK_PLAYER) \
    X(ADV_GUEST) \
    X(ADV_HAS_SAVE) \
    X(ADV_CONTINUE_Q) \
    X(ADV_CONTINUE) \
    X(ADV_NEW_GAME) \
    X(ADV_LOAD_FAIL) \
    X(ADV_KEYS) \
    X(ADV_ENERGY) \
    X(ADV_LIVES) \
    X(ADV_LIVES_USED) \
    X(ADV_SUIT_TAG) \
    X(ADV_CANT_GO) \
    X(ADV_NO_WAY) \
    X(ADV_DANGER) \
    X(ADV_NEED_BOAT_GO) \
    X(ADV_UNKNOWN) \
    X(ADV_SUIT_OK) \
    X(ADV_TREASURE) \
    X(ADV_POTION) \
    X(ADV_HEART) \
    X(ADV_SUIT) \
    X(ADV_LIFE) \
    X(ADV_SUIT_USED) \
    X(ADV_MINE) \
    X(ADV_DIED) \
    X(ADV_DIED_LEFT) \
    X(ADV_GAME_OVER_Q) \
    X(ADV_CHECKPOINT) \
    X(ADV_PORTAL_BLOCKED) \
    X(ADV_EXIT) \
    X(ADV_SAVED) \
    X(ADV_SAVE_FAIL) \
    X(ADV_MENU_TITLE) \
    X(ADV_MODE_PRO) \
    X(ADV_MODE_ASSIST) \
    X(ADV_MODE_HELP) \
    X(ADV_SAVE) \
    X(ADV_LEAVE) \
    X(ADV_HELP_MINES) \
    X(ADV_TALK) \
    X(ADV_TALK_OK) \
    X(ADV_TALK_BYE) \
    X(ADV_TALK_NONE) \
    X(ADV_TALK_READ) \
    X(ADV_ITEM_GOT) \
    X(ADV_ITEM_GOT_N) \
    X(ADV_ITEM_LOST) \
    X(ADV_QUEST_NEW) \
    X(ADV_QUEST_DONE) \
    X(ADV_WORLD_VILLAGE) \
    X(ADV_WORLD_VILLAGE_DESC) \
    X(ADV_WORLD_JOSEPH) \
    X(ADV_WORLD_JOSEPH_DESC) \
    X(WEB_ERR_NUMBER) \
    X(WEB_ERR_TZ) \
    X(WEB_ERR_PIN) \
    X(WEB_ERR_PIN_FORMAT) \
    X(WEB_ERR_HOSTNAME) \
    X(WEB_ERR_NVS) \
    X(WEB_ERR_LANG) \
    X(WEB_ERR_SSID_MISSING) \
    X(WEB_ERR_SSID_LONG) \
    X(WEB_ERR_PASS) \
    X(WEB_ERR_WIFI_OFF) \
    X(WEB_ERR_MEMORY) \
    X(WEB_ERR_ACTION_MISSING) \
    X(WEB_ERR_SSID) \
    X(WEB_ERR_DELTA) \
    X(WEB_ERR_ACTION) \
    X(WEB_ERR_NET_NOT_FOUND) \
    X(WEB_ERR_EPOCH) \
    X(WEB_ERR_TIME) \
    X(WEB_ERR_NAME) \
    X(WEB_ERR_ID_NAME) \
    X(WEB_ERR_ID) \
    X(BOOT_TOUCH_NO_REPLY) \
    X(BOOT_PSRAM_INFO) \
    X(BOOT_PSRAM_MISSING) \
    X(BOOT_NO_DEVICES) \
    X(BOOT_RTC_MISSING) \
    X(BOOT_FS_INFO) \
    X(BOOT_FS_FAIL) \
    X(BOOT_STARTING_WIFI) \
    X(BOOT_WIFI_SAVED) \
    X(BOOT_WIFI_NONE) \
    X(BOOT_WIFI_FAIL) \
    X(BOOT_WEB_INFO) \
    X(BOOT_WEB_FAIL) \
    X(BOOT_FAILURES) \
    X(BOOT_DONE) \
    X(WEB_ERR_WS_NOGAME) \
    X(WEB_ERR_WS_PIN) \
    X(WEB_ERR_WS_NOPIN) \
    X(WEB_ERR_WS_LOCKED) \
    X(WEB_ERR_WS_SEAT) \
    X(WEB_ERR_WS_JOIN) \
    X(WEB_ERR_WS_FULL) \
    X(WEB_ERR_WS_CMD) \
    X(WEB_ERR_WS_TURN) \
    X(WEB_ERR_WS_PINEXISTS) \
    X(WEB_ERR_WS_BADPIN) \
    X(WEB_ERR_WS_SETPIN) \
    X(PIN_TITLE_FOR) \
    X(PIN_INFO_WEB) \
    X(PIN_INFO_NEW) \
    X(PIN_SKIP) \
    X(PIN_WEB_TITLE) \
    X(PIN_GUEST_NO_WEB) \
    X(SET_ROTATION) \
    X(SET_ROT_0) \
    X(SET_ROT_180) \
    X(SET_SOUND) \
    X(SET_SOUND_ON) \
    X(SET_SFX_VOL) \
    X(SET_MUSIC_ON) \
    X(SET_MUSIC_VOL) \
    X(SET_JOY) \
    X(SET_JOY_ZONES) \
    X(SET_JOY_SLOW) \
    X(SET_JOY_FAST) \
    X(SET_JOY_HOLD) \
    X(SET_JOY_DIAG) \
    X(SET_JOY_DIAG_DOM) \
    X(SET_JOY_DIAG_ALT) \
    X(SET_MS_PER_STEP) \
    X(SET_SD_INFO) \
    X(SET_SD_NONE) \
    X(SET_AUDIO_INFO) \
    X(SET_SD_REMOUNT) \
    X(BOOT_SD_FREE) \
    X(BOOT_SD_NONE) \
    X(WEB_ERR_FS_PATH) \
    X(WEB_ERR_FS_NOCARD) \
    X(WEB_ERR_FS_NOTFOUND) \
    X(WEB_ERR_FS_EXISTS) \
    X(WEB_ERR_FS_NOTEMPTY) \
    X(WEB_ERR_FS_IO) \
    X(WEB_ERR_FS_FULL) \
    X(WEB_ERR_FS_ROOT) \
    X(WEB_ERR_FS_REPAIR) \
    X(WEB_FS_REPAIR_DONE) \
    X(MENU_KARAOKE) \
    X(MENU_SUB_KARAOKE) \
    X(KARA_FONT_LOADING) \
    X(KARA_FONT_READY) \
    X(KARA_SONGS) \
    X(KARA_NO_SONGS) \
    X(KARA_SILENT) \
    X(KARA_WAIT_FONT) \
    X(KARA_ERR_NO_SD) \
    X(KARA_ERR_NO_SF2) \
    X(KARA_ERR_SF2_BIG) \
    X(KARA_ERR_SF2_BAD) \
    X(KARA_ERR_MEMORY) \
    X(KARA_ERR_NO_FILE) \
    X(KARA_ERR_FILE_BIG) \
    X(KARA_ERR_FILE_BAD) \
    X(KARA_NO_LYRICS) \
    X(KARA_END) \
    X(KARA_BTN_SETTINGS) \
    X(KARA_MELODY_ON) \
    X(KARA_MELODY_OFF) \
    X(KARA_BTN_TRACK) \
    X(KARA_TEMPO) \
    X(KARA_KEY) \
    X(KARA_VOLUME) \
    X(KARA_PICK_MELODY) \
    X(KARA_PICK_HINT) \
    X(KARA_CH_NONE) \
    X(KARA_CH_FMT) \
    X(KARA_AUTO) \
    X(KARA_MATCH) \
    X(KARA_SAVED_NOTE) \
    X(KARA_ERR_SF2_SF3) \
    X(KARA_ERR_SONG_BIG) \
    X(KARA_TR_REJECTED) \
    X(KARA_WEB_ACTIVE) \
    X(KARA_SOUND_OFF_Q) \
    X(KARA_YES) \
    X(KARA_NO) \
    X(KARA_BTN_FONT) \
    X(KARA_FONT_DEFAULT) \
    X(KARA_FONT_SONG) \
    X(KARA_FONT_AUTO) \
    X(KARA_FONT_USE_DEFAULT) \
    X(KARA_FONT_NONE) \
    X(KARA_FONT_SAVED) \
    X(KARA_FONT_SONG_HINT) \
    X(KARA_ALL_SONGS) \
    X(KARA_SEARCH_HINT) \
    X(KARA_SEARCH_TITLE) \
    X(KARA_RESCAN) \
    X(KARA_SCANNING) \
    X(KARA_UP) \
    X(KARA_PAGE) \
    X(KARA_N_SONGS) \
    X(KARA_SEARCH_RESULT) \
    X(KARA_NO_MATCH) \
    X(KARA_LIMIT)
// clang-format on

enum class Str : uint16_t {
#define LANG_X_ENUM(id) id,
    LANG_STR_IDS(LANG_X_ENUM)
#undef LANG_X_ENUM
    ADV_STAGE,
    ADV_SHIELD_SHORT,
    ADV_BOMB,
    ADV_BOMB_PICK,
    ADV_BOMB_PREVIEW,
    ADV_BOMB_EMPTY,
    ADV_SHIELD_HIT,
    ADV_MINE_WIN,
    ADV_CAMPAIGN_WIN,
    ADV_MINE_LOSS,
    ADV_NEXT_STAGE,
    ADV_MINE_RESTART,
    PERR_STORAGE,
    ADV_MEADOW,
    ADV_VILLAGE_MAP,
    ADV_MAP_FAILED,
    ADV_MINE_HELP,
    ADV_TRAVEL_HELP,
    ADV_JOSEPH_CAMP,
    ADV_JOSEPH_ROAD,
    ADV_JOSEPH_DONE,
    ADV_NO_ADVENTURES,       // SD card has no adventures folder / no adventures in it
    ADV_NO_ADVENTURES_HELP,  // where to put them
    ADV_ADVENTURE_DONE,      // finished the last map of an SD adventure
    ADV_SD_LOAD_FAIL,        // an SD adventure failed to load/parse
    ADV_FLASHLIGHT,          // v0.18.0 flashlight pickup name
    ADV_POGO,                // v0.18.0 pogo-stick pickup name
    ADV_FLASHLIGHT_TAG,      // v0.18.0 HUD indicator "Ficklampa %d" (level)
    ADV_POGO_TAG,            // v0.18.0 HUD indicator "Hoppstylta %d" (level)
    ADV_FLASHLIGHT_GOT,      // v0.18.0 pickup toast (level)
    ADV_POGO_GOT,            // v0.18.0 pickup toast (level)
    COUNT
};

// Text of `id` in the current language (never null; "" for an invalid id).
const char *tr(Str id);

// prefix + tr(id) + suffix in one of 8 rotating static buffers (UI thread only; LVGL copies
// label texts, so the result only has to live until the next call or two).
const char *trp(const char *prefix, Str id, const char *suffix = "");

namespace Lang {

constexpr int MAX_LANGS = 8;
constexpr int CODE_CAP = 4;   // "sv" + NUL, room for 3-letter codes

struct Entry {
    Str id;
    const char *text;
};

struct LangDef {
    const char *code;   // ISO 639-1 ("sv", "en")
    const char *name;   // Native name for the language picker ("Svenska", "English")
    const Entry *entries;
    size_t count;
};

bool setLang(const char *code);   // Unknown code -> false, Swedish stays/is selected
const char *currentLang();        // Code of the active language
bool valid(const char *code);

int count();                      // Number of available languages
const char *code(int i);
const char *name(int i);
int indexOf(const char *code);    // -1 if unknown

// Raw lookup in one language without fallback (nullptr if missing) - for tests and tools.
const char *lookup(int lang, Str id);
const char *idName(Str id);       // "BTN_SAVE" (for diagnostics)

}  // namespace Lang
