#include "board_display.h"

#include <string.h>

#include "board_backlight.h"
#include "board_pins.h"
#include "board_rgb.h"
#include "esp_rom_sys.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "my_log.h"

static const char *TAG = "lcd";

namespace Display {
void fillScreen(uint16_t rgb565);
}

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
    0x36, 1, 0, 0x00,  // MADCTL: native portrait, RGB (replaced by g_madctl, see sendInitSequence)
    0x35, 1, 20, 0x01,  // TEON
};

esp_lcd_panel_io_handle_t g_io = nullptr;
uint8_t g_madctl = 0x00;   // 0x00 native portrait, 0xC0 = MX|MY (rotated 180 degrees)
bool g_ok = false;
bool g_panelOk = false;
const char *g_fail = "";
uint16_t *g_bounce[2] = {nullptr, nullptr};
uint8_t g_bounceIdx = 0;
SemaphoreHandle_t g_teSem = nullptr;
volatile uint32_t g_teEdges = 0;
uint32_t g_teTimeouts = 0;
bool g_teActive = false;
uint32_t g_teMissRun = 0;

esp_err_t txParam(uint8_t cmd, const uint8_t *data, size_t len)
{
    return esp_lcd_panel_io_tx_param(g_io, (int)((OPCODE_WRITE_CMD << 24) | ((uint32_t)cmd << 8)), data, len);
}

// Window + pixels from a DMA-capable buffer (asynchronous; the next txParam waits until all
// queued colour transfers have completed).
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
    g_teEdges = g_teEdges + 1;
    BaseType_t woken = pdFALSE;
    if (g_teSem) xSemaphoreGiveFromISR(g_teSem, &woken);
    if (woken) portYIELD_FROM_ISR();
}

void waitForTe()
{
    if (!g_teActive || !g_teSem) return;
    xSemaphoreTake(g_teSem, 0);   // Drop a stale edge, wait for a fresh one
    if (xSemaphoreTake(g_teSem, pdMS_TO_TICKS(TE_WAIT_MS)) == pdTRUE) {
        g_teMissRun = 0;
        return;
    }
    g_teTimeouts++;
    if (++g_teMissRun >= TE_GIVE_UP_AFTER) {
        g_teActive = false;
        MyLog::event("lcd", "Inga TE-pulser på GPIO42 - fortsätter utan tear sync");
    }
}

void flush(int x1, int y1, int x2, int y2, const uint16_t *src, bool firstOfFrame)
{
    if (firstOfFrame) waitForTe();
    const int w = x2 - x1 + 1;
    int rowsPerChunk = (BOUNCE_PIXELS / w) & ~3;
    if (rowsPerChunk < 4) rowsPerChunk = 4;
    for (int y = y1; y <= y2; y += rowsPerChunk) {
        int rows = y2 - y + 1;
        if (rows > rowsPerChunk) rows = rowsPerChunk;
        uint16_t *dst = g_bounce[g_bounceIdx];
        g_bounceIdx ^= 1;
        // Safe: this buffer was used two chunks ago and txParam() inside the previous
        // txWindow() waited for that transfer to finish.
        memcpy(dst, src, (size_t)w * rows * 2);
        src += (size_t)w * rows;
        txWindow(x1, y, x2, y + rows - 1, dst);
    }
}

// Bus/IO options. gpioMatrix MUST stay true on IDF 6.1: spicommon_bus_initialize_io() tests
// `flags & SPICOMMON_BUSFLAG_OCTAL` (non-zero for QUAD too) and then re-muxes the IO_MUX pins to
// the octal function 2, so the quad signals never reach GPIO 9/11-14 (log: FuncSel 2, no TE).
// Through the GPIO matrix the pins carry the right signals; 40 MHz is fine via the matrix.
struct BusOpts {
    bool gpioMatrix = true;
    bool dma = true;
    int spiMode = 0;
    uint32_t hz = LCD_QSPI_HZ;
};
BusOpts g_opts;

bool initBus()
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
    bus.max_transfer_sz = g_opts.dma ? BOUNCE_PIXELS * 2 + 64 : 64;
    bus.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD | (g_opts.gpioMatrix ? SPICOMMON_BUSFLAG_GPIO_PINS : 0);
    esp_err_t err = spi_bus_initialize(LCD_HOST, &bus, g_opts.dma ? SPI_DMA_CH_AUTO : SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        g_fail = "SPI-bussen";
        return false;
    }
    ESP_LOGI(TAG, "SPI2 quad bus ok (max transfer %d B)", bus.max_transfer_sz);
    return true;
}

// The panel reset is tied to EN, and an ESP-IDF app reaches this point much sooner after
// power-up than the Arduino builds did (they waited up to 1.5 s for USB CDC). Give the
// controller time to finish its own power-on reset before talking to it.
constexpr uint32_t POWER_ON_SETTLE_MS = 300;

bool sendInitSequence()
{
    // No reset GPIO (tied to EN): software reset, then the vendor sequence.
    if (txParam(0x01, nullptr, 0) != ESP_OK) {
        ESP_LOGE(TAG, "SWRESET failed");
        g_fail = "SWRESET";
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(150));
    const uint8_t colmod = 0x55;   // RGB565
    txParam(0x36, &g_madctl, 1);
    txParam(0x3A, &colmod, 1);
    for (size_t i = 0; i + 2 < sizeof(kInitSeq);) {
        const uint8_t cmd = kInitSeq[i], len = kInitSeq[i + 1], wait = kInitSeq[i + 2];
        const uint8_t *data = cmd == 0x36 && len == 1 ? &g_madctl : len ? &kInitSeq[i + 3] : nullptr;
        if (txParam(cmd, data, len) != ESP_OK) {
            ESP_LOGE(TAG, "init cmd 0x%02X failed", cmd);
            g_fail = "init-sekvensen";
            return false;
        }
        if (wait) vTaskDelay(pdMS_TO_TICKS(wait));
        i += 3 + len;
    }
    return true;
}

bool createIo()
{
    esp_lcd_panel_io_spi_config_t io = {};
    io.cs_gpio_num = (gpio_num_t)PIN_LCD_CS;
    io.dc_gpio_num = GPIO_NUM_NC;
    io.spi_mode = g_opts.spiMode;
    io.pclk_hz = g_opts.hz;
    io.trans_queue_depth = 10;
    io.lcd_cmd_bits = 32;
    io.lcd_param_bits = 8;
    io.flags.quad_mode = 1;
    return esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io, &g_io) == ESP_OK;
}

void destroyIo()
{
    if (g_io) esp_lcd_panel_io_del(g_io);
    g_io = nullptr;
}

void initTe();

// ---------------------------------------------------------------------------
// v0.4.x boot diagnostics (black screen on IDF while the Arduino build worked).
// Each variant initialises the panel in a different way and fills it with its own colour while
// the LED shows the same colour. The log reports TE pulses (only produced by a panel that is
// awake and has TEON) and register read-back, so the log alone tells which path works.
// ---------------------------------------------------------------------------
constexpr bool RUN_DIAGNOSTICS = false;   // Set true to run the variant test at boot again
constexpr uint32_t VARIANT_HOLD_MS = 1500;

void hexLine(char *out, size_t cap, const uint8_t *b, size_t n)
{
    size_t used = 0;
    out[0] = 0;
    for (size_t i = 0; i < n && used + 4 < cap; i++) used += snprintf(out + used, cap - used, "%02X ", b[i]);
}

// Read-back over the SPI driver: opcode 0x03 (single-line read, data on D1/MISO).
void spiReadback(const char *tag)
{
    static const uint8_t regs[] = {0x04, 0x09, 0x0A, 0x0C};
    static const uint8_t lens[] = {3, 4, 1, 1};
    char line[160];
    size_t used = 0;
    line[0] = 0;
    for (size_t r = 0; r < sizeof(regs); r++) {
        uint8_t buf[4] = {0xEE, 0xEE, 0xEE, 0xEE};
        esp_err_t e = esp_lcd_panel_io_rx_param(g_io, (int)((0x03U << 24) | ((uint32_t)regs[r] << 8)), buf, lens[r]);
        char hex[16];
        hexLine(hex, sizeof(hex), buf, lens[r]);
        used += snprintf(line + used, sizeof(line) - used, " %02X:%s%s", regs[r], e == ESP_OK ? "" : "ERR ", hex);
    }
    MyLog::event("lcd", "diag %s read-back:%s", tag, line);
}

// --- Bit-banged single-line access (no SPI peripheral, no DMA) ---
inline void bbBit(bool v)
{
    gpio_ll_set_level(&GPIO, PIN_LCD_D0, v);
    gpio_ll_set_level(&GPIO, PIN_LCD_CLK, 1);
    gpio_ll_set_level(&GPIO, PIN_LCD_CLK, 0);
}

inline void bbByte(uint8_t b)
{
    for (int i = 7; i >= 0; i--) bbBit((b >> i) & 1);
}

void bbBegin(uint8_t opcode, uint8_t cmd)
{
    gpio_ll_set_level(&GPIO, PIN_LCD_CS, 0);
    bbByte(opcode);
    bbByte(0x00);
    bbByte(cmd);
    bbByte(0x00);
}

void bbEnd() { gpio_ll_set_level(&GPIO, PIN_LCD_CS, 1); }

void bbTx(uint8_t cmd, const uint8_t *p, size_t n)
{
    bbBegin(0x02, cmd);
    for (size_t i = 0; i < n; i++) bbByte(p[i]);
    bbEnd();
}

// Reads n bytes after the command word, sampling D1 and D0 (whichever the panel drives).
[[maybe_unused]] void bbRead(uint8_t cmd, uint8_t *d1, uint8_t *d0, size_t n)
{
    bbBegin(0x03, cmd);
    gpio_set_direction((gpio_num_t)PIN_LCD_D0, GPIO_MODE_INPUT);
    for (size_t i = 0; i < n; i++) {
        uint8_t a = 0, b = 0;
        for (int k = 0; k < 8; k++) {
            gpio_ll_set_level(&GPIO, PIN_LCD_CLK, 1);
            esp_rom_delay_us(1);
            a = (uint8_t)((a << 1) | gpio_ll_get_level(&GPIO, PIN_LCD_D1));
            b = (uint8_t)((b << 1) | gpio_ll_get_level(&GPIO, PIN_LCD_D0));
            gpio_ll_set_level(&GPIO, PIN_LCD_CLK, 0);
            esp_rom_delay_us(1);
        }
        d1[i] = a;
        d0[i] = b;
    }
    bbEnd();
    gpio_set_direction((gpio_num_t)PIN_LCD_D0, GPIO_MODE_OUTPUT);
}

void bbSetupPins()
{
    const int outs[] = {PIN_LCD_CS, PIN_LCD_CLK, PIN_LCD_D0, PIN_LCD_D2, PIN_LCD_D3};
    for (int p : outs) {
        gpio_reset_pin((gpio_num_t)p);
        gpio_set_direction((gpio_num_t)p, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)p, p == PIN_LCD_CLK ? 0 : 1);
    }
    gpio_reset_pin((gpio_num_t)PIN_LCD_D1);
    gpio_set_direction((gpio_num_t)PIN_LCD_D1, GPIO_MODE_INPUT);
}

void bbInitAndFill(uint16_t rgb565)
{
    bbTx(0x01, nullptr, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
    const uint8_t madctl = 0x00, colmod = 0x55;
    bbTx(0x36, &madctl, 1);
    bbTx(0x3A, &colmod, 1);
    for (size_t i = 0; i + 2 < sizeof(kInitSeq);) {
        const uint8_t cmd = kInitSeq[i], len = kInitSeq[i + 1], wait = kInitSeq[i + 2];
        bbTx(cmd, len ? &kInitSeq[i + 3] : nullptr, len);
        if (wait) vTaskDelay(pdMS_TO_TICKS(wait));
        i += 3 + len;
    }
    const uint8_t caset[4] = {0, 0, (uint8_t)((LCD_H_RES - 1) >> 8), (uint8_t)(LCD_H_RES - 1)};
    const uint8_t raset[4] = {0, 0, (uint8_t)((LCD_V_RES - 1) >> 8), (uint8_t)(LCD_V_RES - 1)};
    bbTx(0x2A, caset, 4);
    bbTx(0x2B, raset, 4);
    bbBegin(0x02, 0x2C);
    for (int i = 0; i < LCD_H_RES * LCD_V_RES; i++) {
        bbByte((uint8_t)(rgb565 >> 8));
        bbByte((uint8_t)rgb565);
    }
    bbEnd();
}

void showLed(uint16_t rgb565)
{
    Rgb::setBase((rgb565 & 0xF800) ? 200 : 0, (rgb565 & 0x07E0) ? 200 : 0, (rgb565 & 0x001F) ? 200 : 0);
}

// Puts the panel back to reset/sleep over bit-bang (proven to work), so TE stops and the next
// variant has to wake it up on its own. Leaves the SPI bus freed.
uint32_t bbResetPanel()
{
    spi_bus_free(LCD_HOST);
    bbSetupPins();
    bbTx(0x01, nullptr, 0);   // SWRESET: sleep in, TE off
    vTaskDelay(pdMS_TO_TICKS(150));
    bbTx(0x10, nullptr, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    const uint32_t te0 = g_teEdges;
    vTaskDelay(pdMS_TO_TICKS(100));
    return g_teEdges - te0;
}

// Raw SPI device without esp_lcd: single line, manual CS, polling transmit from DMA RAM.
spi_device_handle_t g_raw = nullptr;

void rawTx(const uint8_t *hdr, size_t hn, const uint8_t *data, size_t dn, bool keepCs)
{
    uint8_t *buf = (uint8_t *)g_bounce[1];
    gpio_set_level((gpio_num_t)PIN_LCD_CS, 0);
    if (hn) {
        memcpy(buf, hdr, hn);
        spi_transaction_t t = {};
        t.length = hn * 8;
        t.tx_buffer = buf;
        spi_device_polling_transmit(g_raw, &t);
    }
    while (dn) {
        const size_t n = dn > (size_t)BOUNCE_PIXELS * 2 ? (size_t)BOUNCE_PIXELS * 2 : dn;
        memcpy(buf, data, n);
        spi_transaction_t t = {};
        t.length = n * 8;
        t.tx_buffer = buf;
        spi_device_polling_transmit(g_raw, &t);
        data += n;
        dn -= n;
    }
    if (!keepCs) gpio_set_level((gpio_num_t)PIN_LCD_CS, 1);
}

void rawCmd(uint8_t cmd, const uint8_t *p, size_t n)
{
    const uint8_t hdr[4] = {0x02, 0x00, cmd, 0x00};
    rawTx(hdr, 4, p, n, false);
}

bool rawVariant(uint16_t colour)
{
    g_opts = BusOpts();
    if (!initBus()) return false;
    spi_device_interface_config_t dc = {};
    dc.mode = 0;
    dc.clock_speed_hz = 10 * 1000 * 1000;
    dc.spics_io_num = -1;
    dc.queue_size = 1;
    dc.flags = SPI_DEVICE_HALFDUPLEX;
    if (spi_bus_add_device(LCD_HOST, &dc, &g_raw) != ESP_OK) return false;
    gpio_reset_pin((gpio_num_t)PIN_LCD_CS);
    gpio_set_direction((gpio_num_t)PIN_LCD_CS, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)PIN_LCD_CS, 1);
    rawCmd(0x01, nullptr, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
    const uint8_t madctl = 0x00, colmod = 0x55;
    rawCmd(0x36, &madctl, 1);
    rawCmd(0x3A, &colmod, 1);
    for (size_t i = 0; i + 2 < sizeof(kInitSeq);) {
        const uint8_t cmd = kInitSeq[i], len = kInitSeq[i + 1], wait = kInitSeq[i + 2];
        rawCmd(cmd, len ? &kInitSeq[i + 3] : nullptr, len);
        if (wait) vTaskDelay(pdMS_TO_TICKS(wait));
        i += 3 + len;
    }
    const uint8_t caset[4] = {0, 0, (uint8_t)((LCD_H_RES - 1) >> 8), (uint8_t)(LCD_H_RES - 1)};
    const uint8_t raset[4] = {0, 0, (uint8_t)((LCD_V_RES - 1) >> 8), (uint8_t)(LCD_V_RES - 1)};
    rawCmd(0x2A, caset, 4);
    rawCmd(0x2B, raset, 4);
    uint16_t *line = (uint16_t *)heap_caps_malloc(LCD_H_RES * 2, MALLOC_CAP_INTERNAL);
    if (line) {
        for (int i = 0; i < LCD_H_RES; i++) line[i] = (uint16_t)((colour << 8) | (colour >> 8));
        const uint8_t hdr[4] = {0x02, 0x00, 0x2C, 0x00};
        rawTx(hdr, 4, nullptr, 0, true);
        for (int y = 0; y < LCD_V_RES; y++) rawTx(nullptr, 0, (const uint8_t *)line, LCD_H_RES * 2, y + 1 < LCD_V_RES);
        heap_caps_free(line);
    }
    spi_bus_remove_device(g_raw);
    g_raw = nullptr;
    return true;
}

struct Variant {
    const char *name;
    BusOpts opts;
    bool raw;
    uint16_t colour;
};

uint32_t runVariant(const Variant &v, int idx)
{
    const uint32_t teReset = bbResetPanel();
    bool ok;
    if (v.raw) {
        showLed(v.colour);
        ok = rawVariant(v.colour);
    } else {
        g_opts = v.opts;
        ok = initBus() && createIo();
        if (ok) {
            ok = sendInitSequence();
            showLed(v.colour);
            Display::fillScreen(v.colour);
        }
    }
    const uint32_t te0 = g_teEdges;
    vTaskDelay(pdMS_TO_TICKS(VARIANT_HOLD_MS));
    const uint32_t te = g_teEdges - te0;
    MyLog::event("lcd", "diag %d %s: TE after bb-reset %u/100ms, setup %s, TE %u pulses in %u ms -> %s", idx, v.name,
                 (unsigned)teReset, ok ? "ok" : "FAILED", (unsigned)te, (unsigned)VARIANT_HOLD_MS,
                 te > 100 ? "PANEL AWAKE" : "no response");
    if (!v.raw && g_io) {
        if (idx == 1) spiReadback(v.name);
        destroyIo();
    }
    return te;
}

bool runDiagnostics(BusOpts *chosen)
{
    Backlight::set(100);
    MyLog::event("lcd", "diag start: TE level %d", gpio_get_level((gpio_num_t)PIN_LCD_TE));
    g_opts = BusOpts();
    if (initBus()) {
        fflush(stdout);
        gpio_dump_io_configuration(stdout, (1ULL << PIN_LCD_CS) | (1ULL << PIN_LCD_CLK) | (1ULL << PIN_LCD_D0) |
                                               (1ULL << PIN_LCD_D1) | (1ULL << PIN_LCD_D2) | (1ULL << PIN_LCD_D3));
        fflush(stdout);
    }
    BusOpts gm;
    gm.gpioMatrix = true;
    BusOpts nodma;
    nodma.dma = false;
    BusOpts mode3;
    mode3.spiMode = 3;
    BusOpts slow;
    slow.gpioMatrix = true;
    slow.hz = 10 * 1000 * 1000;
    const Variant variants[] = {
        {"iomux-dma-40", BusOpts(), false, 0xF800},   // 1 red (as v0.4.0)
        {"gpiomatrix-40", gm, false, 0x07E0},         // 2 green
        {"no-dma-40", nodma, false, 0xFFFF},          // 3 white
        {"mode3-40", mode3, false, 0xF81F},           // 4 magenta
        {"gpiomatrix-10", slow, false, 0x07FF},       // 5 cyan
        {"raw-1line-10", BusOpts(), true, 0xFFE0},    // 6 yellow
    };
    bool found = false;
    for (size_t i = 0; i < sizeof(variants) / sizeof(variants[0]); i++) {
        const uint32_t te = runVariant(variants[i], (int)i + 1);
        if (!found && !variants[i].raw && te > 100) {
            found = true;
            *chosen = variants[i].opts;
        }
    }
    spi_bus_free(LCD_HOST);
    Rgb::setBase(0, 0, 0);
    MyLog::event("lcd", "diag done: %s", found ? "using first esp_lcd variant that woke the panel"
                                               : "no esp_lcd variant woke the panel - showing blue via bit-bang");
    if (!found) {
        bbSetupPins();
        bbInitAndFill(0x001F);
    }
    return found;
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
        ESP_LOGW(TAG, "ISR service failed, TE disabled");
        return;
    }
    if (gpio_isr_handler_add((gpio_num_t)PIN_LCD_TE, teIsr, nullptr) == ESP_OK) g_teActive = true;
}

}  // namespace

namespace Display {

bool begin()
{
    for (auto &b : g_bounce) {
        b = (uint16_t *)heap_caps_malloc(BOUNCE_PIXELS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!b) {
            ESP_LOGE(TAG, "no internal DMA memory for bounce buffers");
            g_fail = "DMA-minne";
            return false;
        }
    }
    const int64_t sinceBootMs = esp_timer_get_time() / 1000;
    if (sinceBootMs < (int64_t)POWER_ON_SETTLE_MS) vTaskDelay(pdMS_TO_TICKS(POWER_ON_SETTLE_MS - sinceBootMs));
    initTe();   // Before the panel init so the diagnostics can count TE pulses
    if (RUN_DIAGNOSTICS) {
        BusOpts chosen;
        if (!runDiagnostics(&chosen)) {
            g_fail = "SPI når inte panelen";
            return false;
        }
        g_opts = chosen;
    }
    if (!initBus()) return false;
    if (!createIo()) {
        ESP_LOGE(TAG, "panel IO failed");
        g_fail = "panel-IO";
        return false;
    }
    if (!sendInitSequence()) {
        ESP_LOGW(TAG, "init sequence failed - retrying once");
        vTaskDelay(pdMS_TO_TICKS(200));
        if (!sendInitSequence()) return false;
    }
    g_panelOk = true;
    {
        const uint32_t te0 = g_teEdges;
        vTaskDelay(pdMS_TO_TICKS(50));
        const uint32_t te = g_teEdges - te0;
        if (te == 0) MyLog::event("lcd", "VARNING: inga TE-pulser efter init - panelen svarar inte");
        else ESP_LOGI(TAG, "panel awake: %u TE pulses in 50 ms", (unsigned)te);
    }
    ESP_LOGI(TAG, "ST77922 init sequence sent (%u MHz, %s, %s DMA, mode %d)", (unsigned)(g_opts.hz / 1000000), g_opts.gpioMatrix ? "GPIO matrix" : "IO_MUX", g_opts.dma ? "with" : "no", g_opts.spiMode);

    MyLvgl::Config cfg;
    cfg.hres = LCD_H_RES;
    cfg.vres = LCD_V_RES;
    cfg.bufLines = DRAW_BUF_LINES;
    cfg.fallbackLines = 16;
    cfg.align = 4;
    cfg.swapBytes = true;
    cfg.flush = flush;
    g_ok = MyLvgl::begin(cfg);
    if (!g_ok) {
        ESP_LOGE(TAG, "LVGL display setup failed");
        g_fail = "LVGL";
    }
    ESP_LOGI(TAG, "ST77922 %dx%d @ %u MHz, TE %s", LCD_H_RES, LCD_V_RES, (unsigned)(LCD_QSPI_HZ / 1000000),
             g_teActive ? "on" : "off");
    return g_ok;
}

void setRotation(bool rot180)
{
    const uint8_t v = rot180 ? 0xC0 : 0x00;
    if (v == g_madctl) return;
    g_madctl = v;
    if (!g_panelOk) return;   // begin() sends it with the init sequence
    txParam(0x36, &g_madctl, 1);
    ESP_LOGI(TAG, "MADCTL 0x%02X (rotation %d)", g_madctl, rot180 ? 180 : 0);
    if (g_ok) {
        lv_obj_invalidate(lv_screen_active());
        lv_obj_invalidate(lv_layer_top());
    }
}

bool rotated() { return g_madctl == 0xC0; }

bool setLandscape(bool on, bool flip)
{
    if (!g_ok) return false;
    return MyLvgl::setRotation(on ? (flip ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90) : LV_DISPLAY_ROTATION_0);
}

bool landscape() { return g_ok && MyLvgl::rotation() != LV_DISPLAY_ROTATION_0; }

bool ok() { return g_ok; }
bool panelOk() { return g_panelOk; }
const char *failReason() { return g_fail; }

void fillScreen(uint16_t rgb565)
{
    if (!g_io) return;
    const uint16_t swapped = (uint16_t)((rgb565 << 8) | (rgb565 >> 8));
    const int rows = BOUNCE_PIXELS / LCD_H_RES;
    for (int i = 0; i < 2; i++) {
        for (int p = 0; p < BOUNCE_PIXELS; p++) g_bounce[i][p] = swapped;
    }
    for (int y = 0; y < LCD_V_RES; y += rows) {
        int y2 = y + rows > LCD_V_RES ? LCD_V_RES : y + rows;
        txWindow(0, y, LCD_H_RES - 1, y2 - 1, g_bounce[(y / rows) & 1]);
    }
    txParam(0x00, nullptr, 0);   // NOP: waits for the queued transfers to finish
}

bool startTask() { return g_ok && MyLvgl::startTask(1, 2, 12 * 1024); }

bool panelSleepSupported() { return false; }
bool panelSleeping() { return false; }
bool setPanelSleeping(bool sleeping)
{
    if (!sleeping) return true;
    ESP_LOGW(TAG, "panel sleep is not enabled on S3");
    return false;
}

Stats stats()
{
    MyLvgl::Stats l = MyLvgl::stats();
    Stats s = {};
    s.flushes = l.flushes;
    s.frames = l.frames;
    s.fps = l.fps;
    s.bufBytes = l.bufBytes;
    s.bufInPsram = l.bufInPsram;
    s.teEdges = g_teEdges;
    s.teTimeouts = g_teTimeouts;
    s.teActive = g_teActive;
    return s;
}

}  // namespace Display
