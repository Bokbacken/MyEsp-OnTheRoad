# MyEsp-OnTheRoad

A pocket dice-game (Yatzy, Maxi-Yatzy, Pig, Farkle, Ship-Captain-Crew) and tile-adventure console for the road, built on the **Heemol ESP32-S3 3.5"** touch board
(ST77922 QSPI 320×480 + capacitive touch, WS2812 LED). **ESP-IDF 6.1** project with **LVGL 9.6**,
Wi-Fi station mode (on-device network setup) and a web UI for status, settings and diagnostics.

> **Status:** v0.18.5 – **SD-card adventures with darkness, flashlight and pogo stick**.
> Adventures now load as `.adv` text files from the SD card (`/otr/adventure`); see
> [docs/AUTHORING_ADVENTURES.md](docs/AUTHORING_ADVENTURES.md). A map can mark **dark areas**
> (`@dark`) that hide their contents until a **flashlight** (glyph `f`, levels 1–3) lights a cone
> ahead – reach equals the level (1/2/3 cells), with the leading rows three wide. A **pogo stick**
> (glyph `j`, levels 1–3) vaults over 1–N blocking cells; jumping is **manual** via a **split
> action button** whose top third (**Byt/Swap**) cycles the actions available right now – **Slå**
> (hit), **Prata** (talk) and **Hoppa** (jump) – and whose lower two-thirds performs the chosen one.
> Both items are permanent and carry across all maps in an adventure. `@treasures` on the same cell
> as a map pickup glyph takes precedence, so `f`/`j` can be raised to level 2–3. Saves are version 3
> (legacy v1/v2 load with the new abilities at 0). **Hardware-tested by Kent** (darkness/flashlight/
> pogo rendering, the flashlight cone, manual jump via the split button, and `@treasures` precedence);
> other parts are host-tested and build-clean only. Karaoke is unchanged and paused. Existing B1
> partitions: normal OTA from v0.12+.
>
> Previous: v0.17.0 – **adventures moved to the SD card** (`.adv` format) with a catalog
> (folders = adventures, `NN_` prefix = order, up to 16 maps each), chained maps with carry/reset,
> an exported 12-stage MineSweeper campaign, and data-driven true minesweeper (`mine_level`).
> The on-device and host parsers are twins (`main/adv_loader` / `tools/adv_parse`). **Not hardware-tested** at release.
>
> Previous: v0.16.1 – **Joseph, chapter 1** (Genesis 37–50): two maps, six characters, bilingual
> SV/EN dialogue, with UTF-8 (åäö) and map/choice-flow fixes. Hardware-tested by Kent ("the game works").
>
> Previous: v0.15.0.2 – portrait (320×480) adventure dialogs, on-device new-player creation,
> and conservative profile-storage error handling. Host tests and firmware build pass;
> **independent layout review pending, not hardware-tested**. The reported intermittent profile loss
> on the user's device is not diagnosed. Automatic FFat formatting is disabled; blank/unreadable
> storage is left untouched. Karaoke is unchanged.
>
> v0.15.0 – **adventure map travel and MineSweeper campaign**. The Village exit leads to
> the Meadow and back, preserving map changes and story state. Version 2 saves bundle both maps, inventory,
> story and campaign progress; legacy v1 saves remain readable. MineSweeper has 12 fixed stages, a findable
> one-hit shield, and one 3×3 / one 4×4 bomb per stage with preview, confirmation and cancellation.
> Read-only `/adventure` mirrors the new state. **Not hardware-tested.** Karaoke is unchanged and paused;
> no Joseph chapter or quest-log expansion is included. Existing B1 partitions: normal OTA from v0.12+.
>
> Previous: v0.13.7 – **song library with folders**: karaoke songs are scanned recursively (up to 2 000, cached
> in `songs.idx`), browsed by folder / search / A–Z on the device and picked from a phone at `/karaoke/songs`. The file
> manager (`/files`) gets multi-select, bulk delete/move, paging, sorting, new folders and drag-and-drop upload; the
> diagnostics page shows a live log (SSE) and downloads all logs as a ZIP (`/otr/logs/otr_{0,1,2}.log` on SD). No
> automatic file sorting. **Not hardware-tested yet.**
>
> Previous: v0.13.6 – **countdown dots**: pauses of 3 s or more between lyric lines (and long intros) show up to
> 8 dots, white at first, lighting one per second in the sung-syllable colour so the last one lights as the next line
> starts – on the device screen and on the `/karaoke` web page (new `countdown` frame, sample-synced). **Not
> hardware-tested yet.**
>
> Previous: v0.13.5 – **web karaoke**: open `http://<device>/karaoke` on a phone, tap Connect and the karaoke
> plays in the browser. The device streams 22 050 Hz mono 16-bit PCM over the WebSocket `/ws/karaoke` (1 024-byte
> frames) with the lyrics as JSON text frames placed at the exact sample, so the page highlights each syllable as it
> is heard (0.3 s buffer). While a browser is connected and a song is open the device speaker is muted automatically
> (ES8311 DAC volume 0) and restored when the last listener leaves. Max 3 listeners; NVS `kara_webstream=0` disables
> the endpoint. **Not hardware-tested yet.**
>
> Previous: v0.13.4 – **resampling to the output rate**: every sample above 22 050 Hz (28/30.5/32/44.1/48 kHz)
> is resampled to the output rate while it is read from the SD card (polyphase Kaiser-windowed sinc, 75 dB stopband;
> loop lengths are rounded and the zone's pitch is compensated exactly). The ±12 key margin is gone: the synth loads
> exactly the keys the song plays at the current transpose and reloads in the background when the key changes
> (keeping the previous key if the new one does not fit). The per-song budget is now the largest free PSRAM block
> minus 512 kB, and the game sound-clip cache is freed first. Host-measured: GeneralUser-GS + Ainbusk 6.03 → 3.10 MB,
> with Alan Parsons "Time" 7.60 → 4.64 MB, Jnsgm2 + "Time" 6.91 → 3.85 MB – all fit now. Dropout diagnostics: every 5 s
> the log shows render load (avg/peak), underruns and voices; NVS `kara_voices` (16–32, default 32) caps the voices.
> **Not hardware-tested yet.**
>
> Previous: v0.13.3 – **halving of high-rate samples**: when a song is prepared, samples recorded at ≥ 1.8 × the
> output rate (44.1/48 kHz) are decimated 2:1 with a 47-tap half-band FIR while they are read from the SD card
> (loops keep their pitch; short odd loops stay at full rate). Saves up to ~1.4 MB per song with large GM banks
> (e.g. eawpats 4.3 → 3.0 MB); banks already at 22 050 Hz are unchanged. GeneralUser-GS still cannot fit two of the
> five test songs (their samples are 28–32 kHz). `Synth::setHalving(false)` turns it off. **Not hardware-tested yet.**
>
> Previous: v0.13.2 – **selective SoundFont loading + reverb/chorus**: opening a bank now reads only its structure
> (hydra); when a song is opened the synth scans it (presets, keys ±12 so transposing never reloads, velocities,
> bank select and drum channel exactly as TinySoundFont picks zones) and reads only those sample ranges from the SD
> card into PSRAM (budget 6 MB per song). Banks larger than PSRAM work – the 7 MB limit is gone (GeneralUser-GS,
> 31 MB, tested on the host). Over budget the synth keeps only the velocity layer holding 100, then one zone per
> key; if it still does not fit the song shows "Song needs too much sample data". New `otr_fx`: Freeverb-style
> reverb and a chorus as mono send effects, per channel via CC91/CC93 (GM defaults 40/0), on at 50 % (NVS, no UI
> controls yet). Host-measured: 0.1–0.8 MB per song with 2–4 MB GM banks, 3.5–4.8 MB with GeneralUser-GS (two of
> five test songs do not fit); selective output is bit-identical to a full load. Selective loading idea from
> copych/ESP32_SF2_Sampler_Synthesizer (MIT). Same partitions → plain OTA. **Not hardware-tested yet.**
> v0.13.1 – **karaoke in landscape + multiple SoundFonts**: the karaoke module turns the display to
> landscape (480×320, LVGL software rotation in the flush, touch mapped by LVGL; portrait and the 180° setting are
> restored when leaving) so whole lyric lines fit; list, player and sheets re-laid out for 480 px. Several `.sf2`
> files can live in `/otr/sf2`: pick a global default (saved in NVS) from the song list and optionally a per-song
> sound bank from the player (saved as `sf2=<file>` in `karaoke.reg`, old lines still valid). The font is reloaded
> in the background when a song wants another one (only one font in PSRAM); the active font name is shown. Files
> are validated: SF3/compressed, non-RIFF/sfbk and > 7 MB (limit removed in v0.13.2) are rejected with a clear message. Entering karaoke with
> sound off asks "Turn sound on?" (yes/no; yes saves the setting). Adventure live view (`/adventure`) now scales to
> the browser window: map and info side by side in wide/landscape windows (≥ 800 px), info below the map in
> portrait, plus a size selector (fit / small / medium / large, saved in the browser). Same partitions → plain OTA.
> **Not hardware-tested yet.**
> v0.13.0 – **karaoke mode**: new pure-C++ components `otr_midi` (SMF/.kar parser, lyrics with
> syllable timing, melody-track detection, sequencer, per-song registry; 102 host tests) and `otr_synth`
> (vendored TinySoundFont, patched for 16-bit samples in PSRAM, float filters, fixed-point mono render and voice
> stealing, max 32 voices). A new gold **Karaoke** card in the menu: song list from `/otr/karaoke`, lyrics on the
> device with word-by-word highlighting (previous/current/next line), play/pause, seek, tempo 50–150 %, key ±12,
> volume, melody on/off (sing it yourself) and a melody-track picker. Tempo, key and melody track are saved per
> song in `karaoke.reg` (keyed by CRC32 + size, survives renames). The synth runs in the audio mixer task through
> a new external-source hook (dice/adventure music is paused, not lost). A SoundFont (`.sf2`, max 7 MB) is needed
> in `/otr/sf2` for sound – without one the lyrics play silently. Same B1 partitions as v0.12.0 → plain OTA.
> **Not hardware-tested yet.** Microphone + scoring is planned next ([docs/MUSIC_KARAOKE_PLAN.md](docs/MUSIC_KARAOKE_PLAN.md)).
> v0.12.0 – new partition layout **B1**: 2 × 5 MB app slots (OTA) + 5.9 MB FFat (was 2 × 3 MB +
> 9 MB), and the firmware is now built with size optimisation (`-Os`): 1.77 MB, 34 % of a slot.
> **Requires a one-time USB flash of the merged image; FFat is reformatted (players and adventure saves on FFat are
> lost).** Not hardware-tested yet.
> v0.11.1 – adventure live view: new page `/adventure` shows the running adventure read-only
> (canvas map with player, people, items and gates, HUD with energy/lives/keys/gems/items, and the current
> dialogue with speaker, full text, reference and choices), fed by a separate `AdvLive` snapshot via
> `/api/adventure` (map only sent when its revision changes). Links from the overview and `/game`.
> **Not hardware-tested yet.**
> v0.11.0 – conversations in the adventure: new pure-C++ component `otr_story` (dialogue
> trees, conditions, effects, flags, items, quests; 182 host tests), people on the map (`Obj::NPC`), the
> Hit button turns into **Talk** when facing a person, tap a person to walk there and talk, dialogue box
> with portrait + typewriter text and up to 4 answer buttons, demo world "Byn" (the village) with three
> people. Story state is saved as `.sts` next to the world save. Hardware-tested by Kent: the village and
> the dialogues work very well.
> Before that (`40ea276`) – touch release filter against double taps, 500 ms
> guard on every toggle, new adventure HUD (icon + count rows left, carried items right, quick sound on/off,
> zoom in the hamburger menu); not hardware-tested yet. Next: a Bible adventure with conversations
> ([docs/BIBLE_ADVENTURE_PLAN.md](docs/BIBLE_ADVENTURE_PLAN.md)).
> v0.10.0 – adventure control panel without a top bar, half walking speed (300/150 ms), 30° straight joystick
> sectors, lava tie-break fix in the Pro pathfinder (bridge bug), SD/boot status on the web overview.
> Hardware-tested by Kent (v0.9.0/v0.10.0): sound, SD card, file browser, adventure saves, joystick feel and
> speed, diagonal sectors, bridge fix, web SD status, zoom debounce.
> v0.9.0 – sound (ES8311, chiptune + WAV), microSD with web file browser, adventure saves on SD and an
> analog-style joystick (hardware-tested). v0.8.0 – adventure engine with tap-to-walk, energy/lives,
> Minfältet and per-player saves (saves hardware-tested). Earlier: v0.5.6 – 180° screen rotation (setting on device and web; MADCTL MX|MY + mirrored touch),
> personal 4-digit player PINs (player file v2 with automatic v1 migration, on-device numeric keypad),
> and a new `/play` flow: pick your name, enter your PIN (no shared game code any more; web seats are
> bound to an internal session id, with a 5-attempt lockout). The overview page links to Live and Play.
> v0.5.6 builds with 0 errors / 0 warnings and passes the host tests, but is **not yet tested on
> hardware** (the rotation via MADCTL in particular). v0.5.5 web play, v0.5.0's RTC and sprite dice
> animation and v0.2.0 are hardware-verified. See [ROADMAP.md](ROADMAP.md).

## Features

- **Karaoke (v0.13.0/v0.13.1, not hardware-tested)** – landscape screen, multiple SoundFonts (global default + per song), plays `.mid`/`.kar` from the SD card through a SoundFont synth
  (TinySoundFont) with the lyrics on screen, word-by-word highlighting, tempo/key/volume, melody on/off and a
  per-song melody-track choice stored in `karaoke.reg`. Silent text-only mode without a SoundFont.
- **Sound (v0.9.0, hardware-tested)** – ES8311 codec + amplifier, mixer task with 1 music + 4 effect
  channels, built-in chiptune synth (square/triangle/noise, ADSR, MML tunes). Any sound can be replaced by a WAV on
  the SD card. Volumes and on/off on the device and in the web settings.
- **microSD + file browser (v0.9.0, hardware-tested)** – 4-bit SDMMC (1-bit fallback), hot-plug tolerant.
  `/files.html`: browse the whole card (or the internal flash), download, streamed upload, delete, rename, new
  folder; changes require the admin PIN.

### SD card layout (optional)

```
/sdcard/otr/sounds/<name>.wav       effect override: click dice_shake dice_roll die_land score zero yatzy bust win
                                    step bump push hit break gem key potion heart life door locked mine splash hurt
                                    life_lost game_over checkpoint exit
/sdcard/otr/music/adventure.wav     looping music (adventure); dice.wav = menus and dice games
/sdcard/otr/adventure/saves/        adventure saves (fallback: internal flash)
/sdcard/otr/adventure/maps/         reserved for future maps
/sdcard/otr/karaoke/                karaoke songs (.mid / .kar, max 1 MB each); "Artist_Title.mid" gives artist + title
/sdcard/otr/karaoke/karaoke.reg     per-song settings (melody track, key, tempo) – created automatically, plain text
/sdcard/otr/sf2/                    SoundFonts for karaoke (.sf2 v2, any size since v0.13.2 – max 6 MB of samples per song, several allowed): per-song choice, else the default chosen on the device, else default.sf2, else the first valid .sf2
```

SoundFont: not included (licence and size). A good fit is **TimGM6mb.sf2** (~5.7 MB, GPL v2):
<https://github.com/arbruijn/TimGM6mb/raw/master/TimGM6mb.sf2> – upload it to `/otr/sf2/` with the web file browser.
Since v0.13.2 only its structure is read when the karaoke menu opens; the samples a song needs are loaded when the
song is opened and everything is freed when you leave.

WAV: PCM 8 or 16 bit, mono or stereo, 8-44.1 kHz (mixed to 22 050 Hz mono). Effects are cached in PSRAM
(max ~2 MB total); music is streamed. Missing file = built-in tone version.

- **Adventure HUD (v0.10.x, not yet hardware-tested)** – bottom panel: left icon + count rows (lives, suit, keys,
  gems; icons are 20 px copies of the world sprites), right the Slå button, energy bar and icons of carried items
  (boat; room for two rows), bottom-left hamburger menu + quick sound on/off. Zoom, sound and Pro/Assist live in
  the menu.
- **Double-tap protection (v0.10.x)** – the touch driver only reports a release after the finger has been up
  60 ms (the controller can drop a held finger for a read or two, which made buttons fire twice; filtered gaps
  are counted on the diagnostics page), and every toggle has a 500 ms guard (`Theme::debounce`).
- **Adventure engine (v0.8.0)** – separate module (`components/otr_engine`
  + `main/ui_adventure.cpp`): own tile renderer on a PSRAM canvas, smooth camera, 32/16 px zoom. Two worlds:
  the 60 × 60 *Testvärlden* (rocks, keys/doors, boat, lava, plates/gates) and the minesweeper-style
  *Minfältet* (fog, number hints, flood reveal, flags). Tap-to-walk with A* (Pro / Assistent mode, the
  assistant stops before deadly lava, unknown ground or water without boat), arrow pad with STOP, energy
  bar, potions/hearts, lives (finite or infinite), protective suit, hidden treasures under crates/bushes/
  walls/rocks/doors, and per-player saves on FFat (Continue / New game). See
  [docs/ENGINE_2D_BRIEF.md](docs/ENGINE_2D_BRIEF.md).
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
- **Web play (v0.5.2, PIN flow v0.5.6)** – in the player setup each registered player can be marked as
  a web player (Wi-Fi button); a player without a personal PIN is asked for one on the numeric keypad
  first (guests cannot play via the web). On a web player's turn the console shows "Väntar på X via
  webben..." and blocks local input; the phone opens `http://<device-ip>/play`, picks its name, enters its
  4-digit PIN and holds dice, rolls (the console runs the normal throw animation) and writes a box. There
  is no shared game code: seats are bound to an internal session id that changes with every new game.
  Protocol on the WebSocket `/ws/game` (max 4 clients, no polling): `{"cmd":"join","player":n,"pin":"1234"}`,
  errors `nogame|seat|pin|nopin|locked`; 5 wrong PINs per connection or per seat lock for 60 s. PINs are
  never sent out (`has_pin` only) and can also be managed on `/players`.
- **Screen rotation (v0.5.6)** – 0° (USB down) or 180°, set on the device or via `/api/config`
  (`rotation`); implemented with MADCTL MX|MY and mirrored touch coordinates.
- **Clock** – NTP (pool.ntp.org, time.google.com, time.cloudflare.com) when online, POSIX time zone
  (default Swedish CET/CEST), manual setting on the device (Inställningar → Klocka) or from the browser.
  The last time is kept in NVS across reboots and shown as uncertain (orange "?") until NTP or a manual set.
  With the optional **DS3231 RTC** the clock is set from the RTC at boot (source "RTC", certain); the RTC
  keeps UTC and is rewritten after every NTP sync / manual set when it drifts ≥ 1 s. Log lines carry
  uptime and are shown in real time as soon as the clock is certain (also lines from before that point).
- **Live view** – `/game` shows the running game read-only (dice, kept dice, full score sheet, winner,
  "skakar bägaren..." while the cup is shaken),
  polling `/api/game` every 700 ms; handy on a phone or TV next to the device.
- **Adventure live view (v0.11.1)** – `/adventure` shows the running adventure read-only: canvas map
  (whole map scaled to fit, or follow the player), people, items, gates, the player with facing, a HUD and the
  current dialogue (speaker, full text, reference, choices with locked hints). Polls `/api/adventure` ~4–5×/s;
  the map is only resent when its revision changes. `/game` shows a hint while an adventure is running.
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

microSD (4-bit SDMMC) and audio (ES8311 + amplifier) are used since v0.9.0; see the hardware reference for pins.

**RTC module:** the Raspberry Pi "DS3231 for Pi" board (5-pin header marked `- NC C D +`) is wired as
`-` → GND, `NC` → not connected, `C` (SCL) → IO39, `D` (SDA) → IO38, `+` → **3V3** (not 5 V – the board's
pull-ups go to its supply). These modules have no charging circuit, so a CR1220/LIR1220 coin cell simply
lasts until it is empty; the boot checklist shows "tid ogiltig (batteri?)" when the time was lost.

### Hardware profiles

The board is selected at build time in `idf.py menuconfig` -> *OnTheRoad hardware profile*
(`CONFIG_OTR_HW_PROFILE_*`). `main/hw_profile.*` exposes the active profile (name, logical
display size, touch/audio/SD capabilities). The default is **ES3C35P**, the current 320x480
board, so existing builds behave as before. `P4_43` (Waveshare ESP32-P4 4.3", 480x800) is only a
placeholder and is not ported or verified yet. `main/ui_metrics.*` holds shared UI sizes
(margins, button sizes, card spacing) derived from the live display size; the main menu now uses
an LVGL flex layout built on them. This is a first step towards more screen profiles - the other
screens still use fixed 320x480 coordinates.

## Building

Requires **ESP-IDF 6.1** (`idf.py`). Managed components (LVGL 9.6.0, mdns, cJSON, led_strip) are
fetched automatically on the first build (`dependencies.lock` pins the versions). Board settings
(16 MB flash, OPI PSRAM, custom partition table, app rollback, `-Os`) are in `sdkconfig.defaults`.

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
idf.py build                      # build/MyEsp_OnTheRoad.bin (= -ota.bin)
idf.py merge-bin -o merged.bin    # build/merged.bin (= -merged.bin, flash at 0x0)
idf.py -p /dev/ttyACM0 flash monitor
```

The firmware version comes from `PROJECT_VER` in the top-level `CMakeLists.txt`.

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

```sh
INC="-Imain -Icomponents/my_wifi/include -Icomponents/my_ui/include"
g++ -std=c++17 -Wall -Wextra $INC tests/test_yatzy.cpp main/game_yatzy.cpp -o tests/test_yatzy && ./tests/test_yatzy
g++ -std=c++17 -Wall -Wextra $INC tests/test_core.cpp -o tests/test_core && ./tests/test_core   # net list, scroll wheel, player registry
g++ -std=c++17 -Wall -Wextra -Icomponents/my_rtc/include tests/test_rtc.cpp -o tests/test_rtc && ./tests/test_rtc   # DS3231 date format
g++ -std=c++17 -Wall -Wextra -Imain tests/test_cup.cpp main/dice_cup.cpp -o tests/test_cup && ./tests/test_cup   # dice cup gesture/physics
g++ -std=c++17 -Wall -Wextra -Imain tests/test_games.cpp main/game_pig.cpp main/game_farkle.cpp main/game_scc.cpp -o tests/test_games && ./tests/test_games   # Pig / Farkle / Ship-Captain-Crew
g++ -std=c++17 -Wall -Wextra -Icomponents/otr_engine/include tests/test_adventure.cpp components/otr_engine/otr_world.cpp components/otr_engine/otr_maps.cpp -o tests/test_adventure && ./tests/test_adventure   # adventure engine
g++ -std=c++17 -Wall -Wextra -Imain -Imain/lang tests/test_lang.cpp main/lang/lang.cpp main/lang/lang_sv.cpp main/lang/lang_en.cpp -o tests/test_lang && ./tests/test_lang   # string tables
g++ -std=c++17 -Wall -Wextra -Imain -Itests/stub tests/test_live.cpp main/game_live.cpp main/game_yatzy.cpp main/game_pig.cpp main/game_farkle.cpp main/game_scc.cpp -o tests/test_live && ./tests/test_live   # web seat release
g++ -std=c++17 -Wall -Wextra -Imain -Itests tests/test_audio.cpp main/audio_synth.cpp main/sound_bank.cpp -o tests/test_audio && ./tests/test_audio   # synth, MML, WAV parsing
g++ -std=c++17 -Wall -Wextra -Imain -Itests tests/test_fspath.cpp main/fs_path.cpp -o tests/test_fspath && ./tests/test_fspath   # file browser path safety
g++ -std=c++17 -Wall -Wextra -Icomponents/otr_engine/include -Itests tests/test_joystick.cpp -o tests/test_joystick && ./tests/test_joystick   # joystick zones/rate/diagonal
g++ -std=c++17 -Wall -Wextra -Icomponents/otr_story/include -Itests tests/test_story.cpp components/otr_story/*.cpp -o tests/test_story && ./tests/test_story   # dialogue engine + village script
g++ -std=c++17 -Wall -Wextra -Imain -Itests -Icomponents/otr_engine/include -Icomponents/otr_story/include tests/test_advlive.cpp main/adv_live.cpp components/otr_engine/*.cpp components/otr_story/*.cpp -o tests/test_advlive && ./tests/test_advlive   # adventure live snapshot + JSON
g++ -std=c++17 -Wall -Wextra -Icomponents/otr_midi/include -Itests tests/test_midi.cpp components/otr_midi/*.cpp -o tests/test_midi && ./tests/test_midi [file.mid ...]   # MIDI/KAR parser, lyrics, melody, registry
g++ -std=c++17 -Wall -Wextra -O2 -Icomponents/otr_midi/include -Icomponents/otr_synth/include -Itests tests/test_sfsynth.cpp components/otr_synth/otr_synth.cpp components/otr_midi/*.cpp -o tests/test_sfsynth && ./tests/test_sfsynth font.sf2 song.mid [out.wav]   # SF2 synth render
```

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
