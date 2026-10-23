# MyEsp-OnTheRoad

A pocket Yatzy / Maxi-Yatzy console for the road, built on the **Heemol ESP32-S3 3.5"** touch board
(ST77922 QSPI 320×480 + capacitive touch, WS2812 LED). Arduino-ESP32 3.x sketch with LVGL 8.4,
Wi-Fi station mode (on-device network setup) and a web UI for status, settings and diagnostics.

> **Status:** v0.3.0. v0.2.0 is verified on hardware; the v0.3.0 additions (OTA, saved networks,
> clock, player registry, on-screen scroll wheel) compile and pass the host tests but are **not yet
> verified on hardware**. See [ROADMAP.md](ROADMAP.md).

## Features

- **Board support** – ST77922 over QSPI (40 MHz) with LVGL 8 partial PSRAM draw buffers, I2C capacitive
  touch (0x55), LEDC PWM backlight on GPIO41, WS2812 status LED on GPIO40.
- **Boot screen** – logo, progress bar and hardware checklist (display, touch, PSRAM, I2C, storage, Wi-Fi, web)
  with green ✓ / red ✗, plus an R→G→B→white LED sequence.
- **Yatzy & Maxi-Yatzy** – 1–8 players, all 15 (Yatzy) / 20 (Maxi) categories, upper bonus (50 / 100),
  free or ordered play, Maxi "saved rolls" rule. Full scoring in `game_yatzy.cpp` with host tests.
- **Player registry** – up to 200 players with unique names (case-insensitive, å/ä/ö aware) and
  stable ids, stored on the FFat partition (`/players.bin`, power-safe tmp + rename). Before a game the
  players are picked from a sorted list with search; "Ny spelare" saves a new name, "Gäst" plays
  without saving. Managed on the web page `/players` (add, rename, remove – remove needs the admin PIN).
- **Game UI** – variant menu with clock, player picker, game screen with
  roll area + "nest" (tap a die to keep it), 800 ms ease-out roll animation, LED flash on roll,
  scrollable score table with best-category hint, result screen with medals.
- **Wi-Fi (station only)** – joins an existing access point; the soft-AP is disabled (`AP_MODE_ENABLED 0`).
  On-device setup (`ui_wifi.cpp`): network list sorted by RSSI with 5 signal bars and lock/"Öppen" marker,
  password entry with the on-screen scroll wheel (`ui_charwheel` + `mypi_kbd_core.h`, same model as
  MyPi-Hotspot's keyboard: swipe/drag the wheel, tap to add), "Visa/Dölj", backspace, max 64 chars, then a
  connect screen with a 15 s countdown. Up to **50 saved networks** in priority order (NVS); at boot and
  after a lost connection the device scans and joins the highest-priority saved network in range. LED: pulsing blue while
  connecting, green = connected, red = failed. mDNS `ontheroad.local`. The main menu (Wi-Fi icon) and
  settings screen open the network list; the games work without Wi-Fi.
- **Clock** – NTP (pool.ntp.org, time.google.com, time.cloudflare.com) when online, POSIX time zone
  (default Swedish CET/CEST), manual setting on the device (Inställningar → Klocka) or from the browser.
  The last time is kept in NVS across reboots and shown as uncertain (orange "?") until NTP or a manual set.
- **OTA firmware update** – web page `/update` streams the `-ota.bin` into the inactive app slot
  (app0/app1) with progress on the web page and on the display; the running firmware stays if anything fails.
- **Admin PIN** – optional 4–8 digit PIN (Inställningar on the web) protecting OTA, factory reset,
  player removal and PIN changes.
- **Web UI** – dark card-based theme: dashboard, players, settings (display, LED, game, saved networks,
  clock, time zone, admin PIN), diagnostics and update. Pages are gzip-embedded in flash
  (`web_assets.h`), no filesystem upload needed.

The hardware rotary encoder was removed in v0.3.0; all input is touch.

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
g++ -std=c++17 -Wall -Wextra -I MyEsp_OnTheRoad tests/test_core.cpp -o test_core && ./test_core   # net list, scroll wheel, player registry
```

## Flashing and updating

- **First time / from v0.2.0** (no OTA yet): flash `MyEsp_OnTheRoad-vX.Y.Z-merged.bin` at offset `0x0`
  over USB (esptool.js / esptool). The first boot formats the FFat partition (a few seconds,
  "Lagring" on the boot screen).
- **After that:** open `http://ontheroad.local/update` and upload `MyEsp_OnTheRoad-vX.Y.Z-ota.bin`.
  Settings, saved networks and players are kept.

## Web endpoints

| Method | Path | Description |
|---|---|---|
| GET | `/`, `/players`, `/settings`, `/diag`, `/update` | Web pages |
| GET | `/api/info` | System, display, touch, Wi-Fi, clock, OTA slots, storage (JSON) |
| GET / POST | `/api/config` | Read / update settings (NVS); `tz`, `admin_pin_new` (+ `pin`) |
| POST | `/api/wifi` | Connect to `ssid`/`pass` (added to the saved list on success) |
| GET | `/api/scan` | Wi-Fi scan (async) |
| GET / POST | `/api/nets` | Saved networks (never passwords); `action` = add / remove / move (`delta`) / forget_all |
| POST | `/api/time` | Set the clock manually (`epoch`, UTC seconds) |
| GET / POST | `/api/players` | Player registry; `action` = add (`name`) / rename (`id`, `name`) / remove (`id`, admin `pin`) |
| POST | `/api/ota` | Firmware upload (multipart, `?size=&pin=`) |
| POST | `/api/reboot`, `/api/reset` | Reboot / factory reset settings (reset needs the admin PIN) |

## Project layout

```
MyEsp_OnTheRoad/      Arduino sketch (board_*, ui_*, game_yatzy, web server, fonts, web_assets.h)
MyEsp_OnTheRoad/data/ Web page sources (HTML/CSS)
docs/                 Hardware reference
extras/lv_conf.h      LVGL 8.4 configuration
tests/                Host unit tests (game logic, saved networks, scroll wheel, player registry)
tools/embed_web.py    data/ -> web_assets.h generator
```

## Roadmap

See [ROADMAP.md](ROADMAP.md).
