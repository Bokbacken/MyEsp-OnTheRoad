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
    uint32_t errors;
};

// ES3C35P only. Raw ADC sampling remains useful without divider calibration; battery voltage does not.
bool begin();
Status status();

}  // namespace BoardBattery
