# Fas A – UI-inventering och migrationsordning

Inventeringsdatum: **27 oktober 2026**. Avsedd som arbets- och testunderlag inför **28 oktober**.
Inventeringen är en källkodsgranskning; Fas B1-status och verifieringsgräns följer nedan.
Firmwareversion **0.19.1** bevaras.

## Läs först: beslut inför första P4-testet

- **Gemensam testspärr först:** en hårdvaruprofil är inte en display-/touchportning.
  `P4_43` anger **480×800**, men är uttryckligen placeholder/overifierad. Det är ett
  belagt profilvärde, **inte verifierad upplösning på morgondagens fysiska skärm**.
  Bekräfta faktisk panel, aktiv logisk upplösning, fungerande flush och touch innan speltest.
- Därefter **Äventyr först**, sedan **Pig, Farkle och Skepp & kapten**
  (engelsk menytext: Ship & Captain; motorn Ship/Captain/Crew, kodtyp `SCC`). De tre har en egen gemensam
  minispelsvy, inte Yatzys redan omarbetade spelvy.
- Första implementations-PR bör vara **B1: Äventyrs väljare och vanliga dialoger,
  med spelviewport och kontroller orörda**, utan ändrad inputsemantik. Ingen generell
  header/content/footer-modell ska pressas in i spelrenderingen.
- **B2 är villkorad:** ändra koordinat-/hitboxkoppling först där B1 eller ett
  senare viewportbeslut faktiskt kräver det; behåll styrplattans spelkänsla.
- **Karaoke:** endast gemensam rotation och resurslivscykel kartläggs här.
  Layoutmigrering, PSRAM-optimering och syntharbete hör till ett senare separat steg.

**Evidensnivåer:** **Kod** = statiskt verifierat i angivna symboler/rader;
**Användaruppgift** = tidigare Yatzy-steg fungerar på nuvarande enhet, Karaoke är
textdominerad/landscape och nästa plattform väntas ha 4× PSRAM;
**Fysisk kontroll återstår** = all touchprecision, läsbarhet, spelkänsla, prestanda och
minnesmarginal på verkliga enheter. Ingen användaruppgift räknas här som egen mätning.

### Referenskonvention och befintliga planer

Filreferenser nedan är absoluta sökvägar i den granskade arbetskopian; `:a–b` anger
källkodsrader vid inventeringen, inte framtida stabila radnummer.
Inventeringen kompletterar, ersätter inte innehålls-/motor-/Karaokeplanerna:

- `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/docs/GAME_ARCHITECTURE.md:7–49`
  beskriver separationen spelmotor/device-UI/webb och de fyra tärningsspelens regler.
- `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/docs/ENGINE_2D_BRIEF.md:38–89`
  beskriver canvas, kamera, tryck-för-att-gå och styrplattan; detta är inte en ny motorplan.
- `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/docs/BIBLE_ADVENTURE_PLAN.md:110–137,172–226`
  behandlar berättelsedialoger och senare innehåll, inte en generell skärmmigrering.
- `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/docs/MUSIC_KARAOKE_PLAN.md:17–93,124–152`
  är fortsatt hemvist för Karaoke-/synth-/minnesarbetet.
- `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/docs/COMPONENTS.md:1–44`
  beskriver delade komponenter och planerade utbrytningar; ingen sådan utbrytning ingår här.
- `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/README.md:290–296,374–380`
  beskriver profilernas status och hostlayouttester. Den äldre generaliseringen
  ”other screens still use fixed 320x480 coordinates” ska inte tolkas som aktuell
  klassificering av varje vy; nedan skiljs faktiskt migrerade och kvarvarande delar åt.

## Gemensam grund och övriga enhetsvyer

**Layoutklass:** *absolut* = fasta huvudytor/koordinater; *delvis responsiv* =
procent/flex/ankring inom en fortfarande fast eller blandad ram; *responsiv* =
huvudytor beräknas från tillgänglig yta. Responsiv betyder inte automatiskt
skalande font eller bevisad fysisk användbarhet.

| Område och entry/render | Klass, orientering och input | Belägg, risk och återanvändning |
|---|---|---|
| Hårdvaruprofil, `HwProfile::active/get`; `UiMetrics::screenW/H`, `scalePct` | Upplösningsmedveten grund, inte en vy/inputadapter. | `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/hw_profile.cpp:7–23`, `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/hw_profile.h:8–19`, `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/Kconfig.projbuild:5–16`: ES3C35P 320×480, P4_43 480×800 placeholder. `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/ui_metrics.cpp:8–49`: live LVGL-storlek före profilfallback, skala från kortsida begränsad till 100–150 %. **Hög gemensam risk:** profilval bevisar inte fungerande drivrutin; tokens skalar inte godtyckliga anropare eller fonter. |
| Huvudmeny `UiYatzy::showMenu`, `menuListCard`, `menuCardIn` | Responsiv huvudlista; portraitflöde, touch. Ingen egen landscapeväxling. | `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/ui_yatzy.cpp:107–150,167–224`: flexlista mellan `headerH/footerH`, live bredd/höjd, minhöjd och spacing från metrics. Kortens underrad har dots; titel/font 18/24 och inre offset 28 är fortfarande fasta; klocka/hint y=52 och toppknappar 52×36. **Låg–medel:** återanvänd listmönstret, testa att alla sex kort går att nå genom scroll och att långa titlar inte kolliderar. |
| Tema `Theme::screen/card/label/button/header/msgboxFonts/loadScreen` | Delvis responsiva byggstenar, touch via anropande vy. Orientering ärver displayen. | `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/components/my_ui/include/my_theme.h:45–143`: skärmen oskrollbar, header 100 % bred men 44 hög, titel font 24; knapp får storlek från anroparen och centrerad font 18 utan generell textpassning. `msgboxFonts` säkrar tecken/fonter, **inte layout**. `loadScreen` raderar tidigare skärm efter 180 ms fade. **Medel:** ändra inte temat globalt för att försöka skala alla spel. |
| `TextInput::open/openQwerty`, `CharWheel::create/wheelEvent/close` | Responsiv inom **given** rektangel, touch; tangentbordsfokus är inte Äventyrs styrplatta. Ingen egen rotation. | `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/components/my_ui/my_textinput.cpp:85–178`: breddbaserat AUTO-val, textarea w−12, keyboard h−82; fasta fonter 14/18/24 och padding 6. `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/components/my_ui/my_charwheel.cpp:227–362`: minsta 120×200, höjdprocent 14/9/6/14, dots/clip i fält och hint, font 18/24/36; touchyta följer objekten, tabs har utökad klickyta. Clickable rot blockerar bakomliggande touch; async close och delete-callback äger state. **Medel:** korrekt anroparrektangel behövs, hjälpen kan inte reparera fast 320×480 hos anroparen. |
| Inställningar `UiSettings::show`, `switchRow`, `leave`, `applyRotation/applyHardware` | Delvis responsiv, portrait 0/180°, touch. | `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/ui_settings.cpp:30–47,218–261,263–408`: kropp (0,46), 320×378 med vertikal flex/scroll; procentkort, fasta switchar 50×26, spar-/avbrytknappar vid y=430. Joystickzoner och långsam/snabb takt konfigureras här; det är inte en separat inputenhet. Display och touch roteras tillsammans under lås. **Medel:** kropp/footer behöver senare måttanpassas; redan skrollbara kort kan behållas. |
| Klocka `UiClock::attachLabel/labelTick`, `showSetter`, `roller` | Setter absolut, portrait, touchrollers; menyetikett är en återanvändbar timerfunktion. | `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/ui_clock.cpp:17–73,100–149`: datumrollers x=10/92/194 y=126, tid y=274, knappar y=430; font 14/24/36, ingen generell responsiv setter/scroll. **Låg–medel:** senare hjälpvy, inte blockerare om man kan återgå. |
| Wi-Fi `UiWifi::showNetworks/fillList/forgetDialog/showPassword/showConnecting`, `rowEvent` | Delvis responsiv lista inom absolut portrait-ram; touch och long-press, ingen styrplatta. | `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/ui_wifi.cpp:87–129,142–203,227–280,283–340,375–437`: lista 320×344 vid y=48 med flex/vertikal scroll; rader 100 %×50 men SSID-label 178 med dots, dialogbredd 280, status 304 vid y=400, bottenknappar y=426. Lösenord anropar `CharWheel::create` med 320×480, max 64 tecken; anslutningsetiketter 300 breda. Timer stängs vid navigation; `navLater` väntar på async keyboardstängning. **Medel:** fungerande nät är separat förutsättning för webbspel, inte skäl att ändra nätlogik i layout-PR. |
| Boot `UiBoot::show/set/setProgress/setStatus` | Delvis ankrad men fast innehåll, portrait; statusvy utan spelinput. | `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/ui_boot.cpp:14–97`: logo 64², bar 260×10, kort 296 med radhöjd 33, oskrollbart; detail 240 med dots och font 14. Hårdvarunamn är nuvarande S3:s, inte bevis på P4. **Låg** layoutrisk, diagnostik måste tolkas mot faktisk hårdvara. |
| OTA `UiOta::begin/tick/build` | Delvis responsiv modal på `lv_layer_top`, ärvd orientering, blockerar touch. | `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/ui_ota.cpp:18–87`: overlay 100 %×100 %, centrerat fast innehåll (bar 260×22, wrap-info 280, font 14/24/36), ej scroll; global 250 ms timer, feloverlay raderas efter sex sekunder. **Medel:** kontrollera top-layer efter rotation/återgång; kör inte en riktig uppdatering mitt i första speltestet. |

Ingen av de fasta hjälpvyerna ovan blir metrics-baserad bara genom att inkludera temat.
Webbgränssnittet är en separat renderer; en fungerande mobilvy bevisar inte enhetens
hitboxes. Device-overlay för webbspel ingår däremot i tärningsinventeringen.

## Äventyr – högsta granskningsprioritet

### Kodkarta och vyinventering

**A** nedan avser `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/ui_adventure.cpp`.
**AH** avser `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/adv_setup_ui.h`.
Äventyret har **ingen användning av `UiMetrics`** i dessa filer. Det är inte en
vanlig LVGL-listvy: världen ritas till en RGB565-canvas med egen kamera och blitter.
Vyerna är portraitkonstruerade men `UiAdventure::show()` framtvingar inte portrait;
de ärver displayens aktuella orientering. 0/180° sköts gemensamt av display/touch,
inte av en alternativ Äventyrslayout.

| Vy/komponent, entry och render | Faktisk layout och input | Risk, begränsning och ändringsbehov |
|---|---|---|
| Inträde `show → buildWorldPick → worldChosen → buildPlayerPick → playerChosen → startGame → buildGame` | **Delvis responsiva väljare inom absolut ram**; touch. A:2660–2922. SD-katalog är det aktiva flödet; `s_session` för inbyggda äventyr är markerad dormant (A:157–163). | **Medel:** inte bara `show*` måste migreras. Ingen SD-katalog ger fel-/hjälpvy med wrap-bredd 296 och tillbaka, inte en inbyggd värld som automatisk fallback. Tillgång till giltiga SD-äventyr är en testförutsättning. |
| Världsval `buildWorldPick/addWorldRow` | A:2786–2800,2839–2884: lista (6,50), 308×424, vertikal flex/scroll; knappar 296×62, namn font 18, beskrivning font 14 wrap 272. | **Medel:** redan scroll, men ram/rad/textbredd är fasta. Wrap innebär inte obegränsat utrymme för SD-beskrivningar i 62 px; långa titlar och flera rader måste provas. B1 kan anpassa dessa utan att röra världens input. |
| Spelarval `buildPlayerPick/addPlayerRow/fillPlayerList` | A:2746–2772,2803–2836: mode 308×40 y=52, lista 308×288 vid (6,128), knappar 296×42, Ny spelare y=424 h=48, font 14/18. **Delvis responsiv**, touch. | **Medel:** scroll och vald spelares scroll-to-view finns; bredd/footer är ändå absoluta. Sparmarkering i namnsträng kan konkurrera med namn. Återanvänder Players-data, inte tärningsspelens responsiva spelarvy. |
| Ny profil `newPlayer/saveNewPlayer/closeNameForm`, `AdvUi::nameForm` | A:2697–2725, AH:30–73: **delvis responsiv**, live helskärm/bredd, men input y=44 h=48, felområde y=100 h=68, knappar y=178, keyboard y=240 h=H−240. Touch/QWERTY-fokus; max `PLAYER_NAME_CAP−1`, fonter 14/18. | **Medel:** fungerar inte godtyckligt vid låg landscapehöjd bara för att W/H läses; portraitavsikt. Spara/avbryt använder async delete; fel/dubblett ska behålla form, vald profil och listuppdatering. |
| Fortsätt/nytt, minresultat `playerChosen/mineResult`, `AdvUi::resultLayout` | A:1894–1921,2728–2743, AH:8–23: **delvis responsiva** dialoger; displaybredd−24, maxhöjd H−24, vertikal footer, fullbreddsknappar h=48. Touch. | **Låg–medel:** redan förbättrade via AH, inte via Yatzys `layoutDialog`. Fast footerhöjd från knappantal; prova lång text och alla knappar. Det finns resultathantering för inbyggda minbanor, men normalt SD-flöde avancerar via `sdExitAdvance` (A:1564–1580,2020–2027); utlova inte nåbar inbyggd kampanj från dagens väljare. |
| Spelcanvas `buildGame`, `render/blit/camera/currentCamera/playerPixel` | **Absolut**, hybrid **touch + virtuell touchstyrplatta**, inte två bekräftade fysiska enheter. A:52–65,740–757,829–922,2608–2657: canvas 320×320 vid (0,0), kontrollpanel y=320 h=160. Tilezoom 32/16, manuell clipping till VIEW_W/H, kamera i pixelkoordinater. | **Hög:** framebuffer, stride, kameragränser, rendering och karttryck måste ändras tillsammans om viewport ändras. `toggleZoom` byter tilepixelstorlek, inte upplösning eller orientering. Större skärm ger inte automatiskt större världsvy. |
| HUD/handlingar `buildControls/updateStats/refreshActBtn/doSwap/doHit/bombAction/cancelBomb` | **Absolut** med lokal flex; A:933–983,1678–1750,1858–1892,2477–2602. Vänster 80 px, höger x=240 w=76; ikoner 20², knappar meny 36×40/mute 34×40. ”Byt” h=18 font 14; handling h=36 på `PRESSED`, Byt på `CLICKED`; mina bombknapp h=56. | **Hög** vid layoutflytt: små knappar, oskrollbar HUD/item-wrap och separat joystickyta på skärmen. Behåll `PRESSED`/`CLICKED`, debounce och Slå/Prata/Hoppa respektive bombflödets semantik. Delade temaikoner innebär inte delad geometri. |
| Styrplatta `buildJoystick/joyEvent/configureJoy/joyShow` | **Absolut**, A:2100–2212, konstanter A:60–65. Touchrektangel 160×160 vid (80,320), lokal mitt (80,80), ringradie 74, stoppradie `74*17/53` = 23. Hela rektangeln fångar input, även hörnen utanför ringen. | **Hög:** ring är visuell, inte hitboxgräns. Knopp/pilar är feedback, inte OS-/LVGL-musmarkör eller menyfokus. Ritning, konfigurerade radier och faktisk objektstorlek måste hållas synkade; B1 lämnar allt detta orört. |
| Berättelse `startTalk/talkBuild/talkShowNode/talkShowChoices/addChoice/choiceClicked/talkTick/endTalk` | **Absolut/bara textmätt**, A:1262–1267,1340–1539. Dialogbredd 308 med padding 10, porträtt 48²; text font 14 wrap, mäts till innehållshöjd och placeras uppåt från viewportens botten. Svarspanel 320×160 vid y=320; svar h=36, gap=4, font 14 om hint eller >26 tecken annars 18. Touch; styrplattan väljer inte svar. | **Hög text-/clippingrisk:** box/panel är oskrollbara; långa SD-texter kan göra boxens y negativt och många/långa svar får inte automatiskt mer plats. Ingen generell fokusordning hittad. Inte samma komponent som modal msgbox. Anpassa detta separat från vanlig dialog i B1 om tidsramen kräver. |
| Spelmeny/omstart/game over/bombval `showMenu/dialog/showGameOver/bombAction`, toast/stage | **Delvis responsiv LVGL-msgbox + fasta inre mått**, A:985–1003,1850–1948,2340–2439,2621–2637. Menyknappar 250×42, hjälpwrap 250, toastwrap 296; toast är ej klickbar, stage vid (4,4). | **Medel–hög:** `Theme::msgboxFonts` rättar bara font; AH-layout används inte av alla dialoger. Menyn stoppar väg/styrplatta och tillåter inte en andra meny när dialog/samtal är öppet. Långa dialoger kräver maxhöjd, scroll och nåbara avslut; toast/stage kan hamna visuellt fel om bara canvas centreras. |

### Verklig inputkedja, koordinatsystem och fokus

1. **Panel → LVGL:** `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/board_touch.cpp:108–141,210`
   klipper till panelens `LCD_H_RES/LCD_V_RES`, filtrerar korta släpp och speglar x/y
   för 180°. `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/components/my_lvgl/my_lvgl.cpp:84–95,190–199`
   registrerar ett pointer-indev och lämnar punkten till LVGL. Det finns ingen
   separat fysisk joystickdrivrutin i denna kedja.
2. **LVGL → platta:** A:2124–2147 läser aktuell pointerpunkt och plattobjektets
   `lv_obj_get_coords`; `dx=pt.x−(a.x1+JOY_CX)`, `dy=pt.y−(a.y1+JOY_CY)`.
   **Objektets translation räknas alltså redan bort**, men mitt/radier är fasta.
   `PRESSED` stoppar A*-väg och ger `press`; `PRESSING` ger `drag`;
   `RELEASED/PRESS_LOST` ger `release` och döljer knoppen.
3. **Platta → spelvärld:** `configureJoy` tar sparade zoner/tider/diagonalläge.
   `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/components/otr_engine/include/otr_joystick.h:30–80,83–171`
   beräknar stopzon, 1–4 fartzoner, dominant riktning/alternerande axlar,
   omedelbart tappsteg och hold-repeat. Utanför ringen räknas som yttre zon;
   släppt men väntande tapp kan behållas upp till 400 ms. A:2029–2043 kör
   `s_joy.poll → doMove`, inte en fokus-/markörförflyttning i LVGL.
   **Styrplattan styr spelvärlden.** STOP-mitten avbryter också tap-to-walk.
4. **Karttryck → värld:** A:2215–2267 använder
   `fx=(p.x+camX)/s_ts`, `fy=(p.y−VIEW_Y+camY)/s_ts`.
   Kameran är i **pixlar**; divisionen ger **tilekoordinater**. Spelarträff använder
   ±0,6 **tile**, inte ±0,6 pixel. NPC identifieras via cell, annars väljs A*-mål.
   Long-press sätter flagga i fog-läge; samma release ignoreras genom `s_longDone`.
   Bombpreview väljer cell utan att börja gå.
   **Canvasens verkliga x/y läses inte**, x=0 och VIEW_Y förutsätts.
   Att flytta canvas eller skala bilden kräver en invers transform i samma PR;
   större `s_ts` är inte en generell layouttransform.
5. **Rotation/resize:** Äventyr har ingen egen 90/270°-transform eller
   `SIZE_CHANGED`-baserad ombyggnad. Mjukvarurotation ligger i det gemensamma
   displaylagret; inga extra råkoordinatrotationer ska läggas på spelet utan
   verifiering av LVGL-kedjan. Storleksbyte räknar inte om A:s konstanter eller
   framebuffer. Testa portrait 0/180° och återkomst från Karaoke, inte en
   påhittad färdig landscapevariant av Äventyr.

### Overlay- och resurslivscykel

- `startTalk` avbryter väg/styrplatta och bygger två klickbara ytor;
  `viewEvent` avvisar världstryck när dialog/samtal/game over råder.
  `tick` pausar både plattans steg och väg när dialog/samtal/bombpreview är aktivt.
  `joyEvent` har inte själv motsvarande modalguard: bevara både täckande objekt
  **och** tick-spärren vid flytt. Meny/platta får inte plötsligt ta emot samma touch.
- `choiceClicked` köar `s_pendingChoice`; `talkTick` byter/raderar först senare,
  så knappen inte raderas i sin egen callback. `endTalk` tar bort box/panel,
  återställer toast och uppdaterar HUD/sparbehov. Ingen fokusdriven svarsväljare
  eller fristående joystickmarkör hittades; textinmatningens caret är ett annat flöde.
- `dialog` stänger tidigare msgbox; `closeDialog` anropar `lv_msgbox_close`
  och nollställer pekaren (A:993–997). Backdrop-hanteringen ligger i LVGL,
  vars implementation inte har verifierats i denna arbetskopia. Spelmenyn hanterar dessutom
  `DELETE` för kryss-stängning (A:2436–2439). Testa avbryt, kryss, omstart,
  fortsätt, minresultat och återinträde så inga top-layer-objekt blockerar nästa vy.
- A:2887–2904 allokerar **redan före väljaren** canvas, sprites, värld, planerare,
  väg, sparbuffert, katalog och laddat äventyr i PSRAM; misslyckande återgår till menyn.
  Canvasen ensam är 320×320×2 = **204 800 byte**, inte en mätning av total minnesåtgång.
  A:2269–2325: `leave` pausar tick/stoppar live, sparar och byter meny;
  `screenDeleted` frigör bara när det raderade objektet är `s_activeScr`.
  Gamla väljarskärmar under fade får därmed inte frigöra det nya spelets buffertar.
  `freeAll` stoppar live/timer, frigör allokeringarna och nollställer state.
  **Behåll ägarskap och fade-ordning; ”free on navigation” är för tidigt.**
- `liveStop` gör webbsnapshoten inaktiv, inte nödvändigtvis oallokerad.
  `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/adv_live.h:1–11`
  beskriver en separat PSRAM-snapshot som allokeras vid första publicering och
  behålls. A:1952–2010 publicerar till webb-livevyn; detta är inte en extra
  joystick-/fokuskanal. Att lämna Äventyr bevisar därför inte att all
  Äventyrsrelaterad PSRAM har återlämnats.

## Pig, Farkle, Skepp & kapten – vad är redan gemensamt?

**D** avser `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/ui_yatzy.cpp`.
Namnen och dispatchen beläggs av `GAMES`, `gameTitle`, `startNewGame`
(D:89–104,597–619) och språknycklarna `GAME_NAME_PIG/FARKLE/SCC` i
`/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/lang/lang_sv.cpp:203–205` respektive
`/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/lang/lang_en.cpp:203–205`.
Alla nedanstående enhetsflöden är **touch**, eventuellt kombinerat med webbspel,
**inte Äventyrs styrplatta**. De framtvingar ingen egen rotation; portrait
är baslinjen, större landscape är en separat kontroll av ärvd displaystorlek.

| Vy/render/event | Kodläge och konkreta begränsningar | Risk och migrationsbehov |
|---|---|---|
| Variantval `showVariants/menuCardIn/selectionHeader/selectionBody/selectionColumn/selectionButtonRow` | **Responsiv ram** för alla fyra spelen, D:221–331; metrics, flex, vertikal scroll. Pig erbjuder 1/2 tärningar, Farkle Standard, SCC tre ronder. Fonter 14/18/24, fasta inre kortoffset och dots i underrad. | **Låg:** tidigare Yatzy-arbete gäller dessa gemensamma väljare också. Ingen ny parallell variantvy behövs. Kontrollera varje gren och långa översättningar. |
| Spelare `showPlayers/rebuildPlayerList`, register `showPicker/pickTableEvent/openWheel` | **Responsiv**, D:470–570,630–821: live mått, flexrader, namn dots, roster vertikalscroll, registertabell anpassar kolumn vid storleksändring; CharWheel får live W/H. | **Låg–medel:** återanvänd redan fungerande gemensamt flöde, prova 1/8 spelare, gäster, långt namn, filter, lägg till/ta bort och återgång för vart spel. Inte samma kod som Äventyrs profilval. |
| Informations-/avslutsdialoger `layoutDialog/infoBox/onMenu/quitEvent` | **Responsiv/begränsad**, D:402–433,2210–2237: maxbredd från metrics, maxhöjd H−2×marginLarge; innehållsscroll förväntas från LVGL-msgbox, inte uttryckligen aktiverad i `layoutDialog`. Fonter sätts separat. | **Låg–medel:** delas även av minispelen; migrera inte om i onödan. Befintliga hosttester innehåller långtext/scrollfall men har inte körts här. Bekräfta scroll samt att avsluta/avbryt inte lämnar overlay eller gamla timers. |
| PIN `webToggleEvent/webPinDone`, `UiNumpad::open/keyEvent/finish/close` | D:436–467; `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/ui_numpad.cpp:65–164,180–188`: **responsiv**, top-layer 100 %, breddstak `buttonWMax`, höjder begränsas av H/8 och H/9; titel/info/hint dots (info max två rader), font 14/24. Touch på buttonmatrix, blockerar underliggande vy. | **Låg–medel:** redan metrics-baserat, men långa förklaringar kan avsiktligt trunkeras; prova PIN fel/omförsök/bekräfta/avbryt. Async delete och callbackens godkännande måste bevaras. |
| Webbväntan `buildWebOverlay/updateWebOverlay/webTimer/webLocalEvent` | **Responsiv** geometri, D:2245–2283,2359–2425; blocker från y=48 i mini, live W/H, panel max 600 bred, scroll/wrap-text font 14 och ”spela lokalt”. Generation `s_gen` avvecklar inaktuell timer. | **Medel:** gemensam overlay fungerar geometriskt även ovanpå fast mini-layout; det bevisar inte att själva mini-spelet migrerats. Behåll blocker för tärningar/knappar och headerns meny, prova ansluten/frånkopplad webbsits. Ingen nätlogikändring. |
| Pig `showMini/miniRefresh/mRoll/mAction/mPlaceDie/mRowY` | **Absolut**, D:2656–2667,2746–2759,2803–2880,3005–3055,3084–3226. En/två tärningar, 52-px sprite med 1,5× bildskala; mitt på fast filt, inga hållplatser för Pig. | **Medel:** särskilt synlig bild kontra objekt/hitbox vid skalning; Pig har ingen hold-semantik. Banka/Kasta vid fasta bottenkoordinater måste senare följa gemensam mini-geometri utan regeländring. |
| Farkle samma entry + `mToggle/mDieDown` | **Absolut**; sex 46-px tärningar, steg 51 i `mSlotX`, hållna tärningar flyttas till fast nest-y, låsta dimmas; objekt har extra klickyta 4 px. D:2720–2725,2748–2762,3058–3066,3092–3094,3193–3204. | **Medel–hög:** tätaste tärningsraden; verifiera samtliga sex träffytor, låst/valbar och rörelse till/från nest. Delar sprites, inte Yatzys positionslayout. |
| Skepp & kapten / SCC samma entry + `miniRefresh/mToggle` | **Absolut**; fem 52-px tärningar, steg 60; tre rollchips (6+k×104,350), 100×30, font 14; last/hint vid y=388, wrap-hint bredd 170; Spara/Kasta. D:2746–2759,2803–2880,3058–3066,3150–3169. | **Medel:** kontrollera skepp/kapten/besättning, last och låsta kontra omkastbara tärningar; långa texter kan kollidera. Samma layout-PR som Pig/Farkle, men egna acceptansfall. |
| Gemensam mini-scoreboard, filt, informationsyta, flash | D:836–838,2656–2659,3111–3219: **absolut** SCR_W/H=320/480, board y=50 h=112, filt y=168 h=176, info y=350, knappar y=432 h=44. Scoreboard 1 kolumn ≤4, annars två fasta 150-px kolumner med dots-namn. Flash 260×70 och wrap font 24. | **Medel–hög:** inga live metrics för dessa huvudytor; på större portrait blir delar kvar i 320-px-området medan header/ankrad text kan breda ut sig. Fast layout är en migrationsrisk, **inte belägg för att allt är otestbart på större panel**. Filt/board är ej en adaptiv tabell; längre flashtext kan klippas. |
| Resultat `showMiniResults` och Yatzys `showResults` | **Absolut med enstaka ankare**, D:2594–2640,3229–3289: kort 310×360 vid (5,54), rader var 42, namn 170 dots, font 18/24, knappar y=424. | **Medel:** även Yatzys resultatsida återstår. Återspel/huvudmeny och 8-spelarranking måste ingå; förbättrad spelskärm betyder inte att slutskärmen är migrerad. |

### Kontroll av det uppgivna färdiga Yatzy-spåret

Koden stöder att metrics/hårdvaruprofiler, menyn, variants/players/picker,
dialoger, webb-overlay/numpad och **Yatzys spelvylager** har genomförts.
`GameLayout/gameLayout`, `showGame`, `setPhase`, `slotX`, `cupPlace` och
`refreshSuggestions` delar beräknad geometri (D:848–889,2428–2588).
Fasta 320/480-konstanter finns kvar som baslinje och för mini, inte som bevis
för att Yatzys huvudytor fortfarande alltid är absoluta.
Metrics beräknas vid `showGame`, inte som en generell garanti om ombyggnad mitt
under pågående spel/rotation.

Tärningsbilder och cup/gestfysik är avsiktligt oförändrade; **bägaren används bara av
Yatzy**, inte av de tre minispelen (D:2644–2654).
`facesFor/drawFace` (D:1032–1124) bygger sju ARGB8888-faces per storlek i
PSRAM och cachar två storlekar; den tillfälliga LVGL-canvasen raderas men
`s_faceSets[].mem` behålls. `cupCenterX/Y`, `cupPlace`, `cupEvent` och
`cupKill` använder layout/geststate, inte någon hårdvarustyrplatta
(D:993–1019,1531–1851). Radie 73 och press-slack 14 px är fortfarande fasta;
cup-tärningarnas klick stängs av så att touch går till bägaren.
Det är fungerande återanvändning i kod, inte en utförd fysisk precisionstest.
Återanvänd bildcache/temafonter/animationer men koppla minispelens ritpositioner,
nest och hitboxes till **en och samma** mini-layout i efterföljande PR.
Ingen anledning att ändra motorerna
`/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/game_pig.cpp`,
`/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/game_farkle.cpp` eller
`/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/game_scc.cpp`.

## Karaoke – enbart gemensamma beroenden i Fas A

**K** avser `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/ui_karaoke.cpp`.
Karaoke har **absolut landscape-bas 480×320**, `W/H` (K:26–29).
`showList/showPlayer/sheetBase/msg` och sökinput är touchbaserade; listans
sidstorlek 40 begränsar LVGL-objekt (K:131–140), men det är inte responsiv
layout eller ett bevis på liten total PSRAM-användning.
`sheetBase` använder W/H, full täckande klickbar rot och oskrollbart kort
(K:788–815). Ingen kart-/styrplatterouting används här.
Textdominans är användarens beskrivning; källan har labels/spans/lyrics,
men text är inte gratis vare sig i minne eller renderkostnad.

| Gemensamt beroende | Verifierat kodfaktum | Risk/avgränsning |
|---|---|---|
| In-/utrotation | K:1190–1202 anropar `Display::setLandscape(true, karaFlip)` före listbygge; K:62–66 återställer portrait före huvudmeny. `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/board_display.cpp:653–675`: 180° via MADCTL separat från 90/270° via LVGL. | **Hög gemensam testpunkt:** `UiKaraoke::show/leaveToMenu` kontrollerar inte bool-resultatet från `setLandscape`; misslyckad rotationsbufferallokering kan lämna 480×320-UI i fel logisk orientering. Dokumenterat behov, ingen fix/PSRAM-optimering här. |
| Flush, pekning och storleksbyte | `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/components/my_lvgl/my_lvgl.cpp:34–95,169–184`: chunkrotation med `lv_display_rotate_area/lv_draw_rotate`, invalidate-klippning mot logisk upplösning, `lv_display_set_rotation` byter logiska mått. Pointer-callback har ingen egen 90/270°-matris; LVGL förväntas hantera logisk pekmappning. Touchs 180°-spegling beskrivs ovan. | **Inte fysiskt verifierat:** prova karaFlip båda lägen med systemrotation 0/180°, hörn/mitt och återgång till Äventyr. Lägg inte till dubbel rotation i respektive spel på antagande. LVGL-dependencyn finns inte här för separat inspektion av dess interna transform. |
| Rit-/rotationsbuffertar | `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/board_display.cpp:30–31,635–643` och `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/components/my_lvgl/my_lvgl.cpp:122–184`: två partiella RGB565-ritbuffertar, `hres*bufLines*2`, PSRAM först, intern fallback med 16 rader. Vid lyckad PSRAM-allokering: 320×40×2 = **25 600 byte per ritbuffert**, två = 51 200. Intern fallback: 320×16×2 = **10 240 byte per buffert**, två = 20 480. Rotation allokerar **en** extra `g_stats.bufBytes` enligt vald bufferstorlek vid första behov och återanvänder den. | Ingen fullskärmsbuffer allokeras av just denna rotationskod. `g_rotBuf` frigörs inte vid rotation 0: **kvarhållen cache, inte belägg för en ny läcka per besök**. Frigör inte en buffer medan flush/fade kan använda den; separat framtida livscykelbeslut och mätning. Större panel/bufferhöjd ändrar budgeten. |
| UI-timers, dialogs och async | K:493–522,771–815,1181–1185: `listDeleted/playerDeleted` raderar sina timers bara för rätt skärm; sheet raderas med skärmen, stängning kan köas async. Väntedialog `s_waitDlg` på top-layer stängs när laddning slutar (K:82–128). Vid inträde med ljud av skapas också en Ja/Nej-msgbox **efter rotation**; båda svaren stänger async (K:1201–1222). | **Medel:** byt inte resursägare i gemensam layoutfix. Lista→meny spärras vid fontladdning; testa Ja och Nej vid ljud-av-start, direkt återinträde och att top-layer inte hänger kvar. |
| Låt/synth/mixer | `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/karaoke.cpp:285–400,773–783,890–903`: bakgrundstask laddar font; `close()` kopplar bort extern audio, stoppar spelare och tömmer låtdata men är **inte samma sak** som bank-unload. Listans exit anropar `unloadFont`, som vägrar under LOADING och annars `close` + `s_synth.unload`. | Ingen synthändring här. Radering av skärmens labels säger inget om huruvida banken är kvar. Följ task/lås/audioägarskap i senare minnespass; hävda inte uppmätt frigjord PSRAM från dessa kodvägar. |
| Minnesbudget och långlivade data | `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/components/otr_synth/otr_synth.cpp:230–245,563–570`: unload frigör synthdata, prepare-budget begränsas av största PSRAM-block minus reserv. Bibliotek/index och audio-stream har egen livstid, se `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/karaoke.cpp:26–80`, `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/kara_index.h:35–54,74–115`, `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/kara_web.cpp:240–265`; de ska inte antas följa LVGL-screen-delete. | **Hög för senare minnesarbete:** större total PSRAM garanterar inte tillräckligt sammanhängande block. Uppgiften ”4× mer” är inte en verifierad P4-minnesprofil. Nuvarande enhet måste fortsatt fungera. |

**Senare separat Karaoke-steg:** mät fri PSRAM **och största block**, intern heap,
rotations-/ritbufferstorlek och synthens faktiska samplestorlek före inträde, efter
rotation, vid bank-/låtstart och efter exit/fade, gärna över upprepade växlingar.
Först därefter besluta om bufferfrigöring, bibliotekscache eller layoutändring.
Inga nya mätvärden, frigjorda byte eller prestandavinster påstås i Fas A.

## Prioriterad backlog och kortaste säkra arbetsordning

**P0** = startspärr för meningsfullt fysiskt större-skärm-test om kontrollen fallerar,
inte automatiskt ett kodarbete i denna PR. **P1** = nästa avgränsade migrationsarbete.
**P2** = senare förbättring/utvidgning. En fast men fullt nåbar 320×480-yta på en
större skärm hindrar inte att en första baslinje tas; felmappad input eller ingen
säker väg tillbaka gör det.

| Ordning/prio | Arbete, risk och berörda filer | Beroende och tydliga acceptanskriterier |
|---|---|---|
| 0 / **P0, gemensam testspärr** | Bekräfta körbar P4-testmiljö, verklig logisk/fysisk upplösning, flush, touch och 0/180° innan UI-felsökning. **Hög.** `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/hw_profile.cpp`, `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/board_pins.h:20–21`, `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/board_display.cpp:635–643`, `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/main/board_touch.cpp:117–140`. | Befintlig boardkod använder fortfarande LCD-konstanter 320×480; profilval ensamt portar inte kortet. **Godkänt:** faktisk W/H och firmware dokumenterade, alla fyra hörn/mitt träffar rätt, meny/retur fungerar. Om inte: endast host-/320×480-baslinje tills separat bring-up löst det. **Ingen P4-portning/ny profil i Fas A/B1.** |
| 1 / **P0, fysisk startkontroll** | Äventyrs data, modalrouting och säker retur. **Hög.** A:2016–2043,2124–2267,2269–2325,2840–2922 samt AH. | Efter 0: giltigt SD-äventyr med kontrollerat innehåll; saknat SD ger läsbar hjälp/retur. **Godkänt:** platta/tap och STOP fungerar, ingen världsrörelse i dialog, långtryck ger inte oavsiktlig gång, meny→huvudmeny→återinträde utan kvarliggande blockerare/krasch. Upptäckt fel är blockerare för just det flödet; antas inte redan föreligga. |
| 2 / **P1, B1 först** | Äventyrs väljarramar, radbredder/text och vanliga dialogers maxmått/scroll; återanvänd AH där lämpligt. **Medel.** A:2340–2439,2690–2884; AH:8–73. | Efter inventering; kan utvecklas host-side före P4 bring-up. **Godkänt:** 320×480-flöde oförändrat, målprofil 480×800 utan överlapp; långa SV/EN-namn/beskrivningar/fel, full spelarlista, fortsätt/nytt/avbryt samt menyavslut nåbara. Canvas (0,0)/320², kontrollpanel/plattgeometri, kamerafunktioner och eventtyper lämnas exakt som förr. |
| 3 / **P1, B2 villkorad** | Äventyrs input-/hitboxkoppling vid **beslutad** flytt/storleksändring av spelvyn. **Hög.** A:52–65,740–922,2100–2267,2477–2657 och joystickheadern ovan. | Endast efter B1 och explicit viewportbeslut; B1 kräver inte detta. **Godkänt:** en gemensam geometri för canvasorigin/ritning/touch, skärm→lokal→värld korrekt även med icke-noll origin, kamera vid kartkant och 16/32-zoom. Plattans mitt/radie/stopzon/knopp och objekt-hitbox överens, 1–4 zoner, dominant/alternate, hold/release/press-lost och 400-ms väntande tapp behåller beteende. Ingen ny kalibrering, fysisk joystick, multitouch eller känslighetsändring utan separat behov. |
| 4 / **P1, berättelsetext** | Äventyrs samtalsbox/svarspanel, scroll/clipping. **Medel–hög.** A:1262–1539. | Efter B1, före/med B2 bara om spelviewporten berörs. **Godkänt:** längsta befintliga SD-nod och låsta svar/hints, skip/typewriter/sista svar/avslut nåbara på båda portraitstorlekar; ingen rörelse bakom, ingen fokusändring till styrplattan. Håll separat om B1 annars blir för stor. |
| 5 / **P1, C1 efter Äventyr** | Gemensam mini-layout för **Pig → Farkle → Skepp & kapten**, inklusive board/filt/info/knappar, tärningsplatser/nest och flash. **Medel**, Farkle högre hitboxrisk. D:2656–2880,3084–3226. | Återanvänd metrics och redan migrerade väljare/dialog/overlay; dela inte in Yatzys cupfysik. **Godkänt:** 1/2/5/6 tärningar och 1/8 spelare, korrekta klickplatser efter animation, läsbara totaler/chips/hints, Banka/Spara/Kasta nåbara vid 320×480 och målprofil; oförändrade kast/poäng/regler/tidsförlopp. |
| 6 / **P1, C2 eller liten del av C1** | Båda resultatvyerna `showResults/showMiniResults`. **Medel.** D:2594–2640,3229–3289. | Efter gemensamt mini-lager; motorkod orörd. **Godkänt:** 8 långa namn/rankningar, totaler, återspel och huvudmeny nåbara utan överlapp; ingen ny sessions-/poänglogik. |
| 7 / **P2, hjälpvyer** | Inställningar, Wi-Fi, klocksetter, boot, OTA och temaankare enligt tabellen ovan. **Låg–medel.** | Kan skjutas upp om navigationen är säker i första testet. Uppgradera till P0 för det faktiska testflödet om t.ex. nätval/retur blir oåtkomligt. **Godkänt:** kropp/footer anpassas med befintliga helpers, långa texter/scroll och touch utan regress; inga globala temabyte som oförutsett påverkar alla spel. |
| 8 / **P2, separat Karaoke-steg** | Karaoke-layout/minnesmätning, eventuell cache-/bufferlivscykel och senare syntharbete. **Hög resursrisk.** K, karaoke-/LVGL-/synthfilerna ovan. | Efter Äventyr och de tre tärningsspelen, enligt befintlig Karaokeplan. **Godkänt:** dokumenterad före/efter-mätning på befintlig S3 och senare P4, korrekt rotation/retur och fortsatt S3-funktion. OOM vid rotation/återgång som påverkar övriga spel lyfts till gemensam P0; full Karaoke-migrering är inte en förutsättning för första speltestet. |

### Avgränsade Äventyrs-ändringar

**B1 – säkra layoutändringar utan ändrad inputsemantik (genomförd):** väljarramar,
världs-/spelarrader, ny-profil-formulär och vanliga msgbox-dialoger använder
`UiMetrics`-mått i `main/adv_setup_ui.h` och `main/ui_adventure.cpp`. 320×480 behåller
de tidigare marginalerna, ramstorlekarna och knapphöjderna; bredare portraitlayout
får mer bredd/höjd och radbrytning för långa texter. Dialoginnehåll kan scrollas,
och listorna behåller vertikal scroll. Hosttesterna utökades för 320×480, 480×800,
SV/EN, långa texter och scroll till sista världs-/spelarraden.

**B2 – villkorad och inte nödvändig för B1:** ingen viewportflytt/-skalning eller
input-/hitboxjustering krävdes. Spelcanvas, joystick, tick-/modalrouting och
karttryckskoordinater är därför oförändrade. B2 är medvetet uppskjuten, inte
implementerad; en framtida viewportändring kräver en separat evidensbaserad ändring
och gemensam rit-/träffgeometri.

Berättelsesamtalets specialpanel är separat inventerat arbete och ingår inte i B1.
Ingen större framebuffer, ny långlivad cache, P4-portning eller fysisk P4-kontroll
ingår. `P4_43` 480×800 är endast hostmått från befintlig overifierad profil.

Om B2 senare blir nödvändigt: gör canvasorigin/invers transform och plattgeometri
atomärt tillsammans med ritningen. Lägg hostfall för översatt origin, kamera, zoom
och plattans gränser innan fysisk jämförelse. Behåll rörelsetider, zoninställningar,
tapp-/hold-/diagonalsemantik och handlingarnas eventtyper. Utöka inte spelkänslan
eller inputtyperna. En större framebuffer kräver separat minnes-/renderbedömning;
ingen sådan utökning måste hinnas före första P4-baslinjen.

## Smoke-testmatris för 28 oktober 2026

**Alla fysiska resultat nedan är ännu EJ KÖRDA.** Kör först på befintlig
**320×480** och därefter på P4 **endast om gemensam startspärr passerar**.
P4-målprofilen är 480×800 portrait (vid 90/270° skulle logisk storlek vara
800×480); verklig panel/upplösning bekräftas på plats. Nuvarande S3:s
Karaoke-baslinje är logiskt 480×320 efter rotation.
Ingen P4-firmware eller hårdvarufunktion levereras av detta dokument.

Förbered två språk (SV/EN), kort/långt spelarnamn, 1/8 spelare, giltigt
SD-äventyr med NPC/dialog och vid behov separat mina/bomb-karta, plus fall utan
SD/katalog. Använd **befintliga** testdata; anta inte att alla specialflöden
finns i varje SD-äventyr. Notera ”ej tillgängligt” i stället för godkänt.
För webb-overlay behövs separat ansluten och frånkopplad webbsits.

| Ordning och moment | 320×480: utför och jämför | Större P4: utför och bedöm | Stopp-/godkännandekriterium |
|---|---|---|---|
| S0 Boot/display/touch | Kallstart, hörn/mitt, systemrotation 0/180°, återställ 0. | Dokumentera faktisk panel, logisk W/H, profil/firmware, touch i hörn/mitt och flush. | **STOPP** vid felmappning, krasch eller ingen fungerande återväg. UI-layout bedöms först därefter. |
| S1 Navigation/text/knappar | Alla sex menykort, scroll till Karaoke, inställningar/Wi-Fi och tillbaka; SV/EN och långa titlar. | Samma; se om lista/text använder ytan men notera att fasta fonter kan vara små fysiskt. | Alla val/retur kan nås, inga blockerande överlapp; tom yta är observation, inte automatiskt fel. |
| S2 Äventyr setup | Välj katalograd, spelare/gäst/ny profil; tomt/dubblettnamn, feltext, spara/avbryt; fortsätt/nytt/avbryt. Prova saknat SD. | Samma inklusive nedersta listpost och långa namn/SD-beskrivningar. | Avslut nås, vald profil korrekt, ingen dubblerad handling eller blockerande keyboard/dialog. |
| S3 Äventyr spelvy/touch | Gå genom kartcentrum/kant, 32↔16-zoom, tryck egen figur/NPC/mål; long-press flagga i relevant karta. | Samma; tryck precis synlig cell efter kameraflytt/zoom och särskilt efter eventuell framtida viewportflytt. | Synlig cell och mål stämmer, flaggsläpp går inte, ingen avklippt kritisk HUD; ingen utlovad större viewport. |
| S4 Styrplatta/spelkänsla | Mitt/STOP, fyra riktningar, zonövergångar, hörn utanför ring, kort tapp, håll, drag, släpp utanför ytan; dominant/alternate med samma sparade tider. | Upprepa och jämför fysisk fingeråtkomst och takt med baslinjen. | Inga spöksteg/ny fart eller tappad riktning; stopzon och knopp matchar. Fysisk storlek/känsla protokollförs, inte antas från pixlar. |
| S5 Äventyr overlays/dialog | Öppna meny under/efter gång, avbryt/kryss/omstart; NPC-text skip, låst/aktivt svar och avslut; bombval/preview/avbryt/OK och resultat där data finns. | Samma med längsta tillgängliga text och sista knappen. | Världen tar inte dialogtouch, alla val/avslut nås, inga kvarvarande rörelser/overlays. |
| S6 Äventyr retur/livscykel | Meny→lämna→huvudmeny→nytt Äventyr minst tre gånger; prova även setup-back/resultat-retur. Vänta ut fade. | Samma; observera loggar/heap om tillgängligt, inte bara att skärmen ser stängd ut. | Ingen krasch, stale timer/canvas eller kvarliggande top-layer; minnesvärden registreras separat utan garanterad nollförbrukning. |
| S7 Pig (båda varianter) | Kasta/Banka, bust/flash, 1/8 spelare, avslut/återspel/meny. | Samma, kontrollera större tärningsbilder, score/knappar och långa namn. | Rätt knappar/träffytor, inga oavsiktliga hold-handlingar, läsbart och säkert returflöde. |
| S8 Farkle | Samtliga sex tärningar, välj/avvälj, låst/hållen/nest, kast/flash och Banka; webb-overlay. | Samma, även kantträff mellan täta tärningar. | Ingen granntärning togglas; låsta kan inte frigöras, överlagrad touch blockeras. |
| S9 Skepp & kapten | Fem tärningar, skepp/kapten/besättning/chips, last, Spara/Kasta, resultat. | Samma; text/last-hint och långa namn får inte täcka knappar. | Låst/valbar skiljs, alla kontroller/resultat/retur nåbara. |
| S10 Gemensamt/Yatzy regress | Variant/players/picker/filter, PIN fel/omförsök/avbryt, Yatzy cup/ROLL↔RESULT/table-scroll, avslutsdialog och ranking. | Samma; webbspel ansluten/frånkopplad→spela lokalt för alla tre mini-spel också. | Tidigare färdiga flöden förblir fungerande; fixa inte deras motor/nätprotokoll för layout. |
| S11 Orienteringsretur, avgränsad Karaoke | In/ut från låtlista, karaFlip båda lägen, 0/180° systemrotation; hörn/mitt, sheet/väntedialog. Starta också med ljud av: prova både Ja och Nej i startup-dialogen, sedan tillbaka till meny→Äventyr. Prova låtstart bara om data/bank finns och tid medger. | Samma **efter** spelbaslinjen; större landscape-UI förväntas ännu ha fasta 480×320-mått. | Portrait och touch återställda, inget dolt modalobjekt efter något av dialogsvaren. **STOPP för rotationsväxlingen** vid OOM/felmappning; ingen full Karaoke-/synthacceptans krävs eller påstås. |

**Kortaste praktiska körordning:** S0 → S1 → S2–S6 (Äventyr) → S7 → S8 → S9 →
S10 → S11. Vid knapp tid: ta baslinje och avvikelser hellre än att stressa in B2
eller minnesoptimeringar utan regressionskontroll.

Protokoll per körning: datum/enhet, firmware/profil, faktisk panel/logisk upplösning,
systemrotation/karaFlip, språk/spelarantal/testdata, S-id, förväntat/faktiskt,
pass/fail/ej körd, foto/logg och reproduktionssteg. Läsbarhet i fysisk storlek,
touchprecision, FPS/renderlatens, ljudunderruns och heap/största PSRAM-block kräver
**fysisk mätning**; inga acceptansvärden hittas på här.

## Kontroller och verifieringsgräns

Faktiskt körda befintliga motortester i Fas A, med binärer utanför repot:

| Test | Resultat | Vad det **inte** verifierar |
|---|---|---|
| `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/tests/test_joystick.cpp` | **62 passed, 0 failed** | LVGL-routing, fysisk styrplatta, rotation, fingerprecision. |
| `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/tests/test_cup.cpp` | **50 passed, 0 failed** | Mini-vyns rit-/touchgeometri på större panel. |
| `/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/tests/test_games.cpp` med Pig/Farkle/SCC-motorerna | **137 passed, 0 failed** | Skärmlayout, animation eller fysisk input. |

Kompilerat med befintligt README-upplägg, `g++ -std=c++17 -Wall -Wextra`, till
`/tmp/ui-inventory-checks/`. Dessa var Fas A-motortester och ändrade inte
produktions- eller testfiler.

För Fas B1 kontrollerades Python-syntax och patch whitespace. De utökade
`python3 tests/test_adv_setup.py` och `python3 tests/test_mine_dialog.py` försöktes,
men båda stoppade vid CMake-konfiguration eftersom
`managed_components/lvgl__lvgl` saknas i checkouten; inga LVGL-layoutpåståenden görs
från dessa försök. Firmwarebygge kunde inte köras (`idf.py` saknas).
Fysisk 320×480-/P4-testning och minnes-/FPS-mätning återstår.
Befintliga testkällor är stöd för kommande arbete, inte nya godkända testresultat:
`/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/tests/test_yatzy_layout.py:1–5,519–562`
extraherar produktionskod till temporär CMake/Ninja-byggnad;
`/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/tests/test_mine_dialog.py:14–25,165–181`
gör motsvarande för utvalda Äventyrsdialoger.
`/home/runner/work/MyEsp-OnTheRoad/MyEsp-OnTheRoad/tests/test_adv_setup.py:1–26`
extraherar också verkliga väljarknappar men låser nuvarande 296-px-baslinje;
dess förväntningar måste uppdateras avsiktligt i B1, inte kringgås.
