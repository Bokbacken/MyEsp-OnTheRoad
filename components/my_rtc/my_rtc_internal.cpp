#include "my_rtc.h"

#include <sys/time.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rtc_time.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace MyRtc {
namespace {

constexpr time_t MIN_T = 1704067200;
constexpr time_t MAX_T = 4102444800LL;
constexpr uint32_t ANCHOR_MAGIC = 0x52544331;

struct Anchor {
    uint32_t magic;
    time_t epoch;
    uint64_t ticksUs;
    uint64_t check;
};

// Keep a trusted UTC anchor separate from MyTime's deliberately uncertain NVS fallback.
RTC_NOINIT_ATTR Anchor s_anchor;
Status s_st{};

uint64_t checksum(const Anchor &a)
{
    return static_cast<uint64_t>(a.epoch) ^ a.ticksUs ^ ANCHOR_MAGIC;
}

bool readAnchor(time_t &utc)
{
    const uint64_t ticks = esp_rtc_get_time_us();
    if (s_anchor.magic != ANCHOR_MAGIC || s_anchor.check != checksum(s_anchor) ||
        s_anchor.epoch < MIN_T || s_anchor.epoch >= MAX_T || ticks < s_anchor.ticksUs)
        return false;
    const uint64_t seconds = (ticks - s_anchor.ticksUs) / 1000000;
    if (seconds >= static_cast<uint64_t>(MAX_T - s_anchor.epoch)) return false;
    utc = s_anchor.epoch + static_cast<time_t>(seconds);
    return true;
}

}  // namespace

const char *chipName() { return "ESP32-P4 RTC"; }
bool temperatureAvailable() { return false; }
bool readTemp(float &) { return false; }

bool begin(i2c_master_bus_handle_t, time_t *outTime)
{
    s_st = {};
    s_st.present = true;
    const uint64_t before = esp_rtc_get_time_us();
    vTaskDelay(pdMS_TO_TICKS(20));
    s_st.ticking = esp_rtc_get_time_us() > before;
    const esp_reset_reason_t reset = esp_reset_reason();
    // Diagnostics only: shows whether the anchor and counter survived resets we do not trust yet.
    const uint64_t nowTicks = esp_rtc_get_time_us();
    ESP_LOGI("rtc", "anchor before reset check: reset=%d magic=%d check=%d anchor_us=%llu now_us=%llu",
             static_cast<int>(reset), s_anchor.magic == ANCHOR_MAGIC,
             s_anchor.check == checksum(s_anchor),
             static_cast<unsigned long long>(s_anchor.ticksUs),
             static_cast<unsigned long long>(nowTicks));
    if (reset != ESP_RST_SW && reset != ESP_RST_DEEPSLEEP) s_anchor.magic = 0;
    time_t utc = 0;
    s_st.timeValid = s_st.ticking && readAnchor(utc);
    s_st.osf = !s_st.timeValid;
    s_st.state = !s_st.ticking ? State::NOT_TICKING :
                 s_st.timeValid ? State::OK : State::TIME_INVALID;
    if (s_st.timeValid) {
        const timeval tv = {utc, 0};
        settimeofday(&tv, nullptr);
        s_st.bootTime = utc;
        if (outTime) *outTime = utc;
    }
    ESP_LOGI("rtc", "internal RTC: %s (reset=%d); VBAT retention requires controlled deep sleep, not abrupt power-off",
             stateText(s_st.state), static_cast<int>(reset));
    return s_st.timeValid;
}

bool read(time_t &utc)
{
    return s_st.timeValid && readAnchor(utc);
}

bool write(time_t utcNow)
{
    if (!s_st.present || !s_st.ticking) return false;
    if (!utcNow) utcNow = time(nullptr);
    if (utcNow < MIN_T || utcNow >= MAX_T) {
        ++s_st.errors;
        ESP_LOGW("rtc", "refusing invalid internal RTC time");
        return false;
    }
    const uint64_t ticks = esp_rtc_get_time_us();
    s_anchor.magic = 0;
    s_anchor.epoch = utcNow;
    s_anchor.ticksUs = ticks;
    s_anchor.check = checksum(s_anchor);
    s_anchor.magic = ANCHOR_MAGIC;
    s_st.lastWrite = utcNow;
    ++s_st.writes;
    s_st.lastDriftS = 0;
    s_st.timeValid = true;
    s_st.state = State::OK;
    return true;
}

bool syncFromSystem(bool force)
{
    const time_t sys = time(nullptr);
    if (sys < MIN_T || sys >= MAX_T) return false;
    time_t rtc = 0;
    if (read(rtc)) {
        const int64_t drift = static_cast<int64_t>(rtc) - sys;
        s_st.lastDriftS = static_cast<int32_t>(drift);
        if (!force && drift > -1 && drift < 1) return true;
    }
    return write(sys);
}

Status status() { return s_st; }

const char *stateText(State state)
{
    switch (state) {
    case State::OK: return "OK";
    case State::TIME_INVALID: return "interna RTC väntar på NTP/manuell tid";
    case State::NOT_TICKING: return "interna RTC går inte";
    case State::MISSING: return "saknas";
    case State::BUS_ERROR: return "RTC-fel";
    default: return "ej testad";
    }
}

}  // namespace MyRtc
