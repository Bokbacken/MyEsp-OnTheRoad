#include "board_battery.h"

#include "battery_math.h"

#include "esp_log.h"
#include "sdkconfig.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
#include "board_pins.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#endif

namespace BoardBattery {
namespace {

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
static constexpr const char *TAG = "battery";
#endif
static Status s_status{};

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
static portMUX_TYPE s_statusMux = portMUX_INITIALIZER_UNLOCKED;
static adc_oneshot_unit_handle_t s_adc = nullptr;
static adc_cali_handle_t s_adcCali = nullptr;
static adc_channel_t s_channel;

static void publish(const Status &status)
{
    portENTER_CRITICAL(&s_statusMux);
    s_status = status;
    portEXIT_CRITICAL(&s_statusMux);
}

static void sampleTask(void *)
{
    int32_t filteredRaw = -1;
    for (;;) {
        int32_t total = 0;
        bool ok = true;
        for (int i = 0; i < 8; ++i) {
            int raw = 0;
            const esp_err_t err = adc_oneshot_read(s_adc, s_channel, &raw);
            if (err != ESP_OK || raw < 0 || raw > 4095) {
                ESP_LOGW(TAG, "GPIO%d ADC read failed: %s (raw %d)", PIN_BAT_ADC, esp_err_to_name(err), raw);
                ok = false;
                break;
            }
            total += raw;
            vTaskDelay(pdMS_TO_TICKS(8));
        }

        if (ok) {
            filteredRaw = BatteryMath::filterRaw(filteredRaw, (total + 4) / 8);
            Status next = status();
            next.sampleValid = true;
            next.raw = filteredRaw;
            next.adcCalibrated = false;
            next.adcMillivolts = 0;
            if (s_adcCali) {
                int voltage = 0;
                if (adc_cali_raw_to_voltage(s_adcCali, filteredRaw, &voltage) == ESP_OK &&
                    voltage >= 0 && voltage <= BatteryMath::ADC_MAX_MV) {
                    next.adcCalibrated = true;
                    next.adcMillivolts = static_cast<uint16_t>(voltage);
                } else {
                    ++next.errors;
                }
            }
            publish(next);
        } else {
            Status next = status();
            next.sampleValid = false;
            next.adcCalibrated = false;
            ++next.errors;
            publish(next);
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
#endif

}  // namespace

bool begin()
{
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
    if (s_status.initialized) return true;

    Status initial{};
    initial.supported = true;
    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(PIN_BAT_ADC, &unit, &s_channel);
    if (err != ESP_OK) {
        initial.errors = 1;
        publish(initial);
        ESP_LOGE(TAG, "GPIO%d ADC mapping failed: %s", PIN_BAT_ADC, esp_err_to_name(err));
        return false;
    }
    adc_oneshot_unit_init_cfg_t unitConfig{};
    unitConfig.unit_id = unit;
    err = adc_oneshot_new_unit(&unitConfig, &s_adc);
    if (err != ESP_OK) {
        initial.errors = 1;
        publish(initial);
        ESP_LOGE(TAG, "ADC unit creation failed: %s", esp_err_to_name(err));
        return false;
    }
    adc_oneshot_chan_cfg_t channelConfig{};
    channelConfig.atten = ADC_ATTEN_DB_12;
    channelConfig.bitwidth = ADC_BITWIDTH_12;
    err = adc_oneshot_config_channel(s_adc, s_channel, &channelConfig);
    if (err != ESP_OK) {
        adc_oneshot_del_unit(s_adc);
        s_adc = nullptr;
        initial.errors = 1;
        publish(initial);
        ESP_LOGE(TAG, "ADC channel configuration failed: %s", esp_err_to_name(err));
        return false;
    }

    adc_cali_curve_fitting_config_t caliConfig{};
    caliConfig.unit_id = unit;
    caliConfig.chan = s_channel;
    caliConfig.atten = ADC_ATTEN_DB_12;
    caliConfig.bitwidth = ADC_BITWIDTH_12;
    err = adc_cali_create_scheme_curve_fitting(&caliConfig, &s_adcCali);
    const bool calibrated = err == ESP_OK;
    if (!calibrated) {
        ++initial.errors;
        ESP_LOGW(TAG, "ADC calibration unavailable: %s; only raw samples will be reported", esp_err_to_name(err));
    }

    initial.initialized = true;
    initial.adcCalibrated = calibrated;
    publish(initial);
    if (xTaskCreatePinnedToCore(sampleTask, "battery_adc", 3072, nullptr, 2, nullptr, 0) != pdPASS) {
        if (s_adcCali) {
            adc_cali_delete_scheme_curve_fitting(s_adcCali);
            s_adcCali = nullptr;
        }
        adc_oneshot_del_unit(s_adc);
        s_adc = nullptr;
        initial.initialized = false;
        initial.adcCalibrated = false;
        initial.errors = 1;
        publish(initial);
        ESP_LOGW(TAG, "sampling task unavailable");
        return false;
    }
    ESP_LOGI(TAG, "GPIO%d ADC sampling started; board default divider scale %u permille",
             PIN_BAT_ADC, (unsigned)BAT_DEFAULT_SCALE_PERMILLE);
    return true;
#else
    s_status = {};
    return false;
#endif
}

Status status()
{
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)
    portENTER_CRITICAL(&s_statusMux);
    const Status copy = s_status;
    portEXIT_CRITICAL(&s_statusMux);
    return copy;
#else
    return s_status;
#endif
}

}  // namespace BoardBattery
