// board_rgb.h - onboard WS2812B status LED on GPIO40 (RMT driver from Arduino-ESP32 3.x,
// rgbLedWriteOrdered with GRB order). A small FreeRTOS task renders base color + effects.
#pragma once

#include <stdint.h>

namespace Rgb {
bool begin();
void bootSequence();                          // Blocking R -> G -> B -> white (~1 s)
void setBase(uint8_t r, uint8_t g, uint8_t b); // Steady status color (e.g. Wi-Fi state)
void setPulse(bool on);                       // Base color breathes slowly while on
void setBrightness(uint8_t level);            // 0..255 global scaling
void setEnabled(bool on);
void effectDiceRoll(uint32_t ms);             // White/gold pulse while the dice roll
void effectFlash(uint8_t r, uint8_t g, uint8_t b, uint32_t ms);  // Short single flash
}
