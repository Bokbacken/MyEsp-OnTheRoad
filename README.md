# MyEsp-OnTheRoad

A pocket Yatzy / Maxi-Yatzy console for the road, built on the **Heemol ESP32-S3 3.5"** touch board
(ST77922 QSPI 320×480 + capacitive touch, WS2812 LED). Arduino-ESP32 3.x sketch with LVGL 8.4,
a Wi-Fi AP+STA web UI for status, settings and diagnostics.

> **Status:** scaffold. Compiles with arduino-cli; **not yet verified on hardware**.

## Features

- **Board support** – ST77922 over QSPI (40 MHz) with LVGL 8 partial PSRAM draw buffers, I2C capacitive
  touch (0x55), LEDC PWM backlight on GPIO41, WS2812 status LED on GPIO40.
- **Boot screen** – logo, progress bar and hardware checklist (display, touch, PSRAM, I2C, Wi-Fi, web)
  with green ✓ / red ✗, plus an R→G→B→white LED sequence.
- **Yatzy & Maxi-Yatzy** – 1–8 players, all 15 (Yatzy) / 20 (Maxi) categories, upper bonus (50 / 100),
  free or ordered play, Maxi "saved rolls" rule. Full scoring in `game_yatzy.cpp` with host tests.
- **Game UI** – variant menu, player names via a touch scroll wheel (char wheel), game screen with
  roll area + "nest" (tap a die to keep it), 800 ms ease-out roll animation, LED flash on roll,
  scrollable score table with best-category hint, result screen with medals.
- **Wi-Fi** – AP (`OnTheRoad`, default password `yatzy1234`) + optional STA, mDNS `ontheroad.local`.
- **Web UI** – dark card-based theme: dashboard, settings (display, LED, game, Wi-Fi with scan) and
  diagnostics. Pages are gzip-embedded in flash (`web_assets.h`), no filesystem needed.
- **Encoder input** – LVGL encoder indev with quadrature ISR (pins defined, not wired yet).

## Hardware

See [docs/hardware_reference.md](docs/hardware_reference.md) for the full reference. Pin map in
`MyEsp_OnTheRoad/board_pins.h`.

| Function | GPIO |
|---|---|
| LCD QSPI CS / CLK / D0 / D1 / D2 / D3 | 10 / 12 / 11 / 13 / 14 / 9 |
| LCD TE / Backlight | 42 / 41 |
| Touch SDA / SCL / INT / RST (I2C 0x55) | 38 / 39 / 47 / 48 |
| WS2812 LED (GRB) | 40 |
| Battery ADC | 8 |

SD card and audio (ES8311) are documented but not used yet.

## Building

### Arduino IDE 2

1. Install **esp32 by Espressif** core 3.x.
2. Install libraries: **lvgl 8.4.0** (not 9.x), **ESP Async WebServer** and **Async TCP** (ESP32Async).
3. Copy `extras/lv_conf.h` to your Arduino `libraries/` folder (next to the `lvgl` folder).
4. Open `MyEsp_OnTheRoad/MyEsp_OnTheRoad.ino` and select:

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash Size | 16MB |
| PSRAM | OPI PSRAM |
| USB CDC on Boot | Enabled |
| Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS) |
| CPU Frequency | 240 MHz |

### arduino-cli

```sh
arduino-cli lib install "lvgl@8.4.0" "ESP Async WebServer" "Async TCP"
cp extras/lv_conf.h ~/Arduino/libraries/lv_conf.h
arduino-cli compile \
  --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,CPUFreq=240 \
  MyEsp_OnTheRoad
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,CDCOnBoot=cdc,CPUFreq=240 MyEsp_OnTheRoad
```

### Web pages

Edit files in `MyEsp_OnTheRoad/data/`, then regenerate the embedded header:

```sh
python3 tools/embed_web.py          # writes MyEsp_OnTheRoad/web_assets.h
python3 tools/embed_web.py --check  # CI: fails if web_assets.h is stale
```

### Tests (host)

```sh
g++ -std=c++17 -Wall -Wextra -I MyEsp_OnTheRoad tests/test_yatzy.cpp MyEsp_OnTheRoad/game_yatzy.cpp -o test_yatzy && ./test_yatzy
```

## Web endpoints

| Method | Path | Description |
|---|---|---|
| GET | `/`, `/settings`, `/diag` | Dashboard, settings, diagnostics pages |
| GET | `/api/info` | System, display, touch, Wi-Fi status (JSON) |
| GET / POST | `/api/config` | Read / update settings (stored in NVS) |
| POST | `/api/wifi` | Set STA credentials and reconnect |
| GET | `/api/scan` | Wi-Fi scan (async) |
| POST | `/api/reboot`, `/api/reset` | Reboot / factory reset settings |

## Project layout

```
MyEsp_OnTheRoad/      Arduino sketch (board_*, ui_*, game_yatzy, web server, fonts, web_assets.h)
MyEsp_OnTheRoad/data/ Web page sources (HTML/CSS)
docs/                 Hardware reference
extras/lv_conf.h      LVGL 8.4 configuration
tests/                Host unit tests for the game logic
tools/embed_web.py    data/ -> web_assets.h generator
```

## Roadmap

- Verify display/touch on hardware, tune touch coordinates and refresh rate
- Wire up the rotary encoder
- Sound (ES8311), SD card, battery gauge
- More dice animation polish
