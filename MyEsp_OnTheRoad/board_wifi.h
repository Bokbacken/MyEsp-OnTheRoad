// board_wifi.h - Wi-Fi in AP+STA dual mode (pattern from MyPi-Hotspot).
//
// - Soft-AP "OnTheRoad-XXXX" (last 4 hex digits of the MAC) is always on, WPA2 with the
//   password from NVS (default "yatzy1234"), IP 192.168.4.1
// - STA joins the network stored in NVS (if any), with periodic retries
// - mDNS "<hostname>.local" (default "ontheroad") on both interfaces
// - A status callback reports state changes (used to drive the RGB LED)
#pragma once

#include <Arduino.h>
#include <IPAddress.h>

namespace Wifi {

enum class Status : uint8_t {
    OFF = 0,
    AP_ONLY,         // AP up, no STA network configured
    STA_CONNECTING,  // AP up, STA trying to connect
    STA_CONNECTED,   // AP up, STA has an IP
    STA_FAILED,      // AP up, STA could not connect (keeps retrying)
    ERROR            // Wi-Fi driver / AP start failed
};

typedef void (*StatusCallback)(Status s);

bool begin(StatusCallback cb);
// Call regularly from loop(): handles retries, deferred reconnects and mDNS.
void loop();
// Thread-safe: asks loop() to re-apply STA credentials and AP password from Config.
void requestReconnect();

Status status();
const char *statusText(Status s);
bool apOk();
bool staConnected();
bool mdnsOk();
const char *apSsid();
IPAddress apIp();
IPAddress staIp();
int rssi();                 // dBm, 0 when not connected
int apClients();
String staSsid();           // SSID currently connected/configured
uint32_t staConnects();     // Successful connections since boot
uint32_t staDisconnects();

// Asynchronous network scan for the settings page.
// Returns -2 while scanning, -1 if no scan has been started, otherwise the result count.
int scanStatus();
void startScan();
bool scanResult(int i, String &ssid, int &rssi, bool &secure);

}  // namespace Wifi
