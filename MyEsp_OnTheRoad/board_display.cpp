#include "board_display.h"

#include <Arduino.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "board_pins.h"

// ---------------------------------------------------------------------------
// Compile-time checks of lv_conf.h (see extras/lv_conf.h)
// ---------------------------------------------------------------------------
#if LVGL_VERSION_MAJOR != 8
#error "This project targets LVGL 8.x (tested with 8.4.0). Install lvgl@8.4.0."
#endif
#if LV_COLOR_DEPTH != 16
#error "lv_conf.h: LV_COLOR_DEPTH must be 16"
#endif
#if LV_COLOR_16_SWAP != 1
#error "lv_conf.h: LV_COLOR_16_SWAP must be 1 (panel expects big-endian RGB565)"
#endif

namespace {

constexpr spi_host_device_t LCD_HOST = SPI2_HOST;
constexpr int DRAW_BUF_LINES = 40;                       // 2 x 320 x 40 px in PSRAM
constexpr int BOUNCE_PIXELS = LCD_H_RES * 16;            // 2 x 10 KB internal DMA RAM
constexpr uint32_t TE_WAIT_MS = 25;
constexpr uint32_t TE_GIVE_UP_AFTER = 8;                 // Consecutive timeouts before disabling TE

// QSPI framing used by the ST77922: 32-bit "command" = opcode << 24 | register << 8.
constexpr uint32_t OPCODE_WRITE_CMD = 0x02;
constexpr uint32_t OPCODE_WRITE_COLOR = 0x32;

// Vendor initialisation sequence for this panel, encoded as {cmd, nparams, delay_ms, params...}.
// Taken from the matching community BSP ESP32S3_ST77922C_LVGL8 (esp_bsp.c, CC0-1.0) which is
// verified on the ES3C35P board.
const uint8_t kInitSeq[] = {
    0xF1, 1, 0, 0x00,
    0x60, 3, 0, 0x00, 0x00, 0x00,
    0x65, 1, 0, 0x80,
    0x79, 1, 0, 0x06,
    0x7B, 3, 0, 0x00, 0x08, 0x08,
    0x80, 11, 0, 0x55, 0x62, 0x2F, 0x17, 0xF0, 0x52, 0x70, 0xD2, 0x52, 0x62, 0xEA,
    0x81, 4, 0, 0x26, 0x52, 0x72, 0x27,
    0x84, 2, 0, 0x92, 0x25,
    0x87, 6, 0, 0x10, 0x10, 0x58, 0x00, 0x02, 0x3A,
    0x88, 15, 0, 0x00, 0x00, 0x2C, 0x10, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x06,
    0x89, 3, 0, 0x00, 0x00, 0x00,
    0x8A, 11, 0, 0x13, 0x00, 0x2C, 0x00, 0x00, 0x2C, 0x10, 0x10, 0x00, 0x3E, 0x19,
    0x8B, 9, 0, 0x15, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x97, 0x8E,
    0x8C, 13, 0, 0x1D, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x50, 0x0F, 0x01, 0xC5, 0x12, 0x09,
    0x8D, 1, 0, 0x0C,
    0x8E, 6, 0, 0x33, 0x01, 0x0C, 0x13, 0x01, 0x01,
    0xB3, 2, 0, 0x00, 0x30,
    0xF1, 1, 0, 0x00,
    0x71, 1, 0, 0xC0,
    0x66, 2, 0, 0x02, 0x3F,
    0xBE, 3, 0, 0x24, 0x00, 0x9D,
    0x70, 12, 0, 0x01, 0xA0, 0x11, 0x40, 0xE0, 0x00, 0x11, 0x69, 0x11, 0x00, 0x00, 0x1A,
    0x90, 9, 0, 0x04, 0x04, 0x55, 0x74, 0x00, 0x40, 0x43, 0x27, 0x27,
    0x91, 9, 0, 0x04, 0x04, 0x55, 0x75, 0x00, 0x40, 0x42, 0x27, 0x27,
    0x92, 10, 0, 0x04, 0x44, 0x55, 0xC0, 0x06, 0x00, 0x07, 0x05, 0x90, 0x27,
    0x93, 10, 0, 0x04, 0x43, 0x11, 0x00, 0x00, 0x00, 0x00, 0x05, 0x90, 0x27,
    0x94, 6, 0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x95, 5, 0, 0x96, 0x16, 0x00, 0x00, 0xFF,
    0x96, 12, 0, 0x44, 0x53, 0x03, 0x12, 0x23, 0x24, 0x06, 0x05, 0x94, 0x27, 0x00, 0x44,
    0x97, 12, 0, 0x44, 0x53, 0x47, 0x56, 0x20, 0x20, 0x02, 0x01, 0x94, 0x27, 0x00, 0x44,
    0xBA, 5, 0, 0x55, 0x94, 0x2D, 0x94, 0x27,
    0x9A, 7, 0, 0x40, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00,
    0x9B, 7, 0, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00,
    0x9C, 13, 0, 0x5C, 0x12, 0x00, 0x00, 0x10, 0x12, 0x00, 0x00, 0x10, 0x02, 0x00, 0x00, 0x00,
    0x9D, 8, 0, 0x8A, 0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01,
    0x9E, 7, 0, 0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01,
    0xB4, 12, 0, 0x1D, 0x1C, 0x1E, 0x0B, 0x14, 0x02, 0x13, 0x09, 0x1E, 0x00, 0x1E, 0x10,
    0xB5, 12, 0, 0x1D, 0x1C, 0x1E, 0x0A, 0x15, 0x03, 0x11, 0x08, 0x1E, 0x01, 0x1E, 0x12,
    0xB6, 7, 0, 0x77, 0x77, 0x00, 0x0A, 0xFF, 0x0A, 0xFF,
    0x86, 14, 0, 0xCD, 0x04, 0xB1, 0x02, 0x58, 0x12, 0x58, 0x0C, 0x13, 0x01, 0xA5, 0x00, 0xA5, 0xA5,
    0xB7, 16, 0, 0x07, 0x0A, 0x0E, 0x06, 0x05, 0x03, 0x2B, 0x03, 0x03, 0x42, 0x07, 0x10, 0x10, 0x2E, 0x3F, 0x0D,
    0xB8, 16, 0, 0x07, 0x0A, 0x0D, 0x05, 0x05, 0x02, 0x2B, 0x02, 0x03, 0x42, 0x06, 0x10, 0x0F, 0x2E, 0x3F, 0x0D,
    0xB9, 2, 0, 0x23, 0x23,
    0xBF, 6, 0, 0x10, 0x14, 0x14, 0x0B, 0x0B, 0x0B,
    0xF2, 1, 0, 0x00,
    0x73, 5, 0, 0x04, 0xDA, 0x12, 0x54, 0x47,
    0x77, 5, 0, 0x6B, 0x5B, 0xFD, 0xC3, 0xC5,
    0x7A, 2, 0, 0x15, 0x27,
    0x7B, 2, 0, 0x04, 0x57,
    0x7E, 2, 0, 0x01, 0x0E,
    0xBF, 1, 0, 0x36,
    0xE3, 2, 0, 0x40, 0x40,
    0xF0, 1, 0, 0x00,
    0xD0, 1, 0, 0x00,
    0x2A, 4, 0, 0x00, 0x00, 0x01, 0x3F,  // CASET 0..319
    0x2B, 4, 0, 0x00, 0x00, 0x01, 0xDF,  // RASET 0..479
    0x21, 0, 0,  // INVON (inversion is required on this panel)
    0x11, 0, 120,  // SLPOUT
    0x29, 0, 0,  // DISPON
    0x2C, 0, 0,
    0x3A, 1, 0, 0x01,  // As in the verified BSP sequence
    0x36, 1, 0, 0x00,  // MADCTL: native portrait, RGB
    0x35, 1, 20, 0x01,  // TEON
};

esp_lcd_panel_io_handle_t g_io = nullptr;
bool g_ok = false;
uint16_t *g_bounce[2] = {nullptr, nullptr};
uint8_t g_bounceIdx = 0;

lv_disp_draw_buf_t g_drawBuf;
lv_disp_drv_t g_dispDrv;
lv_disp_t *g_disp = nullptr;

SemaphoreHandle_t g_lvglMutex = nullptr;
SemaphoreHandle_t g_teSem = nullptr;
TaskHandle_t g_lvglTask = nullptr;

volatile uint32_t g_teEdges = 0;
Display::Stats g_stats = {};
bool g_frameStart = true;
uint32_t g_teMissRun = 0;
uint32_t g_fpsWindowStart = 0;
uint32_t g_fpsFrames = 0;

esp_err_t txParam(uint8_t cmd, const uint8_t *data, size_t len)
{
    return esp_lcd_panel_io_tx_param(g_io, (int)((OPCODE_WRITE_CMD << 24) | ((uint32_t)cmd << 8)), data, len);
}

// Sends a window + pixels from a DMA-capable buffer (asynchronous; the next txParam waits
// until all queued colour transfers have completed).
void txWindow(int x1, int y1, int x2, int y2, const uint16_t *px)
{
    const uint8_t caset[4] = {(uint8_t)(x1 >> 8), (uint8_t)x1, (uint8_t)(x2 >> 8), (uint8_t)x2};
    const uint8_t raset[4] = {(uint8_t)(y1 >> 8), (uint8_t)y1, (uint8_t)(y2 >> 8), (uint8_t)y2};
    txParam(0x2A, caset, 4);
    txParam(0x2B, raset, 4);
    size_t bytes = (size_t)(x2 - x1 + 1) * (size_t)(y2 - y1 + 1) * 2;
    esp_lcd_panel_io_tx_color(g_io, (int)((OPCODE_WRITE_COLOR << 24) | (0x2CU << 8)), px, bytes);
}

void IRAM_ATTR teIsr(void *)
{
    g_teEdges++;
    BaseType_t woken = pdFALSE;
    if (g_teSem) xSemaphoreGiveFromISR(g_teSem, &woken);
    if (woken) portYIELD_FROM_ISR();
}

void waitForTe()
{
    if (!g_stats.teActive || !g_teSem) return;
    xSemaphoreTake(g_teSem, 0);   // Drop a stale edge, wait for a fresh one
    if (xSemaphoreTake(g_teSem, pdMS_TO_TICKS(TE_WAIT_MS)) == pdTRUE) {
        g_teMissRun = 0;
        return;
    }
    g_stats.teTimeouts++;
    if (++g_teMissRun >= TE_GIVE_UP_AFTER) {
        g_stats.teActive = false;
        Serial.println("[lcd] no TE pulses on GPIO42 - continuing without tear sync");
    }
}

// ST77922 needs draw windows aligned to 4 pixels.
void rounderCb(lv_disp_drv_t *, lv_area_t *a)
{
    a->x1 &= ~3;
    a->y1 &= ~3;
    a->x2 |= 3;
    a->y2 |= 3;
    if (a->x2 >= LCD_H_RES) a->x2 = LCD_H_RES - 1;
    if (a->y2 >= LCD_V_RES) a->y2 = LCD_V_RES - 1;
}

void flushCb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *colors)
{
    if (g_frameStart) waitForTe();
    g_frameStart = false;

    const int w = area->x2 - area->x1 + 1;
    int rowsPerChunk = (BOUNCE_PIXELS / w) & ~3;
    if (rowsPerChunk < 4) rowsPerChunk = 4;
    const uint16_t *src = (const uint16_t *)colors;

    for (int y = area->y1; y <= area->y2; y += rowsPerChunk) {
        int rows = min(rowsPerChunk, area->y2 - y + 1);
        uint16_t *dst = g_bounce[g_bounceIdx];
        g_bounceIdx ^= 1;
        // Safe: the buffer we fill was used two chunks ago, and txParam() inside the previous
        // txWindow() waited for that transfer to finish.
        memcpy(dst, src, (size_t)w * rows * 2);
        src += (size_t)w * rows;
        txWindow(area->x1, y, area->x2, y + rows - 1, dst);
    }

    g_stats.flushes++;
    if (lv_disp_flush_is_last(drv)) {
        g_frameStart = true;
        g_stats.frames++;
        g_fpsFrames++;
    }
    uint32_t now = millis();
    if (now - g_fpsWindowStart >= 1000) {
        g_stats.fps = g_fpsFrames * 1000.0f / (float)(now - g_fpsWindowStart);
        g_fpsFrames = 0;
        g_fpsWindowStart = now;
    }
    lv_disp_flush_ready(drv);
}

void lvglTask(void *)
{
    for (;;) {
        uint32_t waitMs = 5;
        if (xSemaphoreTakeRecursive(g_lvglMutex, portMAX_DELAY) == pdTRUE) {
            waitMs = lv_timer_handler();
            xSemaphoreGiveRecursive(g_lvglMutex);
        }
        g_stats.teEdges = g_teEdges;
        vTaskDelay(pdMS_TO_TICKS(constrain(waitMs, 2U, 20U)));
    }
}

bool initPanel()
{
    spi_bus_config_t bus = {};
    bus.sclk_io_num = PIN_LCD_CLK;
    bus.data0_io_num = PIN_LCD_D0;
    bus.data1_io_num = PIN_LCD_D1;
    bus.data2_io_num = PIN_LCD_D2;
    bus.data3_io_num = PIN_LCD_D3;
    bus.data4_io_num = -1;
    bus.data5_io_num = -1;
    bus.data6_io_num = -1;
    bus.data7_io_num = -1;
    bus.max_transfer_sz = BOUNCE_PIXELS * 2 + 64;
    bus.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD;
    esp_err_t err = spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        Serial.printf("[lcd] spi_bus_initialize failed: %s\n", esp_err_to_name(err));
        return false;
    }

    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = PIN_LCD_CS;
    io.dc_gpio_num = -1;
    io.spi_mode = 0;
    io.pclk_hz = LCD_QSPI_HZ;
    io.trans_queue_depth = 10;
    io.lcd_cmd_bits = 32;
    io.lcd_param_bits = 8;
    io.flags.quad_mode = 1;
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io, &g_io);
    if (err != ESP_OK) {
        Serial.printf("[lcd] panel IO failed: %s\n", esp_err_to_name(err));
        return false;
    }

    // No reset GPIO (tied to EN): software reset, then the vendor sequence.
    txParam(0x01, nullptr, 0);
    delay(120);
    const uint8_t madctl = 0x00, colmod = 0x55;   // RGB order, RGB565
    txParam(0x36, &madctl, 1);
    txParam(0x3A, &colmod, 1);
    for (size_t i = 0; i + 2 < sizeof(kInitSeq);) {
        const uint8_t cmd = kInitSeq[i], len = kInitSeq[i + 1], wait = kInitSeq[i + 2];
        if (txParam(cmd, len ? &kInitSeq[i + 3] : nullptr, len) != ESP_OK) {
            Serial.printf("[lcd] init cmd 0x%02X failed\n", cmd);
            return false;
        }
        if (wait) delay(wait);
        i += 3 + len;
    }
    return true;
}

void initTe()
{
    g_teSem = xSemaphoreCreateBinary();
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << PIN_LCD_TE;
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    cfg.intr_type = GPIO_INTR_POSEDGE;
    gpio_config(&cfg);
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        Serial.println("[lcd] ISR service failed, TE disabled");
        return;
    }
    if (gpio_isr_handler_add((gpio_num_t)PIN_LCD_TE, teIsr, nullptr) == ESP_OK) g_stats.teActive = true;
}

}  // namespace

namespace Display {

bool begin()
{
    g_lvglMutex = xSemaphoreCreateRecursiveMutex();
    for (auto &b : g_bounce) {
        b = (uint16_t *)heap_caps_malloc(BOUNCE_PIXELS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!b) {
            Serial.println("[lcd] no internal DMA memory for bounce buffers");
            return false;
        }
    }
    if (!initPanel()) return false;
    initTe();

    lv_init();

    const size_t bufPixels = (size_t)LCD_H_RES * DRAW_BUF_LINES;
    g_stats.bufBytes = bufPixels * sizeof(lv_color_t);
    lv_color_t *b1 = (lv_color_t *)heap_caps_aligned_alloc(64, g_stats.bufBytes, MALLOC_CAP_SPIRAM);
    lv_color_t *b2 = (lv_color_t *)heap_caps_aligned_alloc(64, g_stats.bufBytes, MALLOC_CAP_SPIRAM);
    g_stats.bufInPsram = b1 && b2;
    if (!g_stats.bufInPsram) {
        // PSRAM disabled in the board menu? Fall back to smaller internal buffers.
        Serial.println("[lcd] PSRAM draw buffers unavailable - is 'OPI PSRAM' enabled? Using internal RAM");
        if (b1) heap_caps_free(b1);
        if (b2) heap_caps_free(b2);
        g_stats.bufBytes = LCD_H_RES * 16 * sizeof(lv_color_t);
        b1 = (lv_color_t *)heap_caps_malloc(g_stats.bufBytes, MALLOC_CAP_INTERNAL);
        b2 = (lv_color_t *)heap_caps_malloc(g_stats.bufBytes, MALLOC_CAP_INTERNAL);
        if (!b1 || !b2) return false;
    }
    lv_disp_draw_buf_init(&g_drawBuf, b1, b2, g_stats.bufBytes / sizeof(lv_color_t));

    lv_disp_drv_init(&g_dispDrv);
    g_dispDrv.hor_res = LCD_H_RES;
    g_dispDrv.ver_res = LCD_V_RES;
    g_dispDrv.flush_cb = flushCb;
    g_dispDrv.rounder_cb = rounderCb;
    g_dispDrv.draw_buf = &g_drawBuf;
    g_disp = lv_disp_drv_register(&g_dispDrv);
    g_fpsWindowStart = millis();
    g_ok = g_disp != nullptr;
    Serial.printf("[lcd] ST77922 ready: %dx%d @ %lu MHz, draw buf 2x%lu B (%s), TE %s\n", LCD_H_RES, LCD_V_RES,
                  (unsigned long)(LCD_QSPI_HZ / 1000000), (unsigned long)g_stats.bufBytes,
                  g_stats.bufInPsram ? "PSRAM" : "internal", g_stats.teActive ? "on" : "off");
    return g_ok;
}

bool ok() { return g_ok; }

void fillScreen(uint16_t rgb565)
{
    if (!g_io) return;
    const uint16_t swapped = (uint16_t)((rgb565 << 8) | (rgb565 >> 8));
    const int rows = BOUNCE_PIXELS / LCD_H_RES;
    for (int i = 0; i < 2; i++) {
        for (int p = 0; p < BOUNCE_PIXELS; p++) g_bounce[i][p] = swapped;
    }
    for (int y = 0; y < LCD_V_RES; y += rows) {
        txWindow(0, y, LCD_H_RES - 1, min(y + rows, LCD_V_RES) - 1, g_bounce[(y / rows) & 1]);
    }
    txParam(0x00, nullptr, 0);   // NOP: waits for the queued transfers to finish
}

bool startTask()
{
    if (!g_ok || g_lvglTask) return false;
    return xTaskCreatePinnedToCore(lvglTask, "lvgl", 12 * 1024, nullptr, 2, &g_lvglTask, 1) == pdPASS;
}

bool lock(uint32_t timeoutMs)
{
    if (!g_lvglMutex) return false;
    TickType_t t = timeoutMs ? pdMS_TO_TICKS(timeoutMs) : portMAX_DELAY;
    return xSemaphoreTakeRecursive(g_lvglMutex, t) == pdTRUE;
}

void unlock()
{
    if (g_lvglMutex) xSemaphoreGiveRecursive(g_lvglMutex);
}

lv_disp_t *disp() { return g_disp; }

Stats stats()
{
    Stats s = g_stats;
    s.teEdges = g_teEdges;
    return s;
}

}  // namespace Display
