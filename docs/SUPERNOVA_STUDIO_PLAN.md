# Supernova Studio – plan och specifikation

Planeringsdokument för ett separat Windows-program (eget repo `Bokbacken/Supernova_Studio`) som
ersätter `tools/supernova_convert.ps1`. Status: **plan, inget är implementerat**. Beslut är
ägarens önskemål från 2026-11-28; tekniska val är förslag.

## Mål

1. Konvertera en samling UltraStar-låtar och skriva dem direkt på SD-kortet (ersätter skriptet).
2. Förhandsgranska och justera synk: egen videospelare, singlestep och ljudvågsbild.
3. Föreslå och skriva `#GAP`, `#START`, `#VIDEOGAP` i låtens `.txt`.

## Kravbild och motiv

Varje krav har en orsak. Förslag som inte tjänar någon av orsakerna är inte relevanta för ägaren;
fråga hellre än att lägga till funktioner utanför kravbilden.

| Krav | Orsak |
|---|---|
| Konvertering sker på PC, direkt till SD-kortet | Enhetens CPU och WiFi är för svaga för att koda video; en hel samling ska kunna läggas på kortet utan mellansteg. |
| Windows-program i C#, ffmpeg som motor | Ägaren behärskar C#; ffmpeg gör redan alla kodningar, så Studio ska styra det, inte ersätta det. |
| AVI (MJPEG + MP3) alltid för lokal uppspelning | Enhetens hårdvaru-JPEG-avkodare spelar MJPEG; bildytan 800x420 ska utnyttjas maximalt med bevarad bildproportion. |
| Webb-MP4 (H.264 + AAC, <= 720p, ~2,5 Mbit/s) | Webbläsaren spelar MP4 direkt med bättre kvalitet och jämnare flyt än AVI-bildrutor. Enhetens WiFi (C6 via SDIO) är flaskhalsen, därför låg bitrate. |
| Ljud i MP4:n | Visar att ljud finns och kan spara resurser (en ström i stället för två) när mp3 saknas. |
| MP4 skapas även när bara AVI finns | Omkodade SingStar-kataloger saknar original-MP4 men ska ändå få bra webbuppspelning. |
| Egen videospelare med bildrutesteg | Synk (GAP, VIDEOGAP) måste kunna justeras exakt; bildruta för bildruta är den enda pålitliga metoden. |
| Ljudvågsbild med mitt-/sido-/differensfilter | Många låtar har sången lika i L och R, och med karaoke- plus originalfil blir differensen sången; det visar var sången verkligen startar. |
| Skriva GAP, START, VIDEOGAP i `.txt` | Texten ska börja rulla vid sångstart, inte vid filens början (långa intron). Enhetens spelare ignorerar dem ännu; webbspelaren använder dem. |
| `#VIDEO`-pekaren ignoreras | Vi skapar egna filer (AVI/MP4) vid sidan av originalet. |
| Befintliga filer hoppas över, omkörning med tvång | Stora samlingar ska kunna konverteras i omgångar och återupptas. |
| Format är kontrakt mot firmware | Ändras kodek eller filnamn i endera repot slutar enheten eller webben spela. |
| Inget påstås vara hårdvaruverifierat | Ett lyckat bygge/test visar inte att filen spelas på enheten; ägaren testar på riktig hårdvara. |

Utanför kravbilden (gör inte utan att fråga): streaming över UDP (webbläsare kan bara HTTP/TCP),
ändrad enhetsfirmware, molntjänster, kontosystem.

## Teknik (förslag)

- C# / .NET (WPF eller WinUI), Windows 10+.
- ffmpeg/ffprobe som motor (medföljande binär eller sökväg i inställningar).
- Uppspelning: LibVLCSharp eller ffmpeg-baserad egen spelare med bildruteexakt singlestep.
- PCM/vågform: NAudio eller ffmpeg (`-f f32le`).
- Skriptet bevaras som referens tills programmet når samma funktion; konvertering körs via ffmpeg
  med exakt samma argument som nedan så att resultatet blir identiskt.

## Utdataformat (måste matcha enheten)

Enheten: ESP32-P4 (Waveshare 4.3", 800x480). Supernova-menyn finns bara på P4.

Utdatamapp speglar spelarens layout: `<Dest>\<Artist - Titel>\` med

| Fil | Innehåll | Används av |
|---|---|---|
| `<namn>.avi` | MJPEG (yuvj420p, `-q:v 10`, 15 fps) + MP3 (libmp3lame 160 kbit/s, 44,1 kHz, stereo) | enhetens lokala uppspelning (alltid AVI) |
| `<namn>.mp4` | H.264 High, yuv420p, `-crf 23 -maxrate 2500k -bufsize 5000k`, **AAC-ljud** 160 kbit/s, `+faststart`, höjd <= 720 (ingen uppskalning) | webbspelaren (föredras om den finns) |
| `.txt` | UltraStar-text (UTF-8) | båda |
| `.mp3`, omslag (`.jpg/.jpeg/.png`), `.lrc` | kopieras oförändrade | båda |

AVI-layout (`Get-Layout` i skriptet): videoytan är 800x420 (`-Area`), golvad till 16-multipler
(800x416). Innehållet skalas med bevarad bildproportion (SAR och rotation beaktas) så att full
bredd eller full höjd används, jämna mått, och paddas med svart till 16-pixelmultipler
(hårdvaru-JPEG-avkodaren kräver det). Skala med `lanczos`, `setsar=1`.

Webb-MP4: `scale=w=trunc(min(ih,720)*dar/2)*2:h=trunc(min(ih,720)/2)*2`. Mätningar visade att
WiFi-sändningen från enheten (ESP32-C6 via SDIO, ~2-6 Mbit/s) är flaskhalsen, inte SD-kortet;
håll därför MP4-bitraten låg (<= ~2,5 Mbit/s totalt).

Befintliga filer hoppas över (`-Force` skriver om). `-Mp4Only` motsvarar "lägg bara till MP4 på ett
redan konverterat kort".

## Hur enheten/webben läser en låtmapp

- Biblioteket (`main/supernova_library.cpp`, `findMedia`) tar första `.mp3` som ljud (annars
  ljudet i AVI:n), första `.mp4` som webbvideo, första `.avi` som video och `.txt` som text.
- Webbspelaren (`main/web/supernova-play.html`) använder ett `<audio>` som huvudklocka; MP4-videon
  är avstängd i ljud och följer klockan. Att använda MP4:ns eget AAC-ljud när mp3 saknas är
  ännu inte implementerat.
- UltraStar-parsern: `main/supernova_lyrics.cpp/.h`. Tolkas: TITLE, ARTIST, BPM, GAP, RELATIVE,
  VERSION, START, END, VIDEOGAP, PREVIEWSTART, AUDIO/MP3, INSTRUMENTAL, VOCALS, COVER, BACKGROUND,
  GENRE, LANGUAGE, YEAR, EDITION, TAGS, CREATOR, P1/P2 samt `B beat bpm` (BPM-byte) och
  duettrader. `#VIDEO` används **inte** (vi skapar egna filer).
- Tid: `ms = GAP + takt * 60000/(BPM*4)`. `START` är sekunder, `END`, `GAP` millisekunder,
  `VIDEOGAP` sekunder (video startar så mycket före ljudet).
- Webbspelaren: hoppar till START, stoppar vid END, förskjuter MP4 med VIDEOGAP. Enhetens egen
  spelare använder ännu inte START/END/VIDEOGAP.

## Funktioner

### MVP

1. Välj källmapp och mål (SD-kort), lista låtar med status (AVI/MP4/mp3/txt finns?).
2. Konvertera markerade låtar parallellt (`-Jobs`), framsteg, avbryt, loggar.
3. Förhandsvisning: spela originalvideon + ljud, bildrutestegning (singlestep, +/- 1 bildruta).
4. Ljudvågsbild med spelmarkör, zoom och klick för att hoppa.
5. Markörer "sången börjar här" och "sången slutar här" som föreslår `#GAP`
   (och `#START`/`#END`) och kan skriva dem till `.txt` (med säkerhetskopia).

### Vågformsfilter (för att hitta var sången börjar)

- **Mitt (L+R)/2:** sången ligger oftast lika i båda kanaler och framhävs; fungerar sämre vid
  panorerad sång.
- **Sida (L-R):** motsatsen, för att hitta kompet.
- **Differens karaoke - original:** först tidsjustera (korskorrelation) och nivåjustera, annars
  blir resultatet brus. Det som återstår är sången.
- Visa RMS/energi i fönster (t.ex. 10 ms) och en tröskel som föreslår första sångstart.

### Senare

- Förhandsvisning av AVI/MP4-resultatet (bildyta 800x420 och beskärning/kanter).
- Tagg-editor (alla USDX-taggar), tidsjustering av hela texten, notvisning över vågformen.
- `VIDEOGAP`-förslag genom att jämföra videons och ljudets början.
- Importera/exportera `.upl`-spellistor (USDX); spellistorna på enheten har eget format.
- Webb-MP4 som reserv: skapa MP4 även när bara AVI finns (redan i skriptet via `-Mp4Only`).

## Projektstruktur (förslag)

```
Supernova_Studio/
  README.md
  docs/            specifikation, format (kopia/länk till detta dokument), beslutslogg
  src/
    SupernovaStudio.App/       UI (WPF/WinUI)
    SupernovaStudio.Core/      låtmodell, USDX-parser/skrivare, layoutregler, ffmpeg-kommandon
    SupernovaStudio.Audio/     PCM-läsning, vågform, filter, korskorrelation
  tests/
    SupernovaStudio.Tests/     enhetstester (parser, layout, ffmpeg-argument, filter)
  tools/           ev. kvarvarande skript
```

Core ska vara UI-fritt så att tester körs utan Windows-UI. Testa parsern mot samma exempel som
`tests/test_supernova_lyrics.cpp` i firmware-repot (BPM-byte, duett, relativt läge, GAP).

## Gränssnitt mot firmware-repot

- Formatet i tabellen ovan är kontraktet. Ändras det (kodek, upplösning, filnamn) ska båda repon
  uppdateras; firmware-repots `docs/SUPERNOVA_STUDIO_PLAN.md` och `tools/supernova_convert.ps1`
  är referensen tills Studio tagit över.
- Maskinläsbart kontrakt kan läggas som `supernova-format.json` i Studio-repot.
- Allt hårdvaruverifierat markeras explicit; en lyckad konvertering visar inte att filen spelas
  på enheten.

## Öppna frågor till ägaren

- WPF eller WinUI 3? Medföljande ffmpeg eller användarens egen?
- Ska Studio kunna kopiera direkt till SD-kort med säker utmatning?
- Ska konvertering på sikt även kunna skapa instrumental (karaoke) via filter, eller bara
  använda befintliga filer?
