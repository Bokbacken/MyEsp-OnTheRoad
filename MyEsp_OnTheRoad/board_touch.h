// board_touch.h - ST77922 capacitive touch (I2C 0x55 on GPIO38/39, INT 47, RST 48) and the
// LVGL pointer input device. Also owns the shared I2C bus (Wire) and the boot-time bus scan.
#pragma once

#include <lvgl.h>
#include <stdint.h>

namespace Touch {

struct Info {
    bool present;            // Controller answered at 0x55
    uint8_t fwVersion;
    uint8_t fwRevision[4];
    uint16_t maxX, maxY;
    uint8_t maxTouches;
    uint16_t lastX, lastY;
    bool pressed;
    uint32_t touchCount;     // Press events since boot
    uint32_t irqCount;
    uint32_t readErrors;
};

// Starts Wire (400 kHz), scans the bus, resets and probes the controller and registers the
// LVGL input device (lv_init() must have run). Returns true if the controller responds.
bool begin();

// Results of the boot-time I2C scan (bus is owned by the LVGL task afterwards).
int scanResults(uint8_t *addrs, int max);
bool deviceFound(uint8_t addr);

lv_indev_t *indev();
Info info();

}  // namespace Touch
