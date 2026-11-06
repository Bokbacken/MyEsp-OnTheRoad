# Changelog

Version history and historical delivery notes for MyEsp-OnTheRoad.
For current capabilities and setup, see [README.md](README.md).

Add new entries above the historical archive, newest first. Describe actual
changes and migration requirements; distinguish automated checks from
owner-reported hardware verification. A merged change is not automatically
a published GitHub Release.

## v0.25.16

- Adds an isolated touch diagnostic from the Sudoku menu with separate 9x9 and
  number-key target passes, highlighted whole-cell targets, tap undo, discard/retry
  prompts for wrong targets, and an on-screen per-target heatmap. Explicit
  export writes raw CSV samples and board/profile metadata to the SD card.
- Done and the test's Back button open results immediately, keeping partial
  samples available for export without completing all 81 cells or 9 keys.
- A circle/cross pointer and movement trail show registered touch coordinates
  while pressed and for 300 ms after release. CSV includes maximum displacement
  from the initial press and duration; the cell centre is only a reference,
  not a precision target. Dead-zone simulation and edge coverage use measured
  release positions, matching Sudoku's click handling.
- No touch records a manually reported unregistered attempt for the current
  target without advancing it or inventing coordinates. Results separate these
  reports from registered taps, CSV marks them as unregistered with blank touch
  fields, and undo/discard also applies to these reports.
- Results overlay actual release positions as a point cloud on the target
  grid, with brightening overlaps across repeated passes in the same RAM
  session. Registered misses/cancellations are red; manually unregistered
  attempts have no invented point. Number-key coordinates are mapped from
  the real 5x3 input layout into the result's 3x3 digit layout.
- Adds a separate full-screen rectangular 9x9 test covering all pixel coordinates
  including screen edges and corners, without a header or fixed controls.
  Holding in the centre third opens Continue/No touch/Undo/Done/Discard;
  this menu gesture is not recorded as a sample. CSV uses a separate screen
  mode, and its measurements are excluded from Sudoku dead-zone analysis.
- Allocate touch sample buffers lazily in 8-bit PSRAM instead of reserving
  their full capacity in internal DRAM at boot, preserving internal heap for
  Wi-Fi and other drivers. Report a visible error if PSRAM allocation fails.
- Simulates Sudoku dead zones 0-8 px against the raw board samples and reports
  correct/rejected/misassigned counts. A candidate is shown only with at least
  81 samples, 20 edge samples, zero misassignments and at least 90% accepted.
  Samples remain in RAM; no compensation or normal Sudoku progress is changed.
- Automated math and all-board firmware checks are required; physical 3.5-inch
  suitability and touch accuracy still require owner testing.

## v0.25.15

- Adds cancellation for browser file/folder upload batches: stop scheduling,
  abort active requests, preserve already completed files and explicitly
  report unconfirmed in-flight outcomes. Temporary upload files retain the
  existing cleanup/original-preservation rules.
- Adds cancellable background directory deletion, with admin-protected
  start/status/cancel routes and per-entry cancellation checks. Already
  deleted entries cannot be restored; partial results are reported explicitly.
  Active deletion blocks formatting, flash repair and controlled restart.
- File-manager failures and partial/cancelled batch results remain in an
  acknowledgement dialog. Upload diagnostics distinguish requested, received,
  staged (accepted by stdio) and installed bytes, not just declared file size.
- Corrects RTC synchronization checks and second-boundary write alignment;
  no game logic, network modes, RTC retention policy or SD clock changes.

## v0.25.14

- Applies drag-is-not-click protection to every board through the shared LVGL
  pointer layer, including ES3C35P and P4_43, not only S3_35B. Movement over
  20 physical pixels cancels all click variants until the next press, including
  release-only movement and dragging back to the starting point. Release,
  scrolling and normal taps remain available. S3_35B retains its additional
  sampled movement/read-error protection.

## v0.25.13

- Ends the owner-accepted S3_35B touch investigation: disables raw capture by
  default and silently discards uniform idle reports, including `0x37`.
  Real I2C/parser failures still have counters and rate-limited warnings.
- Restores movement/read-error click suppression: drags exceeding 20 px
  cannot become clicks on release; ordinary taps and release events remain.
  Opt-in raw diagnostics now preserve this protection and print all 14 bytes.
- Retains the shared task (touch each 20 ms, PWR after every fifth sample),
  cached LVGL state, 14-byte/two-point report and locked STOP-separated I2C.

## v0.25.12

- Updates P4's exact ESP-Hosted dependency and the pinned C6 OTA example to
  release 3.1.0. Upstream changes include SDIO receive-buffer preallocation
  and capacity handling, packet-mode support, task-stack sizing corrections
  and a detached-task PSRAM heap fix. No board-level speed improvement has
  been measured; the shared SD/C6 clock remains 40 MHz.
- C6 CI checks the Wi-Fi coprocessor configuration and OTA slot size and
  includes its generated configuration in the radio artifact.

## v0.25.11

- Pins the P4 ESP-Hosted dependency exactly to the already locked 3.0.10.
  This avoids older dependency managers collecting USB Kconfig requirements
  from an unselected newer release. SDIO transport and C6 firmware are unchanged.
- Firmware CI stops immediately on a failed configure/build rather than
  continuing to image validation and merge commands without an application.

## v0.25.10

- Taller Sudoku keypad and tool buttons on both S3 profiles (minimum 36 px
  in portrait, previously 30) and P4 (minimum 50 px, previously 40).
  Landscape controls grow from 42 to 48 px; larger fonts can increase these
  heights further. The board remains fitted to the available space.
- Removes Sudoku's brief control-hint overlays and their settings switch.
  Save failures instead show a dismissible modal popup; failed saves still
  keep the game open for retry. Touch sampling is unchanged.

## v0.25.9 (diagnostic)

- S3_35B samples touch and the PWR expander in one task: touch every 20 ms,
  then TCA9554 EXIO6 on every fifth pass (nominally 100 ms). PMIC refresh stays
  nominally at ten seconds. LVGL consumes cached touch state.
- Restores the 14-byte, two-point touch report so deliberate two-finger
  gestures remain representable. Raw capture records 512 consecutive reports
  without suppressing expander reads. Physical comparison pending.

## v0.25.8 (diagnostic)

- During the 512-read raw touch capture, skip TCA9554 input-register reads;
  automatically resume after the last sample. The power button is unavailable
  for the capture window (typically about 20-25 seconds); its held timer and
  pressed status reset at capture start. PMIC refresh and all other behavior
  continue normally. Touch protocol/timing and TCA's 109 ms task delay are
  unchanged. Physical comparison pending.

## v0.25.7 (diagnostic)

- S3_35B changes only the TCA9554/PWR polling delay from 100 to 109 ms to
  compare touch-error periodicity. The 1 kHz RTOS tick preserves this value;
  actual intervals also include bus work and scheduling. The 2000 ms hold
  threshold remains (19 nominal samples instead of 20); nominal PMIC refresh
  is 92 samples, 10.028 s. Touch retains the 8-byte STOP-separated request,
  shared lock/100 us gaps, 30 ms poll limit and existing diagnostic capture.
  Physical comparison pending; this does not guarantee fewer coincidences.

## v0.25.6 (diagnostic)

- S3_35B keeps the 8-byte single-point touch request but sends the command
  and reads the response as separate I2C transfers with STOP between,
  following Espressif's transfer structure. One shared mutex covers both;
  the existing minimum 100 us bus gap also applies after the command.
  Clock, poll intervals, parser, dropout bound and diagnostic capture/bypass
  remain unchanged. Physical comparison pending.

## v0.25.5 (diagnostic)

- S3_35B requests and reads an 8-byte single-point touch report instead of
  14 bytes, matching Espressif AXS15231B 2.1.1's report size. Raw capture and
  invalid-report logs contain all 8 bytes. The combined command/read (no STOP
  between), 400 kHz clock, 30/100 ms touch/PWR polling, shared mutex/100 us gap,
  60 ms dropout bound and diagnostic click bypass are unchanged.
  Physical comparison pending; this is not a switch to Espressif's driver.

## v0.25.4 (diagnostic)

- S3_35B shared I2C users now take an explicit mutex and leave at least 100 us
  between transaction completions and the next start, including failed calls
  and probes. Applies to touch, TCA9554, AXP2101, PCF85063 and ES8311.
  The touch command/read remains one combined transaction; 400 kHz, touch
  polling at 30 ms, PWR polling at 100 ms and parser behavior are unchanged.
  Other board profiles retain their direct I2C calls. Physical comparison pending.
- Raw touch `dt_us` includes waiting for the new mutex/gap, not just the
  driver's transfer time; it is not an on-wire bus-occupancy measurement.

## v0.25.3 (diagnostic)

- S3_35B limits touch I2C polling to one read per 30 ms instead of 15 ms,
  for comparison with the factory example's observed cadence. Actual intervals
  still depend on LVGL scheduling and display transfers. PWR polling remains
  100 ms; protocol, 60 ms gap bound and raw capture are unchanged.
  Physical comparison pending.

## v0.25.2 (diagnostic)

- S3_35B polls the PWR-button expander every 100 ms instead of 20 ms,
  reducing its nominal I2C polling rate from 50 to 10 reads/second.
  The two-second shutdown hold and ten-second PMIC sampling period remain
  unchanged. Touch polling and the buffered capture are unchanged to isolate
  the effect of reduced expander traffic. Hardware comparison pending.

## v0.25.1 (diagnostic)

- S3_35B captures 512 raw touch reads in RAM after the first valid contact and
  prints them only after recording, with timestamps, transfer duration, transport
  status and all 14 bytes. Live touch diagnostic output is disabled during this
  comparison. Reset to rearm; dumping temporarily pauses the UI.
- Retains the disabled extra click veto and existing startup/gap handling.
  No touch protocol, IDF or LVGL version changes. Physical comparison pending.

## v0.25.0 (unreleased)

- Added the separate `S3_35B` Waveshare ESP32-S3-Touch-LCD-3.5B build:
  AXS15231B QSPI display/touch, TCA9554 shared reset, AXP2101 power monitoring,
  1-bit SDMMC, ES8311 audio and PCF85063 RTC. Hardware verification remains
  required; the owner's PCB revision is not confirmed.
- A completely erased (all logical bytes 0xFF) FFat volume is formatted on
  first boot, including after an earlier failed mount initialized wear-levelling
  metadata. Volumes containing any data are never formatted automatically;
  failed-mount wear-levelling handles are released before retry or explicit repair.
- S3_35B touch follows Waveshare's board-specific factory BSP: a combined
  command/read transaction and 14-byte report. Contact event 3 is accepted;
  corrupt/out-of-bounds reports are rejected rather than clamped into edge taps.
  Logs distinguish I2C failures from invalid reports and include the report bytes.
- S3_35B's exact all-`0x37` touch report is treated as no point, with the existing
  release debounce. The owner confirmed it occurs without contact and stops during
  contact. Other malformed reports and I2C failures remain errors.
- Battery voltage, USB presence, charging/completion and valid fuel-gauge
  percentage are read from the PMIC without ADC-divider calibration. Existing
  ADC-based boards retain their calibration and voltage-derived estimates.
  AXP2101 voltage is read directly in millivolts (the earlier 1.1 multiplier
  overstated it by 10%). Charger settings and supply voltages are not blindly
  overwritten.
- Independent screen zoom (85/100/115%) and text size in device/web settings,
  using P4's existing physical Normal typography and spacing as the reference.
  Missing zoom settings migrate to Normal. Menus/settings/results reflow or
  scroll; Sudoku, adventure HUD and karaoke text follow physical font roles
  while game boards remain fitted. Nominal panel dimensions still need
  measurement; integer font steps prevent exact equality.
- OTA images now carry a board identity and reject cross-board uploads,
  including the two incompatible ESP32-S3 boards. Once installed, this
  firmware also rejects old untagged images: reverting to them requires USB
  flashing. Initial upgrade from older firmware must use the correct board image.

## v0.24.7 (unreleased)

- Web song picker defaults to folder browsing (immediate subfolders and direct
  songs only) and has a separate all-songs view; playlist selection and creation
  stay above the song list. A dedicated playlist page manages contents, ordering,
  rename and delete without pop-up windows. Songs are started individually, not
  automatically queued.
  Choose and explicitly save a SoundFont per song without changing the global bank.
- Song tempo, key, melody channel/mute, MIDI gain and SoundFont are now previews:
  save/revert from the device song menu, or decide when leaving/changing songs.
  The web picker also exposes Save/Discard for the open song. Hardware master
  volume remains global and is still saved immediately.
- Registry writes are serialized and installed atomically; failed writes keep the
  previous saved state and leave the draft available for retry. Existing entries,
  playlists and favorites remain readable; optional playlist paths help the web
  picker resolve songs without hashing the entire library. Legacy playlists above
  the new creation/insertion limits are preserved through load/save. Discard
  restores the saved SoundFont and key together, and stale device confirmation
  dialogs cannot act on a song opened remotely.
- Device **All songs** now lists songs only, instead of prepending every
  subfolder (145 folders could fill several pages before the first song).
  Folder navigation stays available in the root browser. Letter jumps select
  the closest matching initial rank instead of the first matching item, tolerating
  an out-of-order cached list such as the S3 diagnostic where an Ö entry captured
  A–V jumps.
  Jumps reuse the current view instead of allocating another full song-index
  vector. Log lines identify the selected sort key, entry and page. Host coverage
  includes a deliberately misplaced Ö entry, 1,521 songs across 145 folders,
  visible targets, S3/P4, both landscape rotations and SV/EN. Physical retesting
  is still required.

## v0.24.6 (unreleased)

- Keep the host's thrown dice rotating and changing faces while waiting for
  the throwing ESP to acknowledge its displayed result. Held dice stay still;
  final faces are revealed only when the existing acknowledgement/disconnect/
  timeout gate releases. Previously the host paused on temporary faces.
- Owner confirms the editor improvements and the result-first principle work
  in v0.24.5; this animation follow-up still needs physical verification.

## v0.24.5 (unreleased)

- Native remote Yatzy rolls reveal their completed dice on the throwing
  client's display before the host, spectators or public HTTP state.
  Session-/seat-bound acknowledgements release the result after rendering;
  disconnect or a five-second timeout releases a stalled reveal with a warning.
  Browser-initiated rolls retain their existing behavior. Update both ESPs.
- Compact coordinate-layer rows include inline Show/Delete controls for
  treasures, with undo. Ending placement hides its list unless hidden layers
  were independently enabled.
- The adventure editor automatically restores per-tab drafts after refresh,
  including unsaved source, map/view/tool selections, treasure settings and
  bounded undo. Recovery does not fetch or overwrite the draft from SD;
  unavailable browser storage is reported visibly.
- Owner reports the initial native Yatzy/editor version looks good, but
  observed the host revealing a remote roll early. Physical verification
  of these follow-up changes remains pending.

## v0.24.4 (unreleased)

- ESP players joining a hosted Yatzy game now use the native Yatzy screen,
  dice/cup and score table. The host remains authoritative; other players'
  turns and connection loss block input. Periodic snapshots preserve local
  category selection. Browser play remains compatible and unchanged.
- Place `@treasures` directly in the adventure map editor: select an item
  and amount/level, enable placement and click multiple cells. Placements
  support undo, reveal hidden layers, retain comments/map scope and reject
  duplicate coordinates, mechanism cells and the 48-treasure limit.
- Includes v0.24.3 daily power logs. Physical S3/P4 mixed-device/browser
  play, reconnect and editor save/reload verification remain pending.

## v0.24.3 (unreleased)

- Deliver daily UTC power-history files (`power-YYYYMMDD.csv`) instead of the
  16-slot ring. Append across restarts; use boot-specific files until time is
  certain. Keep at most 150 matching log files when creating a file, removing
  the oldest by modification time. Legacy `power-NN.csv` files are retained
  until normal pruning; there is no per-file or total byte limit.
- Preserve the existing CSV schema and five-minute sampling. Physical
  verification of midnight rollover and restart behavior on S3/P4 is pending.

## v0.24.2 (unreleased)

- Record estimated active panel dimensions in the typed hardware profile and
  derive pixel pitch and axis PPI from the profile's logical resolution.
  Values are based on owner-reported pixel pitch, not independent
  measurements; the UI still scales from logical resolution.
- Document the reusable profile/evidence convention, physical display metrics,
  button/audio modeling thresholds and future cross-project hardware inventory.

## v0.24.1 (unreleased)

- Center the web Karaoke connection button beneath the lyric panel and let
  the song picker use the same responsive content width as the other pages.
  Mobile padding now shares the header gutter, avoiding horizontal overflow.
- Reuse the last known device name within the browser tab before painting
  page headers and titles. Live status still refreshes the name; failed
  requests show a connection warning rather than leaving the brand hidden.
- The owner reports that Sudoku works well (2026-11-03); specific board and
  restart/SD scenarios were not identified, so those checks remain open.

## v0.24.0 (unreleased)

- Native Sudoku under Pastimes, with digit-first entry, positional pencil
  notes, undo/redo, erase, pause, elapsed time and saved progress. Checking
  marks entered digits that differ from the solution with red crosses and
  ignores notes; rule conflicts are crossed immediately. Fixed clues ignore
  taps, and the per-cell inactive border is adjustable from 0 to 8 px
  (default 2) in Sudoku's Game settings, with a red live illustration.
  The model separately detects row, column and box conflicts.
  Completed keypad digits gray
  out, replacing an entered digit requires confirmation, pause includes the
  pending elapsed interval, and a failed save keeps the game open for retry.
  Tapping the selected digit in a filled cell toggles it off; a randomized
  animated cheer appears for 3.7 seconds when the puzzle is solved, followed by
  Back/Next choices in that order. Undo/redo use curved arrows. Notes matching a
  newly placed digit are removed from peer cells; larger puzzle status appears
  above the board.
- Bundle 100 puzzles per difficulty from the public-domain Sudoku Exchange
  bank, then load additional puzzles from matching files under
  `/sdcard/otr/sudoku/`. The selector shows the total available count and
  accepts `extreme.txt` or `diabolical.txt` for Extreme. Valid counts and block
  offsets are cached by file size and modification time, so unchanged files
  avoid rescanning; changed large files may take several minutes to index. A
  popup appears for new or changed files and stays until acknowledged, while
  the first-100 solved count remains visible.
  No scan task is started if no SD puzzle files are present. A confirmation-
  gated action in Sudoku's Game settings clears only its saved puzzle and solved
  counts from NVS; it leaves SD puzzle files and `.sidx` caches untouched.
- Make the Sudoku board use the full available portrait width, move Back to a
  header icon and alternate the header title with compact puzzle/time details.
  Brief contextual control hints can be disabled in Sudoku game settings.
- Add an animated mini-Sudoku home icon, controlled by the existing menu
  animation setting. Yatzy multiplayer behaviour is unchanged.
- Make the app/C6 OTA file pickers larger, with a highlighted drop area and
  larger native file-selection button.
- Log the first valid battery-voltage reading after boot immediately, then
  retain the five-minute cadence. SD power-history intervals are unchanged.
- Physical S3/P4 touch-layout verification remains pending.

## v0.23.24 (unreleased)

- Home-screen artwork animates a rolling die, village walker and musical
  notes at five frames per second, using built-in frames rather than GIF
  decoding or SD reads. Administration can disable animations (also available
  through the PIN-protected web setting); the choice persists across restarts.
- Animation updates stop while the screen is off or the home screen is
  inactive; disabling restores the original static icons. Menu structure is
  unchanged. S3/P4 hardware appearance and power measurements remain pending.

## v0.23.23 (unreleased)

- On-device Settings now groups game options, Players, user controls, and
  Administration; the configured admin PIN gates administrative settings.
  User screen-timeout and eco controls remain separate from technical power
  actions.
- Home, dice-game, and adventure pickers use compact image-left cards with
  bounded descriptions. Adventure cards display optional `@about cover` /
  `cover.image` PNGs; missing or invalid declared images use the fallback icon
  and log a warning.
- Static illustrations distinguish the home game families and dice-game
  variants. Settings can choose Yatzy, Maxi, or Midi in-order variants; existing
  saved variant selections retain their meanings.
- Settings-toggle, Adventure UI, and source-level menu checks pass.
  LVGL-dependent layout builds cannot configure here because
  `managed_components/lvgl__lvgl` is absent. ESP32-S3/P4 hardware verification
  remains pending.

## v0.23.22 (unreleased)

- Review fixes keep Wi-Fi/multiplayer screens from navigating away before
  their cleanup, synchronize Settings toggles with status-bar changes, accept
  4–8 digit admin PINs locally, add authenticated saved-network loading, and
  prevent the shared web navigation from overflowing beside desktop scrollbars.
- Settings-toggle and web-shell host checks pass; the production keypad/layout
  host test could not run here because `managed_components/lvgl__lvgl` is absent.
  S3/P4 hardware checks remain pending.
- Memory-aware SD uploads: extra parallel uploads are admitted only while
  internal RAM keeps a 40 KiB floor (HTTP 429 otherwise; the browser waits
  and retries). The file list reports `upload_slots`; the internal DMA
  write cache is used only when the floor survives. HTTP 429/503 now
  have correct status lines (503 was sent as 500).
- File manager mkdir no longer restarts background music; only changes
  under `/otr/music` restart it and only `/otr/sounds` reloads clips.
- Background music pauses while an OTA update runs (owner report on S3)
  and resumes at the same position if the update fails or is aborted.
  Effects and karaoke are unaffected. Owner confirmed on S3 (v0.23.22):
  the music went silent during OTA.
- OTA image/rollback state is read once at boot, so status polling no
  longer maps otadata (owner S3 log: `mmap otadata failed 0x101`).
- Owner S3 hardware test (v0.23.22): uploads ran one file at a time and a
  batch of more than 500 files completed without problems.

## v0.23.21 (unreleased)

- Karaoke overload guard: when a 5 s diagnostics period averages ≥100 %
  render load or has ≥5 late mixer blocks, playback is paused, notes are
  released and the device shows that the SF2 or voice count is too
  demanding for this hardware. Prompted by an owner S3 test with a
  ~55 MB SF2 at 96 voices (stutter + task-watchdog warnings, no reboot).
  Documented as unsupported in the karaoke reference. Owner confirmed on
  S3 (v0.23.21): the warning appeared when the watchdog triggered.

## v0.23.20 (unreleased)

- P4 panel sleep now runs on every screen-off (timeout or manual), not
  only in eco idle. Owner log from v0.23.19 showed plain screen-off
  only turned the backlight off. A failed plain panel sleep is logged
  and keeps the backlight off; touch wake is unchanged. Hardware check
  pending. S3 is unchanged.

## v0.23.19 (unreleased)

- P4 eco idle also sends ST7701 display-off/sleep-in, suppresses LVGL
  framebuffer updates during panel sleep, and wakes the panel before
  restoring backlight. Failed transitions are logged and wake retries
  are rate-limited. CPU/touch stay awake; DSI stream and panel power
  remain enabled. Hardware power savings and wake behavior are pending.
- Local Settings adds a confirmed software restart through the shared
  web restart task. Transfers/OTA block restart; task creation failures
  are reported. FFat event logging is quiesced before restart. Owner
  confirmed that restart from local Settings works.
- S3 panel behavior is unchanged; it can remain on v0.23.18 during the
  separate SPI-crash observation. No C6 firmware update is needed.

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
