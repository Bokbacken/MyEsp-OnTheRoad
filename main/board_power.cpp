#include "board_power.h"

#include "board_power_math.h"

#include <string.h>

#include "esp_log.h"
#include "sdkconfig.h"
#include "otr_i2c.h"

#if defined(CONFIG_OTR_HW_PROFILE_S3_35B)
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"

namespace BoardPower {
namespace {

constexpr const char *TAG = "board_power";
constexpr gpio_num_t PIN_I2C_SDA = GPIO_NUM_8;
constexpr gpio_num_t PIN_I2C_SCL = GPIO_NUM_7;
constexpr uint32_t I2C_HZ = 400000;
constexpr uint8_t TCA9554_ADDR = 0x20;
constexpr uint8_t TCA9554_REG_INPUT = 0x00;
constexpr uint8_t TCA9554_REG_OUTPUT = 0x01;
constexpr uint8_t TCA9554_REG_CONFIG = 0x03;
constexpr uint8_t EXIO_DISPLAY_RESET = 1U << 1;
constexpr uint8_t EXIO_POWER_BUTTON = 1U << 6;
constexpr uint8_t AXP2101_ADDR = 0x34;
constexpr uint8_t AXP2101_REG_STATUS1 = 0x00;
constexpr uint8_t AXP2101_REG_STATUS2 = 0x01;
constexpr uint8_t AXP2101_REG_CHIP_ID = 0x03;
constexpr uint8_t AXP2101_REG_COMMON_CONFIG = 0x10;
constexpr uint8_t AXP2101_REG_ADC_CHANNEL_CTRL = 0x30;
constexpr uint8_t AXP2101_REG_BAT_ADC_HIGH = 0x34;
constexpr uint8_t AXP2101_REG_BAT_ADC_LOW = 0x35;
constexpr uint8_t AXP2101_REG_BAT_PERCENT = 0xA4;
constexpr uint8_t AXP2101_CHIP_ID = 0x4A;
constexpr int I2C_TIMEOUT_MS = 30;
constexpr uint32_t BUTTON_POLL_MS = 109;
constexpr uint32_t BUTTON_POWER_OFF_MS = 2000;

i2c_master_bus_handle_t s_bus = nullptr;
i2c_master_dev_handle_t s_expander = nullptr;
i2c_master_dev_handle_t s_pmic = nullptr;
Status s_status = {};
portMUX_TYPE s_statusMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t s_buttonHeldMs = 0;
uint32_t s_buttonReadErrors = 0;
bool s_buttonShutdownIssued = false;

bool addDevice(uint8_t address, i2c_master_dev_handle_t *device)
{
    if (*device) return true;
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = I2C_HZ;
    return i2c_master_bus_add_device(s_bus, &config, device) == ESP_OK;
}

bool readRegister(i2c_master_dev_handle_t device, uint8_t reg, uint8_t &value)
{
    return device && OtrI2c::transmitReceive(device, &reg, 1, &value, 1, I2C_TIMEOUT_MS) == ESP_OK;
}

bool writeRegister(i2c_master_dev_handle_t device, uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    return device && OtrI2c::transmit(device, data, sizeof(data), I2C_TIMEOUT_MS) == ESP_OK;
}

void publish(const Status &next)
{
    portENTER_CRITICAL(&s_statusMux);
    s_status = next;
    portEXIT_CRITICAL(&s_statusMux);
}

void publishPmic(const Status &next)
{
    portENTER_CRITICAL(&s_statusMux);
    Status current = next;
    current.powerButtonPressed = s_status.powerButtonPressed;
    current.powerOffRequested = s_status.powerOffRequested;
    s_status = current;
    portEXIT_CRITICAL(&s_statusMux);
}

bool initializeExpander()
{
    if (OtrI2c::probe(s_bus, TCA9554_ADDR, I2C_TIMEOUT_MS) != ESP_OK ||
        !addDevice(TCA9554_ADDR, &s_expander))
        return false;

    uint8_t output = 0;
    uint8_t direction = 0;
    if (!readRegister(s_expander, TCA9554_REG_OUTPUT, output) ||
        !readRegister(s_expander, TCA9554_REG_CONFIG, direction))
        return false;

    output &= static_cast<uint8_t>(~EXIO_DISPLAY_RESET);
    if (!writeRegister(s_expander, TCA9554_REG_OUTPUT, output)) return false;
    direction = static_cast<uint8_t>((direction | EXIO_POWER_BUTTON) & ~EXIO_DISPLAY_RESET);
    if (!writeRegister(s_expander, TCA9554_REG_CONFIG, direction)) return false;

    vTaskDelay(pdMS_TO_TICKS(100));
    output |= EXIO_DISPLAY_RESET;
    if (!writeRegister(s_expander, TCA9554_REG_OUTPUT, output)) return false;
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "TCA9554 EXIO1 panel reset complete; EXIO6 remains input");
    return true;
}

bool initializePmic()
{
    if (OtrI2c::probe(s_bus, AXP2101_ADDR, I2C_TIMEOUT_MS) != ESP_OK ||
        !addDevice(AXP2101_ADDR, &s_pmic))
        return false;
    uint8_t chipId = 0;
    if (!readRegister(s_pmic, AXP2101_REG_CHIP_ID, chipId) || chipId != AXP2101_CHIP_ID) return false;

    // Enable only battery and VBUS ADC measurement channels. Preserve all unrelated bits.
    uint8_t adcControl = 0;
    if (!readRegister(s_pmic, AXP2101_REG_ADC_CHANNEL_CTRL, adcControl)) return false;
    adcControl |= 0x05;
    if (!writeRegister(s_pmic, AXP2101_REG_ADC_CHANNEL_CTRL, adcControl)) return false;
    return true;
}

void refreshPmic()
{
    Status next = status();
    uint8_t status1 = 0, status2 = 0, batteryHigh = 0, batteryLow = 0, percentage = 0;
    const bool ok = readRegister(s_pmic, AXP2101_REG_STATUS1, status1) &&
                    readRegister(s_pmic, AXP2101_REG_STATUS2, status2) &&
                    readRegister(s_pmic, AXP2101_REG_BAT_ADC_HIGH, batteryHigh) &&
                    readRegister(s_pmic, AXP2101_REG_BAT_ADC_LOW, batteryLow) &&
                    readRegister(s_pmic, AXP2101_REG_BAT_PERCENT, percentage);
    if (!ok) {
        next.sampleValid = false;
        ++next.errors;
        publishPmic(next);
        return;
    }

    next.sampleValid = true;
    next.batteryConnected = (status1 & (1U << 3)) != 0;
    next.usbPresent = (status1 & (1U << 5)) != 0 && (status2 & (1U << 3)) == 0;
    next.charging = ((status2 >> 5) & 0x03) == 1;
    next.chargeDone = (status2 & 0x07) == 4;
    next.batteryVoltageValid = next.batteryConnected &&
                               BoardPowerMath::batteryMillivolts(
                                   static_cast<uint16_t>(((batteryHigh & 0x1F) << 8) | batteryLow),
                                   next.batteryMillivolts);
    next.percentValid = next.batteryConnected && BoardPowerMath::percentage(percentage, next.percent);
    publishPmic(next);
}

bool readPowerButton(bool &pressed)
{
    uint8_t input = 0;
    if (!readRegister(s_expander, TCA9554_REG_INPUT, input)) {
        ++s_buttonReadErrors;
        if (s_buttonReadErrors == 1 || s_buttonReadErrors % 100 == 0)
            ESP_LOGW(TAG, "power-button read failed (%u errors)", (unsigned)s_buttonReadErrors);
        return false;
    }
    s_buttonReadErrors = 0;
    pressed = (input & EXIO_POWER_BUTTON) != 0;
    return true;
}

bool requestPmicShutdown()
{
    uint8_t config = 0;
    if (!readRegister(s_pmic, AXP2101_REG_COMMON_CONFIG, config)) {
        ESP_LOGE(TAG, "AXP2101 shutdown register read failed");
        return false;
    }
    if (!writeRegister(s_pmic, AXP2101_REG_COMMON_CONFIG, config | 0x01)) {
        ESP_LOGE(TAG, "AXP2101 shutdown request failed");
        return false;
    }
    ESP_LOGI(TAG, "AXP2101 shutdown requested by power button");
    return true;
}

void handlePowerButton(bool pressed)
{
    portENTER_CRITICAL(&s_statusMux);
    s_status.powerButtonPressed = pressed;
    portEXIT_CRITICAL(&s_statusMux);
    if (!pressed) {
        s_buttonHeldMs = 0;
        s_buttonShutdownIssued = false;
        return;
    }
    if (s_buttonHeldMs < BUTTON_POWER_OFF_MS) s_buttonHeldMs += BUTTON_POLL_MS;
    if (s_buttonHeldMs >= BUTTON_POWER_OFF_MS && !s_buttonShutdownIssued) {
        s_buttonShutdownIssued = true;
        portENTER_CRITICAL(&s_statusMux);
        s_status.powerOffRequested = true;
        portEXIT_CRITICAL(&s_statusMux);
        ESP_LOGI(TAG, "power-off requested after 2-second button hold");
    }
}

void sampleTask(void *)
{
    uint32_t pmicElapsedMs = 0;
    for (;;) {
        bool pressed = false;
        if (readPowerButton(pressed)) handlePowerButton(pressed);
        else handlePowerButton(false);
        pmicElapsedMs += BUTTON_POLL_MS;
        if (pmicElapsedMs >= 10000) {
            pmicElapsedMs = 0;
            refreshPmic();
        }
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}

}  // namespace

bool begin()
{
    if (s_status.initialized) return true;
    if (!OtrI2c::begin()) {
        Status next = {};
        next.errors = 1;
        publish(next);
        return false;
    }

    i2c_master_bus_config_t config = {};
    config.i2c_port = I2C_NUM_0;
    config.sda_io_num = PIN_I2C_SDA;
    config.scl_io_num = PIN_I2C_SCL;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;
    const esp_err_t busError = i2c_new_master_bus(&config, &s_bus);
    if (busError != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus initialization failed: %s", esp_err_to_name(busError));
        Status next = {};
        next.errors = 1;
        publish(next);
        return false;
    }
    if (!initializeExpander()) {
        ESP_LOGE(TAG, "TCA9554 missing or EXIO reset failed");
        Status next = {};
        next.errors = 1;
        publish(next);
        return false;
    }

    Status next = {};
    next.supported = initializePmic();
    next.initialized = true;
    if (!next.supported) {
        ++next.errors;
        ESP_LOGW(TAG, "AXP2101 missing or initialization failed; display I2C is ready");
    }
    publish(next);
    if (s_status.supported) {
        refreshPmic();
        ESP_LOGI(TAG, "AXP2101 PMIC ready at 0x%02X", AXP2101_ADDR);
        if (xTaskCreatePinnedToCore(sampleTask, "board_power", 3072, nullptr, 2, nullptr, 0) != pdPASS) {
            Status failed = status();
            ++failed.errors;
            publish(failed);
            ESP_LOGW(TAG, "periodic PMIC sampling task unavailable");
        }
    }
    ESP_LOGI(TAG, "shared I2C bus ready (SDA 8, SCL 7)");
    return true;
}

i2c_master_bus_handle_t bus() { return s_bus; }

bool resetPanel()
{
    if (!status().initialized || !initializeExpander()) {
        ESP_LOGE(TAG, "shared LCD/touch reset failed");
        return false;
    }
    return true;
}

Status status()
{
    portENTER_CRITICAL(&s_statusMux);
    const Status copy = s_status;
    portEXIT_CRITICAL(&s_statusMux);
    return copy;
}

bool powerOff()
{
    if (!status().supported || !requestPmicShutdown()) return false;
    cancelPowerOff();
    return true;
}

void cancelPowerOff()
{
    portENTER_CRITICAL(&s_statusMux);
    s_status.powerOffRequested = false;
    portEXIT_CRITICAL(&s_statusMux);
}

}  // namespace BoardPower
#else
namespace BoardPower {

bool begin() { return false; }
bool resetPanel() { return false; }
i2c_master_bus_handle_t bus() { return nullptr; }
Status status() { return {}; }
bool powerOff() { return false; }
void cancelPowerOff() {}

}  // namespace BoardPower
#endif
