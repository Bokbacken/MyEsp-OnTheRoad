// web_util.h - v0.13.7 helpers shared by the newer web handlers: JSON request bodies (MyWeb::Req
// only parses url-encoded forms) and JSON string building without cJSON node overhead (large
// lists are built in one std::string, which lands in PSRAM, instead of thousands of small nodes).
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include "cJSON.h"
#include "esp_http_server.h"

namespace WebUtil {

// Reads and parses a JSON body of at most maxLen bytes. nullptr when missing, too big or invalid.
inline cJSON *readJson(httpd_req_t *r, size_t maxLen)
{
    if (r->content_len == 0 || r->content_len > maxLen) return nullptr;
    char *buf = (char *)malloc(r->content_len + 1);
    if (!buf) return nullptr;
    size_t got = 0;
    int timeouts = 0;
    while (got < r->content_len) {
        const int n = httpd_req_recv(r, buf + got, r->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) continue;
        if (n <= 0) break;
        got += (size_t)n;
    }
    buf[got] = 0;
    cJSON *j = got == r->content_len ? cJSON_Parse(buf) : nullptr;
    free(buf);
    return j;
}

// Appends s as a quoted JSON string
inline void appendStr(std::string &o, const char *s)
{
    o += '"';
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        switch (*p) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (*p < 0x20) {
                char u[8];
                snprintf(u, sizeof(u), "\\u%04x", *p);
                o += u;
            } else {
                o += (char)*p;
            }
        }
    }
    o += '"';
}

inline void appendNum(std::string &o, unsigned long long v)
{
    char b[24];
    snprintf(b, sizeof(b), "%llu", v);
    o += b;
}

// Sends a prepared JSON text in chunks (no Content-Length needed)
inline esp_err_t sendJsonText(httpd_req_t *r, const std::string &json)
{
    httpd_resp_set_type(r, "application/json");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    const size_t CH = 4096;
    for (size_t off = 0; off < json.size(); off += CH) {
        const size_t n = json.size() - off < CH ? json.size() - off : CH;
        if (httpd_resp_send_chunk(r, json.data() + off, (ssize_t)n) != ESP_OK) return ESP_FAIL;
    }
    return httpd_resp_send_chunk(r, nullptr, 0);
}

}  // namespace WebUtil
