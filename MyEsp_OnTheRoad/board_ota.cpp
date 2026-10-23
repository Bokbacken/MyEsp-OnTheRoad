#include "board_ota.h"

#include <Arduino.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <string.h>

namespace Ota {

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static State s_state = {Phase::IDLE, 0, 0, ""};
static volatile uint32_t s_lastWriteMs = 0;
static constexpr uint32_t STALE_MS = 20000;   // No data for 20 s = browser gone

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
    s_lastWriteMs = millis();
    portENTER_CRITICAL(&s_mux);
    s_state = {Phase::RUNNING, 0, total, ""};
    portEXIT_CRITICAL(&s_mux);
    if (total && total > slotSize()) {
        setPhase(Phase::FAILED, "Filen är större än app-partitionen");
        return false;
    }
    if (!Update.begin(total ? total : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        setPhase(Phase::FAILED, Update.errorString());
        Serial.printf("[ota] begin failed: %s\n", Update.errorString());
        return false;
    }
    Serial.printf("[ota] writing to %s (%lu bytes expected)\n", nextPartition(), (unsigned long)total);
    return true;
}

bool write(const uint8_t *data, size_t len)
{
    if (s_state.phase != Phase::RUNNING) return false;
    // First chunk: an ESP32 app image starts with the magic byte 0xE9. Catches the common
    // mistake of uploading the merged (bootloader + partitions + app) image.
    if (s_state.written == 0 && len > 0 && data[0] != 0xE9) {
        abort("Inte en app-fil (.bin). Använd OTA-filen, inte merged.bin");
        return false;
    }
    if (Update.write(const_cast<uint8_t *>(data), len) != len) {
        abort(Update.errorString());
        return false;
    }
    s_lastWriteMs = millis();
    portENTER_CRITICAL(&s_mux);
    s_state.written += len;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

bool end()
{
    if (s_state.phase != Phase::RUNNING) return false;
    if (!Update.end(true)) {
        setPhase(Phase::FAILED, Update.errorString());
        Serial.printf("[ota] end failed: %s\n", Update.errorString());
        return false;
    }
    setPhase(Phase::DONE);
    Serial.printf("[ota] ok, %lu bytes, next boot from %s\n", (unsigned long)s_state.written,
                  nextPartition());
    return true;
}

void abort(const char *why)
{
    if (s_state.phase == Phase::RUNNING) Update.abort();
    setPhase(Phase::FAILED, why ? why : "Avbruten");
    Serial.printf("[ota] aborted: %s\n", why ? why : "-");
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
    if (busy() && millis() - s_lastWriteMs > STALE_MS) abort("Uppladdningen avbröts (ingen data)");
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

}  // namespace Ota
