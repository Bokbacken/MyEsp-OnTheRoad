# Waveshare 4.3-inch ESP32-P4 Wi-Fi 6 Touch Display

## Hardware reference, pinout, drivers, and bring-up notes

**Document status:** Working hardware reference based on the official Waveshare product documentation, the Waveshare GitHub board repository, and matching community/ESP-IDF material. Structured to match the existing Heemol 3.5-inch ESP32-S3 reference.
**Board marking / SKU:** `ESP32-P4-WIFI6-Touch-LCD-4.3` — Waveshare SKU 33874 (standard) / 33875 (`-C`, with OV5647 camera).
**Reference design:** Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 (official first-party board).

> **Confidence note:** The SoC (ESP32-P4NRW32), the ESP32-C6-MINI-1 Wi-Fi 6 co-processor, the 4.3-inch 480 × 800 IPS MIPI-DSI panel with ST7701 driver, the GT911 touch controller, the ES8311 + ES7210 audio chain, the microSD (SDIO), and the full 40-pin header pinout are all taken directly from Waveshare's own documentation and repository and are high-confidence. Internal peripheral GPIO assignments that are **not** printed on the 40-pin header (display MIPI lanes, touch I²C, audio I²S, SD SDIO) come from the board BSP / factory firmware and community sources — treat those as highly likely but confirm against the official schematic (`schematic/ESP32-P4-WIFI6-Touch-LCD-4.3-schematic.pdf`) before connecting external hardware. This is a very different architecture from the 3.5-inch board: the display is MIPI-DSI (not QSPI) and the radio is a separate hosted co-processor (not on-die).

---

## 1. Hardware overview

| Function | Identified hardware |
|---|---|
| Main MCU | Espressif ESP32-P4 (ESP32-P4NRW32), RISC-V: dual-core HP CPU + single-core LP CPU |
| Wireless | ESP32-C6-MINI-1 module as co-processor over SDIO — 2.4 GHz Wi-Fi 6 + Bluetooth 5 (LE) |
| Flash | 32 MB external NOR flash |
| PSRAM | 32 MB PSRAM stacked in the chip package |
| Internal memory | 128 KB HP ROM, 16 KB LP ROM, 768 KB HP L2MEM, 32 KB LP SRAM, 8 KB TCM |
| Display | 4.3-inch IPS TFT, native portrait 480 × 800 |
| Pixel format | RGB (true-color); 500 cd/m², 1000:1 contrast, 160° viewing angle |
| Display driver IC | Sitronix ST7701 |
| Display interface | MIPI-DSI, 2-lane |
| Touch controller | Goodix GT911, capacitive, up to 5 points, over I²C |
| Audio codec | Everest Semiconductor ES8311 (I²C control + I²S audio) |
| Audio front end | Everest Semiconductor ES7210 echo-cancellation / ADC |
| Microphone | Dual onboard MEMS microphone array |
| Speaker | GH 1.25 2-pin locking connector, 8 Ω 2 W recommended |
| Camera | MIPI-CSI, 15-pin 0.5 mm pitch, 2-lane; optional OV5647 (on `-C` SKU) |
| Storage | microSD/TF slot, 4-line SDIO 3.0 |
| USB | USB 2.0 High-Speed OTG (Type-C) + USB-to-UART (Type-C) |
| Battery | 3.7 V LiPo via MX1.25 2-pin connector, onboard charge/discharge |
| RTC battery | Dedicated RTC battery header (rechargeable RTC cells only) |
| Controls | POWER (long-press 2 s off / short-press on), BOOT, RESET buttons |
| Expansion | 40-pin 2.54 mm header, partly Raspberry Pi HAT-compatible (via adapter) |
| Security | Secure Boot, flash encryption, HW crypto accelerator, TRNG, APM / privilege separation |
| Media acceleration | JPEG codec, Pixel Processing Accelerator (PPA), ISP, H.264 video encoder |

### Visible board features (from the hardware description)

- ESP32-P4-Core area (ESP32-P4NRW32 + 32 MB NOR flash).
- ESP32-C6-MINI-1 module (Wi-Fi 6 / BLE 5 co-processor over SDIO).
- ES7210 echo-cancellation chip and ES8311 audio codec.
- MIPI-CSI camera connector (15-pin, 0.5 mm pitch).
- 2.54 mm 4-pin pads for flashing the ESP32-C6 module directly.
- RTC battery header.
- POWER, BOOT, and RESET buttons.
- microSD/TF card slot (SDIO 3.0).
- MIPI-DSI LCD flex connector.
- Speaker header (GH 1.25, 2-pin, locking).
- Two Type-C ports: one USB-to-UART (power / program / debug), one USB OTG 2.0 HS.
- Dual onboard microphones.
- PWR LED indicator.
- MX1.25 2-pin lithium battery header.
- 40-pin 2.54 mm expansion header.

---

## 2. 40-pin expansion header pinout

This is the one connector whose pin allocation is printed and officially documented. Read with the board silkscreen; the official diagram corresponds to the silkscreen rotated 90° counter-clockwise. Physical pin numbering is the standard Raspberry Pi style (odd column on one side, even on the other).

| Pin | Signal | Type | | Pin | Signal | Type |
|---:|---|---|---|---:|---|---|
| 1 | 5V | Power | | 2 | 3V3 | Power |
| 3 | 5V | Power | | 4 | SDA (GPIO7) | I²C |
| 5 | GND | Ground | | 6 | SCL (GPIO8) | I²C |
| 7 | GPIO37 | GPIO | | 8 | GPIO2 | GPIO |
| 9 | GPIO38 | GPIO | | 10 | GND | Ground |
| 11 | GPIO5 | GPIO | | 12 | GPIO3 | GPIO |
| 13 | GND | Ground | | 14 | GPIO4 | GPIO |
| 15 | GPIO21 | GPIO | | 16 | GPIO28 | GPIO |
| 17 | GPIO22 | GPIO | | 18 | 3V3 | Power |
| 19 | GND | Ground | | 20 | GPIO29 | GPIO |
| 21 | USB1P1_N (GPIO24) | USB/GPIO | | 22 | GPIO30 | GPIO |
| 23 | USB1P1_P (GPIO25) | USB/GPIO | | 24 | GPIO31 | GPIO |
| 25 | USBD_N | USB | | 26 | GND | Ground |
| 27 | USBD_P | USB | | 28 | GPIO34 | GPIO |
| 29 | GND | Ground | | 30 | GPIO35 | GPIO |
| 31 | GPIO32 | GPIO | | 32 | GPIO49 | GPIO |
| 33 | GND | Ground | | 34 | GPIO50 | GPIO |
| 35 | GPIO46 | GPIO | | 36 | GPIO51 | GPIO |
| 37 | GPIO47 | GPIO | | 38 | GPIO52 | GPIO |
| 39 | GND | Ground | | 40 | GPIO48 | GPIO |

### Important header warnings

- **Pins 25 / 27 (USBD_N / USBD_P)** are the ESP32-P4's dedicated USB 2.0 High-Speed OTG lines (chip pins 49/50). They are **not** on the GPIO matrix, have no GPIO number, and cannot be used as ordinary I/O.
- **Pins 21 / 23 (USB1P1_N / USB1P1_P = GPIO24 / GPIO25)** provide USB Full-Speed functions. When USB is not used they can serve as regular GPIO.
- **SDA/SCL on pins 4/6 (GPIO7/GPIO8)** are the shared I²C bus — the GT911 touch controller and other peripherals also live on this bus (see §5). Check for address conflicts before adding external I²C devices.
- The board is only *partly* Raspberry Pi HAT-compatible and needs a pin-header adapter. Do not assume a HAT's power/signal expectations match without checking.

---

## 3. Connectors on the board

Always confirm connector orientation and continuity against the silkscreen and schematic before attaching a cable. ESP32-P4 GPIO is 3.3 V logic and is not 5 V tolerant.

### Two USB Type-C ports

- **USB-to-UART (Type-C):** power supply, firmware programming, and serial debugging of the ESP32-P4. This is the normal "flash + monitor" port.
- **USB OTG (Type-C):** native ESP32-P4 USB 2.0 High-Speed OTG (on the dedicated USBD_N/USBD_P lines). Used for host/device USB applications such as the USB extended-screen example.

### I²C (40-pin header, pins 4/6)

```text
SDA = GPIO7
SCL = GPIO8
```

Shared bus. GT911 touch and other peripherals sit on it; scan before adding devices.

### Speaker header

GH 1.25 2-pin connector with lock. Driven through the ES8311 codec and the onboard amplifier — **not** a generic GPIO output. Recommended speaker: 8 Ω, 2 W. Confirm impedance before substituting another unit.

### Battery header (main)

MX1.25 2-pin connector for a single-cell 3.7 V LiPo, with onboard charge/discharge. Verify polarity against the PCB marking before connecting — MX1.25 battery leads from different vendors do not always share polarity.

### RTC battery header

Separate header for a **rechargeable** RTC backup cell only. Do not fit a non-rechargeable primary cell here.

The board schematic connects this header to the P4's **ESP_VBAT**, not to
an external I²C calendar RTC. ESP-IDF's VBAT example requires controlled
deep sleep to switch to backup power and retain the RTC timer; do not
assume calendar retention after abrupt main-power loss.
See [Espressif's VBAT example](https://github.com/espressif/esp-idf/tree/v6.1/examples/lowpower/vbat).

### microSD power switch

The schematic's MicroSD section has Q1 (AO3401) switching card VDD from
ESP_3V3, controlled by **GPIO45 (active low)**. Internal LDO4 supplies the
SD IO bank/pullups; enabling LDO4 alone is not explicit card-VDD control.
The card uses SDMMC slot 0 (CLK43/CMD44/D0–D3 = 39/40/41/42), separate
from the C6 radio's SDIO slot 1.

### MIPI-DSI display connector

Flex connector carrying the 2-lane MIPI-DSI link to the ST7701 panel (see §4). Not a GPIO breakout.

### MIPI-CSI camera connector

15-pin, 0.5 mm pitch, 2-lane MIPI-CSI with an integrated ISP in the ESP32-P4. Supports OV5647 and compatible cameras (fitted on the `-C` SKU).

### ESP32-C6 flashing pads

2.54 mm 4-pin pads dedicated to flashing firmware directly to the ESP32-C6 wireless module (separate from the main ESP32-P4 UART port).

---

## 4. Display subsystem

### Electrical / interface configuration

| Parameter | Value |
|---|---|
| Controller | ST7701 |
| Native orientation | Portrait |
| Resolution | 480 × 800 |
| Panel type | IPS TFT, capacitive touch |
| Brightness | 500 cd/m² |
| Contrast | 1000:1 |
| Viewing angle | 160° |
| Bus | MIPI-DSI, 2-lane |
| Data-lane rate (factory firmware) | 500 Mbps per lane |
| DPI/pixel clock (factory firmware) | 30 MHz |
| Backlight | Dedicated backlight circuit (brightness configurable in firmware) |

> **Architecture note:** Unlike the 3.5-inch ST77922 board, this panel is driven over **MIPI-DSI through the ESP32-P4's dedicated DSI PHY**, not over a QSPI GPIO bus. There is therefore no "LCD CS/CLK/D0–D3" GPIO map to document — the DSI lanes are hard-wired peripheral pins. Configure the panel through the ESP-IDF MIPI-DSI driver and the Waveshare BSP, not through bit-banged SPI.

### Driver requirements

For ESP-IDF (the recommended path on ESP32-P4), a practical driver stack is:

1. `esp_lcd` MIPI-DSI panel I/O (DSI PHY + DPI panel).
2. ST7701 panel initialization sequence / driver.
3. GT911 capacitive-touch driver over I²C.
4. `esp_lvgl_port` or a project-specific LVGL display/touch port (examples use LVGL 9).
5. Backlight brightness control.

The official Waveshare component/BSP to target is the published registry release `waveshare/esp32_p4_wifi6_touch_lcd_4_3` (the repo's examples 06–12 pin version `1.0.1`). It carries the correct ST7701 timing, GT911 address probing, and board pin map for current silicon revisions.

### ESP32-P4 silicon revision profiles (important)

The BSP and ESP-IDF defaults are revision-sensitive — picking the wrong profile is a common bring-up failure:

- **Default `rev3_x` profile:** requires `CONFIG_ESP32P4_REV_MIN_300`, uses **250 MHz PSRAM**.
- **Rev 1.3 boards:** use the `config/ci/rev1_3.defaults` overlay — `CONFIG_ESP32P4_REV_MIN_100`, **200 MHz PSRAM**.
- **Arduino-ESP32 `3.3.11`:** `ChipVariant=postv3` resolves to `CONFIG_ESP32P4_REV_MIN_301`, **200 MHz PSRAM**; requires P4 **Rev 3.1 or newer** and does not support Rev 3.0.
- Leave MIPI PHY clock selection to ESP-IDF's revision-aware default; do **not** copy a clock-source setting between profiles.

### Frame-buffer memory

A full 480 × 800 frame at 16 bpp (RGB565) needs:

```text
480 × 800 × 2 bytes = 768,000 bytes
```

Double buffering needs 1,536,000 bytes, which is comfortable in the 32 MB in-package PSRAM. Higher color depth or extra working canvases simply cost more PSRAM; memory is not the constraint it is on the smaller board.

---

## 5. Touch subsystem

| Parameter | Value |
|---|---|
| Controller | Goodix GT911 |
| Touch points | Up to 5 |
| Interface | I²C |
| I²C address | `0x5D`, falling back to `0x14` (probed in that order) |
| SDA | GPIO7 |
| SCL | GPIO8 |
| Reset | GPIO23 (used by factory firmware) |
| Interrupt | Not driven by the current registry BSP (no-INT configuration); factory firmware also runs without INT |

GT911 address handling differs by firmware generation: the **factory image** polls after probing `0x5D` then `0x14` with reset on GPIO23 and no interrupt; the **registry BSP examples** drive neither GT911 INT nor RST and rely on address probing. If touch is not detected, verify the I²C bus (GPIO7/GPIO8), confirm which address the panel answers at, and check the reset-line assumption your firmware is making.

A basic I²C scan on GPIO7/GPIO8 should normally find:

- `0x5D` or `0x14`: GT911 touch
- `0x18`: ES8311 audio codec (typical)
- ES7210 audio front end (typical address `0x40`, confirm against schematic)

---

## 6. Status / power LED

The board has a **PWR LED power indicator** only. Unlike the 3.5-inch board, there is **no documented onboard WS2812B addressable RGB status LED**. If an addressable-LED status indicator is needed, drive one from a free header GPIO (see §2) using the usual `led_strip` (ESP-IDF, RMT/SPI backend) or NeoPixel/FastLED (Arduino) approach, and confirm the chosen pin is not reserved for a peripheral.

---

## 7. Backlight control

The 4.3-inch panel has a dedicated backlight circuit with 500 cd/m² rated brightness. Brightness is controlled through the board BSP / firmware rather than a documented single "backlight GPIO" on the header. Use the Waveshare BSP's brightness API (or the ESP-IDF backlight control it wraps) instead of bit-banging a pin, and confirm the actual control line against the schematic if you need direct PWM control.

---

## 8. microSD / TF subsystem

The slot uses a 4-line SDIO 3.0 interface (SDMMC), not plain SPI.

| SD signal | GPIO |
|---|---:|
| CLK | 43 |
| CMD | 44 |
| D0 | 39 |
| D1 | 40 |
| D2 | 41 |
| D3 | 42 |

> These SDIO pin assignments come from community/ESP-IDF sources, not the official header diagram — confirm against the schematic before relying on them. The official ESP-IDF example to follow is `05_sdmmc`.

### Drivers

- ESP-IDF: `esp_vfs_fat`, `sdmmc_cmd`, and the SDMMC host driver (4-bit mode).
- The board's own `05_sdmmc` example is the authoritative wiring reference.

---

## 9. Audio subsystem

### Hardware

| Function | Device |
|---|---|
| Codec | ES8311 (playback + control) |
| Codec I²C address | normally `0x18` |
| Echo-cancellation / ADC front end | ES7210 (confirm address, typically `0x40`) |
| Control bus | I²C shared bus (GPIO7 SDA / GPIO8 SCL) |
| Audio data | I²S (exact I²S GPIOs via BSP/schematic) |
| Input | Dual onboard MEMS microphone array (through ES7210) |
| Output | Speaker header via codec + amplifier (8 Ω 2 W recommended) |

> The exact I²S GPIO assignments (MCLK/BCLK/WS/DOUT/DIN) are managed by the Waveshare BSP and are not printed on the header. Use the board BSP and the `06_I2SCodec` example rather than hard-coding I²S pins; confirm against the schematic if you must set them manually.

### Required drivers

- ESP-IDF I²C master driver for ES8311 / ES7210 register control.
- ESP-IDF I²S standard-mode driver.
- Espressif audio-codec component (ES8311 + ES7210) or the Waveshare BSP audio layer.

### Safe bring-up order

1. Keep the output amplifier / volume low.
2. Initialize I²C and verify ES8311 (`0x18`) and ES7210 respond.
3. Configure the I²S clocks and data lines through the BSP.
4. Initialize codec registers; start with low DAC volume.
5. Test a low-level tone or short PCM sample.
6. Bring up the dual microphones / ES7210 capture separately and inspect sample levels.

This order helps avoid startup pops and unexpectedly loud output.

---

## 10. Wireless subsystem (ESP32-C6 co-processor)

Unlike the ESP32-S3 board, the ESP32-P4 has **no radio of its own**. Wi-Fi 6 and Bluetooth 5 (LE) are provided by an **onboard ESP32-C6-MINI-1 module** acting as a hosted co-processor over SDIO.

| Item | Information |
|---|---|
| Co-processor | ESP32-C6-MINI-1 |
| Host link | SDIO |
| Radio | 2.4 GHz Wi-Fi 6, Bluetooth 5 (LE) |
| Flashing | Separate 2.54 mm 4-pin pads for the C6 module |

### Required software components

On ESP-IDF, Wi-Fi/BT on the ESP32-P4 is driven **remotely** through the C6:

- `espressif/esp_hosted` — host/co-processor transport and OTA RPC.
- With ESP-IDF 6.1+, `esp_wifi` routes through ESP-Hosted; older ESP-IDF
  versions may also need `espressif/esp_wifi_remote`.

The C6 runs its own "slave"/hosted firmware. Keep the P4 host firmware and the C6 slave firmware versions compatible — a mismatch is a common cause of Wi-Fi not coming up. Official examples: `04_wifistation` (station via the hosted link). The factory image also bundles ESP-Hosted Wi-Fi.

The application CI builds `host_fw.bin` from the pinned Espressif
ESP-Hosted MCU coprocessor-OTA example. On a working P4 Host-radio connection,
upload that C6 app image under **Uppdatera → ESP32-C6 Host-radio (OTA)**.
The uploader checks the ESP32-C6 image header and queries the running C6
firmware before using the ESP-Hosted OTA RPCs. If the installed firmware
cannot report its version or is older than v2.6.0, OTA still proceeds but
skips the explicit activation RPC; older slave firmware reboots after the
OTA end/validation step. The first update can therefore be a bootstrap step
before later updates use explicit activation. The C6 OTA app uses two
`0x1C0000` app slots; the upload is size-limited accordingly. After activation
or the legacy auto-reboot, the P4 restarts to reconnect to the C6.

If the legacy firmware cannot start or complete the OTA RPCs, recovery requires
the dedicated 4-pin C6 flashing pads and an ESP32-C6 serial flashing setup; use
the board schematic to verify pad order and voltage rather than assuming the
P4 USB-UART connector is wired to the C6.

---

## 11. Battery and power

| Item | Information |
|---|---|
| Board input | 5 V through either Type-C port |
| Main battery | Single-cell 3.7 V LiPo via MX1.25 2-pin header, onboard charge/discharge |
| Charger IC | ETA6098 (U19) |
| Battery voltage sense | `BAT_ADC` on **GPIO20** (ADC1 channel) |
| Divider | R12 = 200 kΩ, R15 = 100 kΩ → V_adc ≈ V_bat / 3 |
| Charge-status (STAT) | Routed only to test pad **TP1** — **not** connected to any GPIO |
| RTC battery | Separate header, rechargeable RTC cells only |
| Power control | POWER button: long-press 2 s to power off, short-press to power on |

### Battery voltage monitoring (ADC)

Battery voltage is sensed through a resistor divider: `BAT` → R12 (200 kΩ) / R15 (100 kΩ) → `BAT_ADC` on **GPIO20**, which is an **ADC1** channel on the ESP32-P4. The divider scales the battery to roughly one third, so the ADC pin stays well inside its safe input range:

```text
V_adc ≈ V_bat / 3
  4.2 V (full)      → ~1.40 V at the pin
  3.3 V (low)       → ~1.10 V at the pin
  < ~2.5 V actual   → battery likely disconnected/missing (divider pulled toward GND)
```

Practical reading (ESP-IDF):

1. Use the **calibrated ADC1 oneshot API** on GPIO20.
2. Average / smooth several samples (the rail is noisy under display + radio load).
3. **Multiply the calibrated millivolt reading by 3** to recover actual battery millivolts.

> **Source confidence:** The `BAT_ADC` = GPIO20 assignment, the R12/R15 divider values, and the ETA6098 charger come from the board schematic as confirmed by community work, not from the official pinout *page* (GPIO20 is an internal signal and is not exposed on the 40-pin header). Confirm against `schematic/ESP32-P4-WIFI6-Touch-LCD-4.3-schematic.pdf` before relying on it.

**Charge-status caveat:** the ETA6098's `STAT` output only reaches test pad **TP1**; it is not wired to any GPIO. You cannot read "charging / charged" in firmware without a hardware bodge from TP1 to a spare input — infer state from the voltage trend instead.

Verify battery polarity against the PCB marking before connecting — do not rely on connector shape alone. The ESP32-P4 is a markedly more power-hungry SoC than the ESP32-S3, especially with the MIPI display, camera, and C6 radio active; budget power and battery capacity accordingly.

---

## 12. Recommended software options

### Option A: ESP-IDF (recommended)

Waveshare explicitly recommends ESP-IDF for the ESP32-P4 at this stage, because Arduino support for the P4 is still limited. ESP-IDF gives full control of the MIPI-DSI display, camera ISP, hosted Wi-Fi, audio, and SDMMC. ESP-IDF **v5.3.1 or later** is recommended (the factory image is built with v5.5.5).

Suggested components:

- `esp_lcd` + MIPI-DSI panel support
- ST7701 panel component
- GT911 touch component
- `lvgl` + `esp_lvgl_port` (examples use LVGL 9)
- `espressif/esp_hosted` + `espressif/esp_wifi_remote`
- `sdmmc_cmd`, `esp_vfs_fat`
- ES8311 + ES7210 audio-codec components
- The Waveshare BSP `waveshare/esp32_p4_wifi6_touch_lcd_4_3` (ties the above together with correct pins/timing)

### Option B: Arduino-ESP32 3.3.11+

Faster for simple prototypes, but P4 Arduino support is limited and **requires P4 Rev 3.1 or newer** (`ChipVariant=postv3`, 200 MHz PSRAM). Rev 3.0 is not supported. Prefer ESP-IDF unless a specific Arduino library is required.

### Option C: Factory / Brookesia firmware

The board ships with a Brookesia launcher factory image (ESP-Hosted Wi-Fi, camera, audio, multimedia, settings, and Xiaozhi apps). Recovery images are provided in the repo. Useful as a known-good baseline and for confirming hardware before custom development.

---

## 13. Recommended bring-up sequence

1. Confirm the P4 silicon revision and select the matching ESP-IDF profile (`rev3_x` vs `rev1_3`) and PSRAM speed.
2. Start USB serial logging over the USB-to-UART Type-C port; build and flash `02_HelloWorld`.
3. Print flash and PSRAM size and run a small PSRAM allocation test.
4. Run `03_i2c_tools` and scan I²C on GPIO7/GPIO8; expect GT911 (`0x5D` or `0x14`) and ES8311 (`0x18`).
5. Bring up the display with `07_Displaycolorbar` (ST7701 over MIPI-DSI); verify color bars and panel timing.
6. Read raw GT911 touch coordinates, then verify orientation mapping against the portrait panel.
7. Add LVGL 9 (`08_lvgl_demo_v9`) with a PSRAM draw buffer.
8. Mount microSD in 4-bit SDMMC mode (`05_sdmmc`) and perform a write/read test.
9. Bring up audio with `06_I2SCodec`: ES8311 playback and ES7210 microphone loopback at low volume.
10. Bring up hosted Wi-Fi with `04_wifistation` (verify the ESP32-C6 slave firmware is compatible).
11. (Optional, `-C` SKU) Bring up the OV5647 camera over MIPI-CSI (`09_video_lcd_display`).
12. Only then integrate the application and any 40-pin header peripherals.

---

## 14. Minimal diagnostic checklist

### Screen remains black

- Confirm the ESP-IDF revision profile and PSRAM speed match the physical board revision.
- Confirm the MIPI-DSI driver and ST7701 init are from the correct BSP version (`1.0.1`+).
- Do not override the MIPI PHY clock between revision profiles.
- Check backlight is enabled via the BSP.

### Touch not detected

- Scan I²C on GPIO7/GPIO8.
- Check both `0x5D` and `0x14`.
- Confirm whether your firmware expects GT911 RST on GPIO23 and whether it drives INT at all (registry BSP does not).

### Wi-Fi / Bluetooth does not work

- Confirm `esp_hosted` + `esp_wifi_remote` are present and configured.
- Verify the ESP32-C6 slave firmware version is compatible with the P4 host firmware.
- Re-flash the C6 via its dedicated 4-pin pads if the hosted link never initializes.

### Audio is silent

- Confirm ES8311 at `0x18` and the ES7210 front end respond on I²C.
- Use the `06_I2SCodec` example wiring rather than hand-set I²S pins.
- Start with a known PCM format and conservative volume.
- Confirm speaker impedance (8 Ω 2 W) and connector.

### SD card does not mount

- Confirm the 4-line SDIO pin map against the schematic / `05_sdmmc`.
- Try a known-good FAT32 card.
- Temporarily test 1-bit mode to isolate D1–D3 problems.

---

## 15. Known uncertainties to verify on the physical board

- Exact P4 silicon revision (Rev 1.3 / 3.0 / 3.1) — determines ESP-IDF profile and PSRAM speed. Check before first flash.
- Internal GPIO assignments for the SD SDIO lines, audio I²S lines, and backlight control are from BSP/community sources — confirm against `schematic/ESP32-P4-WIFI6-Touch-LCD-4.3-schematic.pdf`.
- ES7210 I²C address (quoted here as `0x40`) should be confirmed by scan.
- Battery sense on GPIO20 (ADC1), the R12/R15 divider, and the ETA6098 charger are schematic/community-confirmed, not from the official pinout page — verify against the schematic.
- Whether your target firmware drives GT911 RST/INT (factory image vs registry BSP differ).
- ESP32-C6 slave-firmware version compatibility with the chosen P4 host firmware.
- 40-pin header is only partly HAT-compatible; confirm a given HAT's power/signal needs before fitting an adapter.

---

## 16. Source material

- [Waveshare product documentation (primary source)](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4.3)
- [Waveshare ESP-IDF guide for this board](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4.3/ESP-IDF)
- [Waveshare resources and schematic index](https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4.3/Resources-And-Documents)
- [Waveshare product page](https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-4.3.htm)
- [Official Waveshare GitHub board repository](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3)
- [Waveshare ESP32-P4 BSP components](https://github.com/waveshareteam/Waveshare-ESP32-components)
- [Community wiki (Spotpear) with pin notes](https://spotpear.com/wiki/ESP32-P4-4.3-inch-LCD-Display-TouchScreen-WIFI6-OV5647.html)
- Espressif ESP32-P4, `esp_hosted`, and `esp_wifi_remote` component documentation.

---

## 17. Key differences vs the Heemol 3.5-inch ESP32-S3 board

This board is **not** a drop-in bigger version of the 3.5-inch unit. The main architectural differences that affect porting:

| Area | 3.5-inch (ESP32-S3) | 4.3-inch (ESP32-P4) |
|---|---|---|
| CPU | Xtensa LX7 dual-core | RISC-V dual-core HP + single-core LP |
| Radio | On-die Wi-Fi/BT | Separate ESP32-C6 co-processor (hosted over SDIO) |
| Display bus | QSPI (ST77922), GPIO-mapped | MIPI-DSI (ST7701), dedicated PHY — no GPIO map |
| Resolution | 320 × 480 | 480 × 800 |
| Touch | ST77922 TDDI @ `0x55` | GT911 @ `0x5D`/`0x14` |
| PSRAM | 8 MB | 32 MB (in-package) |
| Flash | 16 MB | 32 MB |
| Audio | ES8311 | ES8311 + ES7210 (echo cancellation), dual mics |
| Camera | none | MIPI-CSI + ISP (OV5647 optional) |
| Status LED | WS2812B addressable | PWR indicator only |
| Primary toolchain | Arduino or ESP-IDF | ESP-IDF strongly recommended |

Because the display, radio, and touch controller all differ, board-support code (display init, touch driver, Wi-Fi bring-up) has to be rewritten against the P4 BSP — only the higher-level LVGL application screens port across relatively cleanly. Keeping the board-support functions separate from the application and LVGL screens, as in the 3.5-inch project structure, is what makes that reuse practical.
