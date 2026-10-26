# Plan: bibliskt äventyr med dialoger

Status: **steg A + B byggda i v0.11.0 (HW-testade av Kent)**, live view på webben i v0.11.1 (ej HW-testad), resten plan. Första versionen av planen: Skrivet 2026-10-25 efter v0.10.0 + HUD-commit `40ea276`.
Uppdaterad 2026-10-25 med Kents riktlinjer för text, namn och utseende (avsnitt 1.2–1.5), och samma dag
med Kents beslut om de öppna frågorna (avsnitt 5). v0.11.0 är HW-testad: Byn och dialogerna fungerar mycket bra.
Bygger vidare på [ENGINE_2D_BRIEF.md](ENGINE_2D_BRIEF.md) (Kroz-inspirerat, vänlig ton) och
[COMPONENTS.md](COMPONENTS.md) (ny komponent `otr_story`).

### Levererat i v0.15.0 (ej HW-testat)
Steg C är **delvis implementerat**: `main/adv_session.*` ger Byn ↔ Ängen med bevarade kartändringar,
säker spawn utanför portalen och samlad v2-sparning av karta, berättelse och inventory. V1-sparningar
kan migreras; för Byn krävs även en giltig gammal `.sts`. Uppdragslogg och generell HUD-konfiguration
är **senare arbete**, inte leveranskrav för denna version. Inget Josef-kapitel eller nytt bildmaterial ingår.
MineSweeper-kampanjen är separat; dess regler och begränsningar står i ROADMAP:s v0.15.0-avsnitt.
De äldre versionsrubrikerna nedan är historiska planeringsmål, inte leveransstatus.

## 1. Idé i korthet

Spelaren går omkring i en bibelmiljö, möter personer och väljer att prata med dem. Varje replik ger
**upp till 4 svarsalternativ**. Valen kan föra samtalet vidare, ge information eller föremål, öppna nya
vägar, eller leda bort från målet. Personerna minns vad man sagt (flaggor), och vissa svar kräver att man
har något med sig eller har gjort något tidigare (villkor). Mellan samtalen finns utforskning och pussel
med motorn vi redan har: putta stenar, nycklar/dörrar, båt, vatten och samla saker.

### Beslutat: Josefs resa (1 Mos 37–50)
Varför Josef: många personer att prata med, tydliga platser (Kanaan → karavan → Egypten), naturliga
uppdrag (hitta bröderna, sköta Potifars hus, tyda drömmar, samla korn) och ett budskap om trofasthet och
förlåtelse som fungerar för alla åldrar. Inga strider mot människor behövs. Slå används bara på
föremål: krukor, buskar och lådor.

| Kapitel | Plats | Kärna (bibeltext) | Spelinnehåll |
|---|---|---|---|
| 1 | Hebron → Sikem → Dotan | 37:1–17 | Jakob ger manteln, drömmarna, skickas till bröderna. Mannen på fältet visar vägen (37:15–17). Utforskning, första dialogerna |
| 2 | Dotan | 37:18–36 | Brunnen, Ruben och Juda, karavanen. Händelsen är fast, men valen påverkar vad Josef har med sig |
| 3 | Potifars hus | 39:1–20 | Sköta förråd (puttpussel), förtroende-räknare. Potifars hustru vill att Josef ska "umgås med henne en stund". Moraliskt val: att fly. Leder till fängelset oavsett |
| 4 | Fängelset | 40 | Munskänken och bagaren: välj tydning bland 4 med ledtrådar. Känd vers citeras (40:8) |
| 5 | Faraos palats | 41 | Faraos drömmar, sju goda år: samla korn till förråden (räknare + karta) |
| 6 | Hungersnöden | 42–45, 50:20 | Bröderna kommer, silverbägaren i Benjamins säck, förlåtelsen och återföreningen med Jakob |

**Alternativ** om du hellre vill: David (herde → Goliat → hovet), Rut (kort, fint, mycket dialog),
Mose/uttåget (stort, mycket miljöer) eller Daniel. Motorn blir densamma oavsett berättelse.

### 1.1 Principer för berättelsen
- **Huvudhändelserna följer bibeltexten.** Valen styr *hur* man tar sig fram, vad man får veta och vad man
  har med sig, inte utgången.
- **Vänlig ton**, inga skrämmande scener, ingen grafisk våldsskildring.
- **Heltokiga val** (sådant som går emot texten) ger en mild tillrättavisning, t.ex. *"Det där var väl inte
  så smart?"*, och man får välja om. Mindre bra val ger omvägar eller förlorad hjälp.
- **Dör man under ett uppdrag** (terrängfaror) visas en varning, och sedan fortsätter man. Ingen game over
  som tvingar fram omstart i berättelsen.
- **Målgrupp: alla.** Enkelt språk och korta repliker.

### 1.2 Bibeltext (Kents riktlinjer)
- **Översättning: Nya världen-översättningen (NWT)**, den svenska utgåvan för svenska och den engelska
  för engelska.
- **Kända texter citeras ordagrant** ur NWT, till exempel 1Mo 40:8 och 50:20. Citatet visas i en egen ruta
  med referensen under.
- **Referenser** skrivs med NWT:s egna boknamn och förkortningar: "1 Moseboken 37:15–17" i uppdragsloggen,
  och den korta formen ("1Mo 37:15–17") i dialogrutan. På engelska blir det "Genesis 37:15-17" och "Ge 37:15-17".
- Allt annat är **egen dialog** som håller sig till händelserna i texten.

### 1.3 Dialogspråk
- **Modernt talspråk**: *dom* i stället för de/dem, *ska* i stället för skall, *sa* i stället för sade,
  *mig/dig* som vanligt, korta meningar. Ingen ålderdomlig stil utanför citaten.
- **Känsliga formuleringar skrivs barnvänligt.** "Ligg med mig" blir "håll mig sällskap" eller "umgås med
  mig en stund". Samma princip gäller överallt: innebörden ska finnas kvar, men orden ska passa barn.
- Engelska: samma princip, med vardaglig modern engelska.
- Manusverktyget får en **stilkontroll** som varnar för *de/dem* (som pronomen), *skall*, *sade*, *icke* och
  liknande i dialograder. Citatrader är undantagna.

### 1.4 Namn (Kents namnpolicy 2026-10-25)
- **Kända namn** används som i NWT: Josef, Jakob, Ruben, Juda, Simeon, Benjamin, Potifar, Asenat, Farao.
- **Personer utan namn i Bibeln får sin titel** som namn. Påhittade namn används bara när en titel inte
  räcker (t.ex. flera bybor eller tjänare med samma roll).
- **Potimor och de tidigare namnförslagen (Skänkhotep, Degmose, Nyckelhotep, Stigur, Kryddib, Säckhotep)
  utgår.**

| Roll (bibeltext) | Svenska | Engelska |
|---|---|---|
| Potifars hustru (1Mo 39) | **Potifars hustru** | **Potiphar's wife** |
| Överste munskänken (1Mo 40) | **Munskänken** | **The Cupbearer** |
| Överste bagaren (1Mo 40) | **Bagaren** | **The Baker** |
| Fängelsets föreståndare (1Mo 39:21) | **Fängelsechefen** | **The Prison Keeper** |
| Josefs förvaltare (1Mo 44:1) | **Förvaltaren** | **The Steward** |
| Mannen på fältet vid Sikem (1Mo 37:15) | **Mannen på fältet** | **The Man in the Field** |
| Köpmannen i karavanen (1Mo 37:28) | **Köpmannen** | **The Merchant** |
| Bybor och tjänare | Titel, eller korta påhittade namn när titeln inte räcker | |

  Om påhittade namn behövs kontrollerar manusverktyget dem mot en lista över **alla namn i Bibeln** och
  varnar om namnet redan finns där.

### 1.5 Utseende
- Personer, kläder och miljöer tar **inspiration från barn- och ungdomsmaterialet på jw.org**: *Lärdomar från
  Bibeln*-bilderna och *Bli Jehovas vän* (Kaleb och Sofia). Det gäller färger, dräkter, huvudbonader,
  skägg/frisyrer, Josefs randiga mantel, egyptiska kläder och miljöer.
- Materialet är **inspiration och stilreferens**. Vi ritar egen pixelgrafik och egna porträtt i samma
  anda, så att figurerna känns igen till typ och stämning.

### 1.6 Upphovsrätt och det publika repot
jw.org:s användarvillkor tillåter personligt bruk. Däremot får material därifrån (bilder, text, musik)
**inte distribueras i eller med en programvara eller publiceras på internet**. GitHub-repot är
**publikt**, så:
- **Bilder:** egna ritningar inspirerade av materialet, så de påverkas inte.
- **NWT-citat:** läggs **inte** i repot. **Beslutat (tills vidare):** citaten ligger i en lokal fil
  (`story/quotes_sv.txt`, `story/quotes_en.txt`) som står i `.gitignore`. Den byggs in i Kents firmware om
  den finns, eller läses från SD (`/sdcard/otr/adventure/stories/quotes_*.txt`). Saknas filen visas bara
  referensen ("Läs 1Mo 40:8"). Manuset i repot innehåller då bara nyckeln: `@quote 1Mo40:8`.
- Alternativet att göra repot **privat** (då kan citaten ligga i manuset) är inte aktuellt nu.

## 2. Hur dialogen fungerar

```
┌──────────────────────────────┐  y 0
│   karta (320 × 320)          │
│ ┌──────────────────────────┐ │
│ │[porträtt] RUBEN          │ │  dialogruta ovanpå kartans nedre del
│ │ "Josef! Vad gör du här   │ │  (skrivmaskinseffekt, tryck = visa allt)
│ │  ute alldeles ensam?"    │ │
│ └──────────────────────────┘ │
├──────────────────────────────┤  y 320
│ 1  Jag letar efter er.       │  kontrollpanelen (160 px) byts mot
│ 2  Har ni hört min dröm?     │  4 svarsknappar à 36 px, hela bredden
│ 3  Vart ska ni med fåren?    │
│ 4  [Gå därifrån]             │
└──────────────────────────────┘  y 480
```

- **Starta ett samtal:** gå in i personen, eller stå vänd mot personen så att Slå-knappen byter till
  **Prata**. Tryck-för-att-gå på en person = gå dit och prata.
- **Svarsknappar:** 1–4 st. Ett alternativ som kräver något man saknar visas grått med en
  ledtråd, eller döljs (författaren väljer per alternativ). Alla knappar har dubbeltryckskyddet.
- **Personer minns:** startnoden väljs efter villkor, till exempel första mötet, efter ett uppdrag eller med
  rätt föremål.
- **Effekter av ett val:** sätt/rensa flagga, ändra räknare (t.ex. förtroende, korn), ge/ta föremål,
  starta/avsluta uppdrag, öppna port, flytta person, byt karta, spela ljud, visa bibelreferens.
- **Uppdragslogg** i hamburgermenyn: aktuellt mål + referens + hittills avklarat.
- **Sparning:** samtalsflaggor, räknare och uppdrag sparas med världen (sparformat v2).

### Manusformat (författarvänligt)
Manus skrivs som textfiler i repot och konverteras med ett Python-verktyg till C++-tabeller, på samma sätt som
`embed_web.py`. Verktyget validerar också manuset. Senare kan samma filer läsas direkt från SD
(`/sdcard/otr/adventure/stories/`).

```
@npc ruben "Ruben" portrait=ruben sprite=shepherd
@npc munskanken "Munskänken" portrait=cupbearer sprite=egypt_servant

== ruben.start  ? !met_ruben
Ruben: Josef! Vad gör du här ute alldeles ensam?
* Jag letar efter er. Far vill veta hur ni har det.   -> ruben.far      {set met_ruben}
* Har ni hört min dröm om kärvarna?                   -> ruben.drom     {add broder_ilska 1}
* Vart ska ni med fåren?                              -> ruben.faren
* [Gå därifrån]                                       -> END

== ruben.start  ? met_ruben & has:mantel
Ruben: Göm den där manteln, lillebror. Den gör dom bara argare.
* Varför då?                                          -> ruben.mantel
* [Gå därifrån]                                       -> END

== josef.tydning
Munskänken: Vi har drömt, både jag och bagaren. Men det finns ingen som kan säga vad det betyder.
@quote 1Mo40:8                                        (NWT-citat från den lokala citatfilen)
* Berätta din dröm för mig.                           -> skank.drom
* [Gå därifrån]                                       -> END
```

Validering (både i verktyget och i hosttest): alla `->`-mål finns, varje nod går att nå, varje kapitel kan
slutföras, max 4 alternativ, texten får plats (rader × tecken för font 14) och alla flaggor/föremål är
deklarerade. Dessutom stilkontroll av talspråket (1.3), namnkontroll mot bibelns namn (1.4) och att varje
`@quote` finns i citatfilen (varning, inte fel, om filen saknas).

## 3. Tekniska steg

### Steg A – `otr_story` (ren C++, hosttestad) · v0.11.0 ✅ (182 hosttester)
- Datamodell: `Npc`, `Node` (talare, text-id, villkor), `Choice` (text-id, villkor, effekter, nästa),
  `Cond` (flagga, räknare ≥/≤, har föremål, uppdragsstatus), `Effect` (se ovan).
- `StoryState`: 256 flaggor (bitset), 16 räknare, 32 föremål, 16 uppdrag. Serialiseras.
- `Dialog`-körare: `start(npc)`, `choices()`, `choose(i)` → nästa nod eller slut + effekter.
- Hosttester: villkor, effekter, nodval, slutnoder, serialisering och ett litet testmanus.

### Steg B – Motor och UI · v0.11.0 ✅ (HW-testat av Kent)
- `Obj::NPC` med person-id och riktning. Personen blockerar rutan och kan flyttas av effekter. Enkel
  "titta mot spelaren"-animation.
- Kontextknapp: Slå → **Prata** när man står vänd mot en person.
- Dialog-UI i `ui_adventure.cpp` (eller egen `ui_dialog.cpp`): dialogruta + porträtt + 4 knappar,
  skrivmaskinseffekt och dialogljud ("blip").
- Demokarta "Byn" med 3 personer som visar alla funktioner. Ingen bibeltext ännu, bara mekaniken.
- **Gjort:** Bullbert (bagaren), Fårhild (herden), Gamle Brunolf (gubben vid brunnen) i
  `components/otr_story/otr_story_village.cpp` (handskrivet manus i C++ tills `story2c.py` finns).
  Dialogrutan ligger i `ui_adventure.cpp`; berättelsestatus sparas som `.sts` bredvid `.sav`
  (sparformat v2 kommer i steg C).

### Steg C – Flera kartor, uppdrag, sparformat v2 · v0.13.0
- Världar med flera kartor (utgångar/dörrar → annan karta + startpunkt). En karta åt gången i PSRAM.
- Sparformat **v2**: dagens block + `StoryState` + aktuell karta. v1-sparningar läses fortfarande för de
  gamla världarna.
- Världsdefinitionen anger vilka **HUD-räknare** och **föremål** som visas (t.ex. korn, silvermynt,
  mantel), så att den nya HUD:en i `40ea276` kan återanvändas.
- Uppdragslogg i hamburgermenyn.

### Steg D – Innehållsverktyg och grafik · v0.13.0
- `tools/story2c.py`: manus → C++-tabeller + validering, och `--check` för CI.
- Kartor: Tiled (TMX) → C++, med konverterare enligt 2D-briefen. Fram till dess fungerar textrader som i dag.
- Nya rutor och sprites: öken/sand, tält, palmer, brunn, lerhus, palatsgolv, kornbod, kameler, får.
  Personer: herde, köpman, soldat, tjänare, farao, Jakob, Josef (med/utan mantel). Porträtt 48 × 48.
- **Stil enligt 1.5.** Först görs ett stilark med Josef, Jakob, en bror, Potifar och en egyptisk tjänare.
  Det visas för Kent och godkänns innan resten görs (beslut 6). Samma sak gäller de första
  kapitelrundorna (manus och karta).
- Två vägar: **procedurellt i kod** som dagens sprites (snabbt, enhetligt), eller **genererad
  pixelgrafik** som konverteras till RGB565 och läggs i flash/SD (snyggare porträtt). Förslag: porträtt
  genereras, rutor görs procedurellt.
- Ny musik: "Kanaan" (pastoral) och "Egypten" (modal skala), med samma chiptune-motor eller WAV på SD.

### Steg E – Kapitel 1 som vertikal skiva · v0.13.0
Hebron → Sikem → Dotan, ca 8–10 personer och 2–3 pussel. Det spelbara kapitlet testas på enheten innan resten
byggs. Det är här vi får veta om text, knappar och tempo känns rätt på 3,5".

### Steg F – Kapitel 2–6, SD-laddning, engelska · v0.14+
Ett kapitel per version. Manus kan läsas från SD (`stories/`), så att nya kapitel inte kräver ny firmware.
Engelska (dialog i vardaglig engelska, citat ur NWT EN) läggs till när svenskan är klar för ett kapitel.
Strukturen stöder redan text-id per språk.

## 4. Minne och prestanda (uppskattning)
- Manus: ca 20–40 kB text per kapitel i flash. Appen har ca 1,2 MB ledigt av 3 MB, så hela Josef ryms.
- `StoryState` < 200 B. Porträtt 48 × 48 RGB565 = 4,6 kB styck i PSRAM.
- Dialog-UI ritas med LVGL ovanpå canvasen. Kartrenderingen pausas medan dialogen är öppen.

## 5. Beslut

### Klart (Kent 2026-10-25)
- Bibeltext: NWT (SE/EN), kända texter citeras, och referenser visas (1.2).
- Dialog i modernt talspråk (dom, ska) med barnvänliga omskrivningar (1.3).
- Utseende inspirerat av jw.org:s barn- och ungdomsmaterial; egna ritningar (1.5).
- Båda språken (SE och EN).

### Kents beslut om de tidigare öppna frågorna (2026-10-25)
1. **Första berättelse: Josef.**
2. **Trohet mot texten.** Huvudhändelserna är fasta. Heltokiga val ger en mild tillrättavisning
   (*"Det där var väl inte så smart?"*) och man får välja om.
3. **Dör man under ett uppdrag** visas en varning, sedan fortsätter man.
4. **NWT-citaten** ligger i en extern, gitignorerad fil tills vidare (1.6).
5. **Målgrupp: alla.** Enkelt.
6. **Stilark/grafik och de första kapitelrundorna visas för Kent först**, innan resten görs.
7. **Namnpolicy:** personer utan namn i Bibeln får sin titel (Bagaren/The Baker, Munskänken/The Cupbearer,
   Potifars hustru/Potiphar's wife, Fängelsechefen/The Prison Keeper, Förvaltaren/The Steward). Påhittade
   namn bara när en titel inte räcker. Potimor och de tidigare namnförslagen utgår (1.4).

### Öppet
1. **Flerspelare via webben:** flera personer följer äventyret i webbläsaren (grunden finns i v0.11.1:s
   live view, `/adventure`) och röstar, t.ex. majoritetsröst vid dialogval, kanske också om vägval. Frågor:
   röstningstid, vad som händer vid lika röster, om enheten alltid kan avgöra, och om röster ska krävas
   PIN som i `/play`.
2. **MIDI-musik:** en SMF-parser (Standard MIDI File, typ 0/1) som spelar via den egna synten med ca 8
   polyfona röster, så att musik kan skrivas i vanliga verktyg och läggas på SD. Frågor: instrumentmappning
   (General MIDI → fyrkant/triangel/brus/enkla vågformer), CPU/minne i mixertasken, och om MML ska finnas kvar
   som reserv.
3. **Grafikteknik:** procedurella rutor + genererade porträtt (förslag), godkänns via stilarket (beslut 6).
