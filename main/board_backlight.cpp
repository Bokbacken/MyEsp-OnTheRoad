#include "board_backlight.h"

#include "board_pins.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

namespace {
constexpr uint32_t BL_FREQ_HZ = 20000;   // Above audible/visible flicker range
constexpr ledc_mode_t MODE = LEDC_LOW_SPEED_MODE;
constexpr ledc_channel_t CH = LEDC_CHANNEL_0;
uint8_t g_percent = 0;
bool g_pwm = false;
bool g_sleeping = false;
}

namespace Backlight {

bool begin(uint8_t percent)
{
    ledc_timer_config_t t = {};
    t.speed_mode = MODE;
    t.duty_resolution = LEDC_TIMER_8_BIT;
    t.timer_num = LEDC_TIMER_0;
    t.freq_hz = BL_FREQ_HZ;
    t.clk_cfg = LEDC_AUTO_CLK;
    ledc_channel_config_t c = {};
    c.gpio_num = PIN_LCD_BL;
    c.speed_mode = MODE;
    c.channel = CH;
    c.timer_sel = LEDC_TIMER_0;
    c.duty = 0;
    c.flags.output_invert = LCD_BL_ACTIVE_LOW ? 1 : 0;
    g_pwm = ledc_timer_config(&t) == ESP_OK && ledc_channel_config(&c) == ESP_OK;
    if (!g_pwm) {
        ESP_LOGW("bl", "LEDC failed, using GPIO on/off");
        gpio_reset_pin((gpio_num_t)PIN_LCD_BL);
        gpio_set_direction((gpio_num_t)PIN_LCD_BL, GPIO_MODE_OUTPUT);
    }
    set(percent);
    return g_pwm;
}

void set(uint8_t percent)
{
    if (percent > 100) percent = 100;
    g_percent = percent;
    if (g_sleeping) percent = 0;
    if (g_pwm) {
        // Perceptual curve: squares the level so low settings are usable.
        uint32_t duty = (uint32_t)percent * percent * 255 / 10000;
        if (percent > 0 && duty == 0) duty = 1;
        ledc_set_duty(MODE, CH, duty);
        ledc_update_duty(MODE, CH);
    } else {
        gpio_set_level((gpio_num_t)PIN_LCD_BL, (percent != 0) != LCD_BL_ACTIVE_LOW);
    }
}

uint8_t get() { return g_percent; }

void setSleeping(bool sleeping)
{
    g_sleeping = sleeping;
    set(g_percent);
}

bool sleeping() { return g_sleeping; }

}  // namespace Backlight
