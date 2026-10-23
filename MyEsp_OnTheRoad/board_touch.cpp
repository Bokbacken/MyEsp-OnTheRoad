#include "board_touch.h"

#include <Arduino.h>
#include <Wire.h>

#include "board_pins.h"

namespace {

// ST77922 touch register map (16-bit register addresses, big-endian on the wire).
constexpr uint16_t REG_FW_VERSION = 0x0000;
constexpr uint16_t REG_MAX_X_H = 0x0005;      // max_x_h, max_x_l, max_y_h, max_y_l, max_touches
constexpr uint16_t REG_FW_REVISION = 0x000C;
constexpr uint16_t REG_MAX_TOUCHES = 0x0009;
constexpr uint16_t REG_TOUCH_INFO = 0x0010;   // bit3 = report contains coordinates
constexpr uint16_t REG_REPORT_0 = 0x0014;     // 7 bytes per point
constexpr int REPORT_BYTES = 7;
constexpr int MAX_POINTS = 5;
constexpr uint32_t IDLE_POLL_MS = 100;        // Safety poll if an INT edge is missed

Touch::Info g_info = {};
uint8_t g_scan[16];
int g_scanCount = 0;
volatile uint32_t g_irq = 0;
uint32_t g_irqSeen = 0;
uint32_t g_lastPoll = 0;
lv_indev_drv_t g_drv;
lv_indev_t *g_indev = nullptr;

void IRAM_ATTR touchIsr() { g_irq++; }

bool readReg(uint16_t reg, uint8_t *buf, size_t len)
{
    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write((uint8_t)(reg >> 8));
    Wire.write((uint8_t)(reg & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;   // Repeated start
    size_t got = Wire.requestFrom((uint16_t)TOUCH_I2C_ADDR, len);
    if (got != len) return false;
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)Wire.read();
    return true;
}

void scanBus()
{
    g_scanCount = 0;
    for (uint8_t a = 0x08; a < 0x78 && g_scanCount < (int)sizeof(g_scan); a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) g_scan[g_scanCount++] = a;
    }
    Serial.print("[i2c] scan:");
    for (int i = 0; i < g_scanCount; i++) Serial.printf(" 0x%02X", g_scan[i]);
    Serial.println(g_scanCount ? "" : " (nothing)");
}

void hwReset()
{
    pinMode(PIN_TOUCH_RST, OUTPUT);
    digitalWrite(PIN_TOUCH_RST, LOW);
    delay(10);
    digitalWrite(PIN_TOUCH_RST, HIGH);
    delay(100);
}

// Reads the current touch state. Returns false on I2C error.
bool readPoint(bool &pressed, uint16_t &x, uint16_t &y)
{
    uint8_t infoReg = 0;
    pressed = false;
    if (!readReg(REG_TOUCH_INFO, &infoReg, 1)) return false;
    if (!(infoReg & 0x08)) return true;            // No coordinates: released
    uint8_t n = 0;
    if (!readReg(REG_MAX_TOUCHES, &n, 1)) return false;
    if (n == 0) return true;
    if (n > MAX_POINTS) n = MAX_POINTS;
    uint8_t buf[MAX_POINTS * REPORT_BYTES];
    if (!readReg(REG_REPORT_0, buf, (size_t)n * REPORT_BYTES)) return false;
    for (int i = 0; i < n; i++) {
        const uint8_t *r = &buf[i * REPORT_BYTES];
        if (!(r[0] & 0x80)) continue;              // valid flag
        x = (uint16_t)(((r[0] & 0x3F) << 8) | r[1]);
        y = (uint16_t)((r[2] << 8) | r[3]);
        pressed = true;
        return true;                               // First valid point only (single touch UI)
    }
    return true;
}

void lvglRead(lv_indev_drv_t *, lv_indev_data_t *data)
{
    uint32_t irq = g_irq;
    uint32_t now = millis();
    // Read only when something may have changed: INT edge, finger still down, or idle poll.
    if (irq != g_irqSeen || g_info.pressed || now - g_lastPoll >= IDLE_POLL_MS) {
        g_irqSeen = irq;
        g_lastPoll = now;
        bool pressed;
        uint16_t x = g_info.lastX, y = g_info.lastY;
        if (readPoint(pressed, x, y)) {
            if (pressed) {
                if (x >= LCD_H_RES) x = LCD_H_RES - 1;
                if (y >= LCD_V_RES) y = LCD_V_RES - 1;
                if (!g_info.pressed) g_info.touchCount++;
                g_info.lastX = x;
                g_info.lastY = y;
            }
            g_info.pressed = pressed;
        } else {
            g_info.readErrors++;
            g_info.pressed = false;
        }
    }
    g_info.irqCount = irq;
    data->point.x = (lv_coord_t)g_info.lastX;
    data->point.y = (lv_coord_t)g_info.lastY;
    data->state = g_info.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

}  // namespace

namespace Touch {

bool begin()
{
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);
    Wire.setTimeOut(20);
    hwReset();
    scanBus();

    uint8_t info[5] = {};
    g_info.present = readReg(REG_FW_VERSION, &g_info.fwVersion, 1) &&
                     readReg(REG_FW_REVISION, g_info.fwRevision, 4) &&
                     readReg(REG_MAX_X_H, info, 5);
    if (g_info.present) {
        g_info.maxX = (uint16_t)((info[0] << 8) | info[1]);
        g_info.maxY = (uint16_t)((info[2] << 8) | info[3]);
        g_info.maxTouches = info[4];
        Serial.printf("[touch] ST77922 fw %u (%u.%u.%u.%u) max %ux%u, %u points\n", g_info.fwVersion,
                      g_info.fwRevision[0], g_info.fwRevision[1], g_info.fwRevision[2], g_info.fwRevision[3],
                      g_info.maxX, g_info.maxY, g_info.maxTouches);
    } else {
        Serial.println("[touch] no answer at 0x55 - check GPIO38/39, reset GPIO48");
    }

    pinMode(PIN_TOUCH_INT, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_TOUCH_INT), touchIsr, FALLING);

    // Register the indev even if the probe failed: the controller may wake up later.
    lv_indev_drv_init(&g_drv);
    g_drv.type = LV_INDEV_TYPE_POINTER;
    g_drv.read_cb = lvglRead;
    g_indev = lv_indev_drv_register(&g_drv);
    return g_info.present;
}

int scanResults(uint8_t *addrs, int max)
{
    int n = min(max, g_scanCount);
    for (int i = 0; i < n; i++) addrs[i] = g_scan[i];
    return n;
}

bool deviceFound(uint8_t addr)
{
    for (int i = 0; i < g_scanCount; i++) {
        if (g_scan[i] == addr) return true;
    }
    return false;
}

lv_indev_t *indev() { return g_indev; }
Info info() { return g_info; }

}  // namespace Touch
