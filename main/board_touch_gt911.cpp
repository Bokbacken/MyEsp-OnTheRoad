// board_touch_gt911.cpp - Goodix GT911 capacitive touch (P4_43 board) and the LVGL pointer input
// device. Same Touch API as the ES3C35P driver: owns the shared I2C bus and the boot-time scan.
// INT/RST are not used (polled, address strapped by the board: 0x5D or 0x14). Rotation is done in
// software by LVGL, which also maps the pointer, so setRotation() is a no-op.
#include "board_touch.h"

#include <string.h>

#include "board_pins.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "my_lvgl.h"
#include "my_sys.h"

static const char *TAG = "touch";

namespace {

constexpr uint16_t REG_PRODUCT_ID = 0x8140;   // 4 ASCII chars, then fw (LE16), x res (LE16), y res (LE16)
constexpr uint16_t REG_TOUCH_NUM = 0x804C;    // Config: max touch points
constexpr uint16_t REG_STATUS = 0x814E;       // bit7 = buffer ready, low nibble = number of points
constexpr uint16_t REG_POINT_0 = 0x814F;      // 8 bytes per point: id, x (LE16), y (LE16), size (LE16), -
constexpr int I2C_TIMEOUT_MS = 20;
constexpr uint32_t POLL_MS = 15;
constexpr uint32_t RELEASE_HOLD_MS = 60;

Touch::Info g_info = {};
uint8_t g_scan[16];
int g_scanCount = 0;
uint32_t g_lastPoll = 0;
uint32_t g_lastDown = 0;
bool g_gap = false;
lv_indev_t *g_indev = nullptr;
i2c_master_bus_handle_t g_bus = nullptr;
i2c_master_dev_handle_t g_dev = nullptr;

bool readReg(uint16_t reg, uint8_t *buf, size_t len)
{
    if (!g_dev) return false;
    const uint8_t r[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF)};
    return i2c_master_transmit_receive(g_dev, r, 2, buf, len, I2C_TIMEOUT_MS) == ESP_OK;
}

bool writeReg8(uint16_t reg, uint8_t v)
{
    if (!g_dev) return false;
    const uint8_t b[3] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), v};
    return i2c_master_transmit(g_dev, b, 3, I2C_TIMEOUT_MS) == ESP_OK;
}

void scanBus()
{
    g_scanCount = 0;
    char line[16 * 5 + 1] = "";
    size_t used = 0;
    for (uint8_t a = 0x08; a < 0x78 && g_scanCount < (int)sizeof(g_scan); a++) {
        if (i2c_master_probe(g_bus, a, 10) == ESP_OK) {
            g_scan[g_scanCount++] = a;
            used += snprintf(line + used, sizeof(line) - used, " 0x%02X", a);
        }
    }
    ESP_LOGI("i2c", "scan:%s", g_scanCount ? line : " (nothing)");
}

bool deviceFoundLocal(uint8_t addr)
{
    for (int i = 0; i < g_scanCount; i++) {
        if (g_scan[i] == addr) return true;
    }
    return false;
}

uint16_t scale(uint16_t v, uint16_t max, int res)
{
    if (max > 0 && max != res) v = (uint16_t)((uint32_t)v * res / max);
    return v >= res ? (uint16_t)(res - 1) : v;
}

bool readPoint(bool &pressed, uint16_t &x, uint16_t &y)
{
    pressed = false;
    uint8_t st = 0;
    if (!readReg(REG_STATUS, &st, 1)) return false;
    if (!(st & 0x80)) {
        pressed = g_info.pressed && !g_gap;   // No new report yet: keep the previous state
        return true;
    }
    const int n = st & 0x0F;
    bool ok = true;
    if (n > 0) {
        uint8_t p[8];
        ok = readReg(REG_POINT_0, p, sizeof(p));
        if (ok) {
            x = scale((uint16_t)(p[1] | (p[2] << 8)), g_info.maxX, LCD_H_RES);
            y = scale((uint16_t)(p[3] | (p[4] << 8)), g_info.maxY, LCD_V_RES);
            pressed = true;
        }
    }
    writeReg8(REG_STATUS, 0);
    return ok;
}

bool pointerRead(int16_t *outX, int16_t *outY)
{
    const uint32_t now = MySys::millis();
    if (now - g_lastPoll >= POLL_MS) {
        g_lastPoll = now;
        bool pressed;
        uint16_t x = g_info.lastX, y = g_info.lastY;
        if (!readPoint(pressed, x, y)) {
            g_info.readErrors++;
            pressed = false;
        }
        if (pressed) {
            if (!g_info.pressed) g_info.touchCount++;
            g_info.lastX = x;
            g_info.lastY = y;
            if (g_gap) g_info.bridgedGaps++;
            g_gap = false;
            g_info.pressed = true;
            g_lastDown = now;
        } else if (g_info.pressed) {
            g_gap = true;
            if (now - g_lastDown >= RELEASE_HOLD_MS) {
                g_info.pressed = false;
                g_gap = false;
            }
        }
    }
    *outX = (int16_t)g_info.lastX;
    *outY = (int16_t)g_info.lastY;
    return g_info.pressed;
}

}  // namespace

namespace Touch {

bool begin()
{
    i2c_master_bus_config_t bc = {};
    bc.i2c_port = I2C_NUM_0;
    bc.sda_io_num = (gpio_num_t)PIN_I2C_SDA;
    bc.scl_io_num = (gpio_num_t)PIN_I2C_SCL;
    bc.clk_source = I2C_CLK_SRC_DEFAULT;
    bc.glitch_ignore_cnt = 7;
    bc.flags.enable_internal_pullup = true;
    esp_err_t err = i2c_new_master_bus(&bc, &g_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus: %s", esp_err_to_name(err));
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    scanBus();

    const uint8_t addr = deviceFoundLocal(TOUCH_I2C_ADDR) || !deviceFoundLocal(TOUCH_I2C_ADDR_ALT) ? TOUCH_I2C_ADDR
                                                                                                    : TOUCH_I2C_ADDR_ALT;
    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = addr;
    dc.scl_speed_hz = I2C_FREQ_HZ;
    i2c_master_bus_add_device(g_bus, &dc, &g_dev);

    uint8_t id[10] = {};
    g_info.present = readReg(REG_PRODUCT_ID, id, sizeof(id));
    if (g_info.present) {
        memcpy(g_info.fwRevision, id, 4);   // Product ID, e.g. "911\0"
        const uint16_t fw = (uint16_t)(id[4] | (id[5] << 8));
        g_info.fwVersion = fw;
        g_info.address = addr;
        g_info.maxX = (uint16_t)(id[6] | (id[7] << 8));
        g_info.maxY = (uint16_t)(id[8] | (id[9] << 8));
        readReg(REG_TOUCH_NUM, &g_info.maxTouches, 1);
        g_info.maxTouches &= 0x0F;
        ESP_LOGI(TAG, "GT%.4s at 0x%02X fw 0x%04X max %ux%u, %u points", (const char *)id, addr, fw, g_info.maxX,
                 g_info.maxY, g_info.maxTouches);
        writeReg8(REG_STATUS, 0);
    } else {
        ESP_LOGW(TAG, "GT911 not answering at 0x%02X - check GPIO7/8", addr);
    }

    g_indev = MyLvgl::addPointer(pointerRead);
    return g_info.present;
}

int scanResults(uint8_t *addrs, int max)
{
    int n = max < g_scanCount ? max : g_scanCount;
    for (int i = 0; i < n; i++) addrs[i] = g_scan[i];
    return n;
}

void setRotation(bool) {}
bool deviceFound(uint8_t addr) { return deviceFoundLocal(addr); }
i2c_master_bus_handle_t bus() { return g_bus; }
lv_indev_t *indev() { return g_indev; }
Info info() { return g_info; }

}  // namespace Touch
