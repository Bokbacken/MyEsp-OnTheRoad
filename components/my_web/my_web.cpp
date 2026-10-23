// my_web.cpp - esp_http_server helpers: params, JSON answers, assets, OTA upload, log routes.
#include "my_web.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "my_log.h"
#include "my_ota.h"

namespace MyWeb {

static const char *TAG = "web";
static httpd_handle_t s_server = nullptr;
static Config s_cfg;
static constexpr size_t MAX_BODY = 4096;

static void *psAlloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

// ----------------------------------------------------------------------------------------
// Responses
// ----------------------------------------------------------------------------------------
void setStatus(httpd_req_t *r, int code)
{
    const char *s;
    switch (code) {
    case 200: s = "200 OK"; break;
    case 202: s = "202 Accepted"; break;
    case 302: s = "302 Found"; break;
    case 400: s = "400 Bad Request"; break;
    case 403: s = "403 Forbidden"; break;
    case 404: s = "404 Not Found"; break;
    case 409: s = "409 Conflict"; break;
    case 413: s = "413 Payload Too Large"; break;
    default: s = "500 Internal Server Error"; break;
    }
    httpd_resp_set_status(r, s);
}

esp_err_t sendJsonStr(httpd_req_t *r, int code, const char *json)
{
    setStatus(r, code);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    return httpd_resp_sendstr(r, json ? json : "{}");
}

esp_err_t sendJson(httpd_req_t *r, int code, cJSON *obj)
{
    char *s = obj ? cJSON_PrintUnformatted(obj) : nullptr;
    cJSON_Delete(obj);
    if (!s) return sendJsonStr(r, 500, "{\"ok\":false,\"error\":\"Minnet räcker inte\"}");
    esp_err_t e = sendJsonStr(r, code, s);
    cJSON_free(s);
    return e;
}

esp_err_t sendError(httpd_req_t *r, int code, const char *msg)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", false);
    cJSON_AddStringToObject(o, "error", msg ? msg : "");
    return sendJson(r, code, o);
}

esp_err_t sendOk(httpd_req_t *r) { return sendJsonStr(r, 200, "{\"ok\":true}"); }

void rebootSoon(uint32_t ms)
{
    static uint32_t s_delay;
    s_delay = ms;
    xTaskCreate(
        [](void *) {
            vTaskDelay(pdMS_TO_TICKS(s_delay));
            if (s_cfg.onReboot) s_cfg.onReboot();
            MyLog::event("sys", "restart requested");
            vTaskDelay(pdMS_TO_TICKS(50));
            esp_restart();
        },
        "reboot", 3072, nullptr, 1, nullptr);
}

// ----------------------------------------------------------------------------------------
// Parameters
// ----------------------------------------------------------------------------------------
static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Decodes %XX and '+' in place.
static void urlDecode(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '+') *o++ = ' ';
        else if (*s == '%' && hexv(s[1]) >= 0 && hexv(s[2]) >= 0) {
            *o++ = (char)(hexv(s[1]) * 16 + hexv(s[2]));
            s += 2;
        } else *o++ = *s;
    }
    *o = 0;
}

// Finds key in an url-encoded "a=1&b=2" string; copies the decoded value.
static bool findParam(const char *qs, const char *key, char *out, size_t cap, bool *present)
{
    if (!qs) return false;
    size_t kl = strlen(key);
    const char *p = qs;
    while (*p) {
        const char *amp = strchr(p, '&');
        size_t len = amp ? (size_t)(amp - p) : strlen(p);
        if (len >= kl && strncmp(p, key, kl) == 0 && (len == kl || p[kl] == '=')) {
            if (present) *present = true;
            const char *v = len == kl ? p + len : p + kl + 1;
            size_t vl = (size_t)(p + len - v);
            char *tmp = (char *)malloc(vl + 1);
            if (!tmp) return false;
            memcpy(tmp, v, vl);
            tmp[vl] = 0;
            urlDecode(tmp);
            bool ok = out && strlen(tmp) < cap;
            if (ok) strcpy(out, tmp);
            free(tmp);
            return ok;
        }
        if (!amp) break;
        p = amp + 1;
    }
    return false;
}

Req::Req(httpd_req_t *r) : r_(r)
{
    size_t ql = httpd_req_get_url_query_len(r);
    if (ql > 0) {
        query_ = (char *)malloc(ql + 1);
        if (query_ && httpd_req_get_url_query_str(r, query_, ql + 1) != ESP_OK) query_[0] = 0;
    }
    char ct[64] = "";
    httpd_req_get_hdr_value_str(r, "Content-Type", ct, sizeof(ct));
    if (r->method == HTTP_POST && r->content_len > 0 && r->content_len <= MAX_BODY &&
        strstr(ct, "x-www-form-urlencoded")) {
        body_ = (char *)malloc(r->content_len + 1);
        size_t got = 0;
        int timeouts = 0;
        while (body_ && got < r->content_len) {
            int n = httpd_req_recv(r, body_ + got, r->content_len - got);
            if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) continue;
            if (n <= 0) break;
            got += n;
        }
        if (body_) body_[got] = 0;
    }
}

Req::~Req()
{
    free(query_);
    if (body_) {
        memset(body_, 0, strlen(body_));   // May contain passwords
        free(body_);
    }
}

bool Req::has(const char *name) const
{
    bool present = false;
    char dummy[2];
    findParam(body_, name, dummy, sizeof(dummy), &present);
    if (!present) findParam(query_, name, dummy, sizeof(dummy), &present);
    return present;
}

bool Req::get(const char *name, char *out, size_t cap) const
{
    bool present = false;
    if (findParam(body_, name, out, cap, &present)) return true;
    if (present) return false;
    return findParam(query_, name, out, cap, nullptr);
}

bool Req::getInt(const char *name, long lo, long hi, long &out, bool &bad) const
{
    char buf[24];
    if (!has(name)) return false;
    if (!get(name, buf, sizeof(buf)) || !buf[0]) { bad = true; return false; }
    char *end = nullptr;
    long v = strtol(buf, &end, 10);
    if (!end || *end || v < lo || v > hi) { bad = true; return false; }
    out = v;
    return true;
}

bool Req::getBool(const char *name, bool &out) const
{
    char buf[8];
    if (!has(name)) return false;
    if (!get(name, buf, sizeof(buf))) buf[0] = 0;
    out = !strcmp(buf, "1") || !strcmp(buf, "true") || !strcmp(buf, "on");
    return true;
}

bool Req::header(const char *name, char *out, size_t cap) const
{
    return httpd_req_get_hdr_value_str(r_, name, out, cap) == ESP_OK;
}

bool Req::adminAllowed() const
{
    if (!s_cfg.adminOk) return true;
    char pin[16];
    if (has("pin")) return get("pin", pin, sizeof(pin)) && s_cfg.adminOk(pin);
    if (header("X-Admin-Pin", pin, sizeof(pin))) return s_cfg.adminOk(pin);
    return s_cfg.adminOk("");
}

// ----------------------------------------------------------------------------------------
// Routes
// ----------------------------------------------------------------------------------------
bool on(const char *uri, httpd_method_t method, Handler fn)
{
    if (!s_server) return false;
    httpd_uri_t u = {};
    u.uri = uri;
    u.method = method;
    u.handler = fn;
    esp_err_t e = httpd_register_uri_handler(s_server, &u);
    if (e != ESP_OK) ESP_LOGE(TAG, "route %s failed: %s", uri, esp_err_to_name(e));
    return e == ESP_OK;
}

static esp_err_t assetHandler(httpd_req_t *r)
{
    const Asset *a = (const Asset *)r->user_ctx;
    httpd_resp_set_type(r, a->mime);
    if (a->gzip) httpd_resp_set_hdr(r, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(r, "Cache-Control", "no-cache");
    return httpd_resp_send(r, (const char *)a->data, a->len);
}

bool addAsset(const Asset &a)
{
    if (!s_server) return false;
    httpd_uri_t u = {};
    u.uri = a.uri;
    u.method = HTTP_GET;
    u.handler = assetHandler;
    u.user_ctx = (void *)&a;
    return httpd_register_uri_handler(s_server, &u) == ESP_OK;
}

// GET /api/log?from=<seq>
static esp_err_t logHandler(httpd_req_t *r)
{
    Req q(r);
    long from = 0;
    bool bad = false;
    q.getInt("from", 0, 0x7FFFFFFF, from, bad);
    constexpr size_t CAP = 16 * 1024;
    char *buf = (char *)psAlloc(CAP);
    if (!buf) return sendError(r, 500, "Minnet räcker inte");
    uint32_t next = 0;
    size_t n = MyLog::read(buf, CAP, (uint32_t)from, &next);
    char hdr[16];
    snprintf(hdr, sizeof(hdr), "%lu", (unsigned long)next);
    httpd_resp_set_type(r, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_set_hdr(r, "X-Next-Seq", hdr);
    esp_err_t e = httpd_resp_send(r, buf, n);
    free(buf);
    return e;
}

// GET /api/events
static esp_err_t eventsHandler(httpd_req_t *r)
{
    constexpr size_t CAP = 16 * 1024;
    char *buf = (char *)psAlloc(CAP);
    if (!buf) return sendError(r, 500, "Minnet räcker inte");
    size_t n = MyLog::readEvents(buf, CAP);
    httpd_resp_set_type(r, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send(r, buf, n);
    free(buf);
    return e;
}

static esp_err_t rebootHandler(httpd_req_t *r)
{
    sendOk(r);
    rebootSoon();
    return ESP_OK;
}

// POST /api/ota: multipart/form-data, file field "fw". Streams the part body into MyOta,
// holding back enough bytes to recognise the closing "\r\n--boundary".
static esp_err_t otaHandler(httpd_req_t *r)
{
    Req q(r);   // Query only (multipart body is not parsed by Req)
    if (!q.adminAllowed()) return sendError(r, 403, "Fel admin-PIN");
    if (MyOta::busy()) return sendError(r, 409, "En uppdatering pågår redan");
    char ct[128] = "";
    httpd_req_get_hdr_value_str(r, "Content-Type", ct, sizeof(ct));
    const char *b = strstr(ct, "boundary=");
    if (!b) return sendError(r, 400, "Ingen fil mottogs");
    char delim[80];
    snprintf(delim, sizeof(delim), "\r\n--%s", b + 9);
    if (char *semi = strchr(delim + 4, ';')) *semi = 0;
    size_t dl = strlen(delim);

    long total = 0;
    bool bad = false;
    q.getInt("size", 1, 0x7FFFFFFF, total, bad);
    ESP_LOGI(TAG, "OTA upload, %ld bytes announced, request %u bytes", total, (unsigned)r->content_len);

    constexpr size_t CHUNK = 4096;
    char *buf = (char *)malloc(CHUNK + 128);
    if (!buf) return sendError(r, 500, "Minnet räcker inte");
    size_t have = 0, remaining = r->content_len;
    bool inBody = false, done = false, started = false;
    const char *err = nullptr;
    int timeouts = 0;

    while (!done && !err) {
        if (remaining > 0 && have < CHUNK) {
            int n = httpd_req_recv(r, buf + have, remaining < CHUNK - have ? remaining : CHUNK - have);
            if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) continue;
            if (n <= 0) { err = "Uppladdningen avbröts"; break; }
            timeouts = 0;
            have += n;
            remaining -= n;
        }
        if (!inBody) {
            // Part headers end with an empty line; only the "fw" part is used.
            buf[have] = 0;
            char *hdrEnd = (char *)memmem(buf, have, "\r\n\r\n", 4);
            if (!hdrEnd) {
                if (have >= CHUNK || remaining == 0) err = "Ingen fil mottogs";
                continue;
            }
            *hdrEnd = 0;
            if (!strstr(buf, "name=\"fw\"")) { err = "Ingen fil mottogs"; break; }
            size_t skip = (size_t)(hdrEnd + 4 - buf);
            memmove(buf, buf + skip, have - skip);
            have -= skip;
            inBody = true;
            if (!MyOta::begin(total > 0 ? (uint32_t)total : 0)) { err = "Kunde inte starta uppdateringen"; break; }
            started = true;
        }
        char *end = (char *)memmem(buf, have, delim, dl);
        size_t usable = end ? (size_t)(end - buf) : (have > dl ? have - dl : 0);
        if (remaining == 0 && !end) { err = "Ingen fil mottogs"; break; }
        if (usable && !MyOta::write((const uint8_t *)buf, usable)) { err = "Skrivfel"; break; }
        memmove(buf, buf + usable, have - usable);
        have -= usable;
        if (end) done = true;
    }
    free(buf);

    if (!err && started && !MyOta::end()) err = "Verifieringen misslyckades";
    if (err) {
        // Drain the rest so the browser gets the answer.
        char tmp[256];
        while (remaining > 0) {
            int n = httpd_req_recv(r, tmp, remaining < sizeof(tmp) ? remaining : sizeof(tmp));
            if (n <= 0) break;
            remaining -= n;
        }
        if (MyOta::busy()) MyOta::abort("Uppladdningen avbröts");
        MyOta::State st = MyOta::state();
        char msg[128];
        if (st.phase == MyOta::Phase::FAILED && st.error[0]) snprintf(msg, sizeof(msg), "%s: %s", err, st.error);
        else snprintf(msg, sizeof(msg), "%s", err);
        return sendError(r, 400, msg);
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddNumberToObject(o, "bytes", MyOta::state().written);
    sendJson(r, 200, o);
    rebootSoon();
    return ESP_OK;
}

static esp_err_t notFound(httpd_req_t *r, httpd_err_code_t)
{
    if (strncmp(r->uri, "/api/", 5) == 0) return sendError(r, 404, "Okänd endpoint");
    httpd_resp_set_status(r, "302 Found");
    httpd_resp_set_hdr(r, "Location", "/");
    return httpd_resp_send(r, nullptr, 0);
}

bool begin(const Config &cfg)
{
    if (s_server) return true;
    s_cfg = cfg;
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.server_port = cfg.port;
    c.max_uri_handlers = cfg.maxUris;
    c.stack_size = cfg.stackSize;
    c.lru_purge_enable = true;
    c.uri_match_fn = httpd_uri_match_wildcard;
    c.recv_wait_timeout = 10;
    c.send_wait_timeout = 10;
    c.core_id = 0;
    esp_err_t e = httpd_start(&s_server, &c);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(e));
        s_server = nullptr;
        return false;
    }
    on("/api/log", HTTP_GET, logHandler);
    on("/api/events", HTTP_GET, eventsHandler);
    on("/api/ota", HTTP_POST, otaHandler);
    on("/api/reboot", HTTP_POST, rebootHandler);
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, notFound);
    ESP_LOGI(TAG, "server started on port %u", cfg.port);
    return true;
}

bool started() { return s_server != nullptr; }
httpd_handle_t handle() { return s_server; }

}  // namespace MyWeb
