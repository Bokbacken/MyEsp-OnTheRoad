// Waveshare ESP32-S3-Touch-LCD-3.5B, vendor factory BSP pin map.
// PCB revision is not confirmed. Reset/power are on TCA9554, not ESP GPIOs.
#pragma once

#include <stdint.h>

constexpr int PIN_LCD_CS = 12;
constexpr int PIN_LCD_CLK = 5;
constexpr int PIN_LCD_D0 = 1;
constexpr int PIN_LCD_D1 = 2;
constexpr int PIN_LCD_D2 = 3;
constexpr int PIN_LCD_D3 = 4;
constexpr int PIN_LCD_TE = -1;
constexpr int PIN_LCD_BL = 6;
constexpr bool LCD_BL_ACTIVE_LOW = false;
constexpr int PIN_LCD_RST = -1;
constexpr int LCD_H_RES = 320;
constexpr int LCD_V_RES = 480;
constexpr uint32_t LCD_QSPI_HZ = 40UL * 1000UL * 1000UL;
constexpr const char *LCD_BUS_DESC = "QSPI 40 MHz";
constexpr const char *LCD_MODEL = "AXS15231B QSPI";
constexpr const char *TOUCH_MODEL = "AXS15231B";

constexpr int PIN_I2C_SDA = 8;
constexpr int PIN_I2C_SCL = 7;
constexpr uint32_t I2C_FREQ_HZ = 400000;
constexpr int PIN_TOUCH_INT = -1;
constexpr int PIN_TOUCH_RST = -1;
constexpr uint8_t TOUCH_I2C_ADDR = 0x3B;
constexpr uint8_t ES8311_I2C_ADDR = 0x18;

constexpr int PIN_WS2812 = -1;
constexpr int PIN_SD_CLK = 11;
constexpr int PIN_SD_CMD = 10;
constexpr int PIN_SD_D0 = 9;
constexpr int PIN_SD_D1 = -1;
constexpr int PIN_SD_D2 = -1;
constexpr int PIN_SD_D3 = -1;

constexpr int PIN_I2S_MCLK = 44;
constexpr int PIN_I2S_BCLK = 13;
constexpr int PIN_I2S_WS = 15;
constexpr int PIN_I2S_DOUT = 16;
constexpr int PIN_I2S_DIN = 14;
constexpr int PIN_AMP_EN = -1;
constexpr bool AMP_EN_ACTIVE_HIGH = false;

constexpr int PIN_BOOT_BTN = 0;
constexpr int PIN_BAT_ADC = -1;
constexpr uint16_t BAT_DEFAULT_SCALE_PERMILLE = 0;
