# Minsvepar-design (`mine_level`) — MyEsp-OnTheRoad

> Auktoritativt designdokument för minsveparläget. Beskriver hur `mine_level`
> fungerar **idag**: enheten kör **enbart SD-banor**, och den inbyggda
> 1→12-kampanjen är vilande (dormant) och går inte att nå från menyn.
> Källor i koden anges per avsnitt så att dokumentet kan verifieras.

## 0. Sammanfattning i en mening

På en SD-bana är `mine_level` ett **läges- och svårighets-/etikettval i den
enskilda filen** — det slår på minsveparläget och sätter HUD-etiketten `N/12`.
Det **genererar inga minor** (dem ritar du själv) och det **driver ingen
progression** till nästa bana (det gör filordningen).

## 1. Två världar: inbyggd kampanj (vilande) kontra SD-banor

Historiskt fanns en inbyggd minsvepar-värld (`WORLD_MINES`, "Minfältet") med en
kampanj på 12 nivåer. Den koden finns kvar men är **vilande**:

- Menyn är helt SD-driven. De inbyggda världarna listas inte längre.
  (`main/ui_adventure.cpp`: *"built-in sessions (dormant; SD adventures only now)"*,
  *"built-in WorldId adventures are no longer listed"*.)
- Den inbyggda nivåkedjan 1→12 (`Session::next` → `World::loadMines(level+1)`)
  körs **aldrig** för SD-banor. (`main/ui_adventure.cpp`:
  *"Never run the built-in 1..12 chaining."*)
- `World::loadMines()`, tabellen `MINE_LEVEL[]` och `WORLDS[]` ligger kvar i
  `components/otr_engine/` mest för att host-testerna och den vilande
  Session-koden ska kompilera.

**Allt spelbart minsvepar-innehåll är alltså `.adv`-filer på SD-kortet.** De 12
gamla kampanjnivåerna finns exporterade som filer under
`docs/adventures/generated/minkampanj/` (`01_niva01.adv` … `12_niva12.adv`),
genererade av `tools/gen_minkampanj.cpp` från den riktiga motorn — byte-identiska
layouter med den gamla inbyggda nivån N, men nu vanliga SD-banor.

## 2. Vad `mine_level: N` gör på en SD-bana

Sätt i `@rules`:

```
@rules
mine_level: 1            # 1..12 slår på äkta minsvepare; 0 = vanlig karta
```

Alias (exakt samma sak som `mine_level: 1`):

```
@rules
minesweeper: true        # även "yes" / "1" accepteras
```

Effekter (`main/adv_loader.cpp`, `components/otr_engine/otr_world.cpp`,
`main/ui_adventure.cpp`):

- **Tvingar på dimma.** `fog` sätts alltid till `true` när `mine_level > 0`.
- **Slår på minsvepar-UI:t:** tryck för att **avtäcka** en ruta, tryck-och-håll
  (eller flagg-knappen) för att **flagga**. HUD-raden visar `N/12`, antal minor,
  flaggor och BOMB-knappen.
- **Vinstvillkoret byts ut:** du vinner när **varje ruta som inte är en mina** är
  avtäckt — inte när du når `X`. (Ett `X` på kartan är ofarligt och håller kartan
  giltig; minsvepar-vinsten utlöses först.)
- **Startremsan** (startrutan `S` plus de tre försörjningsrutorna till höger)
  avtäcks alltid direkt, även om en angränsande mina skulle stoppa flood-fill.

### 2.1 `mine_level` placerar INTE minorna

Detta är den vanligaste missuppfattningen. För en SD-bana anropas **aldrig**
`loadMines()`. Kartan laddas rakt av från dina glyfer, och **`m`-tecknen du
ritar ut ÄR minorna**. Siffran `N`:

- genererar inga minor,
- ändrar inte deras placering,
- delar inte ut bomber/dräkt (dem placerar du som glyfer, se avsnitt 4).

All svårighet mellan dina banor styr du genom **planens storlek och hur många
minor du ritar** — inte genom siffran.

### 2.2 Siffran driver INTE progressionen

När du vinner en SD-minsveparbana avancerar sessionen via `sdExitAdvance()`
(`main/ui_adventure.cpp`), som laddar **nästa fil i äventyrets ordning**
(`s_sd.hasNext()`), precis som att nå `X` i ett vanligt äventyr. På sista filen
markeras äventyret som klarat. Att bana 4 kommer efter bana 3 avgörs alltså av
**filordningen** (`01_`, `02_`, …), inte av att `mine_level` råkar vara högre.

## 3. Giltiga värden och kant-fall vid 12

- Giltigt intervall: **0..12**. Värden utanför ger parse-fel
  (`mine_level must be 0..12`). Validerat på två ställen som måste hållas i synk:
  `main/adv_loader.cpp` och `tools/adv_parse.cpp` (`Otr::MINE_LEVELS == 12`).
- **Varför just 12:** den inbyggda generatorn bygger banan i en fast buffert
  `char grid[26][33]` (`components/otr_engine/otr_world.cpp`). Nivå 12 är
  `{32, 26, …}` — exakt så stor bufferten rymmer. `MINE_LEVELS` är satt där den
  största inbyggda planen slutar passa. För SD-banor är detta bara den övre
  gränsen för etiketten; din egen kartstorlek begränsas av `.adv`-formatet, inte
  av den här bufferten.
- **Kant-fallet `mine_level: 12`:** vinst-/förlustdialogen tittar på siffran.
  `mineLevel == 12` ger **kampanjvinst-texten** (`ADV_CAMPAIGN_WIN`) i stället för
  den vanliga vinsttexten (`ADV_MINE_WIN`). (`main/ui_adventure.cpp`,
  `mineResult()`.) För SD avgör det **inte** om du går vidare (det gör
  filordningen), men sätter du `12` på en bana mitt i serien får spelaren fel
  vinsttext. **Spara `12` till din allra sista bana** — eller nå aldrig 12.

## 4. Teckenlegend i en minsveparkarta

Samma glyfer som vanligt (se `AUTHORING_ADVENTURES.md`, avsnitt 5):

| Tecken | Betydelse |
|:------:|-----------|
| `m` | mina (dold tills avtäckt/sprängd) |
| `u` | skyddsdräkt |
| `a` / `z` | bomb 3×3 / 4×4 (avtäcker/spränger ett område) |
| `.` | säker mark som ska avtäckas |
| `S` | start (startremsan runt `S` avtäcks direkt) |

## 5. Designmönster för flera banor

Tre fullt giltiga upplägg. Välj utifrån vad HUD-etiketten ska signalera — inte
utifrån svårighet (svårigheten bestämmer din minplacering oavsett).

| Upplägg | Vad siffran gör | När det passar |
|---|---|---|
| Samma `mine_level` på alla banor (t.ex. `1`) | HUD visar alltid `1/12` | Enklast — du struntar i etiketten och designar fritt |
| Stigande i klumpar, t.ex. bana 1–3 = `1`, 4–6 = `2`, … | HUD visar `1/12 … N/12`; ger känsla av "nivåklump" | Du vill att skärmen visar vilken nivåklump spelaren är på |
| Stigande 1→12 (en bana per nivå) | HUD visar `1/12 … 12/12` | En rak "kampanj"-känsla; lägg `12` sist (avsnitt 3) |

**Exempel (klumpupplägg, 25 banor):** `01`–`03` med `mine_level: 1`, `04`–`06`
med `2`, och så vidare. Fullt stödd — men kom ihåg att siffran då bara är en
HUD-etikett; den svårighetsökning spelaren känner skapar du med fler minor och
större planer i själva kartorna. Låt den sista banan ha det högsta talet och
reservera `12` för den om du vill ha kampanjvinst-texten.

## 6. Rekommenderade regler (vänliga och testbara)

Samma uppsättning som den exporterade `minkampanj` använder:

```
@rules
start_energy: 8
energy_cap: 20
lives: 3
mine_damage: 4
mine_level: N            # N = banans etikett/nivå
fog: true
carry: energy maxenergy lives suit
```

- `carry` styr vad som följer med till nästa bana (energi, maxenergi, liv, dräkt).
- Förlust (game over) startar om den **aktuella** banan.

## 7. Snabb begreppskarta

| Fråga | SD-bana (det som gäller) | Inbyggd kampanj (vilande) |
|---|---|---|
| Vad gör `mine_level`? | Läge + HUD-etikett/svårighetsparitet | Både räknare **och** väljer genererad layout |
| Vem placerar minorna? | Du (glyfen `m`) | Generatorn (`loadMines`) |
| Går den vidare till nästa nivå? | Ja — via **filordningen** (`sdExitAdvance`) | Via `Session::next` (siffran +1) — men nås ej |
| Nåbar från menyn? | Ja | Nej (dormant) |

## 8. Relaterade dokument

- `docs/AUTHORING_ADVENTURES.md` — fullständig `.adv`-författarguide (avsnitt 4b).
- `docs/ENGINE_2D_BRIEF.md` — motoröversikt (det inbyggda Minfältet markerat vilande).
- `docs/adventures/generated/minkampanj/` — 12 färdiga exempelbanor.
