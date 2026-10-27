# MyEsp-OnTheRoad – Roadmap

Status per 2026-10-27. ✅ = klart (kompilerar + värdtester), 🔧 = HW-test återstår, ⏳ = planerat.

### Aktuellt fokus – tillbaka till bibeläventyret
- **Karaoke pausad på Kents begäran:** grunden fungerar mycket väl enligt Kent. Det innebär inte att alla funktioner i v0.13.7 är HW-verifierade; kvarvarande testpunkter står kvar. Inga nya karaokeändringar nu.
- **Nästa fokus: Josefs resa** enligt [bibeläventyrsplanen](docs/BIBLE_ADVENTURE_PLAN.md). v0.15.0 levererar kartövergångar och v2-sparning som teknisk grund; inget Josef-kapitel eller utbyggd uppdragslogg ingår. Uppdragslogg och generell världsspecifik HUD återstår.
- [x] **MineSweeper-utbyggnad:** 12 banor, hittbar skyddsdräkt och 3×3/4×4-bomber i v0.15.0; regler och begränsningar nedan. Ej HW-testad.
- [ ] **Eventspellistor kvar som TODO:** Kent bygger själv listor med låtar och ordning via webbsidan, med TXT/JSON som planerad lagring. Ingen automatisk filsortering; alla får välja låt. Favoriter/senaste är uppskjutna. Ingen implementation under karaokepausen.
- [x] **ASCII-authoringformat (.adv) för att importera/skapa egna äventyr:** format specificerat och dokumenterat i [docs/AUTHORING_ADVENTURES.md](docs/AUTHORING_ADVENTURES.md) med ett komplett, verkligt exempel ([docs/adventures/example_joseph_hebron.adv](docs/adventures/example_joseph_hebron.adv) = Hebron-lägret). Formatet speglar motorns riktiga strukturer (`MapDef`/`Rules`, `Script`/`Npc`/`Node`). **Host-parser byggd:** `tools/adv_parse.{h,cpp}` läser `.adv` → `Otr::MapDef` + `Otr::Story::Script`; host-testet `tests/test_adv_parser.cpp` parsar exempelfilen, laddar kartan med `World::load()` och kör `Script::validate()` rent (19 kontroller, inga kompilatorvarningar). Parsern är ett host-verktyg och ingår inte i firmware-bygget. **Nästa steg:** läsa `.adv` även på enheten (SD/flash) för direktimport där.

### v0.17.0 – SD-kortsäventyr: datadriven meny, spara per mapp, carry/reset 🔧 (ej HW-testad)
- **Äventyrsmenyn läser nu enbart `/sdcard/otr/adventure/`.** Varje undermapp med ordnade kartfiler `NN_namn.adv` blir ett äventyr; menyn byggs från varje mapps `@about`-header (titel/beskrivning SV/EN, fallback till mappnamnet). Reserverade mappar (`saves`, `maps`) och mappar utan kartfiler hoppas över. Saknas kortet eller mappen visas meddelandet **"Inga äventyr på SD-kortet"** med sökvägen (SV/EN). De inbyggda WorldId-äventyren listas inte längre; motorns enum/kartor finns kvar men är vilande.
- **Firmware-säker `.adv`-laddare på enheten:** `main/adv_loader.{h,cpp}` speglar host-parsern men utan `std::string`/`vector`/`ifstream` och utan dynamisk allokering – all backing-lagring ligger i en `Adv::Loaded` (~70 kB) som UI:t äger i PSRAM och återanvänder mellan kartor. `main/adv_catalog.{h,cpp}` skannar kortet (POSIX `dirent`) och bygger en fast `Adv::Catalog` (PSRAM, inga allokeringar).
- **Spara per äventyrsmapp:** `main/adv_sd_session.{h,cpp}` nycklar sparfilen på mappnamnet i stället för ett WorldId, så varje SD-äventyr har egen progress; `josef`-mappen bevarar därmed Josef-linjens sparning som förut. Sparformat: 56-byte-header (magic `ADVS`, version, kartindex, mappnamn, längder) + `World`-image + `StoryState`-image. Trasiga sparfiler och sparfiler från en annan mapp avvisas utan att ändra pågående tillstånd.
- **Carry/reset enligt `@rules` mellan kartor:** vid utgång till nästa karta behålls/nollställs resurser enligt den lämnade kartans `carry:`/`reset:` (granulära `World`-setters: energi, maxenergi, skydd, nycklar, båt, liv). Ädelstenar är alltid kartlokala. Story-föremål/flaggor/räknare/uppdrag bärs över via namn-remap när `items` ingår i carry. Josefs override bevarad: 2 kartor (`01_lager`, `02_vag`), `carry: energy, maxenergy, items, lives`, `reset: suit`.
- **Dialoger i stående läge** är orörda; liggande läge gäller endast karaoke. Minfältets "Ny spelare"-dialog är **inte** ändrad.
- **Levererat SD-paket (`sdcard_bundle/otr/adventure/`):** de fyra inbyggda äventyren exporterade till `.adv` (`byn`, `josef` med 2 kartor, `minfaltet`, `testvarlden`) plus ett komplett resursdemo (`resdemo`) med egna 32×32-bitmappar (`stone.png`, `waterjug.png`). Samma `.adv`-filer finns som kopieringsbara exempel i `docs/adventures/generated/`, och motorns **standarduppsättning på 61 st 32×32-PNG-sprites** är exporterad till `docs/adventures/sprites/` via `tools/sprite_export` + `tools/sprite_png.py` (troget motorns procedurella ritning, inte handritat).
- **Värdtester (g++):** `tests/test_adv_loader` 244 godkända, `tests/test_adv_catalog` 71 godkända (0 fel) – inklusive ett nytt test som parsar + laddar + går igenom alla kartor + sparar/återläser **varje** äventyr i paketet genom motorn. Befintliga sviter oförändrade.
- **Byggverifiering:** ESP-IDF 6.1-bygget `idf.py build` slutförs med exitkod 0, inga kompilatorvarningar. App-binär 0x1d4240 byte (~1,86 MB), 63 % av app-partitionen ledig. **Inget är HW-testat** – endast värdtester och kompilering är verifierade.
- [ ] HW-test: SD-skanning och meny på enheten, spara/återläs per mapp, carry/reset mellan kartor, resursbitmappar.

### v0.15.0.2 – Portrait, nya spelare och lagringsskydd (ej HW-testad)
- Rättar felaktigt landscape-antagande i v0.15.0.1 och dess test. Verklig skärm 320×480: resultatdialog 296 px bred, staplade 272×48-knappar, totalhöjd 171–250 px. Samma korta SV/EN-texter och oförändrade Minfält-callbacks/debounce. Även återuppta-dialogen använder portrait-layout.
- Synlig Ny spelare i äventyrsinställningarna: namnfält/tangentbord, Spara/Avbryt, befintlig Players-validering och ID-hantering. Först lyckad lagring, sedan valt ID, markerad spelare och uppdaterad lista utan skärmbyte. Fel håller formuläret öppet. Nya knappar och tangenter har dubbeltrycksskydd.
- Bevisade kodrisker: gamla Players ignorerade sparfel och publicerade ändrat RAM ändå; läsfel kunde lämna tom skrivbar databas; generiska MyFat-skrivaren tog bort originalet före rename och ignorerade fsync/fclose; uppstarten begärde formatering vid mountfel. Detta bevisar INTE orsaken till Kents sporadiska profilförlust.
- Skydd: Players använder nu egen kontrollerad temp/backup-commit, sparfel behåller RAM och original/backup. Läsfel blockerar skrivningar och behåller senaste goda RAM; vid kallstart kan backup visas skrivskyddad, utan automatisk omskrivning. Filformat v2 och v1-läsning oförändrade. Uppstartens FFat-mount begär aldrig formatering. **Helt tom/oläsbar FFat initieras inte automatiskt**; ingen erase/format/migration eller partitionsändring har utförts.
- OTA-koden skriver nästa app-partition, inte FFat; ingen direkt profilradering hittad där. B1 oförändrad. Profiler finns i `/ffat/players.bin`, backup `.bin.bak`; äldre `.bin.tmp`/`players.tmp` kan läsas konservativt. NVS-init kan radera NVS men profilerna ligger i separat FFat. Generiska MyFat-skrivaren och karaoke lämnas orörda.
- Riktade tester: 26 layout-/text-/tryckytekontroller i verklig LVGL 9.6 portrait (alla resultatvarianter + återuppta SV/EN, namnfält/tangentbord), verkliga spelar-UI-callbacks för tomt/dubblett/lagringsfel/lyckat/avbryt; 74 lagringskontroller inklusive omstart, kort skrivning, flush/fsync/close/stat/read/rename och misslyckad rollback; uppstartsskyddstest; språk 1 873, core 360 godkända. Fullsviten inte omkörd. Oberoende layoutkontroll återstår.
- Firmware/merge byggda i befintlig build-v015, exit 0. Inga kompilatorvarningar; 5 befintliga SDK-CMake-varningar och SDK-diagnostik om saknad Git-katalog kvarstår.
- Vid nästa spontana profilbortfall: öppna enhetens befintliga Diagnostik och välj **Ladda ner loggar (ZIP)** innan omstart/nya profiler. `/api/log/download` innehåller RAM-logg samt SD-loggar `/otr/logs/otr_0..2.log`. Taggarna `players`, `storage` och `ota` är relevanta; ny profilloggning innehåller status/antal/felkoder, inga namn/PIN. Ingen avsiktlig OTA-reproduktion eller gissad shellinstruktion behövs.

### v0.15.0.1 – Textpassning i Minfältets resultatdialog 🔧 (ej HW-testad)
- Endast denna dialog: **Nästa / Omstart / Lämna**, **Next / Restart / Leave**. Gemensamma omstartsetiketter på andra skärmar är oförändrade, liksom callbacks och dubbeltrycksskydd.
- Samma 18-punktsfont; dialogbredd 420 px och knappar 144×50, 120×50 respektive 100×50 px. Inga mindre tryckytor.
- Riktat test `python3 tests/test_mine_dialog.py`: verklig LVGL 9.6-layout, projektets font och utdragen dialogkod vid 480×320. Alla 14 etikett-/tryckytekontroller godkända (vinst, sista bana, förlust; SV/EN), utan överlapp eller klippning. Svenska textbredder 53/76/64 px ryms i 112/88/68 px. Språktest: 1 869 godkända kontroller. Fullsviten körs inte om för denna avgränsade fix.
- Firmwarebygg och merge i återanvänd `build-v015` godkända (exit 0). Version 0.15.0.1 och bildens kontrollsumma/hash verifierade. OTA 1 915 408 byte; merged 1 980 944 byte. Inga kompilatorvarningar; samma 5 SDK-CMake-varningar om Wi-Fi-komponenternas privata include-sökvägar samt SDK-diagnostik om saknad Git-katalog kvarstår.
- Karaoke, spelregler, sparning, live view och övriga skärmar lämnas orörda. B1-partitioner oförändrade; vanlig OTA.

### v0.15.0 – Kartövergångar, v2-sparning och MineSweeper 🔧 (ej HW-testad)
- [x] `Adventure::Session`: Byn ↔ Ängen via befintlig utgång; säker startpunkt bredvid portalen förhindrar omedelbart återhopp. Kartändringar bevaras vid återbesök. Nycklar, båt, skydd, energi/liv och berättelsestatus följer med; ädelstenar/checkpoint är kartlokala.
- [x] Samlad `OTS2`-sparfil med CRC: aktuell karta, båda kartornas tillstånd, berättelse och kampanjprogression. Gamla `OTRS` v1-filer kan läsas; Byns gamla `.sts` krävs vid migrering så att berättelsestatus inte tappas tyst. Trasiga/okända versioner avvisas utan delvis ändrat tillstånd. SD först, FFat som reserv; gäster sparas inte.
- [x] Minfältet: **12 fasta, deterministiska banor**, större yta och ökande mintäthet. Alla säkra rutor måste röjas för vinst; flaggor räcker inte. Förlust utan skydd → omstart på samma bana; nästa bana efter vinst. Gamla Minfältet-sparningar kan fortsättas som tidigare; omstart går till kampanjens bana 1.
- [x] Varje bana har en dräkt som absorberar **en minträff**, en 3×3-bomb och en 4×4-bomb på den synliga säkra startremsan. Nästa bana/omstart återställer banans förråd. Bomber väljs, förhandsvisas på kartan och används först med OK; Avbryt förbrukar inget.
- [x] 3×3 centreras på tryckt ruta; 4×4 har ankaret i ruta 2,2 (en ruta upp/vänster, två ned/höger). Ytan klipps vid kartkanten. Minor tas bort utan spelarskada, flaggor i området tas bort, minsiffror räknas om; ingen spridning utanför förhandsvisningen. Tom/redan röjd yta förbrukar inget.
- [x] Kampanj-HUD och nya UI-texter SV/EN. Skrivskyddad live view visar karta, bana, skydd, bomber och förhandsvisning utan att avslöja dolda minor. Nya åtgärdsknappar har dubbeltrycksskydd; zoom-toggle och befintlig diagonalinställning behålls. Tärningsflöden och karaoke/SF2/filhanterare/loggar är orörda.
- **Avgränsning:** teknisk tvåkartsdemo, inget Josef-manus/grafik, ingen ny uppdragslogg, ingen flerspelarstyrning. Banorna använder ett förutsägbart säkert rutnät (minor bara på jämna x/y) för garanterad framkomlighet; detta är inte slumpmässig klassisk Minesweeper. Svårighetskänsla och touch-layout återstår att prova på enheten.
- [x] Värdtester: 20 testprogram, **36 961 godkända kontroller, 0 testfel**. `test_adventure`: 984; `test_campaign`: 31 608 (samtliga 12 banor, skydd/förlust, bomber inklusive kanter/avbryt, karttur/retur, v1→v2, CRC/felversioner och live view utan minläckage). `test_advlive`: 83; `test_lang`: 1 865. JavaScript-syntax och `embed_web.py --check` godkända. Endast äventyrssidan ändras bland inbäddade webbsidor.
- **Byggverifiering:** färdigt ESP-IDF 6.1.0-bygge i `build-v015`, exitkod 0; appens inbäddade version 0.15.0 och kontrollsumma/hash verifierade med esptool. OTA 1 915 280 byte; merged 1 980 816 byte, med identisk app vid offset 0x10000. **0 kompilatorvarningar**, men fulla byggloggen innehåller **5 CMake-varningar** om privata include-sökvägar mellan SDK-komponenterna esp_wifi/wpa_supplicant. Därför inte helt varningsfritt. SDK:s Git-versionsuppslag gav också diagnostik om saknad Git-katalog; bygget slutfördes ändå och rapporterar IDF 6.1.0. Inga SDK- eller karaokeändringar för detta. Två testprogram behövde rättade kompileringskommandon (include-sökväg respektive effektmodul vid länkning); deras separata omkörningar är godkända och ingår i totalsumman ovan.
- [ ] HW-test: portaler, sparning på SD/FFat, pekning/joystick, bombförhandsvisning och synk med webben.
- **Partitioner:** oförändrad B1; vanlig OTA från v0.12+.

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

## v0.13.7 – Låtbibliotek med mappar, webbväljare, filhanterare och loggar 🔧 (ej HW-testad)
- [x] **Rekursiv låtskanning:** `/sdcard/otr/karaoke` och alla undermappar (ingen hårdkodad djupgräns; rekursionsskydd), `.mid/.kar/.midi/.rmi`, max **2 000** låtar (var 500). `SongFile.dir` = relativ mapp
- [x] **`songs.idx`** (binärt, `OTRK` v1): skanningstid, SD-fritt utrymme, rotmappens mtime, antal + per låt sökväg/titel/artist/storlek, FNV-1a-kontrollsumma. `load()` läser indexet; saknas det, är det trasigt eller skiljer sig fritt utrymme/mtime skannas kortet om och ett nytt index skrivs. `rescan()`/`rescanAsync()` tvingar omskanning. `openByPath()` öppnar och startar en låt från webben
- [x] **Enheten:** mappar före låtar, bakåtknapp i mappläge, sökfält (artist + titel i alla mappar), A–Ö-hoppkolumn längs högerkanten, sidbläddring, "Skanna om"-knapp. Låtar startade från webben följs av listan. Alla texter SV + EN
- [x] **Webb `/karaoke/songs`** (`songs_picker.html`): mappbläddring, sök, tryck på låt → spelas på enheten, skanna om, SV/EN, mobilanpassad, länk från/till `/karaoke`. API utan PIN: `GET /api/karaoke/songs?dir=&q=&offset=&limit=`, `POST /api/karaoke/open {"path"}`, `POST /api/karaoke/rescan`
- [x] **Filhanterare** (`/files`): kryssrutor, shift-klick för intervall, markera alla, åtgärdsrad (Ladda ner / Byt namn vid 1 vald / Flytta / Radera), sidväljare (25–200 per sida), Ny mapp (även `a/b`), dra-och-släpp-uppladdning, sortering på namn/storlek/datum (mappar alltid först). Nya API (admin-PIN via `X-Admin-Pin` eller `pin`): `GET /api/files?root=&dir=&page=&limit=50&sort=&order=`, `POST /api/files/mkdir`, `DELETE /api/files/bulk {"paths"}` (mappar raderas rekursivt), `POST /api/files/move {"paths","dest"}` (målmappen skapas). `/api/fs/*` finns kvar. **Ingen automatisk sortering** – filerna organiseras för hand
- [x] **Loggar** (`otr_log`): kedjad `esp_log_set_vprintf`-hook efter MyLog, 64 kB byte-ring (PSRAM), SD-rotation `/otr/logs/otr_{0,1,2}.log` à 256 kB (skrivs var 2:a sekund i bakgrunden). `GET /api/log/stream` (SSE, max 2 klienter, 8 kB historik vid anslutning, ping var 10:e s), `GET /api/log/download` (ZIP/store: `info.txt`, `ram.log`, `otr_2..0.log`). Diagnostiksidan har ett live-loggfönster med Error/Warn/Info/Debug-filter, fritextfilter, snabbknappar per modul och "Ladda ner loggar (ZIP)"
- [x] Hosttester: `test_karaindex` (index, sortering, sök, A–Ö, sökvägsskydd), `test_otrlog` (ring, radstädning, CRC-32, ZIP läses tillbaka), `test_filespage` (sortering, sidor, sökvägsrelationer); alla 19 sviter gröna (5 289), bygge 0 varningar
- **Avvikelser:** filhanterarens "Ladda ner" för flera filer startar en nedladdning per fil (ingen ZIP); mappar laddas inte ner. En katalog listas med högst 4 000 poster. SSE-klienterna körs som egna tasks på asynkrona httpd-förfrågningar
- [ ] HW-test: skanning av 2 000 låtar (tid, minne), A–Ö/sök på skärmen, SSE med musik igång, ZIP-nedladdning
- **Obs:** samma partitioner → vanlig OTA

## v0.13.6 – Nedräkningspunkter vid långa pauser 🔧 (ej HW-testad)
- [x] Paus ≥ 3 s (`COUNTDOWN_GAP_MS = 3000`) mellan sista stavelsen på en rad och nästa rads första stavelse – och **långa intron** före första raden (räknas från 0) – visar N punkter, N = avrundat antal sekunder, max 8 (`COUNTDOWN_MAX`). Konstanterna finns i `kara_stream.h` och speglas i `karaoke.h`
- [x] Punkterna visas direkt när pausen börjar, alla vita; en i taget (vänster → höger) tar den sjungna stavelsens färg, en per sekund, så att den sista tänds exakt när nästa rad börjar. Vid > 8 s paus är alla vita tills 7 s återstår
- [x] Stavelser har bara starttid: sista stavelsen antas hållas radens medelavstånd mellan stavelser, max 1 s (0,5 s för en rad med en stavelse); pausen börjar efter det
- [x] Tidtagningen följer syntens position (sampel), inte väggklockan: `KaraStream::countdownAt(song, posUs)` (ren funktion) används av både enheten och webbströmmen
- [x] Enheten (LVGL): rad med 13 px cirkelpunkter, 8 px mellanrum, centrerad under aktuell rad (y = 163), vit → guld (`Theme::GOLD`, samma som sjungen stavelse). `onCountdown(dots, elapsed)` anropas från karaokevyns tick (40 ms) via `Karaoke::View.cdDots/cdLit`
- [x] Webben: ny frame `{"type":"countdown","dots":N,"elapsed":k,"ts":ms}` från `LyricTracker` när pausen börjar och vid varje ny tänd punkt (i strömmen på exakt rätt sampel); `dots:0` döljer vid sökning ut ur en paus; ny rad/stavelse/clear döljer också. `karaoke.html` ritar punkterna med CSS-övergång vit → orange (samma som sjungen text). Ny klient mitt i en paus får nedräkningen direkt (resync)
- [x] Hosttester: 125 nya i `test_karastream` (5 s paus → 5 punkter, elapsed 0–4 med 22 050 sampels mellanrum inom ett mixerblock; 2,9 s → ingen frame; exakt 3 s; 20 s → 8 punkter; avrundning 4,6/4,4 s; intro; sökning; resync); alla sviter gröna (5 058), bygge 0 varningar
- **Avvikelse:** på enheten pollas nedräkningen i UI-tickens takt (40 ms) i stället för ett callback från mixertasken (LVGL får bara röras i UI-tasken); källan är ändå syntens position. Har aktuell rad tre rader text kan punkterna överlappa den tredje
- [ ] HW-test: placering och synlighet på skärmen, synk mot ljudet i webbläsaren
- **Obs:** samma partitioner → vanlig OTA

## v0.13.5 – Webbkaraoke: PCM-ström till webbläsaren, synkad text, automatisk tystning 🔧 (ej HW-testad)
- [x] WebSocket **`/ws/karaoke`** på den befintliga webbservern: binärframes med 22 050 Hz mono int16 PCM (512 sampel = 1 024 B, ~43 frames/s) av den mixade karaokeutgången; JSON-textframes för texten. Max 3 lyssnare (fler får `{"type":"full"}`)
- [x] `Audio::setTap()` ger varje mixat block (efter klippning) till `KaraStream::Stream`: låsfri SPSC-ring i PSRAM (16 384 sampel = 32 kB ≈ 0,74 s). Full ring eller ingen klient → blocket hoppas över, renderingen blockeras aldrig. Sändartasken `kara_web` (core 0, prio 4) skickar med `httpd_ws_send_data`
- [x] **Sampelexakt textsynk:** `KaraStream::LyricTracker` körs i mixertasken efter sekvenseraren och lägger radbyten/stavelser i strömmen vid det sampel där blocket börjar; sändaren skickar PCM fram till händelsen och sedan händelsen. Webbläsaren visar händelsen när ljudet före den har spelats (`setTimeout` mot schemalagd tid + utgångslatens)
- [x] Protokoll: `{"type":"hello"|"state","rate":22050,"frame":512,"open":b,"song":".."}`, `{"type":"newrow","row":N,"ts":ms,"syls":[..],"next":".."}`, `{"type":"lyric","text":"..","row":N,"k":K,"ts":ms}`, `{"type":"clear","ts":ms}`. Ny klient → aktuell rad och stavelse skickas om
- [x] **Automatisk tystning:** `Audio::setWebStream(bool)` – när minst en webbläsare är ansluten **och** en låt är öppen sätts ES8311:s DAC-volym till 0 och förstärkaren stängs av; synten fortsätter med normal nivå till strömmen (även om ljudet är avstängt i inställningarna). Sista klienten borta eller låten stängd → volymen återställs. Karaokevyn visar "Webbläsare: N (högtalaren tyst)" / "Browsers: N (speaker muted)" på diagnostikraden
- [x] Ny webbsida **`/karaoke`** (`main/web/karaoke.html`, länk från startsidan): Anslut/Koppla ned, AudioContext 22 050 Hz (skapas på knapptrycket, iOS-upplåsning; reserv: valfri frekvens + `createBuffer` med 22 050 Hz), Int16 → Float32, `AudioBufferSourceNode` schemalagda med 0,3 s förbuffring, föregående/aktuell/nästa rad med sjungen del markerad, SV/EN (väljare, annars `navigator.language`), återanslutning efter 2 s
- [x] Ny NVS-nyckel `kara_webstream` (standard 1, inget UI): 0 → endpointen registreras aldrig
- [x] Hosttester: `test_karastream` (ring, drop vid full/avstängd, händelser vid rätt sampel, resync, UTF-8-avkortning, JSON-escaping, LyricTracker); alla sviter gröna (4 933), bygge 0 varningar, appen 1 860 800 B (35 % av 5 MB)
- **Avvikelse:** endpointen registreras vid start (ESP-IDF:s httpd kan inte avregistrera WebSocket-rutter säkert med öppna sessioner); den strömmar bara medan en låt är öppen och svarar annars med `"open":false`
- [ ] HW-test: ljud i Chrome (Android) och Safari (iOS), avbrott vid svagt Wi-Fi, textsynk, att högtalaren tystnar/återställs, CPU-last i mixertasken
- **Obs:** samma partitioner → vanlig OTA

## v0.13.4 – Omsampling till 22 050 Hz, exakt tangentskanning, dynamisk budget, hackdiagnostik 🔧 (ej HW-testad)
- [x] `prepare()` omsamplar alla sampelintervall med `sample_rate` > utfrekvensen (28 000, 30 536, 32 000, 44 100, 48 000 Hz …) till utfrekvensen när de läses från SD: polyfas windowed-sinc, Kaiser-fönster (75 dB stoppband, brytfrekvens 0,45 × utfrekvensen), 64 faser med linjär interpolation, float-koefficienter (~10–25 kB, bara medan något omsamplas), 1 024 utsampel per pass, läscache med två fönster om 4 096 sampel. Ersätter 2:1-halveringen (halvbandsfiltret och `HALVE_MIN_ODD_LOOP` borta); sampel ≤ utfrekvensen lämnas orörda
- [x] Loopar: looplängden avrundas till ett heltal nya sampel och zonens `sample_rate` (nu float i tsf) kompenseras exakt (källfrekvens × ny längd / gammal längd), så tonhöjden blir rätt även för korta udda loopar. Överlappande intervall som delas av zoner med olika loopar/kompensation delas upp i block per källfrekvens så att varje zon får egna, korrekt mappade gränser
- [x] API: `Synth::setResampling()/resampling()` (på som standard), `PrepStats.resampled/resampledSaved/budget` (ersätter `setHalving`/`halved`)
- [x] **±12-marginalen borttagen**: skanningen registrerar exakt de tangenter låten spelar med aktuell transponering (trummor otransponerade). Vid byte av transponering med selektivt laddad bank körs `prepare()` om i bakgrunden (låten pausas och fortsätter); får den nya tonarten inte plats behålls föregående och enheten visar "Tonarten får inte plats i minnet – föregående tonart behålls" (SV/EN)
- [x] **Dynamisk budget**: största lediga PSRAM-block − 512 kB (`PSRAM_RESERVE`) i stället för fast 6 MB; utanför enheten 8 MB (`HOST_BUDGET`). Spelets ljudklippscache (SFX) frigörs innan en låt förbereds (`Audio::releaseClips()`). Budgeten loggas vid varje prepare
- [x] **Hackdiagnostik**: var 5:e sekund under karaoke loggas renderbelastning (medel/max i % av mixerblockets tid), underruns (totalt och nya) och högsta antal aktiva röster. Ny NVS-nyckel `kara_voices` (16–32, standard 32, inget UI) begränsar antalet röster, t.ex. 24 för test; UI:t visar aktuellt tak
- [x] Hosttester: omsampling 44,1/32/30,536/28/48 kHz × (2 000-sampels loop, udda 101-sampels loop, delat intervall med loopar 101/88) mot original, periodstabilitet via autokorrelation; transponering laddar om rätt tangenter; alla sviter gröna (4 873), bygge 0 varningar, appen 1 851 888 B (35 % av 5 MB)
- [x] Mätt på värddatorn (22 050 Hz, budget 8 MB): GeneralUser-GS (30,8 MB) + Ainbusk 6,03 MB (v0.13.3) → **3,10 MB** (367 zoner omsamplade, 1,42 MB sparat); + Alan Parsons Time 7,60 MB (TOO_BIG) → **4,64 MB** (2,03 MB sparat). Jnsgm2 + Time 6,91 MB (TOO_BIG) → **3,85 MB**; Jnsgm2 + Ainbusk **2,92 MB**. Ingen låt behövde tunnas. Rel. skillnad mot ej omsamplat 0,011–0,067 (första 60 s, torrt)
- [ ] Röststöld vid underrun (de tystaste rösterna) är inte gjord – först loggarna från enheten, sedan beslut
- [ ] HW-test: laddningstid med omsampling från SD, omladdning vid transponering, diagnostikloggar för Time (32 resp. 24 röster), PSRAM-marginal
- **Obs:** samma partitioner → vanlig OTA

## v0.13.3 – Halvering av högupplösta sampel vid laddning 🔧 (ej HW-testad)
- [x] `prepare()` decimerar sampel med ≥ 1,8 × utfrekvensen (44,1/48 kHz vid 22 050 Hz) 2:1 när de läses från SD: halvbands-FIR, 47 tappar, Kaiser β 7, Q15 (−0,9 dB vid 10 kHz, −6 dB vid 11 kHz, −53 dB vid 13 kHz för 44,1 kHz-källa), 4 096 utsampel per läsning, skrapbuffert ~16 kB bara när något halveras
- [x] Överlappande sampelintervall bildar block som halveras bara om alla zoner i blocket tillåter det; zonens offset, slut, loopgränser och `sample_rate` halveras
- [x] Loopar behåller tonhöjden: jämna loopar halveras alltid, udda bara om de är ≥ `HALVE_MIN_ODD_LOOP` (1 500 sampel, < 1,2 cent fel); kortare udda loopar och sampel < 39,7 kHz laddas i full upplösning
- [x] `Synth::setHalving()/halving()` (på som standard), `PrepStats.halved/halvedSaved`; karaoke loggar antal halverade zoner och sparade byte
- [x] Hosttester: halverade sinusar mot original (rel. skillnad ~0,01), 101-sampels udda loopar oförändrade, 2 000-sampels loopar halverade och tonstabila över 3 s; realtestet jämför halverat mot ohalverat; alla sviter gröna (4 673), bygge 0 varningar, appen 1 840 960 B (35 % av 5 MB)
- [x] Mätt (Kents 5 låtar): sparar 0–1,4 MB per låt för stora banker (eawpats Ainbusk 4,31 → 2,97 MB, Alan Parsons 4,70 → 3,30; Unison 3,3–5,0 → 1,9–4,2; Masterpiece Ainbusk får nu alla hastighetslager, 5,51 MB i st f ett lager); rel. skillnad mot ohalverat 0,001–0,21 (första 60 s, torrt). Florestan och Yamaha XG oförändrade (redan 22 050 Hz)
- [ ] **Löser inte** GeneralUser-GS + Ainbusk (minst 6,03 MB) och Alan Parsons (7,60 MB) eller Jnsgm2 + Alan Parsons (6,91 MB): de sampel låtarna använder ligger mest på 28–32 kHz, under halveringsgränsen. Möjligt nästa steg: bråktalsomsampling av allt > 22 050 Hz till utfrekvensen
- [ ] HW-test: laddningstid med halvering från SD, ljudkvalitet (diskant), PSRAM-marginal
- **Delvis HW-testad av Kent:** med GeneralUser GS 1.35 spelas Ainbusk och Time; Time hackar lite efter några minuter; synthljudet är "oerhört mycket bättre"
- **Obs:** samma partitioner → vanlig OTA

## v0.13.2 – Selektiv SF2-laddning, reverb och chorus 🔧 (ej HW-testad)
- [x] `Sf::Synth::open()` läser bara bankens struktur (hydra) och kommer ihåg var sampeldatan börjar; `prepare(låt)` skannar låten genom TinySoundFonts egen kanallogik (bank select, programbyten, trumkanal) och läser bara de sampelintervall låten kan spela (förinställningar, tangenter ±12, hastigheter) från SD till en PSRAM-buffert. Budget 6 MB per låt (mindre om PSRAM saknas). Banker på över 30 MB fungerar (idé från copych/ESP32_SF2_Sampler_Synthesizer, MIT)
- [x] Transponering ±12 laddar aldrig om (melodiska tangenter breddas ±12 vid skanningen; trummor exakt)
- [x] Över budget: 1) bara hastighetslagret som innehåller 100 behålls och svarar på alla hastigheter, 2) därefter en zon per tangent (stereopar/staplade lager tas bort, kvarvarande zon +6 dB för stereo, +3 dB för andra lager). Räcker inte det visas "Låten behöver för många sampel" och banken förblir öppen för nästa låt
- [x] Reverb (Freeverb, public domain; 8 kamfilter + 4 allpass skalade till 22 050 Hz) och chorus (15 ms ±4 ms, 0,7 Hz) som mono-sändeffekter; sändnivå per kanal med CC91/CC93 (GM-standard 40 resp. 0); blockvis rendering grupperar röster per kanalernas sändnivåer
- [x] NVS: `kara_rev`, `kara_revl`, `kara_cho`, `kara_chol` (på, 50 %); `Karaoke::effects()/setEffects()`, `loadPct()`, `fontInfo()` (filstorlek, laddade byte, förinställningar, zoner, reducerad). Inga reglage i UI:t ännu
- [x] Generationsräkning i karaoke (`s_songGen/s_prepGen/s_failGen`): fonttasken öppnar banken en gång och kör `prepare()` för varje ny låt; byte av låt under laddning hanteras
- [x] Hosttester: test_sfsynth (fel, probe utan storleksgräns, effekter stabila/inga klipp/svansen dör ut, selektiv == full bit för bit, budgetreduktion båda stegen, banken öppen efter TOO_BIG); alla sviter gröna (4 650), bygge 0 varningar, appen 1 839 648 B (35 % av 5 MB)
- [x] Mätt på värddatorn (22 050 Hz, Kents 5 låtar): Florestan 3,1 MB → 0,23–0,63 MB per låt; Super Nintendo 1,8 MB → 0,08–0,29 MB; Yamaha XG 3,7 MB → 0,23–0,76 MB; GeneralUser-GS 30,8 MB → 3,5–4,8 MB för 3 av 5 låtar, Ainbusk (6,6 MB) och Alan Parsons (8,3 MB) får inte plats. Selektiv rendering är bit-identisk med hel laddning. Effekterna kostar ungefär lika mycket som torr rendering (0,1 % → 0,2–0,3 % CPU på värden)
- [ ] HW-test: laddningstid med 30 MB-bank från SD, ljud (reverb/chorus-nivå, +3/+6 dB efter tunnning), CPU och underruns i mixern med båda effekterna, PSRAM-marginal
- **Obs:** samma partitioner → vanlig OTA

## v0.13.1 – Karaoke liggande, flera ljudbanker, skalbar live view 🔧 (ej HW-testad)
- [x] Karaoke visas liggande (480×320) via LVGL-mjukvarurotation (`MyLvgl::setRotation`, rotation i flush till en PSRAM-buffert); pekning mappas av LVGL; stående läge och 180°-inställningen återställs när man lämnar modulen (`Display::setLandscape`). MADCTL rörs inte (ST77922 QSPI)
- [x] `ui_karaoke` omgjord för 480 px: låtlista i två kolumner, spelare med hela rader (föregående/aktuell upp till 3 rader/nästa/nästnästa), två knapprader, ark i full bredd
- [x] Flera SF2 i `/otr/sf2`: global standard (NVS `kara_sf2`) väljs i låtlistan, ljudbank per låt väljs i spelaren och sparas som `sf2=<fil>` i `karaoke.reg` (bakåtkompatibelt, mellanslag som `%20`); omladdning i bakgrunden när låten vill ha en annan bank, bara en bank i PSRAM; aktiv bank visas
- [x] Validering av ljudbanker (`Sf::probe`): SF3/komprimerad, inte RIFF/sfbk och > 7 MB avvisas med tydligt meddelande; ogiltiga filer visas gråa med orsak i väljaren
- [x] Ljudet avstängt när man går in i karaoke → dialog "Ljudet är avstängt. Slå på ljudet?" Ja/Nej (Ja sparar som inställningssidan)
- [x] Äventyr live view (`/adventure`) skalar till fönstret: sida vid sida i liggande/brett fönster (≥ 800 px), info under kartan i stående; storleksväljare (Anpassa till fönster/Liten/Mellan/Stor) sparas i webbläsaren; ritas om vid storleks-/orienteringsbyte
- [x] Hosttester: test_midi 100 (nytt: registerformat med `sf2=`), test_sfsynth 17 (nytt: probe SF2 v2/v3); alla 15 sviter gröna; bygge 0 varningar, appen 1 829 744 B
- [ ] HW-test: rotationens prestanda och pekmappning (90°/270°), fontbyte per låt, ljuddialogen, live view på TV/mobil
- **Obs:** samma partitioner → vanlig OTA

## v0.13.0 – Karaoke: MIDI/KAR, SF2-synt, text på enheten 🔧 (ej HW-testad)
- [x] Ny komponent `otr_midi` (ren C++17): SMF format 0/1, running status, sysex/meta, tempokarta, .kar-text (`@`-taggar, `/` och `\\` som rad/vers) och vanliga lyrics-händelser, stavelser → rader, melodispårsdetektering (textträff, enstämmighet, spårnamn), sekvenserare med sökning/tempo/transponering, register `karaoke.reg`
- [x] Ny komponent `otr_synth`: vendrad TinySoundFont, patchad för 16-bitars sampel i PSRAM (halva minnet), float-filter (S3 saknar double-FPU), fixpunkts-monorendering, röststöld, max 32 röster, -14 dB headroom
- [x] `Audio::setExternal()` – extern musikkälla i mixertasken; vanlig musik pausas under karaoke och fortsätter efteråt; CPU-mätning
- [x] `karaoke.h/.cpp`: låtskanning, asynkron SF2-laddning, sekvenserare + synt i mixertasken bakom mutex, tyst läge utan ljudbank
- [x] `ui_karaoke`: låtlista (artist/titel ur filnamnet), spelare med föregående/aktuell/nästa rad och ord-för-ord-markering i guld, reglage för sökning, spela/paus, Tempo/ton-ark (tempo 50–150 %, tonart ±12, volym), Melodi på/av, Spår-väljare med statistik per kanal
- [x] Menyn har sex kort (Karaoke i guld längst ned); 27 nya strängar SV/EN
- [x] Hosttester: test_midi (102, mot Kents 5 filer) och test_sfsynth (14 per fil; alla 5 renderas, högst 32 röster); alla 15 sviter gröna; bygge 0 fel/0 varningar, appen 1 818 480 B (35 % av 5 MB)
- [ ] HW-test: SF2-laddning från SD (tid, PSRAM), ljudkvalitet/CPU på S3, textsynk, tempo/ton, melodispårsval sparas, återgång till spelmusik
- **Obs:** samma partitioner som v0.12.0 → vanlig OTA. Ljudbanken (.sf2) ingår inte; se README

## v0.14.0 – Mikrofon och poäng ⏳ (se docs/MUSIC_KARAOKE_PLAN.md)
- [ ] ES8311 ADC-ingång (mikrofon), I2S full duplex
- [ ] Tonhöjdsdetektering (YIN/autokorrelation) på mikrofonen, jämförelse mot melodispåret (oktavokänslig)
- [ ] Poäng per rad och låt, tonhöjdslinje på skärmen, topplista per låt
- [ ] Låtar och text i webben (`/karaoke`, live view)

## v0.12.0 – Partitionslayout B1 och storleksoptimering 🔧 (ej HW-testad)
- [x] Ny partitionstabell B1: app0/app1 5 MB vardera (OTA), FFat 5,88 MB på 0xA10000, coredump 64 kB (tidigare 2 × 3 MB app + 9 MB FFat)
- [x] Bygget optimeras för storlek (`-Os`, `CONFIG_COMPILER_OPTIMIZATION_SIZE`): appen 1 768 448 B (34 % av 5 MB), förut 1 934 432 B med `-Og`
- [x] Falsk trunkeringsvarning med `-Os` i `ui_wifi.cpp` åtgärdad (`%.32s`); bygge 0 fel/0 varningar
- [x] FFat formateras automatiskt vid första start (`format_if_mount_failed`)
- [ ] HW-test: USB-flash av merged.bin vid 0x0, första start formaterar FFat, ny spelare sparas, OTA till nästa version
- **Obs:** kräver engångsflash via USB; spelarfil och äventyrssparningar på FFat försvinner (Kent: OK)

## v0.11.1 – Live view för äventyret 🔧 (ej HW-testad)
- [x] Ny modul `main/adv_live.h/.cpp` (`AdvLive`): egen ögonblicksbild med eget lås och versionsnummer, skild från `game_live`-unionen. Karta (terräng, objekt, minsiffror, dimma) med kartrevision som bara skickas när den ändrats; spelare (position, riktning, båt), energi, liv, nycklar, ädelstenar, dräkt, föremål, personer och aktuell dialog med val (låsta med tips). Buffertar i PSRAM. Dolda minor läcker inte
- [x] Äventyret publicerar bara när något ändrats (signatur över position, statistik, revision, föremål, dialognod och språk) och markerar inaktivt när man lämnar
- [x] `/api/adventure?v=&mr=`: `{"same":true}` när inget ändrats, kartan bara när `mr` skiljer sig, `{"active":false}` när inget äventyr körs; `?brief=1` för en liten statusfråga
- [x] Ny sida `/adventure` (svenska): canvaskarta med enkla former per ruta och objekt, spelaren med riktningspil, personer med initial (guldring när de pratar), mjuk förflyttning, "Följ spelaren", HUD (energi, liv, nycklar, ädelstenar, föremål), dialogpanel med porträtt, namn i guld, hela texten, referens i cyan och numrerade val. Skrivskyddad, pollar var 220 ms, responsiv (två kolumner på bred skärm), helskärm
- [x] Länkar: knappen "Äventyr live" på översikten, "Äventyr" i menyn på `/game`, och `/game` visar en hänvisning när ett äventyr pågår
- [x] Hosttest test_advlive (83); alla 13 sviter gröna; bygge 0 fel/0 varningar
- [ ] HW-test: sidan på telefon och TV under spel i Byn och Minfältet, dialoger, kartbyte vid "Börja om"

## v0.11.0 – Dialoger: `otr_story`, personer, Prata, demokarta "Byn" ✅ 🔧 (HW-testad av Kent: Byn och dialogerna fungerar mycket bra)
- [x] Ny komponent `components/otr_story` (ren C++17, hosttestad): `Script` (personer, noder med varianter, val med villkor/effekter, föremål, uppdrag), `StoryState` (256 flaggor, 16 räknare, 32 föremål, 16 uppdrag, serialisering), `Dialog`-körare (start, val, låsta val med tips, dolda val, slutnoder), `validate()` för manus. `StoryHost` för världseffekter (flytta/ta bort person, öppna, nycklar) och ljud
- [x] Motorn: `Obj::NPC` med person-id och riktning, blockerar rutan (A* går runt), `EV_TALK` + `talkNpc()`
- [x] Prata-knapp: Slå byts till **Prata** när hjälten står vänd mot en person. Tryck på en person på kartan → hjälten går till närmaste lediga granncell, vänder sig och börjar prata
- [x] Dialog-UI: ruta över kartans nedre del med porträtt (48×48, 2× pixelkonst), namn, skrivmaskinstext med "blip" och referensrad; upp till 4 svarsknappar i panelen (låsta val grå med tips), 500 ms dubbeltryckskydd. Tryck på rutan hoppar över skrivmaskinen
- [x] Personsprites (3 utseenden: bagare, herde, gubbe) som vänder sig mot hjälten
- [x] Demokarta "Byn" med Bullbert (bagaren), Fårhild (herden) och Gamle Brunolf (gubben vid brunnen): flaggor, räknare, föremål (bulle), uppdrag (frukost), gåta med 4 svar, låsta/dolda val, berättarrad, världseffekter. Ingen bibeltext
- [x] Berättelsestatus sparas som `.sts` bredvid världens `.sav` (SD först, annars FFat)
- [x] Ljud `talk` (blip) och `quest`; 12 nya strängar SV/EN
- [x] Hosttester: test_story 182 (nytt), alla övriga oförändrat gröna; bygge 0 fel/0 varningar
- [x] HW-test (Kent): Byn och dialogerna fungerar mycket bra

## v0.10.0 – Kents HW-feedback på v0.9.0 🔧 (HW-testad av Kent: joystick, hastighet, diagonaler, brobugg, webb-SD-status)
- [x] Äventyret: toppstatusbaren borttagen, spelvyn börjar på y=0. Ny kontrollpanel (160 px): vänster status (liv, nycklar, båt) + menyknapp (hamburgare) och zoomknapp (-/+), mitten stor centrerad joystick (radie 74, stoppzon 23), höger Slå-ruta, röd energistapel, röd diamant + ädelstenar "11/13", "Dräkt N"
- [x] Joystick: halverad gånghastighet, std 300/150 ms (sparade v0.9.0-standardvärden 150/75 uppgraderas), intervall 60-600 ms; gånganimationen sträcks till takten (max 260 ms) så hjälten glider jämnt
- [x] Diagonalzoner: rakt 0±30°, diagonal 45±15° (tan 30° = 577/1000), gränsfallstester
- [x] Bro-bugg: A* i Pro-läge såg lava och mark som lika dyra, så tryck-för-att-gå kunde skära genom lavan bredvid basaltbron. Nu vinner vägen med minst lava bland lika korta vägar; värdtest går fram och tillbaka över bron (tryck och joystick)
- [x] Webböversikten visar SD-kort (monterat, 4-bit/1-bit, MHz, ledigt/totalt), FFat och startkontrollens resultat (samma som startbilden)
- [ ] HW-test: ny layout, gånghastighet/-animation, diagonalkänsla, bron, webböversikt


## v0.10.x (ej taggad) – dubbeltryck + HUD-symboler 🔧 (inte HW-testad)
- [x] Dubbeltryck, grundorsak (trolig): touchkontrollern tappar fingret 1-2 avläsningar → LVGL såg släpp+tryck → knappar löste två gånger. Släpp-debounce i touchdrivern (fingret måste vara uppe 60 ms); räknare "Filtrerade släpp-glapp" på diagnostiksidan
- [x] Extra skydd 500 ms på alla toggles: inställningarnas switchar (LED, ljud, musik, maxi-sparade kast), äventyrets ljud, zoom, Pro/Assist och hamburgermenyn
- [x] Äventyrs-HUD: vänster symbol + antal (liv, dräkt, nycklar, ädelstenar), höger ikoner för föremål man bär (båt; plats för 2 rader), 20 px ikoner nedskalade från världens sprites
- [x] Snabb ljud av/på-knapp bredvid hamburgaren; zoom flyttad in i hamburgermenyn (överst: Ljud, Zoom)
- [x] Dokumentation: README uppdaterad, [docs/COMPONENTS.md](docs/COMPONENTS.md) (återanvändbara komponenter + plan för utbrytning), [docs/BIBLE_ADVENTURE_PLAN.md](docs/BIBLE_ADVENTURE_PLAN.md)

## v0.9.0 – Ljud, microSD, filbläddrare, joystick 🔧 (HW-testad: ljud, SD, filbläddrare, sparning, joystick OK)
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

## Planerat – bibliskt äventyr (se docs/BIBLE_ADVENTURE_PLAN.md)
- [x] v0.11.0 – `otr_story` (dialogträd, villkor, effekter, flaggor; hosttestad), personer (`Obj::NPC`), Prata-knapp, dialog-UI med 4 svar, demokarta "Byn" (HW-testad av Kent)
- [x] v0.11.1 – Live view för äventyret på webben (`/adventure`, ej HW-testad)
- [x] v0.15.0 – tvåkartsdemo och sparformat v2 (berättelsestatus + aktuell karta)
- [ ] Senare – uppdragslogg och generell konfiguration av HUD-räknare/föremål per värld
- [ ] Senare milstolpe (version ej bestämd) – `tools/story2c.py` (manus → C++ + validering), nya rutor/personer/porträtt, musik, kapitel 1 (Hebron → Dotan) som vertikal skiva
- [ ] v0.16+ – kapitel 2–6, manus från SD, engelska

## Planerat – utbrytning av komponenter (se docs/COMPONENTS.md)
- [ ] `my_audio` (synt/mixer + ES8311), `my_sd` + `my_fsweb` (SD + filbläddrare), `my_touch`-filter, numpad till `my_ui`, `my_i18n`
## Senare ⏳
- [ ] AP-läge (uppskjutet – `AP_MODE_ENABLED 0`)
- [ ] Admin-roll per spelare (flaggfältet i `PlayerRec` är reserverat)
- [ ] Ljud (ES8311), SD-kort, batterimätare
- [ ] Spara pågående spel vid strömavbrott
- [ ] 802.11k/v/r-roaming (möjligt nu med ESP-IDF)

## Möjliga framtida förbättringar
- MIDI-spelare och karaoke (.mid/.kar från SD) – se svaret i chatten; designfrågor öppna innan kodning
- Packad lagring med miniz i ROM (`tinfl`/`tdefl`): komprimera i PSRAM och spara packat på FFat (historik/loggar); görs som delad komponent så att den kan återanvändas i MyPi-projekten
