// board_display.h - ST77922 QSPI panel (esp_lcd panel IO) on top of the generic my_lvgl port.
//
// - SPI2_HOST in quad mode, 40 MHz, RGB565 big-endian (my_lvgl swaps the bytes before flush)
// - LVGL draw buffers in PSRAM (my_lvgl), flush copies PSRAM -> internal DMA bounce buffers in
//   chunks (ping-pong) so DMA never reads PSRAM and the copy of chunk N+1 overlaps chunk N
// - Areas rounded to 4 px (ST77922 requirement)
// - TE (GPIO42) synchronisation: the first flush of each frame waits for a TE edge
#pragma once

#include <stdint.h>

#include "my_lvgl.h"

namespace Display {

struct Stats {
    uint32_t flushes;
    uint32_t frames;
    uint32_t teEdges;
    uint32_t teTimeouts;
    bool teActive;
    float fps;
    uint32_t bufBytes;
    bool bufInPsram;
};

bool begin();                       // Panel + lv_init + LVGL display (task not started yet)
bool ok();
bool panelOk();                     // Panel IO + init sequence succeeded (LVGL may still have failed)
const char *failReason();           // Short text for the boot screen / log, "" when ok
void fillScreen(uint16_t rgb565);   // Blocking full-screen fill, bypasses LVGL
bool startTask();                   // LVGL handler task on core 1
inline bool lock(uint32_t timeoutMs = 0) { return MyLvgl::lock(timeoutMs); }
inline void unlock() { MyLvgl::unlock(); }
Stats stats();

}  // namespace Display

using DisplayLock = LvLock;
