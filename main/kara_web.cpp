// kara_web.cpp - v0.13.5 web karaoke WebSocket (see kara_web.h)
#include "kara_web.h"

#include <stdio.h>
#include <string.h>

#include <string>

#include "board_audio.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "my_web.h"

namespace KaraWeb {
namespace {

const char *TAG = "kara_web";
constexpr size_t MAX_RX = 128;      // Client -> device frames are tiny ({"cmd":"hello"})
constexpr size_t NAME_CAP = 96;

struct Client {
    int fd;
    bool hello;   // Needs the hello frame (sent by the sender task)
};

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
Client s_cl[MAX_CLIENTS] = {{-1, false}, {-1, false}, {-1, false}};
volatile int s_count = 0;
volatile bool s_running = false;

// Song state (UI task writes, sender task reads; guarded by s_mux)
bool s_songOpen = false;
volatile bool s_songOpenAtomic = false;   // Read by the mixer-task tap
volatile bool s_plainLyricsAtomic = false;
uint32_t s_songSeq = 0;
char s_songName[NAME_CAP] = "";
SemaphoreHandle_t s_bookMux = nullptr;
std::string s_plainLyrics;

KaraStream::Stream s_stream;
KaraStream::LyricTracker s_tracker;   // Mixer task only

// Sender task buffers (static: keeps the task stack small)
int16_t s_pcm[KaraStream::FRAME_SAMPLES];
KaraStream::Event s_ev;
char s_json[KaraStream::JSON_CAP];

void tap(const int16_t *pcm, int n, void *)
{
    if (s_songOpenAtomic) s_stream.pushPcm(pcm, (uint32_t)n);
}

void removeFd(int fd)
{
    bool found = false;
    portENTER_CRITICAL(&s_mux);
    for (auto &c : s_cl)
        if (c.fd == fd) {
            c.fd = -1;
            c.hello = false;
            s_count = s_count - 1;
            found = true;
        }
    portEXIT_CRITICAL(&s_mux);
    if (found) ESP_LOGI(TAG, "client %d gone (%d left)", fd, s_count);
}

bool sendFrame(int fd, httpd_ws_type_t type, const void *data, size_t len)
{
    httpd_ws_frame_t f = {};
    f.type = type;
    f.final = true;
    f.payload = (uint8_t *)data;
    f.len = len;
    if (httpd_ws_send_data(MyWeb::handle(), fd, &f) == ESP_OK) return true;
    removeFd(fd);
    httpd_sess_trigger_close(MyWeb::handle(), fd);
    return false;
}

int snapshot(int *fds)
{
    int n = 0;
    portENTER_CRITICAL(&s_mux);
    for (auto &c : s_cl)
        if (c.fd >= 0) fds[n++] = c.fd;
    portEXIT_CRITICAL(&s_mux);
    return n;
}

void broadcast(httpd_ws_type_t type, const void *data, size_t len)
{
    int fds[MAX_CLIENTS];
    const int n = snapshot(fds);
    for (int i = 0; i < n; i++) sendFrame(fds[i], type, data, len);
}

// {"type":"hello"|"state","rate":22050,"frame":512,"open":b,"song":".."}
size_t stateJson(const char *type, char *out, size_t cap)
{
    bool open;
    char name[NAME_CAP];
    portENTER_CRITICAL(&s_mux);
    open = s_songOpen;
    memcpy(name, s_songName, sizeof(name));
    portEXIT_CRITICAL(&s_mux);
    int k = snprintf(out, cap, "{\"type\":\"%s\",\"rate\":%d,\"frame\":%u,\"open\":%s,\"song\":\"", type,
                     KaraStream::RATE, (unsigned)KaraStream::FRAME_SAMPLES, open ? "true" : "false");
    if (k < 0 || (size_t)k >= cap) return 0;
    char *p = out + k;
    char *end = out + cap;
    if (!KaraStream::jsonEscape(name, strlen(name), p, end) || end - p < 3) return 0;
    *p++ = '"';
    *p++ = '}';
    *p = 0;
    return (size_t)(p - out);
}

bool sameSongSeq(uint32_t seq)
{
    portENTER_CRITICAL(&s_mux);
    const bool same = s_songSeq == seq;
    portEXIT_CRITICAL(&s_mux);
    return same;
}

void sendPlainLyrics(int fd, uint32_t seq)
{
    if (!s_bookMux) return;
    xSemaphoreTake(s_bookMux, portMAX_DELAY);
    const bool current = sameSongSeq(seq);
    const bool plain = !s_plainLyrics.empty();
    const size_t size = s_plainLyrics.size();
    xSemaphoreGive(s_bookMux);
    if (!current) return;
    const char *start = plain ? "{\"type\":\"bookstart\",\"plain\":true}" :
                                "{\"type\":\"bookstart\",\"plain\":false}";
    if (!sendFrame(fd, HTTPD_WS_TYPE_TEXT, start, strlen(start)) || !plain) return;

    size_t pos = 0;
    while (pos < size) {
        char chunk[640];
        size_t n = 0;
        size_t copied = 0;
        xSemaphoreTake(s_bookMux, portMAX_DELAY);
        if (!sameSongSeq(seq)) {
            xSemaphoreGive(s_bookMux);
            return;
        }
        n = size - pos;
        if (n > 512) n = 512;
        while (n && pos + n < size && (((uint8_t)s_plainLyrics[pos + n] & 0xC0) == 0x80)) n--;
        memcpy(chunk, s_plainLyrics.data() + pos, n);
        copied = n;
        xSemaphoreGive(s_bookMux);
        char *json = s_json;   // Sender-task buffer: keep large escaped frames off its stack.
        const char prefix[] = "{\"type\":\"book\",\"text\":\"";
        const char suffix[] = "\"}";
        size_t p = sizeof(prefix) - 1;
        memcpy(json, prefix, p);
        char *out = json + p;
        char *end = json + sizeof(s_json);
        if (!KaraStream::jsonEscape(chunk, copied, out, end) || end - out < (ptrdiff_t)sizeof(suffix)) {
            ESP_LOGE(TAG, "Plain lyrics frame exceeded JSON buffer");
            return;
        }
        memcpy(out, suffix, sizeof(suffix));
        const size_t len = (size_t)(out - json) + sizeof(suffix) - 1;
        if (!sendFrame(fd, HTTPD_WS_TYPE_TEXT, json, len)) return;
        pos += n;
    }
}

void senderTask(void *)
{
    bool enabled = false, muted = false;
    uint32_t seenSeq = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
        httpd_handle_t hd = MyWeb::handle();
        if (!hd) continue;

        // Reap sockets the server closed (tab closed, phone asleep)
        int fds[MAX_CLIENTS];
        int n = snapshot(fds);
        for (int i = 0; i < n; i++)
            if (httpd_ws_get_fd_info(hd, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET) removeFd(fds[i]);

        // New clients: hello + resync of the current lyric line
        int helloFd[MAX_CLIENTS];
        int nh = 0;
        portENTER_CRITICAL(&s_mux);
        for (auto &c : s_cl)
            if (c.fd >= 0 && c.hello) {
                c.hello = false;
                helloFd[nh++] = c.fd;
            }
        const bool open = s_songOpen;
        const uint32_t seq = s_songSeq;
        portEXIT_CRITICAL(&s_mux);

        const bool active = s_count > 0;
        if (active != enabled) {
            s_stream.setEnabled(active);   // On: starts from an empty ring
            enabled = active;
            if (active) s_stream.requestResync();
        }
        if (nh) {
            s_stream.requestResync();
            const size_t len = stateJson("hello", s_json, sizeof(s_json));
            for (int i = 0; i < nh; i++) {
                sendFrame(helloFd[i], HTTPD_WS_TYPE_TEXT, s_json, len);
            }
            for (int i = 0; i < nh; i++) sendPlainLyrics(helloFd[i], seq);
        }
        if (seq != seenSeq) {   // Song opened / closed
            seenSeq = seq;
            if (enabled) {
                s_stream.drain();
                s_stream.requestResync();
                const size_t len = stateJson("state", s_json, sizeof(s_json));
                broadcast(HTTPD_WS_TYPE_TEXT, s_json, len);
                int currentFd[MAX_CLIENTS];
                const int nfd = snapshot(currentFd);
                for (int i = 0; i < nfd; i++) sendPlainLyrics(currentFd[i], seq);
            }
        }
        const bool mute = active && open;
        if (mute != muted) {
            muted = mute;
            Audio::setWebStream(mute);
        }
        if (!enabled) continue;

        // Drain: events at their sample position, PCM in 512-sample frames (bounded per round)
        for (int round = 0; round < 16; round++) {
            bool any = false;
            while (s_stream.popEvent(s_ev)) {
                const size_t len = KaraStream::toJson(s_ev, s_json, sizeof(s_json));
                if (len) broadcast(HTTPD_WS_TYPE_TEXT, s_json, len);
                any = true;
            }
            const uint32_t m = s_stream.readPcm(s_pcm, KaraStream::FRAME_SAMPLES, true);
            if (m) {
                broadcast(HTTPD_WS_TYPE_BINARY, s_pcm, m * sizeof(int16_t));
                any = true;
            }
            if (!any) break;
        }
    }
}

// Registers a socket. false = all slots busy.
bool registerFd(int fd)
{
    bool ok = false, isNew = false;
    portENTER_CRITICAL(&s_mux);
    for (auto &c : s_cl)
        if (c.fd == fd) ok = true;
    if (!ok)
        for (auto &c : s_cl)
            if (c.fd < 0) {
                c.fd = fd;
                c.hello = true;
                s_count = s_count + 1;
                ok = isNew = true;
                break;
            }
    portEXIT_CRITICAL(&s_mux);
    if (isNew) ESP_LOGI(TAG, "client %d connected (%d)", fd, s_count);
    return ok;
}

esp_err_t wsHandler(httpd_req_t *r)
{
    const int fd = httpd_req_to_sockfd(r);
    // ESP-IDF 6.x does the handshake itself (no handler call); the page sends {"cmd":"hello"} on
    // open, so clients register on their first frame. The GET branch is for older IDFs.
    if (r->method == HTTP_GET) return ESP_OK;
    httpd_ws_frame_t f = {};
    esp_err_t e = httpd_ws_recv_frame(r, &f, 0);
    if (e != ESP_OK) return e;
    if (f.len > MAX_RX) return ESP_FAIL;   // Oversized: drop the connection
    uint8_t buf[MAX_RX + 1];
    if (f.len) {
        f.payload = buf;
        e = httpd_ws_recv_frame(r, &f, f.len);
        if (e != ESP_OK) return e;
    }
    if (!registerFd(fd)) {
        static const char full[] = "{\"type\":\"full\"}";
        httpd_ws_frame_t o = {};
        o.type = HTTPD_WS_TYPE_TEXT;
        o.final = true;
        o.payload = (uint8_t *)full;
        o.len = sizeof(full) - 1;
        httpd_ws_send_frame(r, &o);
        httpd_sess_trigger_close(r->handle, fd);
    }
    return ESP_OK;
}

}  // namespace

bool begin()
{
    if (s_running) return true;
    auto *pcm = (int16_t *)heap_caps_malloc(KaraStream::RING_SAMPLES * sizeof(int16_t),
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    auto *ev = (KaraStream::Event *)heap_caps_malloc(KaraStream::EVT_CAP * sizeof(KaraStream::Event),
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pcm || !ev) {
        heap_caps_free(pcm);
        heap_caps_free(ev);
        ESP_LOGE(TAG, "no PSRAM for the stream");
        return false;
    }
    s_stream.init(pcm, KaraStream::RING_SAMPLES, ev, KaraStream::EVT_CAP);
    if (!s_bookMux) s_bookMux = xSemaphoreCreateMutex();
    if (!s_bookMux) {
        heap_caps_free(pcm);
        heap_caps_free(ev);
        return false;
    }

    httpd_uri_t u = {};
    u.uri = "/ws/karaoke";
    u.method = HTTP_GET;
    u.handler = wsHandler;
    u.is_websocket = true;
    if (httpd_register_uri_handler(MyWeb::handle(), &u) != ESP_OK) return false;
    if (xTaskCreatePinnedToCore(senderTask, "kara_web", 4096, nullptr, 4, nullptr, 0) != pdPASS) return false;
    Audio::setTap(&tap, nullptr);
    s_running = true;
    ESP_LOGI(TAG, "/ws/karaoke ready");
    return true;
}

bool running() { return s_running; }

void setSong(bool open, const char *name, const char *plainLyrics)
{
    char tmp[NAME_CAP] = "";
    if (open && name) KaraStream::copyUtf8(tmp, sizeof(tmp), name, strlen(name));
    if (s_bookMux) xSemaphoreTake(s_bookMux, portMAX_DELAY);
    s_plainLyrics = open && plainLyrics ? plainLyrics : "";
    portENTER_CRITICAL(&s_mux);
    s_songOpen = open;
    s_songSeq = s_songSeq + 1;
    memcpy(s_songName, tmp, sizeof(tmp));
    portEXIT_CRITICAL(&s_mux);
    if (s_bookMux) xSemaphoreGive(s_bookMux);
    s_plainLyricsAtomic = open && plainLyrics && *plainLyrics;
    s_songOpenAtomic = open;
    if (open) s_stream.requestResync();
}

void onRender(const Midi::Song &song, uint32_t posUs)
{
    if (!s_running || !s_songOpenAtomic) return;
    if (s_plainLyricsAtomic) {
        s_tracker.reset();
        return;
    }
    s_tracker.update(song, posUs, s_stream);
}

int clients() { return s_count; }

}  // namespace KaraWeb
