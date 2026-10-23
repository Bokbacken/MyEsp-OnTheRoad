// board_wifi.cpp - STA-only Wi-Fi manager: scan, connect with timeout, auto-connect to the
// first visible saved network (up to 50, priority order), mDNS.
#include "board_wifi.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <string.h>

#include "board_config.h"

namespace Wifi {

static constexpr uint32_t RETRY_INTERVAL_MS = 60000;   // Background retry of the saved network

// All state below is guarded by s_lock (recursive: public functions call each other).
// The Wi-Fi event handler only touches the volatile flags.
static SemaphoreHandle_t s_lock = nullptr;
static StatusCallback s_cb = nullptr;
static Status s_status = Status::OFF;
static Status s_reported = Status::OFF;
static FailReason s_lastFail = FailReason::NONE;
static bool s_mdnsOk = false;
static bool s_userIdle = false;          // User disconnected/cancelled: no background retries
static bool s_bgAttempt = false;         // Running attempt is a background retry
static bool s_saveOnSuccess = false;     // Running attempt uses new credentials
static char s_trySsid[33] = "";
static char s_tryPass[65] = "";
static uint32_t s_attemptStartMs = 0;
static uint32_t s_lastAttemptMs = 0;
static uint32_t s_connectedAtMs = 0;
static uint32_t s_attemptId = 0;
static uint32_t s_connects = 0, s_disconnects = 0;
static int s_handshakeFails = 0;

static volatile bool s_gotIp = false;
static volatile bool s_lostIp = false;
static volatile uint8_t s_discReason = 0;   // Last STA disconnect reason (wifi_err_reason_t)
static volatile bool s_discEvent = false;
static volatile int s_scanState = -1;       // -2 running, -1 none/failed, >= 0 count
static volatile bool s_hostnameDirty = false;

// Auto-connect sequence: scan -> try visible saved networks in priority order.
static bool s_autoScan = false;          // Waiting for the auto-connect scan
static bool s_autoActive = false;        // Running attempt belongs to the sequence
static bool s_autoTryNext = false;       // Last sequence attempt failed: try the next one
static int s_autoNext = 0;               // Next saved-list index to consider
static char s_visible[MAX_NETWORKS][33];
static int s_visibleCount = 0;

struct Lock {
    Lock() { if (s_lock) xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
    ~Lock() { if (s_lock) xSemaphoreGiveRecursive(s_lock); }
};

static void onEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
    switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        s_gotIp = true;
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        s_discReason = info.wifi_sta_disconnected.reason;
        s_discEvent = true;
        s_lostIp = true;
        break;
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

// Starts an attempt with s_trySsid / s_tryPass. Caller holds the lock.
static void beginAttempt(bool background, bool saveOnSuccess)
{
    WiFi.disconnect(false, false);
    s_gotIp = false;
    s_lostIp = false;
    s_discEvent = false;
    s_handshakeFails = 0;
    s_bgAttempt = background;
    s_saveOnSuccess = saveOnSuccess;
    s_userIdle = false;
    s_lastFail = FailReason::NONE;
    s_attemptStartMs = s_lastAttemptMs = millis();
    s_attemptId++;
    s_status = Status::CONNECTING;
    Serial.printf("[wifi] connecting to \"%s\"%s\n", s_trySsid, background ? " (background)" : "");
    WiFi.begin(s_trySsid, s_tryPass[0] ? s_tryPass : nullptr);
}

static void failAttempt(FailReason r)
{
    s_lastFail = r;
    s_status = Status::FAILED;
    s_saveOnSuccess = false;
    s_bgAttempt = false;
    s_lastAttemptMs = millis();
    WiFi.disconnect(false, false);   // Stop the driver's own retries (frees the radio for scans)
    Serial.printf("[wifi] connect to \"%s\" failed: %s\n", s_trySsid, failText(r));
    if (s_autoActive && r != FailReason::CANCELLED) s_autoTryNext = true;   // Next saved network
    s_autoActive = false;
}

// Starts the auto-connect sequence with a fresh scan. Caller holds the lock.
static void startAuto(bool background)
{
    if (Config::nets().count == 0 || s_status == Status::OFF || s_status == Status::ERROR) return;
    if (s_scanState == -2) {                 // A scan is already running: reuse its result
        s_autoScan = true;
    } else {
        WiFi.disconnect(false, false);
        WiFi.scanDelete();
        s_scanState = -2;
        s_autoScan = WiFi.scanNetworks(true, false) != WIFI_SCAN_FAILED;
        if (!s_autoScan) s_scanState = -1;
    }
    s_autoTryNext = false;
    s_autoActive = false;
    s_autoNext = 0;
    s_bgAttempt = background;
    s_userIdle = false;
    s_lastAttemptMs = millis();
    if (s_autoScan) {
        s_status = Status::SEARCHING;
        Serial.printf("[wifi] auto-connect: scanning for %u saved network(s)\n", Config::nets().count);
    }
}

// Tries the next visible saved network, or ends the sequence. Caller holds the lock.
static void autoTryNext()
{
    s_autoTryNext = false;
    const NetList &nl = Config::nets();
    const char *vis[MAX_NETWORKS];
    for (int i = 0; i < s_visibleCount; i++) vis[i] = s_visible[i];
    int idx = nl.firstVisible(vis, s_visibleCount, s_autoNext);
    if (idx < 0) {
        bool none = s_autoNext == 0;
        s_lastFail = none ? FailReason::NOT_FOUND : s_lastFail;
        s_status = Status::FAILED;
        s_lastAttemptMs = millis();
        Serial.printf("[wifi] auto-connect: %s\n", none ? "no saved network in range" : "all visible saved networks failed");
        return;
    }
    s_autoNext = idx + 1;
    snprintf(s_trySsid, sizeof(s_trySsid), "%s", nl.items[idx].ssid);
    snprintf(s_tryPass, sizeof(s_tryPass), "%s", nl.items[idx].pass);
    bool bg = s_bgAttempt;
    beginAttempt(bg, false);
    s_autoActive = true;
}

bool begin(StatusCallback cb)
{
    if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutex();
    Lock l;
    s_cb = cb;
    const AppConfig &cfg = Config::get();
    WiFi.persistent(false);          // Credentials live in our own NVS namespace
    WiFi.onEvent(onEvent);
    WiFi.setHostname(cfg.hostname);
#if AP_MODE_ENABLED
    bool modeOk = WiFi.mode(WIFI_AP_STA);
#else
    bool modeOk = WiFi.mode(WIFI_STA);
#endif
    if (!modeOk) {
        s_status = Status::ERROR;
        loop();
        return false;
    }
    WiFi.setSleep(false);            // Lower latency for the web UI
    WiFi.setAutoReconnect(true);     // Driver rejoins after short drops
    s_status = Status::IDLE;
    if (hasSavedCredentials()) startAuto(false);
    loop();
    return true;
}

bool hasSavedCredentials() { return Config::nets().count > 0; }

const char *savedSsid()
{
    if ((s_status == Status::CONNECTING || s_status == Status::CONNECTED) && s_trySsid[0]) return s_trySsid;
    return Config::nets().count ? Config::nets().items[0].ssid : "";
}

bool connect(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32) return false;
    if (!pass) pass = "";
    if (!Config::validWifiPass(pass, true)) return false;   // Empty (open) or 8..63
    Lock l;
    if (s_status == Status::OFF || s_status == Status::ERROR) return false;
    snprintf(s_trySsid, sizeof(s_trySsid), "%s", ssid);
    snprintf(s_tryPass, sizeof(s_tryPass), "%s", pass);
    const NetList &nl = Config::nets();
    int i = nl.find(ssid);
    bool same = i == 0 && strcmp(nl.items[i].pass, pass) == 0;   // Already saved on top
    s_autoScan = s_autoActive = s_autoTryNext = false;
    beginAttempt(false, !same);
    return true;
}

void reconnectSaved()
{
    Lock l;
    startAuto(false);
}

void cancelConnect()
{
    Lock l;
    if (s_status != Status::CONNECTING) return;
    s_lastFail = FailReason::CANCELLED;
    s_userIdle = true;
    s_autoScan = s_autoActive = s_autoTryNext = false;
    s_saveOnSuccess = false;
    WiFi.disconnect(false, false);
    s_status = Status::IDLE;
}

void disconnect()
{
    Lock l;
    s_userIdle = true;
    s_autoScan = s_autoActive = s_autoTryNext = false;
    s_saveOnSuccess = false;
    if (s_status == Status::CONNECTED) s_disconnects++;
    WiFi.disconnect(false, false);
    if (s_status != Status::OFF && s_status != Status::ERROR) s_status = Status::IDLE;
}

void forgetAll()
{
    Lock l;
    disconnect();
    Config::nets().clear();
    Config::saveNets();
    Serial.println("[wifi] all saved networks erased");
}

void savedCopy(NetList &out)
{
    Lock l;
    out = Config::nets();
}

bool savedAdd(const char *ssid, const char *pass)
{
    if (!pass) pass = "";
    if (!Config::validWifiPass(pass, true)) return false;
    Lock l;
    if (Config::nets().upsert(ssid, pass) < 0) return false;
    return Config::saveNets();
}

bool savedRemove(const char *ssid)
{
    Lock l;
    NetList &nl = Config::nets();
    int i = nl.find(ssid);
    if (i < 0) return false;
    if ((s_status == Status::CONNECTED || s_status == Status::CONNECTING) && strcmp(s_trySsid, ssid) == 0) disconnect();
    nl.remove(i);
    Serial.printf("[wifi] forgot \"%s\"\n", ssid);
    return Config::saveNets();
}

bool savedMove(const char *ssid, int delta)
{
    Lock l;
    NetList &nl = Config::nets();
    if (!nl.move(nl.find(ssid), delta)) return false;
    return Config::saveNets();
}

bool savedPassword(const char *ssid, char *out, size_t cap)
{
    Lock l;
    const NetList &nl = Config::nets();
    int i = nl.find(ssid);
    if (i < 0 || !out || !cap) return false;
    snprintf(out, cap, "%s", nl.items[i].pass);
    return true;
}

void requestHostnameUpdate() { s_hostnameDirty = true; }

void loop()
{
    Lock l;
    uint32_t now = millis();

    if (s_hostnameDirty) {
        s_hostnameDirty = false;
        WiFi.setHostname(Config::get().hostname);   // Used by DHCP on the next connect
        if (connected()) startMdns();
    }

    if (s_gotIp) {
        s_gotIp = false;
        s_lostIp = false;
        s_discEvent = false;
        if (s_status != Status::CONNECTED) {
            s_connects++;
            s_connectedAtMs = now;
        }
        s_status = Status::CONNECTED;
        s_lastFail = FailReason::NONE;
        s_bgAttempt = false;
        s_autoActive = s_autoTryNext = s_autoScan = false;
        String cur = WiFi.SSID();
        if (cur.length()) snprintf(s_trySsid, sizeof(s_trySsid), "%s", cur.c_str());
        Serial.printf("[wifi] connected to \"%s\", IP %s, RSSI %d dBm\n", WiFi.SSID().c_str(),
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        if (s_saveOnSuccess) {
            s_saveOnSuccess = false;
            NetList &nl = Config::nets();
            int i = nl.find(s_trySsid);
            if (i > 0) nl.move(i, -i);   // Manually chosen again: move it to the top
            nl.upsert(s_trySsid, s_tryPass);
            Serial.printf("[wifi] credentials for \"%s\" saved: %s\n", s_trySsid, Config::saveNets() ? "ok" : "FAILED");
        }
        startMdns();
    }

    if (s_discEvent) {
        s_discEvent = false;
        uint8_t r = s_discReason;
        if (s_status == Status::CONNECTING) {
            // Fail fast on clear password errors instead of waiting for the timeout.
            if (r == WIFI_REASON_AUTH_FAIL) {
                failAttempt(FailReason::WRONG_PASSWORD);
            } else if (r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT || r == WIFI_REASON_HANDSHAKE_TIMEOUT) {
                if (++s_handshakeFails >= 2) failAttempt(FailReason::WRONG_PASSWORD);
            }
        }
    }

    if (s_lostIp) {
        s_lostIp = false;
        if (s_status == Status::CONNECTED) {
            s_disconnects++;
            Serial.println("[wifi] connection lost, driver reconnects");
            s_status = Status::CONNECTING;           // Driver auto-reconnects first
            s_attemptStartMs = s_lastAttemptMs = now;
            s_attemptId++;
            s_bgAttempt = true;
        }
    }

    if (s_status == Status::CONNECTING && now - s_attemptStartMs > CONNECT_TIMEOUT_MS) {
        uint8_t r = s_discReason;
        FailReason fr = FailReason::TIMEOUT;
        if (r == WIFI_REASON_NO_AP_FOUND) fr = FailReason::NOT_FOUND;
        else if (r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) fr = FailReason::WRONG_PASSWORD;
        failAttempt(fr);
    }

    // Fallback in case the SCAN_DONE event was missed.
    if (s_scanState == -2) {
        int n = WiFi.scanComplete();
        if (n >= 0) s_scanState = n;
        else if (n == WIFI_SCAN_FAILED) s_scanState = -1;
        else if (s_autoScan && now - s_lastAttemptMs > 12000) s_scanState = -1;   // Scan hung
    }

    // Auto-connect: scan finished -> remember visible SSIDs and try the first saved one.
    if (s_autoScan && s_scanState != -2) {
        s_autoScan = false;
        s_visibleCount = 0;
        if (s_scanState > 0) {
            Network tmp[MAX_NETWORKS];
            int n = scanResults(tmp, MAX_NETWORKS);
            for (int i = 0; i < n; i++) snprintf(s_visible[s_visibleCount++], 33, "%s", tmp[i].ssid);
        }
        s_autoNext = 0;
        autoTryNext();
    } else if (s_autoTryNext && s_status == Status::FAILED) {
        autoTryNext();
    }

    // Background retry (e.g. the phone hotspot appears later): new scan + saved list.
    if ((s_status == Status::FAILED || s_status == Status::IDLE) && !s_userIdle && hasSavedCredentials() &&
        s_scanState != -2 && now - s_lastAttemptMs > RETRY_INTERVAL_MS) {
        startAuto(true);
    }

    if (s_status != s_reported) {
        s_reported = s_status;
        Serial.printf("[wifi] status: %s\n", statusText(s_reported));
        if (s_cb) s_cb(s_reported);
    }
}

Status status()
{
    Lock l;   // Never observe the transient FAILED between two auto-connect attempts
    return s_status;
}
bool connected() { return s_status == Status::CONNECTED; }
bool mdnsOk() { return s_mdnsOk; }
IPAddress ip() { return connected() ? WiFi.localIP() : IPAddress(0, 0, 0, 0); }
int rssi() { return connected() ? WiFi.RSSI() : 0; }
uint32_t attemptId() { return s_attemptId; }

uint32_t connectElapsedMs()
{
    Lock l;
    return s_status == Status::CONNECTING ? millis() - s_attemptStartMs : 0;
}

Info info()
{
    Lock l;
    Info i{};
    i.status = s_status;
    i.connected = connected();
    if (i.connected) snprintf(i.ssid, sizeof(i.ssid), "%s", WiFi.SSID().c_str());
    else if (s_status == Status::CONNECTING || s_status == Status::FAILED) snprintf(i.ssid, sizeof(i.ssid), "%s", s_trySsid);
    else snprintf(i.ssid, sizeof(i.ssid), "%s", savedSsid());
    i.ip = ip();
    i.rssi = rssi();
    i.uptimeS = i.connected ? (millis() - s_connectedAtMs) / 1000 : 0;
    i.connects = s_connects;
    i.disconnects = s_disconnects;
    i.lastFail = s_lastFail;
    return i;
}

const char *statusText(Status s)
{
    switch (s) {
    case Status::OFF: return "off";
    case Status::IDLE: return "idle";
    case Status::SEARCHING: return "searching";
    case Status::CONNECTING: return "connecting";
    case Status::CONNECTED: return "connected";
    case Status::FAILED: return "failed";
    default: return "error";
    }
}

const char *failText(FailReason r)
{
    switch (r) {
    case FailReason::NONE: return "";
    case FailReason::TIMEOUT: return "Ingen anslutning inom 15 s";
    case FailReason::WRONG_PASSWORD: return "Fel lösenord?";
    case FailReason::NOT_FOUND: return "Nätverket hittades inte";
    case FailReason::CANCELLED: return "Avbruten";
    default: return "Okänt fel";
    }
}

int signalBars(int rssi)
{
    if (rssi >= -50) return 5;
    if (rssi >= -60) return 4;
    if (rssi >= -70) return 3;
    if (rssi >= -80) return 2;
    return 1;
}

void startScan()
{
    Lock l;
    if (s_scanState == -2 || s_status == Status::OFF || s_status == Status::ERROR) return;
    if (s_status == Status::SEARCHING) return;   // Auto-connect scan running: its result is shared
    // A background retry would make the scan fail: drop it, it is retried later.
    if (s_status == Status::CONNECTING && s_bgAttempt) {
        s_autoActive = false;   // Do not chain to the next saved network during the user's scan
        failAttempt(FailReason::TIMEOUT);
    }
    WiFi.scanDelete();
    s_scanState = -2;
    if (WiFi.scanNetworks(true, false) == WIFI_SCAN_FAILED) s_scanState = -1;
}

int scanStatus() { return s_scanState; }

int scanResults(Network *out, int max)
{
    Lock l;
    int n = s_scanState;
    if (n <= 0 || !out || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.isEmpty()) continue;   // Hidden network
        int rssi = WiFi.RSSI(i);
        int found = -1;
        for (int k = 0; k < count; k++)
            if (strcmp(out[k].ssid, ssid.c_str()) == 0) { found = k; break; }
        if (found >= 0) {               // Same SSID on several APs/bands: keep the strongest
            if (rssi > out[found].rssi) { out[found].rssi = (int8_t)rssi; out[found].channel = WiFi.channel(i); }
            continue;
        }
        if (count >= max) continue;
        Network &nw = out[count++];
        snprintf(nw.ssid, sizeof(nw.ssid), "%s", ssid.c_str());
        nw.rssi = (int8_t)rssi;
        nw.channel = (uint8_t)WiFi.channel(i);
        nw.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    // Insertion sort by RSSI, strongest first (count <= MAX_NETWORKS).
    for (int i = 1; i < count; i++) {
        Network t = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].rssi < t.rssi) { out[j + 1] = out[j]; j--; }
        out[j + 1] = t;
    }
    return count;
}

}  // namespace Wifi
