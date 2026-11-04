#pragma once

namespace UiScreenPower {
enum class PowerMessage { CANCELLED, FAILED };
using PowerMessageHandler = void (*)(PowerMessage);

// Call after boot with the LVGL lock held. Touch and timer callbacks share that lock.
void begin();
void setPowerMessageHandler(PowerMessageHandler handler);
void sleepNow();
bool ecoSleeping();
}  // namespace UiScreenPower
