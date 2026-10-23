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

## v0.5.0 – RTC, ny tärningsanimation, live-webbsida ✅ 🔧
- [x] DS3231 RTC (RPi-modul "DS3231 for Pi", stift `- NC C D +`) på den delade I2C-bussen (SDA IO38, SCL IO39, 3V3) – ny delad komponent `my_rtc`
- [x] Funktionstest vid start: svarar på 0x68, register läsbara, OSF (tiden överlevde på batteri), giltigt datum, sekunden tickar, temperatur
- [x] Ny rad "Klocka (DS3231 RTC)" i bootlistan (tid UTC + temperatur, eller felorsak); 2 × orange LED-blink vid fel
- [x] RTC lagrar UTC; systemklockan sätts från RTC vid start (källa "RTC" = säker), RTC skrivs efter NTP-synk/manuell inställning och vid ≥ 1 s avvikelse (kontroll var 10:e min)
- [x] Referenstid för loggar: varje rad lagrar drifttid; när klockan blir säker (RTC/NTP/manuell) visas även tidigare rader i samma boot med riktig tid. Händelser före klockan köas (max 32 st / 180 s) och skrivs med korrigerad tid
- [x] RTC-status i `/api/info` (`rtc`) och på diagnostiksidan
- [x] Tärningar som förrenderade sprites (ARGB8888 i PSRAM): gradient, mjuk glans, insänkta prickar; en `lv_image` per tärning roteras/skalas billigt
- [x] Ny rullning: alla tärningar kastas in från samma sida, ease-out-bana, tre avtagande studsar, 1,25–2,25 varv som rullar ut på rätt sida, liten "duns" vid landning, 45 ms förskjutning
- [x] Live-webbsida `/game` (skrivskyddad, öppen): tärningar (låsta markerade), protokoll för alla spelare, summa/bonus/totalt, vinnare vid slut, helskärmsknapp. Pollar `/api/game?v=` var 700 ms
- [x] Värdtest `tests/test_rtc.cpp` (BCD/datum, skottår, veckodag, 12 h-läge, rundtur 2024–2099)
- [ ] HW-test (RTC-modulen, animation, livesidan)

## v0.6.0 – Del B: historik och statistik ⏳
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
- [ ] Webbspel: spela från mobilen med 4-siffrig kod som visas på enheten

## Möjliga framtida förbättringar
- Partitionering 6 + 6 MB app (OTA) + ~4 MB lagring (i dag 3 + 3 MB app + 9 MB FFat) – kräver USB-flash och flytt av spelarfilen
- Packad lagring med miniz i ROM (`tinfl`/`tdefl`): komprimera i PSRAM och spara packat på FFat (historik/loggar); görs som delad komponent så att den kan återanvändas i MyPi-projekten
