// my_web.h - small helpers around esp_http_server, shared by the MyEsp projects.
//
// - begin() starts httpd (wildcard URIs, LRU purge) and registers the common routes:
//     GET  /api/log?from=<seq>   RAM log lines (my_log) as text, header X-Next-Seq
//     GET  /api/events           persistent event log tail (text)
//     POST /api/ota              multipart upload (field "fw", ?size=, ?pin=) -> my_ota, restart
//     POST /api/reboot           restart after 1.5 s
//   plus a JSON 404 for unknown /api/* and a redirect to / for everything else.
// - Req parses the query string and an url-encoded body (POST forms) into one parameter set.
// - Static assets are served from flash (optionally gzip-compressed).
// - Answers are JSON (cJSON) with "Cache-Control: no-store"; errors are {"ok":false,"error":".."}.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "cJSON.h"
#include "esp_http_server.h"

namespace MyWeb {

struct Config {
    uint16_t port = 80;
    uint16_t maxUris = 48;
    uint16_t maxSockets = 7;   // httpd max_open_sockets (<= CONFIG_LWIP_MAX_SOCKETS - 3)
    uint32_t stackSize = 8192;
    bool (*adminOk)(const char *pin) = nullptr;   // nullptr = no admin protection
    void (*onReboot)() = nullptr;                 // Called just before esp_restart()
    bool (*canReboot)() = nullptr;                // Reject requests while storage/OTA is busy.
};

bool begin(const Config &cfg);
bool started();
httpd_handle_t handle();

typedef esp_err_t (*Handler)(httpd_req_t *req);
bool on(const char *uri, httpd_method_t method, Handler fn);

// Flash asset. gzip = data is gzip-compressed (Content-Encoding: gzip).
struct Asset {
    const char *uri;
    const char *mime;
    const uint8_t *data;
    size_t len;
    bool gzip;
};
bool addAsset(const Asset &a);   // The Asset must stay alive (static)

// Request parameters: query string + application/x-www-form-urlencoded body (max 4 kB).
class Req {
public:
    explicit Req(httpd_req_t *r);
    ~Req();
    Req(const Req &) = delete;
    Req &operator=(const Req &) = delete;

    httpd_req_t *raw() const { return r_; }
    bool has(const char *name) const;
    // Copies the decoded value; false when missing or longer than cap-1.
    bool get(const char *name, char *out, size_t cap) const;
    // Integer in [lo, hi]. Returns false when missing; sets bad=true for invalid values.
    bool getInt(const char *name, long lo, long hi, long &out, bool &bad) const;
    bool getBool(const char *name, bool &out) const;   // "1" / "true" / "on" = true
    bool header(const char *name, char *out, size_t cap) const;
    bool adminAllowed() const;   // "pin" parameter or X-Admin-Pin header

private:
    httpd_req_t *r_;
    char *query_ = nullptr;
    char *body_ = nullptr;
};

esp_err_t sendJson(httpd_req_t *r, int code, cJSON *obj);   // Deletes obj
esp_err_t sendJsonStr(httpd_req_t *r, int code, const char *json);
esp_err_t sendError(httpd_req_t *r, int code, const char *msg);
esp_err_t sendOk(httpd_req_t *r);
void setStatus(httpd_req_t *r, int code);

bool rebootSoon(uint32_t ms = 1500);  // False if busy or task creation fails.

}  // namespace MyWeb
