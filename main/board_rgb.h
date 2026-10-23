// board_rgb.h - onboard WS2812B status LED on GPIO40 (espressif/led_strip, RMT, GRB order).
// A small FreeRTOS task renders base colour + effects.
#pragma once

#include <stdint.h>

namespace Rgb {
bool begin();
void bootSequence();                          // Blocking R -> G -> B -> white (~1 s)
void setBase(uint8_t r, uint8_t g, uint8_t b); // Steady status colour (e.g. Wi-Fi state)
void setPulse(bool on);                       // Base colour breathes slowly while on
void setBrightness(uint8_t level);            // 0..255 global scaling
void setEnabled(bool on);
void effectDiceRoll(uint32_t ms);             // White/gold pulse while the dice roll
void effectFlash(uint8_t r, uint8_t g, uint8_t b, uint32_t ms);  // Short single flash
}
