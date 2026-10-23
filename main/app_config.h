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
    char adminPin[9];        // 4-8 digits; empty = no admin protection (OTA, reset, deletions)
    char tz[48];             // POSIX TZ string for the clock (default Sweden CET/CEST)
};

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
}  // namespace Config
