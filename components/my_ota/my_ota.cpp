#include "my_ota.h"

#include <string.h>

#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "my_log.h"
#include "my_sys.h"

namespace MyOta {

static const char *TAG = "ota";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static State s_state = {Phase::IDLE, 0, 0, ""};
static volatile uint32_t s_lastWriteMs = 0;
static esp_ota_handle_t s_handle = 0;
static const esp_partition_t *s_target = nullptr;
static constexpr uint32_t STALE_MS = 20000;

static void setPhase(Phase p, const char *err = nullptr)
{
    portENTER_CRITICAL(&s_mux);
    s_state.phase = p;
    if (err) {
        strncpy(s_state.error, err, sizeof(s_state.error) - 1);
        s_state.error[sizeof(s_state.error) - 1] = 0;
    }
    portEXIT_CRITICAL(&s_mux);
}

bool begin(uint32_t total)
{
    checkStale();
    if (busy()) return false;
    s_lastWriteMs = MySys::millis();
    portENTER_CRITICAL(&s_mux);
    s_state = {Phase::RUNNING, 0, total, ""};
    portEXIT_CRITICAL(&s_mux);
    s_target = esp_ota_get_next_update_partition(nullptr);
    if (!s_target) {
        setPhase(Phase::FAILED, "Ingen OTA-partition");
        return false;
    }
    if (total && total > s_target->size) {
        setPhase(Phase::FAILED, "Filen är större än app-partitionen");
        return false;
    }
    // Erase as this sequential stream advances, not the whole image before the UI can refresh.
    esp_err_t e = esp_ota_begin(s_target, OTA_WITH_SEQUENTIAL_WRITES, &s_handle);
    if (e != ESP_OK) {
        setPhase(Phase::FAILED, esp_err_to_name(e));
        ESP_LOGE(TAG, "begin failed: %s", esp_err_to_name(e));
        return false;
    }
    MyLog::event(TAG, "update started -> %s (%lu bytes expected)", s_target->label, (unsigned long)total);
    return true;
}

bool write(const uint8_t *data, size_t len)
{
    if (s_state.phase != Phase::RUNNING) return false;
    // An app image starts with 0xE9. Catches the merged image (bootloader + partitions + app).
    if (s_state.written == 0 && len > 0 && data[0] != ESP_IMAGE_HEADER_MAGIC) {
        abort("Inte en app-fil (.bin). Använd OTA-filen, inte merged.bin");
        return false;
    }
    esp_err_t e = esp_ota_write(s_handle, data, len);
    if (e != ESP_OK) {
        abort(esp_err_to_name(e));
        return false;
    }
    s_lastWriteMs = MySys::millis();
    portENTER_CRITICAL(&s_mux);
    s_state.written += len;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

bool end()
{
    if (s_state.phase != Phase::RUNNING) return false;
    esp_err_t e = esp_ota_end(s_handle);
    s_handle = 0;
    if (e == ESP_OK) e = esp_ota_set_boot_partition(s_target);
    if (e != ESP_OK) {
        setPhase(Phase::FAILED, e == ESP_ERR_OTA_VALIDATE_FAILED ? "Ogiltig app-fil" : esp_err_to_name(e));
        MyLog::event(TAG, "update failed: %s", esp_err_to_name(e));
        return false;
    }
    setPhase(Phase::DONE);
    MyLog::event(TAG, "update ok, %lu bytes, next boot from %s", (unsigned long)s_state.written, s_target->label);
    return true;
}

void abort(const char *why)
{
    if (s_state.phase == Phase::RUNNING && s_handle) {
        esp_ota_abort(s_handle);
        s_handle = 0;
    }
    setPhase(Phase::FAILED, why ? why : "Avbruten");
    MyLog::event(TAG, "update aborted: %s", why ? why : "-");
}

State state()
{
    portENTER_CRITICAL(&s_mux);
    State s = s_state;
    portEXIT_CRITICAL(&s_mux);
    return s;
}

bool busy() { return s_state.phase == Phase::RUNNING; }

void checkStale()
{
    if (busy() && MySys::millis() - s_lastWriteMs > STALE_MS) abort("Uppladdningen avbröts (ingen data)");
}

const char *runningPartition()
{
    const esp_partition_t *p = esp_ota_get_running_partition();
    return p ? p->label : "?";
}

const char *nextPartition()
{
    const esp_partition_t *p = esp_ota_get_next_update_partition(nullptr);
    return p ? p->label : "?";
}

uint32_t slotSize()
{
    const esp_partition_t *p = esp_ota_get_next_update_partition(nullptr);
    return p ? p->size : 0;
}

bool pendingVerify()
{
    esp_ota_img_states_t st;
    const esp_partition_t *p = esp_ota_get_running_partition();
    return p && esp_ota_get_state_partition(p, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
}

void markValid()
{
    if (!pendingVerify()) return;
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
        MyLog::event(TAG, "new firmware marked valid (%s)", runningPartition());
}

bool rolledBack()
{
    const esp_partition_t *p = esp_ota_get_last_invalid_partition();
    return p != nullptr;
}

}  // namespace MyOta
