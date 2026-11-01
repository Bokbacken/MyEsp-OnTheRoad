// app_web.cpp - OnTheRoad routes (see header). JSON field names are unchanged from the
// Arduino 0.3.x builds, so the web pages work as before.
#include "app_web.h"
#include "lang_game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_players.h"
#include "app_web_files.h"
#include "board_audio.h"
#include "board_battery.h"
#include "board_backlight.h"
#include "board_sd.h"
#include "board_display.h"
#include "board_touch.h"
#include "board_pins.h"
#include "battery_math.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "game_live.h"
#include "kara_songs_web.h"
#include "kara_web.h"
#include "adv_live.h"
#include "app_web_adventure_editor.h"
#include "hw_profile.h"
#if defined(CONFIG_OTR_HW_PROFILE_P4_43)
#include "host_ota.h"
#endif
#include "my_diag.h"
#include "my_fonts.h"
#include "my_log.h"
#include "my_ota.h"
#include "my_rtc.h"
#include "power_history.h"
#include "time_zones.h"
#include "ui_screen_power.h"
#include "my_storage.h"
#include "my_time.h"
#include "my_web.h"
#include "my_wifi.h"
#include "multiplayer_protocol.h"
#include "otr_log.h"
#include "ui_boot.h"
#include "web_assets.h"

static const char *TAG = "appweb";

namespace AppWeb {

static ConfigChangedCallback s_configCb = nullptr;

void onConfigChanged(ConfigChangedCallback cb) { s_configCb = cb; }

static void *psAlloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

// Copies a trimmed parameter value. false when missing.
static bool getTrim(const MyWeb::Req &q, const char *name, char *out, size_t cap)
{
    if (!q.get(name, out, cap)) return false;
    char *s = out;
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
    memmove(out, s, n + 1);
    return true;
}

// ----------------------------------------------------------------------------------------
// Diagnostics providers (/api/info)
// ----------------------------------------------------------------------------------------
static void diagWifi(cJSON *o)
{
    MyWifi::Info wi = MyWifi::info();
    cJSON_AddStringToObject(o, "mode", "sta");
    cJSON_AddStringToObject(o, "status", MyWifi::statusText(wi.status));
    cJSON_AddBoolToObject(o, "connected", wi.connected);
    cJSON_AddStringToObject(o, "ssid", wi.ssid);
    cJSON_AddStringToObject(o, "saved_ssid", MyWifi::savedSsid());
    cJSON_AddStringToObject(o, "ip", wi.ip);
    cJSON_AddNumberToObject(o, "rssi", wi.rssi);
    cJSON_AddNumberToObject(o, "bars", wi.connected ? MyWifi::signalBars(wi.rssi) : 0);
    cJSON_AddNumberToObject(o, "uptime_s", wi.uptimeS);
    cJSON_AddNumberToObject(o, "connects", wi.connects);
    cJSON_AddNumberToObject(o, "disconnects", wi.disconnects);
    cJSON_AddNumberToObject(o, "roams", wi.roams);
    cJSON_AddStringToObject(o, "bssid", wi.bssid);
    cJSON_AddNumberToObject(o, "channel", wi.channel);
    cJSON_AddStringToObject(o, "last_error", MyWifi::failText(wi.lastFail));
    cJSON_AddNumberToObject(o, "last_reason", wi.lastReason);
    cJSON_AddStringToObject(o, "mac", wi.mac);
    cJSON_AddStringToObject(o, "hostname", Config::get().hostname);
    cJSON_AddBoolToObject(o, "mdns", MyWifi::mdnsOk());
    cJSON_AddNumberToObject(o, "saved", MyWifi::savedCount());
#if defined(CONFIG_OTR_HW_PROFILE_P4_43)
    if (HwProfile::active().hasHostRadio) {
        const HostOta::VersionInfo &versions = HostOta::versionInfo();
        cJSON *radio = cJSON_AddObjectToObject(o, "host_radio");
        cJSON_AddBoolToObject(radio, "known", versions.known);
        cJSON_AddStringToObject(radio, "host_version", versions.host);
        cJSON_AddStringToObject(radio, "coprocessor_version", versions.coprocessor);
        cJSON_AddBoolToObject(radio, "match", versions.match);
    }
#endif
}

static void diagDisplay(cJSON *o)
{
    Display::Stats ds = Display::stats();
    MyLvgl::Stats ls = MyLvgl::stats();
    cJSON_AddBoolToObject(o, "ok", Display::ok());
    cJSON_AddStringToObject(o, "model", LCD_MODEL);
    cJSON_AddStringToObject(o, "bus", LCD_BUS_DESC);
    cJSON_AddNumberToObject(o, "width", LCD_H_RES);
    cJSON_AddNumberToObject(o, "height", LCD_V_RES);
    cJSON_AddNumberToObject(o, "fps_x10", (int)(ds.fps * 10));
    cJSON_AddNumberToObject(o, "flushes", ds.flushes);
    cJSON_AddNumberToObject(o, "frames", ds.frames);
    cJSON_AddBoolToObject(o, "te_active", ds.teActive);
    cJSON_AddNumberToObject(o, "te_edges", ds.teEdges);
    cJSON_AddNumberToObject(o, "te_timeouts", ds.teTimeouts);
    cJSON_AddNumberToObject(o, "buf_bytes", ds.bufBytes);
    cJSON_AddBoolToObject(o, "buf_psram", ds.bufInPsram);
    cJSON_AddNumberToObject(o, "brightness", Config::get().brightness);
    cJSON_AddBoolToObject(o, "sleeping", Backlight::sleeping());
    cJSON_AddBoolToObject(o, "eco_sleeping", UiScreenPower::ecoSleeping());
    char ver[16];
    snprintf(ver, sizeof(ver), "%d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    cJSON_AddStringToObject(o, "lvgl", ver);
    cJSON_AddNumberToObject(o, "lv_mem_total", ls.lvMemTotal);
    cJSON_AddNumberToObject(o, "lv_mem_free", ls.lvMemFree);
    cJSON_AddNumberToObject(o, "lv_mem_max_used", ls.lvMemMaxUsed);
    cJSON_AddNumberToObject(o, "lv_mem_frag", ls.lvMemFrag);
    cJSON_AddNumberToObject(o, "lv_stack_free", ls.taskStackFree);
}

static void diagTouch(cJSON *o)
{
    Touch::Info ti = Touch::info();
    char fw[24];
#if defined(CONFIG_OTR_HW_PROFILE_P4_43)
    snprintf(fw, sizeof(fw), "0x%04X (GT%.4s)", ti.fwVersion, (const char *)ti.fwRevision);
#else
    snprintf(fw, sizeof(fw), "%u (%u.%u.%u.%u)", ti.fwVersion, ti.fwRevision[0], ti.fwRevision[1], ti.fwRevision[2],
             ti.fwRevision[3]);
#endif
    cJSON_AddBoolToObject(o, "present", ti.present);
    cJSON_AddStringToObject(o, "model", TOUCH_MODEL);
    cJSON_AddNumberToObject(o, "address", ti.address ? ti.address : TOUCH_I2C_ADDR);
    cJSON_AddStringToObject(o, "fw", fw);
    cJSON_AddNumberToObject(o, "max_x", ti.maxX);
    cJSON_AddNumberToObject(o, "max_y", ti.maxY);
    cJSON_AddNumberToObject(o, "max_points", ti.maxTouches);
    cJSON_AddBoolToObject(o, "pressed", ti.pressed);
    cJSON_AddNumberToObject(o, "last_x", ti.lastX);
    cJSON_AddNumberToObject(o, "last_y", ti.lastY);
    cJSON_AddNumberToObject(o, "touches", ti.touchCount);
    cJSON_AddNumberToObject(o, "irqs", ti.irqCount);
    cJSON_AddNumberToObject(o, "errors", ti.readErrors);
    cJSON_AddNumberToObject(o, "bridged", ti.bridgedGaps);
}

static void diagClock(cJSON *o)
{
    cJSON_AddNumberToObject(o, "epoch", (double)MyTime::now());
    cJSON_AddStringToObject(o, "source", MyTime::sourceText(MyTime::source()));
    cJSON_AddBoolToObject(o, "certain", MyTime::certain());
    cJSON_AddStringToObject(o, "tz", Config::get().tz);
    uint32_t age = MyTime::lastNtpSyncS();
    cJSON_AddNumberToObject(o, "ntp_age_s", age == UINT32_MAX ? -1 : (double)age);
    cJSON_AddNumberToObject(o, "ntp_syncs", MyTime::ntpSyncs());
    cJSON_AddBoolToObject(o, "log_anchored", MyLog::clockAnchored());
}

static void diagRtc(cJSON *o)
{
    const MyRtc::Status s = MyRtc::status();
    cJSON_AddStringToObject(o, "chip", MyRtc::chipName());
    cJSON_AddStringToObject(o, "state", MyRtc::stateText(s.state));
    cJSON_AddBoolToObject(o, "ok", s.state == MyRtc::State::OK);
    cJSON_AddBoolToObject(o, "present", s.present);
    cJSON_AddBoolToObject(o, "ticking", s.ticking);
    cJSON_AddBoolToObject(o, "osf_at_boot", s.osf);
    cJSON_AddBoolToObject(o, "time_valid", s.timeValid);
    if (MyRtc::temperatureAvailable()) cJSON_AddNumberToObject(o, "temp_c", (double)s.tempC);
    else cJSON_AddNullToObject(o, "temp_c");
    cJSON_AddNumberToObject(o, "boot_time", (double)s.bootTime);
    cJSON_AddNumberToObject(o, "last_write", (double)s.lastWrite);
    cJSON_AddNumberToObject(o, "writes", s.writes);
    cJSON_AddNumberToObject(o, "errors", s.errors);
    cJSON_AddNumberToObject(o, "drift_s", s.lastDriftS);
}

static void diagOta(cJSON *o)
{
    cJSON_AddStringToObject(o, "running", MyOta::runningPartition());
    cJSON_AddStringToObject(o, "next", MyOta::nextPartition());
    cJSON_AddNumberToObject(o, "slot_size", MyOta::slotSize());
    cJSON_AddBoolToObject(o, "pending_verify", MyOta::pendingVerify());
    cJSON_AddBoolToObject(o, "rolled_back", MyOta::rolledBack());
}

static void diagStorage(cJSON *o)
{
    cJSON_AddBoolToObject(o, "ok", MyFat::mounted());
    cJSON_AddNumberToObject(o, "total", (double)MyFat::totalBytes());
    cJSON_AddNumberToObject(o, "used", (double)MyFat::usedBytes());
    cJSON_AddNumberToObject(o, "players", Players::count());
}

static void diagSd(cJSON *o)
{
    const BoardSd::Info i = BoardSd::info();
    cJSON_AddBoolToObject(o, "ok", i.mounted);
    cJSON_AddNumberToObject(o, "errors", i.errors);
    cJSON_AddStringToObject(o, "error", i.lastError ? esp_err_to_name(i.lastError) : "");
    if (!i.mounted) return;
    cJSON_AddStringToObject(o, "name", i.name);
    cJSON_AddNumberToObject(o, "width", i.width);
    cJSON_AddStringToObject(o, "bus", i.width == 4 ? "4-bit" : "1-bit");
    cJSON_AddNumberToObject(o, "freq_khz", i.freqKhz);
    cJSON_AddNumberToObject(o, "total", (double)i.totalBytes);
    cJSON_AddNumberToObject(o, "free", (double)i.freeBytes);
    cJSON_AddNumberToObject(o, "used", (double)(i.totalBytes - i.freeBytes));
    cJSON_AddNumberToObject(o, "mounts", i.generation);
}

// Boot self-test results, as shown on the device's boot screen
static void diagBoot(cJSON *o)
{
    static const char *const STATE[] = {"pending", "running", "ok", "fail", "skip", "waiting", "warn"};
    cJSON *items = cJSON_AddArrayToObject(o, "items");
    for (int n = 0; n < (int)UiBoot::Item::COUNT; n++) {
        const UiBoot::Item it = (UiBoot::Item)n;
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "name", UiBoot::name(it));
        cJSON_AddStringToObject(e, "state", STATE[(int)UiBoot::state(it)]);
        cJSON_AddStringToObject(e, "detail", UiBoot::detail(it));
        cJSON_AddItemToArray(items, e);
    }
    cJSON_AddNumberToObject(o, "failures", UiBoot::failures());
}

static void diagAudio(cJSON *o)
{
    const Audio::Status s = Audio::status();
    cJSON_AddBoolToObject(o, "ok", s.running);
    cJSON_AddBoolToObject(o, "codec", s.codecFound);
    cJSON_AddBoolToObject(o, "amp", s.ampOn);
    const char *mus = Sound::musicName(s.music);
    cJSON_AddStringToObject(o, "music", mus ? mus : "");
    cJSON_AddBoolToObject(o, "music_file", s.musicFromFile);
    cJSON_AddNumberToObject(o, "channels", s.busyChannels);
    cJSON_AddNumberToObject(o, "files_cached", s.filesCached);
    cJSON_AddNumberToObject(o, "underruns", s.underruns);
    cJSON_AddStringToObject(o, "error", s.error ? s.error : "");
}

static void diagBattery(cJSON *o)
{
    const BoardBattery::Status s = BoardBattery::status();
    cJSON_AddBoolToObject(o, "supported", s.supported);
    cJSON_AddBoolToObject(o, "initialized", s.initialized);
    cJSON_AddBoolToObject(o, "sample_valid", s.sampleValid);
    cJSON_AddBoolToObject(o, "adc_calibrated", s.adcCalibrated);
    cJSON_AddNumberToObject(o, "raw_adc", s.sampleValid ? s.raw : -1);
    cJSON_AddNumberToObject(o, "adc_mv", s.adcCalibrated ? s.adcMillivolts : -1);
    cJSON_AddNumberToObject(o, "scale_permille", Config::get().batteryScalePermille);
    cJSON_AddNumberToObject(o, "full_reference_mv", Config::get().batteryFullMv);
    uint16_t batteryMv = 0;
    const bool voltageValid = s.supported && s.sampleValid && s.adcCalibrated &&
                              BatteryMath::batteryMillivolts(s.adcMillivolts, Config::get().batteryScalePermille,
                                                             batteryMv);
    uint8_t percent = 0;
    const bool percentValid =
        voltageValid && BatteryMath::estimatePercent(batteryMv, Config::get().batteryFullMv, percent);
    cJSON_AddNumberToObject(o, "battery_mv", voltageValid ? batteryMv : -1);
    cJSON_AddNumberToObject(o, "estimated_percent", percentValid ? percent : -1);
    cJSON_AddNumberToObject(o, "errors", s.errors);
}

void registerDiag()
{
    MyDiag::add("wifi", diagWifi);
    MyDiag::add("display", diagDisplay);
    MyDiag::add("touch", diagTouch);
    MyDiag::add("clock", diagClock);
    MyDiag::add("rtc", diagRtc);
    MyDiag::add("ota", diagOta);
    MyDiag::add("storage", diagStorage);
    MyDiag::add("sd", diagSd);
    MyDiag::add("audio", diagAudio);
    MyDiag::add("battery", diagBattery);
    MyDiag::add("boot", diagBoot);
    MyDiag::add("power", [](cJSON *o) {
        const auto s = PowerHistory::status();
        cJSON_AddBoolToObject(o, "enabled", Config::get().powerLog);
        cJSON_AddBoolToObject(o, "running", s.running);
        cJSON_AddBoolToObject(o, "eco_sleeping", UiScreenPower::ecoSleeping());
        cJSON_AddBoolToObject(o, "wifi_suspended", MyWifi::suspended());
        cJSON_AddBoolToObject(o, "audio_sleeping", Audio::sleeping());
        cJSON_AddNumberToObject(o, "written", s.written);
        cJSON_AddNumberToObject(o, "dropped", s.dropped);
        cJSON_AddStringToObject(o, "path", s.path);
    });
}

static esp_err_t handleInfo(httpd_req_t *r)
{
    cJSON *o = cJSON_CreateObject();
    MyDiag::fill(o);
    cJSON_AddStringToObject(o, "lang", Config::get().lang);
    uint8_t addrs[16];
    int n = Touch::scanResults(addrs, 16);
    cJSON *i2c = cJSON_AddArrayToObject(o, "i2c");
    for (int i = 0; i < n; i++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "0x%02X", addrs[i]);
        cJSON_AddItemToArray(i2c, cJSON_CreateString(buf));
    }
    MyWeb::Req q(r);
    if (q.has("tasks")) cJSON_AddItemToObject(o, "task_list", MyDiag::tasks());
    return MyWeb::sendJson(r, 200, o);
}

// POST /api/diag/crash?kind=panic|wdt: deliberate crash to verify reset handling and core dumps.
static esp_err_t handleDiagCrash(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
    char kind[8] = "";
    q.get("kind", kind, sizeof(kind));
    static bool s_wdt;
    if (!strcmp(kind, "panic")) s_wdt = false;
    else if (!strcmp(kind, "wdt")) s_wdt = true;
    else return MyWeb::sendError(r, 400, "kind=panic|wdt");
    MyLog::event("diag", "test crash requested: %s", kind);
    MyWeb::sendOk(r);
    xTaskCreate(
        [](void *) {
            vTaskDelay(pdMS_TO_TICKS(500));   // Let the HTTP reply and log line go out first
            if (s_wdt) {
                portDISABLE_INTERRUPTS();   // Interrupt watchdog panics with reset reason INT_WDT
                for (;;) {}
            }
            abort();
        },
        "testcrash", 3072, nullptr, 1, nullptr);
    return ESP_OK;
}

// ----------------------------------------------------------------------------------------
// /api/config
// ----------------------------------------------------------------------------------------
// Largest text size for this panel (see MyFonts::maxFor); the 320 px panels stop at "large".
static uint8_t maxTextSize()
{
    const HwProfile::Info &hw = HwProfile::active();
    return (uint8_t)MyFonts::maxFor(hw.width < hw.height ? hw.width : hw.height);
}

static cJSON *configJson()
{
    const AppConfig &c = Config::get();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "hostname", c.hostname);
    cJSON_AddNumberToObject(o, "nets_count", MyWifi::savedCount());
    cJSON_AddNumberToObject(o, "nets_max", NET_LIST_MAX);
    cJSON_AddBoolToObject(o, "admin_pin_set", c.adminPin[0] != 0);
    cJSON_AddStringToObject(o, "tz", c.tz);
    cJSON_AddNumberToObject(o, "brightness", c.brightness);
    cJSON_AddNumberToObject(o, "screen_off_seconds", c.screenOffSeconds);
    cJSON_AddBoolToObject(o, "eco_sleep", c.ecoSleep);
    cJSON_AddBoolToObject(o, "power_log", c.powerLog);
    cJSON *zones = cJSON_AddArrayToObject(o, "time_zones");
    for (const auto &zone : TimeZones::ZONES) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "label", zone.label);
        cJSON_AddStringToObject(item, "tz", zone.tz);
        cJSON_AddItemToArray(zones, item);
    }
    cJSON_AddNumberToObject(o, "text_size", c.textSize);
    cJSON_AddNumberToObject(o, "text_size_max", maxTextSize());
    cJSON_AddNumberToObject(o, "led_brightness", c.ledBrightness);
    cJSON_AddBoolToObject(o, "led_enabled", c.ledEnabled);
    cJSON_AddBoolToObject(o, "has_rgb_led", HwProfile::active().hasRgbLed);
    cJSON_AddBoolToObject(o, "has_host_radio", HwProfile::active().hasHostRadio);
    cJSON_AddNumberToObject(o, "default_variant", c.defaultVariant);
    cJSON_AddNumberToObject(o, "roll_anim_ms", c.rollAnimMs);
    cJSON_AddBoolToObject(o, "maxi_saved_rolls", c.maxiSavedRolls);
    cJSON_AddNumberToObject(o, "throw_mode", c.throwMode);
    cJSON_AddNumberToObject(o, "rotation", c.rotation);
    cJSON_AddStringToObject(o, "lang", c.lang);
    cJSON_AddBoolToObject(o, "sound_on", c.soundOn);
    cJSON_AddNumberToObject(o, "sfx_vol", c.sfxVol);
    cJSON_AddBoolToObject(o, "music_on", c.musicOn);
    cJSON_AddNumberToObject(o, "music_vol", c.musicVol);
    cJSON_AddNumberToObject(o, "kara_volume", c.karaVolume);
    cJSON_AddNumberToObject(o, "battery_scale_permille", c.batteryScalePermille);
    cJSON_AddNumberToObject(o, "battery_full_mv", c.batteryFullMv);
    cJSON_AddNumberToObject(o, "joy_zones", c.joyZones);
    cJSON_AddNumberToObject(o, "joy_slow_ms", c.joySlowMs);
    cJSON_AddNumberToObject(o, "joy_fast_ms", c.joyFastMs);
    cJSON_AddNumberToObject(o, "joy_hold_ms", c.joyHoldMs);
    cJSON_AddNumberToObject(o, "joy_diag", c.joyDiag);
    cJSON *langs = cJSON_AddArrayToObject(o, "langs");
    for (int i = 0; i < Lang::count(); i++) {
        cJSON *l = cJSON_CreateObject();
        cJSON_AddStringToObject(l, "code", Lang::code(i));
        cJSON_AddStringToObject(l, "name", Lang::name(i));
        cJSON_AddItemToArray(langs, l);
    }
    return o;
}

static esp_err_t handleConfigGet(httpd_req_t *r) { return MyWeb::sendJson(r, 200, configJson()); }

static esp_err_t handleConfigPost(httpd_req_t *r)
{
    MyWeb::Req q(r);
    AppConfig next = Config::get();
    bool bad = false, hostChanged = false, tzChanged = false;
    long v;
    bool b;
    if (q.getInt("brightness", 5, 100, v, bad)) next.brightness = (uint8_t)v;
    if (q.getInt("screen_off_seconds", 0, 3600, v, bad)) {
        if (!Config::validScreenOffSeconds(static_cast<uint16_t>(v))) bad = true;
        else next.screenOffSeconds = static_cast<uint16_t>(v);
    }
    if (q.getInt("text_size", 0, maxTextSize(), v, bad)) next.textSize = (uint8_t)v;
    if (q.getBool("eco_sleep", b)) next.ecoSleep = b;
    if (q.getBool("power_log", b)) next.powerLog = b;
    if (HwProfile::active().hasRgbLed && q.getInt("led_brightness", 0, 255, v, bad))
        next.ledBrightness = (uint8_t)v;
    if (q.getInt("default_variant", 0, 3, v, bad)) next.defaultVariant = (uint8_t)v;
    if (q.getInt("roll_anim_ms", 300, 2000, v, bad)) next.rollAnimMs = (uint16_t)v;
    if (q.getInt("throw_mode", 0, 1, v, bad)) next.throwMode = (uint8_t)v;
    if (q.getInt("rotation", 0, 180, v, bad)) {
        if (v != 0 && v != 180) bad = true;
        else next.rotation = (uint16_t)v;
    }
    if (q.getInt("sfx_vol", 0, 100, v, bad)) next.sfxVol = (uint8_t)v;
    if (q.getInt("music_vol", 0, 100, v, bad)) next.musicVol = (uint8_t)v;
    if (q.getInt("kara_volume", 0, 100, v, bad)) next.karaVolume = (uint8_t)v;
    if (q.getInt("battery_scale_permille", 0, BatteryMath::SCALE_MAX_PERMILLE, v, bad)) {
        if (!BatteryMath::validScale(static_cast<uint16_t>(v))) bad = true;
        else next.batteryScalePermille = static_cast<uint16_t>(v);
    }
    if (q.getInt("battery_full_mv", BatteryMath::BATTERY_FULL_MIN_MV, BatteryMath::BATTERY_FULL_MAX_MV, v, bad))
        next.batteryFullMv = static_cast<uint16_t>(v);
    bool batteryCalibrationFailed = false;
    if (q.has("battery_reference_mv")) {
        if (q.getInt("battery_reference_mv", BatteryMath::BATTERY_MIN_MV, BatteryMath::BATTERY_MAX_MV, v, bad)) {
            const BoardBattery::Status bs = BoardBattery::status();
            uint16_t scale = 0;
            if (!bs.supported || !bs.sampleValid || !bs.adcCalibrated ||
                !BatteryMath::scaleFromReference(static_cast<int32_t>(v), bs.adcMillivolts, scale))
                batteryCalibrationFailed = true;
            else
                next.batteryScalePermille = scale;
        } else {
            batteryCalibrationFailed = true;
        }
    }
    if (q.getInt("joy_zones", 1, 4, v, bad)) next.joyZones = (uint8_t)v;
    if (q.getInt("joy_slow_ms", JOY_MS_MIN, JOY_MS_MAX, v, bad)) next.joySlowMs = (uint16_t)v;
    if (q.getInt("joy_fast_ms", JOY_MS_MIN, JOY_MS_MAX, v, bad)) next.joyFastMs = (uint16_t)v;
    if (q.getInt("joy_hold_ms", JOY_HOLD_MIN, JOY_HOLD_MAX, v, bad)) next.joyHoldMs = (uint16_t)v;
    if (q.getInt("joy_diag", 0, 1, v, bad)) next.joyDiag = (uint8_t)v;
    if (batteryCalibrationFailed) return MyWeb::sendError(r, 400, tr(Str::BATTERY_CALIBRATION_FAILED));
    if (bad) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_NUMBER));
    if (HwProfile::active().hasRgbLed && q.getBool("led_enabled", b)) next.ledEnabled = b;
    if (q.getBool("sound_on", b)) next.soundOn = b;
    if (q.getBool("music_on", b)) next.musicOn = b;
    if (q.getBool("maxi_saved_rolls", b)) next.maxiSavedRolls = b;
    char buf[64];
    if (q.has("tz")) {
        if (!q.get("tz", buf, sizeof(buf)) || !Config::validTz(buf)) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_TZ));
        if (strcmp(next.tz, buf)) {
            strlcpy(next.tz, buf, sizeof(next.tz));
            tzChanged = true;
        }
    }
    if (q.has("lang")) {
        if (!q.get("lang", buf, sizeof(buf)) || !Lang::valid(buf)) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_LANG));
        strlcpy(next.lang, buf, sizeof(next.lang));
    }
    if (q.has("admin_pin_new")) {
        if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
        if (!q.get("admin_pin_new", buf, sizeof(buf)) || !Config::validAdminPin(buf, true))
            return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_PIN_FORMAT));
        strlcpy(next.adminPin, buf, sizeof(next.adminPin));
    }
    if (q.has("hostname")) {
        if (!q.get("hostname", buf, sizeof(buf)) || !Config::validHostname(buf))
            return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_HOSTNAME));
        if (strcmp(next.hostname, buf)) {
            strlcpy(next.hostname, buf, sizeof(next.hostname));
            hostChanged = true;
        }
    }
    const bool pinChanged = strcmp(next.adminPin, Config::get().adminPin) != 0;
    Config::sanitize(next);
    Config::get() = next;
    if (!Config::save()) return MyWeb::sendError(r, 500, tr(Str::WEB_ERR_NVS));
    if (s_configCb) s_configCb();
    if (hostChanged) MyWifi::requestHostnameUpdate();
    if (tzChanged) MyTime::setTz(next.tz);
    Lang::setLang(next.lang);
    MyLog::event("cfg", "settings saved from web%s%s%s", hostChanged ? ", hostname" : "", tzChanged ? ", tz" : "",
                 pinChanged ? ", admin PIN" : "");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddItemToObject(o, "config", configJson());
    return MyWeb::sendJson(r, 200, o);
}

// ----------------------------------------------------------------------------------------
// Wi-Fi
// ----------------------------------------------------------------------------------------
// ssid + pass: start a connection attempt (saved / moved up only on success).
// keep_pass=1 with an empty pass reuses the stored password of that saved network.
static esp_err_t handleWifiPost(httpd_req_t *r)
{
    MyWeb::Req q(r);
    char ssid[40], pass[72] = "";
    if (!getTrim(q, "ssid", ssid, sizeof(ssid)) || !ssid[0]) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_SSID_MISSING));
    if (strlen(ssid) > 32) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_SSID_LONG));
    if (q.has("pass") && !q.get("pass", pass, sizeof(pass)))
        return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_PASS));
    if (!pass[0] && q.has("keep_pass")) MyWifi::savedPassword(ssid, pass, sizeof(pass));
    esp_err_t e;
    if (!Config::validWifiPass(pass, true)) e = MyWeb::sendError(r, 400, tr(Str::WEB_ERR_PASS));
    else if (!MyWifi::connect(ssid, pass)) e = MyWeb::sendError(r, 500, tr(Str::WEB_ERR_WIFI_OFF));
    else {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", true);
        cJSON_AddBoolToObject(o, "connecting", true);
        cJSON_AddStringToObject(o, "ssid", ssid);
        e = MyWeb::sendJson(r, 200, o);
    }
    memset(pass, 0, sizeof(pass));
    return e;
}

static esp_err_t handleScan(httpd_req_t *r)
{
    MyWeb::Req q(r);
    int st = MyWifi::scanStatus();
    if (q.has("start") || st == -1) {
        MyWifi::startScan();
        return MyWeb::sendJsonStr(r, 202, "{\"scanning\":true,\"networks\":[]}");
    }
    if (st == -2) return MyWeb::sendJsonStr(r, 202, "{\"scanning\":true,\"networks\":[]}");
    static MyWifi::Network nets[MyWifi::MAX_NETWORKS];   // httpd task only
    int n = MyWifi::scanResults(nets, MyWifi::MAX_NETWORKS);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "scanning", false);
    cJSON *arr = cJSON_AddArrayToObject(o, "networks");
    for (int i = 0; i < n; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "ssid", nets[i].ssid);
        cJSON_AddNumberToObject(e, "rssi", nets[i].rssi);
        cJSON_AddNumberToObject(e, "bars", MyWifi::signalBars(nets[i].rssi));
        cJSON_AddNumberToObject(e, "channel", nets[i].channel);
        cJSON_AddBoolToObject(e, "secure", nets[i].secure);
        cJSON_AddItemToArray(arr, e);
    }
    return MyWeb::sendJson(r, 200, o);
}

// Saved networks in priority order (never the passwords).
static esp_err_t sendNets(httpd_req_t *r)
{
    NetList *list = (NetList *)psAlloc(sizeof(NetList));
    if (!list) return MyWeb::sendError(r, 500, tr(Str::WEB_ERR_MEMORY));
    MyWifi::savedCopy(*list);
    MyWifi::Info wi = MyWifi::info();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "max", NET_LIST_MAX);
    cJSON *arr = cJSON_AddArrayToObject(o, "nets");
    for (int i = 0; i < list->count; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "ssid", list->items[i].ssid);
        cJSON_AddBoolToObject(e, "secure", list->items[i].pass[0] != 0);
        cJSON_AddBoolToObject(e, "connected", wi.connected && strcmp(wi.ssid, list->items[i].ssid) == 0);
        cJSON_AddItemToArray(arr, e);
    }
    memset(list, 0, sizeof(NetList));   // Do not leave passwords lying around
    free(list);
    return MyWeb::sendJson(r, 200, o);
}

static esp_err_t handleNetsGet(httpd_req_t *r) { return sendNets(r); }

// action=add (ssid, pass) | remove (ssid) | move (ssid, delta) | forget_all
static esp_err_t handleNetsPost(httpd_req_t *r)
{
    MyWeb::Req q(r);
    char act[16];
    if (!q.get("action", act, sizeof(act))) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_ACTION_MISSING));
    if (strcmp(act, "forget_all") == 0) {
        MyWifi::forgetAll();
        MyLog::event("wifi", "all saved networks forgotten (web)");
        return MyWeb::sendOk(r);
    }
    char ssid[40];
    if (!q.get("ssid", ssid, sizeof(ssid)) || !ssid[0] || strlen(ssid) > 32) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_SSID));
    bool ok = false;
    if (strcmp(act, "add") == 0) {
        char pass[72] = "";
        if (q.has("pass") && !q.get("pass", pass, sizeof(pass)))
            return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_PASS));
        if (!Config::validWifiPass(pass, true)) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_PASS));
        ok = MyWifi::savedAdd(ssid, pass);
        memset(pass, 0, sizeof(pass));
    } else if (strcmp(act, "remove") == 0) {
        ok = MyWifi::savedRemove(ssid);
    } else if (strcmp(act, "move") == 0) {
        long d;
        bool bad = false;
        if (!q.getInt("delta", -NET_LIST_MAX, NET_LIST_MAX, d, bad)) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_DELTA));
        ok = MyWifi::savedMove(ssid, (int)d);
    } else {
        return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_ACTION));
    }
    if (!ok) return MyWeb::sendError(r, 404, tr(Str::WEB_ERR_NET_NOT_FOUND));
    return sendNets(r);
}

// epoch=<UTC seconds>: set the clock manually (e.g. from the browser).
static esp_err_t handleTimePost(httpd_req_t *r)
{
    MyWeb::Req q(r);
    char buf[24];
    if (!q.get("epoch", buf, sizeof(buf))) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_EPOCH));
    if (!MyTime::setManual((time_t)atoll(buf))) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_TIME));
    return MyWeb::sendOk(r);
}

// ----------------------------------------------------------------------------------------
// /api/game - live view of the running game (page /game polls it).
// ?v=<version>: answers {"same":true} while nothing changed.
// ----------------------------------------------------------------------------------------
// Seats for the /play lobby (all games): registry id, PIN set, browser bound.
static void addWebPlayers(cJSON *o, const GameLive::Snapshot &snap)
{
    const GameLive::Snapshot *s = &snap;
    cJSON *wp = cJSON_AddArrayToObject(o, "web_players");
    for (int p = 0; p < GameLive::playerCount(snap) && p < YATZY_MAX_PLAYERS; p++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "player", p);
        cJSON_AddNumberToObject(e, "id", s->pid[p]);
        cJSON_AddStringToObject(e, "name", GameLive::playerName(snap, p));
        cJSON_AddBoolToObject(e, "web", s->web[p]);
        cJSON_AddBoolToObject(e, "has_pin", s->pid[p] && Players::hasPin(s->pid[p]));   // Never the PIN itself
        cJSON_AddBoolToObject(e, "connected", s->conn[p] > 0);
        cJSON_AddItemToArray(wp, e);
    }
}

static const char *gameSessionId(uint32_t session)
{
    static uint32_t cachedSession = 0;
    static char id[17] = {};
    if (!session) return "";
    if (session != cachedSession) {
        const uint32_t high = esp_random();
        uint32_t low = esp_random();
        if (!(high | low)) low = 1;
        snprintf(id, sizeof(id), "%08lx%08lx", (unsigned long)high, (unsigned long)low);
        cachedSession = session;
    }
    return id;
}

template <typename G>
static void addRanking(cJSON *o, const G &g)
{
    int order[YATZY_MAX_PLAYERS], places[YATZY_MAX_PLAYERS];
    int n = g.ranking(order, places);
    cJSON *rk = cJSON_AddArrayToObject(o, "ranking");
    for (int k = 0; k < n; k++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "player", order[k]);
        cJSON_AddNumberToObject(e, "place", places[k]);
        cJSON_AddItemToArray(rk, e);
    }
}

static void addDice(cJSON *o, const uint8_t *d, int n, bool show)
{
    cJSON *dice = cJSON_AddArrayToObject(o, "dice");
    for (int i = 0; i < n; i++) cJSON_AddItemToArray(dice, cJSON_CreateNumber(show ? d[i] : 0));
}

// v0.7.1: while the dice roll the snapshot is the pre-roll state; "roll_mask" says which dice are in
// the air (the web animates those and does not treat them as held).
static void addRollMask(cJSON *o, const GameLive::Snapshot &snap, int n)
{
    if (!snap.rolling) return;
    cJSON *m = cJSON_AddArrayToObject(o, "roll_mask");
    for (int i = 0; i < n; i++) cJSON_AddItemToArray(m, cJSON_CreateBool((snap.rollMask >> i) & 1));
}

static cJSON *addPlayerBase(cJSON *pa, const GameLive::Snapshot &snap, int p, const char *name, int total)
{
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "name", name);
    if (p < YATZY_MAX_PLAYERS && snap.web[p]) cJSON_AddBoolToObject(e, "web", true);
    cJSON_AddNumberToObject(e, "total", total);
    cJSON_AddItemToArray(pa, e);
    return e;
}

// v0.6.0: Pig / Farkle / Ship-Captain-Crew state (see docs/GAME_ARCHITECTURE.md).
static void addOtherGame(cJSON *o, const GameLive::Snapshot &snap)
{
    cJSON_AddBoolToObject(o, "rolling", snap.rolling);
    cJSON_AddNumberToObject(o, "current", GameLive::currentPlayer(snap));
    cJSON_AddNumberToObject(o, "num_dice", GameLive::diceCount(snap));
    addRollMask(o, snap, GameLive::diceCount(snap));
    cJSON_AddBoolToObject(o, "finished", GameLive::finished(snap));
    addWebPlayers(o, snap);
    switch (snap.gameType) {
    case GameType::PIG: {
        const PigGame &g = snap.pig;
        const bool two = g.variant() == PigVariant::TWO_DICE;
        cJSON_AddStringToObject(o, "game_type", "pig");
        cJSON_AddStringToObject(o, "pig_variant", two ? "two_dice" : "one_die");
        cJSON_AddStringToObject(o, "name", tr(two ? Str::VAR_PIG_2 : Str::VAR_PIG_1));
        cJSON_AddNumberToObject(o, "target", PIG_WIN_SCORE);
        addDice(o, g.dice(), g.numDice(), g.hasRolled());
        cJSON_AddBoolToObject(o, "has_rolled", g.hasRolled());
        cJSON_AddNumberToObject(o, "turn_total", g.turnTotal());
        cJSON_AddBoolToObject(o, "busted", g.hasBusted());
        cJSON_AddBoolToObject(o, "lost_all", g.lostAll());
        cJSON_AddNumberToObject(o, "round", g.round());
        cJSON_AddBoolToObject(o, "can_roll", g.canRoll());
        cJSON_AddBoolToObject(o, "can_bank", g.canBank());
        cJSON *pa = cJSON_AddArrayToObject(o, "players");
        for (int p = 0; p < g.playerCount(); p++) addPlayerBase(pa, snap, p, g.player(p).name, g.total(p));
        if (g.finished()) addRanking(o, g);
        break;
    }
    case GameType::FARKLE: {
        const FarkleGame &g = snap.farkle;
        cJSON_AddStringToObject(o, "game_type", "farkle");
        cJSON_AddStringToObject(o, "name", tr(Str::VAR_FARKLE));
        cJSON_AddNumberToObject(o, "target", FARKLE_WIN_SCORE);
        cJSON_AddNumberToObject(o, "open_threshold", FARKLE_OPEN_THRESHOLD);
        bool show[FARKLE_DICE];
        cJSON *dice = cJSON_AddArrayToObject(o, "dice");
        cJSON *held = cJSON_AddArrayToObject(o, "held");
        cJSON *locked = cJSON_AddArrayToObject(o, "locked");
        for (int i = 0; i < FARKLE_DICE; i++) {
            show[i] = g.hasRolled() || g.locked(i);
            cJSON_AddItemToArray(dice, cJSON_CreateNumber(show[i] ? g.dice()[i] : 0));
            cJSON_AddItemToArray(held, cJSON_CreateBool(g.held(i)));
            cJSON_AddItemToArray(locked, cJSON_CreateBool(g.locked(i)));
        }
        cJSON_AddBoolToObject(o, "has_rolled", g.hasRolled());
        cJSON_AddNumberToObject(o, "turn_total", g.turnTotal());
        cJSON_AddNumberToObject(o, "held_score", g.heldScore());
        cJSON_AddBoolToObject(o, "held_valid", g.heldValid());
        cJSON_AddBoolToObject(o, "farkled", g.hasFarkled());
        cJSON_AddBoolToObject(o, "hot_dice", g.hotDice());
        cJSON_AddBoolToObject(o, "final_round", g.finalRound());
        cJSON_AddNumberToObject(o, "round", g.round());
        cJSON_AddBoolToObject(o, "can_roll", g.canRoll());
        cJSON_AddBoolToObject(o, "can_bank", g.canBank());
        cJSON *pa = cJSON_AddArrayToObject(o, "players");
        for (int p = 0; p < g.playerCount(); p++) {
            cJSON *e = addPlayerBase(pa, snap, p, g.player(p).name, g.player(p).total);
            cJSON_AddBoolToObject(e, "opened", g.player(p).opened);
        }
        if (g.finished()) addRanking(o, g);
        break;
    }
    default: {
        const SccGame &g = snap.scc;
        cJSON_AddStringToObject(o, "game_type", "scc");
        cJSON_AddStringToObject(o, "name", tr(Str::VAR_SCC_3));
        addDice(o, g.dice(), SCC_DICE, g.hasRolled());
        cJSON *held = cJSON_AddArrayToObject(o, "held");
        cJSON *role = cJSON_AddArrayToObject(o, "role");   // 0 cargo, 1 ship, 2 captain, 3 crew
        for (int i = 0; i < SCC_DICE; i++) {
            cJSON_AddItemToArray(held, cJSON_CreateBool(g.held(i)));
            cJSON_AddItemToArray(role, cJSON_CreateNumber((int)g.role(i)));
        }
        cJSON_AddBoolToObject(o, "has_rolled", g.hasRolled());
        cJSON_AddNumberToObject(o, "rolls_left", g.rollsLeft());
        cJSON_AddBoolToObject(o, "ship", g.hasShip());
        cJSON_AddBoolToObject(o, "captain", g.hasCaptain());
        cJSON_AddBoolToObject(o, "crew", g.hasCrew());
        cJSON_AddNumberToObject(o, "cargo", g.cargoScore());
        cJSON_AddNumberToObject(o, "round", g.round() > g.totalRounds() ? g.totalRounds() : g.round());
        cJSON_AddNumberToObject(o, "rounds", g.totalRounds());
        cJSON_AddBoolToObject(o, "can_roll", g.canRoll());
        cJSON_AddBoolToObject(o, "can_end", g.canEnd());
        cJSON *pa = cJSON_AddArrayToObject(o, "players");
        for (int p = 0; p < g.playerCount(); p++) {
            const SccPlayer &pl = g.player(p);
            cJSON *e = addPlayerBase(pa, snap, p, pl.name, pl.total);
            cJSON *sc = cJSON_AddArrayToObject(e, "scores");
            for (int k = 0; k < pl.scoreCount; k++) cJSON_AddItemToArray(sc, cJSON_CreateNumber(pl.scores[k]));
        }
        if (g.finished()) addRanking(o, g);
        break;
    }
    }
}

// Builds the live-view JSON from a snapshot (shared by /api/game and the WebSocket broadcast).
static cJSON *gameJson(const GameLive::Snapshot &snap)
{
    const GameLive::Snapshot *s = &snap;
    const YatzyGame &g = s->game;
    const bool hasGame = s->active && GameLive::started(snap) && GameLive::playerCount(snap) > 0;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "version", s->version);
    cJSON_AddBoolToObject(o, "active", hasGame);
    cJSON_AddStringToObject(o, "session_id", hasGame ? gameSessionId(s->session) : "");
    cJSON_AddStringToObject(o, "lang", Lang::currentLang());
    if (hasGame && s->gameType != GameType::YATZY) {
        addOtherGame(o, snap);
        return o;
    }
    if (hasGame) {
        cJSON_AddStringToObject(o, "game_type", "yatzy");
        const YatzyVariant var = g.variant();
        cJSON_AddBoolToObject(o, "rolling", s->rolling);
        cJSON_AddBoolToObject(o, "shaking", s->shaking);
        addRollMask(o, snap, g.diceCount());
        cJSON_AddBoolToObject(o, "choosing", s->resultPhase);
        cJSON_AddBoolToObject(o, "finished", g.finished());
        cJSON_AddStringToObject(o, "variant", var == YatzyVariant::MAXI ? "maxi" : "standard");
        cJSON_AddStringToObject(o, "name", trVariant(var, g.mode(), g.savedRollsRule()));
        cJSON_AddNumberToObject(o, "round", g.round());
        cJSON_AddNumberToObject(o, "rounds", g.totalRounds());
        cJSON_AddNumberToObject(o, "current", g.currentPlayer());
        cJSON_AddNumberToObject(o, "rolls_used", g.rollsUsed());
        cJSON_AddNumberToObject(o, "rolls_left", g.rollsLeft());
        cJSON_AddBoolToObject(o, "can_roll", g.canRoll());
        cJSON_AddBoolToObject(o, "has_rolled", g.hasRolled());
        cJSON_AddNumberToObject(o, "bonus_threshold", Yatzy::bonusThreshold(var));
        cJSON_AddNumberToObject(o, "bonus_points", Yatzy::bonusPoints(var));
        addWebPlayers(o, snap);
        cJSON *dice = cJSON_AddArrayToObject(o, "dice");
        cJSON *held = cJSON_AddArrayToObject(o, "held");
        for (int i = 0; i < g.diceCount(); i++) {
            cJSON_AddItemToArray(dice, cJSON_CreateNumber(g.hasRolled() ? g.die(i) : 0));
            cJSON_AddItemToArray(held, cJSON_CreateBool(g.held(i)));
        }
        int nc = 0;
        const YatzyCategory *cats = Yatzy::categories(var, nc);
        cJSON *ca = cJSON_AddArrayToObject(o, "categories");
        for (int k = 0; k < nc; k++) {
            cJSON *c = cJSON_CreateObject();
            cJSON_AddNumberToObject(c, "id", (int)cats[k]);
            cJSON_AddStringToObject(c, "name", trCategory(cats[k], var));
            cJSON_AddBoolToObject(c, "upper", Yatzy::isUpper(cats[k]));
            // Points this box would give with the current dice (only meaningful after a roll)
            if (g.hasRolled() && !g.finished() && g.canScore(cats[k]))
                cJSON_AddNumberToObject(c, "potential", g.potentialScore(cats[k]));
            cJSON_AddItemToArray(ca, c);
        }
        cJSON *pa = cJSON_AddArrayToObject(o, "players");
        for (int p = 0; p < g.playerCount(); p++) {
            const YatzyPlayer &pl = g.player(p);
            cJSON *e = cJSON_CreateObject();
            cJSON_AddStringToObject(e, "name", pl.name);
            if (p < YATZY_MAX_PLAYERS && s->web[p]) cJSON_AddBoolToObject(e, "web", true);
            cJSON *sc = cJSON_AddArrayToObject(e, "scores");   // Same order as "categories"; null = empty
            for (int k = 0; k < nc; k++) {
                int16_t v2 = pl.scores[(int)cats[k]];
                cJSON_AddItemToArray(sc, v2 == YATZY_EMPTY ? cJSON_CreateNull() : cJSON_CreateNumber(v2));
            }
            cJSON_AddNumberToObject(e, "upper", g.upperSum(p));
            cJSON_AddNumberToObject(e, "bonus", g.bonus(p));
            cJSON_AddNumberToObject(e, "total", g.total(p));
            if (var == YatzyVariant::MAXI || g.savedRollsRule()) cJSON_AddNumberToObject(e, "saved_rolls", pl.savedRolls);
            cJSON_AddItemToArray(pa, e);
        }
        if (g.finished()) {
            int order[YATZY_MAX_PLAYERS], places[YATZY_MAX_PLAYERS];
            int n = g.ranking(order, places);
            cJSON *rk = cJSON_AddArrayToObject(o, "ranking");
            for (int k = 0; k < n; k++) {
                cJSON *e = cJSON_CreateObject();
                cJSON_AddNumberToObject(e, "player", order[k]);
                cJSON_AddNumberToObject(e, "place", places[k]);
                cJSON_AddItemToArray(rk, e);
            }
        }
    }
    return o;
}

static esp_err_t handleGame(httpd_req_t *r)
{
    MyWeb::Req q(r);
    long v = -1;
    bool bad = false;
    q.getInt("v", 0, 0x7FFFFFFF, v, bad);
    if (v >= 0 && (uint32_t)v == GameLive::version()) return MyWeb::sendJsonStr(r, 200, "{\"same\":true}");
    GameLive::Snapshot *s = (GameLive::Snapshot *)psAlloc(sizeof(GameLive::Snapshot));
    if (!s) return MyWeb::sendError(r, 500, tr(Str::WEB_ERR_MEMORY));
    GameLive::get(*s);
    cJSON *o = gameJson(*s);
    free(s);
    return MyWeb::sendJson(r, 200, o);
}

// ----------------------------------------------------------------------------------------
// /api/adventure - v0.11.1 live view of the adventure (page /adventure polls it, read only).
//   ?v=<version>   answers {"same":true} while nothing changed
//   ?mr=<map rev>  the map revision the page has cached: the map is only sent when it differs
//   ?brief=1       only {"active":..,"version":..,"world":..} (the /game page checks this)
// Format: see adv_live.h.
// ----------------------------------------------------------------------------------------
static esp_err_t handleAdventure(httpd_req_t *r)
{
    MyWeb::Req q(r);
    long v = -1, mr = 0;
    bool bad = false, brief = false;
    q.getInt("v", 0, 0x7FFFFFFF, v, bad);
    q.getInt("mr", 0, 0x7FFFFFFF, mr, bad);
    q.getBool("brief", brief);
    const uint32_t ver = AdvLive::version();
    if (v >= 0 && (uint32_t)v == ver) return MyWeb::sendJsonStr(r, 200, "{\"same\":true}");
    if (brief) {
        char small[160];
        if (!AdvLive::writeBriefJson(small, sizeof(small))) return MyWeb::sendError(r, 500, tr(Str::WEB_ERR_MEMORY));
        return MyWeb::sendJsonStr(r, 200, small);
    }
    char *buf = (char *)psAlloc(AdvLive::JSON_CAP);
    if (!buf) return MyWeb::sendError(r, 500, tr(Str::WEB_ERR_MEMORY));
    const size_t n = AdvLive::writeJson((uint32_t)mr, buf, AdvLive::JSON_CAP);
    const esp_err_t err = n ? MyWeb::sendJsonStr(r, 200, buf) : MyWeb::sendError(r, 500, tr(Str::WEB_ERR_MEMORY));
    free(buf);
    return err;
}

// ----------------------------------------------------------------------------------------
// /ws/game - interactive web play (page /play).
//
// Client -> device (text frames, JSON):
//   {"cmd":"join","player":2,"pin":"1234"}  bind to seat 2 (a web seat) with that player's personal PIN
//   {"cmd":"join","player":2,"pin":"1234","session_id":"..."}  also require the same game session
//   {"cmd":"join"}                           watch only (spectator, no PIN)
//   {"cmd":"hold","die":N}                   toggle hold of die N (0-based)
//   {"cmd":"roll"}                           roll the free dice (the console animates the throw)
//   {"cmd":"choose","cat":ID}                write the box with category id ID (see "categories"), Yatzy
//   {"cmd":"bank"}                           v0.6.0 Pig / Farkle: bank the turn total
//   {"cmd":"end"}                            v0.6.0 Ship/Captain/Crew: save the cargo, end the turn
// State frames carry "game_type" ("yatzy" | "pig" | "farkle" | "scc") and game specific fields.
// Device -> client:
//   {"type":"joined","you":seat|-1,"session_id":"..."}
//   {"type":"error","error":"nogame|seat|seatbusy|session|pin|nopin|locked|join|full|cmd|turn","msg":"...","retry_s":n}
//     pin: wrong PIN; nopin: the player has no PIN (set it on the console or /players);
//     locked: too many wrong PINs, retry after retry_s seconds; session/seatbusy: join fresh.
// v0.5.6: the shared 4-digit game code is gone. Bindings carry GameLive's internal session id;
// a new game (or leaving the game) drops every client back to the lobby.
//   {"type":"state","you":seat|-1,"joined":bool, ...same fields as /api/game...}  on every change
//
// All client bookkeeping and every send run in the httpd task (handler + httpd_queue_work), so
// the client table needs no lock. A small poll task only watches the GameLive version and
// queues the broadcast. Commands go to the LVGL task through GameLive's command queue.
// ----------------------------------------------------------------------------------------
static constexpr int WS_MAX_CLIENTS = 4;
static constexpr size_t WS_MAX_FRAME = 256;

static constexpr int WS_PIN_TRIES = 5;             // Wrong PINs per connection before a lockout
static constexpr int WS_SEAT_TRIES = 10;           // Wrong PINs per seat (all connections) per game
static constexpr int64_t WS_LOCK_MS = 60 * 1000;

struct WsClient {
    int fd;              // -1 = free slot
    bool joined;
    int8_t seat;         // -1 = spectator
    uint32_t session;    // GameLive session at join (a new game invalidates the binding)
    bool fresh;          // Needs a state frame even if the version did not change
    uint8_t pinFails;    // Wrong PINs on this connection
    int64_t lockUntil;   // ms (esp_timer) until which joins are refused
};

static constexpr WsClient WS_FREE = {-1, false, -1, 0, false, 0, 0};
static WsClient s_ws[WS_MAX_CLIENTS] = {WS_FREE, WS_FREE, WS_FREE, WS_FREE};
// Guessing protection across reconnects: per seat, reset when a new game starts.
static uint8_t s_seatFails[YATZY_MAX_PLAYERS] = {};
static int64_t s_seatLockUntil[YATZY_MAX_PLAYERS] = {};
static uint32_t s_seatFailSession = 0;

static int64_t nowMs() { return esp_timer_get_time() / 1000; }
static volatile int s_wsCount = 0;
static volatile bool s_wsWorkQueued = false;
static uint32_t s_wsSentVersion = 0;
static GameLive::Snapshot *s_wsSnap = nullptr;   // PSRAM, httpd task only

static void wsSendText(int fd, const char *txt)
{
    httpd_ws_frame_t f = {};
    f.type = HTTPD_WS_TYPE_TEXT;
    f.payload = (uint8_t *)txt;
    f.len = strlen(txt);
    f.final = true;
    httpd_ws_send_frame_async(MyWeb::handle(), fd, &f);
}

static void wsSendError(int fd, const char *code, Str msg, int retryS = 0)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "error");
    cJSON_AddStringToObject(o, "error", code);
    if (retryS > 0) {
        char m[96];
        snprintf(m, sizeof(m), tr(msg), retryS);
        cJSON_AddStringToObject(o, "msg", m);
        cJSON_AddNumberToObject(o, "retry_s", retryS);
    } else {
        cJSON_AddStringToObject(o, "msg", tr(msg));
    }
    char *txt = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (txt) {
        wsSendText(fd, txt);
        cJSON_free(txt);
    }
}

static WsClient *wsFind(int fd)
{
    for (WsClient &c : s_ws)
        if (c.fd == fd) return &c;
    return nullptr;
}

static void wsPublishConnections()
{
    uint8_t conn[YATZY_MAX_PLAYERS] = {};
    int n = 0;
    for (const WsClient &c : s_ws) {
        if (c.fd < 0) continue;
        n++;
        if (c.joined && c.seat >= 0 && c.seat < YATZY_MAX_PLAYERS) conn[c.seat]++;
    }
    s_wsCount = n;
    GameLive::setConnections(conn, YATZY_MAX_PLAYERS);
}

// httpd task only: free slots whose socket is no longer an open WebSocket. Returns true if any was freed.
static bool wsReapDead(httpd_handle_t hd)
{
    bool changed = false;
    for (WsClient &c : s_ws) {
        if (c.fd >= 0 && httpd_ws_get_fd_info(hd, c.fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
            c = WS_FREE;
            changed = true;
        }
    }
    return changed;
}

// httpd task: drop dead sockets, invalidate stale bindings, send state frames.
static void wsBroadcastWork(void *)
{
    s_wsWorkQueued = false;
    httpd_handle_t hd = MyWeb::handle();
    if (!s_wsSnap) s_wsSnap = (GameLive::Snapshot *)psAlloc(sizeof(GameLive::Snapshot));
    if (!s_wsSnap || !hd) return;
    bool changed = wsReapDead(hd);
    GameLive::get(*s_wsSnap);
    for (WsClient &c : s_ws) {
        const bool live = s_wsSnap->active && GameLive::started(*s_wsSnap);
        if (c.fd >= 0 && c.joined && (!live || c.session != s_wsSnap->session)) {   // Game left / new game
            c.joined = false;
            c.seat = -1;
            c.fresh = true;
            changed = true;
        }
    }
    if (changed) {
        wsPublishConnections();
        GameLive::get(*s_wsSnap);
    }
    const bool newVersion = s_wsSnap->version != s_wsSentVersion;
    bool any = false;
    for (const WsClient &c : s_ws) any |= c.fd >= 0 && (newVersion || c.fresh);
    if (!any) return;
    s_wsSentVersion = s_wsSnap->version;
    cJSON *o = gameJson(*s_wsSnap);
    char *base = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!base || base[0] != '{') {
        if (base) cJSON_free(base);
        return;
    }
    const size_t cap = strlen(base) + 64;
    char *msg = (char *)psAlloc(cap);
    if (msg) {
        for (WsClient &c : s_ws) {
            if (c.fd < 0 || !(newVersion || c.fresh)) continue;
            c.fresh = false;
            // {"type":"state","you":n,"joined":b, + the rest of the game object
            snprintf(msg, cap, "{\"type\":\"state\",\"you\":%d,\"joined\":%s,%s", c.joined ? c.seat : -1,
                     c.joined ? "true" : "false", base + 1);
            wsSendText(c.fd, msg);
        }
        free(msg);
    }
    cJSON_free(base);
}

static void wsQueueBroadcast()
{
    if (s_wsWorkQueued || !MyWeb::handle()) return;
    s_wsWorkQueued = true;
    if (httpd_queue_work(MyWeb::handle(), wsBroadcastWork, nullptr) != ESP_OK) s_wsWorkQueued = false;
}

static void wsPollTask(void *)
{
    uint32_t last = 0, idle = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (s_wsCount <= 0) continue;
        uint32_t v = GameLive::version();
        if (v != last || ++idle >= 20) {   // On change, and once a second to reap dead sockets
            last = v;
            idle = 0;
            wsQueueBroadcast();
        }
    }
}

static void wsHandleText(int fd, const char *txt)
{
    WsClient *c = wsFind(fd);
    if (!c) return;
    cJSON *j = cJSON_Parse(txt);
    if (!j) return wsSendError(fd, "cmd", Str::WEB_ERR_WS_CMD);
    const cJSON *jc = cJSON_GetObjectItem(j, "cmd");
    const char *cmd = cJSON_IsString(jc) ? jc->valuestring : "";
    GameLive::Snapshot *s = s_wsSnap ? s_wsSnap : (s_wsSnap = (GameLive::Snapshot *)psAlloc(sizeof(GameLive::Snapshot)));
    if (!s) {
        cJSON_Delete(j);
        return;
    }
    GameLive::get(*s);
    if (strcmp(cmd, "join") == 0) {
        const cJSON *jp = cJSON_GetObjectItem(j, "player");
        const cJSON *jpin = cJSON_GetObjectItem(j, "pin");
        const cJSON *jsession = cJSON_GetObjectItem(j, "session_id");
        const char *pin = cJSON_IsString(jpin) ? jpin->valuestring : "";
        const char *expectedSession = cJSON_IsString(jsession) ? jsession->valuestring : "";
        const bool hasGame = s->active && GameLive::started(*s);
        const bool validateSession = jsession != nullptr;
        const int64_t now = nowMs();
        if (s_seatFailSession != s->session) {   // New game: forget old per-seat failures
            s_seatFailSession = s->session;
            memset(s_seatFails, 0, sizeof(s_seatFails));
            memset(s_seatLockUntil, 0, sizeof(s_seatLockUntil));
        }
        int seat = -1;
        if (cJSON_IsNumber(jp)) seat = jp->valueint;
        if (!hasGame) {
            wsSendError(fd, "nogame", Str::WEB_ERR_WS_NOGAME);
            seat = -2;
        } else if (validateSession &&
                   !MultiplayerProtocol::sameSession(expectedSession, gameSessionId(s->session))) {
            wsSendError(fd, "session", Str::MULTI_SESSION_CHANGED);
            seat = -2;
        } else if (seat != -1 && (seat < 0 || seat >= GameLive::playerCount(*s) || seat >= YATZY_MAX_PLAYERS)) {
            wsSendError(fd, "seat", Str::WEB_ERR_WS_SEAT);
            seat = -2;
        } else if (seat >= 0) {
            const uint16_t pid = s->pid[seat];
            const int64_t until = c->lockUntil > s_seatLockUntil[seat] ? c->lockUntil : s_seatLockUntil[seat];
            if (validateSession && (!s->web[seat] || s->conn[seat] > 0)) {
                wsSendError(fd, "seatbusy", Str::MULTI_SEAT_TAKEN);
                seat = -2;
            } else if (until > now) {
                wsSendError(fd, "locked", Str::WEB_ERR_WS_LOCKED, (int)((until - now + 999) / 1000));
                seat = -2;
            } else if (!pid || !Players::hasPin(pid)) {
                wsSendError(fd, "nopin", Str::WEB_ERR_WS_NOPIN);
                seat = -2;
            } else if (!Players::checkPin(pid, pin)) {
                if (++c->pinFails >= WS_PIN_TRIES) {
                    c->pinFails = 0;
                    c->lockUntil = now + WS_LOCK_MS;
                }
                if (++s_seatFails[seat] >= WS_SEAT_TRIES) {
                    s_seatFails[seat] = 0;
                    s_seatLockUntil[seat] = now + WS_LOCK_MS;
                }
                MyLog::event("web", "wrong PIN for seat %d", seat);
                wsSendError(fd, "pin", Str::WEB_ERR_WS_PIN);
                seat = -2;
            } else {
                c->pinFails = 0;
            }
        }
        if (seat >= -1) {
            c->joined = true;
            c->seat = (int8_t)seat;
            c->session = s->session;
            c->fresh = true;
            char buf[112];
            snprintf(buf, sizeof(buf), "{\"type\":\"joined\",\"you\":%d,\"session_id\":\"%s\"}",
                     seat, gameSessionId(s->session));
            wsSendText(fd, buf);
            wsPublishConnections();
            wsQueueBroadcast();
        }
    } else if (strcmp(cmd, "leave") == 0) {
        // Give the seat back (e.g. "continue on device"): stay connected as a spectator.
        // v0.7.2: also release a seat that was pre-selected as web-played on the device, so the
        // console stops waiting for the web and continues locally (same session, no rebinding).
        if (c->joined && c->seat >= 0 && c->session == s->session) GameLive::releaseWeb(c->seat);
        c->joined = true;
        c->seat = -1;
        c->session = 0;
        c->fresh = true;
        wsPublishConnections();
        wsQueueBroadcast();
        char buf[96];
        const bool activeGame = s->active && GameLive::started(*s);
        snprintf(buf, sizeof(buf), "{\"type\":\"joined\",\"you\":-1,\"session_id\":\"%s\"}",
                 activeGame ? gameSessionId(s->session) : "");
        wsSendText(fd, buf);
    } else if (strcmp(cmd, "setpin") == 0) {
        // First-time PIN from the browser: only allowed while the player has no PIN yet
        const cJSON *jp = cJSON_GetObjectItem(j, "player");
        const cJSON *jpin = cJSON_GetObjectItem(j, "pin");
        const char *pin = cJSON_IsString(jpin) ? jpin->valuestring : "";
        const int seat = cJSON_IsNumber(jp) ? jp->valueint : -1;
        const bool hasGame = s->active && GameLive::started(*s);
        bool pinOk = strlen(pin) == 4;
        for (int i = 0; pinOk && i < 4; i++) pinOk = pin[i] >= '0' && pin[i] <= '9';
        if (!hasGame) {
            wsSendError(fd, "nogame", Str::WEB_ERR_WS_NOGAME);
        } else if (seat < 0 || seat >= GameLive::playerCount(*s) || seat >= YATZY_MAX_PLAYERS || !s->pid[seat]) {
            wsSendError(fd, "seat", Str::WEB_ERR_WS_SEAT);
        } else if (Players::hasPin(s->pid[seat])) {
            wsSendError(fd, "pinexists", Str::WEB_ERR_WS_PINEXISTS);
        } else if (!pinOk) {
            wsSendError(fd, "badpin", Str::WEB_ERR_WS_BADPIN);
        } else if (Players::setPin(s->pid[seat], pin) != 0) {
            wsSendError(fd, "badpin", Str::WEB_ERR_WS_SETPIN);
        } else {
            MyLog::event("web", "PIN created for seat %d", seat);
            c->joined = true;
            c->seat = (int8_t)seat;
            c->session = s->session;
            c->fresh = true;
            c->pinFails = 0;
            char buf[112];
            snprintf(buf, sizeof(buf), "{\"type\":\"joined\",\"you\":%d,\"session_id\":\"%s\"}",
                     seat, gameSessionId(s->session));
            wsSendText(fd, buf);
            wsPublishConnections();
            wsQueueBroadcast();
        }
    } else if (strcmp(cmd, "hold") == 0 || strcmp(cmd, "roll") == 0 || strcmp(cmd, "choose") == 0 ||
               strcmp(cmd, "bank") == 0 || strcmp(cmd, "end") == 0) {
        if (!c->joined || c->seat < 0 || c->session != s->session || !s->active) {
            wsSendError(fd, "join", Str::WEB_ERR_WS_JOIN);
        } else if (GameLive::currentPlayer(*s) != c->seat || GameLive::finished(*s)) {
            wsSendError(fd, "turn", Str::WEB_ERR_WS_TURN);
        } else {
            GameLive::Cmd gc = {GameLive::CmdType::ROLL, 0, c->seat};
            bool ok = true;
            if (cmd[0] == 'h') {
                const cJSON *jd = cJSON_GetObjectItem(j, "die");
                ok = cJSON_IsNumber(jd) && jd->valueint >= 0 && jd->valueint < GameLive::diceCount(*s);
                gc.type = GameLive::CmdType::HOLD;
                gc.arg = ok ? (int8_t)jd->valueint : 0;
            } else if (cmd[0] == 'b') {
                ok = s->gameType == GameType::PIG || s->gameType == GameType::FARKLE;
                gc.type = GameLive::CmdType::BANK;
            } else if (cmd[0] == 'e') {
                ok = s->gameType == GameType::SCC;
                gc.type = GameLive::CmdType::END_TURN;
            } else if (cmd[0] == 'c') {
                ok = s->gameType == GameType::YATZY;
                const cJSON *jd = cJSON_GetObjectItem(j, "cat");
                ok = ok && cJSON_IsNumber(jd) && jd->valueint >= 0 && jd->valueint < (int)YatzyCategory::COUNT;
                gc.type = GameLive::CmdType::CHOOSE;
                gc.arg = ok ? (int8_t)jd->valueint : 0;
            }
            if (!ok || !GameLive::pushCmd(gc)) wsSendError(fd, "cmd", Str::WEB_ERR_WS_CMD);
        }
    } else {
        wsSendError(fd, "cmd", Str::WEB_ERR_WS_CMD);
    }
    cJSON_Delete(j);
}

// Binds a socket to a client slot. nullptr (and the socket is closed) when all slots are busy.
static WsClient *wsRegister(httpd_handle_t hd, int fd)
{
    WsClient *c = wsFind(fd);
    if (c) return c;
    wsReapDead(hd);   // Sweep ghost sockets (e.g. backgrounded phones) before giving up
    c = wsFind(-1);
    if (!c) {
        wsSendError(fd, "full", Str::WEB_ERR_WS_FULL);
        httpd_sess_trigger_close(hd, fd);
        return nullptr;
    }
    *c = WS_FREE;
    c->fd = fd;
    c->fresh = true;
    wsPublishConnections();
    wsQueueBroadcast();
    return c;
}

static esp_err_t wsHandler(httpd_req_t *r)
{
    const int fd = httpd_req_to_sockfd(r);
    // ESP-IDF 6.x answers the handshake itself and does NOT call the handler for it (older
    // releases did, with method GET). Data frames arrive with method 0, so clients are
    // registered lazily on their first frame below; the GET branch is kept for old IDFs.
    if (r->method == HTTP_GET) {
        wsRegister(r->handle, fd);
        return ESP_OK;
    }
    httpd_ws_frame_t f = {};
    f.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t e = httpd_ws_recv_frame(r, &f, 0);
    if (e != ESP_OK) return e;
    if (f.type != HTTPD_WS_TYPE_TEXT || f.len == 0 || f.len > WS_MAX_FRAME) {
        if (f.len > WS_MAX_FRAME) return ESP_FAIL;   // Oversized: drop the connection
        return ESP_OK;
    }
    char buf[WS_MAX_FRAME + 1];
    f.payload = (uint8_t *)buf;
    e = httpd_ws_recv_frame(r, &f, f.len);
    if (e != ESP_OK) return e;
    buf[f.len] = 0;
    if (!wsRegister(r->handle, fd)) return ESP_OK;   // Full: the socket is being closed
    wsHandleText(fd, buf);
    return ESP_OK;
}

static bool wsBegin()
{
    httpd_uri_t u = {};
    u.uri = "/ws/game";
    u.method = HTTP_GET;
    u.handler = wsHandler;
    u.user_ctx = nullptr;
    u.is_websocket = true;
    if (httpd_register_uri_handler(MyWeb::handle(), &u) != ESP_OK) return false;
    return xTaskCreatePinnedToCore(wsPollTask, "ws_poll", 3072, nullptr, 3, nullptr, 0) == pdPASS;
}

// ----------------------------------------------------------------------------------------
// /api/players
// ----------------------------------------------------------------------------------------
// {"ok":true,"count":n,"max":200,"fs_ok":b,"players":[{"id","name","created","has_pin"}...]} sorted
// by name. The PIN itself is never sent.
static esp_err_t sendPlayers(httpd_req_t *r)
{
    PlayerDb *db = (PlayerDb *)psAlloc(sizeof(PlayerDb));
    if (!db) return MyWeb::sendError(r, 500, tr(Str::WEB_ERR_MEMORY));
    Players::snapshot(*db);
    uint8_t idx[PLAYER_MAX];
    int n = db->sortedIndices(idx);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddNumberToObject(o, "count", n);
    cJSON_AddNumberToObject(o, "max", PLAYER_MAX);
    cJSON_AddBoolToObject(o, "fs_ok", Players::ok());
    cJSON *arr = cJSON_AddArrayToObject(o, "players");
    for (int k = 0; k < n; k++) {
        const PlayerRec &p = db->items[idx[k]];
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "id", p.id);
        cJSON_AddStringToObject(e, "name", p.name);
        cJSON_AddNumberToObject(e, "created", p.created);
        cJSON_AddBoolToObject(e, "has_pin", p.pin[0] != 0);
        cJSON_AddItemToArray(arr, e);
    }
    free(db);
    return MyWeb::sendJson(r, 200, o);
}

static esp_err_t handlePlayersGet(httpd_req_t *r) { return sendPlayers(r); }

// action=add (name[, player_pin]) | rename (id, name[, player_pin]) | setpin (id, player_pin; ""
// clears) | remove (id; admin PIN). Answers with the new list. The player's PIN uses the field
// "player_pin" because "pin" is the admin PIN (MyWeb::Req::adminAllowed). Setting a PIN on a
// player without one is free; changing or clearing an existing PIN needs the admin PIN (if set).
static esp_err_t handlePlayersPost(httpd_req_t *r)
{
    MyWeb::Req q(r);
    char act[16], name[PLAYER_NAME_CAP * 2], pin[16] = "";
    if (!q.get("action", act, sizeof(act))) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_ACTION_MISSING));
    const bool hasName = q.get("name", name, sizeof(name));
    const bool hasPin = q.has("player_pin");
    if (hasPin && !q.get("player_pin", pin, sizeof(pin))) pin[0] = 0;
    if (hasPin && !PlayerDb::validPin(pin, true)) return MyWeb::sendError(r, 400, trPlayerErr((int)PlayerErr::BAD_PIN));
    long id = 0;
    bool bad = false;
    int res;
    if (strcmp(act, "add") == 0) {
        if (!hasName) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_NAME));
        res = Players::add(name, pin);
        if (res > 0) res = 0;
    } else if (strcmp(act, "rename") == 0 || strcmp(act, "setpin") == 0) {
        const bool ren = act[0] == 'r';
        if (!q.getInt("id", 1, 65535, id, bad) || (ren && !hasName)) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_ID_NAME));
        if (!ren && !hasPin) return MyWeb::sendError(r, 400, trPlayerErr((int)PlayerErr::BAD_PIN));
        if (hasPin && Players::hasPin((uint16_t)id) && !q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
        res = ren ? Players::rename((uint16_t)id, name) : 0;
        if (res == 0 && hasPin && (pin[0] || Players::hasPin((uint16_t)id))) {
            res = Players::setPin((uint16_t)id, pin);
            if (res == 0) MyLog::event("players", "PIN of player %ld %s (web)", id, pin[0] ? "set" : "cleared");
        }
    } else if (strcmp(act, "remove") == 0) {
        if (!q.getInt("id", 1, 65535, id, bad)) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_ID));
        if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
        res = Players::remove((uint16_t)id);
        if (res == 0) MyLog::event("players", "player %ld removed (web)", id);
    } else {
        return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_ACTION));
    }
    if (res < 0) return MyWeb::sendError(r, res == (int)PlayerErr::NOT_FOUND ? 404 : 400, trPlayerErr(res));
    return sendPlayers(r);
}

static esp_err_t handleReset(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
    MyLog::event("cfg", "factory reset (web)");
    Config::resetToDefaults();
    MyWeb::sendOk(r);
    MyWeb::rebootSoon();
    return ESP_OK;
}

bool begin()
{
    MyWeb::Config c;
    c.adminOk = Config::adminOk;
    c.maxUris = sizeof(WEB_ASSETS) / sizeof(WEB_ASSETS[0]) + 48;  // Static assets plus API/WebSocket routes
    c.maxSockets = 12;   // Room for 4 WebSocket clients next to normal page loads (LWIP: 16)
    if (!MyWeb::begin(c)) return false;
    for (const MyWeb::Asset &a : WEB_ASSETS) MyWeb::addAsset(a);
    bool ok = MyWeb::on("/api/info", HTTP_GET, handleInfo);
    ok &= MyWeb::on("/api/diag/crash", HTTP_POST, handleDiagCrash);
    ok &= MyWeb::on("/api/config", HTTP_GET, handleConfigGet);
    ok &= MyWeb::on("/api/config", HTTP_POST, handleConfigPost);
    ok &= MyWeb::on("/api/wifi", HTTP_POST, handleWifiPost);
    ok &= MyWeb::on("/api/scan", HTTP_GET, handleScan);
    ok &= MyWeb::on("/api/nets", HTTP_GET, handleNetsGet);
    ok &= MyWeb::on("/api/nets", HTTP_POST, handleNetsPost);
    ok &= MyWeb::on("/api/time", HTTP_POST, handleTimePost);
    ok &= MyWeb::on("/api/players", HTTP_GET, handlePlayersGet);
    ok &= MyWeb::on("/api/players", HTTP_POST, handlePlayersPost);
    ok &= MyWeb::on("/api/reset", HTTP_POST, handleReset);
    ok &= MyWeb::on("/api/game", HTTP_GET, handleGame);
    ok &= MyWeb::on("/api/adventure", HTTP_GET, handleAdventure);
#if defined(CONFIG_OTR_HW_PROFILE_P4_43)
    ok &= HostOta::registerRoute();
#endif
    ok &= AppWebFiles::registerRoutes();
    ok &= AppWebAdventureEditor::registerRoutes();
    ok &= KaraSongsWeb::registerRoutes();   // v0.13.7 song picker API
    ok &= otr_log_register_routes();        // v0.13.7 /api/log/stream + /api/log/download
    ok &= wsBegin();
    if (Config::get().karaWebStream) ok &= KaraWeb::begin();   // v0.13.5 web karaoke
    if (!ok) ESP_LOGE(TAG, "route registration failed");
    return ok;
}

}  // namespace AppWeb
