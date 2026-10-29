// board_pins_es3c35p.h - GPIO map for the Heemol / LCDWIKI ES3C35P ESP32-S3 3.5" board.
// Source: docs/hardware_reference.md (section 2). Verify critical pins on the physical board.
#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Display: Sitronix ST77922 TDDI, QSPI, native portrait 320 x 480, RGB565
// ---------------------------------------------------------------------------
constexpr int PIN_LCD_CS   = 10;   // Active-low chip select (also on expansion header)
constexpr int PIN_LCD_CLK  = 12;
constexpr int PIN_LCD_D0   = 11;   // Also on expansion header
constexpr int PIN_LCD_D1   = 13;   // Also on expansion header
constexpr int PIN_LCD_D2   = 14;   // Also on expansion header
constexpr int PIN_LCD_D3   = 9;
constexpr int PIN_LCD_TE   = 42;   // Tearing-effect output from the panel
constexpr int PIN_LCD_BL   = 41;   // Backlight enable, active high, PWM capable
constexpr bool LCD_BL_ACTIVE_LOW = false;
constexpr int PIN_LCD_RST  = -1;   // Not a GPIO: panel reset is tied to CHIP_PU / EN

constexpr int LCD_H_RES = 320;
constexpr int LCD_V_RES = 480;
constexpr uint32_t LCD_QSPI_HZ = 40UL * 1000UL * 1000UL;  // Proven clock (reference: 40 MHz)
constexpr const char *LCD_BUS_DESC = "QSPI 40 MHz";

// ---------------------------------------------------------------------------
// Shared I2C bus: touch (0x55), ES8311 codec (0x18), external I2C connector
// ---------------------------------------------------------------------------
constexpr int PIN_I2C_SDA = 38;
constexpr int PIN_I2C_SCL = 39;
constexpr uint32_t I2C_FREQ_HZ = 400000;

constexpr int PIN_TOUCH_INT = 47;  // Active-low interrupt from the touch controller
constexpr int PIN_TOUCH_RST = 48;  // Active-low reset of the touch controller
constexpr uint8_t TOUCH_I2C_ADDR = 0x55;
constexpr uint8_t ES8311_I2C_ADDR = 0x18;

// ---------------------------------------------------------------------------
// RGB status LED: one WS2812B-compatible LED, GRB byte order (verify on board)
// ---------------------------------------------------------------------------
constexpr int PIN_WS2812 = 40;

// ---------------------------------------------------------------------------
// microSD, 4-bit SDMMC (board_sd.cpp, 1-bit fallback)
// ---------------------------------------------------------------------------
constexpr int PIN_SD_CLK = 5;
constexpr int PIN_SD_CMD = 4;
constexpr int PIN_SD_D0  = 6;
constexpr int PIN_SD_D1  = 7;
constexpr int PIN_SD_D2  = 2;
constexpr int PIN_SD_D3  = 3;

// ---------------------------------------------------------------------------
// Audio: ES8311 codec + speaker amplifier (board_audio.cpp)
// ---------------------------------------------------------------------------
constexpr int PIN_I2S_DOUT = 15;   // ESP32 -> ES8311
constexpr int PIN_I2S_DIN  = 16;   // ES8311 -> ESP32
constexpr int PIN_I2S_MCLK = 17;
constexpr int PIN_I2S_BCLK = 18;
constexpr int PIN_I2S_WS   = 21;
constexpr int PIN_AMP_EN   = 1;    // Active LOW: LOW enables the amplifier. Kept HIGH (off) until the codec is initialised.
constexpr bool AMP_EN_ACTIVE_HIGH = false;

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------
constexpr int PIN_BOOT_BTN = 0;    // BOOT button (strapping pin)
constexpr int PIN_BAT_ADC  = 8;    // S3 battery ADC1_CH7; divider ratio is unverified
constexpr uint16_t BAT_DEFAULT_SCALE_PERMILLE = 0;   // Unknown divider: user calibration required
// GPIO45 / GPIO46 are strapping pins on the expansion header - do not force levels at reset.
