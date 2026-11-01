# Changelog

Version history and historical delivery notes for MyEsp-OnTheRoad.
For current capabilities and setup, see [README.md](README.md).

Add new entries above the historical archive, newest first. Describe actual
changes and migration requirements; distinguish automated checks from
owner-reported hardware verification. A merged change is not automatically
a published GitHub Release.

## v0.23.18 (unreleased)

- S3 only: backport Espressif's `a16c003` SPI bus-lock TOCTOU fix,
  matching the repeated null-pointer crashes in `bg_exit_core`.
  Generate a fingerprint-checked patched source in the build directory;
  never modify the installed SDK. Unknown SDK source fails configuration
  rather than applying an unchecked patch. P4 and C6 are unchanged.
- Raw S3 FFat evidence confirms two erased root-directory sectors with
  valid, matching wear-levelling state. This confirms filesystem damage,
  not physical flash failure or the precise interruption that caused it.
- Owner hardware confirmation of crash prevention remains pending.

## v0.23.17 (unreleased)

- Readable city/UTC timezone dropdowns on-device and in web settings,
  sharing one catalogue with automatic daylight saving. Existing custom
  timezone values remain selectable without exposing syntax to users.
- A compact, sticky common header/navigation on every web page refreshes
  device name and connection status. Settings saves update the name
  immediately; other open visible pages refresh within ten seconds.
- Boot/main-menu display branding uses the configured device name; the
  main menu refreshes it while open. Existing DHCP/mDNS hostname updates
  remain connected to the same setting.
- Web assets are regenerated during CMake configuration so source changes
  (including upload progress, history and idle controls) reach firmware.
- Fix chained volatile assignments rejected by the ESP-IDF compiler in
  the v0.23.16 idle Wi-Fi path; that build did not complete.

## v0.23.16 (unreleased)

- Optional five-minute SD CSV history records battery voltage, boot/time,
  per-game session time, display/Wi-Fi/audio usage, storage IO and health
  counters. Bounded rotation and visible dropped rows; no internal-flash fallback.
- Optional idle screen-off stops Wi-Fi and pauses I2S/mixer/amplifier,
  preserves settings and resumes on touch. Protected during games,
  karaoke, transfers and OTA. CPU remains awake; this is not light/deep sleep.
- Both settings are persisted and exposed on-device, in web settings and
  diagnostics; disabled by default. Hardware discharge/power-saving
  measurements remain pending.

## v0.23.15 (unreleased)

- File uploads show stable batch progress (completed/active files and total
  bytes sent) instead of alternating filenames with a misleading file index.
- P4 automatically selects a DS3231 responding at 0x68 on the shared I2C
  bus. If absent, the internal RTC remains the fallback. A detected DS3231
  with invalid time or failed register reads remains selected and reports
  its error instead of silently falling back.
- RTC time, temperature, diagnostics and NTP/manual synchronization use the
  selected backend. S3 retains its existing DS3231 backend.
- Host tests cover absence, valid time, oscillator-stop recovery and I2C
  register errors. The owner reports that RTC works perfectly with the
  installed DS3231 on P4; individual test scenarios were not specified.

## v0.23.14 (unreleased)

- P4: log the reset reason, the `RTC_NOINIT` anchor's validity, and both the
  stored and current RTC counters before applying the reset policy.
- P4: also trust the RTC anchor after panic, interrupt-watchdog and
  task-watchdog resets. Power-on, brownout, external/pin and other watchdog
  resets (ESP_RST_WDT) still fall back to NTP, manual time or uncertain time.
- Both boards: write a checksummed "last alive" time to `RTC_NOINIT` memory
  each second and log the previous run's value at boot. Without a trusted
  clock, use the newer of that value and NVS as uncertain time.
- Save the NVS fallback time every minute instead of every 10 minutes.
- Diagnostics: a PIN-protected "Testkrasch" section (`POST /api/diag/crash`)
  triggers a deliberate panic or interrupt-watchdog reset. It tests reset
  handling, RTC retention and core dumps.
- The owner reports that P4 web restarts keep RTC time valid. Physical RESET
  and power removal fall back to uncertain NVS time (`?`). With v0.23.14,
  owner logs show that P4 RESET and power removal both cause a power-on
  reset with the RTC counter restarting near 0, so neither can retain internal
  RTC time. Web restarts on both boards logged the previous run's last-alive
  time.

## v0.23.13 (unreleased)

- Consume skipped upload bodies before returning success, without holding
  the SD lock or transfer lease. Async requests also drain remaining data on
  early rejection; interrupted bodies shut down the connection before async
  completion rather than leaving binary file data for the HTTP parser.
- The owner's v0.23.12 log shows 199 completed uploads (9,199,403 bytes),
  including seven new Hit_Trax files, without new SD errors or watchdogs.
  Existing-file attempts in Hit_Trax produced 25 HTTP parser errors, matching
  the unread-body bug. Hardware retesting of the fix remains pending.

## v0.23.12 (unreleased)

- Restored up to four concurrent SD upload sessions in browser batches and
  the server. Card operations remain serialized; internal-flash batches remain
  sequential. Recovery blocks new transfers, aborts peers and waits for every
  tracked upload, download and SD log-export file to close before remounting.
- Normal logging is Info with SD command tracing off. Diagnostics offers
  admin-protected Error/Warning/Info/Debug levels and independent SD tracing
  that expires after ten minutes. Runtime choices reset at restart.
  Debug statements are compiled in; changing verbosity affects new ESP-IDF
  log messages, not historical entries, direct events or bootloader output.
- Preserve available RAM log backlog through SD outages and failed appends;
  check close errors and stop failed ZIP reads instead of zero-padding them.
- Kept 40 MHz for both P4 slots. The owner confirmed actual 50 MHz on both
  slots and three successful uploads without SD errors/watchdogs. Unison's
  mean write time improved only 3.3% (6.469 to 6.258 s), so 50 MHz is not
  promoted. Removed the extra experiment from the routine CI build matrix;
  its opt-in code and historical v0.23.11 artifact remain for reproduction.
- Concurrent-upload recovery and runtime logging controls need hardware
  verification; previous sequential successes do not verify concurrency.

## v0.23.11 (unreleased)

- Added a separate P4 `50mhz-experimental` CI artifact. Normal P4 builds
  remain at 40 MHz; simultaneous SD uploads remain disabled.
- The experiment requests 50 MHz for both SD and C6 SDIO. As in MyPi-Hotspot,
  a linker wrapper lifts ESP-IDF v6.1's 40 MHz SDIO cap only after successful
  high-speed negotiation. SD memory cards already negotiate 50 MHz.
  A second wrapper logs negotiated and actual clocks on both slots and
  rejects a requested 50 MHz initialization that does not reach 50 MHz.
- Owner log for v0.23.10 confirms completed Wi-Fi uploads of 29,258,148 and
  265,847,632 bytes with SD at 40 MHz. No SD errors, watchdogs or remounts
  appear after that boot. Earlier failures in the same log precede v0.23.10.
  This is evidence of improvement, not full stability or 50 MHz verification.

## v0.23.10 (unreleased)

- P4 SD card now runs at 40 MHz, the same clock as the ESP-Hosted C6 SDIO link
  (pinned to 40000 kHz and checked at compile time). Both use separate pins
  but share one SDMMC controller and its host clock divider. With 20 MHz SD
  and 40 MHz SDIO, ESP-IDF reprogrammed the divider on every slot switch
  during Wi-Fi uploads; the 10 MHz recovery mode did the same. This matches
  the lost auto CMD12 seen in logs, but the link is a hypothesis until
  hardware-verified.
- P4 recovery remounts at the same 40 MHz after a card power cycle, and
  falls back to 4-bit/1-bit 20 MHz only if 40 MHz will not mount. A mounted
  card clock that differs from the SDIO clock is logged as a warning.
- Owner hardware result v0.23.9: no watchdog. The write error was detected
  in about 0.1 s, CMD12 returned the card from receive state, and remount
  succeeded. The write itself still failed.

## v0.23.9 (unreleased)

- SD write recovery on S3 and P4. Owner logs from P4 v0.23.8 show a response
  timeout during a multi-block write, after which the card stays in receive
  state (`r1=00000c00`). ESP-IDF then polls CMD13 for 5 s on CPU 0, which
  starves `IDLE0` and trips the task watchdog. Fixed in two places:
  - The SD transaction wrapper sends `STOP_TRANSMISSION` (CMD12) when CMD13
    reports a card stuck in receive state; retries at most every 500 ms.
  - The wrapper yields one tick when the card stays busy for more than 2 ms.
  - The wrapper keeps a failed CMD25 failed. ESP-IDF v6.1 returns the
    follow-up ACMD22 result instead, so after recovery a partial write would
    otherwise be reported as successful. The same bug is fixed upstream.
- Upload I/O errors remount the card before the rest of the HTTP body is
  drained, and remove the temporary file again after a successful remount.
- Unchanged: boot clock (P4 20 MHz) and watchdog limits; recovery remount
  still uses 10 MHz. The trigger (lost CMD12 response, CRC error) is not
  explained, and these changes are not hardware-verified yet.

## v0.23.8 (unreleased)

- P4 diagnostic build enables frame pointers for watchdog backtraces and a
  CPU-1 SD transaction sampler. Reports current command, preceding data command,
  status-poll streak, response and duration through serial/RAM logs without
  accessing the SD card. The C6 slot is not instrumented.
- Diagnostics can pause SD log persistence while leaving RAM/serial capture on,
  and download a RAM-only ZIP without reading SD files. Clock, bus width,
  watchdog limits and upload scheduling are unchanged; this is instrumentation,
  not a verified fix for SD timeouts or CPU starvation.

## v0.23.7 (unreleased)

- Moved internal-flash repair beside the SD format button, at the top right of
  the file manager. Added admin-protected NVS usage and binary partition backup;
  NVS is not JSON/FFat, and the backup may contain credentials. No web restore.
- Karaoke uses the browser width, with larger lyrics that fit up to two lines.
  Device hints identify `/otr/karaoke/karaoke.reg`; registry writes preserve the
  previous file on failure and report errors instead of silently discarding it.
- MIDI/KAR without embedded lyrics can use a matching UTF-8 `.lyr` sidecar:
  plain text scrolls manually on device/web, and LRC timestamps follow playback
  and seeking. Embedded lyrics take priority. Limits and examples are documented
  in `docs/KARAOKE_LYRICS.md`.
- Adventure editor can reveal coordinate layers (hidden treasures, enemy starts,
  portals and arrival points) with object previews, counts and a coordinate list.
  This does not change placements or simulate story actions.
- The owner reports the graphical editor looks good and works on P4 after USB
  recovery. This is not a maximum-map memory test or S3 verification. New changes
  in this version still need hardware verification.

## v0.23.6 (unreleased)

- The HTTP server now sizes its URI-handler table from the generated web asset
  count and reserves room for API/WebSocket routes. This prevents the expanded
  Adventure editor sprite assets from blocking API registration at startup.
- SD uploads are serialized. After an SD write/flush/close/install I/O failure,
  the server attempts the existing card recovery/remount path and skips the
  post-upload usage refresh. A reported timeout/watchdog incident still needs
  hardware reproduction; the software mitigation does not establish card health.
- Maps may now be up to 128×128 (was 80×80). `Otr::MAX_W/MAX_H` are 128; the
  live-view JSON buffer is 96 KiB and `SdSession::SAVE_CAP` is 459,674 bytes.
  Saves remain compatible. Engine memory grows (World ≈181 KB, Planner ≈640 KB,
  all in PSRAM); not yet checked on S3/P4 hardware. One 128×128 map is ≈16.5 KB
  of source, so the 64 KiB file limit still applies.
- The editor can create new `.adv` files (folder, name, title, size; validated
  first, never overwrites), add maps, resize a map with a chosen fill glyph and
  zoom the canvas.
- The device menu entry is now "Äventyr"/"Adventure" without "(test)", with an
  updated description.

- Added the graphical map view to the hosted Adventure editor (`/adventure-editor`):
  map tabs per internal map, a canvas with paint/erase/pick/move tools, a palette
  of built-in glyphs, NPC digits, legend glyphs and resource glyphs, and an
  editor-level undo for the current session.
- Added a legend helper (create/edit/remove `@legend` entries with kind, group,
  colour and belt direction checks, glyph suggestion, usage counts and
  undefined-glyph warnings) and an area helper for `@dark` rectangles. Moving a
  glyph also moves matching `@plategroup`/`@gategroup` coordinates; resizing
  drops coordinates that fall outside the map.
- The new lossless text model (`main/web/adventure-model.js`) rewrites only map
  rows, legend lines, dark rectangles and group coordinates; comments and other
  sections are kept byte for byte. Saves still go through production validation.
- Node tests round-trip all 63 bundled/documented `.adv` files unchanged.
  Destinations, portals, enemies, treasures and resource placements are still
  edited in text mode. Firmware CI and physical S3/P4 verification remain pending.
- Fixed text-mode edit preservation, empty-map fallback and existing legend
  editing; comment parsing and plate/gate group coordinates now remain valid
  through map edits and moves.
- Added image-symbol/character map display with retained tile colours, clearer
  tooltips and automatic width-fitting zoom. Legend/layout controls adapt to
  narrow screens; section text, group coordinates and individual dark areas can
  be edited or highlighted directly in the graphical workspace.
- The image mode and palette now use exported device sprites, including the
  device's standard resource bag. Resource entries show their adventure-specific
  `bitmaps/` PNG when it passes the device's size and 32×32 checks; missing or
  invalid overrides fall back to the same bag. The section helper can create a
  guided `@node` with speaker, localized text and a linked choice.

## v0.23.5

- Added the first hosted Adventure editor step: browse, open and edit existing
  SD `.adv` files in source mode, with validation through the production parser,
  story validator and world loader before saving.
- Saves reuse the file manager's staged atomic upload and require explicit
  confirmation before replacing an existing adventure. Validation is limited
  to 64 KiB and requires the admin PIN when configured.
- Host validator and web UI checks are added. Firmware CI and physical S3/P4
  verification remain pending; visual map editing is not included yet.

## v0.23.4 (unreleased)

- Added guarded `world tile map=N ...` and `world object map=N ...` story effects
  for allowlisted existing terrain/object IDs. Invalid or occupied targets fail
  without committing related story-state changes; existing save formats are
  unchanged.
- Added the numbered Motoräventyret SD chapters, installation/playing guide,
  spoiler solution traces and a mechanics-verification matrix.
- Host Adventure, story, resource, multimap, catalog and campaign tests passed.
  Firmware CI passed for S3, P4 and C6 on the initial implementation commit
  (`c87b57c`); current follow-up commits still need a fresh workflow run. The
  owner reports testing the complete campaign and story-driven map changes on
  P4; S3 hardware verification remains pending.

## Historical release notes through v0.23.3

The following notes were moved from README without changing their historical
claims. Versions, verification caveats and future plans below describe their
original context, not necessarily the current firmware. Current work and
remaining hardware checks belong in the linked TODO documents.

**Status: v0.23.3** – FAT supports long filenames and safe staged uploads;
directory uploads support up to three levels, SD formatting requires explicit
confirmation, and existing-file uploads now skip by default. Hardware
verification of the new SD operations remains pending. See
[file-management changes and measurements](docs/FILE_MANAGEMENT.md). ESP-Hosted versions are
cached and shown in P4 diagnostics; Karaoke lyrics scale with large-screen
height; P4 has a 32 MB partition layout
(existing devices need one USB partition-table flash); SoundFont budgets follow
the board profile; screen-off power policy and ESP-to-ESP Yatzy play are added.
The owner verified Karaoke layout on both boards, P4 partition migration and large
SoundFonts, and play between devices and the browser. Reconnection, reverse-role
coverage and Wi-Fi power-save measurements remain pending.
Earlier v0.22.7: configurable device name shown across web pages and used for
DHCP hostname/mDNS; RGB LED controls are omitted on P4; P4 Host-radio C6 firmware
can be uploaded and updated over ESP-Hosted OTA. CI builds `host_fw.bin` from the
pinned latest ESP-Hosted MCU source. The owner updated the factory C6 to ESP-Hosted
3.0.10 over OTA (host and co-processor versions now match). v0.22.6 boot/Wi-Fi scaling and all text sizes
were tested by the owner on both boards.
Earlier: dual-target ESP32-S3/P4 builds, including the v0.21.3
Adventure and UI changes below. The owner confirmed P4 startup and Wi-Fi with
v0.22.0 and app OTA with v0.22.1. The owner confirmed ADC readings, SD-backed
Adventure/Karaoke playback and RTC synchronization after NTP with v0.22.2.
This build fixes explicit repair of unmounted FFat and board-aware diagnostics.
OTA erases incrementally; P4 display refresh interrupts remain active during flash
operations. This takes effect for uploads performed after installing this version.
Screen power saving on both boards: Settings → screen-off timeout (off by default,
30 seconds to 60 minutes), or turn the screen off manually in Settings. Touch wakes
without activating controls; release and a minimum 250 ms guard are required before
a new touch. Audio/games/network stay active; OTA wakes and keeps the screen on.
This switches off the backlight, not the CPU or display controller; battery savings
need physical measurement. The P4 power button is not remapped.
RTC/VBAT retention after loss of main power still needs controlled deep sleep.

v0.21.3 – internal maps, named portal destinations and persistent
return visits. Player inventory and energy/lives are shared, inactive maps pause,
and blocked arrivals are rejected without moving obstacles.
See the [runtime format](docs/ADVENTURE_MULTIMAP_FORMAT.md) and copy the
[portal test adventure](docs/adventures/multimap_portals/README.md) from the SD bundle.
The Adventure HUD has a reserved 48-pixel strip above the map. Karaoke now has
an actually rectangular 30×14-pixel volume knob with its large touch target
preserved, and percentage inside the battery symbol's existing footprint.
Physical ESP32-S3 verification remains necessary; no editor or general undo
is included in this delivery.

The durable [Adventure checklist and session recovery instructions](docs/ADVENTURE_TODO.md)
record what is implemented, published and hardware-tested.

Previous: v0.21.2 – custom `@resources`: real pickups, shared story inventory,
caps, PNG/bag rendering, inventory menu, persistence and live web view.
The owner completed the [resource check](docs/adventures/z_resource_test/README.md) on ESP.
Engine saves use format 7; old resource-map saves require a new game.

Previous: v0.21.0 – adventure skates, poison ground/swamp/sharp rocks,
six colored push-box/plate pairs and explicit lever+plate AND groups.
The larger external demo and reproducible solution are in
[`docs/adventures/s3_demo/`](docs/adventures/s3_demo/), mirrored in the SD bundle.
Engine saves are now format 5 (one skates byte added to format 4);
formats 1–4 load with skates off. The SD-session container remains version 1.
See [authoring section 10f](docs/AUTHORING_ADVENTURES.md#10f-s3-skridskor-giftterräng-och-färgsokoban-v0210).
The owner confirms that this preceding gameplay block works on hardware.

Previous: v0.20.1 – the adventure engine includes S1 grouped Sokoban plates/gates
(introduced in v0.20.0) and S2 levers, timed gates, ice, belts, colored keys/doors
and par scoring. The S2 `.adv` example is in
[`docs/adventures/s2_demo/`](docs/adventures/s2_demo/); the four-room grouped
Sokoban example is in `sdcard_bundle/otr/adventure/sokoban/`. See
Start with the [adventure authoring quick guide](docs/ADVENTURE_AUTHORING_QUICKSTART.md);
see the [full authoring reference](docs/AUTHORING_ADVENTURES.md) for details.
S2 uses engine save format 4; formats 1–3 migrate, while the SD-session container
remains version 1. `tools/adv_parse.*` is still S1-only; S2 is validated by the
production loader. The owner has tested PR #11 on hardware and reports that it
works well.

Previous: v0.18.5 – **SD-card adventures with darkness, flashlight and pogo stick**.
Adventures now load as `.adv` text files from the SD card (`/otr/adventure`); see
[docs/AUTHORING_ADVENTURES.md](docs/AUTHORING_ADVENTURES.md). A map can mark **dark areas**
(`@dark`) that hide their contents until a **flashlight** (glyph `f`, levels 1–3) lights a cone
ahead – reach equals the level (1/2/3 cells), with the leading rows three wide. A **pogo stick**
(glyph `j`, levels 1–3) vaults over 1–N blocking cells; jumping is **manual** via a **split
action button** whose top third (**Byt/Swap**) cycles the actions available right now – **Slå**
(hit), **Prata** (talk) and **Hoppa** (jump) – and whose lower two-thirds performs the chosen one.
Both items are permanent and carry across all maps in an adventure. `@treasures` on the same cell
as a map pickup glyph takes precedence, so `f`/`j` can be raised to level 2–3. Saves are version 3
(legacy v1/v2 load with the new abilities at 0). **Hardware-tested by Kent** (darkness/flashlight/
pogo rendering, the flashlight cone, manual jump via the split button, and `@treasures` precedence);
other parts are host-tested and build-clean only. Karaoke is unchanged and paused. Existing B1
partitions: normal OTA from v0.12+.

Previous: v0.17.0 – **adventures moved to the SD card** (`.adv` format) with a catalog
(folders = adventures, `NN_` prefix = order, up to 16 maps each), chained maps with carry/reset,
an exported 12-stage MineSweeper campaign, and data-driven true minesweeper (`mine_level`).
At v0.17.0 the on-device and host reference parsers were twins
(`main/adv_loader` / `tools/adv_parse`). **Not hardware-tested** at that release.

Previous: v0.16.1 – **Joseph, chapter 1** (Genesis 37–50): two maps, six characters, bilingual
SV/EN dialogue, with UTF-8 (åäö) and map/choice-flow fixes. Hardware-tested by Kent ("the game works").

Previous: v0.15.0.2 – portrait (320×480) adventure dialogs, on-device new-player creation,
and conservative profile-storage error handling. Host tests and firmware build pass;
**independent layout review pending, not hardware-tested**. The reported intermittent profile loss
on the user's device is not diagnosed. Automatic FFat formatting is disabled; blank/unreadable
storage is left untouched. Karaoke is unchanged.

v0.15.0 – **adventure map travel and MineSweeper campaign**. The Village exit leads to
the Meadow and back, preserving map changes and story state. Version 2 saves bundle both maps, inventory,
story and campaign progress; legacy v1 saves remain readable. MineSweeper has 12 fixed stages, a findable
one-hit shield, and one 3×3 / one 4×4 bomb per stage with preview, confirmation and cancellation.
Read-only `/adventure` mirrors the new state. **Not hardware-tested.** Karaoke is unchanged and paused;
no Joseph chapter or quest-log expansion is included. Existing B1 partitions: normal OTA from v0.12+.

Previous: v0.13.7 – **song library with folders**: karaoke songs are scanned recursively (up to 2 000, cached
in `songs.idx`), browsed by folder / search / A–Z on the device and picked from a phone at `/karaoke/songs`. The file
manager (`/files`) gets multi-select, bulk delete/move, paging, sorting, new folders and drag-and-drop upload; the
diagnostics page shows a live log (SSE) and downloads all logs as a ZIP (`/otr/logs/otr_{0,1,2}.log` on SD). No
automatic file sorting. **Not hardware-tested yet.**

Previous: v0.13.6 – **countdown dots**: pauses of 3 s or more between lyric lines (and long intros) show up to
8 dots, white at first, lighting one per second in the sung-syllable colour so the last one lights as the next line
starts – on the device screen and on the `/karaoke` web page (new `countdown` frame, sample-synced). **Not
hardware-tested yet.**

Previous: v0.13.5 – **web karaoke**: open `http://<device>/karaoke` on a phone, tap Connect and the karaoke
plays in the browser. The device streams 22 050 Hz mono 16-bit PCM over the WebSocket `/ws/karaoke` (1 024-byte
frames) with the lyrics as JSON text frames placed at the exact sample, so the page highlights each syllable as it
is heard (0.3 s buffer). While a browser is connected and a song is open the device speaker is muted automatically
(ES8311 DAC volume 0) and restored when the last listener leaves. Max 3 listeners; NVS `kara_webstream=0` disables
the endpoint. **Not hardware-tested yet.**

Previous: v0.13.4 – **resampling to the output rate**: every sample above 22 050 Hz (28/30.5/32/44.1/48 kHz)
is resampled to the output rate while it is read from the SD card (polyphase Kaiser-windowed sinc, 75 dB stopband;
loop lengths are rounded and the zone's pitch is compensated exactly). The ±12 key margin is gone: the synth loads
exactly the keys the song plays at the current transpose and reloads in the background when the key changes
(keeping the previous key if the new one does not fit). The per-song budget is now the largest free PSRAM block
minus 512 kB, and the game sound-clip cache is freed first. Host-measured: GeneralUser-GS + Ainbusk 6.03 → 3.10 MB,
with Alan Parsons "Time" 7.60 → 4.64 MB, Jnsgm2 + "Time" 6.91 → 3.85 MB – all fit now. Dropout diagnostics: every 5 s
the log shows render load (avg/peak), underruns and voices; choose the `kara_voices` limit (32/64/96/128/160/192,
default 96) in the sound-bank picker.
**Not hardware-tested yet.**

Previous: v0.13.3 – **halving of high-rate samples**: when a song is prepared, samples recorded at ≥ 1.8 × the
output rate (44.1/48 kHz) are decimated 2:1 with a 47-tap half-band FIR while they are read from the SD card
(loops keep their pitch; short odd loops stay at full rate). Saves up to ~1.4 MB per song with large GM banks
(e.g. eawpats 4.3 → 3.0 MB); banks already at 22 050 Hz are unchanged. GeneralUser-GS still cannot fit two of the
five test songs (their samples are 28–32 kHz). `Synth::setHalving(false)` turns it off. **Not hardware-tested yet.**

Previous: v0.13.2 – **selective SoundFont loading + reverb/chorus**: opening a bank now reads only its structure
(hydra); when a song is opened the synth scans it (presets, keys ±12 so transposing never reloads, velocities,
bank select and drum channel exactly as TinySoundFont picks zones) and reads only those sample ranges from the SD
card into PSRAM (budget 6 MB per song). Banks larger than PSRAM work – the 7 MB limit is gone (GeneralUser-GS,
31 MB, tested on the host). Over budget the synth keeps only the velocity layer holding 100, then one zone per
key; if it still does not fit the song shows "Song needs too much sample data". New `otr_fx`: Freeverb-style
reverb and a chorus as mono send effects, per channel via CC91/CC93 (GM defaults 40/0), on at 50 % (NVS, no UI
controls yet). Host-measured: 0.1–0.8 MB per song with 2–4 MB GM banks, 3.5–4.8 MB with GeneralUser-GS (two of
five test songs do not fit); selective output is bit-identical to a full load. Selective loading idea from
copych/ESP32_SF2_Sampler_Synthesizer (MIT). Same partitions → plain OTA. **Not hardware-tested yet.**
v0.13.1 – **karaoke in landscape + multiple SoundFonts**: the karaoke module turns the display to
landscape (480×320, LVGL software rotation in the flush, touch mapped by LVGL; portrait and the 180° setting are
restored when leaving) so whole lyric lines fit; list, player and sheets re-laid out for 480 px. Several `.sf2`
files can live in `/otr/sf2`: pick a global default (saved in NVS) from the song list and optionally a per-song
sound bank from the player (saved as `sf2=<file>` in `karaoke.reg`, old lines still valid). The font is reloaded
in the background when a song wants another one (only one font in PSRAM); the active font name is shown. Files
are validated: SF3/compressed, non-RIFF/sfbk and > 7 MB (limit removed in v0.13.2) are rejected with a clear message. Entering karaoke with
sound off asks "Turn sound on?" (yes/no; yes saves the setting). Adventure live view (`/adventure`) now scales to
the browser window: map and info side by side in wide/landscape windows (≥ 800 px), info below the map in
portrait, plus a size selector (fit / small / medium / large, saved in the browser). Same partitions → plain OTA.
**Not hardware-tested yet.**
v0.13.0 – **karaoke mode**: new pure-C++ components `otr_midi` (SMF/.kar parser, lyrics with
syllable timing, melody-track detection, sequencer, per-song registry; 102 host tests) and `otr_synth`
(vendored TinySoundFont, patched for 16-bit samples in PSRAM, float filters, fixed-point mono render and voice
stealing, max 32 voices). A new gold **Karaoke** card in the menu: song list from `/otr/karaoke`, lyrics on the
device with word-by-word highlighting (previous/current/next line), play/pause, seek, tempo 50–150 %, key ±12,
volume, melody on/off (sing it yourself) and a melody-track picker. Tempo, key and melody track are saved per
song in `karaoke.reg` (keyed by CRC32 + size, survives renames). The synth runs in the audio mixer task through
a new external-source hook (dice/adventure music is paused, not lost). A SoundFont (`.sf2`, max 7 MB) is needed
in `/otr/sf2` for sound – without one the lyrics play silently. Same B1 partitions as v0.12.0 → plain OTA.
**Not hardware-tested yet.** Microphone + scoring is planned next ([docs/MUSIC_KARAOKE_PLAN.md](docs/MUSIC_KARAOKE_PLAN.md)).
v0.12.0 – new partition layout **B1**: 2 × 5 MB app slots (OTA) + 5.9 MB FFat (was 2 × 3 MB +
9 MB), and the firmware is now built with size optimisation (`-Os`): 1.77 MB, 34 % of a slot.
**Requires a one-time USB flash of the merged image; FFat is reformatted (players and adventure saves on FFat are
lost).** Not hardware-tested yet.
v0.11.1 – adventure live view: new page `/adventure` shows the running adventure read-only
(canvas map with player, people, items and gates, HUD with energy/lives/keys/gems/items, and the current
dialogue with speaker, full text, reference and choices), fed by a separate `AdvLive` snapshot via
`/api/adventure` (map only sent when its revision changes). Links from the overview and `/game`.
**Not hardware-tested yet.**
v0.11.0 – conversations in the adventure: new pure-C++ component `otr_story` (dialogue
trees, conditions, effects, flags, items, quests; 182 host tests), people on the map (`Obj::NPC`), the
Hit button turns into **Talk** when facing a person, tap a person to walk there and talk, dialogue box
with portrait + typewriter text and up to 4 answer buttons, demo world "Byn" (the village) with three
people. Story state is saved as `.sts` next to the world save. Hardware-tested by Kent: the village and
the dialogues work very well.
Before that (`40ea276`) – touch release filter against double taps, 500 ms
guard on every toggle, new adventure HUD (icon + count rows left, carried items right, quick sound on/off,
zoom in the hamburger menu); not hardware-tested yet. Next: a Bible adventure with conversations
([docs/BIBLE_ADVENTURE_PLAN.md](docs/BIBLE_ADVENTURE_PLAN.md)).
v0.10.0 – adventure control panel without a top bar, half walking speed (300/150 ms), 30° straight joystick
sectors, lava tie-break fix in the Pro pathfinder (bridge bug), SD/boot status on the web overview.
Hardware-tested by Kent (v0.9.0/v0.10.0): sound, SD card, file browser, adventure saves, joystick feel and
speed, diagonal sectors, bridge fix, web SD status, zoom debounce.
v0.9.0 – sound (ES8311, chiptune + WAV), microSD with web file browser, adventure saves on SD and an
analog-style joystick (hardware-tested). v0.8.0 – adventure engine with tap-to-walk, energy/lives,
Minfältet and per-player saves (saves hardware-tested). Earlier: v0.5.6 – 180° screen rotation (setting on device and web; MADCTL MX|MY + mirrored touch),
personal 4-digit player PINs (player file v2 with automatic v1 migration, on-device numeric keypad),
and a new `/play` flow: pick your name, enter your PIN (no shared game code any more; web seats are
bound to an internal session id, with a 5-attempt lockout). The overview page links to Live and Play.
v0.5.6 builds with 0 errors / 0 warnings and passes the host tests, but is **not yet tested on
hardware** (the rotation via MADCTL in particular). v0.5.5 web play, v0.5.0's RTC and sprite dice
animation and v0.2.0 are hardware-verified. See [ROADMAP.md](ROADMAP.md).
