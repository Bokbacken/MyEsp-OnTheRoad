// app_config.h - persistent application settings (NVS namespace "otr", same keys and encoding
// as the Arduino builds 0.2.x/0.3.x, so settings and saved networks survive the IDF migration).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "net_list.h"

#define FW_NAME "MyEsp-OnTheRoad"
#ifndef FW_VERSION
#define FW_VERSION "0.0.0"   // Set from PROJECT_VER by main/CMakeLists.txt
#endif

constexpr const char *CFG_NVS_NS = "otr";

struct AppConfig {
    char hostname[32];       // mDNS name without ".local"
    uint8_t brightness;      // Backlight 5..100 %
    uint8_t ledBrightness;   // RGB LED 0..255
    bool ledEnabled;
    uint8_t defaultVariant;  // 0 Yatzy, 1 Maxi, 2 Yatzy in order, 3 Maxi in order
    uint16_t rollAnimMs;     // Dice roll animation length, 300..2000 ms
    bool maxiSavedRolls;     // Maxi-Yatzy: unused rolls are saved for later turns
    uint8_t throwMode;       // 0 = dice cup gesture (default), 1 = [Kasta] button
    char adminPin[9];        // 4-8 digits; empty = no admin protection (OTA, reset, deletions)
    char tz[48];             // POSIX TZ string for the clock (default Sweden CET/CEST)
    char lang[4];            // UI language code ("sv" default, "en"); see main/lang/lang.h
    uint8_t advMode;         // Adventure tap-to-walk: 0 = assistant (default), 1 = pro (v0.8.0)
    uint16_t rotation;       // Display rotation in degrees: 0 (USB down, default) or 180 (v0.5.6)
    // v0.9.0: sound (ES8311)
    bool soundOn;            // Master switch (default on)
    uint8_t sfxVol;          // Effects volume 0..100 (default 60)
    bool musicOn;            // Background music (default on)
    uint8_t musicVol;        // Music volume 0..100 (default 25)
    // v0.9.0: adventure joystick (see components/otr_engine/include/otr_joystick.h)
    uint8_t joyZones;        // Speed zones 1..4 (default 2)
    uint16_t joySlowMs;      // Step interval of the inner zone, 60..600 ms (default 300)
    uint16_t joyFastMs;      // Step interval of the outer zone, 60..600 ms (default 150, <= slow)
    uint16_t joyHoldMs;      // Hold time before auto-repeat, 100..600 ms (default 300)
    uint8_t joyDiag;         // Diagonal: 0 = dominant axis (default), 1 = alternate axes
    // v0.13.1: karaoke
    char karaFont[48];       // Default SoundFont file name in /sdcard/otr/sf2 ("" = default.sf2 / first)
    uint8_t karaFlip;        // Landscape direction: 0 = rotated 90 degrees (default), 1 = 270 degrees
};

// Joystick limits shared by the UI and the web API.
constexpr uint16_t JOY_MS_MIN = 60, JOY_MS_MAX = 600, JOY_HOLD_MIN = 100, JOY_HOLD_MAX = 600;

namespace Config {
// Loads settings from NVS, falling back to defaults (MyNvs::initFlash() must have run).
void begin();
// Live settings. Modify, then call save(). Not locked: callers are the LVGL task and the
// web server; fields are small and writes are rare, so last-writer-wins is acceptable.
AppConfig &get();
bool save();
void resetToDefaults();      // Resets RAM copy and clears NVS (incl. saved networks)
void defaults(AppConfig &c);

// Saved Wi-Fi networks: MyWifi owns the list, these are its persistence hooks (one NVS blob,
// migrates the single 0.2.0 sta_ssid/sta_pass network).
bool loadNets(NetList &out);
bool saveNets(const NetList &nets);
const char *hostname();      // For MyWifi::Hooks

// Validation helpers shared by the UI and the web API.
bool validWifiPass(const char *pass, bool allowEmpty);
bool validHostname(const char *name);
bool validAdminPin(const char *pin, bool allowEmpty);
// True if no PIN is set or `pin` matches it (constant-time compare).
bool adminOk(const char *pin);
bool validTz(const char *tz);
// Clamps the sound / joystick fields into range (fast <= slow).
void sanitize(AppConfig &c);
}  // namespace Config
