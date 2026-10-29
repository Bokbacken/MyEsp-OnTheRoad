// my_lvgl.h - Generic LVGL 9 port for ESP-IDF: display buffers (PSRAM when present), tick,
// recursive mutex, handler task, optional RGB565 byte swap, area rounding and frame statistics.
//
// The board supplies a synchronous flush function (pixels are only valid during the call);
// my_lvgl calls lv_display_flush_ready() afterwards. Touch/keys are added with addPointer().
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

namespace MyLvgl {

// Board flush: write the area x1..x2 / y1..y2 (inclusive) from px (RGB565, already swapped if
// Config::swapBytes). firstOfFrame lets the board wait for a TE edge once per frame.
using FlushFn = void (*)(int x1, int y1, int x2, int y2, const uint16_t *px, bool firstOfFrame);
// Pointer read: return true while pressed and fill x/y (display coordinates).
using PointerFn = bool (*)(int16_t *x, int16_t *y);
using PointerFilterFn = bool (*)(bool pressed);

struct Config {
    int hres = 320;
    int vres = 480;
    int bufLines = 40;          // Lines per draw buffer (two buffers, PARTIAL mode)
    int fallbackLines = 16;     // Used in internal RAM when PSRAM is not available
    uint8_t align = 1;          // Round invalidated areas to this many pixels (power of two)
    bool swapBytes = true;      // RGB565 big-endian panels (SPI/QSPI) need this
    FlushFn flush = nullptr;
};

struct Stats {
    uint32_t flushes;
    uint32_t frames;
    float fps;
    uint32_t bufBytes;          // One draw buffer
    bool bufInPsram;
    uint32_t lvMemTotal;        // LVGL builtin heap
    uint32_t lvMemFree;
    uint32_t lvMemMaxUsed;
    uint8_t lvMemFrag;          // Percent
    uint32_t taskStackFree;     // Bytes, high-water mark of the LVGL task
};

bool begin(const Config &cfg);  // lv_init(), tick, buffers, display
bool ok();
lv_display_t *display();
lv_indev_t *addPointer(PointerFn fn);
// Runs on every pointer sample, before LVGL sees it. Set with the LVGL lock held.
void setPointerFilter(PointerFilterFn fn);
// Software rotation (lv_display_set_rotation + rotation in the flush). The pointer keeps reporting
// physical coordinates; LVGL maps them to the rotated screen. A rotation buffer of one draw-buffer
// size is allocated (PSRAM) on the first non-zero rotation and kept. Call with the LVGL lock held.
bool setRotation(lv_display_rotation_t rot);
lv_display_rotation_t rotation();

bool startTask(int core = 1, int prio = 2, uint32_t stackBytes = 12 * 1024);
bool lock(uint32_t timeoutMs = 0);   // 0 = wait forever; recursive
void unlock();
Stats stats();

}  // namespace MyLvgl

// RAII: `LvLock l; if (l) { ... }`
class LvLock {
public:
    explicit LvLock(uint32_t timeoutMs = 0) : locked_(MyLvgl::lock(timeoutMs)) {}
    ~LvLock() { if (locked_) MyLvgl::unlock(); }
    explicit operator bool() const { return locked_; }
    LvLock(const LvLock &) = delete;
    LvLock &operator=(const LvLock &) = delete;
private:
    bool locked_;
};
