#include "my_rtc.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "my_log.h"
#include "my_rtc_time.h"

namespace MyRtc {

static const char *TAG = "rtc";
static constexpr int TIMEOUT_MS = 30;
static constexpr uint32_t I2C_HZ = 400000;
static constexpr uint8_t REG_TIME = 0x00, REG_CONTROL = 0x0E, REG_STATUS = 0x0F, REG_TEMP = 0x11;
static constexpr uint8_t ST_OSF = 0x80;
static constexpr time_t MIN_T = 1704067200;   // 2024-01-01
static constexpr time_t MAX_T = 4102444800;   // 2100-01-01

static i2c_master_dev_handle_t s_dev = nullptr;
static Status s_st = {};

const char *chipName() { return "DS3231"; }
bool temperatureAvailable() { return s_st.present; }

static bool readRegs(uint8_t reg, uint8_t *buf, size_t n)
{
    if (!s_dev) return false;
    bool ok = i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, TIMEOUT_MS) == ESP_OK;
    if (!ok) s_st.errors++;
    return ok;
}

static bool writeRegs(uint8_t reg, const uint8_t *buf, size_t n)
{
    if (!s_dev) return false;
    uint8_t tx[8];
    if (n > sizeof(tx) - 1) return false;
    tx[0] = reg;
    memcpy(tx + 1, buf, n);
    bool ok = i2c_master_transmit(s_dev, tx, n + 1, TIMEOUT_MS) == ESP_OK;
    if (!ok) s_st.errors++;
    return ok;
}

static bool readRaw(time_t &t, uint8_t &sec)
{
    uint8_t r[7];
    if (!readRegs(REG_TIME, r, sizeof(r))) return false;
    sec = r[0];
    return MyRtcTime::decode(r, t) && t >= MIN_T && t < MAX_T;
}

bool readTemp(float &c)
{
    uint8_t r[2];
    if (!readRegs(REG_TEMP, r, 2)) return false;
    c = (float)(int8_t)r[0] + (float)(r[1] >> 6) * 0.25f;
    return true;
}

bool read(time_t &utc)
{
    if (!s_st.timeValid) return false;
    uint8_t sec;
    return readRaw(utc, sec);
}

bool begin(i2c_master_bus_handle_t bus, time_t *outTime)
{
    s_st = {};
    s_st.state = State::MISSING;
    if (!bus || i2c_master_probe(bus, I2C_ADDR, 20) != ESP_OK) {
        ESP_LOGW(TAG, "no DS3231 at 0x%02X", I2C_ADDR);
        return false;
    }
    s_st.present = true;
    if (!s_dev) {
        i2c_device_config_t dc = {};
        dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dc.device_address = I2C_ADDR;
        dc.scl_speed_hz = I2C_HZ;
        if (i2c_master_bus_add_device(bus, &dc, &s_dev) != ESP_OK) s_dev = nullptr;
    }
    uint8_t ctrl = 0, stat = 0;
    if (!readRegs(REG_CONTROL, &ctrl, 1) || !readRegs(REG_STATUS, &stat, 1)) {
        s_st.state = State::BUS_ERROR;
        ESP_LOGE(TAG, "register read failed");
        return false;
    }
    s_st.osf = (stat & ST_OSF) != 0;
    // Oscillator must run on battery (EOSC = 0), square wave off (INTCN = 1), 32 kHz output off:
    // saves the coin cell.
    if (ctrl & 0x80) {
        uint8_t c = (uint8_t)(ctrl & ~0x80) | 0x04;
        writeRegs(REG_CONTROL, &c, 1);
        ESP_LOGW(TAG, "EOSC was set (oscillator off on battery) - cleared");
    }
    if (stat & 0x08) {
        uint8_t s = (uint8_t)(stat & ~0x08);
        writeRegs(REG_STATUS, &s, 1);
    }
    readTemp(s_st.tempC);

    time_t t0 = 0;
    uint8_t sec0 = 0;
    bool valid = readRaw(t0, sec0);
    // Tick test: wait for the seconds register to change (also aligns us to the second edge).
    const int64_t until = esp_timer_get_time() + 1150000;
    time_t t1 = t0;
    uint8_t sec1 = sec0;
    while (esp_timer_get_time() < until) {
        vTaskDelay(pdMS_TO_TICKS(10));
        time_t t = 0;
        uint8_t s = sec0;   // Unchanged if the read fails
        bool v = readRaw(t, s);
        if (s != sec0) {
            s_st.ticking = true;
            t1 = t;
            sec1 = s;
            valid = valid && v;
            break;
        }
    }
    (void)sec1;
    bool tempOk = s_st.tempC > -40.0f && s_st.tempC < 85.0f;
    if (!s_st.ticking) s_st.state = State::NOT_TICKING;
    else if (s_st.osf || !valid) s_st.state = State::TIME_INVALID;
    else s_st.state = State::OK;
    s_st.timeValid = s_st.state == State::OK;
    if (s_st.timeValid) {
        s_st.bootTime = t1;
        // We are right at the edge of second t1: set the system clock with ~10 ms accuracy.
        struct timeval tv = {t1, 5000};
        settimeofday(&tv, nullptr);
        if (outTime) *outTime = t1;
    }
    MyLog::event(TAG, "DS3231: %s (OSF %d, ticking %d, %.2f C%s)", stateText(s_st.state), s_st.osf, s_st.ticking,
                 (double)s_st.tempC, tempOk ? "" : ", temp out of range");
    return s_st.timeValid;
}

bool write(time_t utcNow)
{
    if (!s_dev) return false;
    // Wait for the next system second edge so the RTC second starts in phase.
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint32_t waitMs = (uint32_t)((1000000 - tv.tv_usec) / 1000);
    if (waitMs > 0 && waitMs <= 1000) vTaskDelay(pdMS_TO_TICKS(waitMs));
    gettimeofday(&tv, nullptr);
    time_t t = tv.tv_sec + (tv.tv_usec >= 500000 ? 1 : 0);
    if (utcNow == 0) utcNow = t;
    (void)utcNow;
    uint8_t r[7];
    MyRtcTime::encode(t, r);
    if (!writeRegs(REG_TIME, r, sizeof(r))) return false;
    uint8_t stat = 0;
    if (readRegs(REG_STATUS, &stat, 1)) {
        stat &= (uint8_t)~(ST_OSF | 0x08);
        writeRegs(REG_STATUS, &stat, 1);
    }
    s_st.lastWrite = t;
    s_st.writes++;
    s_st.lastDriftS = 0;
    if (s_st.state == State::TIME_INVALID || s_st.state == State::OK) s_st.state = State::OK;
    s_st.timeValid = s_st.state == State::OK;
    return true;
}

bool syncFromSystem(bool force)
{
    if (!s_st.present || !s_dev || s_st.state == State::NOT_TICKING) return false;
    time_t sys = time(nullptr);
    if (sys < MIN_T) return false;
    time_t rtc = 0;
    uint8_t sec;
    bool ok = readRaw(rtc, sec);
    readTemp(s_st.tempC);
    if (ok && s_st.timeValid) {
        int64_t d = (int64_t)rtc - (int64_t)sys;
        s_st.lastDriftS = (int32_t)d;
        if (!force && d > -1 && d < 1) return true;
    }
    int32_t drift = s_st.lastDriftS;
    bool wasValid = s_st.timeValid;
    if (!write(0)) return false;
    if (wasValid) MyLog::event(TAG, "RTC updated from system clock (drift %ld s)", (long)drift);
    else MyLog::event(TAG, "RTC set from system clock (was invalid)");
    return true;
}

Status status() { return s_st; }

const char *stateText(State s)
{
    switch (s) {
    case State::OK: return "OK";
    case State::MISSING: return "saknas";
    case State::BUS_ERROR: return "I2C-fel";
    case State::TIME_INVALID: return "tid ogiltig (batteri?)";
    case State::NOT_TICKING: return "oscillatorn går inte";
    default: return "ej testad";
    }
}

}  // namespace MyRtc
