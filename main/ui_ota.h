// ui_ota.h - full-screen overlay (LVGL top layer) showing firmware-update progress.
#pragma once

namespace UiOta {
// Starts a 250 ms LVGL timer that shows/updates/hides the overlay from MyOta::state().
// Call once with the display lock held, after the first screen is loaded.
void begin();
}
