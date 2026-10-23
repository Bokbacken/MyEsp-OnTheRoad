#include "my_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace MyLog {

namespace {

constexpr int TEXT_CAP = 148;
const char *TAG = "log";

struct Entry {
    uint32_t seq;
    int64_t uptimeMs;    // Reference time: ms since boot (esp_timer). Real time = anchor + uptime.
    char level;
    char text[TEXT_CAP];
};

// Events logged before the clock is known wait here and are written with the corrected time
// as soon as setClockAnchor() runs (or after PENDING_MAX_MS / when full, with uptime stamps).
constexpr int PENDING_CAP = 32;
constexpr int64_t PENDING_MAX_MS = 180000;
struct PendingEvent {
    int64_t uptimeMs;
    char tag[16];
    char msg[200];
};
PendingEvent *s_pending = nullptr;
int s_pendingCount = 0;
volatile int64_t s_anchorMs = 0;   // Real time (UTC ms) minus uptime ms; 0 = clock unknown

Entry *s_ring = nullptr;
int s_cap = 0;
bool s_psram = false;
uint32_t s_seq = 0;          // Next sequence number
uint32_t s_dropped = 0;
uint32_t s_events = 0, s_eventErrors = 0;
bool s_echo = true;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
vprintf_like_t s_orig = nullptr;

SemaphoreHandle_t s_fileLock = nullptr;
char s_file[64] = "";
size_t s_fileMax = 0;

// Splits "I (12:34:56.789) tag: msg" / "I (1234) tag: msg" into level + "tag: msg" and strips
// ANSI colour codes and the trailing newline.
void capture(const char *line)
{
    char level = 'I';
    const char *p = line;
    if (p[0] == '\033') {                       // Colour prefix "\033[0;32m"
        const char *m = strchr(p, 'm');
        if (m) p = m + 1;
    }
    if (p[0] && p[1] == ' ' && p[2] == '(') {
        level = p[0];
        const char *close = strchr(p + 3, ')');
        if (close && close[1] == ' ') p = close + 2;
    }
    Entry e;
    e.level = level;
    size_t n = 0;
    for (; *p && n < TEXT_CAP - 1; p++) {
        if (*p == '\033') {                     // Skip "\033[0m"
            while (*p && *p != 'm') p++;
            if (!*p) break;
            continue;
        }
        if (*p == '\n' || *p == '\r') continue;
        e.text[n++] = *p;
    }
    e.text[n] = 0;
    if (n == 0) return;
    e.uptimeMs = esp_timer_get_time() / 1000;
    portENTER_CRITICAL(&s_mux);
    e.seq = s_seq++;
    s_ring[e.seq % s_cap] = e;
    portEXIT_CRITICAL(&s_mux);
}

int hook(const char *fmt, va_list args)
{
    int r = 0;
    if (s_echo && s_orig) {
        va_list copy;
        va_copy(copy, args);
        r = s_orig(fmt, copy);
        va_end(copy);
    }
    if (s_ring) {
        char line[TEXT_CAP + 32];
        int n = vsnprintf(line, sizeof(line), fmt, args);
        if (n > 0) capture(line);
        if (!s_echo) r = n;
    }
    return r;
}

void rotateIfNeeded()
{
    struct stat st;
    if (stat(s_file, &st) != 0 || (size_t)st.st_size < s_fileMax) return;
    char old[72];
    snprintf(old, sizeof(old), "%s.1", s_file);
    remove(old);
    rename(s_file, old);
}

}  // namespace

bool begin(const Config &cfg)
{
    if (s_ring) return true;
    s_cap = cfg.entries > 16 ? cfg.entries : 16;
    s_echo = cfg.echoConsole;
    size_t bytes = sizeof(Entry) * (size_t)s_cap;
    s_ring = (Entry *)heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_psram = s_ring != nullptr;
    if (!s_ring) {
        s_cap = 64;
        s_ring = (Entry *)calloc(s_cap, sizeof(Entry));
    }
    if (!s_ring) return false;
    if (!s_fileLock) s_fileLock = xSemaphoreCreateMutex();
    s_orig = esp_log_set_vprintf(hook);
    ESP_LOGI(TAG, "capture on: %d lines (%s)", s_cap, s_psram ? "PSRAM" : "internal");
    return true;
}

void setEventFile(const char *path, size_t maxBytes)
{
    if (!s_fileLock) s_fileLock = xSemaphoreCreateMutex();
    xSemaphoreTake(s_fileLock, portMAX_DELAY);
    snprintf(s_file, sizeof(s_file), "%s", path ? path : "");
    s_fileMax = maxBytes < 4096 ? 4096 : maxBytes;
    xSemaphoreGive(s_fileLock);
}

void formatTime(char *out, size_t cap, time_t epoch, int64_t uptimeMs)
{
    if (epoch >= MIN_VALID_EPOCH) {
        struct tm tm;
        localtime_r(&epoch, &tm);
        strftime(out, cap, "%Y-%m-%d %H:%M:%S", &tm);
    } else {
        snprintf(out, cap, "+%lld.%03llds", (long long)(uptimeMs / 1000), (long long)(uptimeMs % 1000));
    }
}

void formatRef(char *out, size_t cap, int64_t uptimeMs)
{
    const int64_t a = s_anchorMs;
    formatTime(out, cap, a ? (time_t)((a + uptimeMs) / 1000) : 0, uptimeMs);
}

namespace {

// Caller holds s_fileLock.
void writeEventLine(int64_t uptimeMs, const char *tag, const char *msg)
{
    char ts[32];
    formatRef(ts, sizeof(ts), uptimeMs);
    rotateIfNeeded();
    FILE *f = fopen(s_file, "a");
    // Uptime is always added: it orders events of a boot even when the clock was not set.
    if (!f || fprintf(f, "%s [%llus] %s: %s\n", ts, (unsigned long long)(uptimeMs / 1000), tag, msg) < 0)
        s_eventErrors++;
    if (f) fclose(f);
}

// Caller holds s_fileLock.
void flushPendingLocked()
{
    for (int i = 0; i < s_pendingCount; i++)
        writeEventLine(s_pending[i].uptimeMs, s_pending[i].tag, s_pending[i].msg);
    s_pendingCount = 0;
}

}  // namespace

void setClockAnchor()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    if (tv.tv_sec < MIN_VALID_EPOCH) return;
    const int64_t up = esp_timer_get_time() / 1000;
    const bool first = s_anchorMs == 0;
    s_anchorMs = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000 - up;
    if (first) ESP_LOGI(TAG, "clock anchored: earlier lines of this boot now show real time");
    if (!s_fileLock) return;
    xSemaphoreTake(s_fileLock, portMAX_DELAY);
    if (s_file[0]) flushPendingLocked();
    xSemaphoreGive(s_fileLock);
}

bool clockAnchored() { return s_anchorMs != 0; }

void event(const char *tag, const char *fmt, ...)
{
    char msg[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    ESP_LOGI(tag, "%s", msg);
    s_events++;
    if (!s_fileLock || !s_file[0]) return;
    const int64_t up = esp_timer_get_time() / 1000;
    xSemaphoreTake(s_fileLock, portMAX_DELAY);
    if (!s_anchorMs && up < PENDING_MAX_MS) {
        if (!s_pending)
            s_pending = (PendingEvent *)heap_caps_calloc(PENDING_CAP, sizeof(PendingEvent), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_pending && s_pendingCount < PENDING_CAP) {
            PendingEvent &p = s_pending[s_pendingCount++];
            p.uptimeMs = up;
            snprintf(p.tag, sizeof(p.tag), "%s", tag);
            snprintf(p.msg, sizeof(p.msg), "%s", msg);
            xSemaphoreGive(s_fileLock);
            return;
        }
    }
    flushPendingLocked();   // Clock still unknown after the grace period (or queue full): uptime stamps
    writeEventLine(up, tag, msg);
    xSemaphoreGive(s_fileLock);
}

size_t read(char *out, size_t cap, uint32_t fromSeq, uint32_t *nextSeq)
{
    if (!out || cap < 2) return 0;
    out[0] = 0;
    size_t used = 0;
    portENTER_CRITICAL(&s_mux);
    uint32_t last = s_seq;
    portEXIT_CRITICAL(&s_mux);
    uint32_t first = last > (uint32_t)s_cap ? last - s_cap : 0;
    if (fromSeq < first) fromSeq = first;
    uint32_t seq = fromSeq;
    for (; seq < last; seq++) {
        Entry e;
        portENTER_CRITICAL(&s_mux);
        e = s_ring[seq % s_cap];
        portEXIT_CRITICAL(&s_mux);
        if (e.seq != seq) continue;             // Overwritten meanwhile
        char ts[32];
        formatRef(ts, sizeof(ts), e.uptimeMs);
        int n = snprintf(out + used, cap - used, "%s %c %s\n", ts, e.level, e.text);
        if (n < 0 || used + n >= cap) { out[used] = 0; break; }
        used += n;
    }
    if (nextSeq) *nextSeq = seq;
    return used;
}

size_t readEvents(char *out, size_t cap)
{
    if (!out || cap < 2) return 0;
    out[0] = 0;
    if (!s_fileLock || !s_file[0]) return 0;
    xSemaphoreTake(s_fileLock, portMAX_DELAY);
    size_t n = 0;
    FILE *f = fopen(s_file, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        long start = size > (long)(cap - 1) ? size - (long)(cap - 1) : 0;
        fseek(f, start, SEEK_SET);
        n = fread(out, 1, cap - 1, f);
        fclose(f);
        out[n] = 0;
        if (start > 0) {                        // Drop the partial first line
            char *nl = strchr(out, '\n');
            if (nl) {
                size_t skip = (size_t)(nl + 1 - out);
                memmove(out, nl + 1, n - skip + 1);
                n -= skip;
            }
        }
    }
    xSemaphoreGive(s_fileLock);
    return n;
}

Stats stats()
{
    Stats s{};
    portENTER_CRITICAL(&s_mux);
    s.lines = s_seq;
    portEXIT_CRITICAL(&s_mux);
    s.dropped = s_dropped;
    s.events = s_events;
    s.eventWriteErrors = s_eventErrors;
    s.capacity = s_cap;
    s.inPsram = s_psram;
    return s;
}

}  // namespace MyLog
