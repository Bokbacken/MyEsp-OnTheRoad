// MyEsp-OnTheRoad - Yatzy / Maxi-Yatzy travel console on the Heemol ESP32-S3 3.5" board
// (ST77922 QSPI 320x480 + capacitive touch, WS2812 LED, Wi-Fi station + web UI).
//
// Arduino IDE 2 / arduino-cli board settings (Arduino-ESP32 core 3.x):
//   Board:            ESP32S3 Dev Module
//   Flash Size:       16MB
//   PSRAM:            OPI PSRAM (enabled)
//   USB CDC on Boot:  Enabled
//   Partition Scheme: 16M Flash (3MB APP/9.9MB FATFS)   (or equivalent with >= 3 MB app)
//   CPU Frequency:    240MHz (WiFi)
//
//   arduino-cli FQBN:
//   esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,CPUFreq=240
//
// Libraries: lvgl 8.4.x (copy extras/lv_conf.h next to the "libraries" folder),
//            ESP Async WebServer + Async TCP (ESP32Async). See README.md.
//
// Task layout:
//   core 1  "lvgl"    LVGL timer handler + all UI code (Display::startTask)
//   core 0  "rgb"     LED effects; async_tcp (web server); Wi-Fi driver
//   loopTask          Wi-Fi state machine (Wifi::loop)

#include <Arduino.h>
#include <lvgl.h>

#include "board_backlight.h"
#include "board_config.h"
#include "board_display.h"
#include "board_pins.h"
#include "board_rgb.h"
#include "board_touch.h"
#include "board_webserver.h"
#include "board_wifi.h"
#include "encoder_input.h"
#include "ui_boot.h"
#include "ui_settings.h"
#include "ui_wifi.h"
#include "ui_yatzy.h"

static bool g_uiReady = false;

// Wi-Fi status -> RGB LED: pulsing blue = connecting, green = connected, red = failed, off = idle.
static void onWifiStatus(Wifi::Status s)
{
    switch (s) {
    case Wifi::Status::CONNECTING:
        Rgb::setBase(0, 60, 200);
        Rgb::setPulse(true);
        break;
    case Wifi::Status::CONNECTED:
        Rgb::setPulse(false);
        Rgb::setBase(0, 160, 40);
        break;
    case Wifi::Status::FAILED:
    case Wifi::Status::ERROR:
        Rgb::setPulse(false);
        Rgb::setBase(180, 0, 0);
        break;
    default:
        Rgb::setPulse(false);
        Rgb::setBase(0, 0, 0);
        break;
    }
}

// Full-screen colour test straight on the panel, synchronised with the LED boot sequence.
static void colourTest(bool displayOk)
{
    struct Step { uint16_t rgb565; uint8_t r, g, b; };
    static const Step steps[] = {
        {0xF800, 255, 0, 0}, {0x07E0, 0, 255, 0}, {0x001F, 0, 0, 255}, {0xFFFF, 255, 255, 255}};
    for (const Step &s : steps) {
        Rgb::setBase(s.r, s.g, s.b);
        if (displayOk) Display::fillScreen(s.rgb565);
        delay(250);
    }
    Rgb::setBase(0, 0, 0);
    if (displayOk) Display::fillScreen(0x10C5);   // Theme background (#101B2B) in RGB565
}

void setup()
{
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 1500) delay(10);   // USB CDC: give the host a moment
    Serial.printf("\n=== %s v%s ===\n", FW_NAME, FW_VERSION);
    Serial.printf("[sys] %s rev %d, %lu MHz, flash %lu MB, PSRAM %lu kB free\n", ESP.getChipModel(),
                  ESP.getChipRevision(), (unsigned long)ESP.getCpuFreqMHz(),
                  (unsigned long)(ESP.getFlashChipSize() >> 20), (unsigned long)(ESP.getFreePsram() >> 10));

    Config::begin();
    const AppConfig &cfg = Config::get();

    Rgb::begin();
    Rgb::setBrightness(cfg.ledBrightness);
    Rgb::setEnabled(cfg.ledEnabled);
    Backlight::begin(0);   // Dark until the panel holds a defined image

    bool displayOk = Display::begin();
    Serial.printf("[lcd] %s\n", displayOk ? "ok" : "FAILED");
    if (displayOk) {
        Display::fillScreen(0x0000);
        Backlight::set(cfg.brightness);
    }
    colourTest(displayOk);
    if (!displayOk) Rgb::bootSequence();

    if (!lv_is_initialized()) lv_init();   // Touch/encoder register LVGL input devices
    bool touchOk = Touch::begin();
    Serial.printf("[touch] %s\n", touchOk ? "ok" : "not found");
    bool encOk = Encoder::begin();

    if (displayOk) {
        UiBoot::show();
        Display::startTask();
        g_uiReady = true;
    }

    // --- Self-test checklist ---
    UiBoot::set(UiBoot::Item::DISP, displayOk ? UiBoot::State::OK : UiBoot::State::FAIL,
                displayOk ? "320 x 480, QSPI 40 MHz" : "panelen svarar inte");
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
        char buf[48];
        snprintf(buf, sizeof(buf), "%lu kB, %lu kB ledigt", (unsigned long)(ESP.getPsramSize() >> 10),
                 (unsigned long)(ESP.getFreePsram() >> 10));
        bool ok = ESP.getPsramSize() > 0;
        UiBoot::set(UiBoot::Item::PSRAM, ok ? UiBoot::State::OK : UiBoot::State::FAIL,
                    ok ? buf : "PSRAM saknas - välj OPI PSRAM");
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
    UiBoot::setProgress(50);

    UiBoot::set(UiBoot::Item::WIFI, UiBoot::State::RUNNING);
    UiBoot::setStatus("Startar Wi-Fi…");
    bool wifiOk = Wifi::begin(onWifiStatus);   // STA only; connects to the saved network in the background
    {
        char buf[64];
        if (Wifi::hasSavedCredentials()) snprintf(buf, sizeof(buf), "STA, sparat: %s", Wifi::savedSsid());
        else snprintf(buf, sizeof(buf), "STA, inget sparat nätverk");
        UiBoot::set(UiBoot::Item::WIFI, wifiOk ? UiBoot::State::OK : UiBoot::State::FAIL,
                    wifiOk ? buf : "Wi-Fi-drivrutinen startade inte");
    }
    UiBoot::setProgress(75);

    Webserver::onConfigChanged(UiSettings::applyHardware);
    bool webOk = wifiOk && Webserver::begin();
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "http://%s.local (när ansluten)", cfg.hostname);
        UiBoot::set(UiBoot::Item::WEB, webOk ? UiBoot::State::OK : UiBoot::State::FAIL, webOk ? buf : "startade inte");
    }
    UiBoot::setProgress(100);
    (void)encOk;

    int fails = UiBoot::failures();
    UiBoot::setStatus(fails ? "Fel hittades - fortsätter om en stund" : "Klart!");
    Serial.printf("[boot] done in %lu ms, %d failure(s)\n", (unsigned long)millis(), fails);
    delay(fails ? 5000 : 1500);

    if (g_uiReady) {
        // Saved network -> connect screen (auto-connect already running), otherwise the network list.
        // Both screens lead to the main menu (Avbryt / success), so the games work without Wi-Fi.
        DisplayLock lock;
        if (wifiOk && Wifi::hasSavedCredentials()) UiWifi::showConnecting(Wifi::savedSsid(), nullptr, true);
        else if (wifiOk) UiWifi::showNetworks();
        else UiYatzy::showMenu();
    }
}

void loop()
{
    Wifi::loop();
    delay(20);
}
