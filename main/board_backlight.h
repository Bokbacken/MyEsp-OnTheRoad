// board_backlight.h - LCD backlight on GPIO41 (active high) via LEDC PWM.
#pragma once

#include <stdint.h>

namespace Backlight {
bool begin(uint8_t percent = 0);   // LEDC 20 kHz, 8 bit; falls back to GPIO on/off
void set(uint8_t percent);         // 0..100 (0 = off), perceptual curve
uint8_t get();
// Preserve the selected brightness while temporarily switching off the light.
void setSleeping(bool sleeping);
bool sleeping();
}
