#include "my_rtc.h"

#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "my_log.h"
#include "my_rtc_pcf85063_time.h"
#include "my_rtc_sync.h"
#include "otr_i2c.h"

namespace MyRtc {
namespace {

constexpr const char *TAG = "rtc";
constexpr int TIMEOUT_MS = 30;
constexpr uint32_t I2C_HZ = 400000;
constexpr uint8_t REG_CONTROL1 = 0x00;
constexpr uint8_t REG_SECONDS = 0x04;
constexpr uint8_t SECONDS_OS = 0x80;
constexpr uint8_t CONTROL1_12H = 0x02;
constexpr time_t MIN_T = 1704067200;   // 2024-01-01
constexpr time_t MAX_T = 4102444800LL; // 2100-01-01

i2c_master_dev_handle_t s_dev = nullptr;
Status s_st = {};

bool readRegs(uint8_t reg, uint8_t *buf, size_t count)
{
    if (!s_dev || OtrI2c::transmitReceive(s_dev, &reg, 1, buf, count, TIMEOUT_MS) != ESP_OK) {
        ++s_st.errors;
        return false;
    }
    return true;
}

bool writeRegs(uint8_t reg, const uint8_t *buf, size_t count)
{
    if (!s_dev || count > 7) return false;
    uint8_t tx[8];
    tx[0] = reg;
    memcpy(tx + 1, buf, count);
    if (OtrI2c::transmit(s_dev, tx, count + 1, TIMEOUT_MS) != ESP_OK) {
        ++s_st.errors;
        return false;
    }
    return true;
}

bool readRaw(time_t &utc, uint8_t &seconds)
{
    uint8_t registers[7] = {};
    if (!readRegs(REG_SECONDS, registers, sizeof(registers))) return false;
    seconds = registers[0] & 0x7F;
    if (registers[0] & SECONDS_OS) {
        s_st.osf = true;
        return false;
    }
    return MyRtcPcf85063Time::decode(registers, utc) && utc >= MIN_T && utc < MAX_T;
}

}  // namespace

const char *chipName() { return "PCF85063"; }
bool temperatureAvailable() { return false; }
bool readTemp(float &) { return false; }

bool begin(i2c_master_bus_handle_t bus, time_t *outTime)
{
    s_st = {};
    s_st.state = State::MISSING;
    if (!bus || OtrI2c::probe(bus, I2C_ADDR, TIMEOUT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "no PCF85063 at 0x%02X", I2C_ADDR);
        return false;
    }
    s_st.present = true;
    if (!s_dev) {
        i2c_device_config_t config = {};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = I2C_ADDR;
        config.scl_speed_hz = I2C_HZ;
        if (i2c_master_bus_add_device(bus, &config, &s_dev) != ESP_OK) s_dev = nullptr;
    }
    if (!s_dev) {
        s_st.state = State::BUS_ERROR;
        return false;
    }

    uint8_t control = 0;
    if (!readRegs(REG_CONTROL1, &control, 1)) {
        s_st.state = State::BUS_ERROR;
        return false;
    }
    if (control & CONTROL1_12H) {
        control &= static_cast<uint8_t>(~CONTROL1_12H);
        if (!writeRegs(REG_CONTROL1, &control, 1)) {
            s_st.state = State::BUS_ERROR;
            return false;
        }
    }

    time_t firstTime = 0;
    uint8_t firstSeconds = 0;
    const bool firstValid = readRaw(firstTime, firstSeconds);
    const int64_t deadline = esp_timer_get_time() + 1150000;
    time_t tickTime = firstTime;
    while (esp_timer_get_time() < deadline) {
        vTaskDelay(pdMS_TO_TICKS(10));
        time_t candidateTime = 0;
        uint8_t candidateSeconds = firstSeconds;
        const bool candidateValid = readRaw(candidateTime, candidateSeconds);
        if (candidateSeconds != firstSeconds) {
            s_st.ticking = true;
            tickTime = candidateTime;
            s_st.timeValid = firstValid && candidateValid;
            break;
        }
    }
    if (!s_st.ticking) s_st.state = State::NOT_TICKING;
    else if (!s_st.timeValid) s_st.state = State::TIME_INVALID;
    else s_st.state = State::OK;
    if (s_st.timeValid) {
        s_st.bootTime = tickTime;
        const timeval tv = {tickTime, 5000};
        settimeofday(&tv, nullptr);
        if (outTime) *outTime = tickTime;
    }
    MyLog::event(TAG, "PCF85063: %s (OS %d, ticking %d)", stateText(s_st.state), s_st.osf, s_st.ticking);
    return s_st.timeValid;
}

bool read(time_t &utc)
{
    if (!s_st.present || !s_st.timeValid) return false;
    uint8_t seconds = 0;
    return readRaw(utc, seconds);
}

bool write(time_t utcNow)
{
    if (!s_dev) return false;
    (void)utcNow;
    time_t utc = 0;
    if (!MyRtcSync::alignedSystemTime(utc, MIN_T, MAX_T)) {
        ++s_st.errors;
        return false;
    }

    uint8_t registers[7];
    MyRtcPcf85063Time::encode(utc, registers);
    if (!writeRegs(REG_SECONDS, registers, sizeof(registers))) return false;
    s_st.osf = false;
    s_st.lastWrite = utc;
    ++s_st.writes;
    s_st.lastDriftS = 0;
    s_st.ticking = true;
    s_st.timeValid = true;
    s_st.state = State::OK;
    return true;
}

bool syncFromSystem(bool force)
{
    if (!s_st.present || !s_dev || s_st.state == State::NOT_TICKING) return false;
    MyRtcSync::Sample before = {}, after = {};
    if (!MyRtcSync::sample(before, MIN_T, MAX_T)) return false;
    time_t rtcTime = 0;
    const uint32_t errors = s_st.errors;
    const bool ok = read(rtcTime);
    if (!MyRtcSync::sample(after, MIN_T, MAX_T)) return false;
    if (s_st.errors != errors) return false;
    int32_t confirmedDrift = 0;
    if (!MyRtcSync::compare(rtcTime, before, after, confirmedDrift)) return false;
    if (ok) {
        s_st.lastDriftS = confirmedDrift;
        if (!force && confirmedDrift == 0) return true;
    }
    const bool wasValid = s_st.timeValid;
    const int32_t drift = s_st.lastDriftS;
    if (!write(0)) return false;
    if (wasValid) MyLog::event(TAG, "RTC updated from system clock (drift %ld s)", static_cast<long>(drift));
    else MyLog::event(TAG, "RTC set from system clock (was invalid)");
    return true;
}

Status status() { return s_st; }

const char *stateText(State state)
{
    switch (state) {
    case State::OK: return "OK";
    case State::MISSING: return "saknas";
    case State::BUS_ERROR: return "I2C-fel";
    case State::TIME_INVALID: return "tid ogiltig (batteri?)";
    case State::NOT_TICKING: return "oscillatorn går inte";
    default: return "ej testad";
    }
}

}  // namespace MyRtc
