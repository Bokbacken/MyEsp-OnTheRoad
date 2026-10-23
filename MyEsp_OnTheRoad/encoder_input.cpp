#include "encoder_input.h"

#include <Arduino.h>
#include "board_pins.h"

namespace {
bool g_active = false;
lv_indev_drv_t g_drv;
lv_indev_t *g_indev = nullptr;
volatile int32_t g_edges = 0;      // Quadrature edges (ISR)
volatile uint8_t g_lastAB = 0;
int32_t g_reported = 0;            // Detents already passed to LVGL

#if ENCODER_ENABLED
// Gray-code transition table: index = (prev << 2) | curr, value = -1/0/+1.
const int8_t kQuad[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

void IRAM_ATTR encIsr()
{
    uint8_t ab = (uint8_t)((digitalRead(PIN_ENC_CLK) << 1) | digitalRead(PIN_ENC_DT));
    g_edges += kQuad[(g_lastAB << 2) | ab];
    g_lastAB = ab;
}
#endif

void readCb(lv_indev_drv_t *, lv_indev_data_t *data)
{
    int32_t detents = g_edges / ENC_STEPS_PER_DETENT;
    data->enc_diff = (int16_t)(detents - g_reported);
    g_reported = detents;
    bool pressed = PIN_ENC_SW >= 0 && digitalRead(PIN_ENC_SW) == LOW;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}
}  // namespace

namespace Encoder {

bool begin()
{
#if ENCODER_ENABLED
    if (PIN_ENC_CLK < 0 || PIN_ENC_DT < 0) {
        Serial.println("[enc] ENCODER_ENABLED but PIN_ENC_CLK/DT unassigned - disabled");
        return false;
    }
    pinMode(PIN_ENC_CLK, INPUT_PULLUP);
    pinMode(PIN_ENC_DT, INPUT_PULLUP);
    if (PIN_ENC_SW >= 0) pinMode(PIN_ENC_SW, INPUT_PULLUP);
    g_lastAB = (uint8_t)((digitalRead(PIN_ENC_CLK) << 1) | digitalRead(PIN_ENC_DT));
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_CLK), encIsr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_DT), encIsr, CHANGE);

    lv_indev_drv_init(&g_drv);
    g_drv.type = LV_INDEV_TYPE_ENCODER;
    g_drv.read_cb = readCb;
    g_indev = lv_indev_drv_register(&g_drv);
    g_active = g_indev != nullptr;
    Serial.printf("[enc] rotary encoder on CLK=%d DT=%d SW=%d\n", PIN_ENC_CLK, PIN_ENC_DT, PIN_ENC_SW);
    return g_active;
#else
    (void)readCb;
    return false;
#endif
}

bool active() { return g_active; }
lv_indev_t *indev() { return g_indev; }

void setGroup(lv_group_t *group)
{
    if (g_indev && group) lv_indev_set_group(g_indev, group);
}

long position() { return g_edges / ENC_STEPS_PER_DETENT; }

}  // namespace Encoder
