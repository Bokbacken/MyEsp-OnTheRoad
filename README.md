# MyEsp-OnTheRoad

A portable touch-screen console for dice games, tile-based adventures and
SoundFont-powered Karaoke. It runs on the **Heemol ESP32-S3 3.5"** and
**Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3** boards, using **ESP-IDF 6.1**
and **LVGL 9.6**.

Play locally, join a hosted Yatzy game from another device or a browser, or
manage songs and adventures on the SD card through the built-in web interface.
Games work without Wi-Fi; network features use station mode.

See [CHANGELOG.md](CHANGELOG.md) for version history and migration notes.

## Documentation

| Document | Purpose |
|---|---|
| [Adventure quick guide](docs/ADVENTURE_AUTHORING_QUICKSTART.md) | Create and install adventures |
| [Adventure authoring reference](docs/AUTHORING_ADVENTURES.md) | Supported `.adv` syntax and mechanics |
| [Motoräventyret campaign](sdcard_bundle/otr/adventure/motoraventyr/README.md) | Install and play the numbered SD chapters |
| [File management](docs/FILE_MANAGEMENT.md) | SD uploads, folders, formatting and performance |
| [P4 SD/Wi-Fi investigation](docs/P4_SD_WIFI_INVESTIGATION.md) | Failure timeline, shared-controller clock findings and testing on other P4 boards |
| [Karaoke and SoundFonts](docs/MUSIC_KARAOKE_PLAN.md) | Audio architecture and memory/quality tradeoffs |
| [Multiplayer design](docs/MULTIPLAYER_DESIGN.md) | Host-authoritative Yatzy and joining devices |
| [Adventure work plan](docs/ADVENTURE_TODO.md) | Adventure/editor delivery status and remaining tests |
| [Multi-board work plan](docs/MULTI_BOARD_TODO.md) | Board support, unresolved issues and hardware checks |

## Features

- **Dice games** – Yatzy, Maxi-Yatzy, Pig, Farkle and Ship-Captain-Crew.
- **Tile adventures** – SD-loaded `.adv` campaigns, internal maps and portals,
  puzzles, story-driven terrain/object changes, resources, NPC dialogue,
  checkpoints and saved progress. See the [Motoräventyret install guide](sdcard_bundle/otr/adventure/motoraventyr/README.md)
  and the [authoring reference](docs/AUTHORING_ADVENTURES.md) for examples,
  supported mechanics and limits.
- **Play together** – host a Yatzy game and join from another OnTheRoad device
  or a browser, with personal player PINs and host-authoritative game state.
- **Karaoke** – responsive landscape layout at logical
  480×320 / 800×480, wrapping scrollable lyrics, one primary transport row and hamburger settings;
  multiple SoundFonts (song-list global default, existing per-song registry overrides), plays `.mid`/`.kar` from the SD card through a SoundFont synth
  (TinySoundFont) with the lyrics on screen, word-by-word highlighting, tempo/key/volume, melody on/off and a
  per-song melody-track choice stored in `karaoke.reg`. Silent text-only mode without a SoundFont.
  MIDI without lyrics can use a matching [`.lyr` text/LRC sidecar](docs/KARAOKE_LYRICS.md).
  Web karaoke uses the browser width with responsive lyrics.
- **Sound** – ES8311 codec + amplifier, mixer task with 1 music + 4 effect
  channels, built-in chiptune synth (square/triangle/noise, ADSR, MML tunes). Any sound can be replaced by a WAV on
  the SD card. Volumes and on/off on the device and in the web settings.
- **microSD + file browser** – FAT storage over 4-bit SDMMC (1-bit fallback).
  `/files.html`: browse the whole card (or the internal flash), download, streamed upload, delete, rename, new
  folder and directory uploads. Up to four SD uploads run concurrently while
  card operations remain serialized, with recovery deferred until transfer files close.
  Existing files are skipped by default; overwrite is explicit. SD formatting
  requires separate confirmations, including for an unmountable card.
  NVS settings memory has a separate information view and binary backup download
  (the backup may contain credentials). Changes and NVS downloads require the
  admin PIN when configured. See the [file-management reference](docs/FILE_MANAGEMENT.md).
- **Diagnostics logging** – Info by default, with temporary Error/Warning/Info/Debug
  controls and independent ten-minute SD tracing on P4. Changes require admin authorization
  and reset at restart; RAM-only log export remains available during SD trouble.

### SD card layout (optional)

```
/sdcard/otr/sounds/<name>.wav       effect override: click dice_shake dice_roll die_land score zero yatzy bust win
                                    step bump push hit break gem key potion heart life door locked mine splash hurt
                                    life_lost game_over checkpoint exit
/sdcard/otr/music/adventure.wav     looping music (adventure); dice.wav = menus and dice games
/sdcard/otr/adventure/saves/        adventure saves (fallback: internal flash)
/sdcard/otr/adventure/maps/         reserved for future maps
/sdcard/otr/karaoke/                karaoke songs (.mid / .kar, max 1 MB each); "Artist_Title.mid" gives artist + title
/sdcard/otr/karaoke/<song>.lyr      optional UTF-8 lyric sheet or LRC timestamps beside matching .mid/.kar
/sdcard/otr/karaoke/karaoke.reg     per-song settings (melody track, key, tempo) – created on a settings change, plain text
/sdcard/otr/sf2/                    SoundFonts (.sf2 v2); per-song or global bank selection
```

SoundFont: not included (licence and size). A good fit is **TimGM6mb.sf2** (~5.7 MB, GPL v2):
<https://github.com/arbruijn/TimGM6mb/raw/master/TimGM6mb.sf2> – upload it to `/otr/sf2/` with the web file browser.
Only the bank structure is read initially; samples required by the song are
loaded selectively. The memory budget and resampling policy depend on the
board and available PSRAM; file size is not the same as resident sample size.
See the SoundFont memory section in the Karaoke reference.

WAV: PCM 8 or 16 bit, mono or stereo, 8-44.1 kHz (mixed to 22 050 Hz mono). Effects are cached in PSRAM
(max ~2 MB total); music is streamed. Missing file = built-in tone version.

- **Adventure controls** – a reserved HUD strip keeps status separate from the
  map. Touch navigation and action controls provide movement, interaction and
  equipment actions; the menu includes zoom and Pro/Assist settings.
- **Double-tap protection (v0.10.x)** – the touch driver only reports a release after the finger has been up
  60 ms (the controller can drop a held finger for a read or two, which made buttons fire twice; filtered gaps
  are counted on the diagnostics page), and every toggle has a 500 ms guard (`Theme::debounce`).
- **Adventure engine** – separate module (`components/otr_engine`
  + `main/ui_adventure.cpp`): own tile renderer on a PSRAM canvas, smooth camera, 32/16 px zoom. Two worlds:
  the 60 × 60 *Testvärlden* (rocks, keys/doors, boat, lava, plates/gates) and the minesweeper-style
  *Minfältet* (fog, number hints, flood reveal, flags). Tap-to-walk with A* (Pro / Assistent mode, the
  assistant stops before deadly lava, unknown ground or water without boat), arrow pad with STOP, energy
  bar, potions/hearts, lives (finite or infinite), protective suit, hidden treasures under crates/bushes/
  walls/rocks/doors, and per-player saves on FFat (Continue / New game). See
  [docs/ENGINE_2D_BRIEF.md](docs/ENGINE_2D_BRIEF.md).
- **Board support** – S3 has an ST77922 QSPI 320×480 display and capacitive
  touch; P4 has an ST7701 MIPI-DSI 480×800 display and GT911 touch. Both use
  LVGL PSRAM draw buffers. RGB LED controls are only offered on S3.
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
- **Dice cup** – when it is time to roll, the lower part of the screen
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
- **Languages** – all on-device text and API error messages come from string tables
  (`main/lang/`: `enum class Str`, `tr()`, `lang_sv.cpp`, `lang_en.cpp`; missing entries fall back to
  Swedish). Choose the language on the device (Inställningar → Språk), on the web settings page or via
  `/api/config` (`lang`; NVS key `lang`, default `sv`). The `/play` and `/game` pages follow the device
  language; the other web pages are still Swedish. Adding a language is described in `main/lang/lang.h`.
- **Web play** – in the player setup each registered player can be marked as
  a web player (Wi-Fi button); a player without a personal PIN is asked for one on the numeric keypad
  first (guests cannot play via the web). On a web player's turn the console shows "Väntar på X via
  webben..." and blocks local input; the phone opens `http://<device-ip>/play`, picks its name, enters its
  4-digit PIN and holds dice, rolls (the console runs the normal throw animation) and writes a box. There
  is no shared game code: seats are bound to an internal session id that changes with every new game.
  Protocol on the WebSocket `/ws/game` (max 4 clients, no polling): `{"cmd":"join","player":n,"pin":"1234"}`,
  errors `nogame|seat|pin|nopin|locked`; 5 wrong PINs per connection or per seat lock for 60 s. PINs are
  never sent out (`has_pin` only) and can also be managed on `/players`.
- **Screen rotation** – 0° (USB down) or 180°, set on the device or via `/api/config`
  (`rotation`); implemented with MADCTL MX|MY and mirrored touch coordinates.
- **Clock** – NTP (pool.ntp.org, time.google.com, time.cloudflare.com) when online, POSIX time zone
  (default Swedish CET/CEST), selected through a readable city/UTC dropdown
  with automatic daylight saving on-device or in web settings.
  Manual time setting is available on the device (Inställningar → Klocka) or from the browser.
  The last time is kept in NVS across reboots and shown as uncertain (orange "?") until NTP or a manual set.
  With the optional **DS3231 RTC** the clock is set from the RTC at boot (source "RTC", certain).
  P4 automatically prefers a DS3231 at 0x68 on its shared I2C bus; without one it uses its internal RTC.
  The DS3231
  keeps UTC and is rewritten after every NTP sync / manual set when it drifts ≥ 1 s. Log lines carry
  uptime and are shown in real time as soon as the clock is certain (also lines from before that point).
- **Battery history and idle mode** – optional five-minute CSV history on SD,
  plus idle screen-off with Wi-Fi and audio paused and touch wake.
  Both options are off by default.
  P4 also puts the ST7701 controller to sleep; CPU and DSI power stay on.
  Confirmed software restart is available in local Settings and on the web.
  See [measurement fields and limits](docs/POWER_HISTORY.md).
- **Device name and navigation** – the configured hostname is used in the
  boot/main-menu display, shared web header, DHCP and mDNS (`name.local`).
  Every web page shares a compact sticky navigation menu; open visible
  pages pick up name changes within ten seconds.
- **Live view** – `/game` shows the running game read-only (dice, kept dice, full score sheet, winner,
  "skakar bägaren..." while the cup is shaken),
  polling `/api/game` every 700 ms; handy on a phone or TV next to the device.
- **Adventure live view** – `/adventure` shows the running adventure read-only: canvas map
  (whole map scaled to fit, or follow the player), people, items, gates, the player with facing, a HUD and the
  current dialogue (speaker, full text, reference, choices with locked hints). Polls `/api/adventure` ~4–5×/s;
  the map is only resent when its revision changes. `/game` shows a hint while an adventure is running.
- **OTA firmware update** – web page `/update` streams `MyEsp_OnTheRoad.bin` into the inactive app slot
  (app0/app1) with progress on the web page and on the display; the running firmware stays if anything fails.
- **Admin PIN** – optional 4–8 digit PIN (Inställningar on the web) protecting OTA, factory reset,
  player removal and PIN changes.
- **Web UI** – dark card-based theme: dashboard, players, settings (display, LED, game, saved networks,
  clock, time zone, admin PIN), diagnostics and update. Pages are gzip-embedded in flash
  (`main/web_assets.h`), no filesystem upload needed.

All input is touch.

## Hardware

See [the S3 hardware reference](docs/hardware_reference.md) and
[the P4 hardware reference](docs/Waveshare_ESP32-P4-WIFI6-Touch-LCD-4.3_Hardware_Reference.md).
The table below is for S3 only; `main/board_pins.h` selects the active board's pin map.

| Function | GPIO |
|---|---|
| LCD QSPI CS / CLK / D0 / D1 / D2 / D3 | 10 / 12 / 11 / 13 / 14 / 9 |
| LCD TE / Backlight | 42 / 41 |
| Touch SDA / SCL / INT / RST (I2C 0x55) | 38 / 39 / 47 / 48 |
| DS3231 RTC (I2C 0x68, optional) | SDA 38 / SCL 39 (shared bus), 3V3 |
| WS2812 LED (GRB) | 40 |
| Battery ADC | 8 |

microSD (4-bit SDMMC) and audio (ES8311 + amplifier) are used since v0.9.0; see the hardware reference for pins.

**RTC module:** the Raspberry Pi "DS3231 for Pi" board (5-pin header marked `- NC C D +`) is wired as
`-` → GND, `NC` → not connected, `C` (SCL) → IO39, `D` (SDA) → IO38, `+` → **3V3** (not 5 V – the board's
pull-ups go to its supply). These modules have no charging circuit, so a CR1220/LIR1220 coin cell simply
lasts until it is empty; the boot checklist shows "tid ogiltig (batteri?)" when the time was lost.

### Hardware profiles

The board profile follows the chip target (`idf.py set-target`): `ES3C35P` for `esp32s3` and
`P4_43` (Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3, ST7701 MIPI-DSI 480x800, GT911 touch, Wi-Fi via
an ESP32-C6 over ESP-Hosted) for `esp32p4` (`CONFIG_OTR_HW_PROFILE_*`). Pins live in
`main/board_pins_<profile>.h`, board drivers behind the shared `main/board_*.h` APIs. The P4 port
is tracked in [`docs/MULTI_BOARD_TODO.md`](docs/MULTI_BOARD_TODO.md), including
owner-reported results and remaining hardware checks.
`main/hw_profile.*` exposes the active profile (name, logical display size, touch/audio/SD
capabilities). `main/ui_metrics.*` holds shared UI sizes
(margins, button sizes, card spacing) derived from the live display size; the main menu now uses
an LVGL flex layout built on them. Screen adaptations and remaining limitations
are recorded in the multi-board work plan.

## Building

Requires **ESP-IDF 6.1** (`idf.py`). Managed components (LVGL 9.6.0, mdns, cJSON, led_strip and,
on ESP32-P4, ESP-Hosted) are fetched automatically on the first build; `dependencies.lock.<target>`
pins the versions per target. Shared settings (custom partition table, app rollback, `-Os`) are in
`sdkconfig.defaults`; board settings in `sdkconfig.defaults.esp32s3` (16 MB flash, octal PSRAM,
USB console) and `sdkconfig.defaults.esp32p4` (chip rev v3.x, 32 MB flash, PSRAM, UART console,
ESP-Hosted). S3 uses `partitions.csv` (16 MB); P4 uses
`partitions.esp32p4.csv` (32 MB), with larger FFat and the same app-slot offsets.
Existing P4 devices need a one-time USB partition-table migration to expose
the additional space; back up FFat first and follow the
[migration procedure](docs/MULTI_BOARD_TODO.md#p4-partitionstabell-usb-migrering).
The S3 layout is:

| Partition | Offset | Size |
|---|---:|---:|
| nvs | 0x9000 | 20 kB |
| otadata | 0xE000 | 8 kB |
| app0 (ota_0) | 0x10000 | 5 MB |
| app1 (ota_1) | 0x510000 | 5 MB |
| ffat | 0xA10000 | 5.9 MB |
| coredump | 0xFF0000 | 64 kB |

```sh
. ~/esp/esp-idf-v6.1/export.sh
# ES3C35P (ESP32-S3)
idf.py -B build_esp32s3 -D SDKCONFIG=build_esp32s3/sdkconfig set-target esp32s3 build
idf.py -B build_esp32s3 -D SDKCONFIG=build_esp32s3/sdkconfig merge-bin -o merged.bin
idf.py -B build_esp32s3 -D SDKCONFIG=build_esp32s3/sdkconfig -p /dev/ttyACM0 flash monitor
# Waveshare ESP32-P4 4.3" (flash/monitor via the USB-UART Type-C port)
idf.py -B build_esp32p4 -D SDKCONFIG=build_esp32p4/sdkconfig set-target esp32p4 build
idf.py -B build_esp32p4 -D SDKCONFIG=build_esp32p4/sdkconfig merge-bin -o merged.bin
idf.py -B build_esp32p4 -D SDKCONFIG=build_esp32p4/sdkconfig -p /dev/ttyUSB0 flash monitor
```

`<build dir>/MyEsp_OnTheRoad.bin` is the OTA image, `<build dir>/merged.bin` the complete image
to flash at `0x0` (first install, e.g. with esptool.js in the browser). Once the firmware runs,
later updates can use the OTA file through the web page.

The firmware version comes from `PROJECT_VER` in the top-level `CMakeLists.txt`.
S3 builds include Espressif's targeted SPI bus-lock race fix (`a16c003`)
backported to the pinned ESP-IDF v6.1 source. CMake verifies source fingerprints
and compiles a patched copy in the build directory, without changing the SDK.
An SDK upgrade must explicitly review/update this backport; an unknown source
fails configuration. P4 does not use this backport.

CI builds both boards and labels the artifacts `firmware-esp32s3-v<PROJECT_VER>` and
`firmware-esp32p4-v<PROJECT_VER>-unverified` (P4 hardware verification is incomplete;
startup and Wi-Fi have been confirmed).
This build label does not create a Git release tag.

### Firmware releases

PRs validate S3/P4 when build inputs change and retain firmware/debug CI artifacts,
including available files after failures. Documentation-only changes (`*.md`, `docs/`,
`LICENSE`) skip firmware builds. On an updated PR, a successful preceding run can be
reused when only documentation changed and the comparison base is unchanged; otherwise
the complete PR diff is checked. Failed or unfinished builds are not treated as verified.
The lightweight selection job still runs, and skipped build checks do not block merge.
Selection failures fail the build checks rather than silently skipping them.

C6 radio firmware builds only when `.github/c6-firmware.json` changes (its pinned
ESP-Hosted revision and ESP-IDF version), or when manually requested. Changes to the
S3/P4 Wi-Fi code do not rebuild that separate upstream image.

Merging does not start another build. Direct pushes to `main` are not automatically
built: use PRs, or **Actions → Build firmware → Run workflow**, select the branch and
optionally enable **Build C6 radio firmware too**. Manual runs always build S3/P4.
New PR updates cancel obsolete runs to avoid unnecessary waiting. These workflows
**never publish a release**. Release preparation is a separate manual operation:

1. Merge and test the intended changes on `main`. Read the current `PROJECT_VER` from
   `CMakeLists.txt`; the workflow never bumps it. Each release needs a unique canonical
   `MAJOR.MINOR.PATCH` version. Record the full 40-character tested main commit SHA.
2. In **Actions → Prepare firmware release → Run workflow**, select branch **main** and
   paste that SHA into **expected_sha**. It must equal main at dispatch time. If main
   moved since testing, test the new commit first; do not substitute an untested SHA.
   CLI equivalent: `gh workflow run release.yaml --ref main -f expected_sha=FULL_MAIN_SHA`.
3. The workflow checks event/ref, checkout SHA and version, builds that exact dispatch
   SHA using ESP-IDF v6.1 and `sdkconfig.defaults`, and runs `idf.py merge-bin`.
   It checks the generated target/profile, actual app header/version, app size against
   **both generated OTA partitions**, required files and merged-image contents.
   There is no P4 build. Failed builds retain available debug/diagnostic CI artifacts.
4. Only after the build/package job succeeds, a separate publishing job rechecks the
   dispatch identity, manifest and every checksum. It creates `v<PROJECT_VER>` at that
   same SHA (not whatever main points to later), then a **draft** GitHub Release.
   Notes use GitHub's generated changes for the selected tag/SHA, plus flashing and
   hardware-verification warnings. Review the real change list and add actual device
   test results; compilation alone does **not** prove display/touch/audio/SD/OTA work.
5. Download and check the assets, test the intended device, then use **Releases → Edit
   draft → Publish release** for manual final approval. Inspect the diff against the
   previous published tag if GitHub's automatically selected notes baseline is unsuitable.

The workflow must first be merged into `main` before this dispatch is available.
No actual tag/release should be created just to test a CI change. Repository rules or
Actions policies may require maintainer approval/adjustment for tag/release creation;
the workflow cannot bypass protected tags. No PAT or additional secret is needed.
Global `contents: read` covers builds; only the trusted main publishing job has
`contents: write` with `GITHUB_TOKEN`, scoped to the publication step. Checkouts do not
persist credentials. Fork PRs never get write permissions, and `pull_request_target`
is not used. Tag creation does not need to trigger another workflow: build and
publication are in this same workflow. Release runs are serialized (not cancelled);
GitHub concurrency may replace an older pending run when multiple runs are queued.

**Assets** (prefix `MyEsp-OnTheRoad-esp32s3-ES3C35P-v<PROJECT_VER>`):

| Suffix / file | Purpose |
|---|---|
| `-ota.bin` | App **only**; select this file on the device's `/update` web page. Requires compatible installed partitions (current B1). |
| `-initial-flash-0x0.bin` | Bootloader, partition table, initial OTA data and app merged by IDF. USB initial install at **0x0**, never web OTA. |
| `-flash-metadata.zip` | Individual flash files with build-relative paths, generated `flasher_args.json`, generated and source partition CSVs, and these instructions. Not an OTA upload. |
| `-sdcard.zip` | Contents of `sdcard_bundle/`, with `otr/...` directly at ZIP root. Extract at SD root; back up existing content/saves first. Not a complete music/SF2 collection. |
| `manifest.json` | Exact source SHA, target, board, version, app/OTA sizes, flash offsets/settings. |
| `SHA256SUMS` | SHA256 for every other release asset, including ZIPs and manifest. |

Download assets together and run `sha256sum -c SHA256SUMS`. Example USB command
(replace filenames, version and port; ESP-IDF v6.1's esptool installed):

```sh
python -m esptool --chip esp32s3 --port /dev/ttyACM0 write-flash \
  0x0 MyEsp-OnTheRoad-esp32s3-ES3C35P-vVERSION-initial-flash-0x0.bin
```

Back up internal storage **and** SD before USB flashing. The merged image is an
initial-install image, not a backup: it includes padding and initial OTA data and can
overwrite NVS/settings; partition migrations may make FFat/player/adventure saves
inaccessible or require reformatting. It does not initialize FFat or copy SD content.
Never blindly erase flash for a routine OTA update. Use the actual generated partition
metadata, not old offsets, when diagnosing a device with a different layout.
Secure-boot/flash-encrypted configurations are deliberately rejected by this packager.
ELF/map and build diagnostics stay in the separately named **debug CI artifact** under
the release run's Actions page (subject to repository retention), not in release assets.
Save that artifact externally if long-term symbolication is required.

**Safe reruns / recovery:** publication fails closed on any existing release, including
a draft, and never overwrites assets. An existing tag on the selected SHA is reusable;
a tag on any other SHA (including an annotated tag's resolved commit) aborts and is
**never moved or deleted**. If upload fails after draft creation, inspect the run and
draft. Either finish the upload manually from that run's verified artifact without
replacing existing assets, or delete **only the incomplete draft** and rerun the same
workflow run/jobs on the original SHA. Do not delete the version tag. A published
release is not replaced: use a new version through the normal development process.
If main has moved, use Actions **Re-run all jobs** on the original run rather than a
new dispatch. A failure before tag creation has no tag/release side effects; a failure
between tag and draft creation can leave a correctly pinned tag, which a rerun accepts.

Local non-publishing release-tool tests:

```sh
python3 -m unittest discover -s tests -p test_firmware_release.py
```

After an actual local IDF build, the same packaging checks can be exercised without
GitHub credentials or publication (output must not already exist):

```sh
idf.py -B build_release_esp32s3 -D SDKCONFIG_DEFAULTS=sdkconfig.defaults set-target esp32s3
idf.py -B build_release_esp32s3 build
idf.py -B build_release_esp32s3 merge-bin -o merged.bin
python "$IDF_PATH/components/partition_table/gen_esp32part.py" \
  build_release_esp32s3/partition_table/partition-table.bin build_release_esp32s3/release-partitions.csv
python3 tools/firmware_release.py package --build-dir build_release_esp32s3 \
  --output build_release_esp32s3/release
```

**P4 support is in progress** (see `docs/MULTI_BOARD_TODO.md`). CI builds the real `esp32p4`
target (`P4_43 / esp32p4 / sdkconfig.defaults.esp32p4`) as an artifact labelled
**unverified** until the hardware checks in the plan pass. Releases stay S3-only: the release
tooling only accepts the explicit S3 tuple
`ES3C35P / esp32s3 / sdkconfig.defaults / build_release_esp32s3`, and any later P4 publication
must be explicit, opt-in and separated from verified S3 releases. This workflow publishes no P4 firmware.

Official references: [workflow triggering and GITHUB_TOKEN](https://docs.github.com/en/actions/how-tos/writing-workflows/choosing-when-your-workflow-runs/triggering-a-workflow),
[release API](https://docs.github.com/en/rest/releases/releases),
[Espressif merge-bin implementation](https://github.com/espressif/esp-idf/blob/master/tools/idf_py_actions/serial_ext.py)
and [Espressif CI action](https://github.com/espressif/esp-idf-ci-action).

### Shared components

The `components/my_*` folders have no project-specific code and are meant to be reused in other
ESP32 projects (MyPi-Companion, MyPi-Hotspot). Status, dependencies and the plan for further extractions
(audio, SD/file browser, touch filter, numpad, i18n, story engine) are in [docs/COMPONENTS.md](docs/COMPONENTS.md).

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
| `my_ui` | Theme, fonts (Swedish å/ä/ö), scroll wheel keyboard (`my_charwheel`, `mypi_kbd_core.h`), text input, `Theme::debounce` |
| `otr_engine` | Adventure engine: world/rules/saves/A* (`otr_world`), built-in maps, joystick logic (`otr_joystick.h`); pure C++, host-tested |

### Web pages

Edit files in `main/web/`, then regenerate the embedded header:

```sh
python3 tools/embed_web.py          # writes main/web_assets.h
python3 tools/embed_web.py --check  # CI: fails if web_assets.h is stale
```

### Tests (host)

Adventure v0.21.3 multimap and UI checks (from the repository root):

```sh
mkdir -p build_host_multimap
g++ -std=c++17 -Wall -Wextra -Werror -Imain -Icomponents/otr_engine/include -Icomponents/otr_story/include tests/test_adv_multimap.cpp main/adv_loader.cpp main/adv_sd_session.cpp main/adv_live.cpp components/otr_engine/otr_world.cpp components/otr_story/otr_story.cpp -o build_host_multimap/multimap
build_host_multimap/multimap
python3 tests/test_adv_s2_ui.py build_host_multimap
python3 tests/test_adv_game_layout.py build_host_multimap/layout
python3 tests/test_karaoke_layout.py build_host_multimap/karaoke
```

The UI action harness executes real internal-map portal travel, checks camera,
animation, resource-image and live-map refresh, and prevents an immediate return
portal bounce. The Karaoke harness inspects LVGL's actual knob draw rectangles
at 0/50/100%, including pressed state, rather than unused knob size styles.
It also checks battery percentage placement and colors, unknown readings and
the retained touch area at both landscape sizes and both rotations.

Adventure v0.21.2 resource checks (from the repository root):

```sh
mkdir -p build_host_resources
g++ -std=c++17 -Wall -Wextra -Werror -Imain -Icomponents/otr_engine/include -Icomponents/otr_story/include tests/test_adv_resources.cpp main/adv_loader.cpp main/adv_sd_session.cpp main/adv_live.cpp components/otr_engine/otr_world.cpp components/otr_story/otr_story.cpp -o build_host_resources/resources
build_host_resources/resources
python3 tests/test_adv_s2_ui.py build_host_resources
python3 tests/test_adv_game_layout.py build_host_resources/layout
node tests/test_adv_live_web.js
python3 tools/embed_web.py --check
```

The real-LVGL layout harness requires the pinned `managed_components/lvgl__lvgl`.
It also tests the production PNG decoder, repeated decoding, alpha and malformed files.
The resource harness validates both shipped copies with the production loader,
script and world, then exercises pickup/cap/dialogue, session save/restore,
checkpoint/restart, name-based carry/reset, live JSON and invalid-restore rollback.
The UI harness exercises resource PNG/fallback pixels at both zooms and the
Swedish/English inventory menu. ESP-IDF firmware build and physical ESP32-S3
checks remain open; see the demo README.

Adventure v0.21.1 checks (run from the repository root; no ESP-IDF required):

```sh
g++ -std=c++17 -Wall -Wextra -Icomponents/otr_engine/include tests/test_adv_s4_engine.cpp components/otr_engine/otr_world.cpp -o /tmp/test_adv_s4_engine && /tmp/test_adv_s4_engine
g++ -std=c++17 -Wall -Wextra -Imain -Icomponents/otr_engine/include -Icomponents/otr_story/include tests/test_adv_s4_loader.cpp main/adv_loader.cpp main/adv_sd_session.cpp components/otr_engine/otr_world.cpp components/otr_story/otr_story.cpp -o /tmp/test_adv_s4_loader && /tmp/test_adv_s4_loader
g++ -std=c++17 -Wall -Wextra -Imain -Icomponents/otr_engine/include -Icomponents/otr_story/include tests/test_s4_demo.cpp main/adv_loader.cpp components/otr_engine/otr_world.cpp components/otr_story/otr_story.cpp -o /tmp/test_s4_demo && /tmp/test_s4_demo
python3 tests/test_adv_s2_ui.py /tmp
node tests/test_adv_live_web.js
g++ -std=c++17 -Wall -Wextra -Imain -Itests -Icomponents/otr_engine/include -Icomponents/otr_story/include tests/test_advlive.cpp main/adv_live.cpp components/otr_engine/*.cpp components/otr_story/*.cpp -o /tmp/test_advlive && /tmp/test_advlive
g++ -std=c++17 -Wall -Wextra -Imain -Itools -Icomponents/otr_engine/include -Icomponents/otr_story/include tests/test_adv_catalog.cpp main/adv_catalog.cpp main/adv_sd_session.cpp main/adv_loader.cpp tools/adv_parse.cpp components/otr_engine/otr_world.cpp components/otr_story/otr_story.cpp -o /tmp/test_adv_catalog && /tmp/test_adv_catalog
python3 tools/embed_web.py --check
```

The demo test reads the delivered `.adv` through the production loader and
executes the README sequence; it also checks the SD-bundle copy. See
[the demo checklist](docs/adventures/s4_demo/README.md) for physical checks.

Verification for this change: S4 engine **658**, S4 loader **373**, legacy
engine **1,826**, legacy loader **1,267**, catalog **226**, S3 **325**, live JSON **116** checks
passed. Engine/loader/UI extraction also passed ASan/UBSan. S2 and S3 external
demo regressions passed; the S4 solution finishes both delivered copies in
**179 inputs / 175 steps**, with **58/64 energy** and no lives lost.
The web canvas was checked in a browser at desktop/mobile sizes, and the
production LVGL layout passed three screen sizes. Firmware is not
locally build-tested here (no ESP-IDF); physical device verification remains open.
CodeQL found no C++/Python alerts. Its `js/bad-tag-filter` alert in the local
web test is a false positive: the regex extracts trusted, versioned JavaScript
for tests; it does not sanitize HTML or process user input.

Adventure v0.21.0 checks (run from the repository root; binaries stay in `/tmp`):

```sh
g++ -std=c++17 -Wall -Wextra -Imain -Icomponents/otr_engine/include -Icomponents/otr_story/include tests/test_adv_s3.cpp main/adv_loader.cpp main/adv_sd_session.cpp components/otr_engine/otr_world.cpp components/otr_story/otr_story.cpp -o /tmp/test_adv_s3 && /tmp/test_adv_s3
g++ -std=c++17 -Wall -Wextra -Imain -Icomponents/otr_engine/include -Icomponents/otr_story/include tests/test_s3_demo.cpp main/adv_loader.cpp components/otr_engine/otr_world.cpp components/otr_story/otr_story.cpp -o /tmp/test_s3_demo && /tmp/test_s3_demo
python3 tests/test_adv_s2_ui.py /tmp
```

The demo test reads the delivered `.adv` files through the production loader and
executes the solution in its README, including a save/restore checkpoint.
These host checks do not replace an ESP-IDF firmware build or physical testing.

```sh
INC="-Imain -Icomponents/my_wifi/include -Icomponents/my_ui/include"
g++ -std=c++17 -Wall -Wextra $INC tests/test_yatzy.cpp main/game_yatzy.cpp -o tests/test_yatzy && ./tests/test_yatzy
g++ -std=c++17 -Wall -Wextra $INC tests/test_core.cpp -o tests/test_core && ./tests/test_core   # net list, scroll wheel, player registry
g++ -std=c++17 -Wall -Wextra -Icomponents/my_rtc/include tests/test_rtc.cpp -o tests/test_rtc && ./tests/test_rtc   # DS3231 date format
python3 tests/test_board_peripherals.py   # P4 internal RTC, S3/P4 ADC/SD recovery and per-board NVS calibration defaults
python3 tests/test_upload_pipeline.py   # queued writes abort when another SD session requests recovery
python3 tests/test_sd_log_diagnostics.py   # runtime levels, SD persistence and RAM-only export
python3 tests/test_flash_repair.py   # explicit repair after a failed FFat mount; never automatic formatting
python3 tests/test_ota_stream.py   # incremental erase, progress and failed validation leave the boot slot unchanged
python3 tests/test_screen_power.py   # S3/P4 idle/manual off, wake-only touch + 250 ms, OTA and brightness restoration
python3 tests/test_text_size_layout.py   # boot/Wi-Fi layouts on 320x480 and 480x800 for every text size; font role switching
node tests/test_board_diagnostics_web.js   # S3/P4 overview, RTC waiting and separate SD/FFat status
g++ -std=c++17 -Wall -Wextra -Imain tests/test_cup.cpp main/dice_cup.cpp -o tests/test_cup && ./tests/test_cup   # dice cup gesture/physics
g++ -std=c++17 -Wall -Wextra -Imain tests/test_games.cpp main/game_pig.cpp main/game_farkle.cpp main/game_scc.cpp -o tests/test_games && ./tests/test_games   # Pig / Farkle / Ship-Captain-Crew
g++ -std=c++17 -Wall -Wextra -Icomponents/otr_engine/include tests/test_adventure.cpp components/otr_engine/otr_world.cpp components/otr_engine/otr_maps.cpp -o tests/test_adventure && ./tests/test_adventure   # adventure engine
g++ -std=c++17 -Wall -Wextra -Imain -Imain/lang tests/test_lang.cpp main/lang/lang.cpp main/lang/lang_sv.cpp main/lang/lang_en.cpp -o tests/test_lang && ./tests/test_lang   # string tables
g++ -std=c++17 -Wall -Wextra -Imain tests/test_battery.cpp -o tests/test_battery && ./tests/test_battery   # battery scaling, estimated charge, alert timing/filter/gating
g++ -std=c++17 -Wall -Wextra -Werror -Itests/battery_stubs -Imain -Icomponents/my_wifi/include -Imain/lang tests/test_battery_config.cpp main/app_config.cpp main/lang/lang.cpp main/lang/lang_sv.cpp main/lang/lang_en.cpp -o tests/test_battery_config && ./tests/test_battery_config   # battery and karaoke NVS defaults/sanitizing
g++ -std=c++17 -Wall -Wextra -Imain tests/test_karaoke_geometry.cpp -o tests/test_karaoke_geometry && ./tests/test_karaoke_geometry   # volume rail bounds
g++ -std=c++17 -Wall -Wextra -Imain -Itests/stub tests/test_live.cpp main/game_live.cpp main/game_yatzy.cpp main/game_pig.cpp main/game_farkle.cpp main/game_scc.cpp -o tests/test_live && ./tests/test_live   # web seat release
g++ -std=c++17 -Wall -Wextra -Imain tests/test_multiplayer_protocol.cpp -o /tmp/test_multiplayer_protocol && /tmp/test_multiplayer_protocol   # ESP discovery capability and joinable-seat rules
g++ -std=c++17 -Wall -Wextra -Imain -Itests tests/test_audio.cpp main/audio_synth.cpp main/sound_bank.cpp -o tests/test_audio && ./tests/test_audio   # synth, MML, WAV parsing
g++ -std=c++17 -Wall -Wextra -Imain -Itests tests/test_fspath.cpp main/fs_path.cpp -o tests/test_fspath && ./tests/test_fspath   # file browser path safety
g++ -std=c++17 -Wall -Wextra -Werror -Imain -Itests tests/test_fs_upload.cpp main/fs_upload.cpp -o /tmp/test_fs_upload && /tmp/test_fs_upload   # safe upload staging and replacement
node tests/test_files_upload_web.js   # conflict defaults, directory depth, serialized SD/flash uploads
g++ -std=c++17 -Wall -Wextra -Icomponents/otr_engine/include -Itests tests/test_joystick.cpp -o tests/test_joystick && ./tests/test_joystick   # joystick zones/rate/diagonal
g++ -std=c++17 -Wall -Wextra -Icomponents/otr_story/include -Itests tests/test_story.cpp components/otr_story/*.cpp -o tests/test_story && ./tests/test_story   # dialogue engine + village script
g++ -std=c++17 -Wall -Wextra -Imain -Itests -Icomponents/otr_engine/include -Icomponents/otr_story/include tests/test_advlive.cpp main/adv_live.cpp components/otr_engine/*.cpp components/otr_story/*.cpp -o tests/test_advlive && ./tests/test_advlive   # adventure live snapshot + JSON
g++ -std=c++17 -Wall -Wextra -Icomponents/otr_midi/include -Itests tests/test_midi.cpp components/otr_midi/*.cpp -o tests/test_midi && ./tests/test_midi [file.mid ...]   # MIDI/KAR parser, lyrics, melody, registry
g++ -std=c++17 -Wall -Wextra -O2 -Icomponents/otr_midi/include -Icomponents/otr_synth/include -Itests tests/test_sfsynth.cpp components/otr_synth/otr_synth.cpp components/otr_midi/*.cpp -o tests/test_sfsynth && ./tests/test_sfsynth font.sf2 song.mid [out.wav]   # SF2 synth render
```

With the project's LVGL dependency available in `managed_components/lvgl__lvgl`, run
`python3 tests/test_karaoke_layout.py /tmp/karaoke-layout-test` from the repository root
for the production Karaoke UI, real TextInput and MIDI/index at logical 480×320 and 800×480
in Swedish/English, using the production 256 KiB LVGL heap (engine/hardware stubbed).
It covers flip, page next/back, scrolling to the last A–Ö key (minimum 44×42 px targets),
modal-hit search, real keyboard CANCEL/READY callbacks and all keyboard child bounds,
minus/plus on all steppers, battery status/navigation bounds, drag/debounce volume persistence, lyrics/seek/button separation,
last track, long messages, lyrics/no lyrics and actual-font label fit.
See the [Phase D results](docs/ui-migration-inventory.md#körda-d-kontroller-och-verifieringsgräns)
for exact recorded commands, results and remaining physical checks.

Run
`python3 tests/test_yatzy_layout.py` for host-side variants/players/picker, Yatzy game screen/dialogs,
web-turn overlay and PIN numpad layout and interaction
checks at 320×480, 480×800 and 800×480 in Swedish and English (CMake/Ninja required, no hardware).
This includes long/scrollable dialog and overlay text, PIN retry/cancel/confirmation, and picker scrolling.
Game-screen checks cover roll/result transitions, responsive controls and score-table scrolling,
long player names, dice placement and cup visual/touch alignment, with one/eight players,
Yatzy/Maxi, normal/ordered modes and saved rolls. Hardware and roll execution are stubbed.

Manual UI regression checklist (run on 320×480 and a larger portrait/landscape layout, in Swedish and English):

- [ ] Dialogs: guest web-seat info and game-menu quit/continue stay on-screen; buttons work and long text scrolls.
- [ ] Web overlay: waiting/connected text is readable, local game input stays blocked, and the local-takeover button works when disconnected.
- [ ] Numpad: open from new-player and web-seat flows; enter four digits, backspace, retry after error, confirm and cancel; check centering and touch targets.
- [ ] Picker: empty/full/filtered lists, long names, scroll to last row, select/change player, clear search and return without overlaps.

Game-screen layout (UI step 5, firmware 0.19.1) derives header, felt/nest, suggestions, controls,
cup placement and score-table bounds from the active display and `ui_metrics`. The 320×480
baseline geometry is preserved; taller layouts gain playing/table space and wider layouts gain
control, suggestion and player-column space. Long player names stay on one line clear of round status.

Manual game-screen checklist (hardware verification still required, at 320×480 and a larger
portrait/landscape layout, in Swedish and English):

- [ ] Yatzy, Maxi, ordered and Midi: start with one/eight players; verify player/round/menu header, including long names and later rounds.
- [ ] Cup: tap to roll, shake/swipe to throw, tidy/reopen; verify visuals and touch targets align.
- [ ] Dice: tap/swipe/drag to hold/release; verify free/held dice remain in the correct zones.
- [ ] Suggestions and controls: choose a suggestion or “choose”, return to rolling, use the last roll, select/write a score; verify unchanged turn flow and no clipped controls.
- [ ] Result table: scroll vertically/horizontally to the last row/player; verify selection, highlighting and write-button expansion when no rolls remain.
- [ ] Web turn: verify the overlay blocks all local game input below the header, menu remains available, and local takeover works after disconnect.

These refinements only change on-device UI layout. Game rules/engines, scoring, dice physics and
gesture thresholds, web commands/protocols, network flows and data formats/compatibility are unchanged.
Mini-game/result screens, theme redesign and P4 hardware support are outside this step.

## Flashing and updating

- **To v0.12.0 (partition layout B1) – one time, over USB:** flash `MyEsp_OnTheRoad-v0.12.0-merged.bin` at
  offset `0x0` (esptool.js / esptool). OTA cannot change the partition table. The FFat partition moves and is
  formatted on first boot (a few seconds), so `players.bin` and adventure saves on FFat are lost; the merged
  image also resets NVS (settings, saved networks). Files on the microSD card are not touched.
- **First time, and from Arduino builds (≤ v0.3.x) to v0.4.0:** flash `MyEsp_OnTheRoad-vX.Y.Z-merged.bin`
  at offset `0x0` over USB (esptool.js / esptool). This also installs the ESP-IDF bootloader needed for
  OTA rollback. The partition table is unchanged, so the FFat players file is kept; the merged image does
  overwrite the NVS area (settings and saved networks are reset). To keep NVS, flash the separate files
  instead: bootloader `0x0`, partition table `0x8000`, `ota_data_initial.bin` `0xe000`, app `0x10000`.
  (Applies to v0.4.0–v0.11.x only; from v0.12.0 the table differs, see above.)
- **After that:** open `http://ontheroad.local/update` and upload `MyEsp_OnTheRoad-vX.Y.Z-ota.bin`.
  Settings, saved networks and players are kept.

## Web endpoints

| Method | Path | Description |
|---|---|---|
| GET | `/`, `/game`, `/adventure`, `/play`, `/players`, `/settings`, `/diag`, `/update` | Web pages |
| GET | `/api/info` | System, display, touch, Wi-Fi, clock, RTC, OTA slots, storage (JSON) |
| GET | `/api/game` | Live game snapshot (read-only); `?v=<version>` answers `{"same":true}` when unchanged. Also `code`, `web_players` (`[{player,id,connected}]`), `lang` |
| GET | `/api/adventure` | Adventure live snapshot (read-only). `?v=<version>` answers `{"same":true}` when unchanged, `?mr=<map_rev>` omits the map when the client already has it, `{"active":false}` when no adventure runs, `?brief=1` = only `active`/`version`/`world` |
| WS | `/ws/game` | Web play. Client: `{"cmd":"join","code":1234,"player":n}` (no `player` = spectator), `{"cmd":"hold","die":n}`, `{"cmd":"roll"}`, `{"cmd":"choose","cat":id}`. Device: `joined`, `error` (`code/seat/join/full/cmd/turn`), `state` (= `/api/game` + `you`, `joined`) on every change |
| GET / POST | `/api/config` | Read / update settings (NVS); `tz`, `lang` (`sv`/`en`, list in `langs`), `admin_pin_new` (+ `pin`) |
| POST | `/api/wifi` | Connect to `ssid`/`pass` (added to the saved list on success) |
| GET | `/api/scan` | Wi-Fi scan (async) |
| GET / POST | `/api/nets` | Saved networks (never passwords); `action` = add / remove / move (`delta`) / forget_all |
| POST | `/api/time` | Set the clock manually (`epoch`, UTC seconds) |
| GET / POST | `/api/players` | Player registry; `action` = add (`name`) / rename (`id`, `name`) / remove (`id`, admin `pin`) |
| POST | `/api/ota` | Firmware upload (multipart, `?size=&pin=`) |
| POST | `/api/reboot`, `/api/reset` | Reboot / factory reset settings (reset needs the admin PIN) |
| POST | `/api/diag/crash` | Deliberate test crash, `kind=panic` (abort) or `kind=wdt` (interrupt watchdog); admin `pin` |

## Project layout

```
main/                 Application: app_main, app_config, app_players, app_web, board_* drivers,
                      ui_* screens, game_yatzy, player_db, board_pins.h, web_assets.h
main/lang/            UI strings: lang.h (ids, tr), lang.cpp, lang_sv.cpp, lang_en.cpp
main/web/             Web page sources (HTML/CSS)
components/my_*/      Shared reusable components (see above)
components/otr_engine/ OnTheRoad Engine: tile world logic + built-in test map (pure C++, host tested)
docs/                 Hardware reference, game architecture, 2D engine brief, reusable components (COMPONENTS.md),
                      Bible adventure plan (BIBLE_ADVENTURE_PLAN.md)
tests/                Host unit tests (games, saved networks, scroll wheel, players, RTC, dice cup, strings, web seats,
                      audio synth, file paths, adventure engine, joystick)
tools/embed_web.py    main/web/ -> main/web_assets.h generator
tools/lvgl8to9.py     Helper used for the LVGL 8 -> 9 API rename
partitions.csv        B1: nvs 20 kB, otadata, app0/app1 5 MB each, ffat 5.9 MB, coredump 64 kB
```

## Roadmap

See [ROADMAP.md](ROADMAP.md).
