// app_web.cpp - OnTheRoad routes (see header). JSON field names are unchanged from the
// Arduino 0.3.x builds, so the web pages work as before.
#include "app_web.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_players.h"
#include "board_display.h"
#include "board_touch.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "my_diag.h"
#include "my_log.h"
#include "my_ota.h"
#include "my_storage.h"
#include "my_time.h"
#include "my_web.h"
#include "my_wifi.h"
#include "web_assets.h"

static const char *TAG = "appweb";

namespace AppWeb {

static ConfigChangedCallback s_configCb = nullptr;

void onConfigChanged(ConfigChangedCallback cb) { s_configCb = cb; }

static void *psAlloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

// Copies a trimmed parameter value. false when missing.
static bool getTrim(const MyWeb::Req &q, const char *name, char *out, size_t cap)
{
    if (!q.get(name, out, cap)) return false;
    char *s = out;
    while (*s == ' ' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
    memmove(out, s, n + 1);
    return true;
}

// ----------------------------------------------------------------------------------------
// Diagnostics providers (/api/info)
// ----------------------------------------------------------------------------------------
static void diagWifi(cJSON *o)
{
    MyWifi::Info wi = MyWifi::info();
    cJSON_AddStringToObject(o, "mode", "sta");
    cJSON_AddStringToObject(o, "status", MyWifi::statusText(wi.status));
    cJSON_AddBoolToObject(o, "connected", wi.connected);
    cJSON_AddStringToObject(o, "ssid", wi.ssid);
    cJSON_AddStringToObject(o, "saved_ssid", MyWifi::savedSsid());
    cJSON_AddStringToObject(o, "ip", wi.ip);
    cJSON_AddNumberToObject(o, "rssi", wi.rssi);
    cJSON_AddNumberToObject(o, "bars", wi.connected ? MyWifi::signalBars(wi.rssi) : 0);
    cJSON_AddNumberToObject(o, "uptime_s", wi.uptimeS);
    cJSON_AddNumberToObject(o, "connects", wi.connects);
    cJSON_AddNumberToObject(o, "disconnects", wi.disconnects);
    cJSON_AddNumberToObject(o, "roams", wi.roams);
    cJSON_AddStringToObject(o, "bssid", wi.bssid);
    cJSON_AddNumberToObject(o, "channel", wi.channel);
    cJSON_AddStringToObject(o, "last_error", MyWifi::failText(wi.lastFail));
    cJSON_AddNumberToObject(o, "last_reason", wi.lastReason);
    cJSON_AddStringToObject(o, "mac", wi.mac);
    cJSON_AddStringToObject(o, "hostname", Config::get().hostname);
    cJSON_AddBoolToObject(o, "mdns", MyWifi::mdnsOk());
    cJSON_AddNumberToObject(o, "saved", MyWifi::savedCount());
}

static void diagDisplay(cJSON *o)
{
    Display::Stats ds = Display::stats();
    MyLvgl::Stats ls = MyLvgl::stats();
    cJSON_AddBoolToObject(o, "ok", Display::ok());
    cJSON_AddNumberToObject(o, "width", 320);
    cJSON_AddNumberToObject(o, "height", 480);
    cJSON_AddNumberToObject(o, "fps_x10", (int)(ds.fps * 10));
    cJSON_AddNumberToObject(o, "flushes", ds.flushes);
    cJSON_AddNumberToObject(o, "frames", ds.frames);
    cJSON_AddBoolToObject(o, "te_active", ds.teActive);
    cJSON_AddNumberToObject(o, "te_edges", ds.teEdges);
    cJSON_AddNumberToObject(o, "te_timeouts", ds.teTimeouts);
    cJSON_AddNumberToObject(o, "buf_bytes", ds.bufBytes);
    cJSON_AddBoolToObject(o, "buf_psram", ds.bufInPsram);
    cJSON_AddNumberToObject(o, "brightness", Config::get().brightness);
    char ver[16];
    snprintf(ver, sizeof(ver), "%d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    cJSON_AddStringToObject(o, "lvgl", ver);
    cJSON_AddNumberToObject(o, "lv_mem_total", ls.lvMemTotal);
    cJSON_AddNumberToObject(o, "lv_mem_free", ls.lvMemFree);
    cJSON_AddNumberToObject(o, "lv_mem_max_used", ls.lvMemMaxUsed);
    cJSON_AddNumberToObject(o, "lv_mem_frag", ls.lvMemFrag);
    cJSON_AddNumberToObject(o, "lv_stack_free", ls.taskStackFree);
}

static void diagTouch(cJSON *o)
{
    Touch::Info ti = Touch::info();
    char fw[24];
    snprintf(fw, sizeof(fw), "%u (%u.%u.%u.%u)", ti.fwVersion, ti.fwRevision[0], ti.fwRevision[1], ti.fwRevision[2],
             ti.fwRevision[3]);
    cJSON_AddBoolToObject(o, "present", ti.present);
    cJSON_AddStringToObject(o, "fw", fw);
    cJSON_AddNumberToObject(o, "max_x", ti.maxX);
    cJSON_AddNumberToObject(o, "max_y", ti.maxY);
    cJSON_AddNumberToObject(o, "max_points", ti.maxTouches);
    cJSON_AddBoolToObject(o, "pressed", ti.pressed);
    cJSON_AddNumberToObject(o, "last_x", ti.lastX);
    cJSON_AddNumberToObject(o, "last_y", ti.lastY);
    cJSON_AddNumberToObject(o, "touches", ti.touchCount);
    cJSON_AddNumberToObject(o, "irqs", ti.irqCount);
    cJSON_AddNumberToObject(o, "errors", ti.readErrors);
}

static void diagClock(cJSON *o)
{
    cJSON_AddNumberToObject(o, "epoch", (double)MyTime::now());
    cJSON_AddStringToObject(o, "source", MyTime::sourceText(MyTime::source()));
    cJSON_AddBoolToObject(o, "certain", MyTime::certain());
    cJSON_AddStringToObject(o, "tz", Config::get().tz);
    uint32_t age = MyTime::lastNtpSyncS();
    cJSON_AddNumberToObject(o, "ntp_age_s", age == UINT32_MAX ? -1 : (double)age);
    cJSON_AddNumberToObject(o, "ntp_syncs", MyTime::ntpSyncs());
}

static void diagOta(cJSON *o)
{
    cJSON_AddStringToObject(o, "running", MyOta::runningPartition());
    cJSON_AddStringToObject(o, "next", MyOta::nextPartition());
    cJSON_AddNumberToObject(o, "slot_size", MyOta::slotSize());
    cJSON_AddBoolToObject(o, "pending_verify", MyOta::pendingVerify());
    cJSON_AddBoolToObject(o, "rolled_back", MyOta::rolledBack());
}

static void diagStorage(cJSON *o)
{
    cJSON_AddBoolToObject(o, "ok", MyFat::mounted());
    cJSON_AddNumberToObject(o, "total", (double)MyFat::totalBytes());
    cJSON_AddNumberToObject(o, "used", (double)MyFat::usedBytes());
    cJSON_AddNumberToObject(o, "players", Players::count());
}

void registerDiag()
{
    MyDiag::add("wifi", diagWifi);
    MyDiag::add("display", diagDisplay);
    MyDiag::add("touch", diagTouch);
    MyDiag::add("clock", diagClock);
    MyDiag::add("ota", diagOta);
    MyDiag::add("storage", diagStorage);
}

static esp_err_t handleInfo(httpd_req_t *r)
{
    cJSON *o = cJSON_CreateObject();
    MyDiag::fill(o);
    uint8_t addrs[16];
    int n = Touch::scanResults(addrs, 16);
    cJSON *i2c = cJSON_AddArrayToObject(o, "i2c");
    for (int i = 0; i < n; i++) {
        char buf[8];
        snprintf(buf, sizeof(buf), "0x%02X", addrs[i]);
        cJSON_AddItemToArray(i2c, cJSON_CreateString(buf));
    }
    MyWeb::Req q(r);
    if (q.has("tasks")) cJSON_AddItemToObject(o, "task_list", MyDiag::tasks());
    return MyWeb::sendJson(r, 200, o);
}

// ----------------------------------------------------------------------------------------
// /api/config
// ----------------------------------------------------------------------------------------
static cJSON *configJson()
{
    const AppConfig &c = Config::get();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "hostname", c.hostname);
    cJSON_AddNumberToObject(o, "nets_count", MyWifi::savedCount());
    cJSON_AddNumberToObject(o, "nets_max", NET_LIST_MAX);
    cJSON_AddBoolToObject(o, "admin_pin_set", c.adminPin[0] != 0);
    cJSON_AddStringToObject(o, "tz", c.tz);
    cJSON_AddNumberToObject(o, "brightness", c.brightness);
    cJSON_AddNumberToObject(o, "led_brightness", c.ledBrightness);
    cJSON_AddBoolToObject(o, "led_enabled", c.ledEnabled);
    cJSON_AddNumberToObject(o, "default_variant", c.defaultVariant);
    cJSON_AddNumberToObject(o, "roll_anim_ms", c.rollAnimMs);
    cJSON_AddBoolToObject(o, "maxi_saved_rolls", c.maxiSavedRolls);
    return o;
}

static esp_err_t handleConfigGet(httpd_req_t *r) { return MyWeb::sendJson(r, 200, configJson()); }

static esp_err_t handleConfigPost(httpd_req_t *r)
{
    MyWeb::Req q(r);
    AppConfig next = Config::get();
    bool bad = false, hostChanged = false, tzChanged = false;
    long v;
    bool b;
    if (q.getInt("brightness", 5, 100, v, bad)) next.brightness = (uint8_t)v;
    if (q.getInt("led_brightness", 0, 255, v, bad)) next.ledBrightness = (uint8_t)v;
    if (q.getInt("default_variant", 0, 3, v, bad)) next.defaultVariant = (uint8_t)v;
    if (q.getInt("roll_anim_ms", 300, 2000, v, bad)) next.rollAnimMs = (uint16_t)v;
    if (bad) return MyWeb::sendError(r, 400, "Ogiltigt numeriskt värde");
    if (q.getBool("led_enabled", b)) next.ledEnabled = b;
    if (q.getBool("maxi_saved_rolls", b)) next.maxiSavedRolls = b;
    char buf[64];
    if (q.has("tz")) {
        if (!q.get("tz", buf, sizeof(buf)) || !Config::validTz(buf)) return MyWeb::sendError(r, 400, "Ogiltig tidszon");
        if (strcmp(next.tz, buf)) {
            strlcpy(next.tz, buf, sizeof(next.tz));
            tzChanged = true;
        }
    }
    if (q.has("admin_pin_new")) {
        if (!q.adminAllowed()) return MyWeb::sendError(r, 403, "Fel admin-PIN");
        if (!q.get("admin_pin_new", buf, sizeof(buf)) || !Config::validAdminPin(buf, true))
            return MyWeb::sendError(r, 400, "Admin-PIN måste vara 4-8 siffror (tomt = ingen PIN)");
        strlcpy(next.adminPin, buf, sizeof(next.adminPin));
    }
    if (q.has("hostname")) {
        if (!q.get("hostname", buf, sizeof(buf)) || !Config::validHostname(buf))
            return MyWeb::sendError(r, 400, "Ogiltigt värdnamn");
        if (strcmp(next.hostname, buf)) {
            strlcpy(next.hostname, buf, sizeof(next.hostname));
            hostChanged = true;
        }
    }
    const bool pinChanged = strcmp(next.adminPin, Config::get().adminPin) != 0;
    Config::get() = next;
    if (!Config::save()) return MyWeb::sendError(r, 500, "Kunde inte spara i NVS");
    if (s_configCb) s_configCb();
    if (hostChanged) MyWifi::requestHostnameUpdate();
    if (tzChanged) MyTime::setTz(next.tz);
    MyLog::event("cfg", "settings saved from web%s%s%s", hostChanged ? ", hostname" : "", tzChanged ? ", tz" : "",
                 pinChanged ? ", admin PIN" : "");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddItemToObject(o, "config", configJson());
    return MyWeb::sendJson(r, 200, o);
}

// ----------------------------------------------------------------------------------------
// Wi-Fi
// ----------------------------------------------------------------------------------------
// ssid + pass: start a connection attempt (saved / moved up only on success).
// keep_pass=1 with an empty pass reuses the stored password of that saved network.
static esp_err_t handleWifiPost(httpd_req_t *r)
{
    MyWeb::Req q(r);
    char ssid[40], pass[72] = "";
    if (!getTrim(q, "ssid", ssid, sizeof(ssid)) || !ssid[0]) return MyWeb::sendError(r, 400, "ssid saknas");
    if (strlen(ssid) > 32) return MyWeb::sendError(r, 400, "SSID får vara högst 32 tecken");
    if (q.has("pass") && !q.get("pass", pass, sizeof(pass)))
        return MyWeb::sendError(r, 400, "Lösenordet måste vara tomt eller 8-63 tecken");
    if (!pass[0] && q.has("keep_pass")) MyWifi::savedPassword(ssid, pass, sizeof(pass));
    esp_err_t e;
    if (!Config::validWifiPass(pass, true)) e = MyWeb::sendError(r, 400, "Lösenordet måste vara tomt eller 8-63 tecken");
    else if (!MyWifi::connect(ssid, pass)) e = MyWeb::sendError(r, 500, "Wi-Fi är inte startat");
    else {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "ok", true);
        cJSON_AddBoolToObject(o, "connecting", true);
        cJSON_AddStringToObject(o, "ssid", ssid);
        e = MyWeb::sendJson(r, 200, o);
    }
    memset(pass, 0, sizeof(pass));
    return e;
}

static esp_err_t handleScan(httpd_req_t *r)
{
    MyWeb::Req q(r);
    int st = MyWifi::scanStatus();
    if (q.has("start") || st == -1) {
        MyWifi::startScan();
        return MyWeb::sendJsonStr(r, 202, "{\"scanning\":true,\"networks\":[]}");
    }
    if (st == -2) return MyWeb::sendJsonStr(r, 202, "{\"scanning\":true,\"networks\":[]}");
    static MyWifi::Network nets[MyWifi::MAX_NETWORKS];   // httpd task only
    int n = MyWifi::scanResults(nets, MyWifi::MAX_NETWORKS);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "scanning", false);
    cJSON *arr = cJSON_AddArrayToObject(o, "networks");
    for (int i = 0; i < n; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "ssid", nets[i].ssid);
        cJSON_AddNumberToObject(e, "rssi", nets[i].rssi);
        cJSON_AddNumberToObject(e, "bars", MyWifi::signalBars(nets[i].rssi));
        cJSON_AddNumberToObject(e, "channel", nets[i].channel);
        cJSON_AddBoolToObject(e, "secure", nets[i].secure);
        cJSON_AddItemToArray(arr, e);
    }
    return MyWeb::sendJson(r, 200, o);
}

// Saved networks in priority order (never the passwords).
static esp_err_t sendNets(httpd_req_t *r)
{
    NetList *list = (NetList *)psAlloc(sizeof(NetList));
    if (!list) return MyWeb::sendError(r, 500, "Minnet räcker inte");
    MyWifi::savedCopy(*list);
    MyWifi::Info wi = MyWifi::info();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "max", NET_LIST_MAX);
    cJSON *arr = cJSON_AddArrayToObject(o, "nets");
    for (int i = 0; i < list->count; i++) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "ssid", list->items[i].ssid);
        cJSON_AddBoolToObject(e, "secure", list->items[i].pass[0] != 0);
        cJSON_AddBoolToObject(e, "connected", wi.connected && strcmp(wi.ssid, list->items[i].ssid) == 0);
        cJSON_AddItemToArray(arr, e);
    }
    memset(list, 0, sizeof(NetList));   // Do not leave passwords lying around
    free(list);
    return MyWeb::sendJson(r, 200, o);
}

static esp_err_t handleNetsGet(httpd_req_t *r) { return sendNets(r); }

// action=add (ssid, pass) | remove (ssid) | move (ssid, delta) | forget_all
static esp_err_t handleNetsPost(httpd_req_t *r)
{
    MyWeb::Req q(r);
    char act[16];
    if (!q.get("action", act, sizeof(act))) return MyWeb::sendError(r, 400, "action saknas");
    if (strcmp(act, "forget_all") == 0) {
        MyWifi::forgetAll();
        MyLog::event("wifi", "all saved networks forgotten (web)");
        return MyWeb::sendOk(r);
    }
    char ssid[40];
    if (!q.get("ssid", ssid, sizeof(ssid)) || !ssid[0] || strlen(ssid) > 32) return MyWeb::sendError(r, 400, "Ogiltigt SSID");
    bool ok = false;
    if (strcmp(act, "add") == 0) {
        char pass[72] = "";
        if (q.has("pass") && !q.get("pass", pass, sizeof(pass)))
            return MyWeb::sendError(r, 400, "Lösenordet måste vara tomt eller 8-63 tecken");
        if (!Config::validWifiPass(pass, true)) return MyWeb::sendError(r, 400, "Lösenordet måste vara tomt eller 8-63 tecken");
        ok = MyWifi::savedAdd(ssid, pass);
        memset(pass, 0, sizeof(pass));
    } else if (strcmp(act, "remove") == 0) {
        ok = MyWifi::savedRemove(ssid);
    } else if (strcmp(act, "move") == 0) {
        long d;
        bool bad = false;
        if (!q.getInt("delta", -NET_LIST_MAX, NET_LIST_MAX, d, bad)) return MyWeb::sendError(r, 400, "delta saknas");
        ok = MyWifi::savedMove(ssid, (int)d);
    } else {
        return MyWeb::sendError(r, 400, "Okänd action");
    }
    if (!ok) return MyWeb::sendError(r, 404, "Nätverket finns inte / kunde inte sparas");
    return sendNets(r);
}

// epoch=<UTC seconds>: set the clock manually (e.g. from the browser).
static esp_err_t handleTimePost(httpd_req_t *r)
{
    MyWeb::Req q(r);
    char buf[24];
    if (!q.get("epoch", buf, sizeof(buf))) return MyWeb::sendError(r, 400, "epoch saknas");
    if (!MyTime::setManual((time_t)atoll(buf))) return MyWeb::sendError(r, 400, "Ogiltig tid");
    return MyWeb::sendOk(r);
}

// ----------------------------------------------------------------------------------------
// /api/players
// ----------------------------------------------------------------------------------------
// {"ok":true,"count":n,"max":200,"fs_ok":b,"players":[{"id","name","created"}...]} sorted by name.
static esp_err_t sendPlayers(httpd_req_t *r)
{
    PlayerDb *db = (PlayerDb *)psAlloc(sizeof(PlayerDb));
    if (!db) return MyWeb::sendError(r, 500, "Minnet räcker inte");
    Players::snapshot(*db);
    uint8_t idx[PLAYER_MAX];
    int n = db->sortedIndices(idx);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddNumberToObject(o, "count", n);
    cJSON_AddNumberToObject(o, "max", PLAYER_MAX);
    cJSON_AddBoolToObject(o, "fs_ok", Players::ok());
    cJSON *arr = cJSON_AddArrayToObject(o, "players");
    for (int k = 0; k < n; k++) {
        const PlayerRec &p = db->items[idx[k]];
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "id", p.id);
        cJSON_AddStringToObject(e, "name", p.name);
        cJSON_AddNumberToObject(e, "created", p.created);
        cJSON_AddItemToArray(arr, e);
    }
    free(db);
    return MyWeb::sendJson(r, 200, o);
}

static esp_err_t handlePlayersGet(httpd_req_t *r) { return sendPlayers(r); }

// action=add (name) | rename (id, name) | remove (id; admin PIN). Answers with the new list.
static esp_err_t handlePlayersPost(httpd_req_t *r)
{
    MyWeb::Req q(r);
    char act[16], name[PLAYER_NAME_CAP * 2];
    if (!q.get("action", act, sizeof(act))) return MyWeb::sendError(r, 400, "action saknas");
    const bool hasName = q.get("name", name, sizeof(name));
    long id = 0;
    bool bad = false;
    int res;
    if (strcmp(act, "add") == 0) {
        if (!hasName) return MyWeb::sendError(r, 400, "name saknas");
        res = Players::add(name);
        if (res > 0) res = 0;
    } else if (strcmp(act, "rename") == 0) {
        if (!hasName || !q.getInt("id", 1, 65535, id, bad)) return MyWeb::sendError(r, 400, "id/name saknas");
        res = Players::rename((uint16_t)id, name);
    } else if (strcmp(act, "remove") == 0) {
        if (!q.getInt("id", 1, 65535, id, bad)) return MyWeb::sendError(r, 400, "id saknas");
        if (!q.adminAllowed()) return MyWeb::sendError(r, 403, "Fel admin-PIN");
        res = Players::remove((uint16_t)id);
        if (res == 0) MyLog::event("players", "player %ld removed (web)", id);
    } else {
        return MyWeb::sendError(r, 400, "Okänd action");
    }
    if (res < 0) return MyWeb::sendError(r, res == (int)PlayerErr::NOT_FOUND ? 404 : 400, playerErrText(res));
    return sendPlayers(r);
}

static esp_err_t handleReset(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return MyWeb::sendError(r, 403, "Fel admin-PIN");
    MyLog::event("cfg", "factory reset (web)");
    Config::resetToDefaults();
    MyWeb::sendOk(r);
    MyWeb::rebootSoon();
    return ESP_OK;
}

bool begin()
{
    MyWeb::Config c;
    c.adminOk = Config::adminOk;
    if (!MyWeb::begin(c)) return false;
    for (const MyWeb::Asset &a : WEB_ASSETS) MyWeb::addAsset(a);
    bool ok = MyWeb::on("/api/info", HTTP_GET, handleInfo);
    ok &= MyWeb::on("/api/config", HTTP_GET, handleConfigGet);
    ok &= MyWeb::on("/api/config", HTTP_POST, handleConfigPost);
    ok &= MyWeb::on("/api/wifi", HTTP_POST, handleWifiPost);
    ok &= MyWeb::on("/api/scan", HTTP_GET, handleScan);
    ok &= MyWeb::on("/api/nets", HTTP_GET, handleNetsGet);
    ok &= MyWeb::on("/api/nets", HTTP_POST, handleNetsPost);
    ok &= MyWeb::on("/api/time", HTTP_POST, handleTimePost);
    ok &= MyWeb::on("/api/players", HTTP_GET, handlePlayersGet);
    ok &= MyWeb::on("/api/players", HTTP_POST, handlePlayersPost);
    ok &= MyWeb::on("/api/reset", HTTP_POST, handleReset);
    if (!ok) ESP_LOGE(TAG, "route registration failed");
    return ok;
}

}  // namespace AppWeb
