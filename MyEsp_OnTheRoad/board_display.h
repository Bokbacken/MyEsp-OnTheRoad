// board_display.h - ST77922 QSPI panel (esp_lcd panel IO) + LVGL 8 display port.
//
// - SPI2_HOST in quad mode, 40 MHz, RGB565 (LV_COLOR_16_SWAP=1 so no byte swapping at flush)
// - Two partial LVGL draw buffers in PSRAM (320 x 40 px each)
// - Flush copies PSRAM -> internal DMA bounce buffers in chunks (ping-pong), so DMA never
//   reads PSRAM directly and the CPU copy of chunk N+1 overlaps the transfer of chunk N
// - Draw areas are rounded to 4-pixel boundaries (ST77922 requirement)
// - TE (GPIO42) synchronisation: the first flush of each frame waits for the TE edge
// - LVGL runs in its own FreeRTOS task; use Display::lock()/unlock() from any other task
#pragma once

#include <lvgl.h>
#include <stdint.h>

namespace Display {

struct Stats {
    uint32_t flushes;        // Total flush callbacks
    uint32_t frames;         // Completed frames (last flush of a refresh)
    uint32_t teEdges;        // TE interrupts seen
    uint32_t teTimeouts;     // Frames that waited for TE in vain
    bool teActive;           // TE is used (disabled automatically if the pin stays silent)
    float fps;               // Frames per second over the last second
    uint32_t bufBytes;       // Size of one LVGL draw buffer
    bool bufInPsram;
};

// Initialises SPI/QSPI, panel IO, ST77922 init sequence, lv_init() and the LVGL display driver.
bool begin();
bool ok();

// Blocking full-screen fill directly on the panel (bypasses LVGL). For colour/channel tests
// before LVGL takes over. color is RGB565 (host order).
void fillScreen(uint16_t rgb565);

// Starts the LVGL handler task (core 1). Call after the first screen is built.
bool startTask();

// Recursive mutex around all LVGL calls from tasks other than the LVGL task itself.
// timeoutMs = 0 waits forever.
bool lock(uint32_t timeoutMs = 0);
void unlock();

lv_disp_t *disp();
Stats stats();

}  // namespace Display

// RAII helper: `DisplayLock l; if (l) { ... }`
class DisplayLock {
public:
    explicit DisplayLock(uint32_t timeoutMs = 0) : locked_(Display::lock(timeoutMs)) {}
    ~DisplayLock() { if (locked_) Display::unlock(); }
    explicit operator bool() const { return locked_; }
    DisplayLock(const DisplayLock &) = delete;
    DisplayLock &operator=(const DisplayLock &) = delete;
private:
    bool locked_;
};
