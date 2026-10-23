// board_wifi.cpp - AP+STA Wi-Fi manager with mDNS and a status callback.
#include "board_wifi.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "board_config.h"

namespace Wifi {

static constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 15000;
static constexpr uint32_t STA_RETRY_INTERVAL_MS = 30000;

static StatusCallback s_cb = nullptr;
static volatile Status s_status = Status::OFF;
static Status s_reported = Status::OFF;
static char s_apSsid[24] = "OnTheRoad";
static bool s_apOk = false;
static bool s_mdnsOk = false;
static volatile bool s_reconnectRequested = false;
static volatile bool s_gotIp = false;
static volatile bool s_lostIp = false;
static uint32_t s_connectStartMs = 0;
static uint32_t s_lastAttemptMs = 0;
static uint32_t s_connects = 0, s_disconnects = 0;
static volatile int s_scanState = -1;   // -1 idle, -2 running, >= 0 result count

static void setStatus(Status s) { s_status = s; }

static void onEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
    (void)info;
    switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        s_gotIp = true;
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        s_lostIp = true;
        break;
    case ARDUINO_EVENT_WIFI_SCAN_DONE:
        s_scanState = WiFi.scanComplete();
        break;
    default:
        break;
    }
}

static void startMdns()
{
    MDNS.end();
    const AppConfig &cfg = Config::get();
    s_mdnsOk = MDNS.begin(cfg.hostname);
    if (s_mdnsOk) {
        MDNS.addService("http", "tcp", 80);
        MDNS.addServiceTxt("http", "tcp", "fw", FW_NAME " " FW_VERSION);
    }
    Serial.printf("[wifi] mDNS %s.local %s\n", cfg.hostname, s_mdnsOk ? "ok" : "FAILED");
}

static bool startAp()
{
    const AppConfig &cfg = Config::get();
    const char *pass = Config::validWifiPass(cfg.apPass, false) ? cfg.apPass : "yatzy1234";
    s_apOk = WiFi.softAP(s_apSsid, pass, 6, 0, 4);
    Serial.printf("[wifi] AP \"%s\" %s, IP %s\n", s_apSsid, s_apOk ? "up" : "FAILED",
                  WiFi.softAPIP().toString().c_str());
    return s_apOk;
}

static void startSta()
{
    const AppConfig &cfg = Config::get();
    WiFi.disconnect(false, false);
    s_gotIp = s_lostIp = false;
    if (!cfg.staSsid[0]) {
        setStatus(s_apOk ? Status::AP_ONLY : Status::ERROR);
        return;
    }
    Serial.printf("[wifi] STA connecting to \"%s\"\n", cfg.staSsid);
    WiFi.begin(cfg.staSsid, cfg.staPass[0] ? cfg.staPass : nullptr);
    s_connectStartMs = s_lastAttemptMs = millis();
    setStatus(Status::STA_CONNECTING);
}

bool begin(StatusCallback cb)
{
    s_cb = cb;
    const AppConfig &cfg = Config::get();
    WiFi.persistent(false);       // Credentials live in our own NVS namespace
    WiFi.onEvent(onEvent);
    WiFi.setHostname(cfg.hostname);
    if (!WiFi.mode(WIFI_AP_STA)) {
        setStatus(Status::ERROR);
        loop();
        return false;
    }
    WiFi.setSleep(false);         // Lower latency for the web UI
    uint8_t mac[6];
    WiFi.softAPmacAddress(mac);
    snprintf(s_apSsid, sizeof(s_apSsid), "OnTheRoad-%02X%02X", mac[4], mac[5]);
    WiFi.softAPsetHostname(cfg.hostname);
    startAp();
    startSta();
    startMdns();
    loop();
    return s_apOk;
}

void requestReconnect() { s_reconnectRequested = true; }

void loop()
{
    uint32_t now = millis();
    if (s_reconnectRequested) {
        s_reconnectRequested = false;
        WiFi.setHostname(Config::get().hostname);
        startAp();
        startSta();
        startMdns();
    }
    if (s_gotIp) {
        s_gotIp = false;
        s_lostIp = false;
        s_connects++;
        setStatus(Status::STA_CONNECTED);
        Serial.printf("[wifi] STA connected, IP %s, RSSI %d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        startMdns();   // Re-announce on the new interface
    }
    if (s_lostIp) {
        s_lostIp = false;
        if (s_status == Status::STA_CONNECTED) {
            s_disconnects++;
            setStatus(Status::STA_CONNECTING);
            s_connectStartMs = s_lastAttemptMs = now;   // The driver auto-reconnects first
        }
    }
    if (s_status == Status::STA_CONNECTING && now - s_connectStartMs > STA_CONNECT_TIMEOUT_MS) {
        Serial.println("[wifi] STA connect timeout");
        setStatus(Status::STA_FAILED);
    }
    if (s_status == Status::STA_FAILED && now - s_lastAttemptMs > STA_RETRY_INTERVAL_MS) {
        s_lastAttemptMs = now;
        WiFi.disconnect(false, false);
        WiFi.begin(Config::get().staSsid, Config::get().staPass[0] ? Config::get().staPass : nullptr);
        // Stay in FAILED (red LED) until an IP arrives.
    }
    if (s_status != s_reported) {
        s_reported = s_status;
        Serial.printf("[wifi] status: %s\n", statusText(s_reported));
        if (s_cb) s_cb(s_reported);
    }
}

Status status() { return s_status; }

const char *statusText(Status s)
{
    switch (s) {
    case Status::OFF: return "off";
    case Status::AP_ONLY: return "ap_only";
    case Status::STA_CONNECTING: return "connecting";
    case Status::STA_CONNECTED: return "connected";
    case Status::STA_FAILED: return "failed";
    default: return "error";
    }
}

bool apOk() { return s_apOk; }
bool staConnected() { return s_status == Status::STA_CONNECTED; }
bool mdnsOk() { return s_mdnsOk; }
const char *apSsid() { return s_apSsid; }
IPAddress apIp() { return WiFi.softAPIP(); }
IPAddress staIp() { return staConnected() ? WiFi.localIP() : IPAddress(0, 0, 0, 0); }
int rssi() { return staConnected() ? WiFi.RSSI() : 0; }
int apClients() { return WiFi.softAPgetStationNum(); }
String staSsid() { return staConnected() ? WiFi.SSID() : String(Config::get().staSsid); }
uint32_t staConnects() { return s_connects; }
uint32_t staDisconnects() { return s_disconnects; }

int scanStatus() { return s_scanState; }

void startScan()
{
    if (s_scanState == -2) return;
    WiFi.scanDelete();
    s_scanState = -2;
    if (WiFi.scanNetworks(true, false) == WIFI_SCAN_FAILED) s_scanState = -1;
}

bool scanResult(int i, String &ssid, int &rssiOut, bool &secure)
{
    if (s_scanState < 0 || i < 0 || i >= s_scanState) return false;
    ssid = WiFi.SSID(i);
    rssiOut = WiFi.RSSI(i);
    secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    return true;
}

}  // namespace Wifi
