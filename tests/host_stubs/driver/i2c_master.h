#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"

using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_FAIL = -1;
constexpr int I2C_NUM_0 = 0;
constexpr int I2C_CLK_SRC_DEFAULT = 0;
constexpr int I2C_ADDR_BIT_LEN_7 = 0;
using i2c_master_bus_handle_t = void *;
using i2c_master_dev_handle_t = void *;

struct i2c_master_bus_config_t {
    int i2c_port;
    gpio_num_t sda_io_num;
    gpio_num_t scl_io_num;
    int clk_source;
    uint8_t glitch_ignore_cnt;
    struct {
        bool enable_internal_pullup;
    } flags;
};

struct i2c_device_config_t {
    int dev_addr_length;
    uint16_t device_address;
    uint32_t scl_speed_hz;
};

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *, i2c_master_bus_handle_t *);
esp_err_t i2c_master_probe(i2c_master_bus_handle_t, uint16_t, int);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t, const i2c_device_config_t *,
                                    i2c_master_dev_handle_t *);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t, const uint8_t *, size_t, uint8_t *, size_t, int);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t, const uint8_t *, size_t, int);
