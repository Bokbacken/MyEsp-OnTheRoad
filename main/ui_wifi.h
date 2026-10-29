// ui_wifi.h - on-device Wi-Fi setup (station mode) in three linked screens:
//
//  1. Network list   - async scan with spinner, rows sorted by signal (5 bars), lock /
//                      "Öppen" marker, check mark on the connected network, gold priority
//                      number on saved networks. Tap a row = choose; long-press a saved
//                      row = "forget" dialog. [Uppdatera] rescans, [Avbryt] = main menu.
//  2. Password entry - the on-screen scroll wheel (CharWheel, same as MyPi-Hotspot's
//                      mypi_kbd), masked, max 64 characters, pre-filled for saved networks.
//  3. Connecting     - spinner + 15 s countdown bar, [Avbryt]. Success: "Ansluten! IP ..."
//                      then main menu. Failure: error text, then back to the network list.
//
// All functions must be called with the display lock held (LVGL task or DisplayLock).
#pragma once

#include <stdint.h>

namespace UiWifi {

void showNetworks();                       // Screen 1 (starts a scan)
void showPassword(const char *ssid);       // Screen 2
// Screen 3. Starts a connection attempt unless `alreadyStarted` (boot auto-connect, where
// MyWifi::begin() has already begun scanning for / connecting to the saved networks).
void showConnecting(const char *ssid, const char *pass, bool alreadyStarted = false);

// Geometry of screen 3 for a w x h panel and the current font line heights (pure; host-tested).
struct ConnLayout {
    int32_t textW, titleY, ssidY, spinner, arc, spinnerY, iconY;
    int32_t barW, barH, barY, timeY, msgY, msgBottom;
    int32_t btnW, btnH, btnY;
};
ConnLayout connLayout(int32_t w, int32_t h, int32_t titleLineH, int32_t bodyLineH, int32_t smallLineH,
                      int32_t iconLineH);

}  // namespace UiWifi
