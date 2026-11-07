#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <sys/time.h>

#include "my_rtc.h"
#include "my_rtc_backends.h"
#include "my_rtc_time.h"
#include "my_rtc_pcf85063_time.h"

constexpr int64_t SECOND = 1000000;
constexpr time_t EPOCH = 1793820605;
using TickType_t = uint32_t;
#define pdMS_TO_TICKS(ms) ((ms) * configTICK_RATE_HZ / 1000)
#define RTC_NOINIT_ATTR
enum esp_reset_reason_t {
    ESP_RST_POWERON, ESP_RST_SW, ESP_RST_DEEPSLEEP, ESP_RST_PANIC,
    ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_BROWNOUT, ESP_RST_EXT
};

int64_t wallUs, monoUs, rtcUs;
int64_t readDurationUs, clockStepUs, tickReadDurationUs, wakeExtraUs;
int64_t wallCallBeforeUs, wallCallAfterUs;
bool latchAtEnd, failRead, failWrite, failStatusRead, failStatusWrite, failWall;
bool failSecondWall;
bool invalidRegisters;
int wallCalls, delayCalls, rtcWrites;
esp_reset_reason_t resetReason = ESP_RST_SW;

void advance(int64_t us)
{
    wallUs += us;
    monoUs += us;
    rtcUs += us;
}

int fakeGettimeofday(timeval *tv, void *)
{
    ++wallCalls;
    if (failWall || (failSecondWall && wallCalls == 2)) return -1;
    advance(wallCallBeforeUs);
    tv->tv_sec = wallUs / SECOND;
    tv->tv_usec = wallUs % SECOND;
    advance(wallCallAfterUs);
    return 0;
}
time_t fakeTime(time_t *) { return wallUs / SECOND; }
int fakeSettimeofday(const timeval *tv, const void *)
{
    wallUs = static_cast<int64_t>(tv->tv_sec) * SECOND + tv->tv_usec;
    return 0;
}
#define gettimeofday fakeGettimeofday
#define settimeofday fakeSettimeofday
#define time fakeTime

int64_t esp_timer_get_time() { return monoUs; }
uint64_t esp_rtc_get_time_us()
{
    const uint64_t ticks = monoUs;
    advance(tickReadDurationUs);
    wallUs += clockStepUs;
    clockStepUs = 0;
    return ticks;
}
esp_reset_reason_t esp_reset_reason() { return resetReason; }
void vTaskDelay(TickType_t ticks)
{
    ++delayCalls;
    // Worst-case RTOS tick phase: a delay of N ticks can last only N-1 periods.
    assert(ticks > 0 && ticks <= configTICK_RATE_HZ + 1);
    advance((ticks - 1) * (SECOND / configTICK_RATE_HZ) + wakeExtraUs);
}
void logStub(const char *, const char *, ...) {}
#define ESP_LOGI(...) logStub(__VA_ARGS__)
#define ESP_LOGW(...) logStub(__VA_ARGS__)
#define ESP_LOGE(...) logStub(__VA_ARGS__)
namespace MyLog {
void event(const char *, const char *, ...) {}
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t, uint16_t, int) { return ESP_OK; }
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t, const i2c_device_config_t *,
                                  i2c_master_dev_handle_t *dev)
{
    *dev = reinterpret_cast<void *>(1);
    return ESP_OK;
}
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t, const uint8_t *reg, size_t,
                                     uint8_t *out, size_t count, int)
{
    if (failRead || (failStatusRead && *reg == 0x0F)) return ESP_FAIL;
    std::memset(out, 0, count);
    if (count != 7) return ESP_OK;
    time_t captured = rtcUs / SECOND;
    advance(readDurationUs);
    wallUs += clockStepUs;
    clockStepUs = 0;
    if (latchAtEnd) captured = rtcUs / SECOND;
#ifdef TEST_PCF85063
    MyRtcPcf85063Time::encode(captured, out);
#else
    MyRtcTime::encode(captured, out);
#endif
    if (invalidRegisters) out[0] = 0x6A;
    return ESP_OK;
}
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t, const uint8_t *data, size_t count, int)
{
    if (failWrite || (failStatusWrite && data[0] == 0x0F)) return ESP_FAIL;
    if (count == 8) {
        time_t written = 0;
#ifdef TEST_PCF85063
        assert(MyRtcPcf85063Time::decode(data + 1, written));
#else
        assert(MyRtcTime::decode(data + 1, written));
#endif
        assert(written == wallUs / SECOND); // Never encode a future rounded second.
        rtcUs = static_cast<int64_t>(written) * SECOND;
        ++rtcWrites;
    }
    return ESP_OK;
}
namespace OtrI2c {
esp_err_t probe(i2c_master_bus_handle_t b, uint16_t a, int t)
{
    return i2c_master_probe(b, a, t);
}
esp_err_t transmit(i2c_master_dev_handle_t d, const uint8_t *p, size_t n, int t)
{
    return i2c_master_transmit(d, p, n, t);
}
esp_err_t transmitReceive(i2c_master_dev_handle_t d, const uint8_t *p, size_t n,
                          uint8_t *r, size_t s, int t)
{
    return i2c_master_transmit_receive(d, p, n, r, s, t);
}
}

// SYNC_UNDER_TEST
// BACKEND_UNDER_TEST

#ifdef TEST_INTERNAL
namespace Backend = MyRtc::Internal;
#elif defined(TEST_DS3231) && defined(CONFIG_OTR_HW_PROFILE_P4_43)
namespace Backend = MyRtc::Ds3231;
#else
namespace Backend = MyRtc;
#endif

void reset(int64_t phase = 500000, int drift = 0)
{
    wallUs = static_cast<int64_t>(EPOCH) * SECOND + phase;
    monoUs = 10 * SECOND + phase;
    rtcUs = wallUs + static_cast<int64_t>(drift) * SECOND;
    readDurationUs = tickReadDurationUs = 100;
    clockStepUs = wakeExtraUs = 0;
    wallCallBeforeUs = wallCallAfterUs = 0;
    failRead = failWrite = failStatusRead = failStatusWrite = failWall = failSecondWall = false;
    invalidRegisters = false;
    latchAtEnd = false;
    wallCalls = delayCalls = rtcWrites = 0;
    Backend::s_st = {};
    Backend::s_st.present = true;
    Backend::s_st.ticking = true;
    Backend::s_st.timeValid = true;
    Backend::s_st.state = MyRtc::State::OK;
#ifdef TEST_INTERNAL
    Backend::s_anchor.magic = Backend::ANCHOR_MAGIC;
    Backend::s_anchor.epoch = EPOCH + drift;
    Backend::s_anchor.ticksUs = 10 * SECOND;
    Backend::s_anchor.check = Backend::checksum(Backend::s_anchor);
#else
    Backend::s_dev = reinterpret_cast<void *>(1);
#endif
}

void checkComparison()
{
    using MyRtcSync::Sample;
    auto sample = [](int64_t wall, int64_t mono) {
        return Sample{{static_cast<time_t>(wall / SECOND), static_cast<suseconds_t>(wall % SECOND)},
                      mono, mono};
    };
    const int64_t base = static_cast<int64_t>(EPOCH) * SECOND;
    int32_t drift = 99;
    const auto before = sample(base + 999900, 0);
    const auto after = sample(base + 1000100, 200);
    assert(MyRtcSync::compare(EPOCH, before, after, drift) && drift == 0);
    assert(MyRtcSync::compare(EPOCH + 1, before, after, drift) && drift == 0);
    assert(MyRtcSync::compare(EPOCH + 2, before, after, drift) && drift == 0);
    assert(MyRtcSync::compare(EPOCH + 3, before, after, drift) && drift == 1);
    assert(MyRtcSync::compare(EPOCH - 2, before, after, drift) && drift == -1);
    assert(!MyRtcSync::compare(EPOCH, before, sample(base, 200), drift));
    assert(!MyRtcSync::compare(EPOCH, before, sample(base + 3000100, 200), drift));
    const auto longRead = sample(base + 5999900, 5000000);
    assert(MyRtcSync::compare(EPOCH + 4, before, longRead, drift) && drift == 0);
    const auto exact = sample(base, 0);
    assert(MyRtcSync::compare(EPOCH + 1, exact, exact, drift) && drift == 1);
    assert(MyRtcSync::compare(EPOCH - 2, exact, exact, drift) && drift == -1);
}

void checkSamplingBrackets()
{
    // Preemption can occur before OR after gettimeofday acquires the wall time.
    // This exercises sample(), not just artificially constructed Sample objects.
    for (int64_t preemptionBefore : {0, 2000000}) {
        for (int64_t preemptionAfter : {0, 3000000}) {
            reset();
            wallCallBeforeUs = preemptionBefore;
            wallCallAfterUs = preemptionAfter;
            MyRtcSync::Sample before = {}, after = {};
            assert(MyRtcSync::sample(before, 1704067200, 4102444800LL));
            const time_t rtc = rtcUs / SECOND;
            advance(100);
            assert(MyRtcSync::sample(after, 1704067200, 4102444800LL));
            assert(before.monoAfter - before.monoBefore == preemptionBefore + preemptionAfter);
            assert(after.monoAfter - after.monoBefore == preemptionBefore + preemptionAfter);
            int32_t drift = 99;
            assert(MyRtcSync::compare(rtc, before, after, drift) && drift == 0);
        }
    }
}

void checkSync()
{
    reset();
    assert(Backend::syncFromSystem(false));
    assert(Backend::status().writes == 0 && Backend::status().lastDriftS == 0 && delayCalls == 0);

    // The RTC may latch at either end of a read crossing the system second edge.
    for (bool end : {false, true}) {
        reset(999950);
        latchAtEnd = end;
        assert(Backend::syncFromSystem(false) && Backend::status().writes == 0);
    }
    // Adjacent integer seconds alone cannot confirm a full second of drift.
    for (int drift : {-1, 1}) {
        reset(500000, drift);
        assert(Backend::syncFromSystem(false) && Backend::status().writes == 0);
    }
    for (int drift : {-3, 3}) {
        reset(500000, drift);
        assert(Backend::syncFromSystem(false) && Backend::status().writes == 1);
        assert(Backend::status().lastWrite == wallUs / SECOND && delayCalls == 1);
    }
    reset(999999);
    assert(Backend::syncFromSystem(true) && Backend::status().writes == 1 && delayCalls == 1);
    reset();
    Backend::s_st.timeValid = false;
    Backend::s_st.state = MyRtc::State::TIME_INVALID;
    assert(Backend::syncFromSystem(false));
    assert(Backend::status().writes == 1 && Backend::status().timeValid);
    reset();
#ifdef TEST_INTERNAL
    Backend::s_anchor.check ^= 1;
#else
    invalidRegisters = true;
#endif
    assert(Backend::syncFromSystem(false) && Backend::status().writes == 1);

    reset();
    clockStepUs = -2 * SECOND;
    assert(!Backend::syncFromSystem(true) && Backend::status().writes == 0 && delayCalls == 0);
    reset();
    clockStepUs = 2 * SECOND;
    assert(!Backend::syncFromSystem(true) && Backend::status().writes == 0 && delayCalls == 0);
    reset();
    readDurationUs = tickReadDurationUs = 3 * SECOND;
    assert(Backend::syncFromSystem(false) && Backend::status().writes == 0);

    reset();
    failWall = true;
    assert(!Backend::syncFromSystem(true) && Backend::status().writes == 0);
    reset();
    wallUs = (1704067200LL - 1) * SECOND;
    assert(!Backend::syncFromSystem(true) && Backend::status().writes == 0);
    reset();
    wallUs = 4102444800LL * SECOND;
    assert(!Backend::syncFromSystem(true) && Backend::status().writes == 0);
}

void checkWrites()
{
    for (int64_t phase : {0, 1, 499999, 999000, 999999}) {
        reset(phase);
        assert(Backend::write(0));
        assert(delayCalls == 1 && Backend::status().writes == 1);
        assert(Backend::status().lastWrite == EPOCH + 1);
    }
    // Scheduler oversleep near a later boundary must still floor, not round up.
    reset(999999);
    wakeExtraUs = 1999999;
    assert(Backend::write(0));
    assert(Backend::status().lastWrite == wallUs / SECOND);
    reset();
    failSecondWall = true;
    assert(!Backend::write(0) && Backend::status().errors == 1 && Backend::status().writes == 0);
}

void checkFailuresAndRetention()
{
#ifndef TEST_INTERNAL
    reset();
    failRead = true;
    assert(!Backend::syncFromSystem(true) && Backend::status().errors == 1 && delayCalls == 0);
    reset();
    failWrite = true;
    assert(!Backend::syncFromSystem(true) && Backend::status().errors == 1 && Backend::status().writes == 0);
#ifdef TEST_DS3231
    for (bool readFailure : {false, true}) {
        reset();
        failStatusRead = readFailure;
        failStatusWrite = !readFailure;
        assert(!Backend::syncFromSystem(true));
        assert(Backend::status().errors == 1 && Backend::status().writes == 0);
    }
#endif
#else
    // Explicit internal UTC writes still establish the same integer trusted anchor.
    reset();
    assert(Backend::write(EPOCH + 12));
    assert(delayCalls == 0);
    time_t utc = 0;
    assert(Backend::read(utc) && utc == EPOCH + 12);
    for (auto reason : {ESP_RST_SW, ESP_RST_DEEPSLEEP, ESP_RST_PANIC, ESP_RST_INT_WDT, ESP_RST_TASK_WDT}) {
        reset();
        resetReason = reason;
        assert(Backend::begin(nullptr, &utc) && Backend::status().timeValid);
    }
    for (auto reason : {ESP_RST_POWERON, ESP_RST_WDT, ESP_RST_BROWNOUT, ESP_RST_EXT}) {
        reset();
        resetReason = reason;
        assert(!Backend::begin(nullptr, &utc) && !Backend::status().timeValid);
    }
#endif
    reset();
    Backend::s_st.state = MyRtc::State::NOT_TICKING;
    Backend::s_st.ticking = false;
    assert(!Backend::syncFromSystem(true) && Backend::status().writes == 0);
}

int main()
{
    checkComparison();
    checkSamplingBrackets();
    checkSync();
    checkWrites();
    checkFailuresAndRetention();
    std::puts("RTC uncertainty, alignment, failure and retention checks passed");
}
