// board_touch.h - ST77922 capacitive touch (I2C 0x55 on GPIO38/39, INT 47, RST 48) and the
// LVGL pointer input device. Also owns the shared I2C bus (i2c_master) and the boot-time scan.
#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"
#include "lvgl.h"

namespace Touch {

struct Info {
    bool present;            // Controller answered at 0x55
    uint16_t fwVersion;
    uint8_t fwRevision[4];
    uint16_t maxX, maxY;
    uint8_t maxTouches;
    uint16_t lastX, lastY;
    bool pressed;
    uint32_t touchCount;     // Press events since boot
    uint32_t irqCount;
    uint32_t readErrors;
    uint32_t bridgedGaps;    // Short release glitches suppressed by the release debounce
    uint8_t address;         // Actual responding I2C address (0 if unknown)
};

// Creates the I2C bus (400 kHz), scans it, resets and probes the controller and registers the
// LVGL input device (MyLvgl::begin() must have run). Returns true if the controller responds.
bool begin();

int scanResults(uint8_t *addrs, int max);
bool deviceFound(uint8_t addr);
i2c_master_bus_handle_t bus();   // For future devices on the shared bus (ES8311, ...)

lv_indev_t *indev();
// v0.5.6: mirror the reported point (x' = 319 - x, y' = 479 - y) to match Display::setRotation.
// Info::lastX/lastY stay panel-native.
void setRotation(bool rot180);
Info info();

}  // namespace Touch
