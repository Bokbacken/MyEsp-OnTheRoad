# Skriva egna äventyr i ASCII (.adv)

Det här dokumentet visar hur du skriver ett helt äventyr till MyEsp-OnTheRoad
i ett enkelt, läsbart textformat. Tanken är att man ska kunna **skapa och
redigera egna äventyr direkt på SD-kortet** utan att röra C++-koden: du ritar
kartan med ASCII-tecken och skriver dialogen som text på svenska och engelska.

> **Nytt i v0.17.0:** Äventyr läses nu **direkt på enheten** från SD-kortet.
> Lägg dina `.adv`-filer under `/otr/adventure/` så dyker de upp i
> äventyrsmenyn automatiskt. De inbyggda (kompilerade) äventyren listas inte
> längre i menyn – allt som visas kommer från SD-kortet. Se avsnitt 1.
>
> Övriga nyheter i v0.17.0, alla dokumenterade nedan:
> - `@about` – det SD-menyn visar (titel/beskrivning/författare/omslag) — avsnitt 3
> - Kartor upp till **80×80** med **gångbar ram** — avsnitt 5
> - `@treasures` – dolda skatter under täckande objekt — avsnitt 8
> - `@resources` – egna samlingsbara föremål (glyf → bild + tak) — avsnitt 9
> - `carry`/`reset` i `@rules` – vad som följer med mellan nivåer — avsnitt 4
> - Egna **32×32 PNG-sprites** i `bitmaps/` — avsnitt 11
>
> **Nytt i v0.18.0** (avsnitt 10):
> - `@dark` – mörka områden som bara lyses upp av ficklampan — avsnitt 10
> - **Ficklampa** (`f` / `@treasures ... flashlight N`) – lyser upp mörkret i en kon framåt
> - **Hoppstylta** (`j` / `@treasures ... pogo N`) – studsar automatiskt över hinder

Formatet är avsiktligt **nära motorns verkliga datastrukturer** så att det är
realistiskt importbart:

| Del i `.adv`-filen | Motsvarar i koden | Fil |
|--------------------|-------------------|-----|
| `@about`           | `Adv::Header` (SD-menyns metadata) | `main/adv_loader.h` |
| `@map` + `@rules`  | `struct MapDef` / `struct Rules` | `components/otr_engine/include/otr_world.h` |
| teckenlegenden     | `decode()` (tecken -> `Tile`/`Obj`) | `components/otr_engine/otr_world.cpp` |
| `@treasures`       | `Otr::Treasure` (dolt lager i `World`) | `components/otr_engine/include/otr_world.h` |
| `@resources`       | `Adv::Resource` + föremålssystemet | `main/adv_loader.h` |
| `@flags/@counters/@items/@quests/@npc/@node` | `struct Script` m.fl. | `components/otr_story/include/otr_story.h` |

> Två kompletta, fungerande exempel finns att utgå från:
> - [`docs/adventures/example_joseph_hebron.adv`](adventures/example_joseph_hebron.adv) – den verkliga Hebron-kartan ur Josef-kapitlet (1 Mosebok 37).
> - [`docs/adventures/example_resources_demo.adv`](adventures/example_resources_demo.adv) – ett litet demoäventyr som visar **alla** nyheter i v0.17.0 (`@about`, `@treasures`, `@resources`, carry/reset och gångbar ram).
>
> Dessutom finns alla fyra inbyggda äventyr exporterade till `.adv` under
> [`docs/adventures/generated/`](adventures/generated/) – kopiera en av dem som
> startpunkt för ett eget äventyr.

---

## 1. Så hittas äventyr på SD-kortet

Äventyrsmenyn skannar en bestämd katalog på SD-kortet:

```
/otr/adventure/
├── josef/                 # en mapp = ett äventyr (flera kartor)
│   ├── 01_lager.adv
│   ├── 02_vag.adv
│   ├── bitmaps/           # valfria egna 32x32 PNG-sprites
│   │   └── robe.png
│   ├── cover.png          # valfritt omslag (se @about: cover)
│   └── saves/             # skapas av enheten – rör inte
├── byn/
│   └── 01_byn.adv
└── stenhuggaren/
    ├── 01_stig.adv
    └── bitmaps/
```

Reglerna menyn följer:

- **En mapp under `/otr/adventure/` = ett äventyr.** Mappnamnet är äventyrets
  interna id och det som sparfiler knyts till (byt aldrig namn på en mapp om du
  vill behålla sparningar – t.ex. måste Josef-mappen heta `josef`).
- **Filerna i mappen är kartorna (nivåerna), i bokstavsordning.** Döp dem med
  ett numrerat prefix – `01_...adv`, `02_...adv` – så spelas de i den ordningen.
- **Menytiteln och beskrivningen hämtas från den *första* kartans `@about`**
  (avsnitt 3). Saknas `@about` används mappnamnet.
- Undermapparna `saves/`, `maps/` och `bitmaps/` hoppas över vid skanningen.
- Hittas inga äventyr visar menyn ett meddelande om att SD-kortet saknar
  `/otr/adventure/` (strängarna `ADV_NO_ADVENTURES` / `ADV_NO_ADVENTURES_HELP`).

En `.adv`-fil är vanlig text (UTF-8, max 64 KB) och består av **sektioner** som
börjar med `@`. Rader som börjar med `#` är kommentarer. Tomma rader ignoreras
överallt *utom* inuti `@map`, där varje rad är en kartrad.

Sektionerna, i ordning:

```
@meta        id, värld-id, kart-id
@about       titel, beskrivning, författare, omslag (det SD-menyn visar)
@rules       spelregler (energi, liv, lava, dimma) + carry/reset
@map         själva kartan, ritad med ASCII-tecken
@dark        mörka områden som ficklampan lyser upp (valfri, v0.18.0)
@treasures   dolda skatter under täckande objekt (valfri)
@resources   egna samlingsbara föremål kopplade till glyfer (valfri)
@flags       deklarera alla flaggor du använder
@counters    deklarera alla räknare
@items       deklarera föremål (id + namn SV/EN)
@quests      deklarera uppdrag (id + namn SV/EN)
@npc <id>    en person (upprepas per person)
@node <id>   en dialograd (upprepas många gånger)
@legend      informativ teckenreferens (valfri, parsas ej)
```

---

## 2. @meta

```
@meta
id: 01_lager             # internt id för kartan
world: 4                 # WorldId 1..255 (Josef = 4)
map_id: 4                # stabilt kart-id som sparfiler använder
```

`world` är världens id. `map_id` är kartans stabila id som sparfilen refererar
till – varje karta i samma äventyr måste ha ett unikt `map_id`. (I Josef:
lägret = 4, vägen = 5.) För ett helt eget äventyr, välj ett `world` som inte
krockar med de inbyggda (1..4) – t.ex. 90 och uppåt.

---

## 3. @about – det SD-menyn visar

`@about` är **nytt i v0.17.0** och är den metadata äventyrsmenyn läser. Lägg det
i äventyrets **första** karta (`01_...adv`); menyn tar titel och beskrivning
därifrån.

```
@about
title.sv: Stenhuggarens stig
title.en: The stonemason's path
desc.sv: Plocka stenar, ge en till byggaren och öppna stigen.
desc.en: Gather stones, give one to the builder and open the path.
author: Ditt namn
cover: cover.png         # valfritt: 32x32 PNG i äventyrsmappen
```

| Fält | Visas som | Max |
|------|-----------|-----|
| `title.sv` / `title.en` | rubrik i menyn (språk efter enhetens inställning) | 79 tecken |
| `desc.sv` / `desc.en`   | beskrivning under rubriken | – |
| `author`                | upphovsperson | – |
| `cover`                 | filnamn på omslagsbild i äventyrsmappen (valfritt) | 39 tecken |

Saknas den engelska varianten faller menyn tillbaka på den svenska.

---

## 4. @rules – spelregler

Fältnamnen motsvarar exakt `struct Rules` i `otr_world.h`:

```
@rules
start_energy: 8          # energi vid start (och första maxvärdet)
energy_cap: 20           # hur högt max-energin kan växa med hjärtan
lives: infinite          # "infinite" eller ett tal 1..9
lava_damage: 3           # energi per lava-steg utan dräktladdning
mine_damage: 4           # energi per mina
suit_base_steps: 3       # dräktladdningar vid upplockning ...
suit_per_extra_max: 1    # ... + per permanent extra energienhet
potion_default: 2        # energi en trolldryck ger utan eget värde
heart_default: 1         # max-energi ett hjärta ger utan eget värde
mine_level: 0            # 0 = vanlig karta. 1..12 = ÄKTA minsvepare (avsnitt 4b)
fog: false               # true = marken dold tills avtäckt (tvingas på av mine_level)
carry: energy, maxenergy, items, lives   # vad som FÖLJER MED till nästa nivå
reset: suit                              # vad som NOLLSTÄLLS vid nivåbyte
```

### carry / reset – status mellan nivåer (nytt i v0.17.0)

När spelaren går från en karta till nästa i samma äventyr bestämmer `carry` och
`reset` vad som förs över. Ange en kommaseparerad lista med nyckelord. Det som
inte nämns i `carry` nollställs; `reset` är ett sätt att uttryckligen nollställa
något (de två får överlappa, `reset` vinner).

| Nyckelord | Betyder |
|-----------|---------|
| `energy` | aktuell energi |
| `maxenergy` | permanent max-energi (hjärtan) |
| `items` | story-föremål och resurser i inventariet |
| `lives` | liv (använda/kvar) |
| `suit` | dräktladdningar (lavaskydd) |
| `keys` | nycklar |
| `gems` | ädelstenar – **förs aldrig över** (nollas alltid mellan nivåer) |
| `boat` | båt |

Story-föremål, flaggor, räknare och uppdrag matchas **på namn** mellan kartor,
så samma `@items`/`@flags`-id i nästa karta behåller sitt värde när `items`
ingår i `carry`. (Så fungerar Josef: `carry: energy, maxenergy, items, lives`
och `reset: suit`.)

### 4b. Äkta minsvepare – `mine_level` (nytt)

Normalt är en SD-karta ett vandringsäventyr: du går till `X` för att klara den,
och `fog: true` döljer bara marken tills du går dit. Vill du i stället ha ett
**riktigt minsveparläge** – tryck för att avtäcka rutor, flagga minor, spräng
med bomber och vinn när alla säkra rutor är avtäckta – sätter du:

```
@rules
mine_level: 1            # slår på äkta minsvepare, nivå 1..12
```

eller aliaset (exakt samma sak som `mine_level: 1`):

```
@rules
minesweeper: true        # även "yes" / "1" accepteras
```

**Vad `mine_level` gör:**

- **Tvingar på dimma.** `fog` sätts alltid till `true` när `mine_level > 0`, så
  du behöver inte ange `fog` själv (gör gärna ändå för tydlighet).
- **Slår på minsvepar-UI:t:** tryck på en ruta för att **avtäcka** den, tryck
  och håll (eller flagg-knappen) för att **flagga** en misstänkt mina. HUD:en
  visar nivå `N/12`, antal minor och flaggor samt BOMB-knappen.
- **Vinstvillkor byts ut:** du vinner när **varje ruta som inte är en mina** är
  avtäckt (inte när du når `X`). En avtäckt mina = träff (kostar `mine_damage`
  energi / ett liv).
- **Nivåtalet** `N` (1..12) ger HUD-indikatorn och startremsan samma beteende som
  den inbyggda kampanjen; värden utanför 0..12 ger ett parse-fel.

**Teckenlegend i en minsveparkarta** (samma glyfer som vanligt, avsnitt 5):

| Tecken | Betydelse |
|:------:|-----------|
| `m` | mina (dold tills avtäckt/sprängd) |
| `u` | skyddsdräkt |
| `a` / `z` | bomb 3×3 / 4×4 (avtäcker/spränger ett område) |
| `.` | säker mark som ska avtäckas |
| `S` | start (startremsan runt S avtäcks direkt) |

**Flera banor på rad (kampanj).** Lägg flera `.adv`-filer i samma mapp
(`01_...`, `02_...`, ...) precis som vilket annat SD-äventyr som helst. När
spelaren **vinner** en minsveparbana går sessionen vidare till nästa fil i
ordningen – samma mekanism som att nå en `X`-utgång i ett vanligt äventyr – och
på sista banan markeras äventyret som klarat. Ge varje fil sitt eget
`mine_level` (t.ex. stigande 1→12). Förlust (game over) startar om den aktuella
banan som vanligt.

**Rekommenderade regler** (vänliga och testbara, används av `minkampanj`):

```
start_energy: 8
energy_cap: 20
lives: 3
mine_damage: 4
mine_level: N            # N = banans nivå
fog: true
carry: energy maxenergy lives suit
```

Du kan fortfarande ha ett `X` på kartan – det är ofarligt (minsvepar-vinsten
utlöses först) och håller kartan giltig. Den medföljande kampanjen `minkampanj`
(12 banor, genererad av `tools/gen_minkampanj.cpp`) är ett komplett exempel.

---

## 5. @map – rita kartan

Kartan är ett rutnät, en rad per textrad, **ett tecken per ruta**. Alla rader
måste vara **lika långa** (det blir kartans bredd) och antalet rader blir
höjden.

**Gränser (från motorn, `otr_world.h`):**
- Max bredd **80**, max höjd **80** (`MAX_W` / `MAX_H`) – höjt från 64 i
  v0.17.0. Kartrutorna ligger i PSRAM.
- Exakt **ett** `S` (startruta) krävs.
- Minst ett `X` (utgång/mål) om kartan ska kunna klaras.
- Högst **9 talbara personer** per karta (siffrorna `1`..`9`).

### Gångbar ram (nytt i v0.17.0)

Kartans ytterkant behöver **inte** längre vara en mur. Du kan göra hela ramen
gångbar (t.ex. gräs) – motorn hindrar ändå rörelse *utanför* kartan via sin
`inside()`-kontroll, så spelaren kan aldrig gå utanför rutnätet. Vill du ha en
synlig mur ritar du den fortfarande med `#`/`T`; det är nu ett designval, inte
ett krav.

### Teckenlegend

Varje tecken avkodas till en terräng (`Tile`) och ev. ett föremål (`Obj`),
exakt som `decode()` i `otr_world.cpp`:

**Terräng**

| Tecken | Betydelse | Gångbar? |
|:------:|-----------|:--------:|
| `.` | gräs | ja |
| `,` | sand | ja |
| `_` | stengolv | ja |
| `#` | vägg | nej |
| `%` | sprucken vägg (gå sönder efter 2 slag) | nej |
| `T` | träd | nej |
| `~` | vatten | nej (bro/båt krävs) |
| `^` | lava | ja men skadar |
| `o` | tryckplatta | ja |
| `=` | grind | öppnas av tryckplatta |
| `Q` | checkpoint (återkomstpunkt) | ja |
| `X` | utgång (mål) | ja |
| `S` | start (står på gräs) | ja |

**Föremål** (ligger på en terräng; stor/liten bokstav = gräs/stengolv)

| Tecken | Betydelse |
|:------:|-----------|
| `R` / `r` | sten på gräs / golv (knuffbar) |
| `c` / `C` | låda på gräs / golv (slås sönder) |
| `k` / `K` | nyckel på gräs / golv |
| `D` | låst dörr (på golv) |
| `B` | båt (på sand) |
| `*` / `+` / `$` | ädelsten på gräs / golv / sand |
| `b` | buske (ett slag) |
| `p` | trolldryck (energi) |
| `h` | hjärta (höjer max-energi) |
| `u` | skyddsdräkt (lava) |
| `l` | extra liv |
| `f` | ficklampa nivå 1 (lyser upp mörker, se avsnitt 10) |
| `j` | hoppstylta nivå 1 (studsar över hinder, se avsnitt 10) |
| `a` / `z` | bomb (3x3 / 4x4, minvärldar) |
| `m` | dold mina (minvärldar) |

**Personer**

| Tecken | Betydelse |
|:------:|-----------|
| `1`..`9` | en talbar person; siffran kopplas till en `@npc` via dess `num` |

En person står alltid på gräs och **blockerar** sin ruta. Det är så man bygger
"grindvakter": placera en person i enda öppningen, så släpper hen ingen förbi
förrän en dialogeffekt flyttar hen åt sidan (`world npc_move ...`).

> **Lediga glyfer för resurser:** alla tecken som *inte* står i tabellerna ovan
> (och inte är siffror 1–9) är lediga och kan bindas till egna samlingsbara
> föremål via `@resources` (avsnitt 9). I demot används `s` och `w`.
> Obs: `f` (ficklampa) och `j` (hoppstylta) är **inte längre lediga** sedan
> v0.18.0 – de är inbyggda föremål (se avsnitt 10).

---

## 6. Story – deklarera namn först

Allt som används i villkor och effekter måste först **deklareras**, annars
underkänner motorns `validate()` äventyret.

```
@flags                   # en flagga per rad (av/på-värden)
met_jakob
robe_given

@counters                # en räknare per rad (heltal)
trust

@items                   # id : namn.sv : namn.en
robe : Brokig mantel : Ornate robe

@quests                  # id : namn.sv : namn.en
deliver_robe : Manteln till Josef : The robe for Joseph
dotan : Mot Dotan : To Dothan
```

Gränser (`.adv`-laddaren på enheten, `adv_loader.h`): max **64** flaggor, **16**
räknare, **32** föremål, **16** uppdrag, **16** npc:er, **128** noder.

---

## 7. @npc / @node – personer och dialog

### @npc – en person

```
@npc jakob               # internt id (används som "speaker" i noder)
name: Jakob | Jacob      # visningsnamn: svenska | engelska
look: 0                  # sprite-/porträttindex i UI:t
num: 1                   # matchar siffran '1' i @map
```

`num` kopplar personen till rätt siffra på kartan. Varje `num` måste vara unikt
(1..9).

### @node – en dialograd

En nod är en replik. **Flera noder får dela id** – den första vars `cond`
stämmer används. Så får en person "minne": en variant för första mötet, en för
senare. En nod `<npc>.start` är ingången när man pratar med personen.

```
@node jakob.start
cond: !met_jakob                 # villkor (utelämna = alltid). Se avsnitt 8.
speaker: jakob                   # npc-id. Utelämna = berättare.
ref: 1 Mos 37:3                  # bibelreferens under texten (valfri)
quote: 1Mo37:3                   # nyckel till citatfil (valfri)
effects: set met_jakob; give robe; quest deliver_robe active
text.sv: Du måste vara den resande. Jag är Jakob ...
text.en: You must be the traveller. I am Jacob ...
choice: Så klart, var är Josef? | Of course, where is Joseph? => jakob.where
choice: Varför just Josef? | Why Joseph especially? => jakob.why
choice: Hej då! | Bye! => END
```

### Valbara rader (choice)

Grundform:

```
choice: <svensk text> | <engelsk text> => <mål-nod eller END>
```

Med villkor och effekter (valfritt, i den här ordningen):

```
choice: <sv> | <en> => <mål> ?? <villkor> !! <effekter> ~~ <tips.sv> | <tips.en>
```

- `=>` målnoden (`END` avslutar samtalet)
- `??` villkor – raden visas bara om villkoret stämmer
- `!!` effekter – tillämpas när man väljer raden
- `~~` tips – om satt visas raden **gråad med tipset** när villkoret inte
  stämmer (annars döljs den helt)

Exempel (att ge bort manteln):

```
choice: Här, den är din. | Here, it's yours. => josef.getrobe ?? has:robe !! take robe; set robe_given; set josef_ready; quest deliver_robe done; sound quest
```

### Gränser för text (från `otr_story.h`)

- Nodtext: max **200 tecken**.
- En valrad: max **36 tecken** (en knapprad vid typsnitt 14).
- Max **4 val** per nod.
- Saknas en engelsk text faller motorn tillbaka på den svenska.

---

## 8. @treasures – dolda skatter (nytt i v0.17.0)

En skatt är ett föremål som ligger **dolt under ett täckande objekt** (t.ex. en
buske `b` eller en låda). Det avtäcks – och kan plockas upp – först när spelaren
slår bort objektet ovanpå. Motorn har alltid haft ett dolt skattlager; `.adv`
exponerar det nu direkt.

```
# x  y  föremål  antal
@treasures
7 2 gem 1
18 11 robe 1
```

Varje rad är `x y id antal`:
- `x` / `y` – rutans koordinater (0-indexerat, `x` = kolumn, `y` = rad).
- `id` – `gem` för ädelsten, annars ett föremåls-id från `@items` eller en
  resurs från `@resources`. Sedan v0.18.0 även `flashlight` (ficklampa) eller
  `pogo` (hoppstylta) – se avsnitt 10.
- `antal` – hur många (oftast 1). För `flashlight`/`pogo` är fältet i stället
  **nivån** (1–3).

Max **48** skatter per karta. Lägg skatten under ett objekt som går att slå bort
(buske, låda, sprucken vägg) – annars är den aldrig nåbar.

---

## 9. @resources – egna samlingsbara föremål (nytt i v0.17.0)

Med `@resources` binder du en **ledig kartglyf** till ett eget samlingsbart
föremål: en egen bild, ett eget namn och ett eget maxantal. Resurserna
återanvänder motorns föremålssystem, så de fungerar i villkor (`has:...`) och
effekter (`take ... N`) precis som vanliga `@items`.

```
# glyf : id : namn.sv : namn.en : tak : bild
@resources
s : stone : Sten : Stone : 9 : stone.png
w : waterjug : Vattenkruka : Water jug : 3 : waterjug.png
```

Fälten per rad:

| Fält | Betyder |
|------|---------|
| `glyf` | ett ledigt tecken på kartan (inte en inbyggd terräng/objekt/siffra) |
| `id` | föremåls-id; läggs automatiskt till i `@items` |
| `namn.sv` / `namn.en` | visningsnamn |
| `tak` | hur många spelaren kan bära (1..255) |
| `bild` | filnamn på en 32×32 PNG i äventyrets `bitmaps/` (se avsnitt 10) |

Gränser: max **16** resurstyper, och max **160** utplaceringar av resurs-glyfer
totalt på en karta.

### "Ge en resurs till en person"-mönstret

Resurser gör grindvakts-pusslet ännu enklare – vakta stigen tills spelaren
lämnar en resurs:

```
@node builder.start
cond: !path_open
speaker: builder
text.sv: Jag vaktar stigen. Ge mig en sten, så kliver jag åt sidan.
choice: Här, en sten. | Here, a stone. => builder.open ?? has:stone !! take stone 1 ; set path_open ; world npc_move 1 1 1 ~~ Du behöver en sten ('s'). | You need a stone ('s').
choice: Jag letar vidare. | I'll keep looking. => END
```

> **Not (v0.17.0):** Resurserna **parsas och laddas** fullt ut och fungerar i
> story-logiken. Upplockning sker via föremålssystemet, men ett särskilt
> HUD-lager som visar resursräknare i spelet är ännu inte inkopplat (står på
> ROADMAP). Resurs-glyfer som inte laddas ritas som gräs så att motorn aldrig
> kraschar.

---

## 10. Mörker, ficklampa och hoppstylta (nytt i v0.18.0)

v0.18.0 lägger till tre saker som hänger ihop: **mörka områden** på kartan, en
**ficklampa** som lyser upp dem, och en **hoppstylta** som studsar över hinder.
Ficklampa och hoppstylta är **permanenta** – de följer med spelaren mellan alla
kartor i samma äventyr (oberoende av `carry`/`reset` i `@rules`).

### 10a. @dark – mörka områden

`@dark` märker en eller flera **rektanglar** på kartan som mörka. En mörk ruta
ritas **inte ut** (spelaren ser bara svart) om den inte är upplyst – men mörkret
**blockerar inte rörelse**. Man kan alltså gå rakt in i mörkret; man ser bara
inte vad som finns där förrän ficklampan lyser på rutan (eller det är rutan man
själv står på, som alltid är synlig).

```
# x0,y0  x1,y1   (två hörn, inklusive båda; rektangeln fylls)
@dark
2,2 9,6
12,8 14,10
```

Regler för `@dark`:

- Varje rad är **två hörn**: `x0,y0 x1,y1`. Båda hörnen räknas med (inklusivt),
  och motorn vänder automatiskt om du anger dem i "fel" ordning.
- Koordinaterna är 0-indexerade (`x` = kolumn, `y` = rad), samma som `@map`.
- Du får ange **flera** rektanglar (en per rad), max **16** (`MAX_DARK`).
- Rektanglar **klipps mot kartans kant** – delar utanför kartan ignoreras, och
  en rektangel som ligger helt utanför kastas bort.
- Mörkret döljer terräng, föremål, skatter och personer i rutan tills det lyses
  upp. Det är tänkt för grottor, källare och nattkartor.

Du kan även skriva rektangeln på samma rad som taggen: `@dark 2,2 9,6`.

### 10b. Ficklampa – lyser upp mörkret

Ficklampan lyser upp mörka rutor i en **kon framåt** i den riktning spelaren är
vänd. Den har tre nivåer (1–3) – ju högre nivå, desto längre når konen.

Lägg in den på kartan med glyfen `f` (**nivå 1**):

```
@map
........
...f....
...S....
```

Eller sätt en högre nivå via `@treasures` (antals-fältet blir **nivån**, 1–3):

```
# x  y  flashlight  nivå
@treasures
3 1 flashlight 2
```

Konens form och räckvidd:

- Räckvidd framåt `R = nivå` rutor: **nivå 1 → 1**, **nivå 2 → 2**,
  **nivå 3 → 3** rutor framför spelaren.
- De främre raderna (framåt `1..R-1`) är **3 rutor breda** (`abs(sidled) ≤ 1`);
  den sista raden (framåt `= R`) är **en ruta rakt fram**. Alltså: nivå 1 = 1 ruta,
  nivå 2 = en 3-bred rad + 1 rakt fram, nivå 3 = två 3-breda rader + 1 rakt fram.
- Ficklampan avtäcker **enbart mörker** (`@dark`). Den lyser **inte** upp dimma
  (`fog`) i minvärldar – det är två olika system.
- Rutan spelaren står på är alltid synlig, oavsett ficklampa.
- Plockar man upp en starkare ficklampa höjs nivån (max 3). En lägre nivå
  sänker den aldrig.

### 10c. Hoppstylta – studsa över hinder

Hoppstyltan låter spelaren **automatiskt hoppa över hinder** när hen går rakt in
i dem. Tre nivåer (1–3) avgör hur **brett** hinder man klarar.

Lägg in den på kartan med glyfen `j` (**nivå 1**):

```
@map
............
..j.........
..S..RRR..X.
```

Eller sätt en högre nivå via `@treasures` (antals-fältet blir **nivån**, 1–3):

```
# x  y  pogo  nivå
@treasures
2 1 pogo 2
```

Hoppet är **manuellt** (fr.o.m. v0.18.3): gå fram mot hindret så att spelaren
vänder sig mot det. Äger du en hoppstylta och går att hoppa byts handlingsknappen
**Slå** till **Hoppa** — vid klick hoppar spelaren i blickriktningen. Du måste
alltså stå vänd åt rätt håll; inget hopp sker automatiskt.

Så fungerar vaulten vid ett hopp mot ett blockerande hinder (sten, låda,
buske, oknuffbar sten osv.):

- Motorn räknar hur många **blockerande rutor i rad** som ligger i
  blickriktningen, upp till `nivå` stycken.
- Landningsrutan är **första gångbara rutan bortom** hindret. Är den gångbar och
  innanför kartan hoppar spelaren dit i ett svep.
- Knappen **Hoppa** visas bara när hoppet är möjligt; annars står det **Slå** och
  ett vanligt drag mot hindret stoppas (`EV_BLOCKED`) om:
  - hinderraden är bredare än `nivå` (för många rutor i rad),
  - landningsrutan inte är gångbar (ännu ett hinder, vatten utan båt osv.),
  - landningsrutan hamnar utanför kartan.
- En nivå-2-stylta klarar alltså 1–2 rutor breda hinder, nivå 3 klarar 1–3.
- Som ficklampan höjer en starkare hoppstylta nivån (max 3) och sänker den
  aldrig.

> **Carry mellan kartor:** både ficklampa och hoppstylta är permanenta och
> återställs **alltid** på nästa karta i äventyret, även om `@rules` inte listar
> dem i `carry`. Spelaren behöver alltså bara plocka upp dem en gång.

---

## 11. Egna sprites – 32×32 PNG (nytt i v0.17.0)

Du kan ge föremål, resurser och omslag egna bilder. Lägg **32×32 PNG med
alfakanal** i äventyrets `bitmaps/`-mapp (och omslaget direkt i äventyrsmappen,
se `@about: cover`). Saknas en bild ritar motorn föremålet proceduralt som
tidigare – egna bilder är alltså helt valfria.

```
/otr/adventure/stenhuggaren/
├── 01_stig.adv
├── cover.png            # omslag (32x32)
└── bitmaps/
    ├── stone.png        # matchar @resources ... stone.png
    └── waterjug.png
```

**Standard-sprites att utgå från:** verktyget `tools/sprite_export` +
`tools/sprite_png.py` exporterar motorns inbyggda, proceduritade symboler till
**61 färdiga 32×32 PNG-filer** under
[`docs/adventures/sprites/`](adventures/sprites/). Kopiera en därifrån och rita
om den som mall för dina egna.

---

## 12. Validering

Motorns `Script::validate()` plus `.adv`-laddaren kontrollerar äventyret och är
den bästa checklistan när du skrivit klart. Du får fel om:

- en flagga/räknare/föremål/uppdrag används utan att vara deklarerad,
- ett choice-mål (`=>`) pekar på en nod som inte finns,
- en nod inte går att nå från någon `<npc>.start`,
- `speaker` pekar på en okänd npc,
- två personer har samma `num`, eller `num` inte är 1..9,
- nodtext > 200 tecken eller valtext > 36 tecken,
- villkor/effekt har felaktig syntax,
- en `@treasures`/`@resources`-rad har fel antal fält eller en resurs-glyf
  krockar med en inbyggd terräng/objekt.

Kartan kontrolleras av `World::load()`: lika långa rader, inom **80×80**, exakt
ett `S`.

---

## 13. Import/export-status

`.adv`-formatet **läses nu både på värddator och direkt på enheten.**

- **På enheten:** `main/adv_loader.{h,cpp}` är en firmware-säker parser (inga
  dynamiska allokeringar, `Adv::Loaded` ligger i PSRAM) som läser `.adv` rakt in
  i motorns strukturer. `main/adv_catalog.{h,cpp}` skannar `/otr/adventure/` och
  bygger menyn; `main/adv_sd_session.{h,cpp}` sköter spara-per-mapp och
  carry/reset. Detta ingår i firmware-bygget (`idf.py build`).
- **På värddator:** `tools/adv_parse.{h,cpp}` läser samma format för
  test/generering utanför enheten.

Host-tester bevisar att formatet importeras till den **faktiska** motorn (inte
en leksaksmodell):

```
# Laddaren (samma parser som på enheten)
g++ -std=c++17 -Wall -Wextra -Imain -Itools -Icomponents/otr_engine/include \
    -Icomponents/otr_story/include tests/test_adv_loader.cpp main/adv_loader.cpp \
    tools/adv_parse.cpp components/otr_engine/otr_world.cpp \
    components/otr_story/otr_story.cpp -o tests/test_adv_loader && ./tests/test_adv_loader

# Katalog + SD-session (skanning, carry/reset, spara-roundtrip)
g++ -std=c++17 -Wall -Wextra -Imain -Itools -Icomponents/otr_engine/include \
    -Icomponents/otr_story/include tests/test_adv_catalog.cpp main/adv_catalog.cpp \
    main/adv_sd_session.cpp main/adv_loader.cpp tools/adv_parse.cpp \
    components/otr_engine/otr_world.cpp components/otr_story/otr_story.cpp \
    -o tests/test_adv_catalog && ./tests/test_adv_catalog
```

> **Hårdvarustatus:** SD-skanning, meny och spara-logik är verifierade via
> host-tester och `idf.py build`, men är **ännu inte testade på fysisk
> hårdvara**. Lägg SD-paketet (`sdcard_bundle/`) på ett riktigt kort för att
> prova på enheten.

---

## 14. Kompletta exempel

- **Hebron-lägret (Josef, 1 Mos 37):**
  [`docs/adventures/example_joseph_hebron.adv`](adventures/example_joseph_hebron.adv)
  – den verkliga första Josef-kartan: grindvakt (Ruben), nyckel/dörr-pussel och
  flerspråkig dialog.
- **Stenhuggarens stig (nyheterna i v0.17.0):**
  [`docs/adventures/example_resources_demo.adv`](adventures/example_resources_demo.adv)
  – `@about`, `@treasures`, `@resources`, carry/reset och gångbar ram i ett kort,
  körbart äventyr.
- **Alla inbyggda äventyr som `.adv`:**
  [`docs/adventures/generated/`](adventures/generated/) – byn, josef (2 kartor),
  minfaltet och testvarlden, exporterade direkt från den kompilerade
  motordatan. Kopiera en som startpunkt.
