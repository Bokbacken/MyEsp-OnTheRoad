#pragma once

#include "driver/i2c_master.h"
#include "sdkconfig.h"

namespace OtrI2c {

#if defined(CONFIG_OTR_HW_PROFILE_S3_35B)
// Initialize once before starting any shared-bus users.
bool begin();
// STOP-separated command/read, holding the shared lock across both transfers.
esp_err_t transmitThenReceive(i2c_master_dev_handle_t device, const uint8_t *tx, size_t txSize,
                              uint8_t *rx, size_t rxSize, int timeoutMs);
esp_err_t probe(i2c_master_bus_handle_t bus, uint16_t address, int timeoutMs);
esp_err_t transmit(i2c_master_dev_handle_t device, const uint8_t *data, size_t size, int timeoutMs);
esp_err_t transmitReceive(i2c_master_dev_handle_t device, const uint8_t *tx, size_t txSize,
                          uint8_t *rx, size_t rxSize, int timeoutMs);
#else
inline esp_err_t probe(i2c_master_bus_handle_t bus, uint16_t address, int timeoutMs)
{
    return i2c_master_probe(bus, address, timeoutMs);
}
inline esp_err_t transmit(i2c_master_dev_handle_t device, const uint8_t *data, size_t size, int timeoutMs)
{
    return i2c_master_transmit(device, data, size, timeoutMs);
}
inline esp_err_t transmitReceive(i2c_master_dev_handle_t device, const uint8_t *tx, size_t txSize,
                                 uint8_t *rx, size_t rxSize, int timeoutMs)
{
    return i2c_master_transmit_receive(device, tx, txSize, rx, rxSize, timeoutMs);
}
#endif

}  // namespace OtrI2c
