// MyEsp-OnTheRoad - Yatzy / Maxi-Yatzy travel console (ES3C35P ESP32-S3 3.5" and Waveshare ESP32-P4 4.3" boards)
// (ST77922 QSPI 320x480 + capacitive touch, WS2812 LED, Wi-Fi station + web UI).
// ESP-IDF 6.1 + LVGL 9. Build: `idf.py build` (see README.md).
//
// Task layout:
//   core 1  "lvgl"   LVGL timer handler + all UI code (Display::startTask)
//   core 0  "rgb"    LED effects; httpd (web server); Wi-Fi driver
//   main    app_main: boot sequence, then Wi-Fi state machine + clock (MyWifi::loop, MyTime::loop)
#include <stdio.h>
#include <time.h>

#include "app_config.h"
#include "app_players.h"
#include "app_web.h"
#include "board_audio.h"
#include "board_battery.h"
#include "board_sd.h"
#include "otr_log.h"
#include "lang.h"
#include "board_backlight.h"
#include "board_display.h"
#include "board_pins.h"
#include "board_rgb.h"
#include "board_touch.h"
#include "battery_math.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "my_diag.h"
#include "my_log.h"
#include "my_ota.h"
#include "my_rtc.h"
#include "my_storage.h"
#include "my_sys.h"
#include "my_time.h"
#include "my_wifi.h"
#include "ui_boot.h"
#include "ui_ota.h"
#include "ui_settings.h"
#include "ui_wifi.h"
#include "ui_yatzy.h"
#include "my_theme.h"
#include "karaoke.h"

static const char *TAG = "main";

// Wi-Fi status -> RGB LED: pulsing blue = connecting, green = connected, red = failed, off = idle.
static void onWifiStatus(MyWifi::Status s)
{
    switch (s) {
    case MyWifi::Status::SEARCHING:
    case MyWifi::Status::CONNECTING:
        Rgb::setBase(0, 60, 200);
        Rgb::setPulse(true);
        break;
    case MyWifi::Status::CONNECTED:
        Rgb::setPulse(false);
        Rgb::setBase(0, 160, 40);
        MyTime::onNetworkUp();   // NTP
        break;
    case MyWifi::Status::FAILED:
    case MyWifi::Status::ERROR:
        Rgb::setPulse(false);
        Rgb::setBase(180, 0, 0);
        break;
    default:
        Rgb::setPulse(false);
        Rgb::setBase(0, 0, 0);
        break;
    }
}

// Full-screen colour test straight on the panel, synchronised with the LED.
static void colourTest(bool displayOk)
{
    struct Step { uint16_t rgb565; uint8_t r, g, b; };
    static const Step steps[] = {{0xF800, 255, 0, 0}, {0x07E0, 0, 255, 0}, {0x001F, 0, 0, 255}, {0xFFFF, 255, 255, 255}};
    for (const Step &s : steps) {
        Rgb::setBase(s.r, s.g, s.b);
        if (displayOk) Display::fillScreen(s.rgb565);
        MySys::delayMs(250);
    }
    Rgb::setBase(0, 0, 0);
    if (displayOk) Display::fillScreen(0x10C5);   // Theme background (#101B2B) in RGB565
}

static bool otaBusy() { return MyOta::busy(); }

static Theme::BatteryIndicator batteryIndicator()
{
    const BoardBattery::Status status = BoardBattery::status();
    uint16_t batteryMv = 0;
    uint8_t percent = 0;
    const uint16_t scale = Config::get().batteryScalePermille;
    const bool voltageValid = status.supported && status.sampleValid && status.adcCalibrated &&
                              BatteryMath::batteryMillivolts(status.adcMillivolts, scale, batteryMv);
    return {voltageValid && BatteryMath::estimatePercent(batteryMv, Config::get().batteryFullMv, percent), percent};
}

static void checkLowBatteryAlert(uint32_t now)
{
    static uint32_t lastBeep = 0;
    const Theme::BatteryIndicator battery = batteryIndicator();
    if (BatteryMath::lowBatteryAlertDue(battery.percentValid, battery.percent, Config::get().soundOn, now, lastBeep))
        Audio::play(Sound::Sfx::CLICK);
}

extern "C" void app_main(void)
{
    // --- Core services: NVS, log capture, filesystem, settings, clock, diagnostics ---
    MyNvs::initFlash();
    MyLog::begin();
    otr_log_init();   // v0.13.7: 64 KB byte ring + SD rotation, chained after MyLog
    ESP_LOGI(TAG, "=== %s v%s ===", FW_NAME, FW_VERSION);
    const bool fsOk = MyFat::mount("ffat", "/ffat", false);   // Never erase profiles on a mount failure.
    if (fsOk) MyLog::setEventFile("/ffat/events.log", 256 * 1024);
    const bool sdOk = BoardSd::begin();   // microSD at /sdcard (4-bit, 1-bit fallback); optional
    Config::begin();
    Theme::setBatteryIndicatorReader(batteryIndicator);
    BoardBattery::begin();
    const AppConfig &cfg = Config::get();
    Lang::setLang(cfg.lang);
    MyTime::begin(CFG_NVS_NS, "clk_t", cfg.tz);   // Same key as the Arduino builds
    MyDiag::begin("otr_diag", FW_NAME, FW_VERSION);
    AppWeb::registerDiag();

    // --- Board ---
    Rgb::begin();
    Rgb::setBrightness(cfg.ledBrightness);
    Rgb::setEnabled(cfg.ledEnabled);
    Backlight::begin(0);   // Dark until the panel holds a defined image

    Display::setRotation(cfg.rotation == 180);   // Sent as MADCTL with the init sequence (v0.5.6)
    Touch::setRotation(cfg.rotation == 180);
    const bool displayOk = Display::begin();
    const bool panelOk = Display::panelOk();
    ESP_LOGI(TAG, "display %s (panel %s%s%s)", displayOk ? "ok" : "FAILED", panelOk ? "ok" : "FAILED",
             displayOk ? "" : ", reason: ", Display::failReason());
    if (panelOk) {
        // Light the panel as soon as it accepted the init sequence, even if LVGL failed afterwards,
        // so the colour test below is visible.
        Display::fillScreen(0x0000);
        Backlight::set(cfg.brightness < 40 ? 40 : cfg.brightness);
    }
    colourTest(panelOk);
    if (panelOk) Backlight::set(cfg.brightness);
    if (!displayOk) {
        // No usable display: blink a fault code on the LED (3 x red = panel, 3 x magenta = LVGL),
        // then light the backlight anyway so a dead panel shows as a lit grey/white screen.
        const bool lvglFault = panelOk;
        for (int i = 0; i < 3; i++) {
            Rgb::setBase(lvglFault ? 160 : 200, 0, lvglFault ? 160 : 0);
            MySys::delayMs(300);
            Rgb::setBase(0, 0, 0);
            MySys::delayMs(300);
        }
        Backlight::set(100);
        Rgb::setBase(lvglFault ? 40 : 60, 0, lvglFault ? 40 : 0);   // Stays dim red/magenta
    }
    if (displayOk) Rgb::setBase(0, 0, 25);   // Dim blue: touch / UI stage
    const bool touchOk = displayOk && Touch::begin();   // Registers the LVGL pointer device
    ESP_LOGI(TAG, "touch %s", touchOk ? "ok" : "not found");

    if (displayOk) {
        UiBoot::show();
        Display::startTask();
    }

    // --- Self-test checklist ---
    {
        char buf[48];
        snprintf(buf, sizeof(buf), "%d x %d, %s, LVGL 9", LCD_H_RES, LCD_V_RES, LCD_BUS_DESC);
        UiBoot::set(UiBoot::Item::DISP, displayOk ? UiBoot::State::OK : UiBoot::State::FAIL,
                    displayOk ? buf : Display::failReason());
    }
    UiBoot::setProgress(20);
    {
        Touch::Info ti = Touch::info();
        char buf[48];
        snprintf(buf, sizeof(buf), "fw %u, %u x %u", ti.fwVersion, ti.maxX, ti.maxY);
        UiBoot::set(UiBoot::Item::TOUCH, touchOk ? UiBoot::State::OK : UiBoot::State::FAIL,
                    touchOk ? buf : tr(Str::BOOT_TOUCH_NO_REPLY));
    }
    UiBoot::setProgress(35);
    {
        const size_t total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        char buf[48];
        snprintf(buf, sizeof(buf), tr(Str::BOOT_PSRAM_INFO), (unsigned)(total >> 10),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >> 10));
        UiBoot::set(UiBoot::Item::PSRAM, total ? UiBoot::State::OK : UiBoot::State::FAIL, total ? buf : tr(Str::BOOT_PSRAM_MISSING));
    }
    {
        uint8_t addrs[16];
        int n = Touch::scanResults(addrs, 16);
        char buf[64] = "";
        size_t used = 0;
        for (int i = 0; i < n && used < sizeof(buf) - 6; i++)
            used += snprintf(buf + used, sizeof(buf) - used, "%s0x%02X", i ? " " : "", addrs[i]);
        UiBoot::set(UiBoot::Item::I2C, n > 0 ? UiBoot::State::OK : UiBoot::State::FAIL, n > 0 ? buf : tr(Str::BOOT_NO_DEVICES));
    }
    // Board-selected RTC: function test, then it becomes the boot clock source.
    {
        time_t t = 0;
        const bool rtcOk = MyRtc::begin(Touch::bus(), &t);
        const MyRtc::Status rs = MyRtc::status();
        char buf[64];
        if (rtcOk) {
            MyTime::setFromRtc(t);
            struct tm tm;
            gmtime_r(&t, &tm);
            int n = snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d UTC", tm.tm_year + 1900, tm.tm_mon + 1,
                             tm.tm_mday, tm.tm_hour, tm.tm_min);
            if (MyRtc::temperatureAvailable() && n > 0 && n < (int)sizeof(buf))
                snprintf(buf + n, sizeof(buf) - n, ", %.1f °C", (double)rs.tempC);
        } else if (rs.state == MyRtc::State::MISSING) {
            snprintf(buf, sizeof(buf), "%s", tr(Str::BOOT_RTC_MISSING));
        } else {
            snprintf(buf, sizeof(buf), "%s", MyRtc::stateText(rs.state));
        }
        bool waiting = false;
#if defined(CONFIG_OTR_HW_PROFILE_P4_43)
        waiting = rs.present && rs.ticking && rs.state == MyRtc::State::TIME_INVALID;
#endif
        UiBoot::set(UiBoot::Item::RTC, rtcOk ? UiBoot::State::OK :
                    waiting ? UiBoot::State::WAITING : UiBoot::State::FAIL, buf);
        if (!rtcOk && !waiting) {   // 2 x orange blink on the LED
            for (int i = 0; i < 2; i++) {
                Rgb::setBase(200, 70, 0);
                MySys::delayMs(180);
                Rgb::setBase(0, 0, 0);
                MySys::delayMs(180);
            }
            if (displayOk) Rgb::setBase(0, 0, 25);
        }
    }
    // Sound: ES8311 on the same bus. Amplifier stays off unless the codec came up cleanly.
    {
        const bool audioOk = Audio::begin(Touch::bus());
        Audio::setVolumes(cfg.soundOn, cfg.sfxVol, cfg.musicOn, cfg.musicVol);
        Karaoke::setVolume(cfg.karaVolume);
        ESP_LOGI(TAG, "audio %s", audioOk ? "ok" : "not available");
    }
    UiBoot::setProgress(50);

    // Storage: FFat (mounted above) + player registry
    {
        bool ok = fsOk && Players::begin();
        char buf[64];
        int len = snprintf(buf, sizeof(buf), tr(Str::BOOT_FS_INFO),
                           (unsigned)((MyFat::totalBytes() - MyFat::usedBytes()) >> 10), Players::count());
        if (len > 0 && len < (int)sizeof(buf)) {
            if (sdOk) snprintf(buf + len, sizeof(buf) - len, tr(Str::BOOT_SD_FREE), (unsigned)(BoardSd::info().freeBytes >> 20));
            else snprintf(buf + len, sizeof(buf) - len, "%s", tr(Str::BOOT_SD_NONE));
        }
        if (!ok) {
            const BoardSd::Info sd = BoardSd::info();
            snprintf(buf, sizeof(buf), "%s", tr(Str::BOOT_FS_FAIL));
            const size_t n = strlen(buf);
            if (sd.mounted && n < sizeof(buf))
                snprintf(buf + n, sizeof(buf) - n, tr(Str::BOOT_SD_FREE), (unsigned)(sd.freeBytes >> 20));
        }
        UiBoot::set(UiBoot::Item::STORAGE, ok ? UiBoot::State::OK : UiBoot::State::FAIL, buf);
    }
    UiBoot::setProgress(55);

    if (displayOk) Rgb::setBase(25, 18, 0);   // Dim amber: Wi-Fi / web stage
    UiBoot::set(UiBoot::Item::WIFI, UiBoot::State::RUNNING);
    UiBoot::setStatus(tr(Str::BOOT_STARTING_WIFI));
    MyWifi::Hooks hooks;
    hooks.hostname = Config::hostname;
    hooks.loadNets = Config::loadNets;
    hooks.saveNets = Config::saveNets;
    hooks.busy = otaBusy;
    hooks.onStatus = onWifiStatus;
    hooks.mdnsTxtFw = FW_VERSION;
    const bool wifiOk = MyWifi::begin(hooks);   // STA only; connects to saved networks in the background
    {
        char buf[64];
        if (MyWifi::hasSavedCredentials()) snprintf(buf, sizeof(buf), tr(Str::BOOT_WIFI_SAVED), MyWifi::savedCount());
        else snprintf(buf, sizeof(buf), "%s", tr(Str::BOOT_WIFI_NONE));
        UiBoot::set(UiBoot::Item::WIFI, wifiOk ? UiBoot::State::OK : UiBoot::State::FAIL,
                    wifiOk ? buf : tr(Str::BOOT_WIFI_FAIL));
    }
    UiBoot::setProgress(75);

    AppWeb::onConfigChanged(UiSettings::applyHardware);
    const bool webOk = wifiOk && AppWeb::begin();
    {
        char buf[64];
        snprintf(buf, sizeof(buf), tr(Str::BOOT_WEB_INFO), cfg.hostname);
        UiBoot::set(UiBoot::Item::WEB, webOk ? UiBoot::State::OK : UiBoot::State::FAIL, webOk ? buf : tr(Str::BOOT_WEB_FAIL));
    }
    UiBoot::setProgress(100);

    if (displayOk && !(wifiOk && MyWifi::hasSavedCredentials())) Rgb::setBase(0, 0, 0);   // Else Wi-Fi status owns the LED
    const int fails = UiBoot::failures();
    UiBoot::setStatus(fails ? tr(Str::BOOT_FAILURES) : tr(Str::BOOT_DONE));
    ESP_LOGI(TAG, "boot done in %u ms, %d failure(s)", (unsigned)MySys::millis(), fails);
    // A new OTA image confirms itself once the boot sequence completed without crashing
    // (a crash before this point makes the bootloader return to the previous image).
    if (MyOta::pendingVerify()) MyOta::markValid();
    MySys::delayMs(fails ? 5000 : 1500);

    if (displayOk) {
        // Saved network -> connect screen (auto-connect already running), otherwise the network list.
        // Both lead to the main menu (Avbryt / success), so the games work without Wi-Fi.
        DisplayLock lock;
        if (wifiOk && MyWifi::hasSavedCredentials()) UiWifi::showConnecting(MyWifi::savedSsid(), nullptr, true);
        else if (wifiOk) UiWifi::showNetworks();
        else UiYatzy::showMenu();
        UiOta::begin();
    }

    uint32_t lastRtcCheck = MySys::millis();
    uint32_t lastBatteryAlertCheck = lastRtcCheck;
    uint32_t lastBatteryLog = lastRtcCheck;
    for (;;) {
        MyWifi::loop();
        MyTime::loop();
        const uint32_t now = MySys::millis();
        const BoardBattery::Status battery = BoardBattery::status();
        uint16_t batteryMv = 0;
        const AppConfig &currentConfig = Config::get();
        const bool batteryVoltageValid =
            battery.supported && battery.sampleValid && battery.adcCalibrated &&
            BatteryMath::batteryMillivolts(battery.adcMillivolts, currentConfig.batteryScalePermille, batteryMv);
        if (BatteryMath::batteryLogDue(batteryVoltageValid, now, lastBatteryLog)) {
            lastBatteryLog = now;
            MyLog::event("battery", "battery_mV=%u adc_mV=%u raw=%ld scale_permille=%u",
                         static_cast<unsigned>(batteryMv), static_cast<unsigned>(battery.adcMillivolts),
                         static_cast<long>(battery.raw), static_cast<unsigned>(currentConfig.batteryScalePermille));
        }
        if (static_cast<uint32_t>(now - lastBatteryAlertCheck) >= 1000) {
            lastBatteryAlertCheck = now;
            checkLowBatteryAlert(now);
        }
        BoardSd::poll(now);   // Card removed / inserted
        // Keep the RTC in step with a certain clock: right after each NTP sync / manual set, otherwise every
        // 10 minutes (covers manual sets; writes only when the drift is >= 1 s).
        const bool changed = MyTime::takeClockChanged();
        if (changed || (MyTime::certain() && MySys::millis() - lastRtcCheck >= 10UL * 60 * 1000)) {
            lastRtcCheck = MySys::millis();
            if (MyTime::source() != MyTime::Source::RTC) MyRtc::syncFromSystem();
        }
        MySys::delayMs(20);
    }
}
