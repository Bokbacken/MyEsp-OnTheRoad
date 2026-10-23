#include "my_time.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_sntp.h"
#include "my_log.h"
#include "my_storage.h"
#include "my_sys.h"

namespace MyTime {

static const char *TAG = "time";
static constexpr uint32_t SAVE_EVERY_MS = 10UL * 60 * 1000;

static volatile Source s_source = Source::NONE;
static uint32_t s_lastSave = 0;
static volatile uint32_t s_lastNtpMs = 0;
static volatile uint32_t s_syncs = 0;
static volatile bool s_ntpFlag = false;
static bool s_sntpStarted = false;
static char s_ns[16] = "time";
static char s_key[16] = "clk_t";

static void saveNvs()
{
    time_t t = time(nullptr);
    if (t < MIN_EPOCH) return;
    MyNvs::Handle h(s_ns, false);
    if (h) h.setU64(s_key, (uint64_t)t);
    s_lastSave = MySys::millis();
}

static void onSync(struct timeval *)
{
    bool first = s_syncs == 0;
    s_source = Source::NTP;
    s_lastNtpMs = MySys::millis();
    s_syncs = s_syncs + 1;
    s_ntpFlag = true;
    MyLog::setClockAnchor();
    if (first) MyLog::event(TAG, "NTP sync, clock is now certain");
}

void setTz(const char *tz)
{
    setenv("TZ", tz && tz[0] ? tz : "UTC0", 1);
    tzset();
}

void begin(const char *ns, const char *key, const char *tz)
{
    snprintf(s_ns, sizeof(s_ns), "%s", ns);
    snprintf(s_key, sizeof(s_key), "%s", key);
    setTz(tz);
    s_lastSave = MySys::millis();
    if (time(nullptr) >= MIN_EPOCH) {   // Survived a soft reset (RTC timer keeps running)
        s_source = Source::RESTORED;
        return;
    }
    MyNvs::Handle h(s_ns, true);
    uint64_t t = h ? h.getU64(s_key, 0) : 0;
    if ((time_t)t >= MIN_EPOCH && (time_t)t < MAX_EPOCH) {
        struct timeval tv = {(time_t)t, 0};
        settimeofday(&tv, nullptr);
        s_source = Source::RESTORED;
        ESP_LOGI(TAG, "restored %llu from NVS (uncertain)", (unsigned long long)t);
    }
}

void onNetworkUp()
{
    if (!s_sntpStarted) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_setservername(1, "time.google.com");
        esp_sntp_setservername(2, "time.cloudflare.com");
        sntp_set_time_sync_notification_cb(onSync);
        esp_sntp_init();
        s_sntpStarted = true;
        ESP_LOGI(TAG, "SNTP started");
    } else {
        esp_sntp_restart();   // Fresh sync after a reconnect
    }
}

void loop()
{
    if (valid() && MySys::millis() - s_lastSave >= SAVE_EVERY_MS) saveNvs();
}

bool valid() { return s_source != Source::NONE && time(nullptr) >= MIN_EPOCH; }
bool certain() { return s_source == Source::NTP || s_source == Source::MANUAL || s_source == Source::RTC; }
Source source() { return valid() ? s_source : Source::NONE; }

const char *sourceText(Source s)
{
    switch (s) {
    case Source::NTP: return "NTP";
    case Source::RTC: return "RTC";
    case Source::MANUAL: return "manuell";
    case Source::RESTORED: return "osäker";
    default: return "ej satt";
    }
}

time_t now() { return valid() ? time(nullptr) : 0; }

bool setManual(time_t epoch)
{
    if (epoch < MIN_EPOCH || epoch >= MAX_EPOCH) return false;
    struct timeval tv = {epoch, 0};
    settimeofday(&tv, nullptr);
    s_source = Source::MANUAL;
    s_ntpFlag = true;
    saveNvs();
    MyLog::setClockAnchor();
    MyLog::event(TAG, "clock set manually to %lld", (long long)epoch);
    return true;
}

bool setFromRtc(time_t epoch)
{
    if (epoch < MIN_EPOCH || epoch >= MAX_EPOCH) return false;
    if (llabs((long long)(time(nullptr) - epoch)) > 1) {   // my_rtc normally set it already (edge aligned)
        struct timeval tv = {epoch, 0};
        settimeofday(&tv, nullptr);
    }
    if (s_source != Source::NTP) s_source = Source::RTC;
    saveNvs();
    MyLog::setClockAnchor();
    return true;
}

bool takeClockChanged()
{
    if (!s_ntpFlag) return false;
    s_ntpFlag = false;
    return true;
}

bool local(struct tm &out)
{
    if (!valid()) return false;
    time_t t = time(nullptr);
    localtime_r(&t, &out);
    return true;
}

uint32_t lastNtpSyncS() { return s_syncs ? (MySys::millis() - s_lastNtpMs) / 1000 : UINT32_MAX; }
uint32_t ntpSyncs() { return s_syncs; }

}  // namespace MyTime
