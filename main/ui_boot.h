// ui_boot.h - boot screen: "OnTheRoad" title, progress bar and a hardware checklist
// (display, touch, PSRAM, Wi-Fi ...) with a green check or a red X plus error text.
//
// All functions take the display lock internally, so they can be called from setup()
// while the LVGL task is already running.
#pragma once

namespace UiBoot {

enum class Item : int { DISP = 0, TOUCH, PSRAM, I2C, RTC, STORAGE, WIFI, WEB, COUNT };
enum class State : int { PENDING = 0, RUNNING, OK, FAIL, SKIP, WAITING };

void show();                                           // Builds and loads the screen
void set(Item item, State state, const char *detail = nullptr);
void setProgress(int percent);
void setStatus(const char *text);
int failures();
// Boot results, kept after the boot screen is gone (web overview, /api/info "boot")
const char *name(Item item);
State state(Item item);
const char *detail(Item item);                         // "" = no detail text given                                        // Items in FAIL state

}  // namespace UiBoot
