// board_webserver.cpp - HTTP server, static pages and JSON API (see header for routes).
#include "board_webserver.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <esp_chip_info.h>
#include <esp_system.h>

#include "board_config.h"
#include "board_display.h"
#include "board_touch.h"
#include "board_wifi.h"
#include "encoder_input.h"
#include "web_assets.h"

namespace Webserver {

static AsyncWebServer s_server(80);
static ConfigChangedCallback s_configCb = nullptr;
static bool s_started = false;

void onConfigChanged(ConfigChangedCallback cb) { s_configCb = cb; }

// ----------------------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------------------
static void sendAsset(AsyncWebServerRequest *req, const WebAsset &a)
{
    AsyncWebServerResponse *r = req->beginResponse(200, a.mime, a.data, a.len);
    r->addHeader("Content-Encoding", "gzip");
    r->addHeader("Cache-Control", "no-cache");
    req->send(r);
}

// Appends `s` as a JSON string literal (with quotes).
static void jsonStr(String &out, const char *s)
{
    out += '"';
    for (; s && *s; s++) {
        char ch = *s;
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if ((uint8_t)ch < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", (uint8_t)ch);
                out += buf;
            } else {
                out += ch;
            }
        }
    }
    out += '"';
}

static void kv(String &o, const char *key, const char *val, bool comma = true)
{
    jsonStr(o, key); o += ':'; jsonStr(o, val); if (comma) o += ',';
}
static void kv(String &o, const char *key, long val, bool comma = true)
{
    jsonStr(o, key); o += ':'; o += String(val); if (comma) o += ',';
}
static void kvb(String &o, const char *key, bool val, bool comma = true)
{
    jsonStr(o, key); o += ':'; o += val ? "true" : "false"; if (comma) o += ',';
}

static void sendJson(AsyncWebServerRequest *req, int code, const String &body)
{
    AsyncWebServerResponse *r = req->beginResponse(code, "application/json", body);
    r->addHeader("Cache-Control", "no-store");
    req->send(r);
}

static void sendError(AsyncWebServerRequest *req, int code, const char *msg)
{
    String o = "{\"ok\":false,";
    kv(o, "error", msg, false);
    o += '}';
    sendJson(req, code, o);
}

static const AsyncWebParameter *param(AsyncWebServerRequest *req, const char *name)
{
    if (req->hasParam(name, true)) return req->getParam(name, true);
    if (req->hasParam(name)) return req->getParam(name);
    return nullptr;
}

static bool paramInt(AsyncWebServerRequest *req, const char *name, long lo, long hi, long &out, bool &bad)
{
    const AsyncWebParameter *p = param(req, name);
    if (!p) return false;
    char *end = nullptr;
    long v = strtol(p->value().c_str(), &end, 10);
    if (!end || *end || p->value().isEmpty() || v < lo || v > hi) { bad = true; return false; }
    out = v;
    return true;
}

static bool paramBool(AsyncWebServerRequest *req, const char *name, bool &out)
{
    const AsyncWebParameter *p = param(req, name);
    if (!p) return false;
    const String &v = p->value();
    out = v == "1" || v == "true" || v == "on";
    return true;
}

// ----------------------------------------------------------------------------------------
// /api/info
// ----------------------------------------------------------------------------------------
static void handleInfo(AsyncWebServerRequest *req)
{
    String o;
    o.reserve(1400);
    o += '{';
    kv(o, "fw", FW_NAME);
    kv(o, "version", FW_VERSION);
    kv(o, "build", __DATE__ " " __TIME__);
    kv(o, "uptime_s", (long)(millis() / 1000));
    kv(o, "heap_free", (long)ESP.getFreeHeap());
    kv(o, "heap_min", (long)ESP.getMinFreeHeap());
    kv(o, "heap_size", (long)ESP.getHeapSize());
    kv(o, "psram_free", (long)ESP.getFreePsram());
    kv(o, "psram_size", (long)ESP.getPsramSize());
    kv(o, "flash_size", (long)ESP.getFlashChipSize());
    kv(o, "sketch_size", (long)ESP.getSketchSize());
    kv(o, "sketch_free", (long)ESP.getFreeSketchSpace());
    kv(o, "cpu_mhz", (long)ESP.getCpuFreqMHz());
    kv(o, "chip", ESP.getChipModel());
    kv(o, "chip_rev", (long)ESP.getChipRevision());
    kv(o, "sdk", ESP.getSdkVersion());
    kv(o, "reset_reason", (long)esp_reset_reason());

    // Wi-Fi
    const AppConfig &cfg = Config::get();
    o += "\"wifi\":{";
    kv(o, "status", Wifi::statusText(Wifi::status()));
    kv(o, "ap_ssid", Wifi::apSsid());
    kv(o, "ap_ip", Wifi::apIp().toString().c_str());
    kv(o, "ap_clients", Wifi::apClients());
    kv(o, "sta_ssid", Wifi::staSsid().c_str());
    kv(o, "sta_ip", Wifi::staIp().toString().c_str());
    kv(o, "rssi", Wifi::rssi());
    kv(o, "connects", (long)Wifi::staConnects());
    kv(o, "disconnects", (long)Wifi::staDisconnects());
    kv(o, "hostname", cfg.hostname);
    kvb(o, "mdns", Wifi::mdnsOk(), false);
    o += "},";

    // Display
    Display::Stats ds = Display::stats();
    o += "\"display\":{";
    kvb(o, "ok", Display::ok());
    kv(o, "width", 320);
    kv(o, "height", 480);
    kv(o, "fps_x10", (long)(ds.fps * 10));
    kv(o, "flushes", (long)ds.flushes);
    kv(o, "frames", (long)ds.frames);
    kvb(o, "te_active", ds.teActive);
    kv(o, "te_edges", (long)ds.teEdges);
    kv(o, "te_timeouts", (long)ds.teTimeouts);
    kv(o, "buf_bytes", (long)ds.bufBytes);
    kvb(o, "buf_psram", ds.bufInPsram);
    kv(o, "brightness", cfg.brightness, false);
    o += "},";

    // Touch
    Touch::Info ti = Touch::info();
    char fw[24];
    snprintf(fw, sizeof(fw), "%u (%u.%u.%u.%u)", ti.fwVersion, ti.fwRevision[0], ti.fwRevision[1],
             ti.fwRevision[2], ti.fwRevision[3]);
    o += "\"touch\":{";
    kvb(o, "present", ti.present);
    kv(o, "fw", fw);
    kv(o, "max_x", ti.maxX);
    kv(o, "max_y", ti.maxY);
    kv(o, "max_points", ti.maxTouches);
    kvb(o, "pressed", ti.pressed);
    kv(o, "last_x", ti.lastX);
    kv(o, "last_y", ti.lastY);
    kv(o, "touches", (long)ti.touchCount);
    kv(o, "irqs", (long)ti.irqCount);
    kv(o, "errors", (long)ti.readErrors, false);
    o += "},";

    // I2C scan and encoder
    uint8_t addrs[16];
    int n = Touch::scanResults(addrs, 16);
    o += "\"i2c\":[";
    for (int i = 0; i < n; i++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "0x%02X", addrs[i]);
        jsonStr(o, buf);
        if (i + 1 < n) o += ',';
    }
    o += "],";
    o += "\"encoder\":{";
    kvb(o, "active", Encoder::active());
    kv(o, "position", Encoder::position(), false);
    o += "}}";
    sendJson(req, 200, o);
}

// ----------------------------------------------------------------------------------------
// /api/config
// ----------------------------------------------------------------------------------------
static void configJson(String &o)
{
    const AppConfig &c = Config::get();
    o += '{';
    kv(o, "hostname", c.hostname);
    kv(o, "sta_ssid", c.staSsid);
    kvb(o, "sta_pass_set", c.staPass[0] != 0);
    kvb(o, "ap_pass_custom", strcmp(c.apPass, "yatzy1234") != 0);
    kv(o, "ap_ssid", Wifi::apSsid());
    kv(o, "brightness", c.brightness);
    kv(o, "led_brightness", c.ledBrightness);
    kvb(o, "led_enabled", c.ledEnabled);
    kv(o, "default_variant", c.defaultVariant);
    kv(o, "roll_anim_ms", c.rollAnimMs);
    kvb(o, "maxi_saved_rolls", c.maxiSavedRolls, false);
    o += '}';
}

static void handleConfigGet(AsyncWebServerRequest *req)
{
    String o;
    configJson(o);
    sendJson(req, 200, o);
}

static void handleConfigPost(AsyncWebServerRequest *req)
{
    AppConfig next = Config::get();
    bool bad = false, wifiChanged = false;
    long v;
    bool b;
    if (paramInt(req, "brightness", 5, 100, v, bad)) next.brightness = (uint8_t)v;
    if (paramInt(req, "led_brightness", 0, 255, v, bad)) next.ledBrightness = (uint8_t)v;
    if (paramInt(req, "default_variant", 0, 3, v, bad)) next.defaultVariant = (uint8_t)v;
    if (paramInt(req, "roll_anim_ms", 300, 2000, v, bad)) next.rollAnimMs = (uint16_t)v;
    if (bad) return sendError(req, 400, "Ogiltigt numeriskt värde");
    if (paramBool(req, "led_enabled", b)) next.ledEnabled = b;
    if (paramBool(req, "maxi_saved_rolls", b)) next.maxiSavedRolls = b;
    if (const AsyncWebParameter *p = param(req, "hostname")) {
        if (!Config::validHostname(p->value().c_str())) return sendError(req, 400, "Ogiltigt värdnamn");
        if (strcmp(next.hostname, p->value().c_str())) {
            snprintf(next.hostname, sizeof(next.hostname), "%s", p->value().c_str());
            wifiChanged = true;
        }
    }
    if (const AsyncWebParameter *p = param(req, "ap_pass")) {
        if (!p->value().isEmpty()) {   // Empty = keep current
            if (!Config::validWifiPass(p->value().c_str(), false))
                return sendError(req, 400, "AP-lösenordet måste vara 8-63 tecken");
            snprintf(next.apPass, sizeof(next.apPass), "%s", p->value().c_str());
            wifiChanged = true;
        }
    }
    Config::get() = next;
    if (!Config::save()) return sendError(req, 500, "Kunde inte spara i NVS");
    if (s_configCb) s_configCb();
    if (wifiChanged) Wifi::requestReconnect();
    String o = "{\"ok\":true,\"config\":";
    configJson(o);
    o += '}';
    sendJson(req, 200, o);
}

// ----------------------------------------------------------------------------------------
// /api/wifi, /api/scan, /api/reboot, /api/reset
// ----------------------------------------------------------------------------------------
static void handleWifiPost(AsyncWebServerRequest *req)
{
    const AsyncWebParameter *ssid = param(req, "ssid");
    const AsyncWebParameter *pass = param(req, "pass");
    if (!ssid) return sendError(req, 400, "ssid saknas");
    String s = ssid->value();
    s.trim();
    if (s.length() > 32) return sendError(req, 400, "SSID får vara högst 32 tecken");
    AppConfig &c = Config::get();
    // Keep the stored password when the field is left empty for the same network.
    bool keepPass = (!pass || pass->value().isEmpty()) && s == c.staSsid && param(req, "keep_pass");
    if (!keepPass) {
        const char *pw = pass ? pass->value().c_str() : "";
        if (!Config::validWifiPass(pw, true)) return sendError(req, 400, "Lösenordet måste vara tomt eller 8-63 tecken");
        snprintf(c.staPass, sizeof(c.staPass), "%s", pw);
    }
    snprintf(c.staSsid, sizeof(c.staSsid), "%s", s.c_str());
    if (!Config::save()) return sendError(req, 500, "Kunde inte spara i NVS");
    Wifi::requestReconnect();
    String o = "{\"ok\":true,";
    kv(o, "sta_ssid", c.staSsid, false);
    o += '}';
    sendJson(req, 200, o);
}

static void handleScan(AsyncWebServerRequest *req)
{
    int st = Wifi::scanStatus();
    if (req->hasParam("start") || st == -1) {
        Wifi::startScan();
        return sendJson(req, 202, "{\"scanning\":true,\"networks\":[]}");
    }
    if (st == -2) return sendJson(req, 202, "{\"scanning\":true,\"networks\":[]}");
    String o = "{\"scanning\":false,\"networks\":[";
    for (int i = 0; i < st && i < 30; i++) {
        String ssid;
        int rssi;
        bool secure;
        if (!Wifi::scanResult(i, ssid, rssi, secure)) break;
        if (i) o += ',';
        o += '{';
        kv(o, "ssid", ssid.c_str());
        kv(o, "rssi", rssi);
        kvb(o, "secure", secure, false);
        o += '}';
    }
    o += "]}";
    sendJson(req, 200, o);
}

static void rebootSoon()
{
    // Give the TCP stack time to deliver the response.
    xTaskCreate([](void *) { vTaskDelay(pdMS_TO_TICKS(600)); ESP.restart(); }, "reboot", 2048,
                nullptr, 1, nullptr);
}

bool begin()
{
    if (s_started) return true;
    s_server.on("/", HTTP_GET, [](AsyncWebServerRequest *r) { sendAsset(r, WEB_INDEX_HTML); });
    s_server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *r) { sendAsset(r, WEB_INDEX_HTML); });
    s_server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *r) { sendAsset(r, WEB_SETTINGS_HTML); });
    s_server.on("/settings.html", HTTP_GET, [](AsyncWebServerRequest *r) { sendAsset(r, WEB_SETTINGS_HTML); });
    s_server.on("/diag", HTTP_GET, [](AsyncWebServerRequest *r) { sendAsset(r, WEB_DIAGNOSTICS_HTML); });
    s_server.on("/diagnostics.html", HTTP_GET, [](AsyncWebServerRequest *r) { sendAsset(r, WEB_DIAGNOSTICS_HTML); });
    s_server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *r) { sendAsset(r, WEB_STYLE_CSS); });

    s_server.on("/api/info", HTTP_GET, handleInfo);
    s_server.on("/api/config", HTTP_GET, handleConfigGet);
    s_server.on("/api/config", HTTP_POST, handleConfigPost);
    s_server.on("/api/wifi", HTTP_POST, handleWifiPost);
    s_server.on("/api/scan", HTTP_GET, handleScan);
    s_server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *r) {
        sendJson(r, 200, "{\"ok\":true}");
        rebootSoon();
    });
    s_server.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *r) {
        Config::resetToDefaults();
        sendJson(r, 200, "{\"ok\":true}");
        rebootSoon();
    });
    s_server.onNotFound([](AsyncWebServerRequest *r) {
        if (r->url().startsWith("/api/")) return sendError(r, 404, "Okänd endpoint");
        // Captive-portal style: anything else goes to the dashboard.
        r->redirect("/");
    });
    s_server.begin();
    s_started = true;
    Serial.println("[web] server started on port 80");
    return true;
}

}  // namespace Webserver
