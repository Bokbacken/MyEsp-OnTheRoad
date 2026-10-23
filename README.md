# MyEsp-OnTheRoad

A pocket Yatzy / Maxi-Yatzy console for the road, built on the **Heemol ESP32-S3 3.5"** touch board
(ST77922 QSPI 320×480 + capacitive touch, WS2812 LED). **ESP-IDF 6.1** project with **LVGL 9.6**,
Wi-Fi station mode (on-device network setup) and a web UI for status, settings and diagnostics.

> **Status:** v0.5.2 – Swedish/English user interface (i18n), Latin-1 font fix for message boxes and
> interactive web play from a phone (`/play`, WebSocket, 4-digit game code). v0.5.2 builds with
> 0 errors / 0 warnings and passes the host tests, but is **not yet tested on hardware** (nor is v0.5.1's
> dice cup). v0.5.0's RTC and
> sprite dice animation are hardware-verified; v0.2.0 is hardware-verified. See [ROADMAP.md](ROADMAP.md).

## Features

- **Board support** – ST77922 over QSPI (40 MHz) with LVGL 9 partial PSRAM draw buffers (`esp_lcd`), I2C capacitive
  touch (0x55), LEDC PWM backlight on GPIO41, WS2812 status LED on GPIO40.
- **Boot screen** – logo, progress bar and hardware checklist (display, touch, PSRAM, I2C, RTC, storage, Wi-Fi, web)
  with green ✓ / red ✗, plus an R→G→B→white LED sequence.
- **Yatzy & Maxi-Yatzy** – 1–8 players, all 15 (Yatzy) / 20 (Maxi) categories, upper bonus (50 / 100),
  free or ordered play, Maxi "saved rolls" rule. Full scoring in `game_yatzy.cpp` with host tests.
- **Player registry** – up to 200 players with unique names (case-insensitive, å/ä/ö aware) and
  stable ids, stored on the FFat partition (`players.bin`, power-safe tmp + rename). Before a game the
  players are picked from a sorted list with search; "Ny spelare" saves a new name, "Gäst" plays
  without saving. Managed on the web page `/players` (add, rename, remove – remove needs the admin PIN).
- **Game UI** – variant menu with clock, player picker, game screen with
  roll area + "nest" (tap a die to keep it), sprite dice (pre-rendered ARGB8888 faces in PSRAM with gradient,
  gloss and inset pips) thrown in from one side with bounces and spin that rolls out onto the result
  (800 ms default), LED flash on roll,
  scrollable score table with best-category hint, result screen with medals.
- **Dice cup (v0.5.1, not yet hardware-tested)** – when it is time to roll, the lower part of the screen
  becomes a leather dice cup seen from above; the dice to be thrown rattle inside it. Hold a finger in
  the cup and shake (back and forth or in circles) – the cup follows the finger, the dice bounce off the
  wall and each other, and the LED flashes with the knocks. Swipe up quickly (≥ 500 px/s) or drag out of
  the cup upwards to throw; releasing without a swipe keeps the dice in the cup, a tap gives a standard
  throw. Swipe angle and speed shape the flight path, reach, length (600–1400 ms) and bounces; shake
  energy sets the spin and scatter; the circling direction sets the roll direction and a slight curve.
  The values still come from the game engine. Gesture and physics maths live in `main/dice_cup.*`
  (host-tested). Setting **Kastsätt: Bägare / Knapp** (device and web, default Bägare) switches back
  to the classic [Kasta] button.
- **Wi-Fi (station only)** – joins an existing access point; the soft-AP is disabled (`AP_MODE_ENABLED 0`).
  On-device setup (`ui_wifi.cpp`): network list sorted by RSSI with 5 signal bars and lock/"Öppen" marker,
  password entry with the on-screen scroll wheel (`my_charwheel` + `mypi_kbd_core.h`, same model as
  MyPi-Hotspot's keyboard: swipe/drag the wheel, tap to add), "Visa/Dölj", backspace, max 64 chars, then a
  connect screen with a 15 s countdown. Up to **50 saved networks** in priority order (NVS); at boot and
  after a lost connection the device scans and joins the highest-priority saved network in range,
  pinned to the **strongest AP (BSSID)** of that SSID. **Roaming:** while connected it scans in the
  background (every 15 s–5 min depending on RSSI) and moves to another AP of the same SSID when it is
  ≥ 8 dB stronger (≥ 5 dB below −75 dBm, 30 s cooldown, never at ≥ −55 dBm, skipped during OTA);
  policy in `roam_policy.h` (host-tested). 802.11k/v/r are not used (yet). LED: pulsing blue while
  connecting, green = connected, red = failed. mDNS `ontheroad.local`. The main menu (Wi-Fi icon) and
  settings screen open the network list; the games work without Wi-Fi.
- **Languages (v0.5.2)** – all on-device text and API error messages come from string tables
  (`main/lang/`: `enum class Str`, `tr()`, `lang_sv.cpp`, `lang_en.cpp`; missing entries fall back to
  Swedish). Choose the language on the device (Inställningar → Språk), on the web settings page or via
  `/api/config` (`lang`; NVS key `lang`, default `sv`). The `/play` and `/game` pages follow the device
  language; the other web pages are still Swedish. Adding a language is described in `main/lang/lang.h`.
- **Web play (v0.5.2, not yet hardware-tested)** – in the player setup each seat (guests too) can be
  marked as a web player (Wi-Fi button). A 4-digit code is created when the game starts (kept for
  "Spela igen") and shown in the game header, on `/game` and in the waiting panel. On a web player's turn
  the console shows "Väntar på X via webben..." (plus URL and code until a phone has joined) and blocks
  local input; the phone opens `http://<device-ip>/play?code=XXXX`, picks its seat and holds dice, rolls
  (the console runs the normal throw animation) and writes a box. Everything runs over the WebSocket
  `/ws/game` (max 4 clients, no polling). Without web players everything works as before.
- **Clock** – NTP (pool.ntp.org, time.google.com, time.cloudflare.com) when online, POSIX time zone
  (default Swedish CET/CEST), manual setting on the device (Inställningar → Klocka) or from the browser.
  The last time is kept in NVS across reboots and shown as uncertain (orange "?") until NTP or a manual set.
  With the optional **DS3231 RTC** the clock is set from the RTC at boot (source "RTC", certain); the RTC
  keeps UTC and is rewritten after every NTP sync / manual set when it drifts ≥ 1 s. Log lines carry
  uptime and are shown in real time as soon as the clock is certain (also lines from before that point).
- **Live view** – `/game` shows the running game read-only (dice, kept dice, full score sheet, winner,
  "skakar bägaren..." while the cup is shaken),
  polling `/api/game` every 700 ms; handy on a phone or TV next to the device.
- **OTA firmware update** – web page `/update` streams the `-ota.bin` into the inactive app slot
  (app0/app1) with progress on the web page and on the display; the running firmware stays if anything fails.
- **Admin PIN** – optional 4–8 digit PIN (Inställningar on the web) protecting OTA, factory reset,
  player removal and PIN changes.
- **Web UI** – dark card-based theme: dashboard, players, settings (display, LED, game, saved networks,
  clock, time zone, admin PIN), diagnostics and update. Pages are gzip-embedded in flash
  (`main/web_assets.h`), no filesystem upload needed.

The hardware rotary encoder was removed in v0.3.0; all input is touch.

## Hardware

See [docs/hardware_reference.md](docs/hardware_reference.md) for the full reference. Pin map in
`main/board_pins.h`.

| Function | GPIO |
|---|---|
| LCD QSPI CS / CLK / D0 / D1 / D2 / D3 | 10 / 12 / 11 / 13 / 14 / 9 |
| LCD TE / Backlight | 42 / 41 |
| Touch SDA / SCL / INT / RST (I2C 0x55) | 38 / 39 / 47 / 48 |
| DS3231 RTC (I2C 0x68, optional) | SDA 38 / SCL 39 (shared bus), 3V3 |
| WS2812 LED (GRB) | 40 |
| Battery ADC | 8 |

SD card and audio (ES8311) are documented but not used yet.

**RTC module:** the Raspberry Pi "DS3231 for Pi" board (5-pin header marked `- NC C D +`) is wired as
`-` → GND, `NC` → not connected, `C` (SCL) → IO39, `D` (SDA) → IO38, `+` → **3V3** (not 5 V – the board's
pull-ups go to its supply). These modules have no charging circuit, so a CR1220/LIR1220 coin cell simply
lasts until it is empty; the boot checklist shows "tid ogiltig (batteri?)" when the time was lost.

## Building

Requires **ESP-IDF 6.1** (`idf.py`). Managed components (LVGL 9.6.0, mdns, cJSON, led_strip) are
fetched automatically on the first build (`dependencies.lock` pins the versions). Board settings
(16 MB flash, OPI PSRAM, custom partition table, app rollback) are in `sdkconfig.defaults`.

```sh
. ~/esp/esp-idf-v6.1/export.sh
idf.py build                      # build/MyEsp_OnTheRoad.bin (= -ota.bin)
idf.py merge-bin -o merged.bin    # build/merged.bin (= -merged.bin, flash at 0x0)
idf.py -p /dev/ttyACM0 flash monitor
```

The firmware version comes from `PROJECT_VER` in the top-level `CMakeLists.txt`.

### Shared components

The `components/my_*` folders have no project-specific code and are meant to be reused in other
ESP32 projects (MyPi-Companion, MyPi-Hotspot):

| Component | Purpose |
|---|---|
| `my_log` | Ring-buffer log + persistent event log (served at `/api/log`, `/api/events`) |
| `my_storage` | NVS helpers, FAT (wear-levelled) mount and atomic file writes |
| `my_time` | SNTP, POSIX time zone, manual set, RTC source, last time kept in NVS |
| `my_rtc` | DS3231 on a shared `i2c_master` bus: function test, UTC read/write at the second edge, drift sync (`my_rtc_time.h` host-tested) |
| `my_diag` | System/heap/PSRAM/partition/reset-reason info as JSON, task list |
| `my_wifi` | STA with saved networks (`net_list.h`), strongest-BSSID join, roaming (`roam_policy.h`), mDNS |
| `my_ota` | OTA into the inactive slot, rollback mark-valid |
| `my_web` | `esp_http_server` wrapper: routes, gzip assets, JSON helpers, admin PIN, log/OTA/reboot routes |
| `my_lvgl` | LVGL 9 tick/task/mutex setup and input devices |
| `my_ui` | Theme, fonts (Swedish å/ä/ö), scroll wheel keyboard (`my_charwheel`, `mypi_kbd_core.h`), text input |

### Web pages

Edit files in `main/web/`, then regenerate the embedded header:

```sh
python3 tools/embed_web.py          # writes main/web_assets.h
python3 tools/embed_web.py --check  # CI: fails if web_assets.h is stale
```

### Tests (host)

```sh
INC="-Imain -Icomponents/my_wifi/include -Icomponents/my_ui/include"
g++ -std=c++17 -Wall -Wextra $INC tests/test_yatzy.cpp main/game_yatzy.cpp -o tests/test_yatzy && ./tests/test_yatzy
g++ -std=c++17 -Wall -Wextra $INC tests/test_core.cpp -o tests/test_core && ./tests/test_core   # net list, scroll wheel, player registry
g++ -std=c++17 -Wall -Wextra -Icomponents/my_rtc/include tests/test_rtc.cpp -o tests/test_rtc && ./tests/test_rtc   # DS3231 date format
g++ -std=c++17 -Wall -Wextra -Imain tests/test_cup.cpp main/dice_cup.cpp -o tests/test_cup && ./tests/test_cup   # dice cup gesture/physics
g++ -std=c++17 -Wall -Wextra -Imain -Imain/lang tests/test_lang.cpp main/lang/lang.cpp main/lang/lang_sv.cpp main/lang/lang_en.cpp -o tests/test_lang && ./tests/test_lang   # string tables
```

## Flashing and updating

- **First time, and from Arduino builds (≤ v0.3.x) to v0.4.0:** flash `MyEsp_OnTheRoad-vX.Y.Z-merged.bin`
  at offset `0x0` over USB (esptool.js / esptool). This also installs the ESP-IDF bootloader needed for
  OTA rollback. The partition table is unchanged, so the FFat players file is kept; the merged image does
  overwrite the NVS area (settings and saved networks are reset). To keep NVS, flash the separate files
  instead: bootloader `0x0`, partition table `0x8000`, `ota_data_initial.bin` `0xe000`, app `0x10000`.
- **After that:** open `http://ontheroad.local/update` and upload `MyEsp_OnTheRoad-vX.Y.Z-ota.bin`.
  Settings, saved networks and players are kept.

## Web endpoints

| Method | Path | Description |
|---|---|---|
| GET | `/`, `/game`, `/play`, `/players`, `/settings`, `/diag`, `/update` | Web pages |
| GET | `/api/info` | System, display, touch, Wi-Fi, clock, RTC, OTA slots, storage (JSON) |
| GET | `/api/game` | Live game snapshot (read-only); `?v=<version>` answers `{"same":true}` when unchanged. Also `code`, `web_players` (`[{player,id,connected}]`), `lang` |
| WS | `/ws/game` | Web play. Client: `{"cmd":"join","code":1234,"player":n}` (no `player` = spectator), `{"cmd":"hold","die":n}`, `{"cmd":"roll"}`, `{"cmd":"choose","cat":id}`. Device: `joined`, `error` (`code/seat/join/full/cmd/turn`), `state` (= `/api/game` + `you`, `joined`) on every change |
| GET / POST | `/api/config` | Read / update settings (NVS); `tz`, `lang` (`sv`/`en`, list in `langs`), `admin_pin_new` (+ `pin`) |
| POST | `/api/wifi` | Connect to `ssid`/`pass` (added to the saved list on success) |
| GET | `/api/scan` | Wi-Fi scan (async) |
| GET / POST | `/api/nets` | Saved networks (never passwords); `action` = add / remove / move (`delta`) / forget_all |
| POST | `/api/time` | Set the clock manually (`epoch`, UTC seconds) |
| GET / POST | `/api/players` | Player registry; `action` = add (`name`) / rename (`id`, `name`) / remove (`id`, admin `pin`) |
| POST | `/api/ota` | Firmware upload (multipart, `?size=&pin=`) |
| POST | `/api/reboot`, `/api/reset` | Reboot / factory reset settings (reset needs the admin PIN) |

## Project layout

```
main/                 Application: app_main, app_config, app_players, app_web, board_* drivers,
                      ui_* screens, game_yatzy, player_db, board_pins.h, web_assets.h
main/lang/            UI strings: lang.h (ids, tr), lang.cpp, lang_sv.cpp, lang_en.cpp
main/web/             Web page sources (HTML/CSS)
components/my_*/      Shared reusable components (see above)
docs/                 Hardware reference
tests/                Host unit tests (game logic, saved networks, scroll wheel, player registry, RTC format, dice cup, strings)
tools/embed_web.py    main/web/ -> main/web_assets.h generator
tools/lvgl8to9.py     Helper used for the LVGL 8 -> 9 API rename
partitions.csv        Same layout as Arduino app3M_fat9M_16MB
```

## Roadmap

See [ROADMAP.md](ROADMAP.md).
