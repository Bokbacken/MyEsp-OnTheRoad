# ESP-gränssnitt: inställningsinventering och plan

Status: förslag och inventering, 2026-11-05. Ingen firmwarebeteendeförändring
ingår i denna planeringsleverans.

## Mål

Göra inställningar och vanliga kontroller lätta att hitta på små skärmar,
utan att tränga undan spelinnehåll eller tappa ett aktivt spel. Gränssnittet
ska fungera i både porträtt och landskap på ES3C35P, S3_35B och P4_43.

Två navigationsytor:

1. **Inställningar** från huvudmenyn: organiserade globala,
   spelkategori- och spelunika inställningar, plus administration.
2. **Utdragbar snabbpanel** från överkanten: en kompakt nivå för omedelbara
   reglage och status, samt en utökad nivå för kontextuella spel- eller
   systeminställningar.

## Grundmodell: inställningen är sanningen, menyerna är genvägar

En inställning har ett enda kanoniskt värde. Den kan visas på flera ytor,
men panel, huvudmeny, kategori och spel får inte skapa separata kopior som
driver isär.

Varje inställningspost ska beskriva:

- stabilt ID och typat värde (bool, val, heltal, text eller åtgärd)
- kategori och valfri underkategori
- ägarskap: global/enhet, spelkategori, enskilt spel eller aktiv session/låt
- visningsytor: huvudmeny, spelkategori, aktivt spel, snabbpanelens nivå 1/2
- aktiveringsregel: omedelbart, nästa tur/omgång/start eller omstart
- om bekräftelse, behörighet eller destruktiv varning behövs
- relevans per hårdvaruprofil och speltyp

**Inte JSON först.** Denna katalog är firmwaremetadata, inte en användarladdad
databas. Börja med den här mänskligt granskbara inventeringen och en liten
typad, statisk C++-katalog när UI:t ska drivas från metadata. Persistenta
värden ligger kvar i NVS genom `Config` eller i respektive spels befintliga
lagring; levande spel-/låtstillstånd ägs av spelsessionen. Undvik en generell
JSON-motor och dynamisk minnesallokering för att beskriva fasta reglage.

Kategorins värde bör vara standard för nya spel. Ett spelspecifikt val är ett
uttryckligt undantag, inte en andra oberoende inställningskopi. Pågående
sessioner använder snapshot eller sessionstillstånd där regeländring annars
skulle ändra spelet mitt i en tur. Värden som påverkar bara nästa omgång kan
ändras medan spelet fortsätter, men ska märkas med när de börjar gälla.

## Inventering av befintligt gränssnitt

| Område | Befintlig funktion och lagring | Föreslagen kategori/åtkomst |
|---|---|---|
| Skärm och läsbarhet | `brightness`, `screenZoom`, `textSize`, `rotation`, `screenOffSeconds`, `ecoSleep`; RGB-enable/brightness där kortet har LED. Inställningar > Användare; ljusstyrka och vila tillämpas omedelbart, zoom/text vid sparning och ombyggnad. Rotation är porträttvändning 0/180, inte allmän porträtt/landskapsväljare. | Global > Skärm och tillgänglighet. Ljusstyrka och zoom kan finnas i snabbpanel nivå 1. Andra val i nivå 2/Inställningar; rotation/viloläge ska inte förväxlas med appens spelorientering. |
| Ljud globalt | `soundOn` är master på/av, `masterVol` är ES8311 DAC-nivå, `sfxVol` är effektgain, `musicOn` stänger av bakgrundsmusik och `musicVol` styr dess gain. Inställningar > Användare; flera reglage förhandsvisas och sparas tillsammans. Statusradens ljudikon korttryck togglar `soundOn`, långtryck öppnar inställningar. Äventyr har en andra mute-knapp som också togglar samma master. | Global > Ljud; status-/snabbpanelens ljudikon är en omedelbar master-mute, aldrig en kanal-mute. Lägg till separat beständig `sfxOn`; behåll separata master-, musik- och effektvolymer. |
| Karaoke-ljud | Aktiv låt har separat MIDI/song-volym (UI-räckvidd 0–200); spelaren har också mastervolym. Låtpanelen har tempo, transponering, melodikanal/mute, låtunik SoundFont samt spara/återställ låtinställningar. SoundFont och röstgräns har UI. `karaRev`, `karaRevLvl`, `karaCho`, `karaChoLvl` och `karaWebStream` finns i konfigurationen, men någon lokal editor för dessa hittades inte i inställningsvyn; verifiera faktiskt redigeringsgränssnitt innan flytt. Volymreglage finns i liggande spelare och vissa val är låtunika. | Global master och kanal-mutes återanvänds. Karaoke > Aktiv låt äger låtvolym, tempo/transponering och låtunik SoundFont. MIDI/song-volym och master visas i snabbpanel när karaoke kör, med layout för både liggande och porträtt. |
| Spelmusik och ljudeffekter | `Sound::Music` är en blandad spel-/menykanal; effekter blandas separat. `musicOn` finns, men inget separat `sfxOn`. Spel startar musik per spelläge. Master stänger förstärkare/ljudutgång och gain utan att ändra kanalgains. | Spel > Ljud: bakgrundsmusik på/av, ljudeffekter på/av, relevanta nivåer. Master-mute är överordnad och återställer inte eller skriver över kanalernas på/av- eller volymval. |
| Wi-Fi, delning och radio | Statusrad visar Wi-Fi av/ansluten/signal; nätverksvyn hanterar sparade nätverk. Delningsikon visas kontextuellt i kompatibelt Yatzy-flöde. AP/Hotspot och BLE är ännu framtida arbete. | Global > Anslutning; nivå 1 visar status och snabb åtkomst, nivå 2 kan visa relevanta åtgärder. Skilj statusindikator från åtgärd; fråga före frånkoppling som avbryter nätverkssession. |
| Batteri och ström | Statusrad visar batteriprocent; långt tryck på batteri togglar energisparläge. Inställningar visar laddning, batteri och kalibrering; kalibrering döljs för PMIC. Skärm-vila nu, vilotid och energisparläge finns separat. | Global > Ström. Snabbpanel visar status och lämpliga strömreglage; kalibrering, historik och serviceinfo stannar i detaljerad vy. |
| System/administration | Språk, klocka/tidszon, batterikalibrering, touchdiagnostik, SD-status/remount, Wi-Fi-inställningar, omstart, power-logg och menyanimationer finns i Inställningar > Admin/Användare. PIN skyddar vissa administrativa operationer. | Global > System, nätverk, lagring och administration med behörighetsgräns. Destruktiva/serviceåtgärder aldrig bland direktreglage. |
| Yatzy/tärningsspel | Globala värden: standardvariant, resultatvy, Maxi sparade kast, kastmetod, tärningsanimation. Vissa ändras i inställningssidan; vald variant och spelare väljs vid spelstart. | Spelkategori > Tärning som standard för nya spel; specifika val i spelstart/aktiv session. Ange tydligt om aktiv omgång påverkas eller om värdet gäller nästa spel. |
| Äventyr | Joystickhastighet/zoner/diagonal är globala `Config`-värden. Assist/Pro ändras i äventyrsmenyn; zoom är lokal vykontroll. Äventyrets mute togglar global master. | Spelkategori/Äventyr > Kontroller, regler och visning. Kontroller/zoom kan vara omedelbara; regelval kan vara nästa session. Ljudmutes flyttas till gemensam modell. |
| Sudoku | Dead-zone är global `Config`; spelets paus, verktyg och återställning av sparad progress finns i Sudoku-vyn/inställningar. | Spelkategori/Sudoku > Touch och spelregler. Paus och återställning är session-/destruktiva åtgärder, inte vanliga reglage. |
| Multiplayer och spelare | Spelare hanteras i spelarkatalogen. Delningsstatus är kontextuell; multiplayer är inte en generell radioinställning. | Spelare och Spela tillsammans är separata destinations-/åtgärdsytor; snabbpanel ska inte skapa eller lämna spel tyst. |
| Navigation och status | Gemensam header har meny, masterljud, Wi-Fi, delning när tillgänglig och batteri. Back-knappar finns i headers, footers och spelverktygsrader. Exempelvis tar nätverksflödet tillbaka till huvudmenyn. | Gemensamma ikoner/placering och konsekvent Tillbaka-destination. Snabbpanel bevarar exakt underliggande vy och session vid stängning. |

### Fullständig inventering av `AppConfig`

Detta är persistence-inventering, inte påstående om att varje värde redan
har en synlig ESP-editor. Webb-API:t exponerar flera av de globala fälten;
delad persistence betyder att webben och ESP:n måste fortsätta läsa/skriva
samma kanoniska värde.

| Fält/fältgrupp | Ägare och föreslagen yta | Effekt / verifieringsnotering |
|---|---|---|
| `hostname`, `adminPin` | Global > Enhet/administration | Identitet och åtkomstpolicy; PIN ska inte exponeras som vanligt reglage. Kontrollera var namn och PIN faktiskt ändras. |
| `lang` | Global > Språk/tillgänglighet | Gränssnitt; aktuell ESP-UI visar språkvalet i inställningssidans editor. Språkvalet ska inte kräva admin-rättighet. |
| `brightness`, `screenZoom`, `textSize`, `rotation` | Global > Skärm | Ljusstyrka är live preview; zoom/text tillämpas när skärmen byggs om/sparas; rotation 0/180 är hårdvaruriktning, inte karaokeorientering. |
| `screenOffSeconds`, `ecoSleep`, `menuAnimations`, `ledEnabled`, `ledBrightness` | Global > Skärm/ström | Vila och RGB påverkar omedelbart eller efter spara; RGB-fält endast på profiler som har LED. |
| `soundOn`, `masterVol`, `sfxVol`, `musicOn`, `musicVol` | Global > Ljud; synliga i snabbpanel | Master och gain verkar direkt. `soundOn` är befintlig master mute; ingen separat beständig SFX-mute finns idag. |
| `defaultVariant`, `resultProtocolFirst`, `rollAnimMs`, `maxiSavedRolls`, `throwMode` | Spelkategori > Tärning | Standard/regel för nya spel eller enligt regelns nuvarande användningspunkt; kontrollera aktiv-sessionsemantik innan visning i pågående spel. |
| `joyZones`, `joySlowMs`, `joyFastMs`, `joyHoldMs`, `joyDiag`, `advMode` | Spelkategori > Äventyr/kontroller | Joystickparametrar är globala configvärden; `advMode` används som assist/pro i äventyr. Klassificera om nästa spel eller aktivt äventyr. |
| `sudokuDeadZone`, `sudokuHelpHints` | Spelkategori > Sudoku/touch | Dead-zone påverkar touchtolkning; `sudokuHelpHints` är legacy och hints visas inte längre. Behåll kompatibilitet men presentera inte ett dött val. |
| `karaFont`, `karaVoices`, `karaFlip` | Spelkategori > Karaoke | Standard-SoundFont/röstgräns och orienteringsriktning. `karaFlip` riktar endast karaoke-landskap; kontrollera hårdvarubegräns och låtval. |
| `karaRev`, `karaRevLvl`, `karaCho`, `karaChoLvl`, `karaWebStream` | Karaoke/ljud, redigeringsyta ej bekräftad | Fält finns sparade. Hitta ägare och faktisk redigeringsyta eller markera som service/legacy; webstream är också endpoint-/nätverksfunktion. |
| `tz` | Global > Tid | Gäller systemklocka efter spara; set-clock är en egen handling. |
| `powerLog` | Global > Ström/diagnostik | Loggpolicy, inte snabbreglage; fil-/lagringskrav. |
| `batteryScalePermille`, `batteryFullMv` | Global > Ström/kalibrering | Kalibrering endast för ADC-profiler; döljs när PMIC levererar direkt mätning. |

### Ljudsemantik som ska låsas före UI-implementation

- Statusradens ljudikon korttryck = **Master mute / stör ej**. Den tystar
  enhetens lokala ljud direkt men behåller mastervolym och kanalval.
- Separat bakgrundsmusik-Mute påverkar bara musikkanalen; separat
  ljudeffekt-Mute påverkar bara SFX-kanalen. Båda har oberoende beständigt
  av/på-läge samt volymgain.
- Master mute ligger ovanpå båda kanalerna. När den slås på/av ska kanalernas
  ljud återgå till sitt tidigare val; ingen slider eller kanalstatus skrivs om.
- Karaoke behåller separat MIDI/låtvolym och mastervolym; channel-mute för
  spelmusik ska inte råka muta den aktiva MIDI-låten. Bekräfta om MIDI ska få
  en separat kanalmute eller om master + låtvolym räcker innan kodning.
- Föreslagen bakåtkompatibilitet: nuvarande `soundOn` fortsätter motsvara
  master på/av och `musicOn` musik på/av. Ny SFX-mute får ett säkert standard-
  värde "på" för befintliga användare. Spara inte gain noll som ersättning
  för mute; volym och mute är olika tillstånd.
- Alla ändringar sparas på samma ställe som motsvarande befintlig inställning
  och kontroller på flera skärmar läser samma källa.

## Snabbpanelens beteende

1. En nedåtgest som börjar i en smal, reserverad övre kant visar nivå 1.
   Ytterligare drag nedåt expanderar nivå 2; gest uppåt eller tryck utanför
   stänger. Trösklar mäts med relativa skärmproportioner och testas på varje
   upplösning, inte hårdkodade telefonmått.
2. Nivå 1 är kompakt och kontextuell: ljusstyrka, en volymvy anpassad till
   aktivt läge, master-mute, Wi-Fi/BLE-/delningsstatus, batteri och vid behov
   vila. Visa bara kontroller som fungerar i den aktuella kontexten.
3. Nivå 2 visar inställningar för aktivt spel om det pågår; från huvudmenyn
   visas relevanta globala kategorier. Administrativa och destruktiva
   operationer kräver explicit navigering/behörighet, inte en oavsiktlig
   gest.
4. Panelen är overlay över befintlig LVGL-vy, inte navigation till en ny
   skärm. Stängning avslutar overlay och återgår till samma spelobjekt,
   timers, scrollposition, musikposition och val.
5. Touch i overlay ägs av panelen. Gesture-recognizer får inte utlösa samtidigt
   en spelhandling, scrolla listan under panelen eller stjäla sliderdrag.
   Start från aktiva textfält, sliders och karaokeverktyg behöver särskilda
   regressionsfall.
6. Ge alternativ synlig knapp/menyväg till panelens viktigaste funktioner där
   en gest kan vara svår, och behåll god tillgänglighet för stora texter.

## Planerade PR-steg

### PR A — Inventering och UX-kontrakt (denna PR)

- Denna kodnära inventering; bekräfta och förfina listan för alla spel och
  hardwareprofiler.
- Besluta kanoniskt ID, ägarskap, ytor och aktiveringsregel för varje värde.
- Lås mute-/volymsemantik och om MIDI ska ha separat mute.
- Skissa panelens korta/långa nivåer och navigerings-/back-kontrakt.
- Ingen settingsmigration eller synlig firmwareförändring.

### PR B — Typad katalog och ljudmodell

- Lägg till en liten statisk, typad beskrivning av inställningsmetadata och
  kategorifiltrering; behåll befintliga NVS-nycklar och läs-/skrivfunktioner.
- Lägg till separat SFX-mute med bakåtkompatibel default och tester för
  master > kanal > gain-prioritet.
- Ersätt dubblerad mutelogik i statusrad och äventyrsmeny med gemensamma
  operationer. Uppdatera web-inställningarnas mappning om samma ljudvärden
  delas, utan att göra om webbnavigeringen.

### PR C — Snabbpanel i två nivåer

- Bygg overlay och gesture-state-machine; först nivå 1 med ljusstyrka,
  master mute, volym och aktuell status.
- Lägg till nivå 2 för kontextuella spelreglage och relevanta
  systemkategorier.
- Prova separat i porträtt och landskap, på alla tre kort och med rullning,
  spelinput, tangent/slider, lång text och ljudspelning.

### PR D — Inställningsnavigation och aktivt spel

- Ordna huvudmenyn i globalt, spelkategori, spel och administration.
- Lägg till gemensam destinationsstack/Tillbaka-kontrakt.
- Koppla aktiva spelinställningar till session eller framtida start enligt
  klassificering; navigation till panel/inställning får aldrig återinitialisera
  spelet.
- Visa omedelbar, nästa omgång eller omstartspåverkan före spara.

### PR E — Migrering och konsistens

- Migrera återstående nuvarande reglage ett område i taget, inte som en enda
  stor omskrivning.
- Behåll NVS- och web-API-kompatibilitet; låt webbytan fortsätta redigera
  samma kanoniska värde.
- Städa gamla genvägar först när ersättningen är fysiskt accepterad.

## Acceptanskriterier

- Alla befintliga `AppConfig`-fält och spelets lokala inställningar har
  inventerad ägare, kategori, visningsyta, persistence och effekt-timing.
- Huvudmeny, spelkategori, aktivt spel och snabbpanel ändrar inte separata
  kopior av samma inställning.
- Korttryck på ljudikonen ger omedelbar master mute. Separat musik- och
  SFX-mute bevarar respektive gain; mastermute ändrar inte deras status.
- Karaoke master/MIDI-volym fungerar i landskap; snabbpanel fungerar både
  över karaoke i landskap och spel/UI i porträtt.
- Att öppna/stänga panel och ändra en live-inställning bevarar aktiv Yatzy-,
  Sudoku-, äventyrs-, multiplayer- och karaoke-session enligt respektive
  klassificering.
- Ingen gest aktiverar både panel och spel-/scrollhandling; det finns alternativ
  åtkomst till viktiga funktioner.
- Profilspecifika kontroller är dolda eller tydligt otillgängliga när
  hårdvaran saknas. Textstorlek/zoom, språk, mörka/ljusa indikatorer och
  felmeddelanden fungerar i båda orienteringarna.
- NVS uppgradering, befintliga web-inställningar och återgång efter avbruten
  inställningsredigering är täckta av tester.
- PR byggs för ES3C35P, S3_35B och P4_43; hårdvaruprovning redovisas per kort,
  orientering och spel, inte bara som ett generellt godkännande.

## Utanför scope

Ingen förändring av spelregler eller multiplayerprotokoll, ingen ny
AP/Hotspot/BLE-implementation, ingen webb-UI-omdesign och ingen automatisk
skärmrotation. Sådana ytor kan länka till samma inställningsvärde eller
åtgärd när deras separata arbete planeras.
