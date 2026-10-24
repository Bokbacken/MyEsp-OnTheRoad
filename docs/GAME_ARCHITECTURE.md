# Spelarkitektur (v0.6.0)

Det här dokumentet beskriver vilka egenskaper som skiljer tärningsspelen på MyEsp-OnTheRoad åt, hur
de är implementerade i dag och vad som måste vara konfigurerbart när fler spel (eller d8/d10) läggs till.
Tekniska termer står på engelska; resonemangen på svenska.

## 1. Översikt

| Lager | Fil(er) | Ansvar |
|---|---|---|
| Typer | `main/game_base.h` | `GameType` (YATZY / PIG / FARKLE / SCC), `PigVariant`, gemensamma konstanter, `rankPlayers()` |
| Spelmotorer | `game_yatzy.*`, `game_pig.*`, `game_farkle.*`, `game_scc.*` | Ren C++ utan LVGL/IDF – host-testade (`tests/test_yatzy.cpp`, `tests/test_games.cpp`) |
| Live-snapshot | `main/game_live.*` | UI-tasken publicerar spelet, webbservern läser. `Snapshot::gameType` + `union { PigGame; FarkleGame; SccGame }`, Yatzy som namngiven medlem. Kommandon från webben: `HOLD`, `ROLL`, `CHOOSE`, `BANK`, `END_TURN` |
| Device-UI | `main/ui_yatzy.cpp` | Tvånivåmeny (spel → variant), Yatzy-skärmar (orörda) och en gemensam "mini"-layout för Pig/Farkle/SCC |
| Webb-API | `main/app_web.cpp` | `/api/game` + WebSocket `/ws/game`. JSON har `game_type` och spelspecifika fält |
| Webb-UI | `main/web/play.html`, `game.html`, `style.css` | Dispatch på `game_type`. Bägarflödet (två steg) gäller enbart Yatzy |

Dispatch sker med `switch (GameType)` – inga virtuella funktioner ännu. Motorerna för Pig/Farkle/SCC är
*trivially constructible* (inga member initializers/konstruktorer) så att de får ligga i snapshot-unionen;
`static_assert(sizeof(Snapshot) < 2048)` vaktar storleken.

## 2. Egenskaper som måste vara konfigurerbara

| Egenskap | Betydelse | Värden i dag |
|---|---|---|
| `num_dice` | Antal tärningar i spel | 1, 2, 5, 6 |
| `num_sides` | Sidor per tärning | 6 (förberett: d8/d10 kräver nya sprites/3D-faces och scoring-tabeller) |
| `max_rolls_per_turn` | Kast per tur | 3 (Yatzy, SCC), ∞ (Pig, Farkle) |
| `can_hold` | Kan spelaren spara tärningar? | Yatzy/Farkle/SCC ja, Pig nej |
| `score_model` | Hur poäng uppstår | `category-based` (Yatzy), `running-total` (Pig, Farkle), `round-based` (SCC) |
| `win_condition` | När spelet tar slut | `all-categories` (Yatzy), `first-to-N` (Pig 100, Farkle 10000 + final round), `most-after-N-rounds` (SCC) |
| `turn_end_actions` | Hur turen avslutas | `forced` (bust / Farkle / välj ruta / 3 kast slut), `voluntary` (bank / spara last) |
| `dice_independence` | Vilka tärningar kastas om | `all` (Pig), `non-held` (Yatzy, SCC), `non-held + locked` (Farkle: undanlagda tärningar låses; hot dice frigör alla sex) |
| `variant_support` | Varianter per spel | se tabell nedan |

## 3. Spelen mot egenskaperna

| | Yatzy | Pig | Farkle | Ship/Captain/Crew |
|---|---|---|---|---|
| `num_dice` | 5 (Maxi: 6) | 1 eller 2 | 6 | 5 |
| `num_sides` | 6 | 6 | 6 | 6 |
| `max_rolls_per_turn` | 3 (+ sparade kast i Maxi/Midi) | ∞ | ∞ | 3 |
| `can_hold` | ja, fritt | nej | ja, bara poänggivande tärningar | ja, bara lasttärningar (6/5/4 låses automatiskt) |
| `score_model` | category-based | running-total | running-total | round-based |
| `win_condition` | alla rutor ifyllda, högst total | first-to-100 | first-to-10000, övriga får en sista tur | högst total efter N ronder (default 3) |
| `turn_end_actions` | forced (välj ruta) | voluntary (bank) / forced (bust) | voluntary (bank) / forced (Farkle) | voluntary (spara) / forced (3 kast) |
| `dice_independence` | non-held | all | non-held + locked | non-held |
| `variant_support` | Standard, Maxi, Midi × Fri/Ordning | 1 tärning, 2 tärningar | Standard | 3 ronder |
| Webbkommandon | `hold`, `roll`, `choose` | `roll`, `bank` | `hold`, `roll`, `bank` | `hold`, `roll`, `end` |

## 4. Per spel

### Yatzy (oförändrat, plus Midi)
- `YatzyVariant` STANDARD (5 tärningar) / MAXI (6), `YatzyMode` NORMAL (fri ordning) / ORDERED.
- **Midi Yatzy** = Standard + `savedRolls = true` (`reset(STANDARD, mode, true)`): oanvända kast sparas
  till senare turer som i Maxi. UI:t identifierar varianten via `trVariant(v, m, savedRolls)`.
- Webben: bägarflödet (Kasta → tryck på bägaren) är exklusivt för Yatzy.

### Pig
- `ONE_DIE`: en etta = bust, rundans poäng förloras.
- `TWO_DICE`: summan läggs till; en etta = bust, två ettor = spelarens hela total nollställs (`lostAll()`).
- `bank()` flyttar rundans poäng till totalen. First-to-100 avslutar spelet direkt.
- Device/webb visar bust i ~1,6 s (`hasBusted()`), därefter `passTurn()`.

### Farkle
- Scoring: 1 = 100, 5 = 50, tretal = face × 100 (ettor 1000), fyrtal = face × 200, femtal = face × 300,
  sextal = face × 400, stege 1–6 = 1500, tre par = 750.
- Varje kast måste ge minst en poänggivande tärning, annars Farkle (rundans poäng förloras).
- Valda tärningar (`held`) måste alla ge poäng (`heldValid()`); vid nästa kast låses de (`locked`).
- Hot dice: alla sex har gett poäng → alla sex kastas igen, rundans poäng behålls.
- Öppning: minst 500 p på en tur innan spelaren kommer in på tavlan. Vid 10000 får övriga en sista tur.

### Ship / Captain / Crew
- Tre kast. 6 (skepp), 5 (kapten), 4 (besättning) måste fås i den ordningen och låses automatiskt.
- Last (cargo) = summan av de två övriga tärningarna, bara om hela skeppet är komplett.
- Efter komplett skepp kan spelaren spara lasten (`endTurn()`) eller kasta om lasttärningarna.
  Efter tre kast avslutas turen automatiskt. Högst total efter N ronder vinner.

## 5. Webbprotokoll (sammandrag)

Gemensamt för alla spel: `game_type`, `name`, `current`, `finished`, `rolling`, `dice[]`, `players[]`
(`name`, `total`, `web`), `web_players[]`, `ranking[]` när spelet är slut.

| `game_type` | Extra fält |
|---|---|
| `yatzy` | `variant`, `round`, `rounds`, `rolls_left`, `held[]`, `categories[]`, `players[].scores/upper/bonus/saved_rolls`, `shaking`, `choosing` |
| `pig` | `pig_variant`, `target`, `turn_total`, `busted`, `lost_all`, `round`, `can_roll`, `can_bank` |
| `farkle` | `target`, `open_threshold`, `held[]`, `locked[]`, `turn_total`, `held_score`, `held_valid`, `farkled`, `hot_dice`, `final_round`, `can_roll`, `can_bank`, `players[].opened` |
| `scc` | `held[]`, `role[]` (0 last, 1 skepp, 2 kapten, 3 besättning), `rolls_left`, `ship`, `captain`, `crew`, `cargo`, `round`, `rounds`, `can_roll`, `can_end`, `players[].scores[]` |

Alla kommandon valideras mot `game_type` och aktuell spelare i `app_web.cpp` innan de köas till UI-tasken.

## 6. Nästa steg (förslag)
- En gemensam `GameRules`-struct (`num_dice`, `num_sides`, `max_rolls`, `can_hold`, …) som både UI och
  webb läser i stället för hårdkodade `switch`-grenar.
- d8/d10: kräver nya pip-/siffer-faces i LVGL-sprites och i webbens 3D-tärning, samt `num_sides` i RNG.
- Fler varianter: Farkle med valbart mål, SCC med 5/10 ronder (motorn stödjer redan upp till 10).
