# MyEsp-OnTheRoad – Roadmap

Status per 2026-10-24. ✅ = klart (kompilerar + värdtester), 🔧 = HW-test återstår, ⏳ = planerat.

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
- [x] Byt ut föråldrade `lv_obj_add/remove_flag` mot LVGL 9.6-setters (gjort i v0.5.1, bygget är varningsfritt)

## v0.5.0 – RTC, ny tärningsanimation, live-webbsida ✅ (RTC + animation HW-verifierade) 🔧
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
- [x] HW-test RTC-modulen (Kent 2026-10: sattes via NTP vid första start, andra start OK från RTC)
- [x] HW-test tärningsanimationen (flyter bra)
- [ ] HW-test livesidan `/game`

## v0.5.1 – Bägaren, varningsfritt bygge ✅ 🔧
- [x] **Bägaren** (Kents idé): i kastfasen med kast kvar blir nedre delen av skärmen (där förslagskortet och knapparna ligger) en läderbägare sedd ovanifrån – brun kant med gradient, skugga och glans, mörk insida, filtbotten. Tärningarna som ska kastas (inte de i boet; vid första kastet alla) glider in i bägaren och skramlar där i ~70 % storlek
- [x] Skaka: håll fingret i bägaren och rör fram och tillbaka eller i cirklar. Bägaren följer fingret (35 % av förskjutningen, max ±18 px) och insidan förskjuts mot kanten (parallax = lätt lutning). Bägarens acceleration knuffar tärningarna, som studsar mot väggen och varandra (enkel fysik) och byter sida vid knuffar. LED blinkar varmt vid varje knuff, starkare vid hårdare stöt
- [x] Kast: snabbt svep uppåt (≥ 500 px/s, högst ~50° från rakt upp) eller att fingret lämnar bägaren uppåt (≥ 8 px ovanför kanten, ≥ 150 px/s). Släpp utan svep = inget kast. Kort tryck (≤ 350 ms, ≤ 10 px) = standardkast. Kompakt [Kasta]-knapp i bägarytan
- [x] Gesten formar animationen (värdena kommer fortfarande från spelmotorn): start där tärningarna ligger i bägaren, svepets vinkel (±0,6 rad) och fart ger bana (kvadratisk Bezier), räckvidd, längd 600–1400 ms och 2–4 studsar; skakenergin ger antal varv och spridning; snurriktningen ger rotationsriktning och en lätt kurva
- [x] "Kast N kvar" vid bägaren, hint "Skaka och svep uppåt" tills första bägarkastet i varje spel, [Välj >] och [Ordna] (lägger undan bägaren så att tärningar kan flyttas) efter första kastet
- [x] Efter kastet glider bägaren bort och förslagen + [Kasta n] [Välj ruta] visas; [Kasta n] tar fram bägaren igen. Bägaren visas också efter "< Kasta n" från protokollet. En tärning som tas ur boet medan bägaren är framme hamnar i bägaren
- [x] Uppdatering via LVGL-timer 16 ms bara när bägaren är aktiv, inga allokeringar per bildruta, skyddad med `s_gen` och stoppas när spelet lämnas
- [x] Ren gest-/fysikmatematik i `main/dice_cup.h/.cpp` (utan LVGL) + värdtest `tests/test_cup.cpp` (50 kontroller)
- [x] Inställning "Kastsätt: Bägare / Knapp" (standard Bägare) på enheten och webben (NVS `throw_mode`, `/api/config` `throw_mode`)
- [x] Livesidan `/game` visar "skakar bägaren..." (fält `shaking` i `/api/game`)
- [x] Alla föråldrade LVGL-anrop (`lv_obj_add/remove/has_flag`, 49 st) bytta mot 9.6-setters (`lv_obj_set_hidden/clickable/...`, `lv_obj_is_hidden`) – bygget har 0 varningar
- [ ] HW-test (känsla, trösklar och bildfrekvens i bägaren måste provas på enheten)

## v0.5.2 – Språk (SV/EN), msgbox-font, webbspel ✅ 🔧 (inte HW-testad)
- [x] **Msgbox-font:** "Avsluta spelet?" visade "Forts#tt" – msgboxen (titel, text, footer-knappar) får nu appens Latin-1-font (`Theme::msgboxFonts`), i alla msgbox-dialoger
- [x] **i18n:** `main/lang/` – `enum class Str` (216 strängar), `tr()`/`trp()`, `Lang::setLang/currentLang`, tabeller `lang_sv.cpp` (bas) och `lang_en.cpp`; saknad översättning faller tillbaka till svenska. Instruktion för nya språk i `lang.h`
- [x] Alla skärmar på enheten (boot, meny, spelare, spel, resultat, inställningar, Wi-Fi, klocka, OTA) och API-felmeddelanden går via `tr()`
- [x] Språkval på enheten (Inställningar → Språk), på webben (Inställningar → Språk / Language) och i `/api/config` (`lang`, lista `langs`); NVS-nyckel `lang`, standard `sv`
- [x] Webbsidorna `/play` och `/game` är tvåspråkiga (JS-ordbok, språk från API:t); övriga webbsidor är fortfarande på svenska
- [x] Värdtest `tests/test_lang.cpp`: alla id finns i alla språk, printf-platshållare och radbrytningar matchar, setLang/fallback, trp
- [x] **Webbspel:** en 4-siffrig spelkod (1000–9999) skapas när spelet startar (behålls vid "Spela igen") och visas i spelhuvudet (när någon spelar via webben), på `/game` och i väntepanelen
- [x] Spelaruppsättning: wifi-knapp per rad markerar spelaren som webbspelare (även gäster)
- [x] Webbspelarens tur: enheten visar "Väntar på X via webben..." + URL och kod tills en mobil är ansluten; lokal input (tärningar, bägare, knappar, protokoll) blockeras. Kast från webben körs med vanlig kastanimation på enheten
- [x] WebSocket `/ws/game` (max 4 klienter): `join` (kod + plats eller åskådare), `hold`, `roll`, `choose`; enheten skickar `state` vid varje ändring. Kommandon går via en kö till LVGL-tråden
- [x] `/play`: lobby (kod), val av spelare, live-vy med klickbara tärningar, Kasta och rutval med poäng; mobilanpassad (360 px), ingen polling
- [x] `/api/game` har nya fält `code`, `web_players`, `lang`, `can_roll`, `has_rolled`, `potential` per ruta (bakåtkompatibelt)
- [ ] HW-test (språkbyte, msgbox, webbspel med 1–2 mobiler)

## v0.5.5 – Webbspel: klienter registreras igen 🔧 (inte HW-testad)
- [x] `/ws/game`: ESP-IDF 6.x anropar inte handlern vid WebSocket-handskakningen, så klienten registrerades aldrig och `join` ignorerades tyst (varken rätt eller fel kod gav svar). Klienten registreras nu vid första meddelandet (`wsRegister`)
- [x] HW-test (anslutning från mobil, rätt och fel kod) – verifierat av Kent

## v0.5.4 – Webbspel: auto-join och spökanslutningar 🔧 (inte HW-testad)
- [x] `/play`: auto-join till enda webbspelaren sker så länge mobilen är åskådare (`you < 0`), även om platsen redan rapporteras som ansluten (race när join-kommandona processas utan state-frame emellan)
- [x] `/ws/game`: döda/halvöppna WebSocket-sessioner rensas direkt vid ny anslutning (`wsReapDead`) innan "för många klienter" ges – tidigare bara en gång per sekund
- [ ] HW-test (anslutning från mobil, flera återanslutningar)

## v0.5.3 – Webbspel: tydligare anslutning i `/play` 🔧 (inte HW-testad)
- [x] `/play`: meddelande vid för kort kod, Anslut-knappen visar "Ansluter..." (återställs efter 5 s utan svar), besked när WebSocket inte är öppen än, felmeddelandet scrollas in i bild på mobil, `?debug=1` loggar WS-trafik i konsolen
- [ ] HW-test (anslutning från mobil)

## v0.5.6 – Rotation, personlig PIN och ny `/play` 🔧 (inte HW-testad)
- [x] **Rotation 180°:** nytt fält `rotation` (0/180, standard 0 = USB nedåt) i `AppConfig`, sparas i NVS; val i Inställningar på enheten (förhandsvisas direkt, återställs vid Avbryt) och på webben (`/api/config`)
- [x] Display: rotation via MADCTL (0x36) = 0xC0 (MX|MY) i `sendInitSequence` och vid byte under drift; skärmen ritas om. Touch: x/y speglas i `pointerRead` när roterad. **Obs:** MADCTL-spegling på ST77922 över QSPI är oprövad – om bilden hamnar förskjuten/fel är reservplanen LVGL-mjukvarurotation (`lv_display_set_rotation`), ej implementerad än
- [x] **Personlig PIN** (4 siffror) per spelare: `PlayerRec.pin`, `validPin/hasPin/checkPin/setPin`, spelarfilen version 2 med automatisk migrering från v1. PIN skickas aldrig ut, bara `has_pin`
- [x] Enheten: nytt numeriskt tangentbord (`ui_numpad`, stora knappar 0–9, ⌫, OK/Avbryt). Visas när en spelare utan PIN markeras som webbspelare, och (valfritt) vid registrering av ny spelare
- [x] `/players`: PIN kan sättas/tas bort per spelare och anges vid ny spelare; listan visar "PIN ✓"
- [x] **Spelkoden borttagen** helt (spelhuvud, `/game`, protokoll). Ersatt av internt sessions-id i `GameLive` som räknas upp vid varje nytt spel – gamla anslutningar blir ogiltiga automatiskt
- [x] WebSocket-protokoll: `{"cmd":"join","player":plats,"pin":"1234"}` (utan player = åskådare). Felkoder `nogame`, `seat`, `pin`, `nopin` (spelaren saknar PIN – sätt den på enheten eller `/players`), `locked` (med `retry_s`). Gissningsskydd: 5 fel per anslutning och 5 fel per plats ger 60 s spärr
- [x] Ny `/play`: startar som åskådare, lista över webbspelare (namn + ansluten), klick → PIN-tangentbord på webben; plats+PIN sparas i localStorage för automatisk återanslutning (rensas vid PIN-fel); hold/roll/choose oförändrade
- [x] `/` (översikt): tydliga knappar till Live (`/game`) och Spela (`/play`)
- [x] Värdtester: PIN-validering och migrering v1 → v2 (test_core 360/360), test_lang 228 strängar
- [ ] HW-test (rotation 0/180 inkl. touch, PIN-dialog, join med rätt/fel PIN, spärr, återanslutning)

## v0.5.7 – Responsiv layout ⏳
- [ ] `/play` på bred skärm (≥ 900 px): kolumner med tärningar/kast och hela poänglistan, bara egen kolumn klickbar
- [ ] `/game`: delad vy (aktuellt spel vänster, komplett poänglista höger) för stor skärm; ev. animerade tärningar via `/ws/game`

## v0.6.0 – Del B: historik och statistik ⏳
- [ ] Spara varje avslutat spel på FFat: datum/tid (+ klockans säkerhet), variant/läge, spelar-id, poäng per ruta, slutpoäng, placering
- [ ] Statistik per spelare: antal spel, vinster, snitt, bästa, Yatzy-antal, bonusfrekvens – per variant
- [ ] Statistik per par (ordningsoberoende, A+B = B+A): inbördes möten, vinster, snitt
- [ ] Topplistor på enheten och på webben
- [ ] Admin: radera enskilda resultat (admin-PIN), nollställ statistik
- [ ] Export (CSV/JSON) via webben

## v0.10.0 – Kents HW-feedback på v0.9.0 🔧 (inte HW-testad)
- [x] Äventyret: toppstatusbaren borttagen, spelvyn börjar på y=0. Ny kontrollpanel (160 px): vänster status (liv, nycklar, båt) + menyknapp (hamburgare) och zoomknapp (-/+), mitten stor centrerad joystick (radie 74, stoppzon 23), höger Slå-ruta, röd energistapel, röd diamant + ädelstenar "11/13", "Dräkt N"
- [x] Joystick: halverad gånghastighet, std 300/150 ms (sparade v0.9.0-standardvärden 150/75 uppgraderas), intervall 60-600 ms; gånganimationen sträcks till takten (max 260 ms) så hjälten glider jämnt
- [x] Diagonalzoner: rakt 0±30°, diagonal 45±15° (tan 30° = 577/1000), gränsfallstester
- [x] Bro-bugg: A* i Pro-läge såg lava och mark som lika dyra, så tryck-för-att-gå kunde skära genom lavan bredvid basaltbron. Nu vinner vägen med minst lava bland lika korta vägar; värdtest går fram och tillbaka över bron (tryck och joystick)
- [x] Webböversikten visar SD-kort (monterat, 4-bit/1-bit, MHz, ledigt/totalt), FFat och startkontrollens resultat (samma som startbilden)
- [ ] HW-test: ny layout, gånghastighet/-animation, diagonalkänsla, bron, webböversikt

## v0.9.0 – Ljud, microSD, filbläddrare, joystick 🔧 (HW-testad delvis: ljud, SD, filbläddrare, sparning OK)
- [x] Ljud via ES8311 (egen registerdrivare, säker uppstart: förstärkare av -> codec -> I2S 22 050 Hz -> låg volym -> förstärkare på). Saknas codec: tyst, ingen krasch
- [x] Mixer-task: 1 musikkanal + 4 effektkanaler. Egen chiptune-synt (fyrkant/triangel/brus, ADSR, MML-melodier) som standard; WAV från SD ersätter per ljud. Ingen OPL/MIDI-emulator: de lätta alternativen är GPL eller för tunga - egen synt valdes
- [x] Ljudhändelser i tärningsspelen (skak, kast, landning, poäng, noll, Yatzy, bust, vinst, knappklick) och äventyret (steg, slag, plock, dörr, mina, skada, liv, checkpoint, utgång). Musikslinga i menyn/tärningsspel och i äventyret
- [x] Inställningar (enhet + webb): ljud på/av, effektvolym (60), musik på/av, musikvolym (25)
- [x] microSD 4-bit SDMMC (20 MHz) med 1-bit som reserv, klarar att kort saknas/tas ur; status i diagnostik
- [x] Webbfilbläddrare `/files.html`: hela SD (och valbart intern flash), lista, ladda ner, ladda upp (strömmat), radera, byt namn, ny katalog, läs in kortet igen; admin-PIN för ändringar; säkra sökvägar
- [x] Äventyrssparning på SD (`/sdcard/otr/adventure/saves/`) med intern flash som reserv; `maps/` förberedd
- [x] Joystick ersätter pilarna: ring med 4 pilar, röd stoppzon, knopp under fingret. 1-4 hastighetszoner (std 2), takt min/max (std 150/75 ms), håll för upprepning (std 300 ms)
- [x] Diagonal: dominant axel per steg (std); "växla axlar" som inställning. Ingen äkta 8-riktad rörelse/hörnklippning
- [ ] HW-test: ES8311-uppstart och volym, WAV från SD, SD-montering 4-bit, filbläddrare med stora filer, joystickkänsla
- Öppet: MP3, ADPCM, mikrofon, karta från SD, effekt-WAV max 2 MB totalt i PSRAM

## v0.8.0 – Äventyrsmotorn: gå med tryck, energi, liv, skatter, Minfältet, sparning ✅ 🔧 (inte HW-testad)
- [x] Tryck för att gå: A* (begränsad, 60 × 60), steg för steg runt hinder, snäpp inom 1/3 ruta, "Dit kan jag inte gå" vid hinder; tryck på figuren = slå; STOP mitt i styrplattan
- [x] Läge Pro / Assistent (NVS), Assistenten stannar före dödlig lava ("Det ser farligt ut!") och okänd mark; båt krävs för vatten
- [x] Energi (start 8, tak 20, stapel uppe till höger), dryck och hjärta, liv (ändliga eller oändliga = "använda liv"), game over med Börja om / Fortsätt
- [x] Skyddsdräkt: 3 + 1 laddning per permanent extra maxenhet, lava 3 / mina 4 energi
- [x] Datadrivna skatter under låda, buske, sprucken vägg, sten och dörr
- [x] Ny värld "Minfältet" (minröjarsiffror, flood fill, flaggor, 3 liv) + världsväljare
- [x] Sparning per spelare och värld på FFat (versionerat format + CRC32, tål trasiga filer), Fortsätt / Nytt spel, gäst sparas inte
- [x] Kompakt rubrik + meny (läge, spara, börja om, lämna), sv + en
- [x] Värdtester: test_adventure 628, test_lang 1393 (341 strängar)
- [ ] HW-test: tryck-gång och snäpp, render-tid (~7 ms), sparning/laddning, Minfältet
- [ ] Senare: planeraren puttar stenar / förutser båt, riktiga tilesets

## v0.7.2 – Webbplats som förvalts med WiFi-ikonen kan lämnas tillbaka ✅ 🔧 (inte HW-testad)
- [x] Bugg: förvald webbplats (WiFi-ikonen i spelarlistan) låstes kvar i "Väntar på ... via webben" efter "Fortsätt på enheten" – `s_web[]` rensades aldrig
- [x] `leave` från `/play` (både "Fortsätt på enheten" och "Byt spelare") släpper platsen: `GameLive::releaseWeb()` rensar webbflaggan i snapshoten (ingen ny session) och UI:t rensar `s_web[plats]` – enheten fortsätter lokalt direkt, webben kan ta platsen igen senare
- [x] Ny knapp "Spela på enheten" i vänterutan (Yatzy, Pig, Farkle, SCC) när ingen webbläsare är ansluten till platsen
- [x] Vanligt avbrott (telefonen tappar WiFi, inget `leave`) släpper inte en förvald webbplats – enheten väntar, knappen finns
- [ ] HW-test: Pig 1 spelare med WiFi-ikon → anslut i `/play` → "Fortsätt på enheten"; samma med knappen på enheten

## v0.7.1 – Tärningsvärden avslöjas först när tärningarna stannat ✅ 🔧 (inte HW-testad)
- [x] Pig/Farkle/SCC: kastet görs på en kopia av motorn som ersätter den först när tärningarna stannat – turpoäng, bust/Farkle, skepp/kapten/besättning, last och poängtavla visar läget före kastet tills dess; ledtråden visar "Tärningarna rullar..."
- [x] Yatzy + alla spel på webben: snapshoten under `rolling` är läget före kastet + `roll_mask[]` (tärningar i luften); `/play` och `/game` använder masken
- [x] Värdtester: test_games 137/137 (nytt: kast på kopia)
- [ ] HW-test: Pig 1/2, Farkle (inkl. hot dice), SCC, Yatzy – på enheten och i `/play` / `/game`

## v0.7.0 – Äventyrsmotor, proof of concept ✅ 🔧 (inte HW-testad)
- [x] `components/otr_engine`: `Otr::World` (terräng/objekt/spelare, händelser) + 60 × 60 testvärld – separat modul, tärningsspelen orörda
- [x] `main/ui_adventure.cpp`: egen tile-renderare på RGB565-canvas i PSRAM, mjuk scroll, zoom 32/16 px, styrplatta, Slå, fps-statistik; menykort "Äventyr (test)"
- [x] Värdtester: test_adventure 258/258 (full genomspelning 13/13 ädelstenar)
- [x] Brief, kommentarer och PoC-plan: [docs/ENGINE_2D_BRIEF.md](docs/ENGINE_2D_BRIEF.md)
- [ ] HW-test: fps vid scroll (32 och 16 px), touch-styrning, minne
- [ ] Nästa: SD + ES8311, kartformat/`tmx2otr.py`, dialog, riktiga tilesets

## Senare ⏳
- [ ] AP-läge (uppskjutet – `AP_MODE_ENABLED 0`)
- [ ] Admin-roll per spelare (flaggfältet i `PlayerRec` är reserverat)
- [ ] Ljud (ES8311), SD-kort, batterimätare
- [ ] Spara pågående spel vid strömavbrott
- [ ] 802.11k/v/r-roaming (möjligt nu med ESP-IDF)

## Möjliga framtida förbättringar
- Partitionering 6 + 6 MB app (OTA) + ~4 MB lagring (i dag 3 + 3 MB app + 9 MB FFat) – kräver USB-flash och flytt av spelarfilen
- Packad lagring med miniz i ROM (`tinfl`/`tdefl`): komprimera i PSRAM och spara packat på FFat (historik/loggar); görs som delad komponent så att den kan återanvändas i MyPi-projekten
