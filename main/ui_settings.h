// ui_settings.h - on-device settings: brightness, LED, dice animation time, default game,
// Wi-Fi / web address info and a live touch test. Changes are saved to NVS on "Spara".
#pragma once

namespace UiSettings {
void show();                 // Must run in the LVGL task or with Display::lock() held
void applyHardware();        // Applies brightness / LED settings from Config (any task)
}
