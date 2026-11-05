# Waveshare ESP32-S3-Touch-LCD-3.5B hardware reference

## Revision and evidence

The physical PCB revision has not been identified. Do not treat the board as V1 or V2 based only on the product name. The firmware profile uses the common display, touch, I2C, expander, and PMIC wiring, probes optional devices at runtime, and does not assume that the V2 RTC is fitted.

The manufacturer repository at commit [`c7dd63c70258da7e3b6dae0c621f30ca62b1e1c9`](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5B/tree/c7dd63c70258da7e3b6dae0c621f30ca62b1e1c9) contains both [`V1 schematic`](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5B/blob/c7dd63c70258da7e3b6dae0c621f30ca62b1e1c9/hardware/schematic/ESP32-S3-Touch-LCD-3.5B-V1-Schematic.pdf) and [`V2 schematic`](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5B/blob/c7dd63c70258da7e3b6dae0c621f30ca62b1e1c9/hardware/schematic/ESP32-S3-Touch-LCD-3.5B-V2-Schematic.pdf). Both show the TCA9554 expander, EXIO1 connected to LCD reset, EXIO6, the RTC nets, and the shared I2C lines. The V2 sheet explicitly identifies U4 as PCF85063ATL; the V1 sheet does not identify a PCF85063 part. The RTC therefore remains optional and must be detected, not presumed.

The manufacturer factory BSP source [`bsp_board.cpp`](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5B/blob/c7dd63c70258da7e3b6dae0c621f30ca62b1e1c9/examples/factory/components/esp32_s3_touch_lcd_3_5b/bsp_board.cpp) identifies the AXP2101 and its I2C address, uses the TCA9554 address and EXIO reset/button pins, and identifies the PCF85063 address. BSP initialization also changes charger limits and many PMIC rails; this project intentionally does not copy those settings. No battery chemistry, charging current, charge voltage, or termination settings are inferred from the BSP.

## Board wiring

| Function | Connection |
|---|---|
| MCU/display | ESP32-S3, AXS15231B QSPI panel, 320 × 480 |
| I2C | SDA GPIO8, SCL GPIO7; all devices including touch at 400 kHz |
| Touch | AXS15231B, I2C address `0x3B`; polled with an atomic command/read transaction and 14-byte report |
| Audio codec | ES8311, I2C address `0x18` |
| Expander | TCA9554, I2C address `0x20` |
| Panel reset | TCA9554 EXIO1; active-low pulse during board initialization |
| Power button signal | TCA9554 EXIO6, configured as an input; active high while pressed |
| PMIC | AXP2101, I2C address `0x34` |
| RTC | PCF85063, I2C address `0x51` when fitted |

`BoardPower::begin()` creates the shared I2C bus and performs the expander reset before display/touch initialization. Display and touch reuse `BoardPower::bus()`; neither creates another bus. Reset remains owned by `BoardPower`, including the bounded startup recovery described below. The PMIC voltage-monitoring and fuel-gauge values are read periodically and exposed from a cached status snapshot, so UI and web diagnostics do not perform I2C transactions.

The PWR button is polled through TCA9554 EXIO6. A short press wakes the backlight if the screen is off; it does not put an awake screen to sleep. Holding the button for two seconds requests shutdown. OTA, file transfers, downloads, and SD formatting are allowed up to 30 seconds to finish first; if still active, shutdown is cancelled with an on-screen explanation. Otherwise bit 0 of the AXP2101 common-configuration register is set, following the manufacturer library's shutdown implementation.

The board-specific factory BSP uses an 11-byte command requesting 14 bytes and `i2c_master_transmit_receive`. The manufacturer's [Arduino touch driver](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5B/blob/c7dd63c70258da7e3b6dae0c621f30ca62b1e1c9/examples/arduino/libraries/esp_lcd_touch_axs15231b/esp_lcd_touch_axs15231b.cpp) uses the same command/report size, but ends the write with STOP before a separate read. An initial firmware experiment adopted that variant; owner logs still showed five consecutive `0xD7` and then ten `0x13` reports after power-on, including after a shared LCD/touch reset. Touch now uses the factory BSP's atomic combined transaction, avoiding a possible interleaving between the two transfers and the concurrent PWR polling task. The race was possible but is not confirmed as the cause. Owner logs also showed that neither 100 kHz nor the shared reset changed the startup result, so touch is back at the manufacturer's 400 kHz. Those startup frames were not corruption; see the no-contact filler rule below.

Startup allows five attempts spaced 50 ms apart. If all fail, the display driver permits one shared EXIO1 reset (100 ms low, 200 ms settling), replays the full panel initialization, and restores the existing framebuffer before another five touch reads. It reuses the existing SPI/LVGL resources, does not reboot the MCU or change PMIC rails/charger settings, and refuses recovery after the LVGL task starts or after a DMA transport fault. Failed reset/reinitialization is propagated as a display failure; persistent invalid reports remain a touch failure. Automatic recovery after cold power-on still requires physical verification.

The firmware accepts non-release contact events including event 3, and rejects invalid point counts or out-of-range coordinates without clamping them into unintended taps. Rate-limited logs distinguish transport failures from invalid reports and include all 14 report bytes for the latter. During an existing press, failed reads retain only the last valid coordinates for less than 60 ms from the last valid contact, sharing the existing no-contact debounce deadline. Errors never extend this deadline, and time blocked in I2C counts toward it. A valid release ends the press immediately. Corruption cancels the gesture's click even if contact resumes within that interval; it still increments the error counter. This avoids repeated visual release/repress transitions on brief dropouts without allowing persistent failures to hold a button indefinitely.

Click suppression checks movement in both contact and release reports. Moving more than 20 pixels from the initial contact cancels all click variants until the next press, including when a quick swipe has only a start frame and a release frame. Release and scrolling events remain available to widgets.

Hardware observation (owner, 2026-11-04/05): without contact the controller returns fourteen copies of a single filler byte, which stop while a finger is held and return after release. The byte differs between boots (`0x02`, `0x13`, `0x17`, `0x37`, `0x57` and `0xD7` observed). Waveshare's factory BSP discards these frames silently (`data[1] > 2` gives no touch). Treating every value other than `0x37` as an error made startup report "touch not found" whenever the finger was off the screen at boot. Any fully uniform report is therefore accepted as no point, both at startup and at runtime, with the normal 60 ms release debounce; a one-time informational log records its detection. Other malformed reports and all I2C failures still increment the error counter and use the bounded failure handling above.

AXP2101 battery ADC data is a 13-bit value already expressed in millivolts, without a 1.1 multiplier. This matches the vendor's pinned [`XPowersAXP2101.tpp` `getBattVoltage()`](https://github.com/waveshareteam/ESP32-S3-Touch-LCD-3.5B/blob/c7dd63c70258da7e3b6dae0c621f30ca62b1e1c9/examples/arduino/libraries/XPowersLib/src/XPowersAXP2101.tpp) and its `readRegisterH5L8` helper. The firmware reports the PMIC fuel-gauge percentage only when its byte is in the 0–100 range. USB presence, charging state, charge-done state, and battery connection are reported from PMIC status registers. The driver only enables the battery-voltage and VBUS ADC measurement channels; it does not change charger current/voltage, battery parameters, regulator rails, or LED policy. Neither this PMIC API nor the PCF85063 provides a supported board-temperature reading.

RTC validity is checked from the PCF85063 oscillator-stop flag, date fields, and a seconds-register tick. The clock is interpreted as UTC. If the optional RTC is missing or its time is invalid, normal system time synchronization remains responsible for setting it.

## Firmware integration

Select `CONFIG_OTR_HW_PROFILE_S3_35B` for this board. No new managed component dependency is required for RTC/power support. See `main/board_pins_s3_35b.h` for the display, touch, SD, and audio pin map, and `main/board_power.h` / `main/board_battery.h` for the cached power APIs.

A fresh board's internal FFat volume is formatted automatically only if every logical byte is erased (`0xFF`). The check uses the wear-levelling handle because even a failed FatFs mount writes wear-levelling metadata. Any logical data or read failure prevents automatic formatting. Failed-mount handles are released before retry or explicit repair. SD mounting/formatting is separate; an FFat failure does not mean that the SD card failed.
