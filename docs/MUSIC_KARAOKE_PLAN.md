# Karaoke och musik – design och plan

Senast uppdaterad: 2026-10-25 (v0.13.1). Status: steg 1 klart i v0.13.0 och utökat i v0.13.1 (liggande skärm,
flera ljudbanker; ej HW-testat), steg 2 (mikrofon och
poäng) planerat till v0.14.0.

## 1. Mål

- Spela vanliga karaokefiler (`.mid`, `.kar`) från SD-kortet med riktiga instrumentljud (SoundFont).
- Visa texten på enheten, rad för rad, med markering av det som ska sjungas just nu.
- Kunna stänga av melodin och sjunga själv, byta tonart och tempo.
- Komma ihåg inställningar per låt (melodispår, tonart, tempo) i en läsbar textfil.
- Sedan: mikrofon, tonhöjdsdetektering och poäng.

Karaoke är en egen modul (`main/karaoke.*`, `main/ui_karaoke.*`) och ersätter inget av tärningsspelen eller äventyret.

## 2. Arkitektur (v0.13.0)

```
SD: /otr/karaoke/*.mid|.kar ──► otr_midi::parse() ──► Song (händelser, stavelser, rader, kanalstatistik)
SD: /otr/sf2/*.sf2 ──► otr_synth::SfSynth (TinySoundFont, sampel i PSRAM)
                                     ▲
mixertasken (core 0) ── renderCb() ──┤  Player::advance() → noteOn/Off/CC/pitch → SfSynth::render()
                                     │  (håller karaoke-mutexen kort; blir den upptagen hoppar vi ett block)
LVGL-tasken ── ui_karaoke ── Karaoke::view() (position, aktuell rad, stavelseindex) ── spangroup
```

- **`otr_midi`** (ren C++17, hosttestad): SMF format 0 och 1, running status, meta/sysex, tempokarta (mikrosekunder
  per händelse), text från `.kar` (`@T`/`@L`-taggar hoppas över, `/` = ny rad, `\` = ny vers) och vanliga
  lyrics-händelser (FF 05). Stavelser grupperas till rader. `Player` sköter tid, tempo 50–150 % och sökning
  (spelar upp controller-, program- och pitch bend-händelser fram till målet så att kanalerna låter rätt).
- **`otr_synth`**: vendrad TinySoundFont (MIT). Patchar för ESP32-S3: sampel lagras som `int16` (halva minnet –
  en ljudbank på 6 MB får plats i 8 MB PSRAM), lågpassfiltret i `float` (S3 har ingen double-FPU), egen
  fixpunktsloop (32.32) för monorendering, transponering ±12 (trumkanalen transponeras aldrig), röststöld (äldsta rösten återanvänds när 32 röster är upptagna).
  -14 dB headroom i stället för -6 dB eftersom täta arrangemang når 32 röster.
- **`Audio::setExternal(fn, ctx)`**: extern musikkälla i mixern. Under karaoke pausas spelmusiken och fortsätter
  efteråt. `setExternal(nullptr)` väntar tills mixern lämnat callbacken innan kontexten släpps.
- **SF2-laddning** sker i en egen task när karaokemenyn öppnas och frigörs när man lämnar den. Utan ljudbank
  spelas texten i tyst läge.
- **v0.13.1 – liggande skärm:** när karaokemodulen öppnas sätts LVGL-rotationen till 90° (eller 270°,
  `kara_flip` i NVS) med `Display::setLandscape(true)`. Flushen roterar varje delområde med `lv_draw_rotate` till en
  extra buffert (samma storlek som en ritbuffert, PSRAM) och skickar det fysiska området; MADCTL ändras inte.
  LVGL roterar pekpunkterna själv. Vid utgång återställs stående läge (180°-inställningen gäller som förut).
- **v0.13.1 – flera ljudbanker:** valordning: låtens `sf2=` i registret → global standard (NVS `kara_sf2`, väljs
  med knappen Ljudbank i låtlistan) → `default.sf2` → första giltiga `.sf2`. När den öppna låten vill ha en annan
  bank laddas den om i fonttasken (gamla banken frigörs först, bara en i PSRAM); under laddningen står låten still
  och mixern rör inte synten. Filerna kontrolleras med `Sf::probe` (RIFF/sfbk, `ifil` < 3, max 7 MB).
- **v0.13.1 – ljud av:** går man in i karaoke med ljudet avstängt frågar en dialog om ljudet ska slås på.

## 3. Registret `karaoke.reg`

Textfil i `/otr/karaoke/`, skapas automatiskt och kan redigeras för hand:

```
# OnTheRoad karaoke registry: crc32 size melody(-1..15) transpose tempo% [sf2=font] name
8c5c36ba 51234 2 0 100 Abba_Another town.mid
1f00aa12 40960 -1 -2 95 sf2=Florestan%20Basic.sf2 Ainbusk Singers_Älska mej.mid
```

- v0.13.1: valfri token `sf2=<filnamn>` (ljudbank för låten i `/otr/sf2`, mellanslag som `%20`, `%` som `%25`).
  Gamla rader utan token läses som förut; snedstreck i namnet eller namn över 63 tecken ignoreras.

- Nyckel = CRC32 + filstorlek, så inställningarna följer med om filen byter namn. Namnet är bara information.
- `melody` -1 = automatisk detektering, 0–15 = vald kanal (visas som kanal 1–16 i UI). Trumkanalen kan inte väljas.
- En rad skrivs när man ändrar något för låten. Skrivningen går via en tmp-fil och rename.

## 4. Melodispårsdetektering

För varje kanal med noter (utom trummor):

1. **Textträff:** andel stavelser som har en not-på i kanalen inom ±120 ms.
2. **Enstämmighet:** andel noter som överlappar en annan not i samma kanal (`overlapPct`); ackord sänker poängen.
3. **Spårnamn:** "melod", "vocal", "voice", "sång", "lead vox", "singer", "sing" ger +5000.

Poäng = textträff × (200 − överlapp) / 2 (0–10000) + namnbonus. Under 2500 utan namnträff → ingen melodi
(-1). Utan text föredras en enstämmig kanal. Användaren kan alltid välja själv i Spår-väljaren, som visar
noter, textträff och spårnamn per kanal; valet sparas i registret.

## 5. Begränsningar

- MIDI-filer max 1 MB, SF2 max 7 MB (PSRAM är 8 MB och delas med LVGL och ljudcache).
- Max 32 samtidiga röster; vid fler stjäls den äldsta rösten.
- Mono 22 050 Hz (samma som mixern).
- Ingen ljudbank ingår i repot (licens och storlek). Förslag: TimGM6mb.sf2 (GPL v2, ~5,7 MB).
- Endast okomprimerade SF2 (version 2, 16-bitars sampel). SF3 (Ogg-komprimerade) avvisas.
- Mindre GM-banker som testats på värddatorn (v0.13.1, test_sfsynth med Kents låtar, inga klipp, max 32 röster):
  Florestan_Basic_GM_GS.sf2 (3,1 MB), Yamaha_XG_Sound_Set.sf2 (3,7 MB), Super_Nintendo_Unofficial_update.sf2
  (1,8 MB) från archive.org "free-soundfonts-sf2-2019-04". Ljudkvaliteten på enheten är inte bedömd.

## 6. Steg 2 – mikrofon och poäng (v0.14.0)

Hårdvara: kortets MEMS-mikrofon sitter på ES8311:s ADC, data till ESP32 på I²S DIN (GPIO16).

1. **Ingång:** I²S i full duplex (TX finns redan), ES8311 ADC på, PGA-förstärkning inställbar. Kontrollera
   nivåer först (enligt hårdvarureferensen ska mikrofonen tas upp separat).
2. **Tonhöjd:** YIN eller normaliserad autokorrelation på block om ~1024 sampel (≈46 ms) vid 22 050 Hz,
   80–1000 Hz, med tystnadsgräns. Körs i en egen task på core 1 för att inte störa mixern.
3. **Jämförelse:** mot melodispårets aktuella not efter transponering. Oktavokänsligt (mod 12), tolerans
   ±50 cent för full poäng, glidande skala till ±150 cent. Timing ±150 ms.
4. **Poäng:** per rad (0–100) och låt; visas efter raden och i slutet; topplista per låt och spelare (spelarregistret
   finns redan).
5. **UI:** tunn tonhöjdslinje under texten (målnot vs. sjungen ton), poäng i hörnet.
6. **Webb (senare):** `/karaoke` med låtlista, uppladdning och live view av texten på TV:n.

Öppna frågor till HW-testet: CPU-last för synt + tonhöjd samtidigt, eko från högtalaren in i mikrofonen
(kan kräva att melodin är av eller att högtalarvolymen sänks under sång).
