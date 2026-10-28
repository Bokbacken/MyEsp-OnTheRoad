#include "board_battery.h"

#include "battery_math.h"
#include "hw_profile.h"

#include "esp_log.h"
#include "sdkconfig.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3)
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

#if defined(CONFIG_IDF_TARGET_ESP32S3)
static constexpr const char *TAG = "battery";
#endif
static Status s_status{};

#if defined(CONFIG_IDF_TARGET_ESP32S3)
static portMUX_TYPE s_statusMux = portMUX_INITIALIZER_UNLOCKED;
static adc_oneshot_unit_handle_t s_adc = nullptr;
static adc_cali_handle_t s_adcCali = nullptr;

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
            if (adc_oneshot_read(s_adc, ADC_CHANNEL_7, &raw) != ESP_OK || raw < 0 || raw > 4095) {
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
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (!BatteryMath::supportsS3BatteryAdc(true, HwProfile::activeId() == HwProfile::Id::ES3C35P)) {
        s_status = {};
        return false;
    }
    if (s_status.initialized) return true;

    adc_oneshot_unit_init_cfg_t unitConfig{};
    unitConfig.unit_id = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&unitConfig, &s_adc) != ESP_OK) {
        s_status = {};
        s_status.supported = true;
        s_status.errors = 1;
        return false;
    }
    adc_oneshot_chan_cfg_t channelConfig{};
    channelConfig.atten = ADC_ATTEN_DB_12;
    channelConfig.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_oneshot_config_channel(s_adc, ADC_CHANNEL_7, &channelConfig) != ESP_OK) {
        adc_oneshot_del_unit(s_adc);
        s_adc = nullptr;
        s_status = {};
        s_status.supported = true;
        s_status.errors = 1;
        return false;
    }

    adc_cali_curve_fitting_config_t caliConfig{};
    caliConfig.unit_id = ADC_UNIT_1;
    caliConfig.chan = ADC_CHANNEL_7;
    caliConfig.atten = ADC_ATTEN_DB_12;
    caliConfig.bitwidth = ADC_BITWIDTH_DEFAULT;
    const bool calibrated = adc_cali_create_scheme_curve_fitting(&caliConfig, &s_adcCali) == ESP_OK;

    Status initial{};
    initial.supported = true;
    initial.initialized = true;
    initial.adcCalibrated = calibrated;
    publish(initial);
    if (xTaskCreatePinnedToCore(sampleTask, "battery_adc", 3072, nullptr, 2, nullptr, 0) != pdPASS) {
        initial.initialized = false;
        initial.adcCalibrated = false;
        initial.errors = 1;
        publish(initial);
        ESP_LOGW(TAG, "sampling task unavailable");
        return false;
    }
    ESP_LOGI(TAG, "GPIO%d ADC sampling started; divider scale requires user calibration", PIN_BAT_ADC);
    return true;
#else
    s_status = {};
    return false;
#endif
}

Status status()
{
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    portENTER_CRITICAL(&s_statusMux);
    const Status copy = s_status;
    portEXIT_CRITICAL(&s_statusMux);
    return copy;
#else
    return s_status;
#endif
}

}  // namespace BoardBattery
