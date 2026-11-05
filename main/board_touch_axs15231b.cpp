// AXS15231B polled touch; the shared I2C bus/reset are owned by BoardPower.
// Protocol: Waveshare factory BSP and Espressif AXS15231B documentation.
#include "board_touch.h"

#include <string.h>

#include "board_pins.h"
#include "board_display.h"
#include "board_power.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "my_lvgl.h"
#include "sdkconfig.h"
#include "otr_i2c.h"

namespace {

const char *TAG = "touch";
constexpr int I2C_TIMEOUT_MS = 100;
constexpr uint32_t TOUCH_I2C_HZ = 400000;  // Waveshare factory BSP rate
constexpr unsigned STARTUP_ATTEMPTS = 5;
constexpr uint32_t STARTUP_RETRY_MS = 50;
constexpr uint32_t POLL_MS = 30;
constexpr uint32_t RELEASE_HOLD_MS = 60;
constexpr uint32_t TAP_SLOP_PX = 20;
#ifdef CONFIG_OTR_S3_TOUCH_DIAGNOSTICS
constexpr bool TOUCH_DIAGNOSTICS = true;
#else
constexpr bool TOUCH_DIAGNOSTICS = false;
#endif
// The combined transaction keeps the command/read pair atomic on the shared bus.
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
constexpr bool BUFFERED_TRACE = TOUCH_DIAGNOSTICS;
struct RawSample {
    int64_t timeUs;
    int64_t durationUs;
    esp_err_t result;
    uint8_t bytes[REPORT_LEN];
};
#ifdef CONFIG_OTR_S3_TOUCH_DIAGNOSTICS
RawSample g_rawSamples[512];
unsigned g_rawCount = 0;
bool g_rawStarted = false;
bool g_rawDumped = false;

void recordRaw(int64_t start, int64_t duration, esp_err_t result,
               const uint8_t *bytes, bool down)
{
    if (g_rawDumped) return;
    if (!g_rawStarted) {
        if (result != ESP_OK || !down) return;
        g_rawStarted = true;
    }
    RawSample &sample = g_rawSamples[g_rawCount++];
    sample.timeUs = start;
    sample.durationUs = duration;
    sample.result = result;
    memcpy(sample.bytes, bytes, REPORT_LEN);
    if (g_rawCount != 512) return;
    g_rawDumped = true;
    ESP_LOGI("touch_raw", "BEGIN 512 buffered reads; first contact triggered capture; "
             "t_us=boot time, dt_us=I2C duration; no serial logging during capture");
    for (unsigned i = 0; i < g_rawCount; ++i) {
        const RawSample &s = g_rawSamples[i];
        const uint8_t *b = s.bytes;
        ESP_LOGI("touch_raw", "n=%u t_us=%lld dt_us=%lld result=%s bytes="
                 "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                 i, (long long)s.timeUs, (long long)s.durationUs, esp_err_to_name(s.result),
                 b[0], b[1], b[2], b[3], b[4], b[5], b[6],
                 b[7], b[8], b[9], b[10], b[11], b[12], b[13]);
    }
    ESP_LOGI("touch_raw", "END capture; reset to record again");
}
#endif
uint32_t g_lastTrace = 0;
lv_obj_t *g_scrollObject = nullptr;

void traceScroll()
{
    lv_obj_t *scroll = lv_indev_get_scroll_obj(g_indev);
    if (scroll != g_scrollObject) {
        ESP_LOGI(TAG, "DIAG LVGL scroll %s object=%p", scroll ? "begin/change" : "end",
                 static_cast<void *>(scroll));
        g_scrollObject = scroll;
    }
}

void cancelMovedClick(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (TOUCH_DIAGNOSTICS && BUFFERED_TRACE) return;
    if (TOUCH_DIAGNOSTICS) {
        const char *name = nullptr;
        switch (code) {
        case LV_EVENT_PRESSED: name = "pressed"; break;
        case LV_EVENT_RELEASED: name = "released"; break;
        case LV_EVENT_PRESS_LOST: name = "press-lost"; break;
        case LV_EVENT_CLICKED: name = "clicked"; break;
        case LV_EVENT_SHORT_CLICKED: name = "short-clicked"; break;
        case LV_EVENT_SINGLE_CLICKED: name = "single-clicked"; break;
        case LV_EVENT_DOUBLE_CLICKED: name = "double-clicked"; break;
        case LV_EVENT_TRIPLE_CLICKED: name = "triple-clicked"; break;
        default: break;
        }
        traceScroll();
        lv_obj_t *scroll = g_scrollObject;
        if (name) {
            lv_point_t point;
            lv_indev_get_point(g_indev, &point);
            ESP_LOGI(TAG, "DIAG LVGL %s x=%ld y=%ld scroll=%p extra_veto_would_cancel=%u",
                     name, long(point.x), long(point.y), static_cast<void *>(scroll),
                     unsigned(g_tapCancelled));
        }
        return;
    }
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
    // Without contact the controller returns 14 copies of one filler byte whose value
    // varies between boots (owner logs: 02, 13, 17, 37, 57, D7). Waveshare's BSP drops
    // these silently. Only a fully uniform report qualifies; other malformed data stays an error.
    bool idle = true;
    for (uint8_t byte : data) {
        if (byte != data[0]) {
            idle = false;
            break;
        }
    }
    if (idle) {
        if (!g_idleReportLogged && !BUFFERED_TRACE) {
            ESP_LOGI(TAG, "AXS15231B uniform 0x%02X no-contact report; treating as no point", data[0]);
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

esp_err_t readPoint(bool &down, bool &released, uint16_t &x, uint16_t &y, bool logReport = false)
{
    uint8_t data[REPORT_LEN] = {};
    const int64_t startUs = esp_timer_get_time();
    esp_err_t err = OtrI2c::transmitReceive(g_dev, READ_COMMAND, sizeof(READ_COMMAND),
                                                data, sizeof(data), I2C_TIMEOUT_MS);
    const int64_t durationUs = esp_timer_get_time() - startUs;
    const bool logError = !BUFFERED_TRACE &&
        (logReport || g_info.readErrors == 0 || (g_info.readErrors + 1) % 100 == 0);
#ifdef CONFIG_OTR_S3_TOUCH_DIAGNOSTICS
    // Keep transport status distinct from parser rejection in the raw capture.
    bool rawDown = false, rawReleased = false;
    uint16_t rawX = 0, rawY = 0;
    if (err == ESP_OK) decodePoint(data, rawDown, rawReleased, rawX, rawY);
    if (g_indev) recordRaw(startUs, durationUs, err, data, rawDown);
#else
    (void)startUs;
    (void)durationUs;
#endif
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
        const esp_err_t err = OtrI2c::probe(BoardPower::bus(), addr, 10);
        if (err == ESP_OK) g_scan[g_scanCount++] = addr;
        else if (err != ESP_ERR_NOT_FOUND) ESP_LOGW(TAG, "I2C probe 0x%02X: %s", addr, esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "shared I2C scan: %d devices", g_scanCount);
}

bool pointerRead(int16_t *outX, int16_t *outY)
{
    if (TOUCH_DIAGNOSTICS && !BUFFERED_TRACE) traceScroll();
    const uint32_t now = uint32_t(esp_timer_get_time() / 1000);
    if (g_dev && now - g_lastPoll >= POLL_MS) {
        g_lastPoll = now;
        bool down = false, released = false;
        uint16_t x = g_info.lastX, y = g_info.lastY;
        const esp_err_t err = readPoint(down, released, x, y);
        const uint32_t sampledAt = uint32_t(esp_timer_get_time() / 1000);
        const bool wasPressed = g_info.pressed;
        const bool wasCancelled = g_tapCancelled;
        if (err != ESP_OK) {
            g_info.readErrors++;
            // A corrupt frame must not turn a held gesture into an accidental click.
            if (g_info.pressed) g_tapCancelled = true;
            if (g_info.pressed && sampledAt - g_lastDown < RELEASE_HOLD_MS) {
                g_gap = true;
            } else {
                g_info.pressed = false;
                g_gap = false;
            }
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
                g_lastDown = sampledAt;
            } else if (released || sampledAt - g_lastDown >= RELEASE_HOLD_MS) {
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
        if (TOUCH_DIAGNOSTICS && !BUFFERED_TRACE &&
            (wasPressed != g_info.pressed || wasCancelled != g_tapCancelled ||
             ((wasPressed || g_info.pressed) && sampledAt - g_lastTrace >= 100))) {
            ESP_LOGI(TAG, "DIAG sample x=%u y=%u down=%u release=%u pressed=%u gap=%u "
                     "read=%s extra_veto_would_cancel=%u dx=%ld dy=%ld",
                     unsigned(x), unsigned(y), unsigned(down), unsigned(released),
                     unsigned(g_info.pressed), unsigned(g_gap), esp_err_to_name(err),
                     unsigned(g_tapCancelled), long(int32_t(x) - g_touchStartX),
                     long(int32_t(y) - g_touchStartY));
            g_lastTrace = sampledAt;
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
    cfg.scl_speed_hz = TOUCH_I2C_HZ;
    esp_err_t err = i2c_master_bus_add_device(BoardPower::bus(), &cfg, &g_dev);
    if (err != ESP_OK) return fail("AXS15231B I2C device", err);
    bool down = false, released = false;
    uint16_t x = 0, y = 0;
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (unsigned attempt = 0; attempt < STARTUP_ATTEMPTS; ++attempt) {
            err = readPoint(down, released, x, y, attempt == 0);
            if (err == ESP_OK) {
                if (attempt || pass)
                    ESP_LOGI(TAG, "AXS15231B startup recovered after %u attempts (shared reset %u)",
                             attempt + 1, pass);
                break;
            }
            ++g_info.readErrors;
            ESP_LOGW(TAG, "AXS15231B startup attempt %u/%u (shared reset %u): %s",
                     attempt + 1, STARTUP_ATTEMPTS, pass, esp_err_to_name(err));
            if (attempt + 1 < STARTUP_ATTEMPTS) vTaskDelay(pdMS_TO_TICKS(STARTUP_RETRY_MS));
        }
        if (err == ESP_OK || pass == 1) break;
        if (!Display::recoverTouchStartup())
            return fail("AXS15231B shared LCD/touch recovery", ESP_ERR_INVALID_STATE);
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
    if (TOUCH_DIAGNOSTICS)
        ESP_LOGW(TAG, "DIAG touch: extra movement/read-error click veto DISABLED; "
                 "LVGL handles clicks/scroll, swipes may activate buttons");
    if (BUFFERED_TRACE)
        ESP_LOGI("touch_raw", "ARMED: first contact records 512 reads in RAM, then dumps; "
                 "live touch traces disabled; reset to rearm");
    // No documented firmware-identification command: version fields remain unknown (zero).
    ESP_LOGI(TAG, "AXS15231B at 0x%02X, %ux%u, %u Hz atomic I2C, polled (no IRQ/reset GPIO)",
             TOUCH_I2C_ADDR, g_info.maxX, g_info.maxY, (unsigned)TOUCH_I2C_HZ);
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
