#include "otr_i2c.h"

#if defined(CONFIG_OTR_HW_PROFILE_S3_35B)
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace OtrI2c {
namespace {

constexpr const char *TAG = "i2c_pacing";
constexpr int64_t BUS_GAP_US = 100;
SemaphoreHandle_t s_mutex = nullptr;
int64_t s_lastEndUs = 0;
bool s_completed = false;

class Transaction {
public:
    Transaction()
    {
        if (!s_mutex || xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "shared I2C pacing lock unavailable");
            return;
        }
        m_locked = true;
        // Keep the lock across the idle interval so another device cannot fill it.
        if (s_completed) {
            int64_t remaining;
            while ((remaining = BUS_GAP_US - (esp_timer_get_time() - s_lastEndUs)) > 0)
                esp_rom_delay_us(static_cast<uint32_t>(remaining));
        }
    }

    ~Transaction()
    {
        if (!m_locked) return;
        s_lastEndUs = esp_timer_get_time();
        s_completed = true;
        xSemaphoreGive(s_mutex);
    }

    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    bool locked() const { return m_locked; }

private:
    bool m_locked = false;
};

}  // namespace

bool begin()
{
    if (s_mutex) return true;
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "shared I2C pacing mutex allocation failed");
        return false;
    }
    ESP_LOGI(TAG, "shared I2C mutex enabled; minimum transaction gap %u us", unsigned(BUS_GAP_US));
    return true;
}

esp_err_t probe(i2c_master_bus_handle_t bus, uint16_t address, int timeoutMs)
{
    Transaction transaction;
    if (!transaction.locked()) return ESP_ERR_INVALID_STATE;
    return i2c_master_probe(bus, address, timeoutMs);
}

esp_err_t transmit(i2c_master_dev_handle_t device, const uint8_t *data, size_t size, int timeoutMs)
{
    Transaction transaction;
    if (!transaction.locked()) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit(device, data, size, timeoutMs);
}

esp_err_t transmitReceive(i2c_master_dev_handle_t device, const uint8_t *tx, size_t txSize,
                          uint8_t *rx, size_t rxSize, int timeoutMs)
{
    Transaction transaction;
    if (!transaction.locked()) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(device, tx, txSize, rx, rxSize, timeoutMs);
}

}  // namespace OtrI2c
#endif
