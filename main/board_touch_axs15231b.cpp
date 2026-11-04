// AXS15231B polled touch; the shared I2C bus/reset are owned by BoardPower.
// Protocol: Waveshare factory BSP and Espressif AXS15231B documentation.
#include "board_touch.h"

#include <string.h>

#include "board_pins.h"
#include "board_power.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "my_lvgl.h"

namespace {

const char *TAG = "touch";
constexpr int I2C_TIMEOUT_MS = 20;
constexpr uint32_t POLL_MS = 15;
constexpr uint32_t RELEASE_HOLD_MS = 60;
// Waveshare's 3.5B factory BSP uses a combined write/read and a 14-byte report.
constexpr size_t REPORT_LEN = 14;
constexpr uint8_t READ_COMMAND[] = {0xB5, 0xAB, 0xA5, 0x5A, 0, 0, 0, REPORT_LEN, 0, 0, 0};
constexpr uint8_t MAX_POINTS = 2;

Touch::Info g_info = {};
uint8_t g_scan[0x78 - 0x08];
int g_scanCount = 0;
i2c_master_dev_handle_t g_dev = nullptr;
lv_indev_t *g_indev = nullptr;
uint32_t g_lastPoll = 0;
uint32_t g_lastDown = 0;
bool g_gap = false;

esp_err_t decodePoint(const uint8_t (&data)[REPORT_LEN], bool &down, bool &released, uint16_t &x, uint16_t &y)
{
    down = false;
    released = false;
    if (data[1] == 0) return ESP_OK;
    if (data[1] > MAX_POINTS || data[0] == 0xFF) return ESP_ERR_INVALID_RESPONSE;
    if ((data[2] >> 6) == 1) {
        released = true;
        return ESP_OK;
    }
    const uint16_t px = ((data[2] & 0x0F) << 8) | data[3];
    const uint16_t py = ((data[4] & 0x0F) << 8) | data[5];
    if (px >= LCD_H_RES || py >= LCD_V_RES) return ESP_ERR_INVALID_RESPONSE;
    x = px;
    y = py;
    down = true;
    return ESP_OK;
}

esp_err_t readPoint(bool &down, bool &released, uint16_t &x, uint16_t &y)
{
    uint8_t data[REPORT_LEN] = {};
    esp_err_t err = i2c_master_transmit_receive(g_dev, READ_COMMAND, sizeof(READ_COMMAND),
                                              data, sizeof(data), I2C_TIMEOUT_MS);
    const bool logError = g_info.readErrors == 0 || (g_info.readErrors + 1) % 100 == 0;
    if (err != ESP_OK) {
        if (logError) ESP_LOGW(TAG, "AXS15231B I2C read: %s (%u errors)",
                              esp_err_to_name(err), (unsigned)(g_info.readErrors + 1));
        return err;
    }
    err = decodePoint(data, down, released, x, y);
    if (err != ESP_OK && logError) {
        ESP_LOGW(TAG, "AXS15231B invalid report (%u errors): "
                 "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 (unsigned)(g_info.readErrors + 1),
                 data[0], data[1], data[2], data[3], data[4], data[5], data[6],
                 data[7], data[8], data[9], data[10], data[11], data[12], data[13]);
    }
    return err;
}

void scanBus()
{
    g_scanCount = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        const esp_err_t err = i2c_master_probe(BoardPower::bus(), addr, 10);
        if (err == ESP_OK) g_scan[g_scanCount++] = addr;
        else if (err != ESP_ERR_NOT_FOUND) ESP_LOGW(TAG, "I2C probe 0x%02X: %s", addr, esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "shared I2C scan: %d devices", g_scanCount);
}

bool pointerRead(int16_t *outX, int16_t *outY)
{
    const uint32_t now = uint32_t(esp_timer_get_time() / 1000);
    if (g_dev && now - g_lastPoll >= POLL_MS) {
        g_lastPoll = now;
        bool down = false, released = false;
        uint16_t x = g_info.lastX, y = g_info.lastY;
        const esp_err_t err = readPoint(down, released, x, y);
        if (err != ESP_OK) {
            g_info.readErrors++;
            // A failed bus must not keep an old button pressed.
            g_info.pressed = false;
            g_gap = false;
        } else if (down) {
            if (!g_info.pressed) g_info.touchCount++;
            if (g_gap) g_info.bridgedGaps++;
            g_gap = false;
            g_info.lastX = x;
            g_info.lastY = y;
            g_info.pressed = true;
            g_lastDown = now;
        } else if (released || now - g_lastDown >= RELEASE_HOLD_MS) {
            g_info.pressed = false;
            g_gap = false;
        } else if (g_info.pressed) {
            g_gap = true;
        }
    }
    *outX = int16_t(g_info.lastX);
    *outY = int16_t(g_info.lastY);
    return g_info.pressed;
}

bool fail(const char *what, esp_err_t err)
{
    ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(err));
    g_info.present = false;
    if (g_dev) {
        const esp_err_t cleanup = i2c_master_bus_rm_device(g_dev);
        if (cleanup != ESP_OK) ESP_LOGE(TAG, "touch device cleanup: %s", esp_err_to_name(cleanup));
        g_dev = nullptr;
    }
    return false;
}

}  // namespace

namespace Touch {

bool begin()
{
    if (g_indev) return g_info.present;
    if (!BoardPower::bus() || !BoardPower::status().initialized) {
        return fail("BoardPower must initialize before touch", ESP_ERR_INVALID_STATE);
    }
    if (!MyLvgl::ok()) return fail("LVGL must initialize before touch", ESP_ERR_INVALID_STATE);
    scanBus();
    if (!deviceFound(TOUCH_I2C_ADDR)) return fail("AXS15231B absent at 0x3B", ESP_ERR_NOT_FOUND);

    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = TOUCH_I2C_ADDR;
    cfg.scl_speed_hz = I2C_FREQ_HZ;
    esp_err_t err = i2c_master_bus_add_device(BoardPower::bus(), &cfg, &g_dev);
    if (err != ESP_OK) return fail("AXS15231B I2C device", err);
    bool down = false, released = false;
    uint16_t x = 0, y = 0;
    err = readPoint(down, released, x, y);
    if (err != ESP_OK) return fail("AXS15231B first report", err);

    g_indev = MyLvgl::addPointer(pointerRead);
    if (!g_indev) return fail("AXS15231B LVGL pointer", ESP_ERR_NO_MEM);
    g_info.present = true;
    g_info.address = TOUCH_I2C_ADDR;
    g_info.maxX = LCD_H_RES;
    g_info.maxY = LCD_V_RES;
    g_info.maxTouches = MAX_POINTS;
    // No documented firmware-identification command: version fields remain unknown (zero).
    ESP_LOGI(TAG, "AXS15231B at 0x%02X, %ux%u, polled (no IRQ/reset GPIO)", TOUCH_I2C_ADDR, g_info.maxX, g_info.maxY);
    return true;
}

int scanResults(uint8_t *addrs, int max)
{
    if (!addrs || max <= 0) return 0;
    const int n = max < g_scanCount ? max : g_scanCount;
    memcpy(addrs, g_scan, size_t(n));
    return n;
}

bool deviceFound(uint8_t addr)
{
    for (int i = 0; i < g_scanCount; i++) {
        if (g_scan[i] == addr) return true;
    }
    return false;
}

i2c_master_bus_handle_t bus() { return BoardPower::bus(); }
lv_indev_t *indev() { return g_indev; }
void setRotation(bool) {}  // All orientations, including 180 degrees, are mapped by LVGL once.
Info info() { return g_info; }

}  // namespace Touch
