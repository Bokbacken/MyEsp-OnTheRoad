// board_config.h - persistent application settings (NVS via Preferences).
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "net_list.h"

#define FW_NAME    "MyEsp-OnTheRoad"
#define FW_VERSION "0.3.0"

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
// Loads settings from NVS (namespace "otr"), falling back to defaults.
void begin();
// Live settings. Modify, then call save(). Not locked: callers are the LVGL task and the
// web server; fields are small and writes are rare, so last-writer-wins is acceptable.
AppConfig &get();
bool save();
// Saved Wi-Fi networks (priority order, max NET_LIST_MAX). Same locking rule as get().
NetList &nets();
bool saveNets();
void resetToDefaults();      // Resets RAM copy and clears NVS
void defaults(AppConfig &c);
// Validation helpers shared by the UI and the web API.
bool validWifiPass(const char *pass, bool allowEmpty);
bool validHostname(const char *name);
bool validAdminPin(const char *pin, bool allowEmpty);
// True if no PIN is set or `pin` matches it (constant-time compare).
bool adminOk(const char *pin);
bool validTz(const char *tz);
}
