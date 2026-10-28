# Uppdrag: Grupperade tryckplattor & grindar (Sokoban), Steg 1

> **Status:** Implementerat i PR #10, firmware v0.20.0. Detta dokument är det
> historiska kravunderlaget; aktuell `.adv`-syntax finns i
> [författarguiden](AUTHORING_ADVENTURES.md). Färgnycklar och S2-mekanismer
> tillkom separat i PR #11.

**Repo:** `MyEsp-OnTheRoad` (ESP-IDF 6.1, LVGL 9.6). Motor/uppspelare-repot.
**Mål:** Låt en enda `.adv`-värld innehålla flera oberoende Sokoban-rum genom att
koppla tryckplattor till specifika grindar via ett **grupp-id**. Idag är all
plattlogik global (en enda `gateOpen_`), så flera rum går inte att sekvensera.

> Detta är **Steg 1** och gäller **enbart plattor/grindar**. Färgade nycklar/dörrar
> är ett separat, senare steg och ska **inte** röras här.

---

## 1. Funktionskrav

### 1.1 Grupp-id
- Varje **tryckplatta** (`Tile::PLATE`) och varje **grind** (`Tile::GATE`) kan ha ett
  grupp-id i intervallet **0–35**.
  - `0` = dagens globala beteende (bakåtkompatibelt, standard för alla gamla banor).
  - `1–35` = oberoende grupper, i text skrivet som `1`–`9` följt av `A`–`Z`
    (`A`=10, `B`=11, … `Z`=35), skiftlägesokänsligt.
- Öppningsregeln blir **per grupp**:
  > Grupp *g*:s grindar är öppna när grupp *g* har minst en platta **och alla**
  > grupp *g*-plattor bär en sten. Grupp `0` fungerar exakt som idag.
- Grupper är helt oberoende: att lösa grupp 1 påverkar inte grupp 2.

### 1.2 Lagring av grupp-id (ingen ändring av sparformatet)
- Grupp-id lagras i den befintliga per-cell-byten `val_[y][x]`, som i dag är
  **oanvänd** på PLATE- och GATE-rutor. Ingen ny array, inga nya fält i spelarblocket.
- `val_` serialiseras redan per cell i `save()`/`restore()`, och grindstatus räknas
  redan om från plattornas stenstatus via `updateGates()` vid `restore()`.
  **Därför ska `SAVE_VERSION` INTE bumpas** — gamla sparfiler (allt `val_`=0 på
  plattor/grindar) läses som grupp 0 = oförändrat beteende.

### 1.3 Två författarsätt (båda ska stödjas)

**A) `@legend` (per-tecken-mappning, det eleganta sättet)**
Sektionen `@legend` finns redan inläst men ignoreras i dag. Implementera den så att
en rad mappar ett **kart-tecken** → ruta/objekt **+ valfritt grupp-id**. Exempel:
```
@legend
a = plate 1      # tryckplatta i grupp 1
A = gate 1       # grind i grupp 1
b = plate 2
B = gate 2
```
Nyckelord som minst måste stödjas i Steg 1: `plate <g>` och `gate <g>` där `<g>`
är `1`–`9`/`A`–`Z`. (Att utöka legend till andra rutor/objekt senare ska vara lätt,
men krävs inte nu.)

**B) `@plategroup` / `@gategroup` (koordinatlistor, i stil med `@dark`)**
Sätt grupp-id på celler som redan är plattor/grindar via kartbokstav (`o`/`=`):
```
@plategroup 1: 3,4 5,4 7,8
@gategroup 1: 9,4
@plategroup 2: 12,10 14,10
@gategroup 2: 16,10
```
Format per cell: `x,y` (samma koordinatkonvention som `@dark`). En cell som pekas ut
men inte är en platta/grind → fel med radnummer.

### 1.4 Krock-/felregler
- **Legend vinner över inbyggd glyph.** Om `@legend` definierar ett tecken som redan
  har en inbyggd betydelse (t.ex. `R`, `o`, `=`) används legend-definitionen.
- **Reserverade tecken får inte omdefinieras** → fel: `S` (startruta), mellanslag,
  samt rad-markörerna `@` och radslut. Försök att lägga t.ex. `S = plate 1` i legend
  ska ge fel med radnummer.
- **Dubbeldefinition i `@legend`** (samma tecken två gånger) → fel med radnummer.
- Grupp-id utanför `0–35`, eller en bokstav utanför `A–Z`/`1–9` → fel med radnummer.
- Alla fel rapporteras via den befintliga `setErr(err, errCap, lineNo, "...")`.

---

## 2. Bugg att åtgärda i samma PR: `#` inne i `@map`

**Problem:** I `main/adv_loader.cpp`, i grenen `if (sec == MAP)`, hoppas en kartrad
vars första icke-blanka tecken är `#` över som kommentar
(`else if (t[0] == 0 || t[0] == '#') continue;`). Det gör att en kartrad som är en
heltäckande mur (`####################`) **försvinner tyst**.

**Krav:** Inne i `@map` finns **inga kommentarer**. Regeln ska vara:
- En rad avslutar kartsektionen **endast** om den (efter trim) börjar med `@`.
- En **helt tom** rad hoppas över (ignoreras).
- **Alla andra rader är kartrader ordagrant**, inklusive rader som börjar med `#`.

Konkret: ta bort `#`-villkoret i MAP-grenen så att bara `t[0] == 0` hoppar över
raden, och `t[0] == '@'` byter sektion. (Kommentarer med `#` fortsätter fungera
som vanligt **utanför** `@map`.)

Lägg en kort rad i `docs/AUTHORING_ADVENTURES.md` som förklarar att `#` inte är en
kommentar inne i `@map`.

---

## 3. Berörda filer (vägledning, inte facit)
- `components/otr_engine/include/otr_world.h` — grupp-begrepp; byt den enda
  `bool gateOpen_` mot per-grupp-status (t.ex. en 36-bitars mask eller liten array),
  behåll `gatesOpen()` för grupp 0-fallet/HUD. Lägg ev. `gateOpenForGroup(g)`.
- `components/otr_engine/otr_world.cpp` — `load()` (läs grupp-id från `val_`),
  `updateGates()` (per grupp), `playerCanEnter()`/rendering-hjälpare som kollar om en
  grind är öppen ska använda cellens grupp, samt `restore()` (räkna om per grupp).
- `main/adv_loader.cpp` — implementera `@legend` (plate/gate), `@plategroup`/
  `@gategroup`, samt `#`-fixen i `@map`.
- `main/ui_adventure.cpp` — valfritt i Steg 1: rendera litet grupp-nummer/tint på
  plattor/grindar för läsbarhet i stora pussel. Får göras men är inte ett krav.

## 4. Bakåtkompatibilitet (måste hålla)
- Banor utan `@legend`/`@plategroup`/`@gategroup` beter sig **exakt** som idag
  (allt grupp 0, en global grind-status).
- Inga ändringar i `SAVE_MAGIC` eller `SAVE_VERSION`. Gamla sparfiler ska läsas
  oförändrat.
- `o` och `=` som kartbokstäver fortsätter betyda platta/grind i grupp 0.

## 5. Tester (host-tester, ingen hårdvara)
Lägg host-tester (samma stil som befintliga motor-tester) som verifierar:
1. Två grupper i samma karta: att täcka alla grupp 1-plattor öppnar **endast**
   grupp 1-grinden; grupp 2 förblir stängd tills dess plattor täcks.
2. Grupp 0 (`o`/`=` utan legend/grupp) beter sig som före ändringen.
3. `save()`+`restore()` på en karta med grupper återställer rätt grindstatus per
   grupp, och en sparfil skapad före ändringen (val_=0 på plattor) laddas som grupp 0.
4. `@legend`-krock: legend skriver över inbyggd glyph; omdefinition av `S` ger fel;
   dubbeldefinition ger fel.
5. `@map`-fix: en rad `####…` som första kartrad bevaras (kartans höjd/bredd stämmer).

## 6. Begränsningar / arbetssätt (viktigt)
- **Minimal ändring.** Rör inte orelaterad kod. Ingen refaktorisering utöver det som
  krävs för grupperna.
- **Ingen versionsbump av sparformatet.**
- Lämna **Minfält-/karaoke-/nyckel-dörr-kod orörd.**
- Bygg projektet och kör host-testerna; bumpa firmware-version enligt repots vana och
  skriv en kort CHANGELOG/commit-rad. Hårdvarutest görs av ägaren efteråt.

## 7. Öppen punkt (ägaren bekräftar vid behov)
- Krock-policyn är satt till **"legend vinner"**. Om ägaren hellre vill att varje
  krock mot en inbyggd glyph ska ge fel, är det en enrads-ändring i valideringen.
