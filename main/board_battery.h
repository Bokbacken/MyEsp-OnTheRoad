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

// Board-selected ADC GPIO. Raw sampling remains useful without a known divider scale.
bool begin();
Status status();

}  // namespace BoardBattery
