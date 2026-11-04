// Host mock platform; test_s3_35b_display_touch.py inserts the unmodified driver
// bodies at DRIVER_UNDER_TEST, replacing only their ESP-IDF include directives.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "../main/board_pins_s3_35b.h"

using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_ERR_INVALID_STATE = 1, ESP_ERR_NO_MEM = 2,
              ESP_ERR_TIMEOUT = 3, ESP_ERR_INVALID_ARG = 4,
              ESP_ERR_NOT_FOUND = 5, ESP_ERR_INVALID_RESPONSE = 6;
const char *esp_err_to_name(int) { return "mock error"; }
template<class... Args> void logMock(Args...) {}
#define ESP_LOGE(...) logMock(__VA_ARGS__)
#define ESP_LOGW(...) logMock(__VA_ARGS__)
#define ESP_LOGI(...) logMock(__VA_ARGS__)
#define IRAM_ATTR
using i2c_master_bus_handle_t = void *;
bool powerReady = true;
bool powerInitialized = true;
namespace BoardPower {
struct Status { bool initialized; };
void *bus() { return powerReady ? reinterpret_cast<void *>(1) : nullptr; }
Status status() { return {powerInitialized}; }
}

struct lv_display_t {};
struct lv_indev_t {};
struct lv_obj_t {};
enum lv_display_rotation_t { LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90,
                             LV_DISPLAY_ROTATION_180, LV_DISPLAY_ROTATION_270 };
lv_display_t displayMock;
lv_indev_t pointerMock;
bool lastFlush = true, lvReady = false, rotationFail = false, lvBeginFail = false, pointerFail = false;
int invalidations = 0, beginCount = 0;
lv_display_rotation_t actualRotation = LV_DISPLAY_ROTATION_0;
bool lv_display_flush_is_last(lv_display_t *) { return lastFlush; }
lv_obj_t *lv_screen_active() { return nullptr; }
lv_obj_t *lv_layer_top() { return nullptr; }
void lv_obj_invalidate(lv_obj_t *) { invalidations++; }
namespace MyLvgl {
using FlushFn = void (*)(int, int, int, int, const uint16_t *, bool);
using PointerFn = bool (*)(int16_t *, int16_t *);
struct Config {
    int hres = 320, vres = 480, bufLines = 40, fallbackLines = 16;
    uint8_t align = 1;
    bool swapBytes = true;
    FlushFn flush = nullptr;
};
struct Stats { uint32_t flushes = 3, frames = 2; float fps = 1; uint32_t bufBytes = 25600; bool bufInPsram = true; };
Config config;
PointerFn pointer = nullptr;
bool begin(const Config &c) { config = c; beginCount++; lvReady = !lvBeginFail; return lvReady; }
bool ok() { return lvReady; }
lv_display_t *display() { return &displayMock; }
bool setRotation(lv_display_rotation_t r) { if (rotationFail) return false; actualRotation = r; return true; }
bool startTask(int, int, uint32_t) { return true; }
bool lock(uint32_t = 0) { return true; }
void unlock() {}
Stats stats() { return {}; }
lv_indev_t *addPointer(PointerFn p) { pointer = p; return pointerFail ? nullptr : &pointerMock; }
}
namespace Display {
bool setLandscape(bool on, bool flip = false);
struct Stats {
    uint32_t flushes, frames, teEdges, teTimeouts;
    bool teActive;
    float fps;
    uint32_t bufBytes;
    bool bufInPsram;
};
}
namespace Touch {
struct Info {
    bool present;
    uint16_t fwVersion;
    uint8_t fwRevision[4];
    uint16_t maxX, maxY;
    uint8_t maxTouches;
    uint16_t lastX, lastY;
    bool pressed;
    uint32_t touchCount, irqCount, readErrors, bridgedGaps;
    uint8_t address;
};
bool deviceFound(uint8_t);
}

#ifdef TEST_DISPLAY
constexpr int SPI2_HOST = 2, SPI_DMA_CH_AUTO = 0;
constexpr int MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2, MALLOC_CAP_INTERNAL = 4, MALLOC_CAP_DMA = 8;
constexpr int pdFALSE = 0, pdTRUE = 1;
using BaseType_t = int;
struct Semaphore { int tokens = 0; };
using SemaphoreHandle_t = Semaphore *;
int allocations = 0, liveAllocations = 0, failAllocation = 0, semaphores = 0;
bool semaphoreFail = false, dmaTimeout = false;
int spiError = ESP_OK, ioError = ESP_OK, colorError = ESP_OK, commandError = ESP_OK;
int failCommand = -1, spiInitializations = 0, spiFrees = 0, ioDeletes = 0;
int delays = 0;
void *heap_caps_malloc(size_t n, int) {
    if (++allocations == failAllocation) return nullptr;
    void *p = std::malloc(n);
    if (p) liveAllocations++;
    return p;
}
void *heap_caps_calloc(size_t n, size_t s, int caps) {
    void *p = heap_caps_malloc(n * s, caps);
    if (p) std::memset(p, 0, n * s);
    return p;
}
void heap_caps_free(void *p) { if (p) { liveAllocations--; std::free(p); } }
SemaphoreHandle_t xSemaphoreCreateBinary() { if (semaphoreFail) return nullptr; semaphores++; return new Semaphore; }
void vSemaphoreDelete(SemaphoreHandle_t p) { semaphores--; delete p; }
void xSemaphoreGiveFromISR(SemaphoreHandle_t p, BaseType_t *) { p->tokens++; }
bool xSemaphoreTake(SemaphoreHandle_t p, int timeout) {
    assert(timeout > 0 && timeout <= 1000);
    if (!p->tokens) return false;
    p->tokens--;
    return true;
}
int pdMS_TO_TICKS(int n) { return n; }
void vTaskDelay(int n) { delays += n; }
struct spi_bus_config_t {
    int sclk_io_num = -1, data0_io_num = -1, data1_io_num = -1,
        data2_io_num = -1, data3_io_num = -1, max_transfer_sz = 0;
};
using esp_lcd_panel_io_handle_t = void *;
using esp_lcd_spi_bus_handle_t = uintptr_t;
struct esp_lcd_panel_io_event_data_t {};
using DoneFn = bool (*)(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t *, void *);
struct esp_lcd_panel_io_spi_config_t {
    int cs_gpio_num = -1, dc_gpio_num = -1, spi_mode = 0, pclk_hz = 0, trans_queue_depth = 0;
    DoneFn on_color_trans_done = nullptr;
    int lcd_cmd_bits = 0, lcd_param_bits = 0;
    struct { bool quad_mode = false; } flags;
};
DoneFn doneFn = nullptr;
std::vector<uint32_t> commands, colors;
std::vector<std::vector<uint8_t>> colorData;
int spi_bus_initialize(int host, const spi_bus_config_t *b, int) {
    assert(host == SPI2_HOST && b->sclk_io_num == 5 && b->data0_io_num == 1 &&
           b->data1_io_num == 2 && b->data2_io_num == 3 && b->data3_io_num == 4);
    assert(b->max_transfer_sz > 0 && b->max_transfer_sz <= 320 * 40 * 2);
    spiInitializations++;
    return spiError;
}
int spi_bus_free(int) { spiFrees++; return ESP_OK; }
int esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t, const esp_lcd_panel_io_spi_config_t *c, void **out) {
    assert(c->cs_gpio_num == 12 && c->dc_gpio_num == -1 && c->spi_mode == 3 &&
           c->pclk_hz == 40000000 && c->lcd_cmd_bits == 32 && c->lcd_param_bits == 8 &&
           c->flags.quad_mode && c->trans_queue_depth == 1);
    doneFn = c->on_color_trans_done;
    if (ioError == ESP_OK) *out = reinterpret_cast<void *>(2);
    return ioError;
}
int esp_lcd_panel_io_del(void *) { ioDeletes++; return ESP_OK; }
int esp_lcd_panel_io_tx_param(void *io, uint32_t cmd, const void *data, size_t n) {
    assert(io && (cmd >> 24) == 2 && (cmd & 0xFFFF00FF) == 0x02000000);
    assert(!n || data);
    commands.push_back(cmd);
    return failCommand == int((cmd >> 8) & 0xFF) ? commandError : ESP_OK;
}
int esp_lcd_panel_io_tx_color(void *io, uint32_t cmd, const void *p, size_t n) {
    assert(io && (cmd >> 24) == 0x32 && p && n == 320 * 24 * 2);
    colors.push_back(cmd);
    const uint8_t *b = static_cast<const uint8_t *>(p);
    colorData.emplace_back(b, b + n);
    if (colorError != ESP_OK) return colorError;
    if (!dmaTimeout) doneFn(io, nullptr, nullptr);
    return ESP_OK;
}
#else
using i2c_master_dev_handle_t = void *;
constexpr int I2C_ADDR_BIT_LEN_7 = 0;
struct i2c_device_config_t { int dev_addr_length = 0, device_address = 0, scl_speed_hz = 0; };
uint8_t report[14] = {};
int reportError = ESP_OK, addError = ESP_OK, reads = 0, removes = 0, probes = 0;
int64_t clockUs = 0;
int64_t esp_timer_get_time() { return clockUs; }
int i2c_master_probe(void *b, uint8_t addr, int timeout) {
    assert(b == BoardPower::bus() && timeout > 0 && timeout <= 20);
    probes++;
    return addr == 0x3B || addr == 0x18 || addr == 0x34 ? ESP_OK : ESP_ERR_NOT_FOUND;
}
int i2c_master_bus_add_device(void *b, const i2c_device_config_t *c, void **out) {
    assert(b == BoardPower::bus() && c->device_address == 0x3B && c->scl_speed_hz == 400000);
    if (addError == ESP_OK) *out = reinterpret_cast<void *>(3);
    return addError;
}
int i2c_master_bus_rm_device(void *) { removes++; return ESP_OK; }
int i2c_master_transmit_receive(void *d, const uint8_t *cmd, size_t len, uint8_t *data, size_t n, int timeout) {
    const uint8_t expected[] = {0xB5, 0xAB, 0xA5, 0x5A, 0, 0, 0, 14, 0, 0, 0};
    assert(d && len == sizeof(expected) && !std::memcmp(cmd, expected, len) && n == sizeof(report));
    assert(timeout > 0 && timeout <= 20);
    reads++;
    std::memcpy(data, report, n);
    return reportError;
}
#endif

// DRIVER_UNDER_TEST

#ifdef TEST_DISPLAY
void checkStream(size_t first) {
    assert(colors.size() == first + 20);
    assert(colors[first] == 0x32002C00);
    for (size_t i = first + 1; i < colors.size(); i++) assert(colors[i] == 0x32003C00);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    const std::string mode = argv[1];
    if (mode == "missing-power") powerReady = false;
    if (mode == "failed-power") powerInitialized = false;
    if (mode == "memory-frame") failAllocation = 1;
    if (mode == "memory-dma") failAllocation = 2;
    if (mode == "semaphore") semaphoreFail = true;
    if (mode == "spi") spiError = ESP_ERR_INVALID_STATE;
    if (mode == "io") ioError = ESP_ERR_NO_MEM;
    if (mode == "init-command") { failCommand = 0xA0; commandError = ESP_ERR_TIMEOUT; }
    if (mode == "dma-timeout") dmaTimeout = true;
    if (mode == "lvgl") lvBeginFail = true;
    for (bool rotated : {false, true}) {
        assert(rotationFor(rotated, false, false) ==
               (rotated ? LV_DISPLAY_ROTATION_180 : LV_DISPLAY_ROTATION_0));
        for (bool flip : {false, true}) {
            assert(rotationFor(rotated, true, flip) ==
                   (flip != rotated ? LV_DISPLAY_ROTATION_270 : LV_DISPLAY_ROTATION_90));
        }
    }
    const bool ready = Display::begin();
    if (mode != "normal" && mode != "runtime-timeout" && mode != "runtime-queue" &&
        mode != "sleep-errors" && mode != "column-error" && mode != "bounds") {
        assert(!ready && !Display::ok() && *Display::failReason());
        assert(!Display::begin());
        if (mode == "dma-timeout") {
            assert(!Display::panelOk() && liveAllocations == 2 && colors.size() == 1);
            Display::fillScreen(0xFFFF);
            assert(colors.size() == 1 && g_dma[0] == 0);
        } else if (mode == "lvgl") assert(Display::panelOk());
        else assert(!Display::panelOk() && liveAllocations == 0 && semaphores == 0);
        return 0;
    }
    assert(ready && Display::ok() && Display::panelOk() && !*Display::failReason());
    assert(Display::begin() && beginCount == 1);
    assert(MyLvgl::config.swapBytes && MyLvgl::config.align == 2);
    checkStream(0);
    for (const auto &band : colorData) for (uint8_t b : band) assert(b == 0);
    size_t before = colors.size();
    if (mode == "runtime-timeout") dmaTimeout = true;
    if (mode == "runtime-queue") colorError = ESP_ERR_TIMEOUT;
    if (mode == "column-error") { failCommand = 0x2A; commandError = ESP_ERR_TIMEOUT; }
    if (mode == "bounds") {
        const uint16_t p = 0;
        MyLvgl::config.flush(-1, 0, 0, 0, &p, true);
    } else Display::fillScreen(0xF800);
    if (mode == "runtime-timeout" || mode == "runtime-queue" || mode == "column-error" || mode == "bounds") {
        assert(!Display::ok() && !Display::panelOk() && *Display::failReason());
        const size_t after = colors.size();
        const uint16_t firstDma = g_dma[0];
        Display::fillScreen(0x07E0);
        assert(colors.size() == after && g_dma[0] == firstDma && liveAllocations == 2);
        assert(!Display::setPanelSleeping(true));
        return 0;
    }
    checkStream(before);
    assert(g_frame[0] == 0x00F8 && colorData.back()[0] == 0xF8 && colorData.back()[1] == 0);
    const uint16_t pixels[] = {0x1111, 0x2222, 0x3333, 0x4444};
    lastFlush = false;
    before = colors.size();
    MyLvgl::config.flush(2, 3, 3, 4, pixels, true);
    assert(colors.size() == before && g_frame[3 * 320 + 2] == 0x1111 && g_frame[4 * 320 + 3] == 0x4444);
    lastFlush = true;
    MyLvgl::config.flush(0, 0, 1, 1, pixels, false);
    checkStream(before);
    assert(g_frame[4 * 320 + 3] == 0x4444);
    Display::setRotation(true);
    assert(Display::rotated() && actualRotation == LV_DISPLAY_ROTATION_180);
    assert(Display::setLandscape(true) && actualRotation == LV_DISPLAY_ROTATION_270);
    assert(Display::setLandscape(true, true) && actualRotation == LV_DISPLAY_ROTATION_90);
    Display::setRotation(false);
    assert(!Display::rotated() && actualRotation == LV_DISPLAY_ROTATION_270);
    assert(Display::setLandscape(false) && actualRotation == LV_DISPLAY_ROTATION_0);
    rotationFail = true;
    assert(!Display::setLandscape(true) && !Display::landscape());
    Display::setRotation(true);
    assert(!Display::rotated() && actualRotation == LV_DISPLAY_ROTATION_0);
    rotationFail = false;
    if (mode == "sleep-errors") { failCommand = 0x28; commandError = ESP_ERR_TIMEOUT; }
    if (mode == "sleep-errors") {
        assert(!Display::setPanelSleeping(true) && !Display::panelSleeping());
        failCommand = 0x10;
        assert(!Display::setPanelSleeping(true) && Display::panelSleeping());
        failCommand = 0x11;
        assert(!Display::setPanelSleeping(false) && Display::panelSleeping());
        failCommand = -1;
    } else assert(Display::setPanelSleeping(true) && Display::panelSleeping());
    before = colors.size();
    Display::fillScreen(0xFFFF);
    MyLvgl::config.flush(0, 0, 1, 1, pixels, true);
    assert(colors.size() == before);
    assert(Display::setPanelSleeping(false) && !Display::panelSleeping());
    checkStream(before);
    const auto stats = Display::stats();
    assert(!stats.teActive && !stats.teEdges && !stats.teTimeouts && stats.bufInPsram);
}
#else
void setPoint(int x, int y, int event = 0) {
    std::memset(report, 0, sizeof(report));
    report[1] = 1;
    report[2] = uint8_t((event << 6) | (x >> 8));
    report[3] = uint8_t(x);
    report[4] = uint8_t(y >> 8);
    report[5] = uint8_t(y);
}
bool poll(int16_t &x, int16_t &y) { clockUs += 15000; return MyLvgl::pointer(&x, &y); }
int main(int argc, char **argv) {
    assert(argc == 2);
    const std::string mode = argv[1];
    lvReady = true;
    if (mode == "missing-power") powerReady = false;
    if (mode == "failed-power") powerInitialized = false;
    if (mode == "missing-lvgl") lvReady = false;
    if (mode == "add-device") addError = ESP_ERR_NO_MEM;
    if (mode == "bad-first-report") report[0] = 0xFF;
    if (mode == "pointer") pointerFail = true;
    assert(Touch::bus() == BoardPower::bus());
    const bool ready = Touch::begin();
    if (mode != "normal") {
        assert(!ready && !Touch::info().present && !Touch::indev());
        if (mode == "bad-first-report" || mode == "pointer") assert(removes == 1);
        return 0;
    }
    assert(ready && Touch::info().present && Touch::info().address == 0x3B);
    assert(Touch::info().maxX == 320 && Touch::info().maxY == 480 && Touch::info().maxTouches == 2);
    assert(Touch::info().fwVersion == 0 && Touch::info().irqCount == 0);
    assert(Touch::begin() && probes == 112 && reads == 1);
    uint8_t addrs[4] = {};
    assert(Touch::scanResults(addrs, 4) == 3 && addrs[0] == 0x18 && addrs[2] == 0x3B);
    assert(Touch::scanResults(nullptr, 4) == 0 && Touch::scanResults(addrs, -1) == 0);
    assert(!Touch::deviceFound(0x55));
    int16_t x = -1, y = -1;
    setPoint(0, 0);
    assert(poll(x, y) && x == 0 && y == 0 && Touch::info().touchCount == 1);
    setPoint(319, 479, 2);
    Touch::setRotation(true);
    assert(poll(x, y) && x == 319 && y == 479 && Touch::info().touchCount == 1);
    std::memset(report, 0, sizeof(report));
    assert(poll(x, y));
    setPoint(10, 20, 2);
    assert(poll(x, y) && Touch::info().bridgedGaps == 1);
    setPoint(10, 20, 1);
    assert(!poll(x, y));
    setPoint(1, 1);
    assert(poll(x, y) && Touch::info().touchCount == 2);
    reportError = ESP_ERR_TIMEOUT;
    assert(!poll(x, y) && Touch::info().readErrors == 1);
    reportError = ESP_OK;
    for (int invalid = 0; invalid < 5; invalid++) {
        setPoint(1, 1);
        if (invalid == 0) report[0] = 0xFF;
        if (invalid == 1) report[1] = 3;
        if (invalid == 2) setPoint(320, 1);
        if (invalid == 3) setPoint(1, 480);
        if (invalid == 4) setPoint(1, 1, 3);
        assert(!poll(x, y));
    }
    assert(Touch::info().readErrors == 6);
    setPoint(2, 3);
    assert(poll(x, y));
    std::memset(report, 0, sizeof(report));
    assert(poll(x, y));
    clockUs += 60000;
    assert(!poll(x, y));
    setPoint(0, 0, 1);
    report[1] = 2;
    report[8] = 0x80;
    report[9] = 42;
    report[10] = 1;
    report[11] = 44;
    assert(poll(x, y) && x == 42 && y == 300);
}
#endif
