// my_wifi.cpp - STA-only Wi-Fi manager on esp_wifi: scan, connect with timeout, auto-connect to
// the first visible saved network, roaming between APs of the same SSID, mDNS.
#include "my_wifi.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mdns.h"
#include "my_log.h"
#include "my_sys.h"
#include "roam_policy.h"

namespace MyWifi {

static const char *TAG = "wifi";
using MySys::millis;

static constexpr uint32_t RETRY_INTERVAL_MS = 60000;   // Background retry of saved networks
static constexpr uint32_t LOSS_GRACE_MS = 6000;        // Driver rejoin window before a fresh scan
static constexpr uint32_t ROAM_DWELL_MS = 120;         // Per-channel dwell for background scans
static constexpr int MAX_APS = 16;                     // BSSIDs tracked per SSID
static constexpr int SCAN_CAP = 48;                    // Raw AP records kept from one scan

static SemaphoreHandle_t s_lock = nullptr;
static Hooks s_hooks;
static NetList *s_nets = nullptr;          // PSRAM when available
static esp_netif_t *s_netif = nullptr;
static Status s_status = Status::OFF;
static Status s_reported = Status::OFF;
static FailReason s_lastFail = FailReason::NONE;
static bool s_mdnsInit = false, s_mdnsOk = false;
static bool s_userIdle = false;
static bool s_screenPowerSave = false;
static bool s_bgAttempt = false;
static bool s_saveOnSuccess = false;
static char s_trySsid[33] = "";
static char s_tryPass[65] = "";
static uint32_t s_attemptStartMs = 0, s_lastAttemptMs = 0, s_connectedAtMs = 0;
static uint32_t s_attemptId = 0;
static uint32_t s_connects = 0, s_disconnects = 0;
static int s_handshakeFails = 0;
static char s_ip[16] = "0.0.0.0";

// Set by the event handler (default event loop task).
static volatile bool s_gotIp = false;
static volatile bool s_lostIp = false;
static volatile uint8_t s_discReason = 0;
static volatile bool s_discEvent = false;
static volatile bool s_scanDoneEvt = false;
static volatile bool s_scanDoneOk = false;
static volatile bool s_driverRetry = false;   // Rejoin automatically after a drop (like Arduino autoReconnect)
static volatile uint32_t s_newIp = 0;
static volatile bool s_hostnameDirty = false;

// Scan results (copied out of the driver in loop()).
static wifi_ap_record_t *s_scan = nullptr;
static volatile int s_scanState = -1;      // -2 running, -1 none/failed, >= 0 count
static uint32_t s_scanStartMs = 0;
static uint32_t s_scanDoneMs = 0;

// Auto-connect sequence.
static bool s_autoScan = false, s_autoActive = false, s_autoTryNext = false;
static int s_autoNext = 0;
static char s_visible[MAX_NETWORKS][33];
static int s_visibleCount = 0;

// Roaming.
static uint8_t s_curBssid[6] = {0};
static uint8_t s_curChannel = 0;
static uint32_t s_roams = 0;
static uint32_t s_lastRoamScanMs = 0, s_lastRoamMs = 0;
static bool s_roamScan = false, s_roamAttempt = false, s_lossRecover = false;

struct Lock {
    Lock() { if (s_lock) xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
    ~Lock() { if (s_lock) xSemaphoreGiveRecursive(s_lock); }
};

static void onEvent(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_DISCONNECTED) {
            auto *d = (wifi_event_sta_disconnected_t *)data;
            s_discReason = (uint8_t)d->reason;
            s_discEvent = true;
            s_lostIp = true;
            if (s_driverRetry && d->reason != WIFI_REASON_ASSOC_LEAVE) esp_wifi_connect();
        } else if (id == WIFI_EVENT_SCAN_DONE) {
            auto *d = (wifi_event_sta_scan_done_t *)data;
            s_scanDoneOk = d->status == 0;
            s_scanDoneEvt = true;
        }
    } else if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP) {
            s_newIp = ((ip_event_got_ip_t *)data)->ip_info.ip.addr;
            s_gotIp = true;
        } else if (id == IP_EVENT_STA_LOST_IP) {
            s_lostIp = true;
        }
    }
}

static void saveNets()
{
    bool ok = s_hooks.saveNets ? s_hooks.saveNets(*s_nets) : true;
    if (!ok) ESP_LOGW(TAG, "saving the network list FAILED");
}

static void startMdns()
{
    const char *host = s_hooks.hostname ? s_hooks.hostname() : "esp32";
    if (!s_mdnsInit) s_mdnsInit = mdns_init() == ESP_OK;
    s_mdnsOk = false;
    if (s_mdnsInit) {
        mdns_service_remove_all();
        s_mdnsOk = mdns_hostname_set(host) == ESP_OK;
        char instance[48];
        uint8_t mac[6] = {};
        if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK)
            snprintf(instance, sizeof(instance), "%s-%02X%02X%02X", host, mac[3], mac[4], mac[5]);
        else
            snprintf(instance, sizeof(instance), "%s", host);
        if (s_mdnsOk) s_mdnsOk = mdns_instance_name_set(instance) == ESP_OK;
        mdns_txt_item_t txt[1] = {{"fw", s_hooks.mdnsTxtFw ? s_hooks.mdnsTxtFw : ""}};
        mdns_txt_item_t gameTxt[] = {{"proto", "1"}, {"games", "yatzy"}, {"device", host}};
        if (s_mdnsOk) s_mdnsOk = mdns_service_add(nullptr, "_http", "_tcp", 80, txt, 1) == ESP_OK;
        if (s_mdnsOk) s_mdnsOk = mdns_service_add(nullptr, "_ontheroad", "_tcp", 80, gameTxt, 3) == ESP_OK;
    }
    ESP_LOGI(TAG, "mDNS %s.local %s", host, s_mdnsOk ? "ok" : "FAILED");
}

static bool scanStart(uint32_t dwellMs)
{
    wifi_scan_config_t sc = {};
    sc.show_hidden = false;
    sc.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    sc.scan_time.active.min = dwellMs ? dwellMs / 2 : 100;
    sc.scan_time.active.max = dwellMs ? dwellMs : 300;
    s_scanDoneEvt = false;
    s_scanState = -2;
    s_scanStartMs = millis();
    esp_err_t e = esp_wifi_scan_start(&sc, false);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "scan start failed: %s", esp_err_to_name(e));
        s_scanState = -1;
        return false;
    }
    return true;
}

// Copies the driver's scan records (frees driver memory). Caller holds the lock.
static void scanCollect()
{
    s_scanDoneEvt = false;
    if (!s_scanDoneOk) {
        esp_wifi_clear_ap_list();
        s_scanState = -1;
        return;
    }
    uint16_t n = SCAN_CAP;
    if (!s_scan || esp_wifi_scan_get_ap_records(&n, s_scan) != ESP_OK) {
        esp_wifi_clear_ap_list();
        n = 0;
    }
    s_scanState = n;
    s_scanDoneMs = millis();
}

static int scannedAps(const char *ssid, Roam::Ap *out, int max)
{
    int n = s_scanState, count = 0;
    for (int i = 0; i < n && count < max; i++) {
        if (strcmp((const char *)s_scan[i].ssid, ssid) != 0) continue;
        memcpy(out[count].bssid, s_scan[i].bssid, 6);
        out[count].rssi = s_scan[i].rssi;
        out[count].channel = s_scan[i].primary;
        count++;
    }
    return count;
}

static bool bestBssid(const char *ssid, Roam::Ap &out)
{
    if (s_scanState <= 0 || millis() - s_scanDoneMs > Roam::BSSID_PIN_MAX_AGE_MS) return false;
    Roam::Ap aps[MAX_APS];
    int n = scannedAps(ssid, aps, MAX_APS);
    int i = Roam::strongest(aps, n);
    if (i < 0) return false;
    out = aps[i];
    return true;
}

static void driverDisconnect()
{
    s_driverRetry = false;
    esp_wifi_disconnect();
}

static void beginAttempt(bool background, bool saveOnSuccess, const Roam::Ap *ap = nullptr)
{
    driverDisconnect();
    s_gotIp = false; s_lostIp = false; s_discEvent = false;
    s_handshakeFails = 0;
    s_bgAttempt = background;
    s_saveOnSuccess = saveOnSuccess;
    s_userIdle = false;
    s_lastFail = FailReason::NONE;
    s_attemptStartMs = s_lastAttemptMs = millis();
    s_attemptId++;
    s_status = Status::CONNECTING;
    s_lossRecover = false;

    wifi_config_t wc = {};
    memcpy(wc.sta.ssid, s_trySsid, strnlen(s_trySsid, sizeof(wc.sta.ssid)));   // 32 chars = not NUL-terminated
    memcpy(wc.sta.password, s_tryPass, strlen(s_tryPass));   // 64 hex = raw PSK, not NUL-terminated
    wc.sta.threshold.authmode = s_tryPass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    // Without a pinned AP: scan all channels and join the strongest AP (the default fast scan
    // takes the first AP found, often a distant one).
    wc.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wc.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    if (ap) {
        wc.sta.bssid_set = true;
        memcpy(wc.sta.bssid, ap->bssid, 6);
        wc.sta.channel = ap->channel;
        char b[18];
        Roam::formatBssid(ap->bssid, b);
        ESP_LOGI(TAG, "connecting to \"%s\" via %s ch %u (%d dBm)%s", s_trySsid, b, ap->channel, ap->rssi,
                 background ? " (background)" : "");
    } else {
        ESP_LOGI(TAG, "connecting to \"%s\"%s", s_trySsid, background ? " (background)" : "");
    }
    esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (e == ESP_OK) {
        s_driverRetry = true;
        e = esp_wifi_connect();
    }
    if (e != ESP_OK) ESP_LOGW(TAG, "connect start failed: %s", esp_err_to_name(e));
}

static void failAttempt(FailReason r)
{
    s_lastFail = r;
    s_status = Status::FAILED;
    s_saveOnSuccess = false;
    s_bgAttempt = false;
    s_lastAttemptMs = millis();
    driverDisconnect();   // Stop the driver's retries (frees the radio for scans)
    ESP_LOGW(TAG, "connect to \"%s\" failed: %s", s_trySsid, failText(r));
    if (s_autoActive && r != FailReason::CANCELLED) s_autoTryNext = true;
    s_autoActive = false;
}

static void startAuto(bool background)
{
    if (s_nets->count == 0 || s_status == Status::OFF || s_status == Status::ERROR) return;
    if (s_scanState == -2) {
        s_autoScan = true;   // Reuse the running scan
    } else {
        driverDisconnect();
        s_autoScan = scanStart(0);
    }
    s_roamScan = s_roamAttempt = s_lossRecover = false;
    s_autoTryNext = s_autoActive = false;
    s_autoNext = 0;
    s_bgAttempt = background;
    s_userIdle = false;
    s_lastAttemptMs = millis();
    if (s_autoScan) {
        s_status = Status::SEARCHING;
        ESP_LOGI(TAG, "auto-connect: scanning for %u saved network(s)", s_nets->count);
    } else if (s_status == Status::CONNECTED || s_status == Status::CONNECTING) {
        s_status = Status::FAILED;   // Scan refused: background retry in ~5 s
        s_lastAttemptMs = millis() - RETRY_INTERVAL_MS + 5000;
    }
}

static void autoTryNext()
{
    s_autoTryNext = false;
    const char *vis[MAX_NETWORKS];
    for (int i = 0; i < s_visibleCount; i++) vis[i] = s_visible[i];
    int idx = s_nets->firstVisible(vis, s_visibleCount, s_autoNext);
    if (idx < 0) {
        bool none = s_autoNext == 0;
        if (none) s_lastFail = FailReason::NOT_FOUND;
        s_status = Status::FAILED;
        s_lastAttemptMs = millis();
        ESP_LOGI(TAG, "auto-connect: %s", none ? "no saved network in range" : "all visible saved networks failed");
        return;
    }
    s_autoNext = idx + 1;
    snprintf(s_trySsid, sizeof(s_trySsid), "%s", s_nets->items[idx].ssid);
    snprintf(s_tryPass, sizeof(s_tryPass), "%s", s_nets->items[idx].pass);
    Roam::Ap ap;
    beginAttempt(s_bgAttempt, false, bestBssid(s_trySsid, ap) ? &ap : nullptr);
    s_autoActive = true;
}

static bool initDriver()
{
    esp_err_t e = esp_netif_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return false;
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return false;
    s_netif = esp_netif_create_default_wifi_sta();
    if (!s_netif) return false;
    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&ic) != ESP_OK) return false;
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onEvent, nullptr);
    esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, onEvent, nullptr);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);   // Credentials live in the app's own NVS namespace
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;
    if (s_hooks.hostname) esp_netif_set_hostname(s_netif, s_hooks.hostname());
    if (esp_wifi_start() != ESP_OK) return false;
    const esp_err_t psErr = esp_wifi_set_ps(s_screenPowerSave ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
    if (psErr != ESP_OK) ESP_LOGW(TAG, "initial power-save mode failed: %s", esp_err_to_name(psErr));
    return true;
}

bool begin(const Hooks &hooks)
{
    if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutex();
    Lock l;
    s_hooks = hooks;
    if (!s_nets) {
        s_nets = (NetList *)heap_caps_calloc(1, sizeof(NetList), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_nets) s_nets = (NetList *)calloc(1, sizeof(NetList));
        s_scan = (wifi_ap_record_t *)heap_caps_calloc(SCAN_CAP, sizeof(wifi_ap_record_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_scan) s_scan = (wifi_ap_record_t *)calloc(SCAN_CAP, sizeof(wifi_ap_record_t));
    }
    if (!s_nets || !s_scan) {
        s_status = Status::ERROR;
        return false;
    }
    s_nets->clear();
    if (s_hooks.loadNets && !s_hooks.loadNets(*s_nets)) s_nets->clear();
    if (s_nets->count > NET_LIST_MAX) s_nets->clear();
    if (!initDriver()) {
        ESP_LOGE(TAG, "driver start failed");
        s_status = Status::ERROR;
        loop();
        return false;
    }
    s_status = Status::IDLE;
    if (hasSavedCredentials()) startAuto(false);
    loop();
    return true;
}

bool hasSavedCredentials() { return s_nets && s_nets->count > 0; }
int savedCount() { return s_nets ? s_nets->count : 0; }

const char *savedSsid()
{
    if ((s_status == Status::CONNECTING || s_status == Status::CONNECTED) && s_trySsid[0]) return s_trySsid;
    return hasSavedCredentials() ? s_nets->items[0].ssid : "";
}

bool connect(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0] || strlen(ssid) > 32) return false;
    if (!pass) pass = "";
    if (!validPass(pass, true)) return false;
    Lock l;
    if (s_status == Status::OFF || s_status == Status::ERROR) return false;
    snprintf(s_trySsid, sizeof(s_trySsid), "%s", ssid);
    snprintf(s_tryPass, sizeof(s_tryPass), "%s", pass);
    int i = s_nets->find(ssid);
    bool same = i == 0 && strcmp(s_nets->items[i].pass, pass) == 0;
    s_autoScan = s_autoActive = s_autoTryNext = false;
    s_roamScan = s_roamAttempt = false;
    Roam::Ap ap;
    beginAttempt(false, !same, bestBssid(ssid, ap) ? &ap : nullptr);
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
    driverDisconnect();
    s_status = Status::IDLE;
}

void disconnect()
{
    Lock l;
    s_userIdle = true;
    s_autoScan = s_autoActive = s_autoTryNext = false;
    s_saveOnSuccess = false;
    if (s_status == Status::CONNECTED) s_disconnects++;
    driverDisconnect();
    if (s_status != Status::OFF && s_status != Status::ERROR) s_status = Status::IDLE;
}

void forgetAll()
{
    Lock l;
    disconnect();
    s_nets->clear();
    saveNets();
    MyLog::event(TAG, "all saved networks erased");
}

void savedCopy(NetList &out)
{
    Lock l;
    if (s_nets) out = *s_nets;
    else out.clear();
}

bool savedAdd(const char *ssid, const char *pass)
{
    if (!pass) pass = "";
    if (!validPass(pass, true)) return false;
    Lock l;
    if (s_nets->upsert(ssid, pass) < 0) return false;
    return s_hooks.saveNets ? s_hooks.saveNets(*s_nets) : true;
}

bool savedRemove(const char *ssid)
{
    Lock l;
    int i = s_nets->find(ssid);
    if (i < 0) return false;
    if ((s_status == Status::CONNECTED || s_status == Status::CONNECTING) && strcmp(s_trySsid, ssid) == 0) disconnect();
    s_nets->remove(i);
    MyLog::event(TAG, "forgot \"%s\"", ssid);
    return s_hooks.saveNets ? s_hooks.saveNets(*s_nets) : true;
}

bool savedMove(const char *ssid, int delta)
{
    Lock l;
    if (!s_nets->move(s_nets->find(ssid), delta)) return false;
    return s_hooks.saveNets ? s_hooks.saveNets(*s_nets) : true;
}

bool savedPassword(const char *ssid, char *out, size_t cap)
{
    Lock l;
    int i = s_nets->find(ssid);
    if (i < 0 || !out || !cap) return false;
    snprintf(out, cap, "%s", s_nets->items[i].pass);
    return true;
}

void requestHostnameUpdate() { s_hostnameDirty = true; }

static void readApInfo()
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return;
    if (ap.ssid[0]) snprintf(s_trySsid, sizeof(s_trySsid), "%s", (const char *)ap.ssid);
    memcpy(s_curBssid, ap.bssid, 6);
    s_curChannel = ap.primary;
}

void loop()
{
    if (!s_lock) return;
    Lock l;
    uint32_t now = millis();

    if (s_hostnameDirty) {
        s_hostnameDirty = false;
        if (s_netif && s_hooks.hostname) esp_netif_set_hostname(s_netif, s_hooks.hostname());
        if (connected()) startMdns();
    }

    if (s_scanDoneEvt) scanCollect();

    if (s_gotIp) {
        s_gotIp = false; s_lostIp = false; s_discEvent = false;
        esp_ip4_addr_t a;
        a.addr = s_newIp;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&a));
        bool wasRoam = s_roamAttempt;
        if (wasRoam) s_roams++;
        else if (s_status != Status::CONNECTED) {
            s_connects++;
            s_connectedAtMs = now;
        }
        s_status = Status::CONNECTED;
        s_lastFail = FailReason::NONE;
        s_bgAttempt = false;
        s_autoActive = s_autoTryNext = s_autoScan = false;
        s_roamAttempt = s_lossRecover = false;
        s_lastRoamScanMs = now;
        readApInfo();
        char bs[18];
        Roam::formatBssid(s_curBssid, bs);
        MyLog::event(TAG, "%s \"%s\" via %s ch %u, IP %s, RSSI %d dBm", wasRoam ? "roamed to" : "connected to", s_trySsid,
                     bs, s_curChannel, s_ip, rssi());
        if (s_saveOnSuccess) {
            s_saveOnSuccess = false;
            int i = s_nets->find(s_trySsid);
            if (i > 0) s_nets->move(i, -i);   // Chosen again by hand: move to the top
            s_nets->upsert(s_trySsid, s_tryPass);
            saveNets();
            ESP_LOGI(TAG, "credentials for \"%s\" saved", s_trySsid);
        }
        startMdns();
    }

    if (s_discEvent) {
        s_discEvent = false;
        uint8_t r = s_discReason;
        if (s_status == Status::CONNECTING) {
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
            MyLog::event(TAG, "connection lost (reason %u), driver rejoins", (unsigned)s_discReason);
            snprintf(s_ip, sizeof(s_ip), "0.0.0.0");
            s_status = Status::CONNECTING;
            s_attemptStartMs = s_lastAttemptMs = now;
            s_attemptId++;
            s_bgAttempt = true;
            s_lossRecover = true;
        }
    }

    if (s_status == Status::CONNECTING && s_lossRecover && now - s_attemptStartMs > LOSS_GRACE_MS) {
        ESP_LOGI(TAG, "rejoin slow, rescanning for the strongest AP");
        startAuto(true);
    }

    if (s_status == Status::CONNECTING && now - s_attemptStartMs > CONNECT_TIMEOUT_MS) {
        uint8_t r = s_discReason;
        FailReason fr = FailReason::TIMEOUT;
        if (r == WIFI_REASON_NO_AP_FOUND) fr = FailReason::NOT_FOUND;
        else if (r == WIFI_REASON_AUTH_FAIL || r == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT) fr = FailReason::WRONG_PASSWORD;
        bool wasRoam = s_roamAttempt;
        s_roamAttempt = false;
        failAttempt(fr);
        if (wasRoam) startAuto(true);
    }

    // A scan that never reports back is treated as failed.
    if (s_scanState == -2 && now - s_scanStartMs > 12000) {
        ESP_LOGW(TAG, "scan hung, giving up");
        esp_wifi_scan_stop();
        s_scanState = -1;
    }

    // Roaming: background scan while connected, interval by link quality.
    bool busy = s_hooks.busy && s_hooks.busy();
    if (s_status == Status::CONNECTED && s_scanState != -2 && !busy &&
        now - s_lastRoamScanMs > Roam::scanIntervalMs(-100) && now - s_lastRoamScanMs > Roam::scanIntervalMs(rssi())) {
        s_lastRoamScanMs = now;
        s_roamScan = scanStart(ROAM_DWELL_MS);
    }
    if (s_roamScan && s_scanState != -2) {
        s_roamScan = false;
        if (s_status == Status::CONNECTED && s_scanState > 0 && now - s_lastRoamMs > Roam::COOLDOWN_MS) {
            Roam::Ap aps[MAX_APS];
            int n = scannedAps(s_trySsid, aps, MAX_APS);
            int link = rssi();
            int t = Roam::pickTarget(aps, n, s_curBssid, link);
            if (t >= 0) {
                char from[18], to[18];
                Roam::formatBssid(s_curBssid, from);
                Roam::formatBssid(aps[t].bssid, to);
                ESP_LOGI(TAG, "roam: %s (%d dBm) -> %s ch %u (%d dBm)", from, link, to, aps[t].channel, aps[t].rssi);
                s_lastRoamMs = now;
                beginAttempt(true, false, &aps[t]);
                s_roamAttempt = true;
            }
        }
    }

    if (s_autoScan && s_scanState != -2) {
        s_autoScan = false;
        s_visibleCount = 0;
        if (s_scanState > 0) {
            Network tmp[MAX_NETWORKS];
            int n = scanResults(tmp, MAX_NETWORKS);
            for (int i = 0; i < n; i++) snprintf(s_visible[s_visibleCount++], 33, "%.32s", tmp[i].ssid);
        }
        s_autoNext = 0;
        autoTryNext();
    } else if (s_autoTryNext && s_status == Status::FAILED) {
        autoTryNext();
    }

    if ((s_status == Status::FAILED || s_status == Status::IDLE) && !s_userIdle && hasSavedCredentials() &&
        s_scanState != -2 && now - s_lastAttemptMs > RETRY_INTERVAL_MS) {
        startAuto(true);
    }

    if (s_status != s_reported) {
        s_reported = s_status;
        ESP_LOGI(TAG, "status: %s", statusText(s_reported));
        if (s_hooks.onStatus) s_hooks.onStatus(s_reported);
    }
}

Status status()
{
    Lock l;
    return s_status;
}
bool connected() { return s_status == Status::CONNECTED; }

void setScreenPowerSave(bool enabled)
{
    Lock l;
    s_screenPowerSave = enabled;
    if (s_status == Status::OFF || s_status == Status::ERROR) return;
    const esp_err_t err = esp_wifi_set_ps(enabled ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
    if (err != ESP_OK) ESP_LOGW(TAG, "screen power-save mode %s failed: %s",
                                enabled ? "enable" : "disable", esp_err_to_name(err));
}
bool mdnsOk() { return s_mdnsOk; }
const char *ipStr() { return connected() ? s_ip : "0.0.0.0"; }
uint32_t attemptId() { return s_attemptId; }

int rssi()
{
    if (!connected()) return 0;
    int r = 0;
    return esp_wifi_sta_get_rssi(&r) == ESP_OK ? r : 0;
}

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
    if (i.connected || s_status == Status::CONNECTING || s_status == Status::FAILED)
        snprintf(i.ssid, sizeof(i.ssid), "%s", s_trySsid);
    else snprintf(i.ssid, sizeof(i.ssid), "%s", savedSsid());
    snprintf(i.ip, sizeof(i.ip), "%s", ipStr());
    i.rssi = rssi();
    i.uptimeS = i.connected ? (millis() - s_connectedAtMs) / 1000 : 0;
    i.connects = s_connects;
    i.disconnects = s_disconnects;
    i.roams = s_roams;
    i.lastFail = s_lastFail;
    i.lastReason = s_discReason;
    if (i.connected) {
        Roam::formatBssid(s_curBssid, i.bssid);
        i.channel = s_curChannel;
    }
    uint8_t mac[6];
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) Roam::formatBssid(mac, i.mac);
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

bool validPass(const char *pass, bool allowEmpty)
{
    if (!pass) return false;
    size_t n = strlen(pass);
    if ((allowEmpty && n == 0) || (n >= 8 && n <= 63)) return true;
    if (n != 64) return false;
    for (size_t i = 0; i < n; i++)
        if (!isxdigit((unsigned char)pass[i])) return false;
    return true;
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
    if (s_status == Status::SEARCHING) return;   // Auto-connect scan running: shared result
    if (s_status == Status::CONNECTING && s_bgAttempt) {
        s_autoActive = false;
        failAttempt(FailReason::TIMEOUT);
    }
    scanStart(0);
}

int scanStatus() { return s_scanState; }

int scanResults(Network *out, int max)
{
    Lock l;
    int n = s_scanState;
    if (n <= 0 || !out || max <= 0) return 0;
    int count = 0;
    for (int i = 0; i < n; i++) {
        const char *ssid = (const char *)s_scan[i].ssid;
        if (!ssid[0]) continue;
        int r = s_scan[i].rssi;
        int found = -1;
        for (int k = 0; k < count; k++)
            if (strcmp(out[k].ssid, ssid) == 0) { found = k; break; }
        if (found >= 0) {
            if (r > out[found].rssi) { out[found].rssi = (int8_t)r; out[found].channel = s_scan[i].primary; }
            continue;
        }
        if (count >= max) continue;
        Network &nw = out[count++];
        snprintf(nw.ssid, sizeof(nw.ssid), "%s", ssid);
        nw.rssi = (int8_t)r;
        nw.channel = s_scan[i].primary;
        nw.secure = s_scan[i].authmode != WIFI_AUTH_OPEN;
    }
    for (int i = 1; i < count; i++) {
        Network t = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].rssi < t.rssi) { out[j + 1] = out[j]; j--; }
        out[j + 1] = t;
    }
    return count;
}

}  // namespace MyWifi
