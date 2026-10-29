# Flera kort: ESP32-S3 + ESP32-P4, arbetsplan och återstart av session

## Börja här i en ny session

Repository: `Bokbacken/MyEsp-OnTheRoad`. Läs detta dokument och
`docs/Waveshare_ESP32-P4-WIFI6-Touch-LCD-4.3_Hardware_Reference.md`.
Kontrollera aktuell `main`, öppna PR:er och deras faktiska innehåll innan du
fortsätter; en ocheckad punkt är inte klar.

Detta är den beständiga checklistan för stöd för flera kort. Uppdatera den i
samma PR som arbetet: vad som är implementerat, CI-byggt och hårdvarutestat.
Arbete ska ligga på en pushad branch med PR, aldrig bara i agentens
arbetskopia. Ändra inte skyddad `main` direkt.

Användaren arbetar via GitHub, flashar artefakter från GitHub Actions och
provar på båda korten. Varje firmwareleverans får en unik version.

### Mål

`MyEsp-OnTheRoad` ska byggas och fungera på flera kort från samma källkod:

| Profil | Kort | Target | Skärm | Radio |
|---|---|---|---|---|
| `ES3C35P` | Heemol/LCDWIKI ES3C35P 3,5" | `esp32s3` | ST77922 QSPI 320×480 | inbyggd Wi-Fi |
| `P4_43` | Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 | `esp32p4` | ST7701 MIPI-DSI 480×800 | ESP32-C6 via ESP-Hosted (SDIO) |

Fler kort ska kunna läggas till genom en ny profil, egna `board_*`-filer och
en sdkconfig-overlay, utan ändringar i spel/UI-logiken.

### Verifierade fakta om P4-kortet (2026-10-29)

- esptool: ESP32-P4 **revision v3.2**, 40 MHz kristall, flash-ID `0x1940c8`
  (GigaDevice, 32 MB). USB-UART-brygga CH343 (VID `0x1a86`, PID `0x55d3`).
- Bygg därför med `CONFIG_ESP32P4_REV_MIN_300` (v3.0–v3.99). Ett v1.x-bygge
  behövs inte för detta kort.
- Ägaren har startat v0.22.0 på P4 och anslutit till Wi-Fi. RTC fungerar
  inte ännu och batteri-ADC saknas. Övriga hårdvarukontroller nedan är
  inte automatiskt godkända av detta första test.
- Ägaren har även bekräftat app-OTA med v0.22.1. SD-loggen visar att kortet
  först monteras (4-bit, 20 MHz) och att kataloger skapas, men senare
  förlorar kommunikationen (`ESP_ERR_TIMEOUT`); det är inte bara ett
  visningsfel eller ett oläsbart filsystem.
- Schemat visar ingen separat I²C-RTC. `RTC BAT` är kopplad till P4:s
  `ESP_VBAT`. I²C-skanningen i bootloggen visar bara `0x18`, `0x40`, `0x5D`.
- SD-kortets VDD styrs av Q1/AO3401 via **GPIO45, aktiv låg**.
  LDO4 försörjer SD-IO/pullups, inte själva kortets VDD.
- Batteriets GPIO20 är **ADC1 kanal 4**, med R12/R15 = 200 kΩ/100 kΩ.
- v0.22.2 på fysisk P4: ägaren bekräftar rätt batterivärden, Karaoke med
  musik och fungerande äventyr från SD. Webbdiagnostiken visar monterat
  SD (4-bit, 10 MHz), ES8311 igång och intern RTC `OK` efter NTP.
- Intern FFat är fortfarande omonterad. Webbens gamla reparationsväg
  krävde felaktigt en redan monterad FFat och hjälpte därför inte.
  v0.22.3 tar bort det kravet och kan formatera/montera den tidigare
  valda partitionen efter uttrycklig adminbekräftelse.

### Beslut

- Senaste releaser föredras (ESP-IDF 6.1, LVGL 9.6). Waveshare-BSP:n låser
  LVGL 9.5 och egen LVGL-adapter; vi använder den bara som referens för stift,
  panelsekvens och timing. Egna drivrutiner bakom samma `board_*`-API.
- Ett `HwProfile` per kort väljs i Kconfig och är bundet till target.
- Gemensam `sdkconfig.defaults` + `sdkconfig.defaults.<target>`; inget
  `CONFIG_IDF_TARGET` i den gemensamma filen. Bygg med separata katalogen
  `build_esp32s3` / `build_esp32p4`.
- Firmwareartefakter namnges per kort. En OTA-fil för fel kort ska avvisas.
- Wi-Fi-API:t i `my_wifi` behålls; på P4 levereras det av `esp_hosted` 3.x
  mot C6 (IDF ≥ 6.1 kopplar `esp_wifi` direkt till esp_hosted, så
  `esp_wifi_remote` behövs inte).
- Bara P4 rev v3.x stöds (`CONFIG_ESP32P4_REV_MIN_300`, CPU 400 MHz).
- Första installation på ett kort: `build/merged.bin` från CI-artefakten
  skrivs på adress `0x0` med esptool/esptool.js. Därefter OTA-filen
  (`MyEsp_OnTheRoad.bin`).
- Komponentlås per target: `dependencies.lock.esp32s3` och
  `dependencies.lock.esp32p4` (rot-`CMakeLists.txt`).

## I. Byggsystem för två targets

- [x] Plan och återstartsdokument (detta dokument).
- [x] Kconfig: profil bunden till target (`ES3C35P` ↔ S3, `P4_43` ↔ P4).
- [x] `sdkconfig.defaults` uppdelad i gemensam + `.esp32s3` + `.esp32p4`.
- [x] Stift per profil (`board_pins_es3c35p.h`, `board_pins_p4_43.h`).
- [x] Källfiler per profil i `main/CMakeLists.txt`; P4-beroenden bara för P4.
- [x] CI-matris: S3 och P4 byggs i varje PR, artefakter
  `firmware-esp32s3-…` och `firmware-esp32p4-…`.
- [x] README: byggkommandon för båda korten.

## II. Första körbara P4-bygget: grafik

- [x] MIPI-DSI-buss, LDO för DSI-PHY, ST7701-initiering, DPI-panel 480×800
  RGB565 (timing från Waveshare-BSP 1.0.1).
- [x] `my_lvgl` flush mot DPI-ramminnet, rotation 180° och karaoke-landskap
  via LVGL-mjukvarurotation.
- [x] Bakgrundsbelysning GPIO26, LEDC PWM, inverterad.
- [x] GT911-touch på I²C GPIO7/8, adress `0x5D`/`0x14`, polling utan INT.
- [x] I²C-skanning på delad buss.
- [x] Ingen RGB-lysdiod: `Rgb` blir no-op utan att påverka appen.
- [x] Startskärmen visar rätt kortnamn/upplösning.
- [x] CI-bygge för P4 grönt.
- [ ] **Hårdvara:** färgtest, startskärm och huvudmeny syns; touch träffar rätt.

## III. P4-kringutrustning

- [x] microSD: SDMMC slot 0 (IO MUX), on-chip LDO kanal 4 för IO-matning;
  avmontering får inte stänga SDMMC-värden som C6-länken använder.
- [ ] Ljud: ES8311 med I²S MCLK13/BCLK12/WS10/DOUT9/DIN11, förstärkare
  GPIO53 (aktiv hög).
- [x] Batteri: ADC-drivrutin väljer kanal från kortets GPIO (S3 GPIO8,
  P4 GPIO20); P4 har förvald skala 3000 permille. Sparad egen kalibrering
  behålls; noll/reset återgår till kortets standard. Hårdvarutest återstår.
- [x] RTC: intern P4-timer bakom `MyRtc`, separat från S3:s DS3231.
  NTP/manuell tid skapar en betrodd UTC-ankring i RTC-minne. Diagnostik
  visar rätt källa och ingen påhittad temperatur. Hårdvarutest återstår.
- [ ] RTC/VBAT: kontrollerad deep sleep och verifierad backup vid förlust
  av huvudmatning. P4:s VBAT kräver rätt viloläges-/strömsekvens; en
  plötslig avstängning får inte utlovas fungera som en fristående DS3231.
- [x] SD: explicit styrning av kortmatning GPIO45, strömcykel vid
  återmontering/fel, 10 MHz vid återhämtning och 1-bit-fallback.
  Felkod exponeras i webbdiagnostik. Slot 1/C6 lämnas igång.
- [ ] **Hårdvara:** verifiera att SD håller sig läs-/skrivbart samtidigt
  som Wi-Fi används, och att återmontering fungerar efter timeout.
- [ ] **Hårdvara:** SD läses/skrivs, ljud hörs, batteriprocent rimlig.
- [x] **Hårdvara v0.22.2:** SD-läsning för Adventure/Karaoke, ljud och
  batterimätning bekräftade av ägaren. Längre SD-stabilitetstest återstår.
- [x] **Hårdvara v0.22.2:** intern RTC sätts av NTP och rapporterar `OK`.
  Betrodd tid efter mjukvaruomstart och VBAT-retention återstår att prova.

## IV. Wi-Fi via ESP32-C6 (ESP-Hosted)

- [x] `esp_hosted` 3.x (SDIO, C6 som slav) initieras på P4;
  Wi-Fi-anslutning bekräftad av ägaren med v0.22.0.
- [ ] `my_wifi`, mDNS, webbserver, NTP och webb-OTA fungerar oförändrat.
- [ ] Visa C6-firmwareversion och värd/slav-kompatibilitet i diagnostik.
- [ ] **Hårdvara:** anslutning, webbsida, klocka via NTP.

## V. OTA för C6-radiochippet

- [ ] Kontrollera C6-slavens version vid start; varna vid inkompatibel version.
- [ ] Uppdatera C6 från P4 via ESP-Hosted OTA-API: fil från SD-kort och
  uppladdning i webbgränssnittet, med förlopp och verifiering.
- [ ] Bygg/hämta matchande C6-slavfirmware i CI som artefakt.
- [ ] Dokumentera återställning via C6:s 4-poliga flashkontakt.
- [ ] **Hårdvara:** C6-uppdatering och återanslutning efter omstart.

## VI. UI för 480×800 och fler upplösningar

- [x] **Hårdvara v0.22.4:** ägaren bekräftar ren skärmväxling följd av
  OTA-progress, utan tidigare startflimmer. De flesta spel samt Karaoke
  har provats och fungerar; detta ersätter inte full layoutverifiering.
- [ ] Nästa layoutsteg: startskärm och Wi-Fi-anslutningsvy ska utnyttja
  P4:s större skärm bättre utan att försämra S3.
- [ ] Utred befintliga fontresurser och gemensamma textroller. Lägg till
  sparad användarinställning för textstorlek på båda korten; alternativ
  och omfattning ska bestämmas innan implementation. Större text måste
  få plats genom layout/scrollning, inte klippas eller täcka kontroller.
- [ ] Ta bort fasta 320×480-koordinater (inställningar, Wi-Fi, Yatzy,
  Adventure-setup) och använd `ui_metrics`/aktuell skärmstorlek.
- [ ] Typsnitt och träffytor skalas för större skärm.
- [ ] Layouttester för 320×480 och 480×800 för alla huvudskärmar.
- [ ] **Hårdvara:** alla appar användbara på båda korten.

## VII. Release, app-OTA och säkerhet mot fel kort

- [ ] Kort-ID i firmware; web-OTA avvisar bild för annat kort.
- [ ] `tools/firmware_release.py` och release-workflow per kort.
- [ ] Partitionstabell för 32 MB på P4 (större app/FAT, ev. C6-bild).

## VIII. Prestanda och P4-möjligheter

- [x] Strömsparning på båda korten: första steg är släckt bakgrundsljus
  med touch kvar för väckning, inte deep sleep eller avstängning.
  Första väcktrycket ska inte samtidigt aktivera en spelkontroll.
- [x] Ägarens val: automatisk och manuell släckning, även i spel/Karaoke
  med fortsatt spel/ljud. Väcktrycket får bara väcka; ett nytt tryck
  kräver släppt finger och minst 250 ms efter väckning. Implementerat i
  v0.22.5 med gemensamt filter före LVGL:s pointerhändelser.
- [x] Sparad tidsgräns i enhetens inställningar och webb/API:
  Av (standard), 30 s, 1/2/5/10/30/60 min. Manuell släckning finns i
  Inställningar från huvudmenyn. Vald ljusstyrka bevaras även om den
  ändras via webben medan skärmen är släckt. OTA väcker och håller tänt.
- [ ] **Hårdvara v0.22.5:** verifiera auto/manuell släckning och väckning
  i spel, Karaoke och menyer på båda korten. Första trycket/ett hållet
  väcktryck får inte spela, navigera eller flytta något; prova även ett
  snabbt andra tryck inom 250 ms. Starta OTA med skärmen släckt.
- [ ] Utred P4:s fysiska powerknapp mot schema innan den får en
  mjukvarufunktion; anta inte att den är en vanlig GPIO-knapp. S3 ska
  kunna använda funktionen utan fysisk powerknapp.
- [ ] Mät faktisk förbrukning och batteritid med skärmen släckt på båda
  korten. Touch-väckning innebär fortsatt aktiv elektronik; lova inte
  deep-sleep-förbrukning eller beräkna drifttid linjärt från batteriprocent.
- [ ] Rivningsfri uppdatering (dubbla ramminnen/vsync) och PPA/DMA2D.
- [ ] Mätning av FPS och minne på båda korten.

## IX. Fler kort i framtiden

- [ ] Guide: lägga till ett kort (profil, stift, drivrutiner, overlay, CI).

## Leveranslogg

| Datum | Steg | Status |
|---|---|---|
| 2026-10-29 | Plan | Skapad. |
| 2026-10-29 | I + II (PR #23) | Kod klar, CI grön för S3 och P4 (run 40884855527). Väntar på hårdvarutest av `merged.bin`. |
| 2026-10-29 | Första P4-test | Ägaren bekräftar uppstart och Wi-Fi med v0.22.0 (CI run 40885750801). RTC och batterimätning återstår. |
| 2026-10-29 | Integration med main | PR #19:s multimap, portaler och UI-fixar integrerade i P4-branchen. Versionskonflikt löst till v0.22.1. S3 och P4 gröna för mergecommit `07b5853` (CI run 40887806090); hostkontroller för multimap, Adventure och Karaoke godkända. |
| 2026-10-29 | Kringutrustning v0.22.2 | Intern RTC, P4 batteri-ADC, GPIO45 för SD-kortmatning och återhämtning implementerade i `2ac004b`. Båda targets och hosttester gröna (CI run 40893098350). OTA bekräftad av ägaren på föregående bygge; nytt hårdvarutest krävs. |
| 2026-10-29 | Diagnostik och FFat v0.22.3 | Ägaren bekräftar SD, ljud, ADC och NTP→RTC med v0.22.2. Korrigerar RTC-vänteläge vid start, P4-identitet/upplösning, GT911 firmwarebredd samt uttrycklig reparation av omonterad intern FFat. |
| 2026-10-29 | v0.22.3 byggverifiering | `9a0ea0d`, CI run 40901183845 grön för S3/P4 inklusive MAC-fixen. Ersätts vid leverans av v0.22.4 med OTA-skärmfixen nedan. |
| 2026-10-29 | OTA-skärm v0.22.4 | `e31311a`, CI run 40902453740 grön för S3/P4. P4-appfil nedladdad och SHA-256 verifierad: `a9b8b4c56d372dbaa96c37f4c3f07badc4848cc0d541dc5f52bb180a41e64f34`. Fysisk flimmerkontroll återstår. PR #23 är fortsatt draft, inte mergad; GitHub rapporterar konflikt med main som behöver hanteras inför merge. |

### OTA-start och P4-flimmer (v0.22.4)

- OTA läser uppladdningen och skriver flash i samma ström, inte först en
  komplett nedladdning till RAM. Tidigare raderade `esp_ota_begin` hela
  målområdet innan första skrivningen, vilket gav flera sekunders väntan.
  Nu används IDF:s `OTA_WITH_SEQUENTIAL_WRITES` för stegvis radering;
  storlekskontroll och verifiering före byte av startpartition behålls.
- P4:s DPI-DMA startas om av avbrott varje bild. Aktiverar
  `CONFIG_LCD_DSI_ISR_CACHE_SAFE` (väljer även cache-säker DW-GDMA) så
  bildöverföringen kan fortsätta när flashoperationer stänger av cache.
  S3-konfigurationen är oförändrad. Flimmerfrihet måste bekräftas på kortet.
- Förändringen körs av installerad firmware: uppladdningen **till**
  v0.22.4 kan fortfarande flimra; testa igen **från** v0.22.4.
- IDF verifierar chip-ID i `esp_ota_end` innan startpartition väljs, så
  S3-app på P4 och omvänt avvisas. Kortprofiler med samma chip skiljs
  inte åt av detta skydd.

### FFat-reparation och startdiagnostik (v0.22.3)

- OTA-appfil som vanligt, sedan **Filer → Intern flash → Reparera intern
  flash**. Kräver admin-PIN om konfigurerad och uttrycklig bekräftelse.
  Detta raderar intern FFat; läsbara profiler/loggar säkerhetskopieras
  först till SD när möjligt. SD, Wi-Fi-inställningar i NVS och firmware
  formateras inte. Ingen automatisk formatering vid uppstart införs.
- Efter lyckad reparation ska aktuell FFat-status visa monterad direkt.
  Startkontrollen är en historisk ögonblicksbild; starta om för ett nytt
  starttest. Misslyckad formatering eller efterföljande mount rapporteras
  som fel, inte som en lyckad reparation.
- Kallstartad intern RTC med fungerande timer men utan betrodd UTC visas
  som väntande, inte trasig. Aktuell RTC-status efter NTP finns under
  Diagnostik; startresultatet skrivs inte om i efterhand.
- Display/touch/PSRAM-namn är inte längre S3-hårdkodade. P4 visar
  480×800, ST7701 MIPI-DSI och GT911 på faktisk I²C-adress. GT911:s
  firmware visas som 16-bitars hex, inte trunkerad till 8 bitar eller
  förväxlad med produkt-ID. Saknade `sketch_*`-fält visas inte som NaN;
  appslotarnas verkliga partitionstorlek visas fortsatt.
- Återkommande `system_api: 0 mac type is incorrect (not found)` kom
  från systemdiagnostikens lokala `WIFI_STA`-MAC-läsning på P4 utan radio.
  Radiofria targets använder nu bas-MAC; Wi-Fi-providern rapporterar
  fortfarande C6:s nätverks-MAC. Ingen sänkning av loggnivå döljer felet.

### Nästa hårdvarutest (v0.22.4)

1. Hämta `firmware-esp32p4-v0.22.4-unverified` från
   [CI run 40902453740](https://github.com/Bokbacken/MyEsp-OnTheRoad/actions/runs/40902453740).
   Installera `build/MyEsp_OnTheRoad.bin` via den fungerande OTA-vägen;
   inte `merged.bin`. Gör därefter en till OTA från v0.22.4 för att prova
   att uppdateringsstapeln visas snabbt och skärmen inte flimrar vid start.
2. Låt NTP synka. RTC-diagnostik ska visa `ESP32-P4 RTC`, `OK` och ingen
   temperatur. Starta om via mjukvara/OTA och kontrollera att RTC kan
   återställa tiden. Kallstart utan nät får fortsatt visa osäker tid.
3. Med anslutet batteri: kontrollera ADC-mV, beräknad batterispänning och
   procent. Jämför mot multimeter och finjustera kalibrering vid behov.
   3:1 är schemats nominella värde, inte en garanti om mätprecision.
4. Läs/skriv filer på SD via webben medan Wi-Fi är aktivt, vänta minst
   fem minuter och prova även återmonteringsknappen. Vid fel: exportera
   logg och SD-diagnostik (`error`, `errors`, busbredd/frekvens).
5. Prova **Filer → Intern flash → Reparera intern flash** enligt
   beskrivningen ovan, efter att eventuell befintlig data har
   säkerhetskopierats. Kontrollera monterad FFat direkt och efter omstart.
   Låt Översikt/Diagnostik stå öppna och kontrollera att MAC-felet inte
   längre fyller loggen.
