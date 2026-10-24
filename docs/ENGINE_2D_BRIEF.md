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

## 5. Öppna frågor

- 16 eller 32 px som standard (beror på uppmätt fps och läsbarhet)?
- Styrning: räcker skärmplattan eller ska mobilen kunna vara handkontroll?
- Första riktiga berättelsen/miljön?
