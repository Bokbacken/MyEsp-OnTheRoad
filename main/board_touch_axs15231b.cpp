// AXS15231B polled touch; the shared I2C bus/reset are owned by BoardPower.
// Protocol: Waveshare factory BSP and Espressif AXS15231B documentation.
#include "board_touch.h"

#include <string.h>

#include "board_pins.h"
#include "board_power.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "my_lvgl.h"

namespace {

const char *TAG = "touch";
constexpr int I2C_TIMEOUT_MS = 100;
constexpr unsigned STARTUP_ATTEMPTS = 5;
constexpr uint32_t STARTUP_RETRY_MS = 50;
constexpr uint32_t POLL_MS = 15;
constexpr uint32_t RELEASE_HOLD_MS = 60;
constexpr uint32_t TAP_SLOP_PX = 20;
// Waveshare's Arduino driver ends the command with STOP before reading 14 bytes.
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
bool g_idleReportLogged = false;
uint16_t g_touchStartX = 0, g_touchStartY = 0;
bool g_tapCancelled = false;

void cancelMovedClick(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (!g_tapCancelled ||
        (code != LV_EVENT_CLICKED && code != LV_EVENT_SHORT_CLICKED &&
         code != LV_EVENT_SINGLE_CLICKED && code != LV_EVENT_DOUBLE_CLICKED &&
         code != LV_EVENT_TRIPLE_CLICKED)) return;
    lv_event_stop_processing(event);
    lv_indev_stop_processing(g_indev);
}

esp_err_t decodePoint(const uint8_t (&data)[REPORT_LEN], bool &down, bool &released, uint16_t &x, uint16_t &y)
{
    down = false;
    released = false;
    // Owner verified all-0x37 reports while untouched, replaced by valid reports
    // during contact. Match the entire report; other malformed data stays an error.
    bool idle = true;
    for (uint8_t byte : data) {
        if (byte != 0x37) {
            idle = false;
            break;
        }
    }
    if (idle) {
        if (!g_idleReportLogged) {
            ESP_LOGI(TAG, "AXS15231B observed all-0x37 no-contact report; treating as no point");
            g_idleReportLogged = true;
        }
        return ESP_OK;
    }
    if (data[1] == 0) return ESP_OK;
    if (data[1] > MAX_POINTS || data[0] == 0xFF) return ESP_ERR_INVALID_RESPONSE;
    const uint16_t px = ((data[2] & 0x0F) << 8) | data[3];
    const uint16_t py = ((data[4] & 0x0F) << 8) | data[5];
    if (px >= LCD_H_RES || py >= LCD_V_RES) return ESP_ERR_INVALID_RESPONSE;
    x = px;
    y = py;
    released = (data[2] >> 6) == 1;
    down = !released;
    return ESP_OK;
}

esp_err_t readPoint(bool &down, bool &released, uint16_t &x, uint16_t &y)
{
    uint8_t data[REPORT_LEN] = {};
    esp_err_t err = i2c_master_transmit(g_dev, READ_COMMAND, sizeof(READ_COMMAND), I2C_TIMEOUT_MS);
    if (err == ESP_OK)
        err = i2c_master_receive(g_dev, data, sizeof(data), I2C_TIMEOUT_MS);
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
            // A corrupt frame must not turn a held gesture into an accidental click.
            if (g_info.pressed) g_tapCancelled = true;
            g_info.pressed = false;
            g_gap = false;
        } else {
            if ((down || released) && g_info.pressed) {
                const int32_t dx = int32_t(x) - g_touchStartX;
                const int32_t dy = int32_t(y) - g_touchStartY;
                if (dx * dx + dy * dy > int32_t(TAP_SLOP_PX * TAP_SLOP_PX))
                    g_tapCancelled = true;
            }
            if (down) {
                if (!g_info.pressed) {
                    g_info.touchCount++;
                    g_touchStartX = x;
                    g_touchStartY = y;
                    g_tapCancelled = false;
                }
                if (g_gap) g_info.bridgedGaps++;
                g_gap = false;
                g_info.lastX = x;
                g_info.lastY = y;
                g_info.pressed = true;
                g_lastDown = now;
            } else if (released || now - g_lastDown >= RELEASE_HOLD_MS) {
                if (released && g_info.pressed) {
                    g_info.lastX = x;
                    g_info.lastY = y;
                }
                g_info.pressed = false;
                g_gap = false;
            } else if (g_info.pressed) {
                g_gap = true;
            }
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
    for (unsigned attempt = 0; attempt < STARTUP_ATTEMPTS; ++attempt) {
        err = readPoint(down, released, x, y);
        if (err == ESP_OK) {
            if (attempt)
                ESP_LOGI(TAG, "AXS15231B startup recovered after %u attempts", attempt + 1);
            break;
        }
        ++g_info.readErrors;
        ESP_LOGW(TAG, "AXS15231B startup attempt %u/%u: %s",
                 attempt + 1, STARTUP_ATTEMPTS, esp_err_to_name(err));
        if (attempt + 1 < STARTUP_ATTEMPTS) vTaskDelay(pdMS_TO_TICKS(STARTUP_RETRY_MS));
    }
    if (err != ESP_OK) return fail("AXS15231B first report", err);

    g_indev = MyLvgl::addPointer(pointerRead);
    if (!g_indev) return fail("AXS15231B LVGL pointer", ESP_ERR_NO_MEM);
    if (!lv_indev_add_event_cb(g_indev, cancelMovedClick, LV_EVENT_ALL, nullptr))
        return fail("AXS15231B click filter", ESP_ERR_NO_MEM);
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
