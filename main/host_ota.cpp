#include "host_ota.h"

#include <string.h>

#include "esp_hosted_ota.h"
#include "eh_host_sys.h"
#include "eh_host_api_types.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "my_ota.h"
#include "my_web.h"

namespace HostOta {
namespace {
// ESP-Hosted rejects writes above EH_HOST_CP_OTA_CHUNK_MAX (~1480 B over SDIO) without an RPC.
constexpr size_t CHUNK_SIZE = 1400;
#ifdef EH_HOST_CP_OTA_CHUNK_MAX
static_assert(CHUNK_SIZE <= EH_HOST_CP_OTA_CHUNK_MAX, "C6 OTA chunk exceeds ESP-Hosted limit");
#endif
constexpr size_t C6_IMAGE_HEADER_SIZE = 24;
constexpr uint32_t MAX_C6_IMAGE_SIZE = 0x1C0000;
constexpr uint8_t ESP_IMAGE_MAGIC = 0xE9;
constexpr uint16_t ESP32C6_CHIP_ID = 0x000D;
static const char *TAG = "host_ota";
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
bool s_busy = false;

bool claim()
{
    portENTER_CRITICAL(&s_mux);
    const bool acquired = !s_busy;
    if (acquired) s_busy = true;
    portEXIT_CRITICAL(&s_mux);
    return acquired;
}

void release()
{
    portENTER_CRITICAL(&s_mux);
    s_busy = false;
    portEXIT_CRITICAL(&s_mux);
}

struct BusyGuard {
    ~BusyGuard() { release(); }
};

void drain(httpd_req_t *r, size_t left)
{
    char tmp[256];
    while (left) {
        int n = httpd_req_recv(r, tmp, left < sizeof(tmp) ? left : sizeof(tmp));
        if (n <= 0) return;
        left -= (size_t)n;
    }
}

bool recvExact(httpd_req_t *r, uint8_t *dst, size_t len)
{
    size_t have = 0;
    int timeouts = 0;
    while (have < len) {
        int n = httpd_req_recv(r, (char *)dst + have, len - have);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) continue;
        if (n <= 0) return false;
        timeouts = 0;
        have += (size_t)n;
    }
    return true;
}

esp_err_t fail(httpd_req_t *r, int status, const char *message, size_t bodyLeft)
{
    drain(r, bodyLeft);
    return MyWeb::sendError(r, status, message);
}

esp_err_t handleUpload(httpd_req_t *r)
{
    MyWeb::Req q(r);
    if (!q.adminAllowed()) return fail(r, 403, "Fel admin-PIN", r->content_len);
    if (!claim()) return fail(r, 409, "En Host-radio-uppdatering pågår redan", r->content_len);
    BusyGuard guard;

    long announced = 0;
    bool bad = false;
    if (!q.getInt("size", 1, MAX_C6_IMAGE_SIZE, announced, bad) ||
        bad || (size_t)announced != r->content_len)
        return fail(r, 400, "Ogiltig C6-firmwarestorlek", r->content_len);
    if (r->content_len < C6_IMAGE_HEADER_SIZE)
        return fail(r, 400, "Filen är för liten för en ESP32-C6-app", r->content_len);

    uint8_t header[C6_IMAGE_HEADER_SIZE] = {};
    if (!recvExact(r, header, sizeof(header)))
        return MyWeb::sendError(r, 400, "Uppladdningen avbröts");
    const size_t afterHeader = r->content_len - sizeof(header);
    const uint16_t chipId = (uint16_t)header[12] | ((uint16_t)header[13] << 8);
    if (header[0] != ESP_IMAGE_MAGIC || chipId != ESP32C6_CHIP_ID)
        return fail(r, 400, "Filen är inte ESP32-C6 firmware", afterHeader);

    eh_host_coprocessor_fwver_t currentVersion = {};
    const esp_err_t versionErr = eh_host_sys_get_cp_fw_version(&currentVersion);
    bool canActivate = false;
    if (versionErr != ESP_OK) {
        ESP_LOGW(TAG, "Cannot read C6 firmware version (%s); proceeding without explicit activation",
                 esp_err_to_name(versionErr));
    } else if (currentVersion.major1 > 2 ||
               (currentVersion.major1 == 2 && currentVersion.minor1 >= 6)) {
        canActivate = true;
    } else {
        ESP_LOGW(TAG, "C6 firmware %lu.%lu.%lu predates explicit OTA activation",
                 (unsigned long)currentVersion.major1, (unsigned long)currentVersion.minor1,
                 (unsigned long)currentVersion.patch1);
    }

    if (!MyOta::beginExternal(r->content_len))
        return fail(r, 409, "En firmwareuppdatering pågår redan", afterHeader);

    esp_err_t err = esp_hosted_slave_ota_begin();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "C6 OTA begin failed: %s", esp_err_to_name(err));
        MyOta::finishExternal(false, "C6 kunde inte starta OTA");
        return fail(r, 500, "C6 kunde inte starta OTA. Kontrollera Host-firmwareversionen.", afterHeader);
    }
    ESP_LOGI(TAG, "C6 OTA started, %u bytes", (unsigned)r->content_len);

    static uint8_t chunk[CHUNK_SIZE];
    memcpy(chunk, header, sizeof(header));
    size_t fill = sizeof(header);
    size_t remaining = afterHeader;
    size_t sent = 0;
    int timeouts = 0;
    bool recvFailed = false;
    while (fill || remaining) {
        if (remaining && fill < CHUNK_SIZE) {
            const size_t room = CHUNK_SIZE - fill;
            const size_t want = remaining < room ? remaining : room;
            int n = httpd_req_recv(r, (char *)chunk + fill, want);
            if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) continue;
            if (n <= 0) {
                recvFailed = true;
                break;
            }
            timeouts = 0;
            fill += (size_t)n;
            remaining -= (size_t)n;
            if (remaining && fill < CHUNK_SIZE) continue;
        }
        err = esp_hosted_slave_ota_write(chunk, (uint32_t)fill);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "C6 OTA write failed at %u/%u (%u B): %s", (unsigned)sent,
                     (unsigned)r->content_len, (unsigned)fill, esp_err_to_name(err));
            break;
        }
        sent += fill;
        fill = 0;
        MyOta::progressExternal((uint32_t)sent);
    }
    if (recvFailed || err != ESP_OK) {
        if (recvFailed) ESP_LOGE(TAG, "C6 OTA upload aborted at %u/%u", (unsigned)sent, (unsigned)r->content_len);
        esp_hosted_slave_ota_end();
        MyOta::finishExternal(false, recvFailed ? "Uppladdningen avbröts" : "Skrivning till C6 misslyckades");
        if (recvFailed) return MyWeb::sendError(r, 400, "Uppladdningen avbröts; gammal C6-firmware behålls");
        return fail(r, 500, "Skrivning till C6 misslyckades; gammal firmware behålls", remaining);
    }

    err = esp_hosted_slave_ota_end();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "C6 OTA validation failed: %s", esp_err_to_name(err));
        MyOta::finishExternal(false, "C6 avvisade firmwarefilen");
        return MyWeb::sendError(r, 400, "C6 avvisade firmwarefilen");
    }
    if (canActivate) {
        err = esp_hosted_slave_ota_activate();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "C6 OTA activation failed: %s", esp_err_to_name(err));
            MyOta::finishExternal(false, "C6 kunde inte aktivera firmware");
            return MyWeb::sendError(r, 500, "C6 kunde inte aktivera den nya firmwareversionen");
        }
    }
    ESP_LOGI(TAG, "C6 OTA complete (%u bytes, activate=%d); restarting", (unsigned)sent, canActivate);
    MyOta::finishExternal(true);

    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddNumberToObject(o, "bytes", r->content_len);
    cJSON_AddBoolToObject(o, "restarting", true);
    MyWeb::sendJson(r, 200, o);
    MyWeb::rebootSoon(3000);
    return ESP_OK;
}
}  // namespace

bool registerRoute()
{
    return MyWeb::on("/api/host-ota", HTTP_POST, handleUpload);
}
}  // namespace HostOta
