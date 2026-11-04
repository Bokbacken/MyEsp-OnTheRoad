// Native esp_lcd QSPI port: partial/rotated LVGL areas update a full physical frame.
// AXS15231B has no QSPI row-address command: every transfer starts at row zero.
#include "board_display.h"

#include <atomic>
#include <string.h>

#include "board_pins.h"
#include "board_power.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

const char *TAG = "lcd";
constexpr int DRAW_LINES = 40;
constexpr int DMA_LINES = 24;
constexpr size_t FRAME_PIXELS = LCD_H_RES * LCD_V_RES;
constexpr size_t DMA_BYTES = LCD_H_RES * DMA_LINES * sizeof(uint16_t);
constexpr uint32_t DMA_TIMEOUT_MS = 1000;

struct InitCmd {
    uint8_t cmd;
    uint8_t len;
    uint16_t delayMs;
    uint8_t data[31];
};

// Hardware register data only, from Waveshare's Apache-2.0 factory BSP:
// examples/factory/components/esp32_s3_touch_lcd_3_5b/esp32_s3_touch_lcd_3_5b.cpp
// https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5B
// Vendor revision: c7dd63c70258da7e3b6dae0c621f30ca62b1e1c9.
// Copyright 2026 Waveshare
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this register data except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// A local copy is included in licenses/Waveshare-ESP32-S3-Touch-LCD-3.5B.txt.
//
// Adapted here to fixed-size data records; no vendor driver implementation is copied.
const InitCmd INIT_CMDS[] = {
    {0x36, 1, 0, {0x00}},
    {0x3A, 1, 0, {0x55}},
    {0xBB, 8, 0, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0xA5}},
    {0xA0, 17, 0, {0xC0, 0x10, 0x00, 0x02, 0x00, 0x00, 0x04, 0x3F, 0x20, 0x05, 0x3F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {0xA2, 31, 0, {0x30, 0x3C, 0x24, 0x14, 0xD0, 0x20, 0xFF, 0xE0, 0x40, 0x19, 0x80, 0x80, 0x80, 0x20, 0xF9, 0x10, 0x02, 0xFF, 0xFF, 0xF0, 0x90, 0x01, 0x32, 0xA0, 0x91, 0xE0, 0x20, 0x7F, 0xFF, 0x00, 0x5A}},
    {0xD0, 30, 0, {0xE0, 0x40, 0x51, 0x24, 0x08, 0x05, 0x10, 0x01, 0x20, 0x15, 0x42, 0xC2, 0x22, 0x22, 0xAA, 0x03, 0x10, 0x12, 0x60, 0x14, 0x1E, 0x51, 0x15, 0x00, 0x8A, 0x20, 0x00, 0x03, 0x3A, 0x12}},
    {0xA3, 22, 0, {0xA0, 0x06, 0xAA, 0x00, 0x08, 0x02, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x55, 0x55}},
    {0xC1, 30, 0, {0x31, 0x04, 0x02, 0x02, 0x71, 0x05, 0x24, 0x55, 0x02, 0x00, 0x41, 0x00, 0x53, 0xFF, 0xFF, 0xFF, 0x4F, 0x52, 0x00, 0x4F, 0x52, 0x00, 0x45, 0x3B, 0x0B, 0x02, 0x0D, 0x00, 0xFF, 0x40}},
    {0xC3, 11, 0, {0x00, 0x00, 0x00, 0x50, 0x03, 0x00, 0x00, 0x00, 0x01, 0x80, 0x01}},
    {0xC4, 29, 0, {0x00, 0x24, 0x33, 0x80, 0x00, 0xEA, 0x64, 0x32, 0xC8, 0x64, 0xC8, 0x32, 0x90, 0x90, 0x11, 0x06, 0xDC, 0xFA, 0x00, 0x00, 0x80, 0xFE, 0x10, 0x10, 0x00, 0x0A, 0x0A, 0x44, 0x50}},
    {0xC5, 23, 0, {0x18, 0x00, 0x00, 0x03, 0xFE, 0x3A, 0x4A, 0x20, 0x30, 0x10, 0x88, 0xDE, 0x0D, 0x08, 0x0F, 0x0F, 0x01, 0x3A, 0x4A, 0x20, 0x10, 0x10, 0x00}},
    {0xC6, 20, 0, {0x05, 0x0A, 0x05, 0x0A, 0x00, 0xE0, 0x2E, 0x0B, 0x12, 0x22, 0x12, 0x22, 0x01, 0x03, 0x00, 0x3F, 0x6A, 0x18, 0xC8, 0x22}},
    {0xC7, 20, 0, {0x50, 0x32, 0x28, 0x00, 0xA2, 0x80, 0x8F, 0x00, 0x80, 0xFF, 0x07, 0x11, 0x9C, 0x67, 0xFF, 0x24, 0x0C, 0x0D, 0x0E, 0x0F}},
    {0xC9, 4, 0, {0x33, 0x44, 0x44, 0x01}},
    {0xCF, 27, 0, {0x2C, 0x1E, 0x88, 0x58, 0x13, 0x18, 0x56, 0x18, 0x1E, 0x68, 0x88, 0x00, 0x65, 0x09, 0x22, 0xC4, 0x0C, 0x77, 0x22, 0x44, 0xAA, 0x55, 0x08, 0x08, 0x12, 0xA0, 0x08}},
    {0xD5, 30, 0, {0x40, 0x8E, 0x8D, 0x01, 0x35, 0x04, 0x92, 0x74, 0x04, 0x92, 0x74, 0x04, 0x08, 0x6A, 0x04, 0x46, 0x03, 0x03, 0x03, 0x03, 0x82, 0x01, 0x03, 0x00, 0xE0, 0x51, 0xA1, 0x00, 0x00, 0x00}},
    {0xD6, 30, 0, {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x93, 0x00, 0x01, 0x83, 0x07, 0x07, 0x00, 0x07, 0x07, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x00, 0x84, 0x00, 0x20, 0x01, 0x00}},
    {0xD7, 19, 0, {0x03, 0x01, 0x0B, 0x09, 0x0F, 0x0D, 0x1E, 0x1F, 0x18, 0x1D, 0x1F, 0x19, 0x40, 0x8E, 0x04, 0x00, 0x20, 0xA0, 0x1F}},
    {0xD8, 12, 0, {0x02, 0x00, 0x0A, 0x08, 0x0E, 0x0C, 0x1E, 0x1F, 0x18, 0x1D, 0x1F, 0x19}},
    {0xD9, 12, 0, {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}},
    {0xDD, 12, 0, {0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}},
    {0xDF, 8, 0, {0x44, 0x73, 0x4B, 0x69, 0x00, 0x0A, 0x02, 0x90}},
    {0xE0, 17, 0, {0x3B, 0x28, 0x10, 0x16, 0x0C, 0x06, 0x11, 0x28, 0x5C, 0x21, 0x0D, 0x35, 0x13, 0x2C, 0x33, 0x28, 0x0D}},
    {0xE1, 17, 0, {0x37, 0x28, 0x10, 0x16, 0x0B, 0x06, 0x11, 0x28, 0x5C, 0x21, 0x0D, 0x35, 0x14, 0x2C, 0x33, 0x28, 0x0F}},
    {0xE2, 17, 0, {0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D}},
    {0xE3, 17, 0, {0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x32, 0x2F, 0x0F}},
    {0xE4, 17, 0, {0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D}},
    {0xE5, 17, 0, {0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0F}},
    {0xA4, 16, 0, {0x85, 0x85, 0x95, 0x82, 0xAF, 0xAA, 0xAA, 0x80, 0x10, 0x30, 0x40, 0x40, 0x20, 0xFF, 0x60, 0x30}},
    {0xA4, 4, 0, {0x85, 0x85, 0x95, 0x85}},
    {0xBB, 8, 0, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {0x13, 0, 0, {}},
    {0x11, 0, 120, {}},
    {0x2C, 4, 0, {0x00, 0x00, 0x00, 0x00}},
    {0x29, 0, 20, {}},
};

esp_lcd_panel_io_handle_t g_io = nullptr;
SemaphoreHandle_t g_done = nullptr;
uint16_t *g_frame = nullptr;
uint16_t *g_dma = nullptr;
bool g_spiCreated = false;
bool g_attempted = false;
bool g_taskStarted = false;
bool g_touchRecoveryAttempted = false;
std::atomic<bool> g_ok{false};
std::atomic<bool> g_panelOk{false};
std::atomic<bool> g_transportFault{false};
bool g_rot180 = false;
bool g_land = false;
bool g_landFlip = false;
std::atomic<const char *> g_fail{""};

bool fail(const char *what, esp_err_t err)
{
    g_fail = what;
    ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(err));
    return false;
}

bool transportFail(const char *what, esp_err_t err)
{
    // A timed-out DMA may still own g_dma. Keep all transport resources alive,
    // and never queue another transaction or overwrite that buffer until reboot.
    g_transportFault = true;
    g_ok = false;
    g_panelOk = false;
    return fail(what, err);
}

esp_err_t command(uint8_t cmd, const void *data = nullptr, size_t len = 0)
{
    return esp_lcd_panel_io_tx_param(g_io, (0x02U << 24) | (uint32_t(cmd) << 8), data, len);
}

bool IRAM_ATTR transferDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(g_done, &woken);
    return woken == pdTRUE;
}

bool sendFrame()
{
    if (!g_panelOk || g_transportFault) return false;
    const uint8_t columns[] = {0, 0, uint8_t((LCD_H_RES - 1) >> 8), uint8_t(LCD_H_RES - 1)};
    esp_err_t err = command(0x2A, columns, sizeof(columns));
    if (err != ESP_OK) return transportFail("LCD column window", err);
    for (int y = 0; y < LCD_V_RES; y += DMA_LINES) {
        const int rows = y + DMA_LINES > LCD_V_RES ? LCD_V_RES - y : DMA_LINES;
        const size_t bytes = size_t(LCD_H_RES) * rows * sizeof(uint16_t);
        memcpy(g_dma, g_frame + y * LCD_H_RES, bytes);
        const uint8_t cmd = y == 0 ? 0x2C : 0x3C;
        err = esp_lcd_panel_io_tx_color(g_io, (0x32U << 24) | (uint32_t(cmd) << 8), g_dma, bytes);
        if (err != ESP_OK) return transportFail("LCD DMA queue", err);
        if (xSemaphoreTake(g_done, pdMS_TO_TICKS(DMA_TIMEOUT_MS)) != pdTRUE) {
            return transportFail("LCD DMA timeout (reboot required)", ESP_ERR_TIMEOUT);
        }
    }
    return true;
}

void flush(int x1, int y1, int x2, int y2, const uint16_t *px, bool)
{
    if (!g_ok) return;
    if (!px || x1 < 0 || y1 < 0 || x2 < x1 || y2 < y1 || x2 >= LCD_H_RES || y2 >= LCD_V_RES) {
        transportFail("LCD flush bounds", ESP_ERR_INVALID_ARG);
        return;
    }
    const size_t width = x2 - x1 + 1;
    for (int y = y1; y <= y2; y++) {
        memcpy(g_frame + y * LCD_H_RES + x1, px, width * sizeof(uint16_t));
        px += width;
    }
    if (lv_display_flush_is_last(MyLvgl::display())) sendFrame();
}

void cleanupInit()
{
    if (g_io) {
        esp_err_t err = esp_lcd_panel_io_del(g_io);
        if (err != ESP_OK) ESP_LOGE(TAG, "LCD IO cleanup: %s", esp_err_to_name(err));
        g_io = nullptr;
    }
    if (g_spiCreated) {
        esp_err_t err = spi_bus_free(SPI2_HOST);
        if (err != ESP_OK) ESP_LOGE(TAG, "LCD SPI cleanup: %s", esp_err_to_name(err));
        g_spiCreated = false;
    }
    if (g_done) vSemaphoreDelete(g_done);
    g_done = nullptr;
    heap_caps_free(g_dma);
    heap_caps_free(g_frame);
    g_dma = g_frame = nullptr;
}

bool configurePanel()
{
    for (const InitCmd &c : INIT_CMDS) {
        const esp_err_t err = command(c.cmd, c.len ? c.data : nullptr, c.len);
        if (err != ESP_OK) return fail("AXS15231B init command", err);
        if (c.delayMs) vTaskDelay(pdMS_TO_TICKS(c.delayMs));
    }
    g_panelOk = true;
    return sendFrame();
}

bool initPanel()
{
    if (!BoardPower::bus() || !BoardPower::status().initialized) {
        return fail("BoardPower must initialize before LCD", ESP_ERR_INVALID_STATE);
    }
    g_done = xSemaphoreCreateBinary();
    g_frame = static_cast<uint16_t *>(heap_caps_calloc(FRAME_PIXELS, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    g_dma = static_cast<uint16_t *>(heap_caps_malloc(DMA_BYTES, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    if (!g_done || !g_frame || !g_dma) return fail("LCD frame/DMA memory", ESP_ERR_NO_MEM);

    spi_bus_config_t bus = {};
    bus.sclk_io_num = PIN_LCD_CLK;
    bus.data0_io_num = PIN_LCD_D0;
    bus.data1_io_num = PIN_LCD_D1;
    bus.data2_io_num = PIN_LCD_D2;
    bus.data3_io_num = PIN_LCD_D3;
    bus.max_transfer_sz = DMA_BYTES;
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) return fail("LCD SPI bus", err);
    g_spiCreated = true;

    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = static_cast<gpio_num_t>(PIN_LCD_CS);
    io.dc_gpio_num = GPIO_NUM_NC;
    io.spi_mode = 3;
    io.pclk_hz = LCD_QSPI_HZ;
    io.trans_queue_depth = 1;
    io.on_color_trans_done = transferDone;
    io.lcd_cmd_bits = 32;
    io.lcd_param_bits = 8;
    io.flags.quad_mode = true;
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io, &g_io);
    if (err != ESP_OK) return fail("LCD QSPI IO", err);

    // BoardPower has already pulsed the shared LCD/touch reset on TCA9554 EXIO1.
    return configurePanel();
}

lv_display_rotation_t rotationFor(bool rot180, bool land, bool flip)
{
    if (!land) return rot180 ? LV_DISPLAY_ROTATION_180 : LV_DISPLAY_ROTATION_0;
    return flip != rot180 ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90;
}

bool applyRotation(bool rot180, bool land, bool flip)
{
    if (!MyLvgl::setRotation(rotationFor(rot180, land, flip))) return fail("LVGL rotation", ESP_ERR_NO_MEM);
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_layer_top());
    return true;
}

}  // namespace

namespace Display {

bool begin()
{
    if (g_attempted) return g_ok;
    g_attempted = true;
    if (!initPanel()) {
        if (!g_transportFault) cleanupInit();
        return false;
    }
    MyLvgl::Config cfg;
    cfg.hres = LCD_H_RES;
    cfg.vres = LCD_V_RES;
    cfg.bufLines = DRAW_LINES;
    cfg.fallbackLines = 16;
    cfg.align = 2;  // RGB565 row strides stay packed, including rotated partial areas.
    cfg.swapBytes = true;
    cfg.flush = flush;
    if (!MyLvgl::begin(cfg)) return fail("LVGL display", ESP_ERR_NO_MEM);
    if (g_rot180 && !applyRotation(g_rot180, g_land, g_landFlip)) return false;
    g_ok = true;
    ESP_LOGI(TAG, "AXS15231B %dx%d QSPI %u Hz, full-frame stream, no TE", LCD_H_RES, LCD_V_RES, (unsigned)LCD_QSPI_HZ);
    return true;
}

bool ok() { return g_ok; }
bool recoverTouchStartup()
{
    if (!g_ok || g_transportFault || g_taskStarted || g_touchRecoveryAttempted)
        return fail("LCD/touch startup recovery unavailable", ESP_ERR_INVALID_STATE);
    g_touchRecoveryAttempted = true;
    ESP_LOGW(TAG, "touch startup failed; resetting shared LCD/touch and reinitializing panel");
    g_ok = false;
    g_panelOk = false;
    if (!BoardPower::resetPanel()) return fail("LCD/touch recovery reset", ESP_ERR_INVALID_STATE);
    if (!configurePanel()) {
        g_panelOk = false;
        return false;
    }
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_layer_top());
    g_fail = "";
    g_ok = true;
    return true;
}

bool panelOk() { return g_panelOk; }
const char *failReason() { return g_fail; }
bool panelSleepSupported() { return false; }
bool panelSleeping() { return false; }

bool setPanelSleeping(bool sleeping)
{
    if (!g_panelOk || g_transportFault) return fail("LCD power unavailable", ESP_ERR_INVALID_STATE);
    if (sleeping) {
        ESP_LOGW(TAG, "panel sleep disabled to keep polled touch available for wake");
        return false;
    }
    return true;
}

void fillScreen(uint16_t rgb565)
{
    if (!g_panelOk || g_transportFault) return;
    const bool locked = MyLvgl::ok();
    if (locked && !MyLvgl::lock(DMA_TIMEOUT_MS)) {
        fail("LCD fill lock", ESP_ERR_TIMEOUT);
        return;
    }
    const uint16_t swapped = (rgb565 >> 8) | (rgb565 << 8);
    for (size_t i = 0; i < FRAME_PIXELS; i++) g_frame[i] = swapped;
    sendFrame();
    if (locked) MyLvgl::unlock();
}

bool startTask()
{
    if (!g_ok) return false;
    if (!MyLvgl::startTask(1, 2, 12 * 1024)) return fail("LVGL task", ESP_ERR_NO_MEM);
    g_taskStarted = true;
    return true;
}

void setRotation(bool rot180)
{
    if (rot180 == g_rot180) return;
    if (g_ok && !applyRotation(rot180, g_land, g_landFlip)) return;
    g_rot180 = rot180;
}

bool rotated() { return g_rot180; }

bool setLandscape(bool on, bool flip)
{
    if (!g_ok || !applyRotation(g_rot180, on, flip)) return false;
    g_land = on;
    g_landFlip = flip;
    return true;
}

bool landscape() { return g_ok && g_land; }

Stats stats()
{
    MyLvgl::Stats l = MyLvgl::stats();
    Stats s = {};
    s.flushes = l.flushes;
    s.frames = l.frames;
    s.fps = l.fps;
    s.bufBytes = l.bufBytes;
    s.bufInPsram = l.bufInPsram;
    return s;
}

}  // namespace Display
