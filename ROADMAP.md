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

## v0.4.0 – Del B: historik och statistik ⏳
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
- [ ] Möjlig migrering till ESP-IDF
