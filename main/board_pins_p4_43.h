// board_pins_p4_43.h - GPIO map for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3.
// Sources: docs/Waveshare_ESP32-P4-WIFI6-Touch-LCD-4.3_Hardware_Reference.md and the
// Waveshare BSP esp32_p4_wifi6_touch_lcd_4_3 v1.0.1.
#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Display: ST7701, 2-lane MIPI-DSI, native portrait 480 x 800, RGB565
// ---------------------------------------------------------------------------
constexpr int PIN_LCD_RST  = 27;
constexpr int PIN_LCD_BL   = 26;   // PWM, active LOW
constexpr bool LCD_BL_ACTIVE_LOW = true;

constexpr int LCD_H_RES = 480;
constexpr int LCD_V_RES = 800;
constexpr int LCD_DSI_LANES = 2;
constexpr uint32_t LCD_DSI_LANE_MBPS = 500;
constexpr uint32_t LCD_DPI_CLK_MHZ = 30;
constexpr int LCD_DSI_PHY_LDO_CHAN = 3;
constexpr int LCD_DSI_PHY_LDO_MV = 2500;
constexpr const char *LCD_BUS_DESC = "MIPI-DSI 2x500 Mbps";

// ---------------------------------------------------------------------------
// Shared I2C bus: GT911 touch (0x5D / 0x14), ES8311 (0x18), ES7210 (0x40)
// ---------------------------------------------------------------------------
constexpr int PIN_I2C_SDA = 7;
constexpr int PIN_I2C_SCL = 8;
constexpr uint32_t I2C_FREQ_HZ = 400000;

constexpr int PIN_TOUCH_INT = -1;  // Polled
constexpr int PIN_TOUCH_RST = -1;
constexpr uint8_t TOUCH_I2C_ADDR = 0x5D;
constexpr uint8_t TOUCH_I2C_ADDR_ALT = 0x14;
constexpr uint8_t ES8311_I2C_ADDR = 0x18;

// No RGB status LED on this board.
constexpr int PIN_WS2812 = -1;

// ---------------------------------------------------------------------------
// microSD: SDMMC slot 0 (IO MUX pins), card powered by on-chip LDO channel 4.
// SDMMC slot 1 is used by ESP-Hosted for the ESP32-C6.
// ---------------------------------------------------------------------------
constexpr int PIN_SD_CLK = 43;
constexpr int PIN_SD_CMD = 44;
constexpr int PIN_SD_D0  = 39;
constexpr int PIN_SD_D1  = 40;
constexpr int PIN_SD_D2  = 41;
constexpr int PIN_SD_D3  = 42;
constexpr int SD_SLOT = 0;
constexpr int SD_PWR_LDO_CHAN = 4;

// ---------------------------------------------------------------------------
// Audio: ES8311 codec + NS4150B amplifier
// ---------------------------------------------------------------------------
constexpr int PIN_I2S_DOUT = 9;
constexpr int PIN_I2S_DIN  = 11;
constexpr int PIN_I2S_MCLK = 13;
constexpr int PIN_I2S_BCLK = 12;
constexpr int PIN_I2S_WS   = 10;
constexpr int PIN_AMP_EN   = 53;
constexpr bool AMP_EN_ACTIVE_HIGH = true;

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------
constexpr int PIN_BOOT_BTN = 35;    // BOOT button (strapping pin, also on header)
constexpr int PIN_BAT_ADC  = 20;   // 200k/100k divider -> x3
constexpr uint16_t BAT_DEFAULT_SCALE_PERMILLE = 3000;
