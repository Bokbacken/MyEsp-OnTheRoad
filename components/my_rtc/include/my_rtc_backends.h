#pragma once

#include "my_rtc.h"

namespace MyRtc::Ds3231 {
bool begin(i2c_master_bus_handle_t bus, time_t *outTime);
bool read(time_t &utc);
bool write(time_t utcNow);
bool readTemp(float &c);
const char *chipName();
bool temperatureAvailable();
bool syncFromSystem(bool force);
Status status();
const char *stateText(State s);
}

namespace MyRtc::Internal {
bool begin(i2c_master_bus_handle_t bus, time_t *outTime);
bool read(time_t &utc);
bool write(time_t utcNow);
bool readTemp(float &c);
const char *chipName();
bool temperatureAvailable();
bool syncFromSystem(bool force);
Status status();
const char *stateText(State s);
}
