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

### Beslut

- Senaste releaser föredras (ESP-IDF 6.1, LVGL 9.6). Waveshare-BSP:n låser
  LVGL 9.5 och egen LVGL-adapter; vi använder den bara som referens för stift,
  panelsekvens och timing. Egna drivrutiner bakom samma `board_*`-API.
- Ett `HwProfile` per kort väljs i Kconfig och är bundet till target.
- Gemensam `sdkconfig.defaults` + `sdkconfig.defaults.<target>`; inget
  `CONFIG_IDF_TARGET` i den gemensamma filen. Bygg med separata katalogen
  `build_esp32s3` / `build_esp32p4`.
- Firmwareartefakter namnges per kort. En OTA-fil för fel kort ska avvisas.
- Wi-Fi-API:t i `my_wifi` behålls; på P4 levereras det av
  `esp_wifi_remote` + `esp_hosted` mot C6.

## I. Byggsystem för två targets

- [x] Plan och återstartsdokument (detta dokument).
- [ ] Kconfig: profil bunden till target (`ES3C35P` ↔ S3, `P4_43` ↔ P4).
- [ ] `sdkconfig.defaults` uppdelad i gemensam + `.esp32s3` + `.esp32p4`.
- [ ] Stift per profil (`board_pins_es3c35p.h`, `board_pins_p4_43.h`).
- [ ] Källfiler per profil i `main/CMakeLists.txt`; P4-beroenden bara för P4.
- [ ] CI-matris: S3 och P4 byggs i varje PR, artefakter
  `firmware-esp32s3-…` och `firmware-esp32p4-…`.
- [ ] README: byggkommandon för båda korten.

## II. Första körbara P4-bygget: grafik

- [ ] MIPI-DSI-buss, LDO för DSI-PHY, ST7701-initiering, DPI-panel 480×800
  RGB565 (timing från Waveshare-BSP 1.0.1).
- [ ] `my_lvgl` flush mot DPI-ramminnet, rotation 180° och karaoke-landskap
  via LVGL-mjukvarurotation.
- [ ] Bakgrundsbelysning GPIO26, LEDC PWM, inverterad.
- [ ] GT911-touch på I²C GPIO7/8, adress `0x5D`/`0x14`, polling utan INT.
- [ ] I²C-skanning och RTC på delad buss.
- [ ] Ingen RGB-lysdiod: `Rgb` blir no-op utan att påverka appen.
- [ ] Startskärmen visar rätt kortnamn/upplösning.
- [ ] CI-bygge för P4 grönt.
- [ ] **Hårdvara:** färgtest, startskärm och huvudmeny syns; touch träffar rätt.

## III. P4-kringutrustning

- [ ] microSD: SDMMC slot 0 (IO MUX), on-chip LDO kanal 4 som kortmatning;
  avmontering får inte stänga SDMMC-värden som C6-länken använder.
- [ ] Ljud: ES8311 med I²S MCLK13/BCLK12/WS10/DOUT9/DIN11, förstärkare
  GPIO53 (aktiv hög).
- [ ] Batteri: ADC på GPIO20, delare 3:1 som förvald skala.
- [ ] **Hårdvara:** SD läses/skrivs, ljud hörs, batteriprocent rimlig.

## IV. Wi-Fi via ESP32-C6 (ESP-Hosted)

- [ ] `esp_wifi_remote` + `esp_hosted` (SDIO, C6 som slav) för P4.
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

- [ ] Rivningsfri uppdatering (dubbla ramminnen/vsync) och PPA/DMA2D.
- [ ] Mätning av FPS och minne på båda korten.

## IX. Fler kort i framtiden

- [ ] Guide: lägga till ett kort (profil, stift, drivrutiner, overlay, CI).

## Leveranslogg

| Datum | Steg | Status |
|---|---|---|
| 2026-10-29 | Plan | Skapad. |
