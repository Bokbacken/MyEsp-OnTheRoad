// app_web.cpp - OnTheRoad routes (see header). JSON field names are unchanged from the
// Arduino 0.3.x builds, so the web pages work as before.
#include "app_web.h"
#include "lang_game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "app_players.h"
#include "board_display.h"
#include "board_touch.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "game_live.h"
#include "my_diag.h"
#include "my_log.h"
#include "my_ota.h"
#include "my_rtc.h"
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
    cJSON_AddBoolToObject(o, "log_anchored", MyLog::clockAnchored());
}

static void diagRtc(cJSON *o)
{
    const MyRtc::Status s = MyRtc::status();
    cJSON_AddStringToObject(o, "chip", "DS3231");
    cJSON_AddStringToObject(o, "state", MyRtc::stateText(s.state));
    cJSON_AddBoolToObject(o, "ok", s.state == MyRtc::State::OK);
    cJSON_AddBoolToObject(o, "present", s.present);
    cJSON_AddBoolToObject(o, "ticking", s.ticking);
    cJSON_AddBoolToObject(o, "osf_at_boot", s.osf);
    cJSON_AddBoolToObject(o, "time_valid", s.timeValid);
    cJSON_AddNumberToObject(o, "temp_c", (double)s.tempC);
    cJSON_AddNumberToObject(o, "boot_time", (double)s.bootTime);
    cJSON_AddNumberToObject(o, "last_write", (double)s.lastWrite);
    cJSON_AddNumberToObject(o, "writes", s.writes);
    cJSON_AddNumberToObject(o, "errors", s.errors);
    cJSON_AddNumberToObject(o, "drift_s", s.lastDriftS);
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
    MyDiag::add("rtc", diagRtc);
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
    cJSON_AddNumberToObject(o, "throw_mode", c.throwMode);
    cJSON_AddStringToObject(o, "lang", c.lang);
    cJSON *langs = cJSON_AddArrayToObject(o, "langs");
    for (int i = 0; i < Lang::count(); i++) {
        cJSON *l = cJSON_CreateObject();
        cJSON_AddStringToObject(l, "code", Lang::code(i));
        cJSON_AddStringToObject(l, "name", Lang::name(i));
        cJSON_AddItemToArray(langs, l);
    }
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
    if (q.getInt("throw_mode", 0, 1, v, bad)) next.throwMode = (uint8_t)v;
    if (bad) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_NUMBER));
    if (q.getBool("led_enabled", b)) next.ledEnabled = b;
    if (q.getBool("maxi_saved_rolls", b)) next.maxiSavedRolls = b;
    char buf[64];
    if (q.has("tz")) {
        if (!q.get("tz", buf, sizeof(buf)) || !Config::validTz(buf)) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_TZ));
        if (strcmp(next.tz, buf)) {
            strlcpy(next.tz, buf, sizeof(next.tz));
            tzChanged = true;
        }
    }
    if (q.has("lang")) {
        if (!q.get("lang", buf, sizeof(buf)) || !Lang::valid(buf)) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_LANG));
        strlcpy(next.lang, buf, sizeof(next.lang));
    }
    if (q.has("admin_pin_new")) {
        if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
        if (!q.get("admin_pin_new", buf, sizeof(buf)) || !Config::validAdminPin(buf, true))
            return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_PIN_FORMAT));
        strlcpy(next.adminPin, buf, sizeof(next.adminPin));
    }
    if (q.has("hostname")) {
        if (!q.get("hostname", buf, sizeof(buf)) || !Config::validHostname(buf))
            return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_HOSTNAME));
        if (strcmp(next.hostname, buf)) {
            strlcpy(next.hostname, buf, sizeof(next.hostname));
            hostChanged = true;
        }
    }
    const bool pinChanged = strcmp(next.adminPin, Config::get().adminPin) != 0;
    Config::get() = next;
    if (!Config::save()) return MyWeb::sendError(r, 500, tr(Str::WEB_ERR_NVS));
    if (s_configCb) s_configCb();
    if (hostChanged) MyWifi::requestHostnameUpdate();
    if (tzChanged) MyTime::setTz(next.tz);
    Lang::setLang(next.lang);
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
    if (!getTrim(q, "ssid", ssid, sizeof(ssid)) || !ssid[0]) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_SSID_MISSING));
    if (strlen(ssid) > 32) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_SSID_LONG));
    if (q.has("pass") && !q.get("pass", pass, sizeof(pass)))
        return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_PASS));
    if (!pass[0] && q.has("keep_pass")) MyWifi::savedPassword(ssid, pass, sizeof(pass));
    esp_err_t e;
    if (!Config::validWifiPass(pass, true)) e = MyWeb::sendError(r, 400, tr(Str::WEB_ERR_PASS));
    else if (!MyWifi::connect(ssid, pass)) e = MyWeb::sendError(r, 500, tr(Str::WEB_ERR_WIFI_OFF));
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
    if (!list) return MyWeb::sendError(r, 500, tr(Str::WEB_ERR_MEMORY));
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
    if (!q.get("action", act, sizeof(act))) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_ACTION_MISSING));
    if (strcmp(act, "forget_all") == 0) {
        MyWifi::forgetAll();
        MyLog::event("wifi", "all saved networks forgotten (web)");
        return MyWeb::sendOk(r);
    }
    char ssid[40];
    if (!q.get("ssid", ssid, sizeof(ssid)) || !ssid[0] || strlen(ssid) > 32) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_SSID));
    bool ok = false;
    if (strcmp(act, "add") == 0) {
        char pass[72] = "";
        if (q.has("pass") && !q.get("pass", pass, sizeof(pass)))
            return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_PASS));
        if (!Config::validWifiPass(pass, true)) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_PASS));
        ok = MyWifi::savedAdd(ssid, pass);
        memset(pass, 0, sizeof(pass));
    } else if (strcmp(act, "remove") == 0) {
        ok = MyWifi::savedRemove(ssid);
    } else if (strcmp(act, "move") == 0) {
        long d;
        bool bad = false;
        if (!q.getInt("delta", -NET_LIST_MAX, NET_LIST_MAX, d, bad)) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_DELTA));
        ok = MyWifi::savedMove(ssid, (int)d);
    } else {
        return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_ACTION));
    }
    if (!ok) return MyWeb::sendError(r, 404, tr(Str::WEB_ERR_NET_NOT_FOUND));
    return sendNets(r);
}

// epoch=<UTC seconds>: set the clock manually (e.g. from the browser).
static esp_err_t handleTimePost(httpd_req_t *r)
{
    MyWeb::Req q(r);
    char buf[24];
    if (!q.get("epoch", buf, sizeof(buf))) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_EPOCH));
    if (!MyTime::setManual((time_t)atoll(buf))) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_TIME));
    return MyWeb::sendOk(r);
}

// ----------------------------------------------------------------------------------------
// /api/game - live view of the running game (page /game polls it).
// ?v=<version>: answers {"same":true} while nothing changed.
// ----------------------------------------------------------------------------------------
// Builds the live-view JSON from a snapshot (shared by /api/game and the WebSocket broadcast).
static cJSON *gameJson(const GameLive::Snapshot &snap)
{
    const GameLive::Snapshot *s = &snap;
    const YatzyGame &g = s->game;
    const bool hasGame = s->active && g.started() && g.playerCount() > 0;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "version", s->version);
    cJSON_AddBoolToObject(o, "active", hasGame);
    cJSON_AddStringToObject(o, "lang", Lang::currentLang());
    if (hasGame) {
        const YatzyVariant var = g.variant();
        cJSON_AddBoolToObject(o, "rolling", s->rolling);
        cJSON_AddBoolToObject(o, "shaking", s->shaking);
        cJSON_AddBoolToObject(o, "choosing", s->resultPhase);
        cJSON_AddBoolToObject(o, "finished", g.finished());
        cJSON_AddStringToObject(o, "variant", var == YatzyVariant::MAXI ? "maxi" : "standard");
        cJSON_AddStringToObject(o, "name", trVariant(var, g.mode()));
        cJSON_AddNumberToObject(o, "round", g.round());
        cJSON_AddNumberToObject(o, "rounds", g.totalRounds());
        cJSON_AddNumberToObject(o, "current", g.currentPlayer());
        cJSON_AddNumberToObject(o, "rolls_used", g.rollsUsed());
        cJSON_AddNumberToObject(o, "rolls_left", g.rollsLeft());
        cJSON_AddBoolToObject(o, "can_roll", g.canRoll());
        cJSON_AddBoolToObject(o, "has_rolled", g.hasRolled());
        cJSON_AddNumberToObject(o, "bonus_threshold", Yatzy::bonusThreshold(var));
        cJSON_AddNumberToObject(o, "bonus_points", Yatzy::bonusPoints(var));
        if (s->code) cJSON_AddNumberToObject(o, "code", s->code);
        cJSON *wp = cJSON_AddArrayToObject(o, "web_players");
        for (int p = 0; p < g.playerCount() && p < YATZY_MAX_PLAYERS; p++) {
            if (!s->web[p]) continue;
            cJSON *e = cJSON_CreateObject();
            cJSON_AddNumberToObject(e, "player", p);
            cJSON_AddNumberToObject(e, "id", s->pid[p]);
            cJSON_AddBoolToObject(e, "connected", s->conn[p] > 0);
            cJSON_AddItemToArray(wp, e);
        }
        cJSON *dice = cJSON_AddArrayToObject(o, "dice");
        cJSON *held = cJSON_AddArrayToObject(o, "held");
        for (int i = 0; i < g.diceCount(); i++) {
            cJSON_AddItemToArray(dice, cJSON_CreateNumber(g.hasRolled() ? g.die(i) : 0));
            cJSON_AddItemToArray(held, cJSON_CreateBool(g.held(i)));
        }
        int nc = 0;
        const YatzyCategory *cats = Yatzy::categories(var, nc);
        cJSON *ca = cJSON_AddArrayToObject(o, "categories");
        for (int k = 0; k < nc; k++) {
            cJSON *c = cJSON_CreateObject();
            cJSON_AddNumberToObject(c, "id", (int)cats[k]);
            cJSON_AddStringToObject(c, "name", trCategory(cats[k], var));
            cJSON_AddBoolToObject(c, "upper", Yatzy::isUpper(cats[k]));
            // Points this box would give with the current dice (only meaningful after a roll)
            if (g.hasRolled() && !g.finished() && g.canScore(cats[k]))
                cJSON_AddNumberToObject(c, "potential", g.potentialScore(cats[k]));
            cJSON_AddItemToArray(ca, c);
        }
        cJSON *pa = cJSON_AddArrayToObject(o, "players");
        for (int p = 0; p < g.playerCount(); p++) {
            const YatzyPlayer &pl = g.player(p);
            cJSON *e = cJSON_CreateObject();
            cJSON_AddStringToObject(e, "name", pl.name);
            if (p < YATZY_MAX_PLAYERS && s->web[p]) cJSON_AddBoolToObject(e, "web", true);
            cJSON *sc = cJSON_AddArrayToObject(e, "scores");   // Same order as "categories"; null = empty
            for (int k = 0; k < nc; k++) {
                int16_t v2 = pl.scores[(int)cats[k]];
                cJSON_AddItemToArray(sc, v2 == YATZY_EMPTY ? cJSON_CreateNull() : cJSON_CreateNumber(v2));
            }
            cJSON_AddNumberToObject(e, "upper", g.upperSum(p));
            cJSON_AddNumberToObject(e, "bonus", g.bonus(p));
            cJSON_AddNumberToObject(e, "total", g.total(p));
            if (var == YatzyVariant::MAXI) cJSON_AddNumberToObject(e, "saved_rolls", pl.savedRolls);
            cJSON_AddItemToArray(pa, e);
        }
        if (g.finished()) {
            int order[YATZY_MAX_PLAYERS], places[YATZY_MAX_PLAYERS];
            int n = g.ranking(order, places);
            cJSON *rk = cJSON_AddArrayToObject(o, "ranking");
            for (int k = 0; k < n; k++) {
                cJSON *e = cJSON_CreateObject();
                cJSON_AddNumberToObject(e, "player", order[k]);
                cJSON_AddNumberToObject(e, "place", places[k]);
                cJSON_AddItemToArray(rk, e);
            }
        }
    }
    return o;
}

static esp_err_t handleGame(httpd_req_t *r)
{
    MyWeb::Req q(r);
    long v = -1;
    bool bad = false;
    q.getInt("v", 0, 0x7FFFFFFF, v, bad);
    if (v >= 0 && (uint32_t)v == GameLive::version()) return MyWeb::sendJsonStr(r, 200, "{\"same\":true}");
    GameLive::Snapshot *s = (GameLive::Snapshot *)psAlloc(sizeof(GameLive::Snapshot));
    if (!s) return MyWeb::sendError(r, 500, tr(Str::WEB_ERR_MEMORY));
    GameLive::get(*s);
    cJSON *o = gameJson(*s);
    free(s);
    return MyWeb::sendJson(r, 200, o);
}

// ----------------------------------------------------------------------------------------
// /ws/game - interactive web play (page /play).
//
// Client -> device (text frames, JSON):
//   {"cmd":"join","code":1234,"player":2}   bind to seat 2 (must be a web seat); no "player" = spectator
//   {"cmd":"hold","die":N}                   toggle hold of die N (0-based)
//   {"cmd":"roll"}                           roll the free dice (the console animates the throw)
//   {"cmd":"choose","cat":ID}                write the box with category id ID (see "categories")
// Device -> client:
//   {"type":"joined","you":seat|-1}
//   {"type":"error","error":"code|seat|join|full|cmd|turn","msg":"..."}
//   {"type":"state","you":seat|-1,"joined":bool, ...same fields as /api/game...}  on every change
//
// All client bookkeeping and every send run in the httpd task (handler + httpd_queue_work), so
// the client table needs no lock. A small poll task only watches the GameLive version and
// queues the broadcast. Commands go to the LVGL task through GameLive's command queue.
// ----------------------------------------------------------------------------------------
static constexpr int WS_MAX_CLIENTS = 4;
static constexpr size_t WS_MAX_FRAME = 256;

struct WsClient {
    int fd;            // -1 = free slot
    bool joined;
    int8_t seat;       // -1 = spectator
    uint16_t code;     // Code used to join (a new game invalidates the binding)
    bool fresh;        // Needs a state frame even if the version did not change
};

static WsClient s_ws[WS_MAX_CLIENTS] = {{-1, false, -1, 0, false}, {-1, false, -1, 0, false},
                                        {-1, false, -1, 0, false}, {-1, false, -1, 0, false}};
static volatile int s_wsCount = 0;
static volatile bool s_wsWorkQueued = false;
static uint32_t s_wsSentVersion = 0;
static GameLive::Snapshot *s_wsSnap = nullptr;   // PSRAM, httpd task only

static void wsSendText(int fd, const char *txt)
{
    httpd_ws_frame_t f = {};
    f.type = HTTPD_WS_TYPE_TEXT;
    f.payload = (uint8_t *)txt;
    f.len = strlen(txt);
    f.final = true;
    httpd_ws_send_frame_async(MyWeb::handle(), fd, &f);
}

static void wsSendError(int fd, const char *code, Str msg)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", "error");
    cJSON_AddStringToObject(o, "error", code);
    cJSON_AddStringToObject(o, "msg", tr(msg));
    char *txt = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (txt) {
        wsSendText(fd, txt);
        cJSON_free(txt);
    }
}

static WsClient *wsFind(int fd)
{
    for (WsClient &c : s_ws)
        if (c.fd == fd) return &c;
    return nullptr;
}

static void wsPublishConnections()
{
    uint8_t conn[YATZY_MAX_PLAYERS] = {};
    int n = 0;
    for (const WsClient &c : s_ws) {
        if (c.fd < 0) continue;
        n++;
        if (c.joined && c.seat >= 0 && c.seat < YATZY_MAX_PLAYERS) conn[c.seat]++;
    }
    s_wsCount = n;
    GameLive::setConnections(conn, YATZY_MAX_PLAYERS);
}

// httpd task only: free slots whose socket is no longer an open WebSocket. Returns true if any was freed.
static bool wsReapDead(httpd_handle_t hd)
{
    bool changed = false;
    for (WsClient &c : s_ws) {
        if (c.fd >= 0 && httpd_ws_get_fd_info(hd, c.fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
            c = {-1, false, -1, 0, false};
            changed = true;
        }
    }
    return changed;
}

// httpd task: drop dead sockets, invalidate stale bindings, send state frames.
static void wsBroadcastWork(void *)
{
    s_wsWorkQueued = false;
    httpd_handle_t hd = MyWeb::handle();
    if (!s_wsSnap) s_wsSnap = (GameLive::Snapshot *)psAlloc(sizeof(GameLive::Snapshot));
    if (!s_wsSnap || !hd) return;
    bool changed = wsReapDead(hd);
    GameLive::get(*s_wsSnap);
    for (WsClient &c : s_ws) {
        if (c.fd >= 0 && c.joined && c.code != s_wsSnap->code) {   // Game left / new code
            c.joined = false;
            c.seat = -1;
            c.fresh = true;
            changed = true;
        }
    }
    if (changed) {
        wsPublishConnections();
        GameLive::get(*s_wsSnap);
    }
    const bool newVersion = s_wsSnap->version != s_wsSentVersion;
    bool any = false;
    for (const WsClient &c : s_ws) any |= c.fd >= 0 && (newVersion || c.fresh);
    if (!any) return;
    s_wsSentVersion = s_wsSnap->version;
    cJSON *o = gameJson(*s_wsSnap);
    char *base = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!base || base[0] != '{') {
        if (base) cJSON_free(base);
        return;
    }
    const size_t cap = strlen(base) + 64;
    char *msg = (char *)psAlloc(cap);
    if (msg) {
        for (WsClient &c : s_ws) {
            if (c.fd < 0 || !(newVersion || c.fresh)) continue;
            c.fresh = false;
            // {"type":"state","you":n,"joined":b, + the rest of the game object
            snprintf(msg, cap, "{\"type\":\"state\",\"you\":%d,\"joined\":%s,%s", c.joined ? c.seat : -1,
                     c.joined ? "true" : "false", base + 1);
            wsSendText(c.fd, msg);
        }
        free(msg);
    }
    cJSON_free(base);
}

static void wsQueueBroadcast()
{
    if (s_wsWorkQueued || !MyWeb::handle()) return;
    s_wsWorkQueued = true;
    if (httpd_queue_work(MyWeb::handle(), wsBroadcastWork, nullptr) != ESP_OK) s_wsWorkQueued = false;
}

static void wsPollTask(void *)
{
    uint32_t last = 0, idle = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(50));
        if (s_wsCount <= 0) continue;
        uint32_t v = GameLive::version();
        if (v != last || ++idle >= 20) {   // On change, and once a second to reap dead sockets
            last = v;
            idle = 0;
            wsQueueBroadcast();
        }
    }
}

static void wsHandleText(int fd, const char *txt)
{
    WsClient *c = wsFind(fd);
    if (!c) return;
    cJSON *j = cJSON_Parse(txt);
    if (!j) return wsSendError(fd, "cmd", Str::WEB_ERR_WS_CMD);
    const cJSON *jc = cJSON_GetObjectItem(j, "cmd");
    const char *cmd = cJSON_IsString(jc) ? jc->valuestring : "";
    GameLive::Snapshot *s = s_wsSnap ? s_wsSnap : (s_wsSnap = (GameLive::Snapshot *)psAlloc(sizeof(GameLive::Snapshot)));
    if (!s) {
        cJSON_Delete(j);
        return;
    }
    GameLive::get(*s);
    if (strcmp(cmd, "join") == 0) {
        const cJSON *jk = cJSON_GetObjectItem(j, "code");
        long code = cJSON_IsNumber(jk) ? (long)jk->valuedouble : cJSON_IsString(jk) ? atol(jk->valuestring) : 0;
        const cJSON *jp = cJSON_GetObjectItem(j, "player");
        const bool hasGame = s->active && s->game.started();
        if (!hasGame || s->code == 0 || code != s->code) {
            wsSendError(fd, "code", Str::WEB_ERR_WS_CODE);
        } else {
            int seat = -1;
            if (cJSON_IsNumber(jp)) {
                seat = jp->valueint;
                if (seat < 0 || seat >= s->game.playerCount() || seat >= YATZY_MAX_PLAYERS || !s->web[seat]) {
                    wsSendError(fd, "seat", Str::WEB_ERR_WS_SEAT);
                    seat = -2;
                }
            }
            if (seat >= -1) {
                c->joined = true;
                c->seat = (int8_t)seat;
                c->code = s->code;
                c->fresh = true;
                char buf[48];
                snprintf(buf, sizeof(buf), "{\"type\":\"joined\",\"you\":%d}", seat);
                wsSendText(fd, buf);
                wsPublishConnections();
                wsQueueBroadcast();
            }
        }
    } else if (strcmp(cmd, "hold") == 0 || strcmp(cmd, "roll") == 0 || strcmp(cmd, "choose") == 0) {
        if (!c->joined || c->seat < 0 || c->code != s->code) {
            wsSendError(fd, "join", Str::WEB_ERR_WS_JOIN);
        } else if (s->game.currentPlayer() != c->seat || s->game.finished()) {
            wsSendError(fd, "turn", Str::WEB_ERR_WS_TURN);
        } else {
            GameLive::Cmd gc = {GameLive::CmdType::ROLL, 0, c->seat};
            bool ok = true;
            if (cmd[0] == 'h') {
                const cJSON *jd = cJSON_GetObjectItem(j, "die");
                ok = cJSON_IsNumber(jd) && jd->valueint >= 0 && jd->valueint < s->game.diceCount();
                gc.type = GameLive::CmdType::HOLD;
                gc.arg = ok ? (int8_t)jd->valueint : 0;
            } else if (cmd[0] == 'c') {
                const cJSON *jd = cJSON_GetObjectItem(j, "cat");
                ok = cJSON_IsNumber(jd) && jd->valueint >= 0 && jd->valueint < (int)YatzyCategory::COUNT;
                gc.type = GameLive::CmdType::CHOOSE;
                gc.arg = ok ? (int8_t)jd->valueint : 0;
            }
            if (!ok || !GameLive::pushCmd(gc)) wsSendError(fd, "cmd", Str::WEB_ERR_WS_CMD);
        }
    } else {
        wsSendError(fd, "cmd", Str::WEB_ERR_WS_CMD);
    }
    cJSON_Delete(j);
}

static esp_err_t wsHandler(httpd_req_t *r)
{
    const int fd = httpd_req_to_sockfd(r);
    if (r->method == HTTP_GET) {   // Handshake done: register the client
        WsClient *c = wsFind(fd);
        if (!c) {
            wsReapDead(r->handle);   // Sweep ghost sockets (e.g. backgrounded phones) before giving up
            c = wsFind(-1);
        }
        if (!c) {
            wsSendError(fd, "full", Str::WEB_ERR_WS_FULL);
            httpd_sess_trigger_close(r->handle, fd);
            return ESP_OK;
        }
        *c = {fd, false, -1, 0, true};
        wsPublishConnections();
        wsQueueBroadcast();
        return ESP_OK;
    }
    httpd_ws_frame_t f = {};
    f.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t e = httpd_ws_recv_frame(r, &f, 0);
    if (e != ESP_OK) return e;
    if (f.type != HTTPD_WS_TYPE_TEXT || f.len == 0 || f.len > WS_MAX_FRAME) {
        if (f.len > WS_MAX_FRAME) return ESP_FAIL;   // Oversized: drop the connection
        return ESP_OK;
    }
    char buf[WS_MAX_FRAME + 1];
    f.payload = (uint8_t *)buf;
    e = httpd_ws_recv_frame(r, &f, f.len);
    if (e != ESP_OK) return e;
    buf[f.len] = 0;
    wsHandleText(fd, buf);
    return ESP_OK;
}

static bool wsBegin()
{
    httpd_uri_t u = {};
    u.uri = "/ws/game";
    u.method = HTTP_GET;
    u.handler = wsHandler;
    u.user_ctx = nullptr;
    u.is_websocket = true;
    if (httpd_register_uri_handler(MyWeb::handle(), &u) != ESP_OK) return false;
    return xTaskCreatePinnedToCore(wsPollTask, "ws_poll", 3072, nullptr, 3, nullptr, 0) == pdPASS;
}

// ----------------------------------------------------------------------------------------
// /api/players
// ----------------------------------------------------------------------------------------
// {"ok":true,"count":n,"max":200,"fs_ok":b,"players":[{"id","name","created"}...]} sorted by name.
static esp_err_t sendPlayers(httpd_req_t *r)
{
    PlayerDb *db = (PlayerDb *)psAlloc(sizeof(PlayerDb));
    if (!db) return MyWeb::sendError(r, 500, tr(Str::WEB_ERR_MEMORY));
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
    if (!q.get("action", act, sizeof(act))) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_ACTION_MISSING));
    const bool hasName = q.get("name", name, sizeof(name));
    long id = 0;
    bool bad = false;
    int res;
    if (strcmp(act, "add") == 0) {
        if (!hasName) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_NAME));
        res = Players::add(name);
        if (res > 0) res = 0;
    } else if (strcmp(act, "rename") == 0) {
        if (!hasName || !q.getInt("id", 1, 65535, id, bad)) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_ID_NAME));
        res = Players::rename((uint16_t)id, name);
    } else if (strcmp(act, "remove") == 0) {
        if (!q.getInt("id", 1, 65535, id, bad)) return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_ID));
        if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
        res = Players::remove((uint16_t)id);
        if (res == 0) MyLog::event("players", "player %ld removed (web)", id);
    } else {
        return MyWeb::sendError(r, 400, tr(Str::WEB_ERR_ACTION));
    }
    if (res < 0) return MyWeb::sendError(r, res == (int)PlayerErr::NOT_FOUND ? 404 : 400, trPlayerErr(res));
    return sendPlayers(r);
}

static esp_err_t handleReset(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return MyWeb::sendError(r, 403, tr(Str::WEB_ERR_PIN));
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
    c.maxUris = 56;
    c.maxSockets = 12;   // Room for 4 WebSocket clients next to normal page loads (LWIP: 16)
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
    ok &= MyWeb::on("/api/game", HTTP_GET, handleGame);
    ok &= wsBegin();
    if (!ok) ESP_LOGE(TAG, "route registration failed");
    return ok;
}

}  // namespace AppWeb
