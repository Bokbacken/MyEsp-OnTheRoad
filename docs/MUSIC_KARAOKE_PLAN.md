# Karaoke och musik – design och plan

Senast uppdaterad: 2026-10-28 (**v0.19.3, Fas D**). Steg 1 och dess historiska
utökningar beskrivs nedan; aktuellt arbete är responsiv Karaoke och
minnesdiagnostik, inte mikrofon/poäng. D-hostresultat och lokal byggbegränsning
redovisas i befintliga UI-inventeringen;
**alla fysiska D-tester återstår**. Steg 2:s äldre v0.14.0-mål är historiskt,
inte en leverans i denna version.

## 1. Mål

- Spela vanliga karaokefiler (`.mid`, `.kar`) från SD-kortet med riktiga instrumentljud (SoundFont).
- Visa texten på enheten, rad för rad, med markering av det som ska sjungas just nu.
- Kunna stänga av melodin och sjunga själv, byta tonart och tempo.
- Komma ihåg inställningar per låt (melodispår, tonart, tempo) i en läsbar textfil.
- Sedan: mikrofon, tonhöjdsdetektering och poäng.

Karaoke är en egen modul (`main/karaoke.*`, `main/ui_karaoke.*`) och ersätter inget av tärningsspelen eller äventyret.

### Karaokevolym och batteristatus

Volymreglaget använder befintliga `Karaoke::setVolume()` / `Audio::setExternalVolume()` och påverkar den
gemensamma externa karaoke-syntutgången (alla MIDI-kanaler tillsammans), inte en analog förstärkargain.
Det bevarar kanalernas relativa mix och ändrar inte GPIO1:ans förstärkar-enable, mute eller sound-on-policy.
Mixerns befintliga volymkurva går upp till 2×; SoundFont-renderingen har redan -14 dB headroom. Reglaget
och inställningspanelens stegreglage delar samma värde, som sparas till NVS efter släpp/debounce.

Batteriindikeringen kan bara visa spänning efter att både ADC-fabriks/eFuse-kalibrering finns och användaren
har sparat en engångsskala från en multimetermätning. GPIO8:s spänningsdelare är inte verifierad; utan
kalibrering visas okänt, aldrig en uppskattad procent eller laddsymbol. Se
[hårdvarureferensens batteriavsnitt](hardware_reference.md#10-battery-and-power) för mätsteg och fysiska tester.

Återstående hårdvarutest: på S3-kortet, kontrollera reglagets touchyta och separation från text/knappar i
90° och 270° Karaoke-läge; ändra volym både med reglaget och stegreglaget, verifiera omedelbart ljud,
sound-on/mute och webbljudets policy samt lyssna efter överstyrning. Upprepa batteriets multimetermätning
och kontrollera ADC/volymstabilitet med ljud aktivt och USB ansluten/frånkopplad. P4-layouten är hosttestad
som 800×480 logisk yta, inte fysiskt verifierad eller portad.

## 2. Arkitektur (v0.13.0, selektiv laddning och effekter v0.13.2)

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
- **`otr_synth`**: vendrad TinySoundFont (MIT). Upp till 192 samtidiga röster är allokerade; valet 32, 64, 96,
  128, 160 eller 192 är globalt och väljs i ljudbanksväljaren. Röststrukturerna tar ytterligare ungefär
  tiotals kB jämfört med 32 röster; högre max kan öka CPU-belastningen även när färre röster spelar, så välj lägsta
  tak som inte ger röststöld. Patchar för ESP32-S3: sampel lagras som `int16` (halva minnet –
  en ljudbank på 6 MB får plats i 8 MB PSRAM), lågpassfiltret i `float` (S3 har ingen double-FPU), egen
  fixpunktsloop (32.32) för monorendering, transponering ±12 (trumkanalen transponeras aldrig), röststöld (äldsta rösten återanvänds när den konfigurerade gränsen är nådd).
  -14 dB headroom i stället för -6 dB för täta arrangemang.
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
  och mixern rör inte synten. Filerna kontrolleras med `Sf::probe` (RIFF/sfbk, `ifil` < 3; storleksgränsen 7 MB togs bort i v0.13.2).
- **v0.13.2 – selektiv laddning:** `Sf::Synth::open()` läser bara bankens struktur (hydra) och sampeldatans
  position. När en låt öppnas kör fonttasken `prepare(låt)`: låtens händelser körs genom TinySoundFonts kanallogik
  (bank select, program, trumkanal) och för varje förinställning noteras tangenter (melodiska ±12 så att
  transponering aldrig kräver omladdning) och hastigheter. Bara de förinställningarna får regioner, och bara de
  sampelintervall zonerna kan spela läses från SD (intervall närmare än 4 kB slås ihop) till en PSRAM-buffert.
  Budget 6 MB per låt (eller mindre om största lediga PSRAM-blocket minus 512 kB är mindre). Över budget:
  1) bara hastighetslagret som innehåller 100 behålls och svarar på alla hastigheter, 2) en zon per tangent
  (stereopar och staplade lager tas bort; kvarvarande zon +6 dB för stereopar, +3 dB annars). Räcker inte det:
  felet `SONG_TOO_BIG` ("Låten behöver för många sampel") och banken förblir öppen. Banker större än PSRAM fungerar
  alltså. Idén kommer från copych/ESP32_SF2_Sampler_Synthesizer (MIT); koden är egen.
- **v0.13.3 – halvering:** sampel med minst 1,8 × utfrekvensen (44,1/48 kHz) decimeras 2:1 med ett halvbandsfilter
  (47 tappar, Kaiser β 7, Q15; −6 dB vid 11 kHz, −53 dB vid 13 kHz) medan de läses från SD. Överlappande intervall
  halveras bara om alla zoner tillåter det; jämna loopar halveras, udda bara om de är ≥ 1 500 sampel (annars ändras
  tonhöjden). `setHalving(false)` stänger av. Sparar 0–1,4 MB per låt med stora banker. Ersatt i v0.13.4.
- **v0.13.4 – omsampling:** allt med `sample_rate` > utfrekvensen omsamplas till utfrekvensen när det läses från SD
  (polyfas windowed-sinc, Kaiser 75 dB, brytfrekvens 0,45 × utfrekvensen, 64 faser, float). Intervall grupperas i
  block per källfrekvens; loopar avrundas till heltal nya sampel och zonens `sample_rate` (float) kompenseras exakt,
  så tonhöjden stämmer även för korta udda loopar och delade intervall. `setResampling(false)` stänger av.
- **v0.13.4 – exakta tangenter:** ±12-marginalen är borta; skanningen tar exakt tangent + transponering. Ändras
  transponeringen med en selektivt laddad bank körs `prepare()` om (låten pausas och fortsätter). Får den nya
  tonarten inte plats behålls den gamla och ett meddelande visas.
- **v0.13.4 – budget och diagnostik:** budgeten är största lediga PSRAM-block − 512 kB (inte längre fast 6 MB);
  spelets ljudklippscache frigörs först. Var 5:e sekund loggas renderbelastning (medel/max), underruns och röster.
  NVS `kara_voices` (32/64/96/128/160/192, standard 96) begränsar antalet röster.
- **v0.23 – rösttak per kort:** S3-profilen förallokerar högst 96 röster och visar valen 32/64/96; P4-profilen
  förallokerar högst 192 och visar valen 32–192. Standardvärdet är 96; 128+ på P4 är opt-in.
- **Mätning på P4:** FluidR3 (151 MB), Alan Parsons Project “Time”, 96 röster: 0 underruns, cirka 45 % medellast,
  97 % maxlast och långa perioder med 96/96 aktiva röster. En separat helsångsmätning med 192 som tak hade också
  0 underruns, men 5-sekunders medellast varierade ungefär 33–78 %, maxlast nådde 134 % och topparna var 179/192.
  Last över 100 % betyder att en mixerperiod tog längre än sin tidsbudget; att mätningen inte hade underruns är
  inte en garanti mot framtida bortfall. Utdata är mono 22 050 Hz. Mindre eller förhandsomsamplade SoundFonts kan
  minska renderarbetet och eventuellt tillåta högre rösttak; behöver mätas av ägaren på hårdvaran.
- **v0.13.5 – webbkaraoke:** mixerns färdiga monoblock går via `Audio::setTap()` till en låsfri ring
  (`main/kara_stream`, 32 kB i PSRAM) och skickas av `main/kara_web` som binärframes (512 sampel int16, 22 050 Hz)
  på WebSocket `/ws/karaoke`. Texten (`newrow`/`lyric`/`clear`, JSON) läggs i strömmen vid exakt rätt sampel av
  `LyricTracker` i mixertasken, så sidan `/karaoke` visar stavelsen när motsvarande ljud spelas (0,3 s förbuffring).
  Är minst en webbläsare ansluten och en låt öppen tystas högtalaren (`Audio::setWebStream`: ES8311 DAC-volym 0,
  förstärkaren av). Max 3 lyssnare. NVS `kara_webstream=0` stänger av endpointen. Bandbredd ~44 kB/s per lyssnare.
- **v0.13.6 – nedräkningspunkter:** pauser ≥ 3 s mellan rader (och långa intron) visar N = avrundade sekunder
  punkter (max 8), vita från pausens början; en per sekund byter till sjungen färg så att den sista tänds när nästa
  rad börjar. Pausen börjar efter sista stavelsen + uppskattad hålltid (radens medelavstånd, max 1 s).
  `KaraStream::countdownAt()` (syntens position) driver både LVGL-raden och webbframen
  `{"type":"countdown","dots":N,"elapsed":k}`, som läggs i PCM-strömmen på rätt sampel.
- **v0.13.7 – låtbibliotek:** rekursiv skanning av `/otr/karaoke` (max 2 000 låtar) med cache i `songs.idx`
  (binärt; header med skanningstid, SD-fritt utrymme och rotmappens mtime – skiljer de sig skannas kortet om).
  `main/kara_index` håller biblioteket i PSRAM (sorterat i svensk ordning, mappar, sök, A–Ö). Enheten bläddrar i
  mappar med bakåtknapp, sökfält och A–Ö-kolumn; webbsidan `/karaoke/songs` (`GET /api/karaoke/songs`,
  `POST /api/karaoke/open`, `POST /api/karaoke/rescan`) startar låtar från mobilen. Ingen automatisk sortering.
- **v0.13.2 – reverb och chorus:** `otr_fx` (Freeverb, public domain: 8 kamfilter + 4 allpass, längder skalade
  från 44,1 till 22,05 kHz; chorus 15 ms ±4 ms, 0,7 Hz triangel, ~4 kB). Mono-sändeffekter: sändnivån per kanal
  följer CC91 (reverb, GM-standard 40) och CC93 (chorus, 0). Renderingen sker i block om 256 sampel; röster
  grupperas efter kanalernas sändnivåer så att varje grupp renderas en gång. Inställningar i NVS (`kara_rev`,
  `kara_revl`, `kara_cho`, `kara_chol`; på, 50 %). Inga reglage i UI:t ännu.
- **v0.13.1 – ljud av:** går man in i karaoke med ljudet avstängt frågar en dialog om ljudet ska slås på.

## 2.5 Fas D – responsiv Karaoke och minneskartläggning (v0.19.3)

Fortsättning på UI-migreringens B/C, utan att ändra deras historia eller lägga
en parallell plan. Layout och den fullständiga manuella acceptanslistan finns i
[UI-inventeringens D-avsnitt](ui-migration-inventory.md#fas-d--karaoke-layout-och-minnesdiagnostik).
Det avsnittet ersätter Fas A:s fasta Karaoke-layoutantaganden.

### Beslut och avgränsning

- Geometri från live `UiMetrics` **efter landscape**, logiskt **480×320 och
  800×480**, med begränsade marginaler/mellanrum/kontrollhöjder. 800×480 är en
  logisk layoutstorlek, inte verifierad fysisk P4-upplösning eller panelportning.
- Begränsad header (`buttonH + gap`) med enradig DOTS-titel/metadata;
  sheetarnas titlar/hints radbryter med innehållsstyrd höjd. Fullbred scrollande
  text i återstående yta efter header/metadata, seek och kontrollrad.
  Aktiv spangroup radbryter sjungen/återstående text med föregående/två nästa
  kontextrader. Nedräkningen ingår i textens flexflöde och döljs när den saknas,
  utan separat fast reservation. Seekrad och **en** primärrad:
  omstart / −5 s / spela-paus / +5 s / stopp på båda måtten.
  Insets är 8–12 px och kontrollhöjd 44–52 px. Produktionshosttesterna mäter
  viewport **460×158** / **776×292** och kontroller **88×44** / **150×52**
  vid respektive logisk storlek; ingen fallback behövs där, ±5-etiketterna
  ryms med riktiga fonter (fysisk läsbarhet ej verifierad). A–Ö-kolumnen scrollar
  med minst 44×42 px träffyta.
- Headerns hamburger samlar Tempo/tonart med befintlig volym, melodi på/av och
  melodispårsval med synligt aktuellt tillstånd. Inget bankval i spelaren;
  listans globala bankval och registrets per-låt-`sf2=`-företräde består.
  Sheets hålls inom skärmen med scrollande innehåll och fast stängkontroll.
- Bevara async-/timer-/fadeägarskap, extern audiokälla, banktask/lås och
  laddningsspärrar. Frigör sheetens lokala fontlistekapacitet vid deletion,
  inte medan köade valcallbacks använder den. Inga nya fullskärmsbuffertar
  eller synth-/protokoll-/driver-/profiländringar ingår.
- Lokal korrigering i delad `main/my_textinput.cpp`, ingen bred omskrivning:
  `lv_obj_align(q->kb, LV_ALIGN_TOP_LEFT, 0, kbY)` ersätter `lv_obj_set_pos`
  så befintlig bottenankring inte skjuter tangentbordet under displayen.
  Det stärkta, godkända Karaokehosttestet täcker verkliga CANCEL/READY-callbacks,
  alla tangentbordsbarns bounds, sidbyte fram/bak, sista A–Ö-knappen och
  minus/plus på samtliga steppers.

### Minneskarta – ägare och mätgränser, inte uppmätta besparingar

| Del | Befintlig allokering/livstid och D-gräns |
|---|---|
| LVGL-låtlista och sheets (`main/ui_karaoke.cpp`) | Sidgränsen **40** begränsar LVGL-låtrader, inte hela biblioteket/resultatvektorn. Labels, spans och styles kostar fortfarande heap. Sheetens skannade `s_fonts` släpper innehåll och kapacitet vid sheet-deletion; UI-vyn `s_entries` släpper kapacitet när ägande listskärm raderas. Biblioteksindexet frigörs inte av detta. |
| Biblioteksindex (`main/kara_index.*`) | Låtrecords och strängpool, högst 2 000 låtar, cache `songs.idx`; bibliotekets egen livstid följer inte automatiskt skärmradering. Paging av UI är inte paging av indexminnet. |
| Låt och lyrics (`main/karaoke.cpp`, `otr_midi`) | Rå MIDI-bytevektor frigörs direkt efter lyckad parsning/CRC före synthförberedelse. Parsade MIDI-händelser, stavelser och rader finns kvar för hela öppna låten. UI skapar endast **fyra synliga logiska lyrikrader** (föregående, aktiv, två nästa); wrapping kan ge fler visuella rader, inte en kopia av hela låten i LVGL. |
| SF2-val och sampel (`components/otr_synth/otr_synth.cpp`) | Bankstruktur/hydra, valda presets/regioner, zon-/intervallurval och selektiva/resamplade sampelbuffertar har synth-/fonttaskens livstid. Budgeten beror på största fria PSRAM-block minus reserv, inte bara totalt ledigt minne. En UI-fontlista är inte synthbanken; `close()` och `unloadFont()` är olika operationer. Ingen ändring av urval, budget eller sampelalgoritm i D. |
| Audio/webbström (`main/kara_stream.*`, `main/kara_web.cpp`) | PCM-ring och webbström hanteras separat från UI-skärmen; befintlig audio-/streamlivstid och protokoll behålls. Färre synliga labels säger inget om dessa buffertar. |
| Ritning/rotation (`main/board_display.cpp`, `components/my_lvgl/my_lvgl.cpp`) | Två partiella RGB565-ritbuffertar och en återanvänd rotationsbuffer, inte en ny Karaoke-fullskärmsbuffer. Rotationscachen kan ligga kvar efter portraitretur; skilj kvarhållen kapacitet från läcka efter upprepade besök. D ändrar inte drivrutinens bufferägarskap. |

`ESP_LOGI`/`heap_caps` loggar fri intern **8-bitars** heap, fri PSRAM och största
fria PSRAM-block. UI-punkter under `kara-ui`: `enter`, `landscape`, `list`, `player`,
`portrait`, `screen-cleanup` (async efter LVGL-deletion). Under `karaoke`:
`bank-task-start`, `bank-task-end`, `bank-unloaded`, `song-open-start`,
`song-open-end`, `song-closed`. Taskstacken frigörs först **efter** `bank-task-end`;
bibliotekets/strömmens/rotationsbuffertens livstid ändras inte. Jämför samma
låt/bank/upplösning efter färdig laddning/async/fade.
Skärmcleanup bevisar inte att synth/stream/rotationscache
är frigjord. Inga nya bytebesparingar, minnesmarginaler eller prestandavinster
hävdas före mätning. De äldre hostmätningarna nedan är historiska synthresultat,
inte D-resultat.

**Hostverifiering:** Karaoke- och Äventyrs spel-layouttester **PASS**.
MIDI **100/0**, index **130/0**, stream **181/0**, språk **1 958/0**
(482 strängar/två språk). Befintliga Yatzy-/Äventyr setup-/min-dialogtester
**FAIL** i oförändrade berörda källor. Exakta kommandon, fel och testgränser
finns i [UI-inventeringens resultattabell](ui-migration-inventory.md#körda-d-kontroller-och-verifieringsgräns).
Karaokehosttestet använder produktions-UI, verklig TextInput/MIDI/index,
LVGL 9.6.0 med 256 KiB heap och stubbad engine/hårdvara.
MIDI-testets befintliga `testParseKar` nollställer/frigör nu originalbytes direkt
efter parsning före datakontrollerna: ägarskapsregressionen passerar inom **100/0**.
`idf.py build` kunde inte köras lokalt (ESP-IDF saknas); CI **40664764225**
för **a8f91ae** **FAIL** vid `ui_karaoke.cpp:41/43/52`, där `std::clamp` inte
kan deducera samma typ för Xtensas `int32_t` (`long`) och `int`-literaler.
Alla tre anrop är korrigerade till `std::clamp<int32_t>` i **18586b0**.
[Kodcommitens CI 40665654561](https://github.com/Bokbacken/MyEsp-OnTheRoad/actions/runs/40665654561)
kräver godkännande (`action_required`); inget godkänt firmwarebygge påstås.
Slutlig hostomkörning av Karaoke/MIDI/index/stream/språk passerar.
CodeQL: **0 larm** i C++/Python; hemlighetsskanning: inga fynd.

**Fysisk verifiering återstår:** hela D-checklistan på båda logiska storlekarna,
inklusive rotation/retur till meny/Äventyr/Yatzy, lång lista/sök/bank, alla
transport-/hamburgervägar, lång text/synk, ljud-av Ja/Nej och upprepad
inträdes-/bank-/låtcykling med heap-/audiologg. Inget fysiskt godkännande eller
uppmätt bytebesparing påstås av denna dokumentationsuppdatering.

## 2.6 SoundFont-minne och P4-ljud

`Sf::Synth::open()` läser bankens struktur och sampelpositioner från SD; den
laddar inte hela `.sf2`-filen till PSRAM. `prepare()` läser bara sampelintervall
som låten använder. Det är därför filstorleken på SD inte är samma sak som
PSRAM-användningen. Den selektiva sampelbudgeten är största lediga PSRAM-blocket
minus profilens reserv; överskridande ger först reducerade velocity-lager och
därefter tunnare zonurval.

| Profil | PSRAM | Reserv | Helt SF2 i minnet | Selektiv Karaoke |
|---|---:|---:|---|---|
| ES3C35P / S3 | 8 MB | 512 kB | Standardtak 7 MB, alltid begränsat av största lediga block minus reserv | Resampling på, som tidigare; native-rate-sampel kan därför kosta ungefär dubbelt så mycket som sampel omsamplade från 44.1 kHz till 22.05 kHz. |
| P4_43 | 32 MB | 2 MB | Profilgräns 32 MB, dynamiskt begränsad av faktiskt ledigt block minus reserv | Resampling av; sampel hålls i källfrekvens. Effektiv sampelbudget är den lediga PSRAM som återstår efter reserv och andra allokeringar. |

Detta är budgettak, inte löfte om att en viss fil ryms: P4 har andra stora
PSRAM-användare, och en enda stor sammanhängande fri block kan vara mindre än
totalt ledigt minne. Selektiv laddning kan öppna en SF2-fil som är större än
profilens helt-i-minnet-tak eftersom bara använda sampelintervall laddas; den
praktiska gränsen blir då sampelbudgeten och SD-läsningen.

Resampling minskar minnet när källsampel ligger över utgångsfrekvensen och
filtrerar bort innehåll som inte kan representeras vid 22.05 kHz. Det kan ge en
liten kvalitetsförlust men har verifierade loop-/tonhöjdstester i
`test_sfsynth.cpp`. P4 stänger av resampling för att undvika denna förlust när
minnesbudgeten tillåter det; om sampel inte ryms väljer syntens befintliga
reducerade lager i stället. P4 behåller för närvarande 22.05 kHz utgång:
44.1 kHz skulle fördubbla antalet renderade sampel och webb-/I²S-datamängden,
kräva ändrad ES8311-klockning och kräver CPU-/ljudmätning på hårdvara innan
rekommendation.

### CPU-gräns: för krävande SF2/röstantal

Minnesbudgeten avgör om en bank kan laddas, inte om den hinner renderas.
Stora banker med många lager/omsamplade zoner och högt röstantal kan kräva
mer CPU än en mixerblocktid. Ägartest på S3 (v0.23.18, `WeedsGM3.sf2`
~55 MB, 96 röster): 75–92 samtidiga röster gav renderlast 87–123 % i snitt,
hackigt ljud, 47 sena block och task-watchdog-varningar för `audio` på CPU 0
(ingen omstart). Sådana banker stöds inte på S3; välj en mindre SF2 (t.ex.
TimGM6mb) eller färre röster.

Från v0.23.21 stoppar karaoken uppspelningen när en 5-sekundersperiod har
genomsnittlig renderlast ≥ 100 % eller minst 5 sena mixerblock
(`main/kara_overload.h`). Spelaren pausas, toner släpps, loggen skriver
`overload: stopped (...)` och enhetens karaokevy visar att SF2 eller
röstantal är för krävande för hårdvaran. Webbspelaren visar ännu inget eget
meddelande. Kortare toppar än en period och enstaka sena block stoppar inte.
Gränsen gäller båda korten; P4-tröskeln är inte separat mätt. Ägaren
bekräftade på S3 (v0.23.21) att varningen visades när watchdogen larmade.

### Webbkaraoke i mono/stereo

Nuvarande tap, ringbuffer, `Sf::Synth::render()` och browserprotokoll är mono.
22.05 kHz PCM16 mono är 44.1 kB/s per klient. 22.05 kHz stereo skulle bli
88.2 kB/s (705.6 kbit/s) per klient, cirka 2.12 Mbit/s för tre webbläsare före
WebSocket/IP-overhead. 44.1 kHz stereo blir 176.4 kB/s (1.41 Mbit/s) per klient.
Bandbredden är sannolikt hanterbar på P4:s nät, men render/mix/tap/ringbuffer
och browseravkodning måste alla bli stereo; syntens nuvarande mono-utgång tappar
panorering innan mixen. Inga CPU- eller ljudmätningar för P4-stereo finns.

**Ägarbeslut v0.23:** behåll mono. En stereoleverans kräver först stereo-render
med SoundFont-pan, stereomix och tap, formatidentifiering/bakåtkompatibilitet i
WebSocket-protokollet samt browserstöd. Börja i så fall med 22.05 kHz stereo och
mät P4-renderbelastning och faktisk flerklientbandbredd före 44.1 kHz.

## 3. Registret `karaoke.reg`

Extern sångtext för MIDI utan inbäddad text: se
[`.lyr`-filer och LRC-tidssynk](KARAOKE_LYRICS.md). Samma filnamn med
`.lyr` i låtens mapp visas scrollbar utan tider eller tidssynkad med LRC.

Textfil på SD: **`/otr/karaoke/karaoke.reg`** (firmware-sökväg
`/sdcard/otr/karaoke/karaoke.reg`). I filhanteraren väljer du **SD-kort** och
öppnar `otr` → `karaoke`. Filen skapas först när en låtinställning ändras,
inte bara när en MIDI-fil laddas upp eller öppnas, och kan redigeras för hand:

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
  En misslyckad skrivning loggas och den tidigare registerfilen bevaras.

Webbkaraoken använder hela sidbredden. Aktuell rad har större responsiv text
som anpassas till högst två rader ned till 24 px; extremt långa rader får
fortsätta radbrytas i stället för att bli oläsligt små.

## 4. Melodispårsdetektering

För varje kanal med noter (utom trummor):

1. **Textträff:** andel stavelser som har en not-på i kanalen inom ±120 ms.
2. **Enstämmighet:** andel noter som överlappar en annan not i samma kanal (`overlapPct`); ackord sänker poängen.
3. **Spårnamn:** "melod", "vocal", "voice", "sång", "lead vox", "singer", "sing" ger +5000.

Poäng = textträff × (200 − överlapp) / 2 (0–10000) + namnbonus. Under 2500 utan namnträff → ingen melodi
(-1). Utan text föredras en enstämmig kanal. Användaren kan alltid välja själv i Spår-väljaren, som visar
noter, textträff och spårnamn per kanal; valet sparas i registret.

## 5. Begränsningar

- MIDI-filer max 1 MB. Ljudbanken kan vara hur stor som helst (v0.13.2). En låt får använda största lediga
  PSRAM-blocket minus 512 kB (v0.13.4; tidigare fast 6 MB). PSRAM är 8 MB och delas med LVGL.
- Mätt på värddatorn (v0.13.2, Kents 5 låtar): Florestan 0,23–0,63 MB per låt, Super Nintendo 0,08–0,29 MB,
  Yamaha XG 0,23–0,76 MB, GeneralUser-GS (30,8 MB) 3,5–4,8 MB; med GeneralUser får två låtar inte plats
  (Ainbusk 6,6 MB, Alan Parsons 8,3 MB – bankens dubbla stråklager använder samma sampel, så tunnning hjälper
  knappt). Selektiv rendering är bit-identisk med hel laddning.
- Med halvering (v0.13.3): eawpats 1,28–3,30 MB, Unison 1,87–4,18 MB, Masterpiece 1,53–5,51 MB (Ainbusk nu med alla
  hastighetslager), ChaosBank 0,78–1,93 MB, Jnsgm2 1,36–5,44 MB (Alan Parsons får inte plats, 6,91 MB),
  GeneralUser-GS 3,10–4,09 MB för tre låtar; Ainbusk (6,03 MB) och Alan Parsons (7,60 MB) får fortfarande inte plats
  eftersom deras sampel ligger på 28–32 kHz.
- Med omsampling och exakta tangenter (v0.13.4, värddatorn, budget 8 MB):

  | Bank + låt | v0.13.3 | utan omsampling (exakta tangenter) | v0.13.4 | sparat av omsampling |
  |---|---|---|---|---|
  | GeneralUser-GS + Ainbusk | 6,03 MB (för stort) | 4,53 MB | 3,10 MB | 1,42 MB |
  | GeneralUser-GS + Alan Parsons Time | 7,60 MB (för stort) | 6,71 MB | 4,64 MB | 2,03 MB |
  | Jnsgm2 + Ainbusk | – | 3,86 MB | 2,92 MB | 0,94 MB |
  | Jnsgm2 + Alan Parsons Time | 6,91 MB (för stort) | 5,50 MB | 3,85 MB | 1,65 MB |

  Ingen av låtarna behöver längre tunnas. Rel. skillnad mot ej omsamplat 0,011–0,067 (första 60 s, torrt).
- Max 192 samtidiga röster (NVS `kara_voices`, val 32–192); vid fler stjäls den äldsta rösten. Mät renderlast
  vid större gränser och använd ett lägre val om CPU-belastningen närmar sig blockgränsen.
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
