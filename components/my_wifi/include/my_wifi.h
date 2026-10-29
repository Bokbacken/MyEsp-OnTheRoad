// my_wifi.h - Wi-Fi station (STA) manager, shared MyEsp component (ESP-IDF, esp_wifi).
//
// - Joins existing access points only (no soft-AP).
// - Up to NET_LIST_MAX saved networks in priority order (net_list.h). The list is owned here;
//   the application persists it through the load/save hooks (e.g. one NVS blob).
// - Auto-connect (boot, background retry every 60 s, reconnectSaved()): scan, then try the
//   visible saved networks in priority order, each pinned to its strongest AP (BSSID).
// - connect() starts a manual attempt with a 15 s timeout; on success the network is saved
//   and moved to the top.
// - Roaming: background scans while connected (interval by link quality, roam_policy.h);
//   reconnects to a clearly stronger AP of the same SSID.
// - mDNS "<hostname>.local" + _http._tcp once an IP is obtained.
//
// Threading: every function may be called from any task (recursive lock). Driver calls that
// change the connection happen in loop(); call it every ~20 ms from one task.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "net_list.h"

namespace MyWifi {

constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
constexpr int MAX_NETWORKS = 24;   // De-duplicated scan results kept

enum class Status : uint8_t {
    OFF = 0,       // Driver not started
    IDLE,          // Started, not connected, no attempt running
    SEARCHING,     // Auto-connect: scanning for saved networks
    CONNECTING,    // Attempt running (timeout CONNECT_TIMEOUT_MS)
    CONNECTED,     // Has an IP address
    FAILED,        // Last attempt failed (saved networks retried in background)
    ERROR          // Driver could not be started
};

enum class FailReason : uint8_t { NONE = 0, TIMEOUT, WRONG_PASSWORD, NOT_FOUND, CANCELLED, OTHER };

struct Info {
    Status status;
    bool connected;
    char ssid[33];       // Connected network, or the one being tried / saved
    char ip[16];         // "192.168.1.10", "0.0.0.0" when not connected
    int rssi;            // dBm, 0 when not connected
    uint32_t uptimeS;    // Seconds since the current connection was established
    uint32_t connects;   // Successful connections since boot
    uint32_t disconnects;
    uint32_t roams;      // Moves to a stronger AP (same SSID) since boot
    FailReason lastFail;
    uint8_t lastReason;  // Last driver disconnect reason (wifi_err_reason_t)
    char bssid[18];      // Connected AP "AA:BB:..", empty when not connected
    uint8_t channel;     // Connected AP channel, 0 when not connected
    char mac[18];        // Own STA MAC
};

struct Network {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    bool secure;
};

typedef void (*StatusCallback)(Status s);

struct Hooks {
    const char *(*hostname)() = nullptr;          // DHCP + mDNS name (required)
    bool (*loadNets)(NetList &out) = nullptr;     // Called once in begin()
    bool (*saveNets)(const NetList &nets) = nullptr;
    bool (*busy)() = nullptr;                     // True = skip background scans (e.g. OTA running)
    StatusCallback onStatus = nullptr;            // Called from loop() on every status change
    const char *mdnsTxtFw = nullptr;              // TXT "fw" value of the _http service
};

// Starts the driver (esp_netif, default event loop, STA) and auto-connects to saved networks.
bool begin(const Hooks &hooks);
void loop();

bool hasSavedCredentials();
const char *savedSsid();           // Network being tried/connected, else the top saved one
bool connect(const char *ssid, const char *pass);
void reconnectSaved();
void cancelConnect();
void disconnect();                 // Drops the connection, keeps the saved networks
void setScreenPowerSave(bool enabled);  // Minimum modem sleep while the display is off

// Saved-network list (all persist through Hooks::saveNets).
void savedCopy(NetList &out);      // Contains passwords - never send them to the web
bool savedAdd(const char *ssid, const char *pass);
bool savedRemove(const char *ssid);
bool savedMove(const char *ssid, int delta);
bool savedPassword(const char *ssid, char *out, size_t cap);
int savedCount();
void forgetAll();
void requestHostnameUpdate();      // Re-applies Hooks::hostname to DHCP/mDNS (deferred to loop)

Status status();
Info info();
const char *statusText(Status s);  // English, for logs/JSON
const char *failText(FailReason r);  // Swedish, for the UI
bool connected();
bool mdnsOk();
const char *ipStr();               // Static buffer, "0.0.0.0" when not connected
int rssi();
uint32_t connectElapsedMs();
uint32_t attemptId();

// Empty (open network), 8..63 printable characters, or 64 hex digits.
bool validPass(const char *p, bool allowEmpty);
int signalBars(int rssi);          // 1..5

// Asynchronous scan. scanStatus(): -2 running, -1 none/failed, >= 0 APs available.
void startScan();
int scanStatus();
int scanResults(Network *out, int max);   // De-duplicated, strongest first

}  // namespace MyWifi
