# OnTheRoad Engine – 2D-äventyrsmotor (brief, kommentarer, PoC)

Status: v0.7.0 innehåller ett första **proof of concept** ("Testvärld", 60 × 60 rutor). Motorn är en
**separat modul** – tärningsspelen (Yatzy, Pig, Farkle, Skepp & kapten) är orörda.

## 1. Briefen i korthet

- Resurssnål, rutbaserad top-down-motor för äventyr/utforskning, inspirerad av Kroz: stenväggar,
  förstörbara väggar, vatten/lava, båt, Sokoban-block, grottnivåer.
- Vänlig ton; berättelser ur en persons perspektiv (ev. bibliska/historiska miljöer).
- Terräng (tiles) skild från objekt. Progression via world state: båt, verktyg, nycklar, flaggor.
- Världsstruktur WORLD → REGION → MAP → LAYERS; kartor i chunks från SD.
- Datadriven berättelse (`ON TALK / IF / SET / GOTO`).
- Tiled/TMX → PC-konverterare → kompakt binärformat. 16 × 16 eller 32 × 32 px rutor.
- LVGL för menyer, dialog och inventarie; egen renderare för rutvärlden.
- Ljud via ES8311; ev. röstkommandon (ESP-SR) som extra input. Plattformsläge ev. senare.

## 2. Kommentarer / bedömning

| Område | Bedömning |
|---|---|
| Repo | Samma repo, egen komponent `components/otr_engine` (ren C++, värdtestbar) + `main/ui_adventure.cpp` (LVGL-skal). |
| Rendering | En RGB565-canvas (320 × 320) i PSRAM, egen blitter per ruta – inga LVGL-objekt per ruta. Rita bara om när något ändrats. |
| Innehåll | Kartor/tilesets på FFat (9,9 MB) eller SD – inte inbakat i appen (utom testvärlden). |
| Verktyg | `tools/tmx2otr.py` (Tiled → kompakt format) – ej påbörjat. |
| Risker | QSPI-bandbredd vid helskärmsscroll (mät fps – därför statistik i PoC:n); SD och ljud ännu inte uppstartade; ESP-SR MultiNet stödjer engelska/kinesiska, inte svenska, och kräver modellpartition; ingen fysisk styrknapp (virtuell styrplatta / mobilen som handkontroll via befintlig WebSocket). |
| Upphovsrätt | Kroz släpptes som freeware/GPL 2009 – inspiration ok, kopiera inte kod eller grafik. |

## 3. PoC-plan

1. ✅ **Renderare + fps-mätning** – v0.7.0 (se nedan). *Mät på hårdvara.*
2. ✅ **Minimal värld** – gå, kollision, putta block, slå, nycklar/dörrar, båt, lava, tryckplattor/grindar.
3. ⏳ HW-bring-up: SD-kort (4-bit SDMMC, GPIO 2–7) och ES8311-ljud.
4. ⏳ Kartformat + `tmx2otr.py`, laddning från FFat/SD, flera kartor/grottnivåer.
5. ⏳ Dialog/berättelse (datadriven), inventarie, sparning.
6. ⏳ Riktiga tilesets (16/32 px) i stället för genererad grafik.

## 4. Vad v0.7.0 innehåller

- `components/otr_engine/` – `Otr::World` (terräng + objekt + spelare, händelseflaggor) och den inbyggda
  testkartan `TEST_WORLD` (60 × 60, ASCII-förklaring i `otr_maps.h`).
  Regler: putta stenar (sten i vatten → bro, i lava → basalt), lådor går sönder av ett slag, spruckna
  väggar efter två, nycklar öppnar dörrar, båt krävs för vatten, lava skickar tillbaka till start,
  alla tryckplattor belastade → grindarna öppnas. 13 ädelstenar.
- `tests/test_adventure.cpp` – 258 värdtester, inklusive en full genomspelning (13/13 ädelstenar).
- `main/ui_adventure.cpp` – meny­kortet **Äventyr (test)** i huvudmenyn:
  - 320 × 320 vy, mjuk kamera som följer spelaren (pixelinterpolering) och stannar vid kanterna.
  - Zoom: 32 px (10 × 10 rutor) eller 16 px (20 × 20).
  - Styrplatta (håll = fortsätt gå), **Slå**, **Zoom**; håll fingret i vyn för att gå mot det, tryck på
    figuren för att slå.
  - Animerat vatten/lava, glidande stenar, slageffekt, LED-blink vid händelser, meddelanderuta.
  - Statistik i rubriken: egna bildrutor/s, ms per ritning, LCD-fps.
  - Grafiken genereras vid start (platshållare); allt minne (canvas ~200 kB, sprites, värld) i PSRAM och
    frigörs när man lämnar vyn.

## 5. Beslut i v0.8.0

- **Styrning:** tryck i vyn = gå dit med A* (Manhattan-heuristik, binär heap, varje ruta avgörs högst en
  gång, 60 × 60 räcker gott). Figuren går ett steg i taget med samma animation som styrplattan. Ett tryck
  inom 1/3 ruta från kanten/hörnet på en gångbar granne snäpps dit, ett tydligt tryck på ett hinder ger
  "Dit kan jag inte gå". Tryck på figuren = slå. Styrplattan finns kvar och har **STOP** i mitten (avbryter
  vägen). Långt tryck i dimma = flagga.
- **Läge Pro/Assistent** (sparas i NVS, `adv_mode`, standard Assistent, ändras i startvyn och i menyn):
  Pro tar kortaste vägen, även genom lava. Assistenten söker först en helt säker väg, annars en väg med så
  få "mjuka" rutor som möjligt (straffkostnad) och stannar på sista säkra ruta: "Det ser farligt ut!"
  (dödlig lava), "Okänd mark - kan vara farlig" (dimma i Minfältet). Vatten utan båt i båda lägena: "Du
  behöver en båt för att komma dit" (stannar vid stranden). Lava som klaras med dräktens laddningar räknas
  som säker. Planeraren puttar aldrig stenar och förutser inte en båt på vägen; plockas båten upp på vägen
  planerar UI:t om en gång.
- **Energi:** start 8, tak 20, stapel uppe till höger. Lava kostar 3 energi per steg, en mina 4 (och
  exploderar, lämnar en krater). Dryck (standard +2, 1-3 per skatt) fyller på upp till max, hjärta (standard
  +1 max, 1-2) höjer max permanent och fyller på. Energi 0 → ett liv förbrukas, återuppstå vid senaste
  checkpoint (annars start) med full energi.
- **Liv:** Testvärlden oändliga (rubriken visar "använda liv"), Minfältet 3. Slut på liv → game over med
  **Börja om** / **Fortsätt** (laddar sparningen, eller återupplivar om ingen finns).
- **Skyddsdräkt:** laddningar = 3 + 1 per permanent extra maxenhet (max − startenergi). Med laddningar
  kostar ett lavasteg ingen energi; laddningen förbrukas före skada. Höjs max medan dräkten bärs förlängs
  den lika mycket. Laddningarna visas bredvid energistapeln.
- **Skatter** är data i kartan (`MapDef::treasures`): dryck, hjärta, dräkt, extraliv, nyckel, ädelsten
  under låda, buske, sprucken vägg, sten eller dörr – avslöjas när objektet försvinner/flyttas.
- **Minfältet** (40 × 30, 95 minor, dimma): besökt mark visar antalet minor runt omkring med klassiska
  siffror, nollor flood-avslöjas, flaggor med långt tryck, mål = utgången. Minorna visas vid game over.
- **Spara:** `/ffat/adv_<spelar-id>_<värld-id>.sav`, skrivs atomärt (tmp + rename). Format: 16 B header
  ("OTRS", version 1, kart-id, b, h, längd) + 32 B spelarblock (position, energi, max, liv, dräkt, nycklar,
  båt, ädelstenar, checkpoint, steg) + 7 B per ruta (terräng, objekt, skatt, dimma/flagga) + CRC32.
  Minfältet 8 452 B, 60 × 60 25 252 B. Fel magic/version/kart-id/storlek/CRC → filen avvisas ("Det sparade spelet
  kunde inte läsas - nytt spel") och nytt spel startas. Autosparning när man lämnar, vid checkpoint och vid ändring
  efter 60 s tomgång; "Spara" i menyn. Gäst sparas inte.
- **Minne:** canvas, sprites, värld, planerare, väg och sparbuffert (32 kB) i PSRAM, frigörs när vyn lämnas.

## 6. Öppna frågor

- 16 eller 32 px som standard (beror på uppmätt fps och läsbarhet)?
- Styrning: räcker skärmplattan eller ska mobilen kunna vara handkontroll?
- Första riktiga berättelsen/miljön?
