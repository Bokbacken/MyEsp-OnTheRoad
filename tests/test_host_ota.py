#!/usr/bin/env python3
"""Compile and exercise the C6 Host OTA HTTP handler with transport doubles."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

STUBS = r'''
#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1;
constexpr int ESP_IMAGE_HEADER_MAGIC = 0xE9, ESP_CHIP_ID_ESP32C6 = 13;
constexpr int HTTP_POST = 1, HTTPD_SOCK_ERR_TIMEOUT = -2;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
#define EH_HOST_CP_OTA_CHUNK_MAX 1480u
inline const char *esp_err_to_name(int) { return "injected error"; }
struct esp_image_header_t {
    uint8_t magic;
    uint8_t reserved[11];
    uint16_t chip_id;
    uint8_t rest[10];
};
struct httpd_req_t {
    size_t content_len = 0, body_size = 0, offset = 0;
    const uint8_t *body = nullptr;
    bool admin = true, has_size = true;
    long announced_size = 0;
    int status = 0;
    char message[128] = {};
};
struct Json { bool ok = false; long bytes = 0; bool restarting = false; };
using cJSON = Json;
inline int httpd_req_recv(httpd_req_t *r, char *dst, size_t want) {
    if (r->offset == r->body_size) return -1;
    size_t n = std::min(want, r->body_size - r->offset);
    memcpy(dst, r->body + r->offset, n);
    r->offset += n;
    return (int)n;
}
namespace MyWeb {
using Handler = esp_err_t (*)(httpd_req_t *);
struct Req {
    explicit Req(httpd_req_t *r) : req(r) {}
    httpd_req_t *req;
    bool adminAllowed() const { return req->admin; }
    bool getInt(const char *, long, long, long &out, bool &) const {
        out = req->announced_size;
        return req->has_size;
    }
};
inline Handler registered = nullptr;
inline bool on(const char *, int, Handler h) { registered = h; return true; }
inline esp_err_t sendError(httpd_req_t *r, int status, const char *msg) {
    r->status = status;
    snprintf(r->message, sizeof(r->message), "%s", msg);
    return ESP_OK;
}
inline esp_err_t sendJson(httpd_req_t *r, int status, cJSON *o) {
    r->status = status;
    snprintf(r->message, sizeof(r->message), "%s", o->ok ? "ok" : "");
    delete o;
    return ESP_OK;
}
inline void rebootDelay(uint32_t delay);
inline uint32_t rebootMs = 0;
inline void rebootSoon(uint32_t ms) { rebootMs = ms; }
}
inline void cJSON_AddBoolToObject(cJSON *j, const char *name, bool v) {
    if (!strcmp(name, "ok")) j->ok = v;
    else if (!strcmp(name, "restarting")) j->restarting = v;
}
inline void cJSON_AddNumberToObject(cJSON *j, const char *, long n) { j->bytes = n; }
inline cJSON *cJSON_CreateObject() { return new cJSON; }
struct eh_host_coprocessor_fwver_t { uint32_t major1, minor1, patch1; };
inline eh_host_coprocessor_fwver_t cpVersion{2, 6, 0};
inline int versionResult = ESP_OK, otaBeginResult = ESP_OK, otaWriteResult = ESP_OK,
           otaEndResult = ESP_OK, otaActivateResult = ESP_OK;
inline int versionCalls = 0, beginCalls = 0, writeCalls = 0, endCalls = 0, activateCalls = 0;
inline size_t bytesWritten = 0, maxWrite = 0, firstWrite = 0;
namespace MyOta {
inline bool running = false, lastOk = false;
inline uint32_t total = 0, progress = 0;
inline bool beginExternal(uint32_t t) { if (running) return false; running = true; total = t; progress = 0; return true; }
inline void progressExternal(uint32_t w) { progress = w; }
inline void finishExternal(bool ok, const char * = nullptr) { running = false; lastOk = ok; }
}
inline int eh_host_sys_get_cp_fw_version(eh_host_coprocessor_fwver_t *v) {
    ++versionCalls; *v = cpVersion; return versionResult;
}
inline int esp_hosted_slave_ota_begin() { ++beginCalls; return otaBeginResult; }
inline int esp_hosted_slave_ota_write(const uint8_t *, uint32_t n) {
    if (!writeCalls) firstWrite = n;
    ++writeCalls; bytesWritten += n; maxWrite = std::max<size_t>(maxWrite, n);
    assert(n <= EH_HOST_CP_OTA_CHUNK_MAX);
    return otaWriteResult;
}
inline int esp_hosted_slave_ota_end() { ++endCalls; return otaEndResult; }
inline int esp_hosted_slave_ota_activate() { ++activateCalls; return otaActivateResult; }
'''

TEST = r'''
#include "host_ota.h"
int main() {
    assert(HostOta::registerRoute() && MyWeb::registered);
    uint8_t image[2048] = {};
    auto *header = reinterpret_cast<esp_image_header_t *>(image);
    header->magic = ESP_IMAGE_HEADER_MAGIC;
    header->chip_id = ESP_CHIP_ID_ESP32C6;
    auto request = [&](bool admin = true) {
        httpd_req_t r{};
        r.content_len = r.body_size = sizeof(image);
        r.body = image;
        r.announced_size = sizeof(image);
        r.admin = admin;
        return r;
    };

    auto unauthorized = request(false);
    assert(MyWeb::registered(&unauthorized) == ESP_OK && unauthorized.status == 403);
    assert(unauthorized.offset == sizeof(image) && beginCalls == 0);

    header->chip_id = 9;
    auto wrongChip = request();
    assert(MyWeb::registered(&wrongChip) == ESP_OK && wrongChip.status == 400);
    assert(wrongChip.offset == sizeof(image) && beginCalls == 0);
    header->chip_id = ESP_CHIP_ID_ESP32C6;

    cpVersion = {2, 5, 9};
    auto oldFirmware = request();
    assert(MyWeb::registered(&oldFirmware) == ESP_OK && oldFirmware.status == 200);
    assert(oldFirmware.offset == sizeof(image));
    assert(beginCalls == 1 && writeCalls == 2 && bytesWritten == sizeof(image));
    assert(endCalls == 1 && activateCalls == 0 && MyWeb::rebootMs == 3000);

    versionCalls = beginCalls = writeCalls = endCalls = activateCalls = 0;
    bytesWritten = MyWeb::rebootMs = 0;
    versionResult = ESP_FAIL;
    auto unknownFirmware = request();
    assert(MyWeb::registered(&unknownFirmware) == ESP_OK && unknownFirmware.status == 200);
    assert(unknownFirmware.offset == sizeof(image));
    assert(versionCalls == 1 && beginCalls == 1 && writeCalls == 2);
    assert(bytesWritten == sizeof(image) && endCalls == 1 && activateCalls == 0);

    versionResult = ESP_OK;
    versionCalls = beginCalls = writeCalls = endCalls = activateCalls = 0;
    bytesWritten = MyWeb::rebootMs = 0;
    cpVersion = {2, 6, 0};
    auto valid = request();
    assert(MyWeb::registered(&valid) == ESP_OK && valid.status == 200);
    assert(valid.offset == sizeof(image));
    assert(beginCalls == 1 && writeCalls == 2 && bytesWritten == sizeof(image));
    assert(endCalls == 1 && activateCalls == 1 && MyWeb::rebootMs == 3000);
    assert(firstWrite == 1400 && maxWrite == 1400);
    assert(!MyOta::running && MyOta::lastOk && MyOta::progress == sizeof(image));

    beginCalls = writeCalls = endCalls = 0;
    otaWriteResult = ESP_FAIL;
    auto failedWrite = request();
    assert(MyWeb::registered(&failedWrite) == ESP_OK && failedWrite.status == 500);
    assert(failedWrite.offset == sizeof(image) && writeCalls == 1 && endCalls == 1);
    assert(!MyOta::running && !MyOta::lastOk);
    otaWriteResult = ESP_OK;

    MyOta::running = true;
    beginCalls = 0;
    auto busyApp = request();
    assert(MyWeb::registered(&busyApp) == ESP_OK && busyApp.status == 409 && beginCalls == 0);
    assert(busyApp.offset == sizeof(image));
}
'''

with tempfile.TemporaryDirectory(prefix="otr-host-ota-") as directory:
    work = pathlib.Path(directory)
    (work / "stubs.h").write_text(STUBS)
    headers = {
        "esp_http_server.h": '#include "stubs.h"\n',
        "esp_hosted_ota.h": '#include "stubs.h"\n',
        "eh_host_sys.h": '#include "stubs.h"\n',
        "eh_host_api_types.h": '#include "stubs.h"\n',
        "esp_log.h": '#include "stubs.h"\n',
        "freertos/FreeRTOS.h": '#include "stubs.h"\n',
        "my_web.h": '#include "stubs.h"\n',
        "my_ota.h": '#include "stubs.h"\n',
    }
    for name, content in headers.items():
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    (work / "test.cpp").write_text('#include "stubs.h"\n' + TEST)
    binary = work / "test"
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        f"-I{work}", f"-I{ROOT / 'main'}",
        str(ROOT / "main/host_ota.cpp"), str(work / "test.cpp"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
print("C6 Host OTA: authorization, chip/version checks, chunk limit, progress, streaming, activation and restart passed")
