# Heemol 3.5-inch ESP32-S3 Touch Display

## Hardware reference, pinout, drivers, and bring-up notes

**Document status:** Working hardware reference based on the photographed board, manufacturer specifications, and matching LCDWIKI ES3C35P technical material.  
**Board marking:** `3.5" LCD Display / ESP32-S3 320x480 / Capacitive Touch`  
**Likely reference design:** LCDWIKI ES3C35P or a rebadged/closely compatible version.

> **Confidence note:** The photographed PCB layout, connector labels, ESP32-S3 N16R8 specification, ST77922 display, 320 × 480 resolution, audio, microSD, battery support, and exposed I²C pins all match the ES3C35P documentation very closely. Treat the pin allocation below as highly likely, but verify critical functions on the actual board before connecting external hardware.

---

## 1. Hardware overview

| Function | Identified hardware |
|---|---|
| MCU | Espressif ESP32-S3, dual-core Xtensa LX7, up to 240 MHz |
| Flash | 16 MB QSPI flash, N16 |
| PSRAM | 8 MB OPI/octal PSRAM, R8 |
| Display | 3.5-inch IPS TFT, native portrait 320 × 480 |
| Pixel format | RGB565, 65K colors |
| Display/touch controller | Sitronix ST77922 TDDI |
| Display interface | QSPI/quad SPI |
| Touch interface | Capacitive touch over I²C, address `0x55` |
| Audio codec | Everest Semiconductor ES8311, I²C control + I²S audio |
| Speaker amplifier | FM8002E or board-revision-compatible equivalent |
| Microphone | Onboard MEMS microphone through ES8311 |
| RGB status LED | One WS2812B-compatible addressable LED |
| Storage | microSD/TF slot using 4-bit SDMMC/SDIO |
| USB | USB-C connected to native ESP32-S3 USB |
| Battery | 3.7 V LiPo connection and TP4054 charging circuit |
| Controls | RESET and BOOT buttons |
| External connections | UART, battery, I²C, GPIO, and speaker connectors |

### Visible PCB features

The rear photograph shows:

- USB-C receptacle.
- RESET and BOOT buttons.
- `SPEAKER` connector.
- `UART` connector.
- `BAT` battery connector with polarity marking.
- `I2C` connector.
- GPIO expansion connector.
- microSD/TF card slot.
- RGB LED package near the lower center.
- ESP32-S3 IC/module area and supporting power, audio, and interface circuitry.
- 40-pin display-flex connector.

---

## 2. Complete working pin map

| GPIO | Function | Notes |
|---:|---|---|
| 0 | BOOT button | Hold during reset/power-up for download mode |
| 1 | Speaker amplifier enable | Reported active low: LOW enables amplifier |
| 2 | SD D2 | 4-bit SDMMC data 2 |
| 3 | SD D3 | 4-bit SDMMC data 3 |
| 4 | SD CMD | SDMMC command |
| 5 | SD CLK | SDMMC clock |
| 6 | SD D0 | SDMMC data 0 |
| 7 | SD D1 | SDMMC data 1 |
| 8 | Battery voltage ADC | Requires calibration/divider information before calculating battery voltage |
| 9 | LCD QSPI D3 | Shared with display, not a free GPIO |
| 10 | LCD QSPI CS | Active-low chip select; also exposed on expansion connector |
| 11 | LCD QSPI D0 | Also exposed on expansion connector |
| 12 | LCD QSPI CLK | Display clock |
| 13 | LCD QSPI D1 | Also exposed on expansion connector |
| 14 | LCD QSPI D2 | Also exposed on expansion connector |
| 15 | I²S DOUT | ESP32 to ES8311 audio playback data |
| 16 | I²S DIN | ES8311/microphone to ESP32 audio data |
| 17 | I²S MCLK | Audio master clock |
| 18 | I²S BCLK | Audio bit clock |
| 21 | I²S WS/LRCLK | Audio word-select clock |
| 38 | I²C SDA | Shared by touch, ES8311, and external I²C connector |
| 39 | I²C SCL | Shared by touch, ES8311, and external I²C connector |
| 40 | WS2812B RGB LED data | One addressable LED |
| 41 | LCD backlight | Active high; PWM-capable for brightness control |
| 42 | LCD TE | Tearing-effect synchronization signal |
| 45 | Expansion/strapping pin | Use carefully during reset/boot |
| 46 | Expansion/strapping pin | Use carefully during reset/boot |
| 47 | Touch interrupt | ST77922 touch IRQ/INT |
| 48 | Touch reset | ST77922 touch RESET |
| EN | Board reset | LCD reset is reportedly tied to CHIP_PU/EN |

### Important GPIO warning

GPIO10, GPIO11, GPIO13, and GPIO14 are brought to the external header but are also used by the LCD QSPI bus. They must **not** be treated as ordinary spare GPIO while the display is active. GPIO45 and GPIO46 are ESP32-S3 strapping pins and must not be forced to unsuitable levels during reset.

---

## 3. Connectors visible on the photographed board

Read with the PCB text upright. Always confirm connector orientation and continuity before attaching a cable.

### UART connector

Visible silkscreen:

```text
5V
GND
TXD
RXD
```

This is intended for power/TTL UART access. Confirm whether TXD/RXD are connected to the default programming UART or another assigned UART before applying external signals. ESP32 GPIO is 3.3 V logic and is not 5 V tolerant.

### I²C connector

Visible silkscreen:

```text
3.3V
GND
GPIO39 / SCL
GPIO38 / SDA
```

Expected bus settings:

- SDA: GPIO38
- SCL: GPIO39
- Common tested bus rate: 400 kHz
- Touch controller: `0x55`
- ES8311 codec: normally `0x18`

Because touch, audio codec, and external devices share this bus, check for address conflicts before adding sensors.

### GPIO expansion connector

Visible labels include:

```text
3.3V
GND
IO14
IO13
IO11
IO10
```

These four GPIOs belong to the display QSPI bus. The connector is therefore better regarded as a bus breakout/test connector unless the display is deliberately disabled.

### Battery connector

The board supports a single-cell 3.7 V lithium-polymer battery and contains a TP4054 charging circuit. The manufacturer material quotes approximately 290 mA actual charging current and a 500 mA device maximum. Verify polarity against the PCB `+` marking before connection. JST-style battery connectors from different vendors do not always use the same polarity.

### Speaker connector

The `SPEAKER` connector is driven through the onboard audio codec and amplifier. It is not a generic GPIO output. Confirm the speaker impedance from the board documentation or supplied speaker before substituting another unit.

---

## 4. Display subsystem

### Electrical/interface configuration

| Parameter | Value |
|---|---|
| Controller | ST77922 |
| Native orientation | Portrait |
| Resolution | 320 × 480 |
| Color depth | RGB565, 16 bits/pixel |
| Bus | QSPI/quad SPI |
| SPI host commonly used | `SPI2_HOST` |
| Proven QSPI clock | 40 MHz |
| Color order | RGB |
| Color inversion | Enabled in known working configurations |
| CS | GPIO10 |
| CLK | GPIO12 |
| D0-D3 | GPIO11, GPIO13, GPIO14, GPIO9 |
| TE | GPIO42 |
| Backlight | GPIO41 |
| Panel RESET | Not a separate GPIO; tied to board reset/CHIP_PU |
| D/C | Not separately used in the known QSPI configuration |

### Driver requirements

For ESP-IDF or Arduino based on Arduino-ESP32 3.x, a practical driver stack is:

1. `esp_lcd` QSPI panel I/O.
2. ST77922 panel initialization sequence/driver.
3. ST77922 capacitive-touch driver over I²C.
4. `esp_lvgl_port` or a project-specific LVGL display/touch port.
5. LEDC PWM for backlight control.

A matching community BSP, `ESP32S3_ST77922C_LVGL8`, contains:

- `esp_lcd_st77922.c/.h`
- touch driver
- LVGL 8 port
- board pin map
- TE synchronization
- backlight control
- rotation and locking helpers

The BSP reports Arduino-ESP32 3.x, LVGL 8.4, RGB565, and OPI PSRAM. For a new project, the pin map and initialization sequence remain useful even if the application is standardized on a newer LVGL release.

### ST77922 implementation details

Known working references report:

- QSPI at 40 MHz.
- Four-pixel-aligned draw windows.
- Native portrait operation is simplest.
- Landscape may require a full-frame canvas in PSRAM, depending on the graphics driver and rotation method.
- TE can be used to reduce visible tearing.
- After replacing firmware, a USB reset may reset the ESP32 without fully resetting the panel. If logs are normal but the screen remains black, disconnect power completely and reconnect it.

### Frame-buffer memory

A complete RGB565 frame needs:

```text
320 × 480 × 2 bytes = 307,200 bytes
```

Two full buffers need 614,400 bytes, so PSRAM is appropriate. Smaller line or partial buffers reduce memory use and latency but require more flush operations.

---

## 5. Touch subsystem

| Parameter | Value |
|---|---|
| Controller | Touch function integrated with ST77922/TDDI family |
| Interface | I²C |
| Address | `0x55` |
| SDA | GPIO38 |
| SCL | GPIO39 |
| Interrupt | GPIO47 |
| Reset | GPIO48 |
| Typical bus frequency | 400 kHz |

A basic I²C scan should normally find at least:

- `0x55`: touch
- `0x18`: ES8311 audio codec

If neither appears, first verify GPIO38/GPIO39, power, pull-ups, and that no external device is holding the bus low.

---

## 6. RGB status LED

The onboard RGB LED is reported as one **WS2812B-compatible addressable LED** on GPIO40. It therefore uses one data line rather than three independent PWM channels.

### Suitable drivers

- Arduino: `Adafruit_NeoPixel`, `FastLED`, or the Arduino-ESP32 addressable RGB API where available.
- ESP-IDF: `led_strip` component using RMT or SPI backend.

### Arduino example using Adafruit NeoPixel

```cpp
#include <Adafruit_NeoPixel.h>

static constexpr uint8_t RGB_LED_PIN = 40;
static constexpr uint8_t RGB_LED_COUNT = 1;

Adafruit_NeoPixel rgb(RGB_LED_COUNT, RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
    rgb.begin();
    rgb.setBrightness(32);   // 0..255; start conservatively

    rgb.setPixelColor(0, rgb.Color(255, 0, 0));
    rgb.show();
    delay(500);

    rgb.setPixelColor(0, rgb.Color(0, 255, 0));
    rgb.show();
    delay(500);

    rgb.setPixelColor(0, rgb.Color(0, 0, 255));
    rgb.show();
    delay(500);

    rgb.clear();
    rgb.show();
}

void loop() {
}
```

If red and green are exchanged, the LED byte order on that board revision differs. Change `NEO_GRB` to the measured order rather than changing application color values everywhere.

### ESP-IDF example using `led_strip`

```c
#include "led_strip.h"

#define RGB_LED_GPIO 40

led_strip_config_t strip_config = {
    .strip_gpio_num = RGB_LED_GPIO,
    .max_leds = 1,
    .led_model = LED_MODEL_WS2812,
    .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
};

led_strip_rmt_config_t rmt_config = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = 10 * 1000 * 1000,
    .mem_block_symbols = 0,
    .flags.with_dma = false,
};

led_strip_handle_t strip;
ESP_ERROR_CHECK(led_strip_new_rmt_device(
    &strip_config, &rmt_config, &strip));

ESP_ERROR_CHECK(led_strip_set_pixel(strip, 0, 0, 32, 0));
ESP_ERROR_CHECK(led_strip_refresh(strip));
```

### Practical notes

- Start at low brightness to reduce current peaks and glare.
- Call `show()` or `led_strip_refresh()` after changing a value.
- The LED can retain its last transmitted color until a new value or power cycle.
- GPIO40 should not be driven as ordinary PWM while the WS2812 driver is active.

---

## 7. Backlight control

The display backlight uses GPIO41 and is reported active high.

### Simple on/off

```cpp
static constexpr int TFT_BL = 41;

void setup() {
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
}
```

### Arduino-ESP32 3.x PWM example

```cpp
static constexpr int TFT_BL = 41;
static constexpr uint32_t BL_FREQ = 20000;
static constexpr uint8_t BL_RESOLUTION = 8;

void setup() {
    ledcAttach(TFT_BL, BL_FREQ, BL_RESOLUTION);
    ledcWrite(TFT_BL, 180);  // 0..255
}
```

Use a frequency above the visible flicker range. If a camera shows banding, try another PWM frequency. The manufacturer specifies six white backlight LEDs and approximately 120 mA backlight current; GPIO41 controls the backlight circuit rather than directly powering those LEDs.

---

## 8. microSD/TF subsystem

The slot is wired for 4-bit SDMMC/SDIO, not merely ordinary SPI.

| SD signal | GPIO |
|---|---:|
| CLK | 5 |
| CMD | 4 |
| D0 | 6 |
| D1 | 7 |
| D2 | 2 |
| D3 | 3 |

### Drivers

- ESP-IDF: `esp_vfs_fat`, `sdmmc_cmd`, and the SDMMC host driver.
- Arduino-ESP32: `SD_MMC.h`.

### Arduino skeleton

```cpp
#include <SD_MMC.h>

void setup() {
    Serial.begin(115200);

    // Pin assignment support depends on the installed Arduino-ESP32 version.
    SD_MMC.setPins(5, 4, 6, 7, 2, 3);  // CLK, CMD, D0, D1, D2, D3

    if (!SD_MMC.begin("/sdcard", false)) {  // false = request 4-bit mode
        Serial.println("SDMMC mount failed");
        return;
    }

    Serial.printf("Card size: %llu MB\n",
                  SD_MMC.cardSize() / (1024ULL * 1024ULL));
}

void loop() {
}
```

GPIO2 and GPIO3 participate in SD access, so avoid attaching unrelated circuitry that loads these lines.

---

## 9. Audio subsystem

### Hardware

| Function | Device/GPIO |
|---|---|
| Codec | ES8311 |
| Codec I²C address | normally `0x18` |
| I²C SDA/SCL | GPIO38 / GPIO39 |
| I²S DOUT | GPIO15 |
| I²S DIN | GPIO16 |
| I²S MCLK | GPIO17 |
| I²S BCLK | GPIO18 |
| I²S WS/LRCLK | GPIO21 |
| Amplifier enable | GPIO1, reported active low |
| Input | Onboard MEMS microphone |
| Output | External speaker connector through amplifier |

### Required drivers

- ESP-IDF I²C master driver for ES8311 register control.
- ESP-IDF I²S standard-mode driver.
- ES8311 codec driver or Espressif audio-codec component.
- GPIO control for amplifier enable.

The matching Xiaozhi board reference reports full-duplex input/output at 24 kHz. That is a known application configuration, not necessarily the only rate supported by the codec.

### Safe bring-up order

1. Keep the amplifier disabled.
2. Initialize I²C and verify the ES8311 responds at `0x18`.
3. Configure MCLK, BCLK, WS, DIN, and DOUT.
4. Initialize codec registers and start with low DAC volume.
5. Enable the amplifier using GPIO1.
6. Test a low-level tone or short PCM sample.
7. Bring up the microphone separately and inspect captured sample levels.

This order helps avoid startup pops and unexpectedly loud output.

---

## 10. Battery and power

| Item | Information |
|---|---|
| Board input | 5 V through USB-C |
| Battery type | Single-cell 3.7 V LiPo |
| Charger | TP4054 |
| Battery measurement | GPIO8 / ADC1 channel 7 through an onboard divider (ratio unverified) |
| Manufacturer-stated actual charge current | approximately 290 mA |
| Manufacturer-stated full-system current example | approximately 401 mA with display, speaker, and charging active |

The firmware samples GPIO8 on the ES3C35P only, using ESP-IDF ADC oneshot at 12 dB attenuation and curve-fitting/eFuse ADC calibration when available. It takes eight readings and filters them in a low-priority task at 10-second intervals. Raw ADC counts remain available without ADC calibration; calibrated pin millivolts require the ADC calibration scheme. The P4 profile reports battery measurement as unsupported and never configures the S3 ADC pin.

The divider ratio is still unverified: no matching schematic or manufacturer value has been confirmed. Battery voltage therefore stays **unknown** until an explicit one-point calibration is saved from a multimeter reading across the battery connector. That calibration is an empirical scale factor, not proof of the resistor values or board revision. The on-device settings and web settings accept a measured reference from 2500–4500 mV; the web diagnostics also expose raw ADC counts, calibrated ADC millivolts, and the estimated battery voltage. Never connect a battery directly to GPIO8.

After divider calibration, the UI shows a **rough** percentage based on a generic single-cell Li-ion/LiPo resting-voltage curve. The implementation uses interpolated points from about 3.0 V (0%) to a configurable full point (default 4.2 V, 3.9–4.3 V allowed), with most percentage change across the broad 3.65–3.8 V plateau and only a small estimated change from 4.2 V to 4.0 V (100% to about 97%). This follows the broad shape in [Ufine's typical Li-ion discharge curve](https://www.ufinebattery.com/blog/lithium-ion-battery-voltage-chart/); the chart is illustrative, not a per-cell characterization. The web settings can adjust that battery-specific full point separately from the ADC divider scale; use the cell's specified full voltage, and leave the default if unknown. The estimate is not a capacity measurement: cell chemistry, age, temperature, recent charging, audio load, and board cutoff behavior all change the voltage-to-capacity relationship, and voltage alone cannot accurately resolve state of charge across a flat plateau. Indicator colors are green above 30%, orange at 10–30%, and red below 10%; below 5% it blinks and plays a short click every 30 seconds when sound is enabled. No low-battery shutdown is performed.

The TP4054 is listed in board material, but the firmware has no verified charge-status input. A rising battery voltage can also result from audio/load changes, and a reading above 4.15 V is not proof of active charging. The UI therefore does **not** show a charging/USB icon or infer charging from voltage trends.

For manual validation, compare the displayed raw ADC and calibrated ADC pin voltage with a meter/known voltage, then calibrate against the voltage measured at BAT+ and BAT−. Repeat with audio idle and playing, and with USB disconnected/connected. Check the reading over several samples and after a reboot. The divider, absolute accuracy, and stability under load have **not** been physically verified. Never connect a battery directly to GPIO8 or attach a battery solely by connector shape; confirm polarity first.

---

## 11. Recommended software options

### Option A: ESP-IDF

Best when full control, current ESP-IDF APIs, audio, SDMMC, and maintainable component separation are priorities.

Suggested components:

- `esp_lcd`
- ST77922 panel component/driver
- ST77922 touch component
- `lvgl`
- `esp_lvgl_port`
- `led_strip`
- `driver/ledc`
- `driver/i2c_master`
- `driver/i2s_std`
- `sdmmc_cmd`
- `esp_vfs_fat`
- ES8311 codec component

### Option B: Arduino-ESP32 3.x

Fastest route to a working prototype. Use `ESP32S3 Dev Module` unless a matching board definition is installed.

Recommended board settings:

- 16 MB flash
- OPI PSRAM enabled
- USB CDC on boot enabled
- RGB565/LVGL color depth 16
- partition scheme selected to leave enough application and filesystem space
  (ESP-IDF build from v0.12.0: layout B1 = 2 × 5 MB app + 5.9 MB FFat, see README)

Potential libraries:

- Matching ST77922/LVGL BSP
- LVGL
- Arduino_GFX only when backed by a verified ST77922 QSPI configuration
- `Adafruit_NeoPixel` or `FastLED`
- `SD_MMC`
- ES8311 driver/library

### Option C: ESPHome

A community hardware reference reports a working ST77922 panel and touch configuration on recent ESPHome versions. This can be useful for Home Assistant style devices, but it is less appropriate than ESP-IDF/Arduino for a general-purpose LVGL development platform.

---

## 12. Recommended bring-up sequence

1. Select an ESP32-S3 N16R8 configuration and enable OPI PSRAM.
2. Start USB serial logging; enable USB CDC on boot when using Arduino.
3. Print flash and PSRAM size and run a small PSRAM allocation test.
4. Scan I²C on GPIO38/GPIO39; expect `0x55` and commonly `0x18`.
5. Test WS2812 RGB LED on GPIO40 at low brightness.
6. Set GPIO41 high and test backlight PWM.
7. Initialize ST77922 in native portrait at a conservative bus speed, then move to the proven 40 MHz configuration.
8. Draw solid red, green, blue, white, and black screens to verify channel order and inversion.
9. Read raw touch coordinates, then verify orientation mapping.
10. Add LVGL with RGB565 and a partial PSRAM draw buffer.
11. Increase buffer size or enable full-frame buffering if landscape rotation requires it.
12. Mount microSD in 4-bit mode and perform a write/read test.
13. Initialize ES8311 while the speaker amplifier remains disabled.
14. Test low-volume playback, then microphone capture.
15. Read raw battery ADC only on a verified S3 profile; show voltage only after ADC and one-point divider calibration, and verify the actual divider before treating it as hardware-confirmed.

---

## 13. Minimal diagnostic checklist

### Screen remains black

- Confirm GPIO41 is high or PWM is non-zero.
- Confirm OPI PSRAM is enabled if the driver allocates its buffer there.
- Verify QSPI pins and CS.
- Check that color inversion and panel initialization match ST77922.
- Disconnect USB and battery fully, then reconnect. The LCD reset is tied to board reset and may not receive a complete reset from every USB software reset.

### Touch not detected

- Scan GPIO38/GPIO39.
- Check for address `0x55`.
- Verify GPIO48 reset and GPIO47 interrupt.
- Check that another I²C device is not holding SDA or SCL low.

### RGB LED does not light

- Confirm GPIO40.
- Use WS2812/NeoPixel signaling, not ordinary `digitalWrite()` or PWM.
- Try GRB ordering first.
- Call the driver's refresh/show function.

### Audio is silent

- Confirm ES8311 at `0x18`.
- Verify all five I²S-related pins.
- Check GPIO1 amplifier-enable polarity.
- Start with a known PCM format and conservative sample rate/volume.
- Confirm speaker and connector polarity/impedance.

### SD card does not mount

- Confirm 4-bit pin map.
- Try a known good FAT32 card.
- Temporarily test 1-bit mode to isolate D1-D3 problems.
- Verify that external connections are not loading GPIO2-GPIO7.

---

## 14. Known uncertainties to verify on the physical board

- Exact Heemol product/revision number is not printed visibly in the supplied photographs.
- Exact UART GPIO assignment is not legible/confirmed from the photographs.
- Exact GPIO45/GPIO46 header order should be checked against continuity or schematic.
- Speaker amplifier may be FM8002E or a board-compatible variant depending on revision.
- Battery ADC divider ratio is not yet confirmed.
- WS2812 color order should be tested; GRB is the likely starting point.
- Connector pitch and mating-series identification should be measured before ordering cables.

---

## 15. Source material

- [LCDWIKI product page](https://www.lcdwiki.com/3.5inch_ESP32-S3_Display)
- [LCDWIKI ES3C35P user manual PDF](https://www.lcdwiki.com/res/ES3C35P/3.5inch_IPS_ESP32-S3_User_Manual.pdf)
- [LCDWIKI ES3C35P specification PDF](https://www.lcdwiki.com/res/ES3C35P/3.5inch_IPS_ESP32-S3_Specification_V1.0.pdf)
- [Sitronix ST77922 datasheet hosted by Espressif](https://dl.espressif.com/AE/esp-iot-solution/ST77922_SPEC_V0.1.pdf)
- [LCD Wizard ES3C35P board reference](https://lcdwizard.com/boards/lcdwiki-es3c35p)
- [Community ES3C35P hardware reference](https://github.com/jlmeredith/ES3C35P)
- [Arduino ST77922C/LVGL 8 BSP](https://github.com/peter58228/ESP32S3_ST77922C_LVGL8)
- [Xiaozhi ES3C35P board reference](https://github.com/dujingtao/esp32xiaozhi/blob/main/esp32-firmware/main/boards/lcdwiki-es3c35p/README.md)
- [Machine-readable ES3C35P pinout repository](https://github.com/lildev1979/esp32-display-pinouts/tree/main/boards/lcdwiki-es3c35p)

---

## 16. Suggested project structure

```text
project/
├── CMakeLists.txt or platformio.ini
├── sdkconfig.defaults or board build settings
├── main/ or src/
│   ├── main.cpp
│   ├── board_pins.h
│   ├── board_display.cpp
│   ├── board_touch.cpp
│   ├── board_backlight.cpp
│   ├── board_rgb.cpp
│   ├── board_sd.cpp
│   ├── board_audio.cpp
│   └── board_power.cpp
├── components/ or lib/
│   ├── st77922/
│   ├── touch/
│   └── es8311/
└── README.md
```

Keeping the board-support functions separate from the application and LVGL screens makes it easier to reuse the same UI on the ESP32-S3 board, ESP32-P4 experiments, and later custom hardware.
