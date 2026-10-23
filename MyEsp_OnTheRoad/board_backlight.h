// board_backlight.h - LCD backlight on GPIO41 (active high) via LEDC PWM.
#pragma once

#include <stdint.h>

namespace Backlight {
bool begin(uint8_t percent = 0);   // Attaches LEDC (20 kHz, 8 bit) and applies the level
void set(uint8_t percent);         // 0..100 (0 = off)
uint8_t get();
}
