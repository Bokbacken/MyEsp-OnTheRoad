#include "board_touch.h"

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "my_lvgl.h"
#include "my_sys.h"

static const char *TAG = "touch";

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
constexpr int I2C_TIMEOUT_MS = 20;

Touch::Info g_info = {};
uint8_t g_scan[16];
int g_scanCount = 0;
volatile uint32_t g_irq = 0;
uint32_t g_irqSeen = 0;
uint32_t g_lastPoll = 0;
lv_indev_t *g_indev = nullptr;
i2c_master_bus_handle_t g_bus = nullptr;
i2c_master_dev_handle_t g_dev = nullptr;

void IRAM_ATTR touchIsr(void *) { g_irq = g_irq + 1; }

bool readReg(uint16_t reg, uint8_t *buf, size_t len)
{
    if (!g_dev) return false;
    const uint8_t r[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF)};
    return i2c_master_transmit_receive(g_dev, r, 2, buf, len, I2C_TIMEOUT_MS) == ESP_OK;   // Repeated start
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

void hwReset()
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << PIN_TOUCH_RST;
    cfg.mode = GPIO_MODE_OUTPUT;
    gpio_config(&cfg);
    gpio_set_level((gpio_num_t)PIN_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)PIN_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
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

// MyLvgl pointer callback (runs in the LVGL task).
bool pointerRead(int16_t *outX, int16_t *outY)
{
    uint32_t irq = g_irq;
    uint32_t now = MySys::millis();
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
    hwReset();
    scanBus();

    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = TOUCH_I2C_ADDR;
    dc.scl_speed_hz = I2C_FREQ_HZ;
    i2c_master_bus_add_device(g_bus, &dc, &g_dev);

    uint8_t info[5] = {};
    g_info.present = readReg(REG_FW_VERSION, &g_info.fwVersion, 1) &&
                     readReg(REG_FW_REVISION, g_info.fwRevision, 4) &&
                     readReg(REG_MAX_X_H, info, 5);
    if (g_info.present) {
        g_info.maxX = (uint16_t)((info[0] << 8) | info[1]);
        g_info.maxY = (uint16_t)((info[2] << 8) | info[3]);
        g_info.maxTouches = info[4];
        ESP_LOGI(TAG, "ST77922 fw %u (%u.%u.%u.%u) max %ux%u, %u points", g_info.fwVersion, g_info.fwRevision[0],
                 g_info.fwRevision[1], g_info.fwRevision[2], g_info.fwRevision[3], g_info.maxX, g_info.maxY,
                 g_info.maxTouches);
    } else {
        ESP_LOGW(TAG, "no answer at 0x55 - check GPIO38/39, reset GPIO48");
    }

    gpio_config_t ic = {};
    ic.pin_bit_mask = 1ULL << PIN_TOUCH_INT;
    ic.mode = GPIO_MODE_INPUT;
    ic.pull_up_en = GPIO_PULLUP_ENABLE;
    ic.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&ic);
    // The display driver normally installed the ISR service already (TE); install only if missing.
    if (gpio_isr_handler_add((gpio_num_t)PIN_TOUCH_INT, touchIsr, nullptr) == ESP_ERR_INVALID_STATE &&
        gpio_install_isr_service(0) == ESP_OK) {
        gpio_isr_handler_add((gpio_num_t)PIN_TOUCH_INT, touchIsr, nullptr);
    }

    // Register the indev even if the probe failed: the controller may wake up later.
    g_indev = MyLvgl::addPointer(pointerRead);
    return g_info.present;
}

int scanResults(uint8_t *addrs, int max)
{
    int n = max < g_scanCount ? max : g_scanCount;
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

i2c_master_bus_handle_t bus() { return g_bus; }
lv_indev_t *indev() { return g_indev; }
Info info() { return g_info; }

}  // namespace Touch
