#pragma once

#include <stdint.h>

namespace BoardBattery {

struct Status {
    bool supported;
    bool initialized;
    bool sampleValid;
    bool adcCalibrated;
    int32_t raw;
    uint16_t adcMillivolts;
    bool powerManaged;
    bool batteryConnected;
    bool usbPresent;
    bool charging;
    bool chargeDone;
    bool pmicVoltageValid;
    uint16_t pmicMillivolts;
    bool pmicPercentValid;
    uint8_t pmicPercent;
    uint32_t errors;
};

// Board-selected ADC GPIO. Raw sampling remains useful without a known divider scale.
bool begin();
Status status();
// Uses the PMIC's direct battery voltage when available; otherwise applies the ADC scale.
bool voltageMillivolts(uint16_t scalePermille, uint16_t &outMillivolts);

}  // namespace BoardBattery
