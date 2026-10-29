// board_display_p4.cpp - Waveshare ESP32-P4 4.3": ST7701 over 2-lane MIPI-DSI (480x800 RGB565).
//
// Same Display API as the ES3C35P driver (board_display.cpp). The DPI panel owns one frame buffer
// in PSRAM; the LVGL flush copies each partial area into it (synchronous CPU copy + cache write
// back), so my_lvgl can call lv_display_flush_ready() right after the callback. Rotation (180 deg
// and the karaoke landscape mode) is done by LVGL in software. Panel init sequence and timing
// come from the Waveshare BSP esp32_p4_wifi6_touch_lcd_4_3 v1.0.1.
#include "board_display.h"

#include "board_pins.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

const char *TAG = "lcd";
constexpr int DRAW_BUF_LINES = 80;

struct InitCmd {
    uint8_t cmd;
    uint8_t len;
    uint16_t delayMs;
    uint8_t data[16];
};

const InitCmd INIT_CMDS[] = {
    {0xFF, 5, 0, {0x77, 0x01, 0x00, 0x00, 0x13}},
    {0xEF, 1, 0, {0x08}},
    {0xFF, 5, 0, {0x77, 0x01, 0x00, 0x00, 0x10}},
    {0xC0, 2, 0, {0x63, 0x00}},
    {0xC1, 2, 0, {0x0D, 0x02}},
    {0xC2, 2, 0, {0x17, 0x08}},
    {0xCC, 1, 0, {0x10}},
    {0xB0, 16, 0, {0x40, 0xC9, 0x94, 0x0E, 0x10, 0x05, 0x0B, 0x09, 0x08, 0x26, 0x04, 0x52, 0x10, 0x69, 0x6B, 0x69}},
    {0xB1, 16, 0, {0x40, 0xD2, 0x98, 0x0C, 0x92, 0x07, 0x09, 0x08, 0x07, 0x25, 0x02, 0x0E, 0x0C, 0x6E, 0x78, 0x55}},
    {0xFF, 5, 0, {0x77, 0x01, 0x00, 0x00, 0x11}},
    {0xB0, 1, 0, {0x5D}},
    {0xB1, 1, 0, {0x4E}},
    {0xB2, 1, 0, {0x87}},
    {0xB3, 1, 0, {0x80}},
    {0xB5, 1, 0, {0x4E}},
    {0xB7, 1, 0, {0x85}},
    {0xB8, 1, 0, {0x21}},
    {0xB9, 2, 0, {0x10, 0x1F}},
    {0xBB, 1, 0, {0x03}},
    {0xBC, 1, 0, {0x00}},
    {0xC1, 1, 0, {0x78}},
    {0xC2, 1, 0, {0x78}},
    {0xD0, 1, 0, {0x88}},
    {0xE0, 3, 0, {0x00, 0x3A, 0x02}},
    {0xE1, 11, 0, {0x04, 0xA0, 0x00, 0xA0, 0x05, 0xA0, 0x00, 0xA0, 0x00, 0x40, 0x40}},
    {0xE2, 13, 0, {0x30, 0x00, 0x40, 0x40, 0x32, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00}},
    {0xE3, 4, 0, {0x00, 0x00, 0x33, 0x33}},
    {0xE4, 2, 0, {0x44, 0x44}},
    {0xE5, 16, 0, {0x09, 0x2E, 0xA0, 0xA0, 0x0B, 0x30, 0xA0, 0xA0, 0x05, 0x2A, 0xA0, 0xA0, 0x07, 0x2C, 0xA0, 0xA0}},
    {0xE6, 4, 0, {0x00, 0x00, 0x33, 0x33}},
    {0xE7, 2, 0, {0x44, 0x44}},
    {0xE8, 16, 0, {0x08, 0x2D, 0xA0, 0xA0, 0x0A, 0x2F, 0xA0, 0xA0, 0x04, 0x29, 0xA0, 0xA0, 0x06, 0x2B, 0xA0, 0xA0}},
    {0xEB, 7, 0, {0x00, 0x00, 0x4E, 0x4E, 0x00, 0x00, 0x00}},
    {0xEC, 2, 0, {0x08, 0x01}},
    {0xED, 16, 0, {0xB0, 0x2B, 0x98, 0xA4, 0x56, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0x65, 0x4A, 0x89, 0xB2, 0x0B}},
    {0xEF, 6, 0, {0x08, 0x08, 0x08, 0x45, 0x3F, 0x54}},
    {0xFF, 5, 0, {0x77, 0x01, 0x00, 0x00, 0x00}},
    {0x11, 0, 120, {}},   // Sleep out
    {0x29, 0, 0, {}},     // Display on
};

esp_ldo_channel_handle_t g_ldo = nullptr;
esp_lcd_dsi_bus_handle_t g_bus = nullptr;
esp_lcd_panel_io_handle_t g_io = nullptr;
esp_lcd_panel_handle_t g_panel = nullptr;
bool g_panelOk = false;
bool g_ok = false;
bool g_rot180 = false;
bool g_land = false;
bool g_landFlip = false;
const char *g_fail = "";

void flush(int x1, int y1, int x2, int y2, const uint16_t *px, bool)
{
    esp_lcd_panel_draw_bitmap(g_panel, x1, y1, x2 + 1, y2 + 1, px);
}

bool fail(const char *what, esp_err_t err)
{
    ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(err));
    g_fail = what;
    return false;
}

bool initPanel()
{
    esp_ldo_channel_config_t ldo = {};
    ldo.chan_id = LCD_DSI_PHY_LDO_CHAN;
    ldo.voltage_mv = LCD_DSI_PHY_LDO_MV;
    esp_err_t err = esp_ldo_acquire_channel(&ldo, &g_ldo);
    if (err != ESP_OK) return fail("DSI-PHY LDO", err);

    esp_lcd_dsi_bus_config_t bus = {};
    bus.bus_id = 0;
    bus.num_data_lanes = LCD_DSI_LANES;
    bus.phy_clk_src = (mipi_dsi_phy_pllref_clock_source_t)0;   // IDF picks the revision-correct reference
    bus.lane_bit_rate_mbps = LCD_DSI_LANE_MBPS;
    err = esp_lcd_new_dsi_bus(&bus, &g_bus);
    if (err != ESP_OK) return fail("DSI-buss", err);

    esp_lcd_dbi_io_config_t dbi = {};
    dbi.virtual_channel = 0;
    dbi.lcd_cmd_bits = 8;
    dbi.lcd_param_bits = 8;
    err = esp_lcd_new_panel_io_dbi(g_bus, &dbi, &g_io);
    if (err != ESP_OK) return fail("DBI-IO", err);

    esp_lcd_dpi_panel_config_t dpi = {};
    dpi.virtual_channel = 0;
    dpi.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpi.dpi_clock_freq_mhz = LCD_DPI_CLK_MHZ;
    dpi.in_color_format = LCD_COLOR_FMT_RGB565;
    dpi.num_fbs = 1;
    dpi.video_timing.h_size = LCD_H_RES;
    dpi.video_timing.v_size = LCD_V_RES;
    dpi.video_timing.hsync_back_porch = 42;
    dpi.video_timing.hsync_pulse_width = 12;
    dpi.video_timing.hsync_front_porch = 42;
    dpi.video_timing.vsync_back_porch = 2;
    dpi.video_timing.vsync_pulse_width = 8;
    dpi.video_timing.vsync_front_porch = 60;
    err = esp_lcd_new_panel_dpi(g_bus, &dpi, &g_panel);
    if (err != ESP_OK) return fail("DPI-panel", err);

    if (PIN_LCD_RST >= 0) {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << PIN_LCD_RST;
        io.mode = GPIO_MODE_OUTPUT;
        gpio_config(&io);
        gpio_set_level((gpio_num_t)PIN_LCD_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level((gpio_num_t)PIN_LCD_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    } else {
        esp_lcd_panel_io_tx_param(g_io, 0x01, nullptr, 0);   // Software reset
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    const uint8_t madctl = 0x00;   // RGB order, no mirroring
    const uint8_t colmod = 0x55;   // 16 bit/pixel
    err = esp_lcd_panel_io_tx_param(g_io, 0x36, &madctl, 1);
    if (err == ESP_OK) err = esp_lcd_panel_io_tx_param(g_io, 0x3A, &colmod, 1);
    for (const InitCmd &c : INIT_CMDS) {
        if (err != ESP_OK) break;
        err = esp_lcd_panel_io_tx_param(g_io, c.cmd, c.len ? c.data : nullptr, c.len);
        if (c.delayMs) vTaskDelay(pdMS_TO_TICKS(c.delayMs));
    }
    if (err != ESP_OK) return fail("ST7701-init", err);

    err = esp_lcd_panel_init(g_panel);   // Starts the DPI video stream
    if (err != ESP_OK) return fail("DPI-start", err);
    return true;
}

lv_display_rotation_t lvRotation()
{
    if (!g_land) return g_rot180 ? LV_DISPLAY_ROTATION_180 : LV_DISPLAY_ROTATION_0;
    const bool r270 = g_landFlip != g_rot180;
    return r270 ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90;
}

void applyRotation()
{
    if (!g_ok) return;
    MyLvgl::setRotation(lvRotation());
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_layer_top());
}

}  // namespace

namespace Display {

bool begin()
{
    if (!initPanel()) return false;
    g_panelOk = true;
    ESP_LOGI(TAG, "ST7701 MIPI-DSI %dx%d, %d lanes @ %u Mbps, DPI %u MHz", LCD_H_RES, LCD_V_RES, LCD_DSI_LANES,
             (unsigned)LCD_DSI_LANE_MBPS, (unsigned)LCD_DPI_CLK_MHZ);

    MyLvgl::Config cfg;
    cfg.hres = LCD_H_RES;
    cfg.vres = LCD_V_RES;
    cfg.bufLines = DRAW_BUF_LINES;
    cfg.fallbackLines = 16;
    cfg.align = 1;
    cfg.swapBytes = false;   // DPI takes little-endian RGB565 straight from memory
    cfg.flush = flush;
    g_ok = MyLvgl::begin(cfg);
    if (!g_ok) {
        ESP_LOGE(TAG, "LVGL display setup failed");
        g_fail = "LVGL";
        return false;
    }
    if (g_rot180) MyLvgl::setRotation(lvRotation());
    return true;
}

void setRotation(bool rot180)
{
    if (rot180 == g_rot180) return;
    g_rot180 = rot180;
    ESP_LOGI(TAG, "rotation %d (software)", rot180 ? 180 : 0);
    applyRotation();
}

bool rotated() { return g_rot180; }

bool setLandscape(bool on, bool flip)
{
    if (!g_ok) return false;
    g_land = on;
    g_landFlip = flip;
    return MyLvgl::setRotation(lvRotation());
}

bool landscape() { return g_ok && g_land; }

bool ok() { return g_ok; }
bool panelOk() { return g_panelOk; }
const char *failReason() { return g_fail; }

void fillScreen(uint16_t rgb565)
{
    if (!g_panel) return;
    constexpr int ROWS = 40;
    uint16_t *band = (uint16_t *)heap_caps_malloc(LCD_H_RES * ROWS * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!band) band = (uint16_t *)heap_caps_malloc(LCD_H_RES * ROWS * 2, MALLOC_CAP_8BIT);
    if (!band) return;
    for (int i = 0; i < LCD_H_RES * ROWS; i++) band[i] = rgb565;
    for (int y = 0; y < LCD_V_RES; y += ROWS) {
        const int y2 = y + ROWS > LCD_V_RES ? LCD_V_RES : y + ROWS;
        esp_lcd_panel_draw_bitmap(g_panel, 0, y, LCD_H_RES, y2, band);
    }
    heap_caps_free(band);
}

bool startTask() { return g_ok && MyLvgl::startTask(1, 2, 12 * 1024); }

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
