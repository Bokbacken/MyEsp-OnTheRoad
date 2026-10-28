# Äventyrsmotorn – funktionsskanning & framtida uppgraderingar

Jämförelse mot klassiska 2D rutnätsmotorer (Chip's Challenge, Boulder Dash,
Sokoban-varianter, Zelda-likes, Baba Is You, enkla roguelikes). Listan tar **bara**
upp sådant motorn *inte* redan har.

**Svårighetsskala (för just denna kodbas):**
`1` = trivialt, en glyph/regel. `2` = liten motor- eller loaderändring, host-testbar.
`3` = ny mekanik men ryms i befintliga strukturer. `4` = rör sparformat / UI / flera
system. `5` = stort, ny delsystemsarkitektur (t.ex. realtid/AI).

## Motorn har redan
Rutnätsgång, skjuta sten (sokoban), sten→bro/basalt, grupperade tryckplattor/grindar,
spakar och tidsgrindar, is och transportband, generiska och färgade nycklar/dörrar,
par-bedömning, dimma/minsvepare, minor, bomber, båt/vatten, lava, skyddsdräkt,
energi/liv/checkpoints, dolda skatter, mörker+ficklampa, hoppstylta (pogo-vault),
NPC-dialog med flaggor/räknare/quests/villkor/effekter och utgång.

---

## Rörelse & terräng

| # | Funktion | Vad det ger | Svårighet |
|---|----------|-------------|:--------:|
| 3 | **Enkelriktade rutor / hål** | Kan bara passeras åt ett håll, eller faller ned (nivåskifte/fälla) | 2 |
| 4 | **Teleport-par (portaler)** | Två rutor som länkar ihop; redan nära `@legend`-tänket (id-par) | 3 |
| 5 | **Rörlig plattform på vatten/lava** | Flytande yta som bär spelaren – mer dynamik än statisk bro | 4 |

## Pusselmekanik

| # | Funktion | Vad det ger | Svårighet |
|---|----------|-------------|:--------:|
| 8 | **Räknardörr** | Dörr som kräver N ädelstenar/föremål istället för nyckel | 2 |
| 9 | **Riktade/tyngdstyrda block** | Boulder Dash-stenar som faller när stödet tas bort | 4 |
| 10 | **Färgkodade block+mål** | Skjut rätt färgs låda till rätt färgs mål (sokoban-variant) | 2 |

## Fiender & fara

| # | Funktion | Vad det ger | Svårighet |
|---|----------|-------------|:--------:|
| 11 | **Patrullerande fiende (fast mönster)** | Rör sig deterministiskt per spelarsteg; turbaserat, passar motorn | 3 |
| 12 | **Jagande fiende (enkel AI)** | Går mot spelaren (BFS/Manhattan) – motorn har redan A*-plannern att återanvända | 3 |
| 13 | **Projektiler / fällor** | Pilfälla, eldpelare som tänds i intervall | 3 |
| 14 | **Enkel strid / HP på fiende** | Slå-knappen finns redan; ge fiender träfftålighet | 3 |
| 15 | **Giftig/skadande terräng (ej lava)** | Taggar, träsk som tär energi – nästan gratis via Rules | 1 |

## Föremål & ekonomi

| # | Funktion | Vad det ger | Svårighet |
|---|----------|-------------|:--------:|
| 16 | **Bärbart/aktiverbart föremål (inventory-slot)** | Plocka upp, använd aktivt senare (ej bara passivt) | 4 |
| 17 | **Huvudnyckel** *(redan påtänkt)* | Öppnar vilket lås som helst – liten påbyggnad på färgnycklar | 2 |
| 18 | **Köp/sälj hos NPC (valuta)** | Ädelstenar som valuta; dialogsystemet kan redan ge/ta | 3 |
| 19 | **Krukor/kärl med slumpdropp** | Slå sönder → slump på innehåll (variation) | 2 |
| 20 | **Kombinerbara föremål** | Craft 2→1 i dialog/meny | 4 |

## Värld & författande

| # | Funktion | Vad det ger | Svårighet |
|---|----------|-------------|:--------:|
| 21 | **Kartövergångar i samma .adv** | Flera rum/kartor i en fil, kantdörrar mellan dem (ni har redan SD-session mellan filer) | 4 |
| 22 | **Namngivna checkpoints / varp** | Hoppa till utpekad checkpoint via effekt (story-driven) | 2 |
| 23 | **Villkorliga rutor** | Ruta ändras när en flagga sätts (t.ex. bro byggs av story) – `world VERB` finns redan | 3 |
| 24 | **Slumpmässig utplacering** | Markera ett område, motorn strösslar föremål/fiender – rejält omspelsvärde | 3 |
| 25 | **Legend för alla rutor/objekt** | Utöka S2-legendens begränsade urval till all terräng/objekt + id | 2 |

## Presentation & känsla

| # | Funktion | Vad det ger | Svårighet |
|---|----------|-------------|:--------:|
| 26 | **Enkel animering (vatten/eld/fiende)** | 2-bilders cykel ni redan har mönster för (`S_WATER0/1`, `S_LAVA0/1`) | 2 |
| 27 | **Mjuk kamerapanorering** | Glid mellan rutor istället för hopp – ren UI-polish | 3 |
| 28 | **Väder/ljuslägen per karta** | Natt/dis utöver `@dark` – stämning | 3 |
| 29 | **Rikare HUD (minikarta/quest-logg)** | Översikt i stora banor | 3 |
| 30 | **Fler ljudeffekter per event** | Event-biten finns; mappa fler Sfx | 1 |

## Progression & meta

| # | Funktion | Vad det ger | Svårighet |
|---|----------|-------------|:--------:|
| 32 | **Tidtagning/bestatid** | Spara bästa tid per bana | 2 |
| 33 | **Samlarmål / bonusstjärnor** | Hitta alla skatter = stjärna; delmål utöver utgången | 2 |
| 34 | **Ångra drag (undo)** | Sokoban-must för svåra pussel; kräver tillståndsstack (PSRAM finns) | 4 |
| 35 | **Nivåväljare med framsteg** | Lås upp banor i följd, visa klarade | 3 |

---

## Mina topp-rekommendationer (bäst värde för insatsen)
- **Ångra drag (#34)** – det enskilt mest efterfrågade i seriösa sokoban-banor; dyrare
  men lyfter hela pusseldelen.
- **Patrull/jagande fiende (#11/#12)** – A*-plannern finns redan att luta sig mot, ger
  motorn en helt ny dimension (action/stealth).
- **Teleport-par (#4)** – bygger vidare på grupp-id-tänket och kan ge nya pussel.

Allt ovan är host-testbart utan hårdvara, i linje med "minimal ändring, kompilera, testa".
