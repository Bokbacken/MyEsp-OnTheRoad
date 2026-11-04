#pragma once

#include <stdint.h>

#include "driver/i2c_master.h"

namespace BoardPower {

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
    uint32_t errors;
};

// Initializes the shared I2C bus, probes the PMIC and resets the panel via EXIO1.
bool begin();
i2c_master_bus_handle_t bus();
// Returns a cached PMIC sample; it never performs I2C transfers.
Status status();

}  // namespace BoardPower
