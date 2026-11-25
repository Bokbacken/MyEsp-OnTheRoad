#include "board_usb_audio.h"

#include <atomic>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "esp_log.h"
#include "sdkconfig.h"

namespace UsbAudio {

const char *stateName(State s)
{
    switch (s) {
    case State::OFF: return "off";
    case State::WAITING: return "waiting";
    case State::OPEN: return "open";
    case State::STREAMING: return "streaming";
    default: return "error";
    }
}

}  // namespace UsbAudio

#if CONFIG_IDF_TARGET_ESP32P4

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "usb/uac_host.h"
#include "usb/usb_host.h"

namespace UsbAudio {
namespace {

const char *TAG = "usbaudio";
constexpr uint32_t RING_BYTES = 32 * 1024;
constexpr uint32_t THRESHOLD_BYTES = 4 * 1024;

struct Found { uint8_t addr, iface; };

// One UAC speaker (a hub or dock can expose several sound cards). The handle stays open so a
// removal is reported; streaming runs on at most one entry.
struct Entry {
    uac_host_device_handle_t h;
    uint8_t iface;
    uint16_t vid, pid;
    char name[40];
    char key[24];
    bool streaming;
    uac_host_stream_config_t cfg;
};

Entry s_list[MAX_DEVICES];
int s_count = 0;
Entry *s_cur = nullptr;   // Streaming entry
char s_pref[24] = "";     // Saved choice: "" = first found, "-" = internal speaker, else Entry::key
SemaphoreHandle_t s_lock = nullptr;
QueueHandle_t s_found = nullptr;
usb_host_client_handle_t s_client = nullptr;
Status s_st{};
std::atomic<bool> s_streaming{false};
uint8_t s_volume = 70;
uint32_t s_rate = 0;
uint32_t s_phase = 0;      // Fixed-point (16.16) read position inside the input block
int16_t s_prev = 0;        // Last sample of the previous block for interpolation
int16_t s_out[2 * 1024];

void setError(const char *msg)
{
    strlcpy(s_st.error, msg, sizeof(s_st.error));
    s_st.state = State::ERROR;
    ESP_LOGE(TAG, "%s", msg);
}

void toAscii(const wchar_t *w, char *out, size_t cap)
{
    size_t i = 0;
    for (; i + 1 < cap && w[i]; i++) out[i] = w[i] < 128 && w[i] >= 32 ? (char)w[i] : '?';
    out[i] = 0;
}

void savePref()
{
    nvs_handle_t n;
    if (nvs_open("usbaudio", NVS_READWRITE, &n) != ESP_OK) return;
    nvs_set_str(n, "pref", s_pref);
    nvs_commit(n);
    nvs_close(n);
}

void loadPref()
{
    nvs_handle_t n;
    if (nvs_open("usbaudio", NVS_READONLY, &n) != ESP_OK) return;
    size_t len = sizeof(s_pref);
    if (nvs_get_str(n, "pref", s_pref, &len) != ESP_OK) s_pref[0] = 0;
    nvs_close(n);
}

// Picks stereo 16-bit 44.1/48 kHz when offered; otherwise the first stereo 16-bit rate.
bool chooseFormat(uac_host_device_handle_t h, uint8_t altCount, uac_host_stream_config_t &cfg)
{
    uint32_t best = 0;
    for (uint8_t alt = 1; alt <= altCount; alt++) {
        uac_host_dev_alt_param_t p{};
        if (uac_host_get_device_alt_param(h, alt, &p) != ESP_OK) continue;
        ESP_LOGI(TAG, "alt %u: %uch %ubit type %u", alt, p.channels, p.bit_resolution, p.sample_freq_type);
        if (p.channels != 2 || p.bit_resolution != 16) continue;
        auto consider = [&](uint32_t f) {
            ESP_LOGI(TAG, "  rate %u", (unsigned)f);
            if (f == 44100 || (f == 48000 && best != 44100) || !best) best = f;
        };
        if (p.sample_freq_type) {
            for (int i = 0; i < p.sample_freq_type && i < UAC_FREQ_NUM_MAX; i++) consider(p.sample_freq[i]);
        } else if (44100 >= p.sample_freq_lower && 44100 <= p.sample_freq_upper) {
            consider(44100);
        }
    }
    if (!best) return false;
    cfg.channels = 2;
    cfg.bit_resolution = 16;
    cfg.sample_freq = best;
    cfg.flags = 0;
    return true;
}

void stopEntry(Entry &e)
{
    s_streaming = false;
    if (s_cur == &e) s_cur = nullptr;
    s_rate = 0;
    s_st.rate = 0;
    s_st.product[0] = 0;
    if (e.streaming) uac_host_device_stop(e.h);
    e.streaming = false;
}

bool startEntry(Entry &e)
{
    esp_err_t err = uac_host_device_start(e.h, &e.cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uac_host_device_start: %s", esp_err_to_name(err));
        setError("UAC start failed");
        return false;
    }
    uac_host_device_set_mute(e.h, false);
    uac_host_device_set_volume(e.h, s_volume);
    e.streaming = true;
    s_cur = &e;
    s_rate = e.cfg.sample_freq;
    s_phase = 0;
    s_st.rate = s_rate;
    s_st.error[0] = 0;
    strlcpy(s_st.product, e.name, sizeof(s_st.product));
    s_st.state = State::STREAMING;
    s_streaming = true;
    ESP_LOGI(TAG, "streaming to '%s' at %u Hz", e.name, (unsigned)s_rate);
    return true;
}

// Lock held. Makes the streaming entry match the saved choice.
void applySelection()
{
    Entry *want = nullptr;
    if (!strcmp(s_pref, "-")) {
        want = nullptr;
    } else if (s_pref[0]) {
        for (int i = 0; i < s_count; i++)
            if (!strcmp(s_list[i].key, s_pref)) want = &s_list[i];
    } else {
        want = s_cur ? s_cur : (s_count ? &s_list[0] : nullptr);
    }
    if (s_cur && s_cur != want) stopEntry(*s_cur);
    if (want && !want->streaming) startEntry(*want);
    if (!s_cur && s_st.state != State::ERROR) s_st.state = s_count ? State::OPEN : State::WAITING;
}

// Lock held.
void removeEntry(uac_host_device_handle_t h)
{
    for (int i = 0; i < s_count; i++) {
        if (s_list[i].h != h) continue;
        ESP_LOGW(TAG, "'%s' removed", s_list[i].name);
        Entry *cur = s_cur;
        if (cur == &s_list[i]) stopEntry(s_list[i]);
        const int curIdx = (s_cur && s_cur != &s_list[i]) ? (int)(s_cur - s_list) : -1;
        for (int j = i; j + 1 < s_count; j++) s_list[j] = s_list[j + 1];
        s_count--;
        if (curIdx > i) s_cur = &s_list[curIdx - 1];
        break;
    }
    if (!s_count) s_st.state = State::WAITING;
}

void deviceEvent(uac_host_device_handle_t h, const uac_host_device_event_t ev, void *)
{
    if (ev == UAC_HOST_DRIVER_EVENT_DISCONNECTED) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        removeEntry(h);
        applySelection();
        xSemaphoreGive(s_lock);
        uac_host_device_close(h);
    } else if (ev == UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR) {
        ESP_LOGW(TAG, "USB transfer error");
    }
}

void driverEvent(uint8_t addr, uint8_t iface, const uac_host_driver_event_t ev, void *)
{
    if (ev != UAC_HOST_DRIVER_EVENT_TX_CONNECTED) return;   // Speaker side only
    Found f{addr, iface};
    xQueueSend(s_found, &f, 0);
}

void openDevice(const Found &f)
{
    if (s_count >= MAX_DEVICES) {
        ESP_LOGW(TAG, "too many USB speakers; ignoring one");
        return;
    }
    uac_host_device_config_t dc{};
    dc.addr = f.addr;
    dc.iface_num = f.iface;
    dc.buffer_size = RING_BYTES;
    dc.buffer_threshold = THRESHOLD_BYTES;
    dc.callback = deviceEvent;
    uac_host_device_handle_t h = nullptr;
    esp_err_t err = uac_host_device_open(&dc, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uac_host_device_open: %s", esp_err_to_name(err));
        setError("UAC open failed");
        return;
    }
    uac_host_dev_info_t info{};
    uac_host_get_device_info(h, &info);
    Entry e{};
    e.h = h;
    e.iface = info.iface_num;
    e.vid = info.VID;
    e.pid = info.PID;
    toAscii(info.iProduct, e.name, sizeof(e.name));
    snprintf(e.key, sizeof(e.key), "%04x:%04x:%u", e.vid, e.pid, e.iface);
    ESP_LOGI(TAG, "speaker %s '%s', %u alt settings", e.key, e.name, info.iface_alt_num);
    if (!chooseFormat(h, info.iface_alt_num, e.cfg)) {
        ESP_LOGW(TAG, "'%s': no stereo 16-bit format", e.name);
        uac_host_device_close(h);
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_list[s_count++] = e;
    if (s_st.state == State::ERROR) s_st.state = State::WAITING;
    applySelection();
    xSemaphoreGive(s_lock);
}

void clientEvent(const usb_host_client_event_msg_t *msg, void *)
{
    if (msg->event != USB_HOST_CLIENT_EVENT_NEW_DEV) return;
    usb_device_handle_t dev = nullptr;
    if (usb_host_device_open(s_client, msg->new_dev.address, &dev) != ESP_OK) return;
    const usb_device_desc_t *d = nullptr;
    if (usb_host_get_device_descriptor(dev, &d) == ESP_OK && d) {
        s_st.deviceSeen = true;
        s_st.vid = d->idVendor;
        s_st.pid = d->idProduct;
        ESP_LOGI(TAG, "USB device %04x:%04x class %u", d->idVendor, d->idProduct, d->bDeviceClass);
    }
    usb_host_device_close(s_client, dev);
}

void libTask(void *)
{
    for (;;) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
    }
}

void clientTask(void *)
{
    for (;;) usb_host_client_handle_events(s_client, portMAX_DELAY);
}

void openTask(void *)
{
    for (;;) {
        Found f;
        if (xQueueReceive(s_found, &f, portMAX_DELAY) == pdTRUE) openDevice(f);
    }
}

}  // namespace

void begin()
{
    if (s_lock) return;
    s_st.supported = true;
    s_lock = xSemaphoreCreateMutex();
    s_found = xQueueCreate(4, sizeof(Found));
    if (!s_lock || !s_found) { setError("out of memory"); return; }
    loadPref();

    usb_host_config_t hc{};
    hc.skip_phy_setup = false;
    hc.intr_flags = ESP_INTR_FLAG_LEVEL1;
    esp_err_t err = usb_host_install(&hc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install: %s", esp_err_to_name(err));
        setError("USB host install failed");
        return;
    }
    xTaskCreatePinnedToCore(libTask, "usb_lib", 4096, nullptr, 5, nullptr, 0);

    usb_host_client_config_t cc{};
    cc.is_synchronous = false;
    cc.max_num_event_msg = 5;
    cc.async.client_event_callback = clientEvent;
    if (usb_host_client_register(&cc, &s_client) == ESP_OK)
        xTaskCreatePinnedToCore(clientTask, "usb_cli", 4096, nullptr, 4, nullptr, 0);

    uac_host_driver_config_t uc{};
    uc.create_background_task = true;
    uc.task_priority = 5;
    uc.stack_size = 4096;
    uc.core_id = 0;
    uc.callback = driverEvent;
    err = uac_host_install(&uc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uac_host_install: %s", esp_err_to_name(err));
        setError("UAC install failed");
        return;
    }
    xTaskCreatePinnedToCore(openTask, "usb_open", 6144, nullptr, 4, nullptr, 0);
    s_st.state = State::WAITING;
    ESP_LOGI(TAG, "USB host ready, saved choice '%s'", s_pref);
}

bool active() { return s_streaming.load(); }

void setVolume(uint8_t percent)
{
    s_volume = percent > 100 ? 100 : percent;
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_cur) uac_host_device_set_volume(s_cur->h, s_volume);
    xSemaphoreGive(s_lock);
}

bool select(int index)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool ok = true;
    if (index < 0) {
        strlcpy(s_pref, "-", sizeof(s_pref));
    } else if (index < s_count) {
        strlcpy(s_pref, s_list[index].key, sizeof(s_pref));
    } else {
        ok = false;
    }
    if (ok) {
        savePref();
        applySelection();
    }
    xSemaphoreGive(s_lock);
    return ok;
}

void push(const int16_t *mono, int n, int inRate)
{
    if (!s_streaming.load() || n <= 0 || inRate <= 0) return;
    if (xSemaphoreTake(s_lock, 0) != pdTRUE) return;
    if (s_cur && s_rate) {
        // Linear interpolation from inRate to s_rate; the (16.16) phase carries across blocks.
        const uint32_t step = (uint32_t)(((uint64_t)inRate << 16) / s_rate);
        int outN = 0;
        uint32_t pos = s_phase;   // Position relative to the previous-block sample at index -1
        const uint32_t limit = (uint32_t)n << 16;
        while (pos < limit && outN < 1024) {
            const int idx = (int)(pos >> 16) - 1;
            const int16_t a = idx < 0 ? s_prev : mono[idx];
            const int16_t b = idx + 1 < n ? mono[idx + 1] : mono[n - 1];
            const int32_t frac = (int32_t)(pos & 0xFFFF);
            const int16_t v = (int16_t)(a + (((int32_t)(b - a) * frac) >> 16));
            s_out[2 * outN] = s_out[2 * outN + 1] = v;
            outN++;
            pos += step;
        }
        s_phase = pos - limit;
        s_prev = mono[n - 1];
        const uint32_t bytes = (uint32_t)outN * 4;
        if (uac_host_device_write(s_cur->h, (uint8_t *)s_out, bytes, 0) == ESP_OK) s_st.written += bytes;
        else s_st.dropped += bytes;
    }
    xSemaphoreGive(s_lock);
}

Status status()
{
    Status s = s_st;
    if (!s_lock) return s;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s.deviceCount = s_count;
    for (int i = 0; i < s_count; i++) {
        strlcpy(s.devices[i].name, s_list[i].name, sizeof(s.devices[i].name));
        s.devices[i].vid = s_list[i].vid;
        s.devices[i].pid = s_list[i].pid;
        s.devices[i].selected = s_list[i].streaming;
    }
    s.internalChosen = !strcmp(s_pref, "-");
    xSemaphoreGive(s_lock);
    return s;
}

}  // namespace UsbAudio

#else  // Targets without a USB host

namespace UsbAudio {
void begin() {}
bool active() { return false; }
void push(const int16_t *, int, int) {}
void setVolume(uint8_t) {}
bool select(int) { return false; }
Status status() { Status s{}; s.state = State::OFF; return s; }
}  // namespace UsbAudio

#endif
