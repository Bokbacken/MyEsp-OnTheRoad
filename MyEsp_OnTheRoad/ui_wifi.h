// ui_wifi.h - on-device Wi-Fi setup (station mode) in three linked screens:
//
//  1. Network list   - async scan with spinner, rows sorted by signal (5 bars), lock /
//                      "Öppen" marker, check mark on the connected network.
//                      Encoder: rotate = move selection, press = choose. Touch: tap a row.
//                      [Uppdatera] rescans, [Avbryt] returns to the main menu.
//  2. Password entry - character strip a-z A-Z 0-9 symbols space. Encoder: rotate = step
//                      character, press = append it (long press leaves the strip so the
//                      buttons can be reached). Touch: tap a character to append it, drag
//                      the strip sideways to scroll. [Visa]/[Dölj], [Rensa] (backspace),
//                      quick jumps abc/ABC/123/#+=, max 64 characters.
//  3. Connecting     - spinner + 15 s countdown bar, [Avbryt]. Success: "Ansluten! IP ..."
//                      then main menu. Failure: error text, then back to the network list.
//
// All functions must be called with the display lock held (LVGL task or DisplayLock).
#pragma once

namespace UiWifi {

void showNetworks();                       // Screen 1 (starts a scan)
void showPassword(const char *ssid);       // Screen 2
// Screen 3. Starts a connection attempt unless `alreadyStarted` (boot auto-connect, where
// Wifi::begin() has already begun connecting to the saved network).
void showConnecting(const char *ssid, const char *pass, bool alreadyStarted = false);

}  // namespace UiWifi
