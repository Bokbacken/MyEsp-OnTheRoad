#include "my_rtc_backends.h"

#include "esp_log.h"

namespace MyRtc {
namespace {
bool s_external = false;
}

bool begin(i2c_master_bus_handle_t bus, time_t *outTime)
{
    const bool valid = Ds3231::begin(bus, outTime);
    // A responding RTC with invalid time or an I2C error must stay visible, not be hidden by fallback.
    s_external = Ds3231::status().present;
    if (s_external) {
        ESP_LOGI("rtc", "selected external DS3231");
        return valid;
    }
    ESP_LOGI("rtc", "DS3231 absent; selected internal RTC");
    return Internal::begin(bus, outTime);
}

bool read(time_t &utc) { return s_external ? Ds3231::read(utc) : Internal::read(utc); }
bool write(time_t utc) { return s_external ? Ds3231::write(utc) : Internal::write(utc); }
bool readTemp(float &c) { return s_external ? Ds3231::readTemp(c) : Internal::readTemp(c); }
const char *chipName() { return s_external ? Ds3231::chipName() : Internal::chipName(); }
bool temperatureAvailable()
{
    return s_external ? Ds3231::temperatureAvailable() : Internal::temperatureAvailable();
}
bool syncFromSystem(bool force)
{
    return s_external ? Ds3231::syncFromSystem(force) : Internal::syncFromSystem(force);
}
Status status() { return s_external ? Ds3231::status() : Internal::status(); }
const char *stateText(State s) { return s_external ? Ds3231::stateText(s) : Internal::stateText(s); }
}
