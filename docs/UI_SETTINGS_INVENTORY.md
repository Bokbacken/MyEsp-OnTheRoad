# Inställningsinventering – kodverifierad (`AppConfig`)

Status: PR A, steg 1, 2026-11-07. Endast dokumentation; ingen firmware ändrad.
Bygger på `docs/ESP_UI_SETTINGS_PLAN.md` och är verifierad mot `main` efter
v0.25.19 (`main/app_config.h`, `main/app_config.cpp`, `main/ui_settings.cpp`,
`main/app_web.cpp`, `main/ui_karaoke.cpp`, `main/ui_adventure.cpp`).

Metod: för varje fält i `struct AppConfig` har skrivningar och läsningar sökts
i `main/` och `main/web/`. Kolumnerna "ESP-editor" och "Webb" visar var värdet
faktiskt kan ändras idag. Det som inte har kontrollerats rad för rad är
markerat **(ej verifierat)**.

## Hur ESP-inställningarna sparas idag

- Inställningssidan (`ui_settings.cpp`) redigerar en kopia, `s_edit`, och
  skriver till `Config::get()` först i `leave(save=true)`. Misslyckas
  `Config::save()` återställs föregående värden och en felruta visas.
- **Live-förhandsvisning** (återställs vid Avbryt via `applyHardware()`):
  ljusstyrka, RGB-LED, rotation, ljudpå/master/SFX/musik.
- **Vid Spara:** språk (`Lang::setLang`), tidszon (`MyTime::setTz`) samt
  textstorlek/skärmzoom (`applyTextSize()` följt av ombyggd vy).
- Genvägar som skriver direkt och sparar utan sidan: `toggleSound()` (statusrad),
  `togglePowerSaving()` (batteriikon), Äventyrets mute och Assist/Pro,
  karaokespelarens mastervolym, standard-SoundFont och röstgräns.
- Webben (`app_web.cpp`) skriver in i samma `AppConfig` med egen validering.

## Inventering per fält

Förkortningar: **G** = global/enhet, **SK** = spelkategori, **S** = specifikt
spel. Aktivering: **Nu** = direkt, **Spara** = vid Spara på sidan, **Nästa** =
gäller nästa omgång/start **(ej verifierat)**, **Omstart** = kräver omstart.

| Fält (NVS-nyckel) | ESP-editor | Webb | Aktivering | Ägare/kategori (förslag) | Yta (förslag) |
|---|---|---|---|---|---|
| `hostname` (`host`) | Ingen (visas) | Ja | Ej verifierat | G > Enhet | Admin |
| `adminPin` (`admin_pin`) | Ingen på sidan; används som spärr | Ja | Nu | G > Administration | Admin; aldrig reglage |
| `lang` (`lang`) | Inställningar > Språk | Ja | Spara | G > Språk | Inställningar; ev. nivå 2 |
| `brightness` (`bl`) | Inställningar > Skärm | Ja | Nu (live) | G > Skärm | Snabbpanel nivå 1 |
| `screenZoom` (`screen_zoom`) | Inställningar > Skärm | Ja | Spara (ombyggd vy) | G > Skärm/tillgänglighet | Inställningar; nivå 2 |
| `textSize` (`text_size`) | Inställningar > Skärm | Ja | Spara (ombyggd vy) | G > Skärm/tillgänglighet | Inställningar; nivå 2 |
| `rotation` (`rotation`) | Inställningar > Skärm | Ja | Nu (live) | G > Skärm | Inställningar |
| `screenOffSeconds` (`screen_off`) | Inställningar > Ström | Ja | Ej verifierat | G > Ström | Nivå 2 |
| `ecoSleep` (`eco_sleep`) | Inställningar > Ström; batteriikon långtryck | Ja | Nu vid genväg, annars Spara | G > Ström | Snabbpanel nivå 1 |
| `powerLog` (`power_log`) | Inställningar > Admin | Ja | Spara | G > Ström/diagnostik | Admin |
| `menuAnimations` (`menu_anim`) | Inställningar > Admin | Ja | Spara | G > Skärm | Inställningar |
| `ledEnabled`, `ledBrightness` (`led_on`, `led_bri`) | Inställningar > Skärm (endast kort med LED) | Ja | Nu (live) | G > Skärm | Inställningar; döljs utan LED |
| `soundOn` (`snd_on`) | Inställningar > Ljud; statusrad kort tryck; Äventyr mute; Karaoke frågar vid start | Ja | Nu | G > Ljud (master mute) | Snabbpanel nivå 1 |
| `masterVol` (`master_vol`) | Inställningar > Ljud; Karaoke-spelare | Ja | Nu | G > Ljud | Snabbpanel nivå 1 |
| `sfxVol` (`sfx_vol`) | Inställningar > Ljud | Ja | Nu (live) | G > Ljud | Nivå 1/2 |
| `musicOn`, `musicVol` (`mus_on`, `mus_vol`) | Inställningar > Ljud | Ja | Nu (live) | G > Ljud | Nivå 1/2 |
| `defaultVariant` (`variant`) | Inställningar > Spel | Ja | Nästa spel | SK Tärning (standard) | Spelkategori |
| `resultProtocolFirst` (`result_protocol`) | Inställningar > Spel | Ja | Ej verifierat | SK Tärning | Spelkategori |
| `rollAnimMs` (`roll_ms`) | Inställningar > Spel | Ja | Ej verifierat | SK Tärning | Spelkategori; nivå 2 i spel |
| `maxiSavedRolls` (`maxi_save`) | Inställningar > Spel | Ja | Nästa spel (regel) | SK Tärning | Spelkategori |
| `throwMode` (`throw_mode`) | Inställningar > Spel | Ja | Ej verifierat | SK Tärning | Spelkategori; nivå 2 i spel |
| `advMode` (`adv_mode`) | **Endast** Äventyrsmenyn | **Nej** | Nu | SK Äventyr (kontroller/regler) | Äventyr; saknas i Inställningar |
| `joyZones`, `joySlowMs`, `joyFastMs`, `joyHoldMs`, `joyDiag` (`joy_*`) | Inställningar > Joystick | Ja | Spara | SK Äventyr | Spelkategori; nivå 2 i äventyr |
| `sudokuDeadZone` (`sud_dz`) | Inställningar > Sudoku | **Nej** | Spara | SK Sudoku > Touch | Spelkategori; läses även av touchtestet |
| `sudokuHelpHints` (`sud_help`) | Ingen | Nej | – | Legacy | Dölj; behåll NVS-nyckel |
| `karaFont` (`kara_sf2`) | Karaoke > låtpanel (`setDefaultFont`) | Nej | Nästa låt | SK Karaoke | Karaoke |
| `karaVoices` (`kara_voices`) | Karaoke > låtpanel (`setVoiceLimit`) | Nej | Ej verifierat | SK Karaoke | Karaoke |
| `karaFlip` (`kara_flip`) | **Ingen** (endast läst) | **Nej** | Nästa karaokestart | SK Karaoke | Saknar editor |
| `karaRev`, `karaRevLvl`, `karaCho`, `karaChoLvl` | **Ingen**: `Karaoke::setEffects` har inga anropare utanför `karaoke.cpp` | Nej | – | SK Karaoke > Ljud | Saknar editor |
| `karaWebStream` (`kara_webstream`) | Ingen, avsiktligt | Nej | Omstart (styr om `/ws/karaoke` registreras vid uppstart) | G > Nätverk/avancerat | Admin |
| `tz` (`tz`) | Inställningar > Klocka | Ja | Spara | G > Tid | Inställningar |
| `batteryScalePermille` (`bat_scale`) | Inställningar > Batteri (döljs för PMIC) | Ja | Spara | G > Ström/kalibrering | Admin |
| `batteryFullMv` (`bat_full`) | **Ingen** | Ja | Ej verifierat | G > Ström/kalibrering | Admin |

## Avvikelser och nya fynd jämfört med planen

1. **Tre editor-luckor:** `karaFlip`, karaokens effekter (`karaRev/Cho` och
   nivåer) och `hostname`/`adminPin` på enheten har ingen ESP-editor.
   Karaokefälten är lagrade men kan inte ändras; planen sa "ej bekräftat" och
   det är nu bekräftat.
2. **Ensidiga ytor:** `advMode`, `sudokuDeadZone` och alla karaoke-standardvärden
   kan bara ändras på enheten, medan resten av spelinställningarna även finns på
   webben. Webb-API:t är alltså inte en komplett spegel av ESP-UI:t.
3. **Dubbel yta som redan finns:** `soundOn` ändras på fyra ställen
   (statusrad, Inställningar, Äventyr, karaoke). Alla skriver samma fält, så
   det är genvägar, inte kopior; logiken är ändå duplicerad (PR B ska samla den).
4. **Karaokes standardvärden ligger i låtpanelen**, inte i Inställningar.
   Menyn "Spelkategori > Karaoke" finns inte som sida idag.
5. **Regelstyrda fält med oklar aktivering:** för `resultProtocolFirst`,
   `rollAnimMs`, `throwMode`, `screenOffSeconds` och `karaVoices` är det inte
   kontrollerat om en pågående session påverkas. Det måste granskas innan de
   visas i aktivt spel (PR D).
6. **`sudokuHelpHints`** används inte längre men sparas fortfarande.

## Karaoke: låtnivå (`karaoke.reg`)

Verifierat mot `components/otr_midi/include/otr_midi.h` (`RegEntry`,
`Registry`) och `main/karaoke.cpp` (`currentSettings`, `songSettingsDirty`,
`saveSongSettings`, `discardSongSettings`).

**Format och identitet.** En textrad per låt, nyckel är CRC32 + storlek på
filinnehållet, så namn- eller mappbyte behåller inställningarna:
`<crc> <size> <melody> <transpose> <tempo> [sf2=<font>] [mute=1] [volume=0..200] <namn>`.
Valfria värden skrivs bara när de avviker från standard, och gamla rader går
att läsa. Filen skrivs atomärt med `.tmp`/`.bak`. Taket är 256 KiB för hela
filen medan biblioteket får ha 2000 låtar, så extra tokens per rad måste hållas
korta (cirka 128 byte per rad i snitt).

**Vad som sparas per låt idag** (dirty-spårning, Spara och Avbryt täcker alla):
vald melodikanal (`melody`), transponering, tempo, melodimute (`mute`),
låtvolym (`volume`) och SoundFont (`sf2`).

**Vad som är globalt idag** (`AppConfig`/NVS): reverb och chorus med nivåer,
röstgräns, standard-SoundFont och mastervolym. Reverb/chorus gäller hela
syntetisatorn och ingår inte i `RegEntry`.

**Webb.** `/api/karaoke/song` (GET/POST) kan bara läsa och sätta låtens
SoundFont. Övriga låtinställningar kan inte ändras på webben.

**Texter.** En `.lyr`-fil bredvid låten används bara när MIDI-filen saknar
egna textrader. Är `.lyr` tidsatt ersätter den MIDI:ns rader, annars visas den
som ren text. Det finns ingen editor, vare sig på enheten eller på webben.

### Gap mot ägarens önskemål (2026-11-07)

| Önskemål | Status idag | Förslag till lagring |
|---|---|---|
| SoundFont per låt | Finns (`sf2=`) | – |
| MIDI-/låtvolym | Finns (`volume=`) | – |
| Sångspår på/av | Finns (`mute=`) | – |
| Vilken kanal är sång | Finns (`melody=`) | – |
| Tempo och transponering | Finns | – |
| Mastervolym | Global, sparas inte per låt | Ska förbli global |
| Melodispårvolym | Saknas | Nytt token, multiplikator på kanalens uttryck (planen, PR B) |
| Instrumentbyte, flera ommappningar | Saknas | Nytt token med en lista `källprogram->ersättning`; se designfrågan nedan |
| Instrumentbyte på andra kanaler | Saknas | Samma token, nyckel per kanal |
| Reverb/chorus per låt | Bara globalt; ingen editor (`setEffects` saknar anropare) | Nya tokens för på/av och nivå per låt, med global standard |
| Mät reverb/chorus på S3/P4 per SoundFont | Inte gjort | Eget mätarbete före beslut |
| Texteditor/manuell synk mot MIDI | Saknas | Troligen webb; skriver tidsatt `.lyr` eller utökat register |

### Beslut (ägaren, 2026-11-07)

1. **Instrumentmappning:** nyckel per kanal + källprogram. Program visas
   1-baserat (1-128) för användaren, 0-127 internt och i filer. Numreringen
   mot etablerade MIDI-editorer är ej verifierad.
2. **Reverb/chorus:** börjar globalt. Mer CPU-/RAM-loggning under karaoke läggs
   till först. En global av/på-inställning beslutas efter mätning.
3. **Textsynk:** första steget skriver tidsstämplar till `.lyr`.
4. **Registret:** ska inte ligga i internt RAM, och inställningar per låt bör
   flyttas till en fil per låt (lätt att flytta till annan enhet).

Underlag för punkt 4 (verifierat): `s_reg` är en `Midi::Registry` som läses in
helt vid start. `CONFIG_SPIRAM_USE_MALLOC=y` med
`SPIRAM_MALLOC_ALWAYSINTERNAL=4096` gör att stora allokeringar normalt hamnar i
PSRAM, men enskilda små strängar i `std::vector`/`std::string` hamnar i internt
RAM (ej mätt). Varje Spara gör dessutom en kopia (`Registry next = s_reg`), så
toppen är ungefär dubbla storleken. Registret rymmer även spellistor, favoriter
och sökvägstips som webben läser, så de kan inte bara flyttas till
låtfilerna. Det som flyttas är `RegEntry` (inställningarna). Uppslagning sker
redan bara vid låtöppning och ett filuppslag kostar ungefär samma som listan.
Öppna frågor: filnamn (`<crc>_<size>.set` i en egen mapp), migrering av
befintliga rader, och om spellistor/favoriter får behålla en central fil.

## Utanför `AppConfig` (återstår att inventera)

Inte täckt här, men del av acceptanskriterierna i planen:

- Sparat spelstate per spel (Sudoku-progress, Yatzy-session, äventyrssessioner)
  och deras återställningsåtgärder.
- Wi-Fi-nätverk (`MyWifi`-listan), spelarkatalogen och SD-/filstatus.
- Touchdiagnostikens val (text- och zoom-loggning, testläge).
- Lokala vykontroller som inte sparas, till exempel äventyrets zoom.

## Ägarbeslut 2026-11-07

1. **Karaoke får fler inställningar; fler önskemål väntas.** Katalogen ska
   kunna växa utan att varje inställning kräver ett specialfall.
   - `karaFlip` och reverb/chorus får editor, men reverb/chorus ska först
     **testas och utvärderas**: resursanvändning (CPU/RAM) och om det fungerar
     på S3 och P4. Resultatet beror även på vald SoundFont (.sf2), så mätningen
     görs per SoundFont och per kort innan standardvärden bestäms.
   - MIDI-/låtvolym och melodispårvolym är separata (melodispårvolym är inte
     implementerad än).
   - Byte av instrument, främst på sångspåret, med flera ommappningar. Ska även
     kunna göras på andra spår/kanaler.
   - Editorläge för att manuellt synka in text mot MIDI-filen, inte bara via
     `.lyr`, med möjlighet till mindre justeringar. Kan hamna på webben.
   - **Allt som ställs in per låt sparas med låten:** SoundFont (.sf2),
     MIDI-volym, sångspår på/av och volym, instrumentbyten, vilken kanal som
     är sång, reverb/chorus med mera. **Mastervolym är global** och sparas
     inte per låt.
2. **Alla spel ska kunna ge en spela-tillsammans-känsla** och visas/spelas på
   webben på olika sätt; hur är inte bestämt. `advMode` och andra spelval
   behöver därför en webbyta på sikt. `sudokuDeadZone` är en lokal hjälp för
   ESP-touchskärmarna och ska **inte** exponeras på webben.
3. **`rollAnimMs`, `throwMode` och liknande ska kunna ändras utan att
   stoppa/starta om** spelet. Sessionen bevaras.

### Konsekvenser för planen

- Karaoke är den största inställningsytan. Den bör få en egen kategori med en
  global nivå (standardvärden) och en låtnivå (sparas i `karaoke.reg` via den
  befintliga CRC32+storleksidentiteten, se PR B i planen).
- Låtunika värden behöver en tydlig standard från de globala värdena och en
  åtgärd för att återgå till standard per värde.
- Mätningen av reverb/chorus är ett eget arbete före beslut om standardvärden.
- Webbpariteten styrs av en lista över vilka inställningar som är lokala för
  enheten (till exempel `sudokuDeadZone`) och vilka som delas med webben.
