// MyEsp-OnTheRoad - Yatzy / Maxi-Yatzy travel console on the Heemol ES3C35P ESP32-S3 3.5" board
// (ST77922 QSPI 320x480 + capacitive touch, WS2812 LED, Wi-Fi station + web UI).
// ESP-IDF 6.1 + LVGL 9. Build: `idf.py build` (see README.md).
//
// Task layout:
//   core 1  "lvgl"   LVGL timer handler + all UI code (Display::startTask)
//   core 0  "rgb"    LED effects; httpd (web server); Wi-Fi driver
//   main    app_main: boot sequence, then Wi-Fi state machine + clock (MyWifi::loop, MyTime::loop)
#include <stdio.h>

#include "app_config.h"
#include "app_players.h"
#include "app_web.h"
#include "board_backlight.h"
#include "board_display.h"
#include "board_rgb.h"
#include "board_touch.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "my_diag.h"
#include "my_log.h"
#include "my_ota.h"
#include "my_storage.h"
#include "my_sys.h"
#include "my_time.h"
#include "my_wifi.h"
#include "ui_boot.h"
#include "ui_ota.h"
#include "ui_settings.h"
#include "ui_wifi.h"
#include "ui_yatzy.h"

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

extern "C" void app_main(void)
{
    // --- Core services: NVS, log capture, filesystem, settings, clock, diagnostics ---
    MyNvs::initFlash();
    MyLog::begin();
    ESP_LOGI(TAG, "=== %s v%s ===", FW_NAME, FW_VERSION);
    const bool fsOk = MyFat::mount("ffat", "/ffat", true);   // First boot formats (a few seconds)
    if (fsOk) MyLog::setEventFile("/ffat/events.log");
    Config::begin();
    const AppConfig &cfg = Config::get();
    MyTime::begin(CFG_NVS_NS, "clk_t", cfg.tz);   // Same key as the Arduino builds
    MyDiag::begin("otr_diag", FW_NAME, FW_VERSION);
    AppWeb::registerDiag();

    // --- Board ---
    Rgb::begin();
    Rgb::setBrightness(cfg.ledBrightness);
    Rgb::setEnabled(cfg.ledEnabled);
    Backlight::begin(0);   // Dark until the panel holds a defined image

    const bool displayOk = Display::begin();
    ESP_LOGI(TAG, "display %s", displayOk ? "ok" : "FAILED");
    if (displayOk) {
        Display::fillScreen(0x0000);
        Backlight::set(cfg.brightness);
    }
    colourTest(displayOk);
    if (!displayOk) Rgb::bootSequence();

    const bool touchOk = displayOk && Touch::begin();   // Registers the LVGL pointer device
    ESP_LOGI(TAG, "touch %s", touchOk ? "ok" : "not found");

    if (displayOk) {
        UiBoot::show();
        Display::startTask();
    }

    // --- Self-test checklist ---
    UiBoot::set(UiBoot::Item::DISP, displayOk ? UiBoot::State::OK : UiBoot::State::FAIL,
                displayOk ? "320 x 480, QSPI 40 MHz, LVGL 9" : "panelen svarar inte");
    UiBoot::setProgress(20);
    {
        Touch::Info ti = Touch::info();
        char buf[48];
        snprintf(buf, sizeof(buf), "fw %u, %u x %u", ti.fwVersion, ti.maxX, ti.maxY);
        UiBoot::set(UiBoot::Item::TOUCH, touchOk ? UiBoot::State::OK : UiBoot::State::FAIL,
                    touchOk ? buf : "ingen svar på 0x55");
    }
    UiBoot::setProgress(35);
    {
        const size_t total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
        char buf[48];
        snprintf(buf, sizeof(buf), "%u kB, %u kB ledigt", (unsigned)(total >> 10),
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >> 10));
        UiBoot::set(UiBoot::Item::PSRAM, total ? UiBoot::State::OK : UiBoot::State::FAIL, total ? buf : "PSRAM saknas");
    }
    {
        uint8_t addrs[16];
        int n = Touch::scanResults(addrs, 16);
        char buf[64] = "";
        size_t used = 0;
        for (int i = 0; i < n && used < sizeof(buf) - 6; i++)
            used += snprintf(buf + used, sizeof(buf) - used, "%s0x%02X", i ? " " : "", addrs[i]);
        UiBoot::set(UiBoot::Item::I2C, n > 0 ? UiBoot::State::OK : UiBoot::State::FAIL, n > 0 ? buf : "inga enheter");
    }
    UiBoot::setProgress(45);

    // Storage: FFat (mounted above) + player registry
    {
        bool ok = fsOk && Players::begin();
        char buf[64];
        snprintf(buf, sizeof(buf), "%u kB ledigt, %d spelare",
                 (unsigned)((MyFat::totalBytes() - MyFat::usedBytes()) >> 10), Players::count());
        UiBoot::set(UiBoot::Item::STORAGE, ok ? UiBoot::State::OK : UiBoot::State::FAIL,
                    ok ? buf : "FFat-partitionen kunde inte monteras");
    }
    UiBoot::setProgress(55);

    UiBoot::set(UiBoot::Item::WIFI, UiBoot::State::RUNNING);
    UiBoot::setStatus("Startar Wi-Fi...");
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
        if (MyWifi::hasSavedCredentials()) snprintf(buf, sizeof(buf), "STA, %d sparade nätverk", MyWifi::savedCount());
        else snprintf(buf, sizeof(buf), "STA, inget sparat nätverk");
        UiBoot::set(UiBoot::Item::WIFI, wifiOk ? UiBoot::State::OK : UiBoot::State::FAIL,
                    wifiOk ? buf : "Wi-Fi-drivrutinen startade inte");
    }
    UiBoot::setProgress(75);

    AppWeb::onConfigChanged(UiSettings::applyHardware);
    const bool webOk = wifiOk && AppWeb::begin();
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "http://%s.local (när ansluten)", cfg.hostname);
        UiBoot::set(UiBoot::Item::WEB, webOk ? UiBoot::State::OK : UiBoot::State::FAIL, webOk ? buf : "startade inte");
    }
    UiBoot::setProgress(100);

    const int fails = UiBoot::failures();
    UiBoot::setStatus(fails ? "Fel hittades - fortsätter om en stund" : "Klart!");
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

    for (;;) {
        MyWifi::loop();
        MyTime::loop();
        MySys::delayMs(20);
    }
}
