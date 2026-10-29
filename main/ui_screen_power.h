#pragma once

namespace UiScreenPower {
// Call after boot with the LVGL lock held. Touch and timer callbacks share that lock.
void begin();
void sleepNow();
}  // namespace UiScreenPower
