#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"

namespace BoardPower {

constexpr uint32_t TOUCH_SAMPLE_PERIOD_MS = 20;
constexpr uint32_t POWER_BUTTON_EVERY_N_SAMPLES = 5;

using TouchSampleCallback = void (*)();

struct Status {
    bool supported;
    bool initialized;
    bool sampleValid;
    bool batteryConnected;
    bool batteryVoltageValid;
    uint16_t batteryMillivolts;
    bool usbPresent;
    bool charging;
    bool chargeDone;
    bool percentValid;
    uint8_t percent;
    bool powerButtonPressed;
    bool powerOffRequested;
    uint32_t errors;
};

// Initializes the shared I2C bus, probes the PMIC and resets the panel via EXIO1.
bool begin();
// S3 shared LCD/touch reset; caller must reinitialize the panel before drawing.
bool resetPanel();
i2c_master_bus_handle_t bus();
// Returns a cached PMIC sample; it never performs I2C transfers.
Status status();
bool powerOff();
void cancelPowerOff();
bool setTouchSampleCallback(TouchSampleCallback callback);

}  // namespace BoardPower
