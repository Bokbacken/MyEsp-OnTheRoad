# Waveshare ESP32-S3-Touch-LCD-3.5B hardware reference

## Revision and evidence

The physical PCB revision has not been identified. Do not treat the board as V1 or V2 based only on the product name. The firmware profile uses the common display, touch, I2C, expander, and PMIC wiring, probes optional devices at runtime, and does not assume that the V2 RTC is fitted.

The manufacturer repository at commit [`c7dd63c70258da7e3b6dae0c621f30ca62b1e1c9`](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5B/tree/c7dd63c70258da7e3b6dae0c621f30ca62b1e1c9) contains both [`V1 schematic`](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5B/blob/c7dd63c70258da7e3b6dae0c621f30ca62b1e1c9/hardware/schematic/ESP32-S3-Touch-LCD-3.5B-V1-Schematic.pdf) and [`V2 schematic`](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5B/blob/c7dd63c70258da7e3b6dae0c621f30ca62b1e1c9/hardware/schematic/ESP32-S3-Touch-LCD-3.5B-V2-Schematic.pdf). Both show the TCA9554 expander, EXIO1 connected to LCD reset, EXIO6, the RTC nets, and the shared I2C lines. The V2 sheet explicitly identifies U4 as PCF85063ATL; the V1 sheet does not identify a PCF85063 part. The RTC therefore remains optional and must be detected, not presumed.

The manufacturer factory BSP source [`bsp_board.cpp`](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5B/blob/c7dd63c70258da7e3b6dae0c621f30ca62b1e1c9/examples/factory/components/esp32_s3_touch_lcd_3_5b/bsp_board.cpp) identifies the AXP2101 and its I2C address, uses the TCA9554 address and EXIO reset/button pins, and identifies the PCF85063 address. BSP initialization also changes charger limits and many PMIC rails; this project intentionally does not copy those settings. No battery chemistry, charging current, charge voltage, or termination settings are inferred from the BSP.

## Board wiring

| Function | Connection |
|---|---|
| MCU/display | ESP32-S3, AXS15231B QSPI panel, 320 × 480 |
| I2C | SDA GPIO8, SCL GPIO7, 400 kHz |
| Touch | AXS15231B, I2C address `0x3B` |
| Audio codec | ES8311, I2C address `0x18` |
| Expander | TCA9554, I2C address `0x20` |
| Panel reset | TCA9554 EXIO1; active-low pulse during board initialization |
| Power button signal | TCA9554 EXIO6, configured as an input |
| PMIC | AXP2101, I2C address `0x34` |
| RTC | PCF85063, I2C address `0x51` when fitted |

`BoardPower::begin()` creates the shared I2C bus and performs the expander reset before display/touch initialization. Display and touch reuse `BoardPower::bus()`; neither creates another bus or toggles reset. The PMIC voltage-monitoring and fuel-gauge values are read periodically and exposed from a cached status snapshot, so UI and web diagnostics do not perform I2C transactions.

AXP2101 battery ADC data uses a 13-bit result with 1.1 mV/LSB; the firmware converts it to millivolts and reports the PMIC fuel-gauge percentage only when its byte is in the 0–100 range. USB presence, charging state, charge-done state, and battery connection are reported from PMIC status registers. The driver only enables the battery-voltage and VBUS ADC measurement channels; it does not change charger current/voltage, battery parameters, regulator rails, or LED policy. Neither this PMIC API nor the PCF85063 provides a supported board-temperature reading.

RTC validity is checked from the PCF85063 oscillator-stop flag, date fields, and a seconds-register tick. The clock is interpreted as UTC. If the optional RTC is missing or its time is invalid, normal system time synchronization remains responsible for setting it.

## Firmware integration

Select `CONFIG_OTR_HW_PROFILE_S3_35B` for this board. No new managed component dependency is required for RTC/power support. See `main/board_pins_s3_35b.h` for the display, touch, SD, and audio pin map, and `main/board_power.h` / `main/board_battery.h` for the cached power APIs.

