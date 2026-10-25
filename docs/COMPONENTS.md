# Återanvändbara komponenter – status och plan

Senast uppdaterad: 2026-10-25 (main efter v0.10.0).

Målet är att allt som inte är OnTheRoad-specifikt ska kunna lyftas in i syskonprojekten
(MyPi-Companion, MyPi-Hotspot) utan ändringar. Regeln: en komponent under `components/` får **inte**
inkludera något från `main/` (inga `board_pins.h`, `app_config.h`, `lang.h`). Hårdvara och inställningar
skickas in som parametrar/callbacks.

## 1. Utbrutna komponenter i dag (`components/`)

| Komponent | Innehåll | Beroenden | Hosttestad | Används i |
|---|---|---|---|---|
| `my_log` | Ringbuffer-logg, beständig händelselogg, `my_sys.h` (millis, uptime) | ESP-IDF (log, esp_timer) | – | alla |
| `my_storage` | NVS-hjälp, FAT-montering (wear levelling), atomisk filskrivning (tmp + rename) | nvs_flash, fatfs | – | spelarfil, sparningar |
| `my_time` | SNTP, POSIX-tidszon, manuell tid, RTC som källa, senaste tid i NVS | my_log, my_storage, lwip (RTC kopplas in av appen) | – | klocka, logg |
| `my_rtc` | DS3231 på delad `i2c_master`-buss, UTC-läs/skriv på sekundflank, driftsynk | esp_driver_i2c, my_log | `my_rtc_time.h` (test_rtc) | klocka |
| `my_diag` | System/heap/PSRAM/partitioner/återstartsorsak som JSON, tasklista | cJSON, my_log, my_storage | – | `/api/info` |
| `my_wifi` | STA med sparade nät (`net_list.h`), starkaste BSSID, roaming (`roam_policy.h`), mDNS | esp_wifi, mdns, my_log | `net_list.h`, `roam_policy.h` (test_core) | Wi-Fi |
| `my_ota` | OTA till inaktiv slot, rollback/mark-valid | app_update, my_log | – | `/update` |
| `my_web` | `esp_http_server`-omslag: rutter, gzip-assets, JSON-hjälp, admin-PIN, logg/OTA/omstart-rutter | esp_http_server, cJSON, my_log, my_ota | – | webbsidor |
| `my_lvgl` | LVGL 9: tick, task, rekursiv mutex, ritbuffertar (PSRAM eller intern RAM), pekdon via callback | LVGL | – | all UI |
| `my_ui` | Tema (färger, kort, knappar, msgbox-fonter), fonter med å/ä/ö + symboler, skrollhjulstangentbord (`my_charwheel`, `mypi_kbd_core.h`), textinmatning, **`Theme::debounce()`** (dubbeltryckskydd, ny) | LVGL | `mypi_kbd_core.h` (test_core) | all UI |
| `otr_engine` | Äventyrsmotorn: `otr_world` (rutnät, terräng/objekt, regler, skatter, sparformat, A*-planerare Pro/Assist), `otr_maps` (inbyggda världar), `otr_joystick.h` (zoner, takt, diagonal) | ingen (ren C++17) | test_adventure, test_joystick | äventyret |

### Nytt sedan v0.10.0
- `Theme::debounce(uint32_t &last, uint32_t ms = 500)` i `my_ui/include/my_theme.h` – generellt skydd
  mot dubbeltryck för toggles. Används i inställningarnas switchar och i äventyret.
- Släppfilter (release debounce) i touchdrivern `main/board_touch.cpp` – fortfarande i `main/` eftersom
  drivern är kortspecifik (ST77922-touch, 0x55). Se punkt 2.

## 2. Planerade utbrytningar

Ordnat efter nytta. Inget av detta är gjort ännu.

| Prio | Ny komponent | Från (`main/`) | Vad som krävs | Varför |
|---|---|---|---|---|
| 1 | `my_audio` | `audio_synth.*` (synt, MML, WAV-parser), mixer-delen av `board_audio.*` | Dela upp i ren mixer/synt (hosttestbar) + kodekdrivare (ES8311) som eget lager; ljudbanken (`sound_bank.*`) stannar i projektet | Ljud behövs i MyPi-projekten; synten är redan hosttestad |
| 2 | `my_sd` + `my_fsweb` | `board_sd.*`, `fs_path.*`, `app_web_files.*`, `web/files.html` | Pinnar och monteringspunkter som parametrar; filbläddrarsidan flyttas till komponentens `web/` | Webbfilbläddrare + säkra sökvägar är generellt användbart |
| 3 | `my_touch` (generisk del) | Släppfiltret + IRQ/poll-logiken i `board_touch.cpp` | Filtret som ren funktion (hosttestbar), kontrollerspecifik läsning som callback | Samma dubbeltrycksproblem kan uppstå på MyPi-Hotspot-skärmen |
| 4 | `my_ui` utökning | `ui_numpad.*` (PIN-tangentbord), generell msgbox-/dialoghjälp | Ta bort `lang.h`-beroendet (texter in som parametrar) | PIN-inmatning finns även i syskonprojekten |
| 5 | `my_i18n` | `lang/lang.h`-mekaniken (X-makro, `tr()`, `trp()`, reservspråk) | Strängtabellerna stannar i projektet; bara mekanismen flyttas | Samma språkstöd i alla projekt |
| 6 | `otr_story` (ny, del av äventyret) | – (ny kod) | Dialogträd, flaggor, uppdrag, NPC:er – ren C++ som `otr_engine` | Se [BIBLE_ADVENTURE_PLAN.md](BIBLE_ADVENTURE_PLAN.md) |
| 7 | `dice_engine` | `game_*.cpp`, `game_base.h`, `dice_cup.*`, `game_live.*` | Redan ren C++; behöver bara egen CMake-komponent | Återanvändning i ett annat tärningsprojekt; låg prio |

### Stannar i projektet
`board_pins.h`, `board_display/backlight/rgb`, `app_config`, `app_players`/`player_db.h`, `app_web.cpp`
(projektets rutter), alla `ui_*`-skärmar, `sound_bank.*` och strängtabellerna.

## 3. Arbetssätt vid utbrytning
1. Flytta filerna oförändrade till `components/<namn>/` (+ `include/`), lägg till `CMakeLists.txt`.
2. Ersätt beroenden till `main/` med parametrar/callbacks; inga globala konfigurationsläsningar.
3. Hosttest flyttas med (eller skapas) och läggs till i README-testlistan.
4. Bygg med 0 varningar, alla tester gröna, en commit per komponent.
