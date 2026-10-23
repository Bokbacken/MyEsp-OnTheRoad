#include "board_clock.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_sntp.h>
#include <sys/time.h>

#include "board_config.h"

namespace Clock {

static constexpr time_t MIN_EPOCH = 1704067200;   // 2024-01-01 00:00 UTC
static constexpr time_t MAX_EPOCH = 4102444800;   // 2100-01-01 00:00 UTC
static constexpr uint32_t SAVE_EVERY_MS = 10UL * 60 * 1000;

static volatile Source s_source = Source::NONE;
static uint32_t s_lastSave = 0;
static volatile uint32_t s_lastNtpMs = 0;
static volatile bool s_ntpEver = false;
static bool s_sntpStarted = false;

static void saveNvs()
{
    time_t t = time(nullptr);
    if (t < MIN_EPOCH) return;
    Preferences p;
    if (p.begin("otr", false)) {
        p.putULong64("clk_t", (uint64_t)t);
        p.end();
    }
    s_lastSave = millis();
}

static void onSntpSync(struct timeval *)
{
    s_source = Source::NTP;
    s_lastNtpMs = millis();
    s_ntpEver = true;
    Serial.println("[clock] NTP sync");
}

void applyTz()
{
    setenv("TZ", Config::get().tz, 1);
    tzset();
}

void begin()
{
    applyTz();
    if (time(nullptr) >= MIN_EPOCH) {   // Survived a soft reset (RTC keeps running)
        s_source = Source::RESTORED;
        return;
    }
    Preferences p;
    if (p.begin("otr", true)) {
        uint64_t t = p.getULong64("clk_t", 0);
        p.end();
        if ((time_t)t >= MIN_EPOCH && (time_t)t < MAX_EPOCH) {
            struct timeval tv = {(time_t)t, 0};
            settimeofday(&tv, nullptr);
            s_source = Source::RESTORED;
            Serial.printf("[clock] restored %llu from NVS (uncertain)\n", (unsigned long long)t);
        }
    }
    s_lastSave = millis();
}

void onWifiConnected()
{
    if (!s_sntpStarted) {
        sntp_set_time_sync_notification_cb(onSntpSync);
        s_sntpStarted = true;
    }
    // (Re)starts SNTP with the configured zone; also triggers a fresh sync after a reconnect.
    configTzTime(Config::get().tz, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

void loop()
{
    if (valid() && millis() - s_lastSave >= SAVE_EVERY_MS) saveNvs();
}

bool valid() { return s_source != Source::NONE && time(nullptr) >= MIN_EPOCH; }
bool certain() { return s_source == Source::NTP || s_source == Source::MANUAL; }
Source source() { return valid() ? s_source : Source::NONE; }

const char *sourceText(Source s)
{
    switch (s) {
    case Source::NTP: return "NTP";
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
    saveNvs();
    Serial.printf("[clock] set manually to %lld\n", (long long)epoch);
    return true;
}

bool local(struct tm &out)
{
    if (!valid()) return false;
    time_t t = time(nullptr);
    localtime_r(&t, &out);
    return true;
}

uint32_t lastNtpSyncS() { return s_ntpEver ? (millis() - s_lastNtpMs) / 1000 : UINT32_MAX; }

}  // namespace Clock
