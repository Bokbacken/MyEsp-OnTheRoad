// ui_boot.h - boot screen: "OnTheRoad" title, progress bar and a hardware checklist
// (display, touch, PSRAM, Wi-Fi ...) with a green check or a red X plus error text.
//
// All functions take the display lock internally, so they can be called from setup()
// while the LVGL task is already running.
#pragma once

#include <stdint.h>

namespace UiBoot {

enum class Item : int { DISP = 0, TOUCH, PSRAM, I2C, RTC, STORAGE, WIFI, WEB, COUNT };
enum class State : int { PENDING = 0, RUNNING, OK, FAIL, SKIP, WAITING, WARN };

// Screen geometry for a w x h panel and the current font line heights (pure; host-tested).
constexpr int32_t LIST_PAD = 18;   // Card padding (2 x 8) + border (2 x 1)
struct Layout {
    int32_t scalePct, margin;
    bool showLogo;                 // Dropped when large text leaves no room (320x480)
    int32_t logo, logoY, titleY, versionY;
    int32_t barW, barH, barY;
    int32_t cardW, listY, listH, rowH, iconW;
    bool bigName;                  // Item names use the body font when the rows have room
    int32_t statusY;
};
Layout layout(int32_t w, int32_t h, int32_t titleLineH, int32_t smallLineH, int32_t bodyLineH);

void show();                                           // Builds and loads the screen
void set(Item item, State state, const char *detail = nullptr);
void setProgress(int percent);
void setStatus(const char *text);
int failures();
// Boot results, kept after the boot screen is gone (web overview, /api/info "boot")
const char *name(Item item);
State state(Item item);
const char *detail(Item item);                         // "" = no detail text given

}  // namespace UiBoot
