#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

struct FakeDevice {
    uint8_t address;
};

uint8_t registers[128][256] = {};
bool present[128] = {};
FakeDevice devices[128] = {};
std::vector<uint8_t> expanderOutputWrites;
int transactionCount = 0;
int delayedMs = 0;
TaskFunction_t scheduledTask = nullptr;
bool pacingReady = false;

}  // namespace

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *config, i2c_master_bus_handle_t *bus)
{
    assert(config->i2c_port == I2C_NUM_0);
    assert(config->sda_io_num == GPIO_NUM_8 && config->scl_io_num == GPIO_NUM_7);
    assert(config->flags.enable_internal_pullup);
    *bus = reinterpret_cast<void *>(1);
    return ESP_OK;
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int)
{
    assert(bus);
    return address < 128 && present[address] ? ESP_OK : ESP_FAIL;
}

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *config,
                                    i2c_master_dev_handle_t *device)
{
    assert(bus && config->dev_addr_length == I2C_ADDR_BIT_LEN_7 && config->scl_speed_hz == 400000);
    devices[config->device_address].address = static_cast<uint8_t>(config->device_address);
    *device = &devices[config->device_address];
    return ESP_OK;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t *tx, size_t txSize,
                                      uint8_t *rx, size_t rxSize, int)
{
    assert(device && txSize == 1);
    const uint8_t address = static_cast<FakeDevice *>(device)->address;
    if (!present[address]) return ESP_FAIL;
    ++transactionCount;
    for (size_t i = 0; i < rxSize; ++i) rx[i] = registers[address][static_cast<uint8_t>(tx[0] + i)];
    return ESP_OK;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device, const uint8_t *tx, size_t size, int)
{
    assert(device && size >= 2);
    const uint8_t address = static_cast<FakeDevice *>(device)->address;
    if (!present[address]) return ESP_FAIL;
    ++transactionCount;
    for (size_t i = 1; i < size; ++i) registers[address][static_cast<uint8_t>(tx[0] + i - 1)] = tx[i];
    if (address == 0x20 && tx[0] == 0x01) expanderOutputWrites.push_back(tx[1]);
    return ESP_OK;
}

void vTaskDelay(int ticks) { delayedMs += ticks; }

int xTaskCreatePinnedToCore(TaskFunction_t entry, const char *, int, void *, int, void *, int)
{
    scheduledTask = entry;
    return pdPASS;
}

#include "otr_i2c.h"
namespace OtrI2c {
bool begin() { return pacingReady; }
esp_err_t probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout)
{
    return i2c_master_probe(bus, address, timeout);
}
esp_err_t transmit(i2c_master_dev_handle_t device, const uint8_t *data, size_t size, int timeout)
{
    return i2c_master_transmit(device, data, size, timeout);
}
esp_err_t transmitReceive(i2c_master_dev_handle_t device, const uint8_t *tx, size_t txSize,
                          uint8_t *rx, size_t rxSize, int timeout)
{
    return i2c_master_transmit_receive(device, tx, txSize, rx, rxSize, timeout);
}
}

#include "../main/board_power.cpp"

int main()
{
    assert(!BoardPower::begin());
    assert(!BoardPower::status().initialized && BoardPower::status().errors == 1);
    assert(transactionCount == 0 && scheduledTask == nullptr);
    pacingReady = true;
    present[0x20] = present[0x34] = true;
    registers[0x20][0x01] = 0xFF;
    registers[0x20][0x03] = 0xFF;
    registers[0x34][0x03] = 0x4A;
    registers[0x34][0x30] = 0xA0;
    registers[0x34][0x00] = 0x28; // battery connected and VBUS good
    registers[0x34][0x01] = 0x24; // charging and charge done status bits
    constexpr uint16_t rawBattery = 4076; // AXP2101 reports millivolts directly.
    registers[0x34][0x34] = static_cast<uint8_t>(rawBattery >> 8);
    registers[0x34][0x35] = static_cast<uint8_t>(rawBattery);
    registers[0x34][0xA4] = 77;

    assert(BoardPower::begin());
    assert(BoardPower::bus());
    assert(BoardPower::status().initialized);
    assert(BoardPower::status().supported);
    assert((registers[0x20][0x03] & (1U << 1)) == 0); // EXIO1 output
    assert((registers[0x20][0x03] & (1U << 6)) != 0); // EXIO6 input
    assert((registers[0x34][0x30] & 0x05) == 0x05); // battery and VBUS monitors enabled
    assert(expanderOutputWrites.size() == 2);
    assert((expanderOutputWrites[0] & (1U << 1)) == 0);
    assert((expanderOutputWrites[1] & (1U << 1)) != 0);
    assert(delayedMs == 300);
    assert(scheduledTask != nullptr);

    const BoardPower::Status sample = BoardPower::status();
    assert(sample.sampleValid && sample.batteryConnected && sample.batteryVoltageValid);
    assert(sample.batteryMillivolts == 4076);
    assert(sample.usbPresent && sample.charging && sample.chargeDone);
    assert(sample.percentValid && sample.percent == 77);
    assert(!sample.powerButtonPressed);
    const int before = transactionCount;
    (void)BoardPower::status();
    assert(transactionCount == before);

    const uint8_t outputBeforeReset = registers[0x20][0x01];
    const uint8_t directionBeforeReset = registers[0x20][0x03];
    const uint8_t adcBeforeReset = registers[0x34][0x30];
    assert(BoardPower::resetPanel());
    assert(expanderOutputWrites.size() == 4 && delayedMs == 600);
    assert(expanderOutputWrites[2] == (outputBeforeReset & ~(1U << 1)));
    assert(expanderOutputWrites[3] == outputBeforeReset);
    assert(registers[0x20][0x03] == directionBeforeReset);
    assert(registers[0x34][0x30] == adcBeforeReset);
    present[0x20] = false;
    assert(!BoardPower::resetPanel());
    present[0x20] = true;

    bool pressed = false;
    registers[0x20][0x00] = 1U << 6; // Vendor BSP confirms PWR press is active high.
    assert(BoardPower::readPowerButton(pressed) && pressed);
    static_assert(BoardPower::BUTTON_POLL_MS == 109);
    static_assert(BoardPower::BUTTON_POWER_OFF_MS == 2000);
    for (int i = 0; i < 18; ++i) BoardPower::handlePowerButton(true);
    assert(!BoardPower::status().powerOffRequested);
    BoardPower::handlePowerButton(false);
    assert(!BoardPower::status().powerOffRequested);
    for (int i = 0; i < 18; ++i) BoardPower::handlePowerButton(true);
    assert(!BoardPower::status().powerOffRequested);
    assert((registers[0x34][0x10] & 0x01) == 0);
    BoardPower::handlePowerButton(true);
    assert(BoardPower::status().powerOffRequested);
    assert((registers[0x34][0x10] & 0x01) == 0); // The UI waits for active operations first.
    assert(BoardPower::powerOff());
    assert((registers[0x34][0x10] & 0x01) == 0x01); // 2 seconds requests PMIC shutdown.
    assert(!BoardPower::status().powerOffRequested);
    BoardPower::handlePowerButton(false);
    assert(!BoardPower::status().powerButtonPressed);

    puts("S3 3.5B bus, expander-reset, and cached PMIC checks passed");
    return 0;
}
