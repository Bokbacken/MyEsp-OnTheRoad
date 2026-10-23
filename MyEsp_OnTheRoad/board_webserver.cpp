// board_webserver.cpp - HTTP server, static pages and JSON API (see header for routes).
#include "board_webserver.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <esp_chip_info.h>
#include <esp_system.h>

#include "board_clock.h"
#include "board_config.h"
#include "board_display.h"
#include "board_ota.h"
#include "board_players.h"
#include "board_touch.h"
#include "board_wifi.h"
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

// Admin PIN from the "pin" parameter (query or form) or the X-Admin-Pin header.
static bool adminAllowed(AsyncWebServerRequest *req)
{
    const AsyncWebParameter *p = param(req, "pin");
    if (p) return Config::adminOk(p->value().c_str());
    if (req->hasHeader("X-Admin-Pin")) return Config::adminOk(req->header("X-Admin-Pin").c_str());
    return Config::adminOk("");
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

    // Wi-Fi (station mode)
    const AppConfig &cfg = Config::get();
    Wifi::Info wi = Wifi::info();
    o += "\"wifi\":{";
    kv(o, "mode", "sta");
    kv(o, "status", Wifi::statusText(wi.status));
    kvb(o, "connected", wi.connected);
    kv(o, "ssid", wi.ssid);
    kv(o, "saved_ssid", Wifi::savedSsid());
    kv(o, "ip", wi.ip.toString().c_str());
    kv(o, "rssi", wi.rssi);
    kv(o, "bars", wi.connected ? Wifi::signalBars(wi.rssi) : 0);
    kv(o, "uptime_s", (long)wi.uptimeS);
    kv(o, "connects", (long)wi.connects);
    kv(o, "disconnects", (long)wi.disconnects);
    kv(o, "last_error", Wifi::failText(wi.lastFail));
    kv(o, "mac", WiFi.macAddress().c_str());
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

    // Clock
    o += "\"clock\":{";
    kv(o, "epoch", (long)Clock::now());
    kv(o, "source", Clock::sourceText(Clock::source()));
    kvb(o, "certain", Clock::certain());
    kv(o, "tz", cfg.tz);
    uint32_t ntpAge = Clock::lastNtpSyncS();
    kv(o, "ntp_age_s", ntpAge == UINT32_MAX ? -1L : (long)ntpAge, false);
    o += "},";

    // OTA slots
    o += "\"ota\":{";
    kv(o, "running", Ota::runningPartition());
    kv(o, "next", Ota::nextPartition());
    kv(o, "slot_size", (long)Ota::slotSize(), false);
    o += "},";

    // Storage (FFat) + player registry
    o += "\"storage\":{";
    kvb(o, "ok", Players::ok());
    kv(o, "total", (long)Players::fsTotalBytes());
    kv(o, "used", (long)Players::fsUsedBytes());
    kv(o, "players", (long)Players::count(), false);
    o += "},";

    // I2C scan
    uint8_t addrs[16];
    int n = Touch::scanResults(addrs, 16);
    o += "\"i2c\":[";
    for (int i = 0; i < n; i++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "0x%02X", addrs[i]);
        jsonStr(o, buf);
        if (i + 1 < n) o += ',';
    }
    o += "]}";
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
    kv(o, "nets_count", Config::nets().count);
    kv(o, "nets_max", NET_LIST_MAX);
    kvb(o, "admin_pin_set", c.adminPin[0] != 0);
    kv(o, "tz", c.tz);
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
    bool bad = false, wifiChanged = false, tzChanged = false;
    long v;
    bool b;
    if (paramInt(req, "brightness", 5, 100, v, bad)) next.brightness = (uint8_t)v;
    if (paramInt(req, "led_brightness", 0, 255, v, bad)) next.ledBrightness = (uint8_t)v;
    if (paramInt(req, "default_variant", 0, 3, v, bad)) next.defaultVariant = (uint8_t)v;
    if (paramInt(req, "roll_anim_ms", 300, 2000, v, bad)) next.rollAnimMs = (uint16_t)v;
    if (bad) return sendError(req, 400, "Ogiltigt numeriskt värde");
    if (paramBool(req, "led_enabled", b)) next.ledEnabled = b;
    if (paramBool(req, "maxi_saved_rolls", b)) next.maxiSavedRolls = b;
    if (const AsyncWebParameter *p = param(req, "tz")) {
        if (!Config::validTz(p->value().c_str())) return sendError(req, 400, "Ogiltig tidszon");
        if (strcmp(next.tz, p->value().c_str())) {
            snprintf(next.tz, sizeof(next.tz), "%s", p->value().c_str());
            tzChanged = true;
        }
    }
    if (const AsyncWebParameter *p = param(req, "admin_pin_new")) {
        if (!adminAllowed(req)) return sendError(req, 403, "Fel admin-PIN");
        if (!Config::validAdminPin(p->value().c_str(), true))
            return sendError(req, 400, "Admin-PIN måste vara 4-8 siffror (tomt = ingen PIN)");
        snprintf(next.adminPin, sizeof(next.adminPin), "%s", p->value().c_str());
    }
    if (const AsyncWebParameter *p = param(req, "hostname")) {
        if (!Config::validHostname(p->value().c_str())) return sendError(req, 400, "Ogiltigt värdnamn");
        if (strcmp(next.hostname, p->value().c_str())) {
            snprintf(next.hostname, sizeof(next.hostname), "%s", p->value().c_str());
            wifiChanged = true;
        }
    }
    Config::get() = next;
    if (!Config::save()) return sendError(req, 500, "Kunde inte spara i NVS");
    if (s_configCb) s_configCb();
    if (wifiChanged) Wifi::requestHostnameUpdate();
    if (tzChanged) Clock::applyTz();
    String o = "{\"ok\":true,\"config\":";
    configJson(o);
    o += '}';
    sendJson(req, 200, o);
}

// ----------------------------------------------------------------------------------------
// /api/wifi, /api/scan, /api/reboot, /api/reset
// ----------------------------------------------------------------------------------------
// ssid + pass: start a connection attempt (the network is saved / moved up only on success).
// keep_pass=1 with an empty pass reuses the stored password of that saved network.
static void handleWifiPost(AsyncWebServerRequest *req)
{
    const AsyncWebParameter *ssid = param(req, "ssid");
    const AsyncWebParameter *pass = param(req, "pass");
    if (!ssid) return sendError(req, 400, "ssid saknas");
    String s = ssid->value();
    s.trim();
    if (s.isEmpty()) return sendError(req, 400, "ssid saknas");
    if (s.length() > 32) return sendError(req, 400, "SSID får vara högst 32 tecken");
    String pw = pass ? pass->value() : String();
    if (pw.isEmpty() && param(req, "keep_pass")) {
        char stored[65];
        if (Wifi::savedPassword(s.c_str(), stored, sizeof(stored))) pw = stored;
        memset(stored, 0, sizeof(stored));
    }
    if (!Config::validWifiPass(pw.c_str(), true)) return sendError(req, 400, "Lösenordet måste vara tomt eller 8-63 tecken");
    if (!Wifi::connect(s.c_str(), pw.c_str())) return sendError(req, 500, "Wi-Fi är inte startat");
    String o = "{\"ok\":true,\"connecting\":true,";
    kv(o, "ssid", s.c_str(), false);
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
    static Wifi::Network nets[Wifi::MAX_NETWORKS];   // async_tcp task only
    int n = Wifi::scanResults(nets, Wifi::MAX_NETWORKS);
    String o = "{\"scanning\":false,\"networks\":[";
    for (int i = 0; i < n; i++) {
        if (i) o += ',';
        o += '{';
        kv(o, "ssid", nets[i].ssid);
        kv(o, "rssi", nets[i].rssi);
        kv(o, "bars", Wifi::signalBars(nets[i].rssi));
        kv(o, "channel", nets[i].channel);
        kvb(o, "secure", nets[i].secure, false);
        o += '}';
    }
    o += "]}";
    sendJson(req, 200, o);
}

// GET /api/nets: saved networks in priority order (never the passwords).
static void handleNetsGet(AsyncWebServerRequest *req)
{
    static NetList list;   // async_tcp task only (5 kB, keep off the stack)
    Wifi::savedCopy(list);
    Wifi::Info wi = Wifi::info();
    String o;
    o.reserve(80 + list.count * 64);
    o += "{\"max\":";
    o += NET_LIST_MAX;
    o += ",\"nets\":[";
    for (int i = 0; i < list.count; i++) {
        if (i) o += ',';
        o += '{';
        kv(o, "ssid", list.items[i].ssid);
        kvb(o, "secure", list.items[i].pass[0] != 0);
        kvb(o, "connected", wi.connected && strcmp(wi.ssid, list.items[i].ssid) == 0, false);
        o += '}';
    }
    o += "]}";
    memset(&list, 0, sizeof(list));   // Do not leave passwords lying around
    sendJson(req, 200, o);
}

// POST /api/nets action=add (ssid, pass) | remove (ssid) | move (ssid, delta) | forget_all
static void handleNetsPost(AsyncWebServerRequest *req)
{
    const AsyncWebParameter *a = param(req, "action");
    if (!a) return sendError(req, 400, "action saknas");
    const String &act = a->value();
    if (act == "forget_all") {
        Wifi::forgetAll();
        return sendJson(req, 200, "{\"ok\":true}");
    }
    const AsyncWebParameter *ssid = param(req, "ssid");
    if (!ssid || ssid->value().isEmpty() || ssid->value().length() > 32) return sendError(req, 400, "Ogiltigt SSID");
    const char *s = ssid->value().c_str();
    bool ok = false;
    if (act == "add") {
        const AsyncWebParameter *pass = param(req, "pass");
        String pw = pass ? pass->value() : String();
        if (!Config::validWifiPass(pw.c_str(), true)) return sendError(req, 400, "Lösenordet måste vara tomt eller 8-63 tecken");
        ok = Wifi::savedAdd(s, pw.c_str());
    } else if (act == "remove") {
        ok = Wifi::savedRemove(s);
    } else if (act == "move") {
        long d;
        bool bad = false;
        if (!paramInt(req, "delta", -NET_LIST_MAX, NET_LIST_MAX, d, bad)) return sendError(req, 400, "delta saknas");
        ok = Wifi::savedMove(s, (int)d);
    } else {
        return sendError(req, 400, "Okänd action");
    }
    if (!ok) return sendError(req, 404, "Nätverket finns inte / kunde inte sparas");
    handleNetsGet(req);
}

// POST /api/time epoch=<UTC seconds>: set the clock manually (e.g. from the browser).
static void handleTimePost(AsyncWebServerRequest *req)
{
    const AsyncWebParameter *p = param(req, "epoch");
    if (!p) return sendError(req, 400, "epoch saknas");
    long long e = atoll(p->value().c_str());
    if (!Clock::setManual((time_t)e)) return sendError(req, 400, "Ogiltig tid");
    sendJson(req, 200, "{\"ok\":true}");
}

// ----------------------------------------------------------------------------------------
// /api/players - registry of up to 200 players (board_players)
// ----------------------------------------------------------------------------------------
// GET: {"ok":true,"count":n,"max":200,"fs_ok":b,"players":[{"id","name","created"}...]} sorted by name.
static void handlePlayersGet(AsyncWebServerRequest *req)
{
    PlayerDb *db = (PlayerDb *)heap_caps_malloc(sizeof(PlayerDb), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!db) db = (PlayerDb *)malloc(sizeof(PlayerDb));
    if (!db) return sendError(req, 500, "Minnet räcker inte");
    Players::snapshot(*db);
    uint8_t idx[PLAYER_MAX];
    int n = db->sortedIndices(idx);
    String o;
    o.reserve(80 + n * 56);
    o += "{\"ok\":true,";
    kv(o, "count", (long)n);
    kv(o, "max", (long)PLAYER_MAX);
    kvb(o, "fs_ok", Players::ok());
    o += "\"players\":[";
    for (int k = 0; k < n; k++) {
        const PlayerRec &r = db->items[idx[k]];
        o += '{';
        kv(o, "id", (long)r.id);
        kv(o, "name", r.name);
        kv(o, "created", (long)r.created, false);
        o += k + 1 < n ? "}," : "}";
    }
    o += "]}";
    free(db);
    sendJson(req, 200, o);
}

// POST action=add (name) | rename (id, name) | remove (id; admin PIN). Answers with the new list.
static void handlePlayersPost(AsyncWebServerRequest *req)
{
    const AsyncWebParameter *a = param(req, "action");
    if (!a) return sendError(req, 400, "action saknas");
    const String &act = a->value();
    const AsyncWebParameter *nm = param(req, "name");
    long id = 0;
    bool bad = false;
    int r;
    if (act == "add") {
        if (!nm) return sendError(req, 400, "name saknas");
        r = Players::add(nm->value().c_str());
        if (r > 0) r = 0;
    } else if (act == "rename") {
        if (!nm || !paramInt(req, "id", 1, 65535, id, bad)) return sendError(req, 400, "id/name saknas");
        r = Players::rename((uint16_t)id, nm->value().c_str());
    } else if (act == "remove") {
        if (!paramInt(req, "id", 1, 65535, id, bad)) return sendError(req, 400, "id saknas");
        if (!adminAllowed(req)) return sendError(req, 403, "Fel admin-PIN");
        r = Players::remove((uint16_t)id);
    } else {
        return sendError(req, 400, "Okänd action");
    }
    if (r < 0) return sendError(req, r == (int)PlayerErr::NOT_FOUND ? 404 : 400, playerErrText(r));
    handlePlayersGet(req);
}

static void rebootSoon();

// POST /api/ota (multipart, field "fw"; admin PIN as ?pin=). Streams into the inactive slot.
static void handleOtaUpload(AsyncWebServerRequest *req, const String &filename, size_t index, uint8_t *data,
                            size_t len, bool final)
{
    if (index == 0) {
        req->_tempObject = nullptr;
        if (!adminAllowed(req)) { req->_tempObject = strdup("Fel admin-PIN"); return; }
        if (Ota::busy()) { req->_tempObject = strdup("En uppdatering pågår redan"); return; }
        long total = 0;
        if (const AsyncWebParameter *sz = param(req, "size")) total = atol(sz->value().c_str());
        Serial.printf("[ota] upload %s (%ld bytes)\n", filename.c_str(), total);
        if (!Ota::begin(total > 0 ? (uint32_t)total : 0)) {
            req->_tempObject = strdup("Kunde inte starta uppdateringen");
            return;
        }
    }
    if (req->_tempObject) return;   // Rejected: swallow the rest
    if (len && !Ota::write(data, len)) { req->_tempObject = strdup("Skrivfel"); return; }
    if (final && !Ota::end()) req->_tempObject = strdup("Verifieringen misslyckades");
}

static void handleOtaDone(AsyncWebServerRequest *req)
{
    const char *err = (const char *)req->_tempObject;
    // _tempObject (strdup'ed) is freed by the request destructor.
    Ota::State st = Ota::state();
    if (err || st.phase != Ota::Phase::DONE) {
        String msg = err ? err : "Ingen fil mottogs";
        if (st.phase == Ota::Phase::FAILED && st.error[0]) { msg += ": "; msg += st.error; }
        if (Ota::busy()) Ota::abort("Uppladdningen avbröts");
        return sendError(req, strcmp(err ? err : "", "Fel admin-PIN") == 0 ? 403 : 400, msg.c_str());
    }
    String o = "{\"ok\":true,";
    kv(o, "bytes", (long)st.written, false);
    o += '}';
    sendJson(req, 200, o);
    rebootSoon();
}

static void rebootSoon()
{
    // Give the TCP stack time to deliver the response (and the screen time to show "Klart").
    xTaskCreate([](void *) { vTaskDelay(pdMS_TO_TICKS(1500)); ESP.restart(); }, "reboot", 2048,
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
    s_server.on("/update", HTTP_GET, [](AsyncWebServerRequest *r) { sendAsset(r, WEB_UPDATE_HTML); });
    s_server.on("/update.html", HTTP_GET, [](AsyncWebServerRequest *r) { sendAsset(r, WEB_UPDATE_HTML); });
    s_server.on("/players", HTTP_GET, [](AsyncWebServerRequest *r) { sendAsset(r, WEB_PLAYERS_HTML); });
    s_server.on("/players.html", HTTP_GET, [](AsyncWebServerRequest *r) { sendAsset(r, WEB_PLAYERS_HTML); });
    s_server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *r) { sendAsset(r, WEB_STYLE_CSS); });

    s_server.on("/api/info", HTTP_GET, handleInfo);
    s_server.on("/api/config", HTTP_GET, handleConfigGet);
    s_server.on("/api/config", HTTP_POST, handleConfigPost);
    s_server.on("/api/wifi", HTTP_POST, handleWifiPost);
    s_server.on("/api/scan", HTTP_GET, handleScan);
    s_server.on("/api/nets", HTTP_GET, handleNetsGet);
    s_server.on("/api/nets", HTTP_POST, handleNetsPost);
    s_server.on("/api/time", HTTP_POST, handleTimePost);
    s_server.on("/api/players", HTTP_GET, handlePlayersGet);
    s_server.on("/api/players", HTTP_POST, handlePlayersPost);
    s_server.on("/api/ota", HTTP_POST, handleOtaDone, handleOtaUpload);
    s_server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *r) {
        sendJson(r, 200, "{\"ok\":true}");
        rebootSoon();
    });
    s_server.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *r) {
        if (!adminAllowed(r)) return sendError(r, 403, "Fel admin-PIN");
        Config::resetToDefaults();
        sendJson(r, 200, "{\"ok\":true}");
        rebootSoon();
    });
    s_server.onNotFound([](AsyncWebServerRequest *r) {
        if (r->url().startsWith("/api/")) return sendError(r, 404, "Okänd endpoint");
        r->redirect("/");
    });
    s_server.begin();
    s_started = true;
    Serial.println("[web] server started on port 80");
    return true;
}

}  // namespace Webserver
