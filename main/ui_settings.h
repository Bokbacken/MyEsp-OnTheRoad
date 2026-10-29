// ui_settings.h - on-device settings: brightness, LED, dice animation time, default game,
// Wi-Fi / web address info and a live touch test. Changes are saved to NVS on "Spara".
#pragma once

#include <stdint.h>

namespace UiSettings {
void show();                 // Must run in the LVGL task or with Display::lock() held
void applyHardware();        // Applies brightness / LED settings from Config (any task)
// Switches the font roles to Config::get().textSize (clamped to the panel). Screens built
// afterwards use the new sizes; UiYatzy::showMenu() calls this before building the menu.
void applyTextSize();
uint8_t maxTextSize();       // Largest MyFonts::TextSize value supported by this panel
}
