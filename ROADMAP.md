# MyEsp-OnTheRoad – Roadmap

Status per 2026-10-23. ✅ = klart (kompilerar + värdtester), 🔧 = HW-test återstår, ⏳ = planerat.

## v0.2.0 – grund ✅ (HW-verifierad)
- Display ST77922 QSPI, touch, LED, bakgrundsbelysning, bootskärm med självtest
- Yatzy / Maxi-Yatzy (fritt och i ordning), 1–8 spelare
- Wi-Fi STA med inställning på enheten, webbsidor (översikt, inställningar, diagnostik)

## v0.3.0 – Del A ✅ 🔧
- [x] HW-encodern borttagen; skärm-scrollhjul (`ui_charwheel` + `mypi_kbd_core.h`, samma modell som MyPi-Hotspot)
- [x] OTA via webben (`/update`, app0/app1, förlopp på webb och skärm, admin-PIN)
- [x] 50 sparade Wi-Fi-nätverk i prioritetsordning, auto-anslutning via skanning
- [x] Klocka: NTP + tidszon + manuell inställning (enhet och webb), sparas över omstart
- [x] Spelarregister: 200 spelare, unika namn, fasta id, FFat-lagring, välj spelare före spel, "Gäst"
- [x] Webb-CRUD för spelare (`/players`, borttagning kräver admin-PIN)
- [x] Admin-PIN (OTA, fabriksåterställning, borttagning av spelare, byte av PIN)
- [ ] HW-test av allt ovan

## v0.3.1 – Wi-Fi-roaming ✅ 🔧
- [x] Ansluter till starkaste AP:n (BSSID) för SSID:t i stället för första hittade (FAST_SCAN → ALL_CHANNEL_SCAN + BSSID-låsning från egen skanning)
- [x] Bakgrundsskanning när ansluten: var 5:e min (≥ −55 dBm), 60 s (≥ −65), 30 s (≥ −72), annars 15 s
- [x] Byter AP vid ≥ 8 dB starkare kandidat (≥ 5 dB när länken är < −75 dBm), 30 s spärr mellan byten (`roam_policy.h`, värdtestad)
- [x] Vid tappad länk: drivrutinen får 6 s, sedan ny skanning och starkaste AP
- [x] BSSID/kanal/antal roaming i `/api/info`, diagnostik, webbinställningar och enhetens inställningsskärm
- [ ] HW-test (fyra AP:er hemma hos Kent)

## v0.4.0 – ESP-IDF 6.1 + LVGL 9.6 ✅ 🔧
- [x] Portad från Arduino-ESP32 3.3 + LVGL 8.4 till ESP-IDF 6.1 + LVGL 9.6 (esp_lcd, esp_http_server, cJSON)
- [x] Delade komponenter `components/my_*` (log, storage, time, diag, wifi, ota, web, lvgl, ui) för återbruk i MyPi-projekten
- [x] Samma partitionstabell som Arduino – spelarfilen på FFat behålls; NVS-nycklar migreras
- [x] Bygger med 0 fel, värdtester 324 + 67 OK
- [ ] HW-test (första flash via USB med merged-bin, sedan OTA)
- [ ] Byt ut föråldrade `lv_obj_add/remove_flag` mot LVGL 9.6-setters (bara varningar)

## v0.5.0 – Del B: historik och statistik ⏳
- [ ] Spara varje avslutat spel på FFat: datum/tid (+ klockans säkerhet), variant/läge, spelar-id, poäng per ruta, slutpoäng, placering
- [ ] Statistik per spelare: antal spel, vinster, snitt, bästa, Yatzy-antal, bonusfrekvens – per variant
- [ ] Statistik per par (ordningsoberoende, A+B = B+A): inbördes möten, vinster, snitt
- [ ] Topplistor på enheten och på webben
- [ ] Admin: radera enskilda resultat (admin-PIN), nollställ statistik
- [ ] Export (CSV/JSON) via webben

## Senare ⏳
- [ ] AP-läge (uppskjutet – `AP_MODE_ENABLED 0`)
- [ ] Admin-roll per spelare (flaggfältet i `PlayerRec` är reserverat)
- [ ] Ljud (ES8311), SD-kort, batterimätare
- [ ] Spara pågående spel vid strömavbrott
- [ ] 802.11k/v/r-roaming (möjligt nu med ESP-IDF)
