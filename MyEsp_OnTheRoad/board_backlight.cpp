#include "board_backlight.h"

#include <Arduino.h>
#include "board_pins.h"

namespace {
constexpr uint32_t BL_FREQ_HZ = 20000;   // Above audible/visible flicker range
constexpr uint8_t BL_RES_BITS = 8;
uint8_t g_percent = 0;
bool g_attached = false;
}

namespace Backlight {

bool begin(uint8_t percent)
{
    g_attached = ledcAttach(PIN_LCD_BL, BL_FREQ_HZ, BL_RES_BITS);
    if (!g_attached) {
        // Fallback: plain on/off so the screen is at least visible.
        pinMode(PIN_LCD_BL, OUTPUT);
        Serial.println("[bl] LEDC attach failed, using GPIO on/off");
    }
    set(percent);
    return g_attached;
}

void set(uint8_t percent)
{
    if (percent > 100) percent = 100;
    g_percent = percent;
    if (g_attached) {
        // Perceptual curve: squares the level so low settings are usable.
        uint32_t duty = (uint32_t)percent * percent * 255 / 10000;
        if (percent > 0 && duty == 0) duty = 1;
        ledcWrite(PIN_LCD_BL, duty);
    } else {
        digitalWrite(PIN_LCD_BL, percent ? HIGH : LOW);
    }
}

uint8_t get() { return g_percent; }

}  // namespace Backlight
