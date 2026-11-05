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
std::vector<std::string> logFormats;
template<class... Args> void logMock(const char *, const char *format, Args...) {
    logFormats.emplace_back(format);
}
#define ESP_LOGE(...) logMock(__VA_ARGS__)
#define ESP_LOGW(...) logMock(__VA_ARGS__)
#define ESP_LOGI(...) logMock(__VA_ARGS__)
#define IRAM_ATTR
using i2c_master_bus_handle_t = void *;
bool powerReady = true;
bool powerInitialized = true;
bool panelResetSuccess = true;
int panelResetCalls = 0;
namespace BoardPower {
struct Status { bool initialized; };
void *bus() { return powerReady ? reinterpret_cast<void *>(1) : nullptr; }
Status status() { return {powerInitialized}; }
bool resetPanel() { ++panelResetCalls; return panelResetSuccess; }
}

struct lv_display_t {};
struct lv_indev_t { bool stopProcessing = false; };
struct lv_obj_t {};
struct lv_point_t { int32_t x = 0, y = 0; };
lv_obj_t *scrollMock = nullptr;
lv_obj_t *lv_indev_get_scroll_obj(lv_indev_t *) { return scrollMock; }
void lv_indev_get_point(lv_indev_t *, lv_point_t *point) { *point = {100, 100}; }
enum lv_event_code_t {
    LV_EVENT_ALL, LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED,
    LV_EVENT_SHORT_CLICKED, LV_EVENT_SINGLE_CLICKED, LV_EVENT_DOUBLE_CLICKED,
    LV_EVENT_TRIPLE_CLICKED, LV_EVENT_CLICKED, LV_EVENT_PRESS_LOST
};
struct lv_event_t { lv_event_code_t code; bool stopped = false; };
struct lv_event_dsc_t {};
using lv_event_cb_t = void (*)(lv_event_t *);
lv_event_cb_t pointerEventCallback = nullptr;
enum lv_display_rotation_t { LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90,
                             LV_DISPLAY_ROTATION_180, LV_DISPLAY_ROTATION_270 };
lv_display_t displayMock;
lv_indev_t pointerMock;
lv_event_code_t lv_event_get_code(lv_event_t *event) { return event->code; }
void lv_event_stop_processing(lv_event_t *event) { event->stopped = true; }
void lv_indev_stop_processing(lv_indev_t *indev) { indev->stopProcessing = true; }
lv_event_dsc_t *lv_indev_add_event_cb(lv_indev_t *indev, lv_event_cb_t callback,
                                      lv_event_code_t filter, void *) {
    assert(indev == &pointerMock && filter == LV_EVENT_ALL);
    pointerEventCallback = callback;
    static lv_event_dsc_t descriptor;
    return &descriptor;
}
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
enum gpio_num_t { GPIO_NUM_NC = -1, GPIO_NUM_12 = 12 };
struct esp_lcd_panel_io_spi_config_t {
    gpio_num_t cs_gpio_num = GPIO_NUM_NC, dc_gpio_num = GPIO_NUM_NC;
    int spi_mode = 0, pclk_hz = 0, trans_queue_depth = 0;
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
uint8_t report[8] = {};
int reportError = ESP_OK, addError = ESP_OK, reads = 0, removes = 0, probes = 0;
int commandError = ESP_OK, writes = 0, startupFailures = 0, delays = 0;
int transactionDelayMs = 0;
bool commandPending = false;
bool recoverySuccess = true;
int recoveryCalls = 0;
namespace Display {
bool recoverTouchStartup() { ++recoveryCalls; return recoverySuccess; }
}
int64_t clockUs = 0;
int64_t esp_timer_get_time() { return clockUs; }
int pdMS_TO_TICKS(int n) { return n; }
void vTaskDelay(int n) { delays += n; }
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
int i2c_master_transmit(void *d, const uint8_t *cmd, size_t len, int timeout) {
    const uint8_t expected[] = {0xB5, 0xAB, 0xA5, 0x5A, 0, 0, 0, 8, 0, 0, 0};
    assert(d && len == sizeof(expected) && !std::memcmp(cmd, expected, len));
    assert(timeout == 100 && !commandPending);
    writes++;
    clockUs += int64_t(transactionDelayMs) * 1000;
    if (commandError != ESP_OK) return commandError;
    commandPending = true;
    return ESP_OK;
}
int i2c_master_receive(void *d, uint8_t *data, size_t n, int timeout) {
    assert(d && n == sizeof(report) && timeout == 100 && commandPending);
    commandPending = false;
    reads++;
    std::memcpy(data, report, n);
    if (startupFailures > 0) {
        --startupFailures;
        for (size_t i = 0; i < n; ++i) data[i] = uint8_t(0x13 + (i & 1)); // Non-uniform: malformed.
    }
    return reportError;
}
namespace OtrI2c {
int probe(void *bus, uint16_t address, int timeout)
{
    return i2c_master_probe(bus, address, timeout);
}
int transmitThenReceive(void *device, const uint8_t *tx, size_t txSize,
                    uint8_t *rx, size_t rxSize, int timeout)
{
    const int err = i2c_master_transmit(device, tx, txSize, timeout);
    if (err != ESP_OK) return err;
    return i2c_master_receive(device, rx, rxSize, timeout);
}
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
        mode != "sleep-errors" && mode != "column-error" && mode != "bounds" &&
        mode != "recovery" && mode != "recovery-reset-fail" &&
        mode != "recovery-command-fail" && mode != "recovery-dma-fail" &&
        mode != "recovery-after-task") {
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
    if (mode == "recovery" || mode == "recovery-reset-fail" ||
        mode == "recovery-command-fail" || mode == "recovery-dma-fail" ||
        mode == "recovery-after-task") {
        const int beforeAllocations = allocations;
        const size_t beforeColors = colors.size();
        const size_t beforeCommands = commands.size();
        if (mode == "recovery-reset-fail") panelResetSuccess = false;
        if (mode == "recovery-command-fail") { failCommand = 0xA0; commandError = ESP_ERR_TIMEOUT; }
        if (mode == "recovery-dma-fail") dmaTimeout = true;
        if (mode == "recovery-after-task") assert(Display::startTask());
        const bool recovered = Display::recoverTouchStartup();
        assert(recovered == (mode == "recovery"));
        assert(allocations == beforeAllocations && beginCount == 1 && liveAllocations == 2);
        assert(panelResetCalls == (mode == "recovery-after-task" ? 0 : 1));
        if (recovered) {
            assert(Display::ok() && Display::panelOk() && !*Display::failReason());
            checkStream(beforeColors);
            assert(commands.size() == beforeCommands + sizeof(INIT_CMDS) / sizeof(INIT_CMDS[0]) + 1);
        } else if (mode != "recovery-after-task") {
            assert(!Display::ok() && !Display::panelOk() && *Display::failReason());
        }
        const size_t afterColors = colors.size();
        assert(!Display::recoverTouchStartup());
        assert(colors.size() == afterColors && panelResetCalls <= 1);
        return 0;
    }
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
        assert(!Display::recoverTouchStartup() && panelResetCalls == 0);
        assert(colors.size() == after && g_dma[0] == firstDma);
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
        assert(!Display::panelSleepSupported() && !Display::panelSleeping());
        const size_t count = commands.size();
        assert(!Display::setPanelSleeping(true) && !Display::panelSleeping());
        assert(commands.size() == count);
        assert(Display::setPanelSleeping(false) && !Display::panelSleeping());
        failCommand = -1;
    } else {
        assert(!Display::panelSleepSupported() && !Display::panelSleeping());
        assert(!Display::setPanelSleeping(true));
    }
    before = colors.size();
    Display::fillScreen(0xFFFF);
    checkStream(before);
    MyLvgl::config.flush(0, 0, 1, 1, pixels, true);
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
bool poll(int16_t &x, int16_t &y) { clockUs += 30000; return MyLvgl::pointer(&x, &y); }
int main(int argc, char **argv) {
    assert(argc == 2);
    const std::string mode = argv[1];
    lvReady = true;
    if (mode == "missing-power") powerReady = false;
    if (mode == "failed-power") powerInitialized = false;
    if (mode == "missing-lvgl") lvReady = false;
    if (mode == "add-device") addError = ESP_ERR_NO_MEM;
    if (mode == "bad-first-report") reportError = ESP_ERR_INVALID_RESPONSE;
    if (mode == "startup-corrupt") startupFailures = 5;
    if (mode == "startup-stuck") startupFailures = 10;
    if (mode == "recovery-failure") { startupFailures = 5; recoverySuccess = false; }
    if (mode == "startup-recovery") startupFailures = 3;
    if (mode == "command-failure") commandError = ESP_ERR_TIMEOUT;
    if (mode == "idle-first") std::memset(report, 0x37, sizeof(report));
    if (mode == "idle-first-57") std::memset(report, 0x57, sizeof(report));
    if (mode == "idle-first-02") std::memset(report, 0x02, sizeof(report));
    if (mode == "pointer") pointerFail = true;
    assert(Touch::bus() == BoardPower::bus());
    const bool ready = Touch::begin();
    if (mode == "idle-first-57" || mode == "idle-first-02") {
        // Boot-varying filler bytes: device present, no retries, no shared reset.
        assert(ready && Touch::info().present && reads == 1 && recoveryCalls == 0);
        assert(Touch::info().readErrors == 0 && MyLvgl::pointer);
        int16_t x = -1, y = -1;
        assert(!poll(x, y) && Touch::info().readErrors == 0);
        return 0;
    }
    if (mode != "normal" && mode != "diagnostics" && mode != "idle-first" && mode != "startup-recovery" &&
        mode != "startup-corrupt") {
        assert(!ready && !Touch::info().present && !Touch::indev());
        if (mode == "bad-first-report" || mode == "startup-stuck" || mode == "command-failure") {
            assert(removes == 1 && writes == 10 && delays == 400 && recoveryCalls == 1);
            assert(reads == (mode == "command-failure" ? 0 : 10));
            assert(Touch::info().readErrors == 10);
        }
        if (mode == "recovery-failure")
            assert(removes == 1 && writes == 5 && delays == 200 && recoveryCalls == 1);
        if (mode == "pointer") assert(removes == 1);
        return 0;
    }
    if (mode == "startup-corrupt") {
        assert(ready && Touch::info().present && reads == 6 && recoveryCalls == 1);
        assert(Touch::info().readErrors == 5 && delays == 200 && MyLvgl::pointer);
        assert(Touch::begin() && reads == 6 && recoveryCalls == 1);
        return 0;
    }
    if (mode == "startup-recovery") {
        assert(ready && Touch::info().present && reads == 4 && writes == 4 && delays == 150);
        assert(Touch::info().readErrors == 3 && MyLvgl::pointer);
        assert(recoveryCalls == 0);
        assert(Touch::begin() && reads == 4);
        return 0;
    }
    assert(ready && Touch::info().present && Touch::info().address == 0x3B);
    assert(Touch::info().maxX == 320 && Touch::info().maxY == 480 && Touch::info().maxTouches == 1);
    assert(Touch::info().fwVersion == 0 && Touch::info().irqCount == 0);
    assert(Touch::begin() && probes == 112 && reads == 1);
    assert(pointerEventCallback);
    uint8_t addrs[4] = {};
    assert(Touch::scanResults(addrs, 4) == 3 && addrs[0] == 0x18 && addrs[2] == 0x3B);
    assert(Touch::scanResults(nullptr, 4) == 0 && Touch::scanResults(addrs, -1) == 0);
    assert(!Touch::deviceFound(0x55));
    int16_t x = -1, y = -1;
    if (mode == "diagnostics") {
        assert(TOUCH_DIAGNOSTICS);
        for (bool failRead : {false, true}) {
            setPoint(100, 100);
            assert(poll(x, y));
            if (failRead) {
                reportError = ESP_ERR_TIMEOUT;
                assert(poll(x, y));
                reportError = ESP_OK;
                setPoint(100, 100);
                assert(poll(x, y));
            } else {
                setPoint(180, 160);
                assert(poll(x, y));
            }
            assert(g_tapCancelled); // Still tracked for comparison, never used as a veto.
            setPoint(180, 160, 1);
            assert(!poll(x, y));
            for (auto code : {LV_EVENT_CLICKED, LV_EVENT_SHORT_CLICKED, LV_EVENT_SINGLE_CLICKED,
                              LV_EVENT_DOUBLE_CLICKED, LV_EVENT_TRIPLE_CLICKED, LV_EVENT_RELEASED}) {
                lv_event_t event{code};
                pointerMock.stopProcessing = false;
                pointerEventCallback(&event);
                assert(!event.stopped && !pointerMock.stopProcessing);
            }
        }
        setPoint(100, 100);
        assert(poll(x, y));
        reportError = ESP_ERR_TIMEOUT;
        assert(poll(x, y)); // The first missing read is at 30 ms.
        assert(!poll(x, y)); // Diagnostics must not remove the bounded failure release.
        reportError = ESP_OK;
#ifdef CONFIG_OTR_S3_TOUCH_DIAGNOSTICS
        static_assert(sizeof(RawSample::bytes) == 8);
        assert(g_rawStarted && !g_rawDumped);
        assert(g_rawSamples[0].bytes[1] == 1);
        assert(g_rawSamples[2].bytes[2] == 0x40); // Release packet retained verbatim.
        assert(std::any_of(g_rawSamples, g_rawSamples + g_rawCount,
                          [](const RawSample &sample) { return sample.result == ESP_ERR_TIMEOUT; }));
        const auto beforeDump = logFormats.size();
        while (g_rawCount < 511) poll(x, y);
        assert(logFormats.size() == beforeDump); // No per-read logging.
        poll(x, y);
        assert(g_rawDumped && g_rawCount == 512);
        assert(logFormats.size() == beforeDump + 514);
        for (int i = 0; i < 1000; ++i) poll(x, y);
        assert(g_rawCount == 512 && logFormats.size() == beforeDump + 514);
        for (const char *marker : {"DIAG touch:", "ARMED:", "BEGIN 512", "n=%u t_us=", "END capture"}) {
            assert(std::any_of(logFormats.begin(), logFormats.end(),
                              [marker](const std::string &line) { return line.find(marker) != std::string::npos; }));
        }
#endif
        return 0;
    }
    setPoint(0, 0);
    assert(poll(x, y) && x == 0 && y == 0 && Touch::info().touchCount == 1);
    static_assert(POLL_MS == 30);
    const int readsBeforeLimit = reads;
    clockUs += 29000;
    assert(MyLvgl::pointer(&x, &y) && reads == readsBeforeLimit);
    clockUs += 1000;
    assert(MyLvgl::pointer(&x, &y) && reads == readsBeforeLimit + 1);
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
    clockUs += 60000;
    assert(!poll(x, y) && Touch::info().readErrors == 1);
    lv_event_t failedReadClick{LV_EVENT_CLICKED};
    pointerMock.stopProcessing = false;
    pointerEventCallback(&failedReadClick);
    assert(failedReadClick.stopped && pointerMock.stopProcessing);
    reportError = ESP_OK;
    reportError = ESP_ERR_INVALID_RESPONSE; // Bus NACK, distinct from an invalid report.
    assert(!poll(x, y) && Touch::info().readErrors == 2);
    reportError = ESP_OK;
    for (int odd = 0; odd < 2; odd++) {
        setPoint(1, 1);
        if (odd == 0) report[0] = 0xFF;
        if (odd == 1) report[1] = 3;
        clockUs += 60000;
        assert(!poll(x, y));
    }
    assert(Touch::info().readErrors == 4);
    // Out-of-bounds or corrupt data must never become taps on a clamped edge.
    setPoint(320, 1);
    assert(!poll(x, y) && Touch::info().readErrors == 5);
    setPoint(1, 480);
    assert(!poll(x, y) && Touch::info().readErrors == 6);
    std::memset(report, 0x37, sizeof(report)); // Owner confirmed no contact, not a bus failure.
    assert(!poll(x, y) && Touch::info().readErrors == 6);
    for (size_t i = 0; i < sizeof(report); ++i) {
        report[i] = 0x36; // Near-matches must not hide a malformed report.
        assert(!poll(x, y) && Touch::info().readErrors == 7 + i);
        report[i] = 0x37;
    }
    // Waveshare accepts contact event 3 as well as events 0 and 2.
    setPoint(5, 6, 3);
    assert(poll(x, y) && x == 5 && y == 6);
    setPoint(2, 3);
    assert(poll(x, y));
    const uint32_t errorsBeforeIdle = Touch::info().readErrors;
    const uint32_t pressesBeforeIdle = Touch::info().touchCount;
    std::memset(report, 0x37, sizeof(report));
    assert(poll(x, y) && x == 2 && y == 3); // Existing 60 ms gap debounce still applies.
    clockUs += 60000;
    assert(!poll(x, y)); // An idle report cannot leave a button held.
    for (int i = 0; i < 1000; ++i) assert(!poll(x, y));
    assert(Touch::info().readErrors == errorsBeforeIdle);
    assert(Touch::info().touchCount == pressesBeforeIdle);
    setPoint(4, 5);
    assert(poll(x, y) && x == 4 && y == 5);
    assert(Touch::info().touchCount == pressesBeforeIdle + 1);
    std::memset(report, 0, sizeof(report));
    assert(poll(x, y));
    clockUs += 60000;
    assert(!poll(x, y));
    setPoint(42, 300, 1);
    assert(!poll(x, y));
    setPoint(42, 300, 2);
    report[1] = 2;
    assert(!poll(x, y));
    assert(Touch::info().readErrors == errorsBeforeIdle + 1);
    setPoint(42, 300, 2);
    assert(poll(x, y) && x == 42 && y == 300);

    setPoint(42, 300, 1);
    assert(!poll(x, y));
    setPoint(100, 100);
    assert(poll(x, y));
    setPoint(112, 108);
    assert(poll(x, y));
    setPoint(112, 108, 1);
    assert(!poll(x, y));
    lv_event_t clickNearStart{LV_EVENT_CLICKED};
    pointerMock.stopProcessing = false;
    pointerEventCallback(&clickNearStart);
    assert(!pointerMock.stopProcessing && !clickNearStart.stopped);
    setPoint(100, 100);
    assert(poll(x, y));
    setPoint(115, 115);
    assert(poll(x, y));
    setPoint(125, 115);
    assert(poll(x, y));
    setPoint(125, 115, 1);
    assert(!poll(x, y));
    for (lv_event_code_t code : {LV_EVENT_SHORT_CLICKED, LV_EVENT_SINGLE_CLICKED,
                                 LV_EVENT_DOUBLE_CLICKED, LV_EVENT_TRIPLE_CLICKED,
                                 LV_EVENT_CLICKED, LV_EVENT_RELEASED}) {
        lv_event_t event{code};
        pointerMock.stopProcessing = false;
        pointerEventCallback(&event);
        assert(pointerMock.stopProcessing == (code != LV_EVENT_RELEASED));
        assert(event.stopped == (code != LV_EVENT_RELEASED));
    }
    lv_event_t movedClick{LV_EVENT_CLICKED};
    pointerMock.stopProcessing = false;
    pointerEventCallback(&movedClick);
    assert(pointerMock.stopProcessing && movedClick.stopped);
    lv_event_t movedShortClick{LV_EVENT_SHORT_CLICKED};
    pointerMock.stopProcessing = false;
    pointerEventCallback(&movedShortClick);
    assert(pointerMock.stopProcessing && movedShortClick.stopped);
    setPoint(125, 115);
    assert(poll(x, y));
    setPoint(125, 115, 1);
    assert(!poll(x, y));
    lv_event_t freshTap{LV_EVENT_CLICKED};
    pointerMock.stopProcessing = false;
    pointerEventCallback(&freshTap);
    assert(!freshTap.stopped && !pointerMock.stopProcessing);
    // A quick swipe may have only a start frame and a release at its destination.
    setPoint(100, 100);
    assert(poll(x, y));
    setPoint(180, 160, 1);
    assert(!poll(x, y) && x == 180 && y == 160);
    for (lv_event_code_t code : {LV_EVENT_SHORT_CLICKED, LV_EVENT_SINGLE_CLICKED,
                                 LV_EVENT_DOUBLE_CLICKED, LV_EVENT_TRIPLE_CLICKED,
                                 LV_EVENT_CLICKED}) {
        lv_event_t event{code};
        pointerMock.stopProcessing = false;
        pointerEventCallback(&event);
        assert(event.stopped && pointerMock.stopProcessing);
    }
    // Crossing the threshold and returning to the origin still cancels the gesture.
    setPoint(100, 100);
    assert(poll(x, y));
    setPoint(121, 100);
    assert(poll(x, y));
    setPoint(100, 100, 1);
    assert(!poll(x, y));
    lv_event_t returnedClick{LV_EVENT_CLICKED};
    pointerMock.stopProcessing = false;
    pointerEventCallback(&returnedClick);
    assert(returnedClick.stopped && pointerMock.stopProcessing);
    for (int distance : {20, 21}) {
        setPoint(100, 100);
        assert(poll(x, y));
        setPoint(100 + distance, 100, 1);
        assert(!poll(x, y));
        lv_event_t event{LV_EVENT_CLICKED};
        pointerMock.stopProcessing = false;
        pointerEventCallback(&event);
        assert(event.stopped == (distance > 20));
        assert(pointerMock.stopProcessing == (distance > 20));
    }
    // Uniform filler of any value means no contact: a held tap bridges, then releases cleanly.
    for (uint8_t filler : {0x02, 0x13, 0x17, 0x57, 0xD7}) {
        setPoint(125, 115);
        assert(poll(x, y));
        const uint32_t errors = Touch::info().readErrors;
        std::memset(report, filler, sizeof(report));
        assert(poll(x, y) && x == 125 && y == 115);
        clockUs += 60000;
        assert(!poll(x, y) && Touch::info().readErrors == errors);
        lv_event_t releasedClick{LV_EVENT_CLICKED};
        pointerMock.stopProcessing = false;
        pointerEventCallback(&releasedClick);
        assert(!releasedClick.stopped && !pointerMock.stopProcessing);
    }
    const uint8_t corruptReports[][8] = {
        {0x37, 0x37, 0x00, 0x01, 0x80, 0x5C, 0x01, 0xCD},
        {0x01, 0x80, 0x68, 0x01, 0x5D, 0x11, 0x12, 0xFF},
    };
    for (const auto &corrupt : corruptReports) {
        setPoint(125, 115);
        assert(poll(x, y));
        const uint32_t errors = Touch::info().readErrors;
        std::memcpy(report, corrupt, sizeof(report));
        clockUs += 60000;
        assert(!poll(x, y) && Touch::info().readErrors == errors + 1);
        lv_event_t corruptClick{LV_EVENT_CLICKED};
        pointerMock.stopProcessing = false;
        pointerEventCallback(&corruptClick);
        assert(corruptClick.stopped && pointerMock.stopProcessing);
    }
    setPoint(125, 115);
    assert(poll(x, y));
    const int readsBeforeCommandFailure = reads;
    commandError = ESP_ERR_TIMEOUT;
    clockUs += 60000;
    assert(!poll(x, y) && reads == readsBeforeCommandFailure);
    commandError = ESP_OK;
    assert(poll(x, y));
    setPoint(125, 115, 1);
    assert(!poll(x, y));
    for (const auto &corrupt : corruptReports) {
        setPoint(100, 100);
        assert(poll(x, y));
        const uint32_t presses = Touch::info().touchCount;
        const uint32_t gaps = Touch::info().bridgedGaps;
        const uint32_t errors = Touch::info().readErrors;
        std::memcpy(report, corrupt, sizeof(report));
        assert(poll(x, y) && x == 100 && y == 100); // Missing read at 30 ms.
        assert(Touch::info().readErrors == errors + 1);
        setPoint(110, 108);
        assert(poll(x, y) && x == 110 && y == 108);
        assert(Touch::info().touchCount == presses && Touch::info().bridgedGaps == gaps + 1);
        setPoint(110, 108, 1);
        assert(!poll(x, y));
        lv_event_t event{LV_EVENT_CLICKED};
        pointerMock.stopProcessing = false;
        pointerEventCallback(&event);
        assert(event.stopped && pointerMock.stopProcessing);
    }
    // Missing data cannot extend the last real contact beyond 60 ms.
    setPoint(100, 100);
    assert(poll(x, y));
    reportError = ESP_ERR_TIMEOUT;
    assert(poll(x, y));
    assert(!poll(x, y));
    for (int i = 0; i < 10; ++i) assert(!poll(x, y));
    reportError = ESP_OK;
    // Time spent blocked in the transport counts toward that same bound.
    setPoint(100, 100);
    assert(poll(x, y));
    transactionDelayMs = 100;
    commandError = ESP_ERR_TIMEOUT;
    assert(!poll(x, y));
    transactionDelayMs = 0;
    commandError = ESP_OK;
    // The bound also holds when the millisecond clock wraps.
    clockUs = int64_t(UINT32_MAX - 30) * 1000;
    setPoint(100, 100);
    assert(poll(x, y));
    std::memcpy(report, corruptReports[0], sizeof(report));
    assert(poll(x, y));
    assert(!poll(x, y));
    setPoint(100, 100);
    assert(poll(x, y));
    setPoint(100, 100, 1);
    assert(!poll(x, y));
    lv_event_t cleanTap{LV_EVENT_CLICKED};
    pointerMock.stopProcessing = false;
    pointerEventCallback(&cleanTap);
    assert(!cleanTap.stopped && !pointerMock.stopProcessing);
}
#endif
