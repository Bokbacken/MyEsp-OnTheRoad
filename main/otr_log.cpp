// otr_log.cpp - v0.13.7 log ring + SD rotation + SSE stream + ZIP download (see otr_log.h).
//
// Threading:
// - The vprintf hook runs in whatever task logs. It first calls the previous hook (MyLog: console
//   echo + line ring), then formats into its own small buffer in a separate non-inlined function
//   (so the two stack frames do not add up) and appends to the ring under a spinlock.
// - The SD writer task owns its read position; s_sdMtx serialises it with the ZIP download so the
//   log files do not grow between the CRC pass and the send pass.
// - Every SSE client runs in its own task on an async httpd request (httpd keeps serving others).
#include "otr_log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include <atomic>
#include <string>
#include <vector>

#include "board_sd.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "my_web.h"
#include "otr_log_core.h"

#ifndef FW_VERSION
#define FW_VERSION "?"
#endif

namespace {

const char *TAG = "otrlog";

constexpr size_t RING_BYTES = 64 * 1024;
constexpr size_t LINE_CAP = 256;
constexpr size_t FILE_MAX = 256 * 1024;
constexpr int FILE_COUNT = 3;
constexpr const char *LOG_DIR = "/sdcard/otr/logs";
constexpr int MAX_SSE = 2;
constexpr size_t SSE_BACKLOG = 8 * 1024;   // Bytes of history a new stream client gets
constexpr uint32_t SD_PERIOD_MS = 2000;

OtrLogCore::Ring s_ring;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
vprintf_like_t s_prev = nullptr;
SemaphoreHandle_t s_sdMtx = nullptr;
std::atomic<int> s_sseClients{0};
std::atomic<bool> s_sdEnabled{true};
std::atomic<int> s_logLevel{ESP_LOG_INFO};
bool s_ready = false;

void *psAlloc(size_t n)
{
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}

// Thread-safe ring snapshot read
size_t ringRead(char *out, size_t max, uint64_t *from)
{
    portENTER_CRITICAL(&s_mux);
    const size_t n = s_ring.read(out, max, from);
    portEXIT_CRITICAL(&s_mux);
    return n;
}

uint64_t ringHead()
{
    portENTER_CRITICAL(&s_mux);
    const uint64_t h = s_ring.head();
    portEXIT_CRITICAL(&s_mux);
    return h;
}

__attribute__((noinline)) void capture(const char *fmt, va_list args)
{
    char line[LINE_CAP + 1];   // +1: cleanLine may append the '\n'
    const int n = vsnprintf(line, LINE_CAP, fmt, args);
    if (n <= 0) return;
    size_t len = (size_t)n < LINE_CAP ? (size_t)n : LINE_CAP - 1;
    len = OtrLogCore::cleanLine(line, len, line);   // In place: output never overtakes input
    if (!len) return;
    portENTER_CRITICAL(&s_mux);
    s_ring.write(line, len);
    portEXIT_CRITICAL(&s_mux);
}

int hook(const char *fmt, va_list args)
{
    int r = 0;
    if (s_prev) {
        va_list copy;
        va_copy(copy, args);
        r = s_prev(fmt, copy);
        va_end(copy);
    }
    capture(fmt, args);
    return r;
}

// ---- SD rotation ----------------------------------------------------------------------------
void filePath(char *out, size_t cap, int i) { snprintf(out, cap, "%s/otr_%d.log", LOG_DIR, i); }

void rotate()
{
    char a[48], b[48];
    filePath(a, sizeof(a), FILE_COUNT - 1);
    remove(a);
    for (int i = FILE_COUNT - 2; i >= 0; i--) {
        filePath(a, sizeof(a), i);
        filePath(b, sizeof(b), i + 1);
        rename(a, b);
    }
}

// Appends data to otr_0.log (rotating first when it is full). Caller holds s_sdMtx.
bool appendSd(const char *data, size_t n)
{
    BoardSd::Guard g;
    if (!BoardSd::transferHealthy()) return false;
    struct stat st = {};
    if (stat(LOG_DIR, &st) != 0) {
        mkdir("/sdcard/otr", 0775);
        if (mkdir(LOG_DIR, 0775) != 0 && errno != EEXIST) {
            const int dirError = errno;
            ESP_LOGW(TAG, "SD log directory creation failed (%d)", dirError);
            if (dirError == EIO) BoardSd::requestRecovery();
            return false;
        }
    }
    char p[48];
    filePath(p, sizeof(p), 0);
    if (stat(p, &st) == 0 && (size_t)st.st_size + n > FILE_MAX) rotate();
    FILE *f = fopen(p, "ab");
    if (!f) {
        const int openError = errno;
        ESP_LOGW(TAG, "SD log open failed (%d); preserving RAM backlog", openError);
        if (openError == EIO) BoardSd::requestRecovery();
        return false;
    }
    const bool ok = fwrite(data, 1, n, f) == n;
    const int writeError = ok ? 0 : (errno ? errno : EIO);
    const bool closed = fclose(f) == 0;
    const int closeError = closed ? 0 : (errno ? errno : EIO);
    if (!ok || !closed) {
        ESP_LOGW(TAG, "SD log append failed; preserving RAM backlog");
        if (writeError == EIO || closeError == EIO) BoardSd::requestRecovery();
    }
    return ok && closed;
}

void sdTask(void *)
{
    char *buf = (char *)psAlloc(8192);
    uint64_t pos = 0;
    bool wasMounted = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(SD_PERIOD_MS));
        if (!buf) continue;
        const bool mounted = BoardSd::transferHealthy();
        if (!s_sdEnabled.load()) {
            wasMounted = false;
            continue;
        }
        if (mounted && !wasMounted) {   // (Re)inserted card: mark the gap, then write the backlog
            static const char mark[] = "----- otr log: SD attached, " FW_VERSION " -----\n";
            xSemaphoreTake(s_sdMtx, portMAX_DELAY);
            appendSd(mark, sizeof(mark) - 1);
            xSemaphoreGive(s_sdMtx);
        }
        wasMounted = mounted;
        if (!mounted) {
            continue;
        }
        xSemaphoreTake(s_sdMtx, portMAX_DELAY);
        size_t n;
        uint64_t next = pos;
        while (s_sdEnabled.load() && (n = ringRead(buf, 8192, &next)) > 0) {
            if (!appendSd(buf, n)) break;
            pos = next;
        }
        xSemaphoreGive(s_sdMtx);
    }
}

// ---- SSE ------------------------------------------------------------------------------------
bool sseSend(httpd_req_t *r, const char *s, size_t n) { return httpd_resp_send_chunk(r, s, (ssize_t)n) == ESP_OK; }

// Sends complete lines from buf[0..n) as SSE events. Returns the bytes consumed (up to the last
// '\n'); -1 when the client is gone.
long sseLines(httpd_req_t *r, const char *buf, size_t n, std::string &out)
{
    out.clear();
    size_t start = 0, used = 0;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] != '\n') continue;
        out += "data: ";
        out.append(buf + start, i - start);
        out += "\n\n";
        start = used = i + 1;
    }
    if (n >= 1024 && used == 0) {   // A line longer than the buffer: flush it anyway
        out += "data: ";
        out.append(buf, n);
        out += "\n\n";
        used = n;
    }
    if (!out.empty() && !sseSend(r, out.data(), out.size())) return -1;
    return (long)used;
}

void sseTask(void *arg)
{
    httpd_req_t *r = (httpd_req_t *)arg;
    char *buf = (char *)psAlloc(4096);
    std::string out;
    uint64_t pos = ringHead();
    pos = pos > SSE_BACKLOG ? pos - SSE_BACKLOG : 0;
    bool alive = buf != nullptr;
    if (alive) {   // Skip to the start of a whole line
        uint64_t p = pos;
        char c;
        while (p > 0 && ringRead(&c, 1, &p) == 1 && c != '\n') {}
        if (p > 0) pos = p;
        static const char hello[] = ": otr log stream\nretry: 3000\n\n";
        alive = sseSend(r, hello, sizeof(hello) - 1);
    }
    int idle = 0;
    while (alive) {
        uint64_t p = pos;
        const size_t n = ringRead(buf, 4096, &p);
        if (p - n > pos) pos = p - n;   // Ring overwrote what we had not sent yet
        if (n > 0) {
            const long used = sseLines(r, buf, n, out);
            if (used < 0) break;
            pos += (uint64_t)used;
            if (used > 0) idle = 0;
            if (n == 4096 && used > 0) continue;   // More waiting
        }
        if (++idle >= 40) {   // ~10 s: keep-alive comment, also detects a closed browser tab
            idle = 0;
            if (!sseSend(r, ": ping\n\n", 8)) break;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    httpd_resp_send_chunk(r, nullptr, 0);
    httpd_req_async_handler_complete(r);
    free(buf);
    s_sseClients--;
    vTaskDelete(nullptr);
}

esp_err_t handleStream(httpd_req_t *req)
{
    if (!s_ready) return MyWeb::sendError(req, 503, "log capture off");
    if (++s_sseClients > MAX_SSE) {
        s_sseClients--;
        return MyWeb::sendError(req, 503, "max 2 log streams");
    }
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "X-Accel-Buffering", "no");
    httpd_req_t *copy = nullptr;
    if (httpd_req_async_handler_begin(req, &copy) != ESP_OK) {
        s_sseClients--;
        return MyWeb::sendError(req, 500, "async");
    }
    if (xTaskCreate(sseTask, "log_sse", 4096, copy, 2, nullptr) != pdPASS) {
        httpd_req_async_handler_complete(copy);
        s_sseClients--;
        return ESP_FAIL;
    }
    return ESP_OK;
}

// ---- ZIP download ---------------------------------------------------------------------------
struct ZipOut {
    httpd_req_t *r;
    uint32_t off = 0;
    bool ok = true;
    std::vector<OtrLogCore::ZipEntry> entries;
    uint16_t t = 0, d = 0;
    std::vector<uint8_t> hdr;

    void send(const void *p, size_t n)
    {
        if (!ok || !n) return;
        ok = httpd_resp_send_chunk(r, (const char *)p, (ssize_t)n) == ESP_OK;
        off += (uint32_t)n;
    }
    void begin(const char *name, uint32_t crc, uint32_t size)
    {
        OtrLogCore::ZipEntry e{name, crc, size, off};
        OtrLogCore::zipLocal(hdr, e, t, d);
        send(hdr.data(), hdr.size());
        entries.push_back(e);
    }
    void memEntry(const char *name, const char *data, size_t n)
    {
        begin(name, OtrLogCore::crc32Update(0, data, n), (uint32_t)n);
        send(data, n);
    }
    void finish()
    {
        OtrLogCore::zipDirectory(hdr, entries, off, t, d);
        send(hdr.data(), hdr.size());
        if (ok) httpd_resp_send_chunk(r, nullptr, 0);
    }
};

// Streams one SD file in two passes (CRC, then data). Caller holds s_sdMtx.
void zipFile(ZipOut &z, const char *path, const char *name, char *buf, size_t cap)
{
    BoardSd::Transfer transfer;
    if (!transfer.active) {
        ESP_LOGW(TAG, "SD log ZIP interrupted by unavailable card or recovery");
        z.ok = false;
        return;
    }
    FILE *f;
    {
        BoardSd::Guard g;
        f = BoardSd::mounted() ? fopen(path, "rb") : nullptr;
    }
    if (!f) return;
    uint32_t crc = 0, size = 0;
    for (;;) {
        size_t n;
        {
            BoardSd::Guard g;
            if (!BoardSd::transferHealthy()) {
                z.ok = false;
                break;
            }
            n = fread(buf, 1, cap, f);
            if (ferror(f)) {
                BoardSd::requestRecovery();
                z.ok = false;
                break;
            }
        }
        if (!n) break;
        crc = OtrLogCore::crc32Update(crc, buf, n);
        size += (uint32_t)n;
    }
    {
        BoardSd::Guard g;
        rewind(f);
    }
    if (z.ok) z.begin(name, crc, size);
    uint32_t left = size;
    while (z.ok && left > 0) {
        size_t n;
        {
            BoardSd::Guard g;
            if (!BoardSd::transferHealthy()) {
                z.ok = false;
                break;
            }
            n = fread(buf, 1, left < cap ? left : cap, f);
        }
        if (!n || ferror(f)) {
            ESP_LOGW(TAG, "SD log ZIP read failed");
            BoardSd::requestRecovery();
            z.ok = false;
            break;
        }
        z.send(buf, n);
        left -= (uint32_t)n;
    }
    BoardSd::Guard g;
    if (fclose(f) != 0) {
        ESP_LOGW(TAG, "SD log ZIP close failed");
        BoardSd::requestRecovery();
        z.ok = false;
    }
}

esp_err_t handleDownload(httpd_req_t *req)
{
    if (!s_ready) return MyWeb::sendError(req, 503, "log capture off");
    MyWeb::Req q(req);
    bool ramOnly = false;
    q.getBool("ram", ramOnly);
    char *ram = (char *)psAlloc(RING_BYTES);
    char *buf = (char *)psAlloc(8192);
    if (!ram || !buf) {
        free(ram);
        free(buf);
        return MyWeb::sendError(req, 500, "memory");
    }
    uint64_t from = 0;
    const size_t ramLen = ringRead(ram, RING_BYTES, &from);

    time_t now = time(nullptr);
    struct tm tm = {};
    localtime_r(&now, &tm);
    char fname[96], disp[128];
    if (tm.tm_year + 1900 >= 2024)
        strftime(fname, sizeof(fname), "otr_logs_%Y%m%d_%H%M%S.zip", &tm);
    else
        snprintf(fname, sizeof(fname), "otr_logs_up%llus.zip", (unsigned long long)(esp_timer_get_time() / 1000000));
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", fname);
    httpd_resp_set_type(req, "application/zip");
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    ZipOut z;
    z.r = req;
    z.t = OtrLogCore::dosTime(tm.tm_hour, tm.tm_min, tm.tm_sec);
    z.d = OtrLogCore::dosDate(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);

    char info[384];
    const int il = snprintf(info, sizeof(info),
                            "MyEsp-OnTheRoad %s\nuptime_s=%llu\nfree_heap=%u\nfree_internal=%u\nram_log_bytes=%u\n"
                            "captured_total=%llu\nsd=%s\nreset_reason=%d\nsd_log_enabled=%d\n",
                            FW_VERSION, (unsigned long long)(esp_timer_get_time() / 1000000),
                            (unsigned)esp_get_free_heap_size(),
                            (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)ramLen,
                            (unsigned long long)ringHead(), BoardSd::mounted() ? "mounted" : "none",
                            (int)esp_reset_reason(), s_sdEnabled.load());
    z.memEntry("info.txt", info, il > 0 ? (size_t)il : 0);
    z.memEntry("ram.log", ram, ramLen);
    free(ram);

    if (!ramOnly) {
        xSemaphoreTake(s_sdMtx, portMAX_DELAY);
        for (int i = FILE_COUNT - 1; i >= 0 && z.ok; i--) {   // Oldest first
            char p[48], n[16];
            filePath(p, sizeof(p), i);
            snprintf(n, sizeof(n), "otr_%d.log", i);
            zipFile(z, p, n, buf, 8192);
        }
        xSemaphoreGive(s_sdMtx);
    }
    free(buf);
    z.finish();
    return z.ok ? ESP_OK : ESP_FAIL;
}

esp_err_t handleSdLogStatus(httpd_req_t *req)
{
    return MyWeb::sendJsonStr(req, 200, s_sdEnabled.load() ? "{\"enabled\":true}" : "{\"enabled\":false}");
}

esp_err_t handleSdLogControl(httpd_req_t *req)
{
    MyWeb::Req q(req);
    if (!q.adminAllowed()) return MyWeb::sendError(req, 403, "Admin PIN required");
    char value[8];
    if (!q.get("enabled", value, sizeof(value)) || (strcmp(value, "0") && strcmp(value, "1")))
        return MyWeb::sendError(req, 400, "enabled must be 0 or 1");
    s_sdEnabled.store(!strcmp(value, "1"));
    ESP_LOGI(TAG, "SD log persistence %s (RAM capture remains on)",
             s_sdEnabled.load() ? "enabled" : "paused");
    return handleSdLogStatus(req);
}

const char *logLevelName(int level)
{
    switch (level) {
        case ESP_LOG_ERROR: return "error";
        case ESP_LOG_WARN: return "warning";
        case ESP_LOG_DEBUG: return "debug";
        default: return "info";
    }
}

esp_err_t handleLogConfigStatus(httpd_req_t *req)
{
    char json[180];
    snprintf(json, sizeof(json),
             "{\"level\":\"%s\",\"sd_trace\":%s,\"sd_trace_supported\":%s,\"trace_seconds\":%u}",
             logLevelName(s_logLevel.load()), BoardSd::traceEnabled() ? "true" : "false",
             BoardSd::traceSupported() ? "true" : "false",
             (unsigned)BoardSd::traceRemainingSeconds());
    return MyWeb::sendJsonStr(req, 200, json);
}

esp_err_t handleLogConfigControl(httpd_req_t *req)
{
    MyWeb::Req q(req);
    if (!q.adminAllowed()) return MyWeb::sendError(req, 403, "Admin PIN required");
    char level[16], trace[8];
    if (!q.get("level", level, sizeof(level)) || !q.get("sd_trace", trace, sizeof(trace)) ||
        (strcmp(trace, "0") && strcmp(trace, "1")))
        return MyWeb::sendError(req, 400, "level and sd_trace=0|1 required");
    int selected;
    if (!strcmp(level, "error")) selected = ESP_LOG_ERROR;
    else if (!strcmp(level, "warning")) selected = ESP_LOG_WARN;
    else if (!strcmp(level, "info")) selected = ESP_LOG_INFO;
    else if (!strcmp(level, "debug")) selected = ESP_LOG_DEBUG;
    else return MyWeb::sendError(req, 400, "Invalid log level");
    const bool enabled = !strcmp(trace, "1");
    if (enabled && !BoardSd::traceSupported())
        return MyWeb::sendError(req, 400, "SD tracing is unavailable on this build");
    xSemaphoreTake(s_sdMtx, portMAX_DELAY);
    esp_log_level_set("*", (esp_log_level_t)selected);
    s_logLevel.store(selected);
    BoardSd::setTrace(enabled);
    // Explicit SD tracing remains visible even when general verbosity is warning/error.
    esp_log_level_set("sdtrace", enabled ? ESP_LOG_INFO : (esp_log_level_t)selected);
    xSemaphoreGive(s_sdMtx);
    return handleLogConfigStatus(req);
}

}  // namespace

bool otr_log_init()
{
    if (s_ready) return true;
    char *mem = (char *)psAlloc(RING_BYTES);
    s_sdMtx = xSemaphoreCreateMutex();
    if (!mem || !s_sdMtx) {
        free(mem);
        ESP_LOGE(TAG, "no memory for the log ring");
        return false;
    }
    s_ring.attach(mem, RING_BYTES);
    s_ready = true;
    s_prev = esp_log_set_vprintf(hook);
    if (xTaskCreate(sdTask, "log_sd", 4096, nullptr, 1, nullptr) != pdPASS) ESP_LOGW(TAG, "SD writer not started");
    ESP_LOGI(TAG, "capture on: %u KB ring, SD %s/otr_{0..2}.log x %u KB", (unsigned)(RING_BYTES / 1024), LOG_DIR,
             (unsigned)(FILE_MAX / 1024));
    return true;
}

bool otr_log_register_routes()
{
    bool ok = MyWeb::on("/api/log/stream", HTTP_GET, handleStream);
    ok &= MyWeb::on("/api/log/download", HTTP_GET, handleDownload);
    ok &= MyWeb::on("/api/log/sd", HTTP_GET, handleSdLogStatus);
    ok &= MyWeb::on("/api/log/sd", HTTP_POST, handleSdLogControl);
    ok &= MyWeb::on("/api/log/config", HTTP_GET, handleLogConfigStatus);
    ok &= MyWeb::on("/api/log/config", HTTP_POST, handleLogConfigControl);
    return ok;
}

uint64_t otr_log_bytes() { return s_ready ? ringHead() : 0; }
