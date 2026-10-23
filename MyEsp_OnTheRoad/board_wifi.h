// board_wifi.h - Wi-Fi in station (STA) mode only.
//
// - The device joins an existing access point (home router, phone hotspot ...). The
//   soft-AP of the first scaffold is disabled (deferred, see AP_MODE_ENABLED below).
// - connect() starts an attempt with a 15 s timeout. On success the SSID + password are
//   stored in NVS (Config) and used for auto-connect at the next boot.
// - Saved credentials are retried in the background (every 60 s) while not connected.
// - mDNS "<hostname>.local" (default "ontheroad") is announced once an IP is obtained.
// - Scans run asynchronously; results are de-duplicated and sorted by RSSI (strongest first).
//
// Threading: all functions may be called from the LVGL task, the web server task or
// loopTask. Driver calls that change the connection are deferred to loop() (loopTask).
#pragma once

#include <Arduino.h>
#include <IPAddress.h>

// The access point mode is postponed. Kept as a compile-time switch so it can come back.
#ifndef AP_MODE_ENABLED
#define AP_MODE_ENABLED 0
#endif

namespace Wifi {

constexpr uint32_t CONNECT_TIMEOUT_MS = 15000;
constexpr int MAX_NETWORKS = 24;

enum class Status : uint8_t {
    OFF = 0,       // Driver not started
    IDLE,          // Started, not connected, no attempt running
    CONNECTING,    // Attempt running (timeout CONNECT_TIMEOUT_MS)
    CONNECTED,     // Has an IP address
    FAILED,        // Last attempt failed / timed out (saved network retried in background)
    ERROR          // Driver could not be started
};

enum class FailReason : uint8_t { NONE = 0, TIMEOUT, WRONG_PASSWORD, NOT_FOUND, CANCELLED, OTHER };

struct Info {
    Status status;
    bool connected;
    char ssid[33];       // Connected network, or the one being tried / saved
    IPAddress ip;
    int rssi;            // dBm, 0 when not connected
    uint32_t uptimeS;    // Seconds since the current connection was established
    uint32_t connects;   // Successful connections since boot
    uint32_t disconnects;
    FailReason lastFail;
};

struct Network {
    char ssid[33];
    int8_t rssi;
    uint8_t channel;
    bool secure;
};

typedef void (*StatusCallback)(Status s);

// Starts the driver in WIFI_STA mode and auto-connects if NVS holds credentials.
// `cb` is called from loopTask on every status change.
bool begin(StatusCallback cb);
void loop();                       // Call regularly from loop()

bool hasSavedCredentials();
const char *savedSsid();
// Starts a connection attempt. Credentials are saved to NVS only if it succeeds.
// pass may be empty/nullptr for open networks. Returns false for invalid input.
bool connect(const char *ssid, const char *pass);
void reconnectSaved();             // Retry the stored network now
void cancelConnect();              // Aborts a running attempt (status -> IDLE)
void disconnect();                 // Drops the connection, keeps the saved network
void forget();                     // Drops the connection and erases saved credentials
void requestHostnameUpdate();      // Re-applies Config hostname to DHCP/mDNS (deferred to loop)

Status status();
Info info();
const char *statusText(Status s);
const char *failText(FailReason r);  // Swedish, for the UI
bool connected();
bool mdnsOk();
IPAddress ip();
int rssi();
uint32_t connectElapsedMs();       // Time since the running attempt started (0 if none)
uint32_t attemptId();              // Incremented by every connect()/reconnectSaved()

// RSSI -> 1..5 bars (>= -50: 5, >= -60: 4, >= -70: 3, >= -80: 2, else 1).
int signalBars(int rssi);

// Asynchronous scan. scanStatus(): -2 running, -1 none/failed, >= 0 networks available.
void startScan();
int scanStatus();
// Copies up to `max` de-duplicated networks, strongest first. Returns the count.
int scanResults(Network *out, int max);

}  // namespace Wifi
