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
- P4-logg 2026-10-30: SD monteras först i 4-bit/20 MHz. Efter senare
  filhanteringsproblem loggas `host controller with slot registered`,
  `conflict found for GPIO[42]` och därefter lyckad montering i
  4-bit/10 MHz. Koden väljer 10 MHz när återhämtningsläget är aktivt:
  `remount()` aktiverar det uttryckligen, och status-/monteringsfel kan också
  aktivera det. Om 4-bit/10 MHz misslyckas görs strömcykel och försök i
  1-bit/10 MHz. Frekvensskillnaden är alltså avsiktlig återhämtningspolicy,
  inte slumpmässig klockvariation. Från v0.23.10 sker återhämtningen
  i stället på 40 MHz, eftersom 10 MHz också växlade den gemensamma
  SDMMC-klockdelaren (se FILE_MANAGEMENT).
- Schemat bekräftar separata P4-I/O-signaler för microSD
  (GPIO39–44; D3 är GPIO42) och C6-värdradions SDIO. De ska därför inte
  kollidera på samma P4-I/O. GPIO42-konfliktvarningen vid återmontering
  kvarstår att förklara och bevisar inte i sig att SD och C6 delar signal.
- Samma loggutdrag visar Wi-Fi-roaming mellan två accesspunkter med cirka
  4–6 sekunders frånkoppling. `recv : 113` kommer vid nedkopplingen, och en
  ofullständig `.mid.part`-fil tas senare bort. Detta kan avbryta HTTP-
  uppladdningen; loggarna fastställer inte att roaming orsakar SD-
  återmonteringen eller filsystemproblemet.
- Filnamnstest 2026-10-30: samma två MIDI-filer misslyckas med långa namn
  på både S3 och P4, även efter byte av SD-kort. Namnen är 87 respektive
  81 ASCII-tecken:
  `Enemy - Imagine Dragons (Arcane _ League of Legends) - Octet - Wastgeim Slaebyrtwyn.mid`
  och
  `Alan-Parsons-Project-You-Don-t-Believe-Anonymous-20211124164554-nonstop2k.com.mid`.
  Ägaren bekräftar att samma innehåll fungerar med de kortare namnen
  `Enemy - Imagine Dragons.mid` och
  `Alan-Parsons-Project-You-Dont-Believe-Anonymous.mid`.
  Gemensam `sdkconfig.defaults` anger `CONFIG_FATFS_MAX_LFN=64`, medan
  `FsPath::validName()` accepterar upp till 255 byte och uppladdningen
  lägger till `.part`. Detta bekräftar ett filnamnslängdsproblem för dessa
  filer, inte ett P4-specifikt SD-hastighetsfel. Det förklarar inte ensamt
  tidigare lyckad-uppladdningsloggar, förlorad katalogåtkomst eller GPIO42-
  varningen; de observationerna ska utredas separat.
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
- Ägaren rapporterar att efter USB-flash av partitionstabellen använder P4
  hela flashen och FFat visar 22 172 kB. Stora SoundFonts fungerar på P4;
  mindre SoundFonts på S3 medger fler samtidiga röster. Karaoke-layouten
  fungerar på båda korten. Detta verifierar inte fjärrspel mellan enheterna.

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
  återmontering/fel och 1-bit-fallback. Från v0.23.10 körs SD på 40 MHz,
  samma klocka som C6-SDIO, även vid återhämtning.
  Felkod exponeras i webbdiagnostik. Slot 1/C6 lämnas igång.
- [ ] **Hårdvara:** verifiera att SD håller sig läs-/skrivbart samtidigt
  som Wi-Fi används, och att återmontering fungerar efter timeout.
- [x] v0.23.8: P4-diagnostik med frame pointers och oberoende SD-kommandosampling
  på CPU 1; pausbar SD-loggning och RAM-only ZIP. Ägarens seriella logg
  2026-10-31 bekräftar IDLE0-watchdog med `fs_upload_sd` på CPU 0.
- [x] Reproducerat med v0.23.8, med SD-loggning både på och av. Svars-timeout
  under CMD25 lämnar kortet i mottagningsläge (`r1=00000c00`). Därefter
  frågar ESP-IDF kortets status (CMD13) i 5 s på CPU 0 och `IDLE0`-watchdogen
  slår till. SD-loggern är inte orsaken.
- [x] v0.23.9 på hårdvara, 20 MHz: ingen watchdog, CMD12 tar kortet ur
  mottagningsläget och återmonteringen går snabbt. Skrivningen misslyckas
  fortfarande.
- [x] v0.23.10: två Wi-Fi-uppladdningar på 29,3 och 265,8 MB slutförda
  med SD på 40 MHz. Ägarens logg visar inga SD-fel, watchdogs eller
  återmonteringar efter v0.23.10-starten. Längre belastningstest återstår.
- [x] v0.23.11: båda slotar loggade faktisk klocka 50000 kHz och tre
  uppladdningar slutfördes utan SD-fel/watchdog. Unison skrivtid förbättrades
  cirka 3,3% i medel. Ägaren valde 40 MHz för liten vinst mot eventuell risk.
- [x] Felsökningsresan, bevisgränser och återanvändning på fler P4-kort
  dokumenterade i [SD/Wi-Fi-utredningen](P4_SD_WIFI_INVESTIGATION.md).
- [x] v0.23.12: fyra SD-uppladdningar med samordnad återhämtning efter
  att överföringarnas filer stängts. Normal Info-loggning, valbara nivåer
  och separat tillfällig SD-spårning i Diagnostik.
- [ ] Hårdvara v0.23.12: flera stora filer samtidigt, hashkontroll,
  avbrutna överföringar och återhämtning med flera filhållare.
  Verifiera loggnivåer, SD-spårningens timeout och normalnivå efter omstart.
- [x] Ägarens v0.23.12-logg: 199 små MIDI/KAR-uppladdningar slutförda
  (9 199 403 byte), utan nya SD-fel/watchdog. Hit_Trax med befintliga filer
  gav 25 HTTP-parserfel, inte SD-skrivfel.
- [x] v0.23.13: läs färdigt hoppade HTTP-kroppar före svar och dränera
  tidiga async-avslag före socketåteranvändning; stäng vid avbruten kropp.
- [x] Hårdvara v0.23.13: ägaren har flashat och provat Hit_Trax-rättningen
  och rapporterar att den fungerar klockrent. Leveransen är godkänd och mergad.
- [x] Ägarrapport 2026-11-01: senaste bygget är installerat på båda enheterna;
  filkopieringens nya status/presentation upplevs som mer logisk och fungerar
  bra. Inga hash- eller långtidsresultat rapporterade.
- [x] Ägarrapport 2026-11-01: ett specifikt Samsung 32PRO-SD-kort ger åter
  många skrivfel efter en period med fungerande filhantering och bedöms av
  ägaren som trasigt. Det är okänt om kortet var defekt från början eller
  skadades under tidigare tester. Kortet är därför en bekräftad
  hårdvarufelkälla i den historiska felsökningen, men förklarar inte i sig
  mjukvarufel som redan reproducerats eller åtgärdats.
- [ ] Undersök P4-loggens `host controller with slot registered` och
  `conflict found for GPIO[42]` vid återmontering. Bekräfta om GPIO42-
  konfliktrapporten är förväntad efter SD-slotens avregistrering eller om
  pin-matrisen inte städas korrekt. Fastställ vilken operation som först
  aktiverar återhämtningsläget och rapportera det ursprungliga SD-felet
  separat från Wi-Fi-roaming/avbruten HTTP-uppladdning.
- [ ] Långa FAT-filnamn på båda korten: höj/verifiera LFN-gränsen och
  samordna filhanterarens validering med FAT-konfigurationen och temporära
  uppladdningsnamn. Avvisa för långa namn med tydligt fel före skrivning.
  Lägg regressionstest för de två rapporterade namnen, gränsfall och
  `.part`-suffix; verifiera att befintliga filer inte skadas vid fel.
- [ ] **Hårdvara:** SD läses/skrivs, ljud hörs, batteriprocent rimlig.
- [x] **Hårdvara v0.22.2:** SD-läsning för Adventure/Karaoke, ljud och
  batterimätning bekräftade av ägaren. Längre SD-stabilitetstest återstår.
- [x] **Hårdvara v0.22.2:** intern RTC sätts av NTP och rapporterar `OK`.
  Betrodd tid efter mjukvaruomstart och VBAT-retention återstår att prova.

## IV. Wi-Fi via ESP32-C6 (ESP-Hosted)

- [x] `esp_hosted` 3.x (SDIO, C6 som slav) initieras på P4;
  Wi-Fi-anslutning bekräftad av ägaren med v0.22.0.
- [x] `my_wifi`, mDNS, webbserver, NTP och app-webb-OTA fungerar på P4.
  Ägaren bekräftar båda kortens senaste firmware efter v0.22.6-test.
- [ ] Visa C6-firmwareversion och värd/slav-kompatibilitet i diagnostik.
- [x] Webb-OTA kontrollerar nu C6-version och kräver firmware ≥2.6.0
  innan en uppdatering kan aktiveras; system-RPC saknas/är för gammal ger
  ett tydligt fel med hänvisning till återställningskontakten.
- [x] **Hårdvara v0.22.6:** ägaren bekräftar funktion på både S3 och P4.

## V. OTA för C6-radiochippet

- [x] Uppdatera C6 från P4 med ESP-Hosted RPC OTA (`begin/write/end/activate`).
  Administratörs-PIN kontrolleras; app-headern måste vara ESP32-C6; enheten
  startar om efter aktivering för att återansluta till radion. Host-OTA finns
  bara på P4 med Host-radio; S3-app-OTA påverkas inte.
- [x] CI bygger `host_fw.bin` från ESP-Hosted MCU-repositoryns aktuella
  källrevision `78b1c1c43c02b0c4e898cf87f94679278b1ddaff` i den separata
  artefakten `host-fw-esp32c6-78b1c1c`. C6-byggprojektets dubbla OTA-slots
  är 0x1C0000 byte; webb-OTA avvisar större filer.
- [ ] Kontrollera C6-version och värd/slav-kompatibilitet i diagnostiken
  vid uppstart. Flyttad till avsnitt X punkt 1.
- [ ] Dokumentera återställning via C6:s 4-poliga flashkontakt.
- [x] **Hårdvara:** C6-OTA från fabriksfirmware till 3.0.10 verifierad av
  ägaren; efter omstart rapporterar `eh_init_evt` host=3.0.10
  coprocessor=3.0.10 (match) och Wi-Fi fungerar.

## VI. UI för 480×800 och fler upplösningar

- [x] **Hårdvara v0.22.4:** ägaren bekräftar ren skärmväxling följd av
  OTA-progress, utan tidigare startflimmer. De flesta spel samt Karaoke
  har provats och fungerar; detta ersätter inte full layoutverifiering.
- [x] v0.22.6: startskärm, Wi-Fi-nätlista, lösenordshjul och
  anslutningsvy beräknar layout från skärmstorlek och aktuella
  radhöjder. S3 Normal behåller tidigare startgeometri; P4 fyller
  skärmen med större logga, rader och namn. Inställningsskärmen använder
  också hela P4-skärmen.
- [x] v0.22.6: sparad textstorlek (NVS `text_size`, enhet + webb/API):
  Liten / Normal (standard) / Stor på båda korten, Extra stor bara på
  skärmar med kortsida ≥ 400 px (P4). De fyra textrollerna
  `my_font_14/18/24/36` byts tillsammans (12/16/20/32, 14/18/24/36,
  16/20/28/40, 18/22/32/44 px) via `MyFonts::apply()`; typsnitten
  genereras reproducerbart med `tools/gen_fonts.sh`. Ändringen gäller
  när huvudmenyn byggs om (direkt efter Spara på enheten, nästa
  menyvisning vid ändring från webben). `tests/test_text_size_layout.py`
  kontrollerar start/Wi-Fi-layout för alla nivåer på båda upplösningarna.
- [x] **Hårdvara v0.22.6:** ägaren bekräftar att ändringarna är testade på
  båda enheterna. Fortsätt notera eventuella layoutproblem i senare test.
- [ ] Ta bort fasta 320×480-koordinater (inställningar, Wi-Fi, Yatzy,
  Adventure-setup) och använd `ui_metrics`/aktuell skärmstorlek.
- [ ] Typsnitt och träffytor skalas för större skärm. Textstorlek finns
  (v0.22.6); träffytor i spelen är kvar att skala.
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
- [x] P4-knappar kontrollerade mot schema: power/Key3 går till U20:s
  KEY-ingång, inte till P4 GPIO. BOOT/Key2 är GPIO35 och kan läsas under
  drift, men är inte deep-sleep-wake-kompatibel. Touch pollas utan wake-INT.
- [x] Välj RTC-/viloväg för P4: separat RTC-GPIO-knapp + kontrollerad
  VBAT-deep-sleep, light sleep med huvudmatning kvar, eller separat kalender-RTC.
  Ägaren har monterat DS3231 på P4. v0.23.15 väljer den automatiskt vid
  I2C-adress 0x68, annars används intern RTC. Ägaren rapporterar
  2026-11-01 att RTC fungerar perfekt med DS3231. Enskilda testfall
  (temperatur, RESET och strömavbrott utan NTP) är inte specificerade.
  DS3231 löser kalender-RTC-behovet; deep sleep är fortsatt en separat
  möjlig energisparfunktion.
  Se [hårdvarureferensens RTC-avsnitt](Waveshare_ESP32-P4-WIFI6-Touch-LCD-4.3_Hardware_Reference.md#rtc-retention-known-limits-and-possible-next-steps).
  Ägartest 2026-11-01 utan Wi-Fi/NTP: omstart via webben behåller betrodd
  RTC-tid. Fysisk RESET-knapp ger "väntar på NTP/manuell tid" och `?`,
  vilket stämmer med att koden bara godtar mjukvaruomstart/deep sleep.
  Uppmätt med v0.23.14: RESET-knappen ger samma power-on-reset som
  strömavbrott. RTC-räknaren börjar om från 0 och RTC-minnet är skräp.
  Intern RTC kan alltså inte bevara tiden; kvarvarande vägar är extern RTC
  eller kontrollerad VBAT-deep-sleep.
- [x] v0.23.14 hårdvarutestat av ägaren: "last alive" loggas efter omstart
  via webben på båda korten. RTC-minnet tappas vid power-on (P4
  RESET/strömavbrott, S3 power-on) och tiden blir osäker (`?`).
- [ ] Testa P4-tid utan Wi-Fi efter Diagnostik → Testkrasch, både Panic
  (`reset=4`) och Watchdog (`reset=5`, interrupt-watchdog), samt att core dump lagras.
- [ ] Utred S3:s spontana panic-omstarter på v0.23.13. Ägarens export
  visar sju panic-boots, lagrat core dump och korrupt intern FFat
  (256 av 257 rotposter med ogiltiga namn vid senaste boot).
  Exporten innehåller ingen kraschstack; samla core dump/seriell panic
  före slutsats eller reparation. USB-matning är inte fastställd som orsak.
  Ägaren rapporterar att FFat-problemet återkommer efter upprepade
  reparationer på S3, medan P4:s interna flash fungerar bra.
  Den exporterade historiken visar redan 128 rapporterat ogiltiga rotposter
  på v0.23.2, före nattens panic-omstarter; på v0.23.12 ökar antalet till 256.
  128 FAT-poster motsvarar 4096 byte (32 byte per diskpost), vilket motiverar
  granskning av rå katalog-/sektordata men inte bevisar en sektorbugg.
  Båda targets använder samma FAT/WL-kod och 4096-byte WL-sektorer;
  partitionstabellerna överlappar inte i källan. Verifiera även S3:s faktiskt
  flashade partitionstabell innan den utesluts.
  Serielogg 2026-11-01 verifierar även faktiskt flashad S3-tabell: samma
  offset/storlek som `partitions.csv`, 16 MB flash, inga överlapp.
  Spontan v0.23.14-krasch avkodad med exakt matchande ELF
  (SHA256 börjar 756290538): LoadProhibited på adress 0 i SPI-avbrottet,
  `spi_bus_lock.c:591` → `spi_bus_lock_bg_exit` → `spi_intr`.
  Där försöker IDF v6.1 återuppta `lock->acquiring_dev`, som saknas.
  Avbrottet avbröt ljudmixern; ljudfunktionerna i stacken är inte i sig
  bevis för ett ljudfel. S3-skärmen använder SPI2 och blandar pollade
  kommandon med köade DMA-färgöverföringar. Utred bus-lock-race och
  eventuell minneskorruption; ingen rotorsak är ännu bevisad.
  Efter denna panic loggas föregående körnings last-alive-tid och
  DS3231 är OK (OSF 0). Core dump sparas med giltig checksumma.
  Senare export 2026-11-01 (17:53 lokal tid) visar v0.23.14 boot #31
  med panic och kvarvarande last-alive-tid. FFat-räknaren är fortfarande
  256 av 257; ingen ytterligare försämring syns i räknaren. Ingen
  kraschstack finns i denna export, så samma SPI-fel är inte bekräftat.
  Ny serielogg 2026-11-01 från v0.23.15 bekräftar två ytterligare
  LoadProhibited på adress 0 i samma `resume_dev_in_isr` /
  `bg_exit_core` → `spi_bus_lock_bg_exit` → `spi_intr`.
  Avkodning med exakt matchande ELF (SHA256 börjar `a199c9825`,
  CI run 41326171656). Första avbrottet avbröt ljudmixern, det andra
  CPU:ns idle-task; felet kräver alltså inte att ljudmixern kör just då.
  Båda visar samma fel-PC `0x40376b11` och samma SPI-stack.
  Boot #33/#34 behåller last-alive och verifierar core-dump-checksumman.
  FFat-räknaren ligger kvar på 256 av 257. SPI-felvägen är nu upprepad,
  men bus-lock-race kontra minneskorruption är fortfarande oavgjort.
  **Formatera inte FFat innan råpartitionen och läsbara filer har säkrats.**
  Den manuella reparationsvägen säkerhetskopierar bara `players.bin` och
  `events.log`, inte alla filer/sparningar, och en formatering raderar övrigt
  innehåll. Filsystemet monteras redan vid boot; formatering behövs inte för
  att enheten ska starta. Efter rådump/backup kan ren formatering användas
  som kontrollerat jämförelsetest med känt fungerande SD-kort. Samsung
  32PRO-kortets bekräftade skrivfel är ett separat fel och får inte blandas
  ihop med den interna FFat-utredningen.
  Hälsokontrollen räknar alla namn med byte utanför ASCII som ogiltiga, även
  giltiga Unicode-namn; dess 256/257-räknare ensam är därför inte bevis för
  fysisk flashskada. Rå katalog-/sektordata behövs för att fastställa skada.
  Trolig mekanism (kodgranskning, ej bevisad med rådump): IDF:s FAT-diskio
  (`ff_wl_write`) raderar en 4096-byte-sektor och skriver den sedan i två
  separata anrop. En reset däremellan (panic, strömavbrott, USB-urkoppling)
  lämnar sektorn som 0xFF = 128 katalogposter med ogiltiga namn, vilket
  stämmer med stegen 128 → 256. `MyLog` gör fopen/append/fclose per händelse
  på `/ffat/events.log`, så rotkatalogen skrivs om ofta. S3:s många
  panic-omstarter ökar risken. Partitionstabellen i källan är korrekt
  (16 MB exakt, inga överlapp, 4 KB-justerat).
- [ ] Mät faktisk förbrukning och batteritid med skärmen släckt på båda
  korten. Touch-väckning innebär fortsatt aktiv elektronik; lova inte
  deep-sleep-förbrukning eller beräkna drifttid linjärt från batteriprocent.
- [ ] Inför femminutershistorik på SD för batterispänning, tidskälla,
  boot-ID/drifttid, aktiv tid per spel, bakgrundsbelysning/skärmtid,
  Wi-Fi-status, lagringsöverföringar och ljud/volym. Komplettera med
  händelser, filrotation och synliga tappade poster. Batterispänning är
  inte uppmätt ström; faktisk energimätning kräver extra mätutrustning.
  Intern flash som destination väntar tills S3:s FFat-problem är utrett.
  v0.23.16 implementerar valbar SD-CSV med rotation och tappade poster,
  sessions-/skärm-/Wi-Fi-/ljudtid samt SD/FFat-byteantal. Verifiera flera
  intervall och CSV-nedladdning på båda korten innan punkten stängs.
  Se [mätfält och begränsningar](POWER_HISTORY.md).
- [ ] Hårdvarutesta v0.23.16:s valbara viloläge på S3/P4: Wi-Fi och
  I2S/förstärkare stoppas endast utan spel/överföring, touch återställer
  inställningar/nätverk och SD-historiken fortsätter. Jämför urladdning;
  CPU/touch är fortsatt vakna, inte light sleep.
- [ ] Verifiera v0.23.17 på S3/P4: läsbara tidszonval på display/webb,
  sommartid och namnbyte i boot/huvudmeny, samtliga webbsidors gemensamma
  statusrad/meny samt DHCP/mDNS. v0.23.17 inkluderar historik/viloläge
  från v0.23.16, vars CI-bygge stoppades av en volatile-kompilatorvarning.
- [ ] Rivningsfri uppdatering (dubbla ramminnen/vsync) och PPA/DMA2D.
- [ ] Mätning av FPS och minne på båda korten.

### Strömsparutredning (v0.23)

- **Wi-Fi:** nuvarande S3-läge är `WIFI_PS_NONE` för låg webblatens. Den säkra
  första kandidaten är `WIFI_PS_MIN_MODEM` endast när skärmen är släckt; det
  är implementerat bakom skärmsläckningen. MIN_MODEM sover mellan access-
  punktens DTIM-beacons. `WIFI_PS_MAX_MODEM` kan sova längre men kräver lämpligt
  station-`listen_interval` och kan öka latens/missade paket. ESP-Hosted 3.0.10
  har RPC-stöd för `esp_wifi_set_ps()` och stationens listen interval; OnTheRoad
  sätter inte ett eget intervall. C6-beteende, strömvinst och återanslutning
  måste mätas på våra S3 respektive P4+C6; inga projektspecifika mätvärden finns.
- **DFS/automatisk light sleep:** `esp_pm_configure()` och
  `CONFIG_PM_ENABLE` kan sänka CPU-frekvensen när lasten är låg, men ingen
  frekvenspolicy är införd. S3 körs med sin befintliga frekvens och P4 med
  400 MHz. I2S/Karaoke kan behöva högre frekvens under rendering; P4:s
  DSI/DPI-vsync/DMA och S3:s QSPI-uppdatering måste verifieras mot PM-lås.
  UART/USB-loggens beteende vid sleep/frekvensändring måste också provas. Ingen
  säkert generaliserbar mW- eller batteritidsvinst är känd.
- **Pausa Wi-Fi helt:** kan minska förbrukning mer än modem-sleep men bryter
  webbtillgänglighet och kräver återanslutning vid väckning. Ingen av/på-
  livscykel eller återanslutningstid är mätt; behåll därför Wi-Fi anslutet och
  använd endast MIN_MODEM i v0.23.
- **Deep sleep/touch:** deep sleep stänger av körande spel/ljud/Wi-Fi och
  återstartar appen. Touch kan väcka endast om controllerens INT är kopplad till
  en RTC/LP-wake-kompatibel GPIO; I2C är inte aktiv som wake-källa. S3 INT är
  GPIO47, inte RTC GPIO på ESP32-S3. P4 touch INT är inte ansluten (`-1`), och
  touch pollas. Deep-sleep touch-wake är därför inte tillgänglig med nuvarande
  pinmap. Väcktiden omfattar boot, enhetsinitiering och Wi-Fi-anslutning; den är
  inte mätt och blir längre än ren backlight-väckning.

## IX. Fler kort i framtiden

### Guide: lägga till ett kort

1. Lägg till ett ID och en rad i `HwProfile::Info` (`main/hw_profile.h/.cpp`):
   namn, upplösning, periferikapacitet, flash/PSRAM, SoundFont-reserv och
   resamplingsstandard. Applikationskod ska fråga profilen om kapacitet i stället
   för att sprida kortvillkor.
2. Lägg kortets GPIO- och busskarta i `main/board_pins_<id>.h`, välj den via
   `main/board_pins.h` och håll kortdrivrutinerna bakom de gemensamma
   `board_*.h`-API:erna.
3. Binda profil till ESP-IDF-target i `main/Kconfig.projbuild`; lägg
   targetflaggor i `sdkconfig.defaults.<target>`. Om kortet behöver annan flash-
   layout ska det ha egen `partitions.<id>.csv`, vald av dess target-overlay.
4. Lägg målberoenden i `idf_component.yml` med target-regler. Lägg till
   drivrutinsfiler i `main/CMakeLists.txt` bakom profilen; undvik spridda
   `CONFIG_IDF_TARGET_*`-villkor i funktionskod.
5. Lägg target/profile-raden i CI-matrisen och verifiera att buildloggen väljer
   rätt profil, SDK-defaults, partitionstabell och beroenden. Lägg till
   hosttest för profilens kapabiliteter och minnesbudgets.
6. Först när stegen är stabila: samla metadata och pin-namn i
   `boards/<id>/board.h` och generera/validera profil- och konfigurationsdelarna
   från den filen. Flytta inte byggval till generering innan CI kan bevisa att
   genererade filer motsvarar targeten.

**Rekommendation – hybridgränser:** härled tillgänglig budget från faktiskt
ledigt/största PSRAM-block och flashpartitionernas storlek vid körning. Profilen
anger rekommenderat tak, reserv och standardval för respektive funktion; en
funktion får inte slås av automatiskt bara för att ett annat kort saknar minne.
Varje funktion ska ha en dokumenterad miniminivå och ett kontrollerat
reservbeteende. För Karaoke är nuvarande rekommendation 8 MB PSRAM, 512 kB
SoundFont-reserv och nedsampling på S3; P4-profilen anger 32 MB, 2 MB reserv och
nativ SoundFont-samplingsfrekvens. Större laddningar begränsas alltid av faktisk
ledig PSRAM.

## X. Nästa runda (v0.23, ägarens lista 2026-10-29)

Underlag och detaljer finns i PR-beskrivningen för den här rundan. Ordningen
nedan är arbetsordningen; utredningspunkter ska ge ett dokumenterat svar med
rekommendation innan kod skrivs.

1. **ESP-Hosted-version i webben.** Visa host-/coprocessor-version och
   match/mismatch (samma info som `eh_init_evt: esp-hosted fw versions:
   host=3.0.10 coprocessor=3.0.10 (match)`) i Översikt och Diagnostik, bara
   för kort med Host-radio. Ersätter punkten "Kontrollera C6-version" i
   avsnitt V.
   - [x] Cacha versionen vid uppstart (ingen RPC per webbanrop).
   - [x] Startkontroll/diagnostik: varning vid mismatch eller okänd version.
   - [ ] **Hårdvara:** verifiera match, mismatch och okänd-version-varning på P4.
2. **Karaoke på stor skärm.** P4 visar 4 textrader på övre halvan; resten är
   svart.
   - [x] Centrera texten horisontellt och vertikalt.
   - [x] Antal rader beräknas från skärmhöjd och textstorlek (S3 oförändrat).
   - [x] **Hårdvara, ägarrapport:** Karaoke-layouten har provats på båda
     skärmarna. Den här rapporten gäller layouten, inte fjärrspel.
3. **Kortdefinitioner.** Utred och föreslå hur nästa kort läggs till med i
   princip en definitionsfil (`hw_profile`, `board_pins_*.h`, sdkconfig,
   partitionstabell, CI-matris, `idf_component.yml`-regler).
   - [x] Dokumentera förslaget i "IX. Fler kort i framtiden"; capability- och
     minnesbudgetar ligger i profilen.
   - [x] Rekommendation: automatiska runtime-budgetar med profilens
     funktionsvisa tak/reserver och standardval; ingen automatisk avstängning.
4. **Flash, PSRAM och SoundFont.**
   - [x] P4-tabell med 32 MB flash och 21.875 MB FFat; OTA-offsetar behålls.
     S3-tabellen är oförändrad. **USB:** befintliga P4 måste flasha
     partitionstabellen en gång; se "P4-partitionstabell: USB-migrering".
   - [x] Profilstyrt SoundFont-tak/reserv; S3 behåller 7 MB/512 kB och
     nedsampling, P4 får dynamiskt minnestak/2 MB reserv utan nedsampling.
   - [x] Profilstyrt rösttak: S3 högst 96, P4 högst 192; väljaren visar bara
     värden som ryms inom kortets gräns.
   - [x] Dokumentera SoundFont-storlek kontra PSRAM, samplingsfrekvens och
     kvalitet i `MUSIC_KARAOKE_PLAN.md`.
   - [x] **Hårdvara, ägarrapport:** efter USB-flash av partitionstabellen
     använder P4 hela flashen och FFat visar 22 172 kB. Stora SoundFonts
     fungerar på P4; mindre SoundFonts på S3 medger fler samtidiga röster.
   - [ ] **Hårdvara:** verifiera omstart och OTA efter P4-migreringen.
5. **Stereoljud till webbsidan i Karaoke.** Utred stereoström (idag mono
   22050 Hz från mixen) för P4 och kommande kort; implementera om
   bandbredd/CPU räcker, annars dokumentera varför inte.
   - [x] Utredning och bandbreddsestimat dokumenterade i
     `MUSIC_KARAOKE_PLAN.md`.
   - [x] **Ägarbeslut:** behåll mono i v0.23; stereo kräver en stereo-synt,
     mixer/tap, ringbuffer och webbavkodare, samt CPU-/ljudmätning på P4.
6. **Strömsparning, nästa steg.**
   - [x] Karaoke: håll vaken medan en låt spelar; inaktivitet räknas från
     låtens slut.
   - [x] Pågående spel: dubbel inaktivitetstid innan skärmsläckning.
   - [x] Wi-Fi `MIN_MODEM` när skärmen släcks, av igen vid väckning. Styrs av
     befintlig skärmtimeout/manuell släckning.
   - [x] Utred Wi-Fi/DFS/deep sleep och dokumentera rekommendationerna nedan.
     **Hårdvara:** mät effekt och verifiera återanslutning på båda korten.
   - [ ] **Hårdvara:** ägaren rapporterar att P4-panelens bild fortfarande syns
     när endast bakgrundsbelysningen släcks. Utred controller-specifik
     display-off/sleep och väcksekvens; verifiera att skärmen släcks och
     återställs på båda korten innan strömsparningen markeras klar.
7. **Spela mellan enheter (Yatzy fjärrspel).** Flera OnTheRoad
   och mobiler/webbläsare på samma nät spelar samma spel; en enhet är
   master. Bygger på befintlig webb-live-vy och WebSocket-platser.
   - [x] Designdokument: upptäckt (mDNS), master-val, protokoll, säkerhet
     (PIN), återanslutning och Yatzy först.
   - [x] mDNS annonserar `_ontheroad._tcp` med protokollversion och stödda spel.
   - [x] Unik MAC-suffix på tjänstinstansen; upptäckaren hämtar SRV/TXT/A även
     när PTR-svaret inte innehåller kompletta serviceposter.
   - [x] Enhetens **Spela tillsammans**-vy hittar protokollkompatibla värdar
     och visar endast aktiva Yatzy-spel med ledig PIN-skyddad webbplats.
   - [x] Värden exponerar ett opakt sessions-ID i `/api/game` och
     WebSocket-`state`/`joined`. Ett `join` med ID avvisas om sessionen ändrats
     eller platsen inte längre är en ledig webbplats; äldre browser-`join`
     utan ID är oförändrad.
   - [x] ESP-klient med värd-/platsval, maskerad PIN-inmatning och touch-vy.
     Snapshot, tur, tärningar, möjliga kategorier och poäng kommer från
     värden; ESP-klienten skickar bara tillåtna `hold`, `roll` och `choose`.
   - [x] Återanslutning använder endast vald värd, sessions-ID och plats.
     Bytt session eller upptagen plats stoppar anslutningen och kräver en ny
     join. PIN skrivs inte i loggar och browser-join utan sessions-ID fungerar
     fortsatt.
   - [x] CI-run 41051798946 bygger ESP32-C6-host, S3 och P4; S3-jobbets
     automatiska hostkontroller passerar. ESP-IDF saknas lokalt, så targetbyggen
     görs i CI.
   - [ ] Komplettera fysisk verifiering av återanslutning och båda
     värd-/klientriktningarna enligt checklistorna nedan.
   - [x] **Hårdvara, ägarrapport:** discovery hittar andra enheter och visar
     deras aktiva spel. Detta är inte ett speltest.
   - [x] **Hårdvara, ägarrapport 2026-10-30:** S3 som värd och P4 som klient:
     P4 anslöt med PIN, väntade på sin tur och kunde spela i den förenklade
     fjärrvyn. Detta verifierar grundläggande kommunikation i denna riktning,
     inte alla spelkommandon eller fullständig hårdvarutestning.
   - [x] **Hårdvara, ägarrapport:** Ägaren bekräftar att spel fungerar mellan
     enheterna och med webben. Exakt kort, riktning och genomgångna moment är
     inte specificerade; checklistan per kort nedan står därför kvar.
   - [x] **Leveransgodkännande 2026-10-30:** Ägaren rapporterar att
     Spela tillsammans är i fungerande skick efter fjärrvyns förbättringar.
     Återanslutning och ospecificerade testmoment är fortsatt öppna;
     godkännandet innebär inte att hela hårdvarumatrisen är verifierad.
   - [ ] **Hårdvara, fjärrspel – väntar på fysisk verifiering:** ESP32-S3:
     [ ] join/PIN [ ] kast/håll/välj [ ] samtidig browser [ ] återanslutning.
   - [ ] **Hårdvara, fjärrspel – väntar på fysisk verifiering:** ESP32-P4:
     [x] join/PIN (som klient mot S3) [ ] kast/håll/välj [ ] samtidig browser
     [ ] återanslutning.
     Full fysisk änd-till-änd-verifiering är alltså inte utförd på något kort;
     den rapporterade P4-klienttestningen täcker inte omvänd riktning eller
     hela testchecklistan.
8. **FAT-långa filnamn och webbuppladdning (v0.23.2).**
   - [x] Aktivera 255 FAT-LFN-kodenheter på båda targets. Validera UTF-8 och
     räkna UTF-16-kodenheter per namnkomponent; avvisa ogiltiga/överlånga namn
     och fullständiga bytevägar som inte ryms i webbgränserna.
   - [x] Webbuppladdning använder unika, korta syskonfiler i stället för att
     lägga `.part` till destinationens basnamn. Staging skapas exklusivt;
     vid ersättning sparas originalet tills den nya filen installerats och
     återställs om installationen misslyckas. URI-gränsen är 1024 byte.
   - [x] Hostregressioner för rapporterade 87/81-teckennamn, 255/256-gränsen,
     UTF-8/UTF-16, temp-/URI-gränser, avbruten staging och befintligt innehåll.
   - [x] Konfliktval vid uppladdning: hoppa över (standard), avbryt eller skriv
     över; hoppa över fortsätter batchen och byter aldrig ut befintligt innehåll.
   - [x] SD-uppladdningar körs en i taget efter rapporterade SDMMC-skrivfel.
     En uppladdning dubbelbuffrar mottagningen i 32 KiB-block och försöker
     använda en 32 KiB intern DMA-kapabel skrivcache, med PSRAM-cache som reserv.
     SD:s öppna filgräns höjs till åtta.
   - [x] Uppladdningsloggen skiljer mottagning, buffertväntan, SD-låsväntan,
     skrivning, flush och stängning åt för prestandadiagnos.
   - [x] **CI:** ESP32-S3, ESP32-P4 och C6-jobbet passerade för v0.23.2 (run 183).
   - [ ] **Hårdvara:** prova 87/81-teckennamnen, Unicode och gränsfall samt
     avbruten/överskrivande uppladdning på båda korten. Verifiering återstår.
   - [ ] **Hårdvara/prestanda:** verifiera uppladdning och återhämtning efter
     SD-I/O-fel på den berörda enheten; watchdog- och kortstatusbeteendet är
     okänt. Upprepa även 144 MB-uppladdningen på båda korten och jämför `recv`,
     `buffer wait`, `SD lock wait`, `write` och `flush`.

9. **Filhantering och SD-kort (v0.23.3).**
   - [x] Lägg till explicit FAT-formatering av SD-kort med två bekräftelsesteg;
     admin-PIN krävs när en sådan är konfigurerad.
   - [x] Webbläsaren kan ladda upp en hel katalog med högst tre mappnivåer;
     befintliga kataloger kan återanvändas och filkonfliktens standard är hoppa över.
   - [x] Dokumentera filhanteringsändringar och ägarrapporterade mätvärden i
     `docs/FILE_MANAGEMENT.md`; höj versionen till v0.23.3.
   - [ ] **CI:** bygg ESP32-S3, ESP32-P4 och C6-jobbet för v0.23.3.
   - [ ] **Hårdvara:** verifiera formatering, admin-PIN, kataloguppladdning och
     återmontering på korten; prestandaresultat är observationsdata, inte garanti.

## Leveranslogg

| Datum | Steg | Status |
|---|---|---|
| 2026-10-30 | v0.23.1 fjärrspel | Ägaren bekräftar fungerande Spela tillsammans efter förbättringarna av fjärrvyn. ESP-klienten återanvänder tärnings-/bägarutseende, separat bo för hållna tärningar, kastanimation och dubbeltryck för poängval. Återanslutning och full testmatris per kort kvarstår. |
| 2026-10-29 | v0.22.6 hårdvarutest + merge | Ägaren bekräftar att båda enheterna är testade; PR #23 mergad till `main`. Start/Wi-Fi/textstorlek fungerar. |
| 2026-10-29 | v0.22.7 Host OTA, namn och P4-inställningar | Host-OTA och `host_fw.bin` CI från ESP-Hosted MCU, enhetsnamn för web/mDNS/DHCP, RGB-kontroller dolda på P4. Första C6-OTA föll på för stora chunkar (1500 B > ~1480 B gräns); efter fix till 1400 B uppdaterade ägaren C6 från fabriksfirmware till 3.0.10 (host=3.0.10 coprocessor=3.0.10, match). |
| 2026-10-29 | Plan | Skapad. |
| 2026-10-29 | I + II (PR #23) | Kod klar, CI grön för S3 och P4 (run 40884855527). Väntar på hårdvarutest av `merged.bin`. |
| 2026-10-29 | Första P4-test | Ägaren bekräftar uppstart och Wi-Fi med v0.22.0 (CI run 40885750801). RTC och batterimätning återstår. |
| 2026-10-29 | Integration med main | PR #19:s multimap, portaler och UI-fixar integrerade i P4-branchen. Versionskonflikt löst till v0.22.1. S3 och P4 gröna för mergecommit `07b5853` (CI run 40887806090); hostkontroller för multimap, Adventure och Karaoke godkända. |
| 2026-10-29 | Kringutrustning v0.22.2 | Intern RTC, P4 batteri-ADC, GPIO45 för SD-kortmatning och återhämtning implementerade i `2ac004b`. Båda targets och hosttester gröna (CI run 40893098350). OTA bekräftad av ägaren på föregående bygge; nytt hårdvarutest krävs. |
| 2026-10-29 | Diagnostik och FFat v0.22.3 | Ägaren bekräftar SD, ljud, ADC och NTP→RTC med v0.22.2. Korrigerar RTC-vänteläge vid start, P4-identitet/upplösning, GT911 firmwarebredd samt uttrycklig reparation av omonterad intern FFat. |
| 2026-10-29 | v0.22.3 byggverifiering | `9a0ea0d`, CI run 40901183845 grön för S3/P4 inklusive MAC-fixen. Ersätts vid leverans av v0.22.4 med OTA-skärmfixen nedan. |
| 2026-10-29 | OTA-skärm v0.22.4 | `e31311a`, CI run 40902453740 grön för S3/P4. P4-appfil nedladdad och SHA-256 verifierad: `a9b8b4c56d372dbaa96c37f4c3f07badc4848cc0d541dc5f52bb180a41e64f34`. Fysisk flimmerkontroll återstår. PR #23 är fortsatt draft, inte mergad; GitHub rapporterar konflikt med main som behöver hanteras inför merge. |
| 2026-10-29 | Skärmsläckning v0.22.5 | Ägaren bekräftar OTA-skärmfixen i v0.22.4 samt de flesta spel och Karaoke på P4. `f7385e2`, CI run 40909089936 grön för båda targets. Automatisk/manuell släckning och väckfilter implementerade; fysisk verifiering på båda korten återstår. |
| 2026-10-29 | Textstorlek v0.22.6 | `4db0df5`, CI run 40924732183 grön för båda targets. Startskärm, Wi-Fi och inställningar skalas efter skärm; textstorlek Liten/Normal/Stor (+ Extra stor på P4). PR #22 (gammal planversion) sammanslagen i main; denna branch behåller den nyare planen. Hårdvarutest återstår. |
| 2026-10-29 | v0.23.0 arbetspaket | ESP-Hosted-diagnostik, Karaoke-layout, profilbaserade SoundFont-budgetar, P4-partitioner, timeoutpolicy, Wi-Fi MIN_MODEM och Yatzy mDNS; utredningar i docs. Target CI och fysisk verifiering ska fortfarande köras. |
| 2026-10-29 | v0.23 X.7 discovery-slice | Enhetens UI filtrerar mDNS-värdar via `/api/game` och visar lediga PIN-skyddade Yatzy-webbplatser. Native WebSocket-join/fjärrstyrning är uttryckligen inte levererad; ESP-IDF-byggen och **Hårdvara**-test återstår. |
| 2026-10-30 | Ägarrapporterad hårdvara | Karaoke-layout fungerar på båda korten. Efter USB-flash av partitionstabellen använder P4 hela flashen och FFat visar 22 172 kB; stora SoundFonts fungerar på P4. Mindre SoundFonts på S3 medger fler samtidiga röster. mDNS-discovery hittar andra enheter och listar aktiva spel. Detta är inte verifiering av fjärrspel; omstart/OTA efter migrering och fysisk Yatzy-omgång återstår. |
| 2026-10-30 | v0.23.1 X.7 sessions-ID-inkrement | `PROJECT_VER` och README bumpade till v0.23.1. `/api/game` och WebSocket-state/joined skickar ett opakt sessions-ID; `join` med ID avvisas vid sessionsbyte eller upptagen/icke-webbplats. Browserformat utan ID bibehålls. ESP WebSocket-klient och fjärr-UI saknas fortfarande. Protokoll-/språktester körda; S3/P4-byggen kan inte köras lokalt eftersom ESP-IDF saknas. Fysisk fjärrspelstestning på båda korten är fortsatt väntande. |
| 2026-10-30 | X.7 ESP Yatzy-klient | ESP-firmware har WebSocket-join, maskerad PIN, värdauktoritativ touch-vy samt lagliga kast/håll/kategori-val. CI-run 41051798946 är grön för C6-host, S3 och P4; S3-jobbets hostkontroller passerar. Fysisk end-to-end-verifiering och samtidig browser-/ESP-testning på S3/P4 väntar. |
| 2026-10-30 | X.7 första ägarrapporterade speltest | S3 som värd och P4 som klient: P4 anslöt med PIN, väntade på sin tur och kunde spela i den förenklade vyn. Grundläggande kommunikation fungerar i denna riktning. Test av alla kommandon, samtidig browser, återanslutning och motsatt riktning återstår; full verifiering på båda korten är fortsatt väntande. |
| 2026-10-30 | X.7 ägarrapporterad gameplay | Ägaren bekräftar att spel fungerar mellan enheterna och med webben. Kort, riktning och detaljerad testmatris är inte angivna; checklisterna för S3/P4 står kvar tills dessa verifierats. |
| 2026-10-30 | v0.23.2 FAT/webbuppladdning | FAT-LFN 255, UTF-8/UTF-16-validering och skyddad staging med återställning; fokuserade hosttester godkända. ESP32-S3/P4/C6 CI passerade i run 183; hårdvaruverifiering återstår. |
| 2026-10-30 | v0.23.3 filhantering | SD-format med bekräftelse/admin-PIN, kataloguppladdning till tre nivåer och hoppa över som standard; mätningar dokumenterade. CI och hårdvaruverifiering återstår. |

### P4-partitionstabell: USB-migrering

P4-tabellen utökar FFat från 5.875 MiB till 21.875 MiB. Appslotarnas offset
(`0x10000` och `0x510000`), `otadata`, NVS och FFat-start (`0xA10000`) är
oförändrade, så app OTA använder samma slotar. En gammal installerad
partitionstabell kan däremot inte ändras via app OTA: uppdateringen måste göras
via USB en gång.

1. Säkerhetskopiera innehåll från FFat innan migrering. Den befintliga FAT-
   volymen kan fortsätta fungera eftersom startadressen inte flyttas, men FAT:s
   egen volymstorlek kan förbli den gamla; utökad kapacitet kan kräva att FFat
   formateras och filer återställs. Förlita dig inte på automatisk migrering.
2. Bygg för `esp32p4` så att `partitions.esp32p4.csv` väljs. Anslut USB och kör
   `idf.py -p PORT partition-table-flash` för att skriva den nya tabellen. Läs
   tillbaka/validera loggen och starta om. En full USB `idf.py -p PORT flash`
   från samma P4-build skriver också rätt tabell, men raderar mer firmwaredata.
3. Kontrollera FFat, OTA till en appslot, omstart och coredump-partition.
   Formatera FFat endast efter säkerhetskopiering och återställ därefter
   användarfilerna. Testa inte migreringen på enda kopian av användardata.

**Hårdvara:** ägaren behöver verifiera USB-uppdatering, befintlig FFat-data,
utökad kapacitet, OTA och återstart på P4. Ingen P4-migrering har mätts här.

OTA-artefakter för v0.22.5, nedladdade och SHA-256 verifierade:

- S3: `firmware-esp32s3-v0.22.5`, app
  `91553aa60006bbd11bd6997697a3c84d9c021f53c176461926e42f2a78dd6864`.
- P4: `firmware-esp32p4-v0.22.5-unverified`, app
  `f7d00ace3b0f3a2e24527023cca32abc42ec4fc91bee4aca811a012e726aeaa4`.
- Fil i respektive artefakt: `build/MyEsp_OnTheRoad.bin`, inte `merged.bin`.
  Ingen merge utförd.

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

### Nästa hårdvarutest (v0.22.6)

OTA-artefakter från [CI run 40924732183](https://github.com/Bokbacken/MyEsp-OnTheRoad/actions/runs/40924732183),
fil `build/MyEsp_OnTheRoad.bin`:

- S3: `firmware-esp32s3-v0.22.6`, SHA-256
  `93f52531098dde01d5461f3d5262df565637057b923f770f588ea437fb8b9389`
- P4: `firmware-esp32p4-v0.22.6-unverified`, SHA-256
  `3e6aba9944a7d71c92478a813ed9693ca12099fad25a5d639892569b968c379d`

1. Starta om och se startskärmen; anslut Wi-Fi via enheten (nätlista,
   lösenordshjul, anslutningsvy). P4 ska fylla skärmen, S3 se ut som förut.
2. Inställningar → Textstorlek: prova varje nivå (Extra stor finns bara
   på P4), spara och gå igenom meny, spel, Karaoke och Äventyr. Notera
   skärmar där text klipps eller överlappar. Ändring via webben gäller
   vid nästa visning av huvudmenyn.

### Tidigare hårdvarutest (v0.22.5)

1. Hämta rätt artefakt för kortet enligt ovan från
   [CI run 40909089936](https://github.com/Bokbacken/MyEsp-OnTheRoad/actions/runs/40909089936).
   Installera `build/MyEsp_OnTheRoad.bin` via den fungerande OTA-vägen;
   inte `merged.bin`. På båda korten: välj 30 s i Inställningar, spara
   och prova timeout samt manuell släckning. Väckning och 250 ms-spärr
   provas enligt checklistan ovan; kontrollera sparad tid efter omstart.
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
