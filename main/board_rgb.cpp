#include "board_rgb.h"

#include <string.h>

#include "board_pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "my_sys.h"

using MySys::millis;
static void delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

namespace {
enum class Effect : uint8_t { NONE, DICE, FLASH };

struct State {
    uint8_t base[3] = {0, 0, 0};
    uint8_t flash[3] = {0, 0, 0};
    uint8_t brightness = 40;
    bool enabled = true;
    bool pulse = false;              // Base colour breathes (e.g. Wi-Fi connecting)
    Effect effect = Effect::NONE;
    uint32_t effectEnd = 0;
};

State g;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
uint8_t g_last[3] = {1, 1, 1};   // Forces the first write
bool g_ready = false;
led_strip_handle_t g_strip = nullptr;

void writeRaw(uint8_t r, uint8_t g_, uint8_t b)
{
    if (!g_strip) return;
    led_strip_set_pixel(g_strip, 0, r, g_, b);
    led_strip_refresh(g_strip);
}

void writeScaled(uint8_t r, uint8_t g_, uint8_t b, uint8_t bri)
{
    uint8_t out[3] = {(uint8_t)(r * bri / 255), (uint8_t)(g_ * bri / 255), (uint8_t)(b * bri / 255)};
    if (memcmp(out, g_last, 3) == 0) return;
    memcpy(g_last, out, 3);
    writeRaw(out[0], out[1], out[2]);
}

void rgbTask(void *)
{
    for (;;) {
        State s;
        portENTER_CRITICAL(&g_mux);
        s = g;
        uint32_t now = millis();
        if (g.effect != Effect::NONE && (int32_t)(now - g.effectEnd) >= 0) g.effect = s.effect = Effect::NONE;
        portEXIT_CRITICAL(&g_mux);

        uint8_t bri = s.enabled ? s.brightness : 0;
        switch (s.effect) {
        case Effect::DICE: {
            // Smooth white/gold pulse: triangle wave 35 %..100 % every 240 ms, the color
            // alternates between white and gold on each pulse.
            constexpr uint32_t PERIOD = 240;
            uint32_t now = millis();
            uint32_t ph = now % PERIOD;
            uint32_t tri = ph < PERIOD / 2 ? ph : PERIOD - ph;              // 0..120
            uint32_t lvl = 90 + tri * 165 / (PERIOD / 2);                    // 90..255
            bool gold = ((now / PERIOD) & 1) != 0;
            if (gold) writeScaled((uint8_t)lvl, (uint8_t)(lvl * 175 / 255), (uint8_t)(lvl * 20 / 255), bri);
            else writeScaled((uint8_t)lvl, (uint8_t)lvl, (uint8_t)lvl, bri);
            break;
        }
        case Effect::FLASH:
            writeScaled(s.flash[0], s.flash[1], s.flash[2], bri);
            break;
        default:
            if (s.pulse) {
                // Breathing: triangle wave 20 %..100 % over 1.2 s
                constexpr uint32_t PERIOD = 1200;
                uint32_t ph = millis() % PERIOD;
                uint32_t tri = ph < PERIOD / 2 ? ph : PERIOD - ph;           // 0..600
                uint32_t lvl = 51 + tri * 204 / (PERIOD / 2);                 // 51..255
                writeScaled((uint8_t)(s.base[0] * lvl / 255), (uint8_t)(s.base[1] * lvl / 255),
                            (uint8_t)(s.base[2] * lvl / 255), bri);
            } else {
                writeScaled(s.base[0], s.base[1], s.base[2], bri);
            }
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void startEffect(Effect e, uint32_t ms)
{
    portENTER_CRITICAL(&g_mux);
    g.effect = e;
    g.effectEnd = millis() + ms;
    portEXIT_CRITICAL(&g_mux);
}
}  // namespace

namespace Rgb {

bool begin()
{
    led_strip_config_t sc = {};
    sc.strip_gpio_num = PIN_WS2812;
    sc.max_leds = 1;
    sc.led_model = LED_MODEL_WS2812;
    sc.color_component_format.format.r_pos = 1;   // GRB on the wire
    sc.color_component_format.format.g_pos = 0;
    sc.color_component_format.format.b_pos = 2;
    sc.color_component_format.format.w_pos = 3;
    sc.color_component_format.format.bytes_per_color = 1;
    sc.color_component_format.format.num_components = 3;
    led_strip_rmt_config_t rc = {};
    rc.clk_src = RMT_CLK_SRC_DEFAULT;
    rc.resolution_hz = 10 * 1000 * 1000;
    if (led_strip_new_rmt_device(&sc, &rc, &g_strip) != ESP_OK) {
        g_strip = nullptr;
        return false;
    }
    writeRaw(0, 0, 0);
    g_ready = xTaskCreatePinnedToCore(rgbTask, "rgb", 2048, nullptr, 1, nullptr, 0) == pdPASS;
    return g_ready;
}

void bootSequence()
{
    static const uint8_t seq[4][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};
    for (auto &c : seq) {
        Rgb::effectFlash(c[0], c[1], c[2], 260);
        delay(250);
    }
    delay(30);
}

void setBase(uint8_t r, uint8_t g_, uint8_t b)
{
    portENTER_CRITICAL(&g_mux);
    g.base[0] = r; g.base[1] = g_; g.base[2] = b;
    portEXIT_CRITICAL(&g_mux);
}

void setPulse(bool on)
{
    portENTER_CRITICAL(&g_mux);
    g.pulse = on;
    portEXIT_CRITICAL(&g_mux);
}

void setBrightness(uint8_t level)
{
    portENTER_CRITICAL(&g_mux);
    g.brightness = level;
    portEXIT_CRITICAL(&g_mux);
}

void setEnabled(bool on)
{
    portENTER_CRITICAL(&g_mux);
    g.enabled = on;
    portEXIT_CRITICAL(&g_mux);
}

void effectDiceRoll(uint32_t ms) { startEffect(Effect::DICE, ms); }

void effectFlash(uint8_t r, uint8_t g_, uint8_t b, uint32_t ms)
{
    portENTER_CRITICAL(&g_mux);
    g.flash[0] = r; g.flash[1] = g_; g.flash[2] = b;
    portEXIT_CRITICAL(&g_mux);
    startEffect(Effect::FLASH, ms);
}

}  // namespace Rgb
