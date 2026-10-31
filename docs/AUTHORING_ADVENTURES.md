# Skriva egna äventyr i ASCII (.adv)

Den här referensen beskriver beteendet i produktionsladdaren
`main/adv_loader.cpp` och motorn. I repot är äventyrsmotorns senaste
dokumenterade firmwareversion **0.21.2**. Versionsetiketterna nedan anger när
en funktion först introducerades; de är inte separata formatprofiler.

Vill du snabbt se vilka tecken och mekaniker som finns? Börja med
[snabbguiden](ADVENTURE_AUTHORING_QUICKSTART.md). Den här sidan är den
fullständiga referensen för formatet och alla avsnitt.

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
> - `@resources` – resursmetadata (glyf → story-id, namn, bild + tak) — avsnitt 9
> - `carry`/`reset` i `@rules` – vad som följer med mellan nivåer — avsnitt 4
> - Egna **32×32 PNG-sprites** i `bitmaps/` — avsnitt 11
>
> **Nytt i v0.18.0** (avsnitt 10):
> - `@dark` – mörka områden som bara lyses upp av ficklampan — avsnitt 10
> - **Ficklampa** (`f` / `@treasures ... flash N`) – lyser upp mörkret i en kon framåt
> - **Hoppstylta** (`j` / `@treasures ... pogo N`) – studsar automatiskt över hinder
>
> **Nytt i v0.20.0:** oberoende grupper av Sokoban-tryckplattor och grindar — avsnitt 10d.
>
> **Nytt i v0.20.1 (Fas S2):** spakar, tidsgrindar, is, transportband, färgade
> nycklar/dörrar och par-bedömning — avsnitt 10e.
>
> **Nytt i v0.21.0 (Fas S3):** skridskor, giftmark/träsk/vassa stenar,
> färglådor/färgplattor och uttryckliga spak+platta-AND-grupper — avsnitt 10f.
>
> **Nytt i v0.21.1:** sugpropp/Dra och turbaserade patruller/jagare —
> avsnitt 10g. 0.21.0-mekanik och äldre demobanor finns kvar.

> **Nytt i v0.21.2:** egna resurser fungerar nu som verkliga pickups med
> innehavstak, `has`/`give`/`take`, föremålslista, valfri PNG, sparning och
> carry/reset. Se avsnitt 9 och [det körbara resursprovet](adventures/z_resource_test/README.md).
> En `.adv`-fil kan nu innehålla upp till fyra interna kartor med namngivna
> destinationer, portaler och gemensamt story/inventory. Se avsnitt 12,
> [flerkartsreferensen](ADVENTURE_MULTIMAP_FORMAT.md) och det körbara
> [portalprovet](adventures/multimap_portals/README.md).

Formatet är avsiktligt **nära motorns verkliga datastrukturer** så att det är
realistiskt importbart:

| Del i `.adv`-filen | Motsvarar i koden | Fil |
|--------------------|-------------------|-----|
| `@about`           | `Adv::Header` (SD-menyns metadata) | `main/adv_loader.h` |
| `@map` + `@rules`  | `struct MapDef` / `struct Rules` | `components/otr_engine/include/otr_world.h` |
| teckenlegenden     | `decode()` (tecken -> `Tile`/`Obj`) | `components/otr_engine/otr_world.cpp` |
| `@treasures`       | `Otr::Treasure` (dolt lager i `World`) | `components/otr_engine/include/otr_world.h` |
| `@resources`       | `Adv::Resource` + föremålssystemet | `main/adv_loader.h` |
| `@enemies`         | `Otr::EnemyDef` + separat dynamiskt fiendetillstånd | `components/otr_engine/include/otr_world.h` |
| `@flags/@counters/@items/@quests/@npc/@node` | `struct Script` m.fl. | `components/otr_story/include/otr_story.h` |

> Kompletta, fungerande exempel finns att utgå från:
> - [`docs/adventures/example_joseph_hebron.adv`](adventures/example_joseph_hebron.adv) – den verkliga Hebron-kartan ur Josef-kapitlet (1 Mosebok 37).
> - [`docs/adventures/example_resources_demo.adv`](adventures/example_resources_demo.adv) – ett litet demoäventyr som visar **alla** nyheter i v0.17.0 (`@about`, `@treasures`, `@resources`, carry/reset och gångbar ram).
> - [S2-demot](adventures/s2_demo/README.md) och [dess karta](adventures/s2_demo/s2_demo.adv) – alla S2-mekanismer tillsammans.
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
- **Flera `.adv`-filer i mappen är separata nivåer i bokstavsordning.** Döp
  dem med numrerat prefix – `01_...adv`, `02_...adv` – så spelas de i ordning.
  En fil kan också innehålla interna kartor som länkas med portaler; de ersätter
  inte eller blandas med separata filer.
- **Menytiteln och beskrivningen hämtas från den *första* kartans `@about`**
  (avsnitt 3). Saknas `@about` används mappnamnet.
- Undermapparna `saves/`, `maps/` och `bitmaps/` hoppas över vid skanningen.
- Hittas inga äventyr visar menyn ett meddelande om att SD-kortet saknar
  `/otr/adventure/` (strängarna `ADV_NO_ADVENTURES` / `ADV_NO_ADVENTURES_HELP`).

En `.adv`-fil är vanlig text (UTF-8, max 64 KB) och består av **sektioner** som
börjar med `@`. Rader som börjar med `#` är kommentarer utanför `@map`. Inuti
`@map` finns inga kommentarer: `#` är ett karttecken (vägg), och endast en rad
som efter trimning börjar med `@` avslutar kartsektionen. Tomma rader ignoreras.
En enskild källrad får vara högst 1199 byte. Kartan är ASCII-rutdata: använd
ett byte/tecken per ruta, inte flerbytes-UTF-8-glyfer.

Vanlig sektionsordning (laddaren kräver inte denna ordning):

```
@meta        id, värld-id, kart-id
@about       titel, beskrivning, författare, omslag (det SD-menyn visar)
@rules       spelregler (energi, liv, lava, dimma, par) + carry/reset
@map         själva kartan, ritad med ASCII-tecken
@dark        mörka områden som ficklampan lyser upp (valfri, v0.18.0)
@enemies     patruller/jagare, rutter och valfria områden (valfri, v0.21.1)
@treasures   dolda skatter under täckande objekt (valfri)
@resources   egna samlingsbara föremål kopplade till glyfer (valfri)
@flags       deklarera alla flaggor du använder
@counters    deklarera alla räknare
@items       deklarera föremål (id + namn SV/EN)
@quests      deklarera uppdrag (id + namn SV/EN)
@npc <id>    en person (upprepas per person)
@node <id>   en dialograd (upprepas många gånger)
@legend      glyph-mappning för stödda grupp-, rörelse-, färg- och terrängmekaniker
@timedgate   tidsgräns för en grupp (valfri, S2)
@plategroup  grupp-id på tryckplattor via koordinater
@gategroup   grupp-id på grindar via koordinater
```

---

## 2. @meta

```
@meta
id: 01_lager             # internt id för kartan
world: 4                 # numerisk metadata för världen (Josef = 4)
map_id: 4                # stabilt kart-id som sparfiler använder
```

`world` är metadata och parsern kontrollerar inte dess intervall. `map_id` blir
motorns 8-bitars kart-id; håll det inom **0–255** och ge kartorna stabila,
unika värden. (I Josef: lägret = 4, vägen = 5.) För ett eget äventyr är 90+
ett praktiskt val som undviker de inbyggda exemplens värden. Båda fälten läses
som heltal; välj giltiga tal i stället för att förlita dig på parserns
tillåtande konvertering.

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
| `title.sv` / `title.en` | rubrik i menyn (språk efter enhetens inställning) | 79 byte per språk |
| `desc.sv` / `desc.en`   | beskrivning under rubriken | 199 byte per språk |
| `author`                | upphovsperson | 63 byte |
| `cover`                 | filnamn på omslagsbild i äventyrsmappen (valfritt) | 39 byte |

Saknas den engelska varianten faller menyn tillbaka på den svenska.
Laddaren trunkerar längre värden till dessa buffertstorlekar; den ger inte
parsefel. Gränserna gäller byte, inte antal UTF-8-tecken.

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

Värdena i den här sektionen konverteras till motorns bytefält. Motorn avvisar
`start_energy` under 1, `energy_cap` under `start_energy`, `lives` utanför
`1..9` (förutom `infinite`) och `mine_level` över 12. För övriga bytefält ska
du ange ett tal 0..255. Parsern gör inte strikt sifferkontroll för de vanliga
regelfälten (`atoi`-konvertering); en editor bör därför validera talvärden
innan den skriver filen.

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
| `suit` | dräktladdningar (lava- och terrängskydd, se 10f) |
| `keys` | nycklar |
| `gems` | accepteras som token men påverkar inte nivåbyte; ädelstenar är kartlokala |
| `boat` | båt |

Story-föremål, flaggor, räknare och uppdrag matchas **på namn** mellan kartor,
så samma `@items`/`@flags`-id i nästa karta behåller sitt värde när `items`
ingår i `carry`. (Så fungerar Josef: `carry: energy, maxenergy, items, lives`
och `reset: suit`.)

`carry` och `reset` delar upp token på kommatecken/mellanslag och ignorerar
okända token. `reset` vinner när samma stödda resurs finns i båda listorna.

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
- **Siffran placerar INTE minorna.** På en SD-karta är det glyfen `m` du ritar ut
  som är minorna – `mine_level` genererar inga minor och ändrar inte deras
  placering. Siffran är ett läges-/etikettval (HUD `N/12` + svårighetsparitet).
  All svårighet styr du med planens storlek och antal `m`. Se
  [`Minesweep_design.md`](Minesweep_design.md) för hela resonemanget.

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
på sista banan markeras äventyret som klarat. Progressionen styrs alltså av
**filordningen**, inte av `mine_level`-talet. Ge varje fil ett `mine_level` som
passar den HUD-etikett du vill visa (t.ex. stigande 1→12, eller samma tal i
klumpar). Förlust (game over) startar om den aktuella banan som vanligt.

> **Obs `mine_level: 12`:** vinsttexten vid `12` är kampanjvinst-texten i stället
> för den vanliga. Sätt `12` först på din **sista** bana (annars får spelaren fel
> vinsttext mitt i serien). Detaljer i [`Minesweep_design.md`](Minesweep_design.md).

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

En enkartafil har en `@map` utan kart-id. En flerkartsfil kan dessutom ha
`@map 2` … `@map 255`; väljare, destinationer och portaler beskrivs i
[avsnitt 12](#12-flerkartor-och-portaler). Flera separata `.adv`-filer
spelas fortfarande i filnamnsordning.

Kartan är ett rutnät, en rad per textrad, **ett byte per ruta**. Alla rader
måste vara **lika långa** (det blir kartans bredd) och antalet rader blir
höjden. Radens inledande/avslutande blanktecken räknas som kartceller, inte
som indrag eller utfyllnad.

**Gränser (från motorn, `otr_world.h`):**
- Max bredd **80**, max höjd **80** (`MAX_W` / `MAX_H`) – höjt från 64 i
  v0.17.0. Kartrutorna ligger i PSRAM.
- Exakt **ett** `S` (startruta) krävs.
- `X` (utgång) är valfritt i motorn; en vanlig bana utan utgång kan inte klaras
  genom att gå i mål. I minsveparläge krävs inte `X`.
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
cond: !met_jakob                 # villkor (utelämna = alltid). Se villkorssyntax nedan.
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

### Villkor och effekter

Samma villkorssyntax används på `@node` och `choice`. Villkor separeras med
`&` och måste alla stämma. Ett inledande `!` negerar ett enskilt villkor.

| Villkor | Exempel | Betydelse |
|---|---|---|
| Flagga | `met_jakob`, `!met_jakob` | Flaggan är satt / inte satt |
| Föremål | `has:robe`, `!has:robe` | Minst en / ingen av föremålet finns |
| Uppdrag | `quest:dotan=active` | Uppdragets tillstånd är `none`, `active` eller `done` |
| Räknare | `trust>=2`, `trust<=5`, `trust=3` | Jämför deklarerad räknare med heltal |

Exempel på flera villkor: `has:robe & quest:dotan=active & trust>=2`.
Flaggor, föremål, uppdrag och räknare måste deklareras i respektive sektion.

Effekter separeras med semikolon. Parsern/storymotorn stöder dessa former:

| Effekt | Funktion |
|---|---|
| `set <flagga>` / `clear <flagga>` | Sätt / rensa en deklarerad flagga |
| `add <räknare> <tal>` | Lägg till ett heltal i räknaren |
| `cnt <räknare> <tal>` | Sätt räknaren till ett heltal |
| `give <föremål> [antal]` / `take <föremål> [antal]` | Ändra deklarerat föremålsantal; utelämnat antal är 1 |
| `quest <uppdrag> none\|active\|done` | Ändra uppdragets tillstånd |
| `world <åtgärd> [heltal ...]` | Skicka åtgärd och upp till tre heltal till äventyrsvärden |
| `sound <namn>` | Skicka namngiven ljudeffekt till värden |

`give`/`take` tillåter uttryckligt antal 1–255. Ogiltiga namn, jämförelser,
effekter eller odeklarerade story-id:n gör att `Script::validate()` underkänner
äventyrsskriptet. `world`-åtgärdens och ljudeffektens namn skickas vidare till
värden; valideringen bekräftar inte att värden har en implementation för just
det namnet.

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
- `id` – ett av de inbyggda föremålsnamnen: `key`, `gem`, `boat`, `potion`,
  `heart`, `suit`, `life`, `bomb3`, `bomb4`, `flash`, `pogo`, `skates` eller
  `suction`. `flashlight` är inte ett giltigt skatt-id; använd `flash`.
  `pogostick` är också alias för `pogo`. Story-id från `@items` och egna
  resurs-id från `@resources` stöds inte som skatter.
- `antal` – valfritt bytevärde 0..255, standard 1. Det används som mängd för
  dryck/hjärta; för `flash`/`pogo` blir det nivå (0 betyder nivå 1, högre värden
  begränsas till nivå 3). Övriga föremål använder inte mängden.

Max **48** skatter per karta. Koordinaterna måste peka inom kartan, och en ruta
får inte ha mer än en skatt. Lägg skatten under ett täckande objekt (buske, låda,
sprucken vägg, sten eller dörr) så avtäcks den när objektet försvinner. Ligger
rutan fri (eller har bara en plockbar glyf, se nedan) är skatten synlig direkt.

### @treasures har företräde över kartglyfen (nytt i v0.18.5)

Ligger en `@treasures`-rad på **samma koordinat** som en **plockbar glyf** på
kartan vinner alltid skatten. Det gör att du kan rita en ficklampa `f` eller
hoppstylta `j` på kartan (som annars ger **nivå 1**) och samtidigt höja den till
nivå 2–3 med en skatt på samma ruta:

```
@map
..j.....
..S..RRRX
@treasures
2 0 pogo 3        # rutan visar j, men spelaren får nivå 3
```

Detta gäller kartans upphämtningsföremål (bland annat ficklampa, hoppstylta,
ädelsten och generisk nyckel) – glyfen ersätts av skatten. **Täckande objekt**
påverkas inte: en skatt
under buske `b`, låda, sten eller sprucken vägg ligger kvar dold tills du slår
bort/knuffar objektet, precis som förut.

---

## 9. @resources – egna samlingsbara föremål (nytt i v0.17.0)

Med `@resources` binder du en **ledig kartglyf** till ett eget samlingsbart
föremål: en bild, ett namn och ett maxantal. Resurs-id:t registreras
som story-föremål och kan användas i villkor (`has:...`) och effekter
(`give ... N`, `take ... N`) precis som andra `@items`.
Sedan **0.21.2** plockas det upp genom att gå på rutan, direkt till samma
storyinventory. Varje utplacering ger **ett** föremål. Vid fullt tak ligger
objektet kvar och rutan går fortfarande att passera. Även `give` och carry
klampar till taket; `take` går aldrig under noll.

```
# glyf : id : namn.sv : namn.en : tak : bild
@resources
s : stone : Sten : Stone : 9 : stone.png
w : waterjug : Vattenkruka : Water jug : 3 : waterjug.png
```

Fälten per rad:

| Fält | Betyder |
|------|---------|
| `glyf` | ett ledigt tecken på kartan (inte en inbyggd terräng/objekt/siffra eller en `@legend`-glyf) |
| `id` | föremåls-id; läggs automatiskt till i `@items` |
| `namn.sv` / `namn.en` | visningsnamn |
| `tak` | maxantal (0 eller tomt blir 1; värden under 1 blir 1 och över 255 blir 255) |
| `bild` | valfritt PNG-filnamn, högst 39 byte, i äventyrets `bitmaps/`; inga sökvägar eller `..` |

Namn kan lämnas tomma: svenskt namn faller då tillbaka på id och engelskt namn
saknas. Resurs-id läggs till i `@items`, så resurstyper räknas också mot
gränsen **32 story-föremål**. Gränser: max **16** resurstyper och **160**
utplaceringar per karta. Parsern kontrollerar glyfkollision med inbyggda
tecken och dubbletter bland resurser/item-id:n. Befintligt företräde behålls:
om samma glyf också finns i `@legend` vinner legendbindningen, oavsett
sektionsordning, och ingen resurs placeras där. Använd därför olika glyfer
för resurser och legendobjekt. Resurs-id är högst 39 byte. Använd inte `@`, kontrolltecken,
flerbytetecken eller blanktecken som glyfer.

### "Ge en resurs till en person"-mönstret

Resursinventeringen kan göra grindvakts-pusslet
enklare – vakta stigen tills spelaren lämnar en resurs. Exemplet visar
storyvillkoret/-effekten; komplettera med NPC-, flagg- och resursdefinition:

```
@node builder.start
cond: !path_open
speaker: builder
text.sv: Jag vaktar stigen. Ge mig en sten, så kliver jag åt sidan.
choice: Här, en sten. | Here, a stone. => builder.open ?? has:stone !! take stone 1 ; set path_open ; world npc_move 1 1 1 ~~ Du behöver en sten ('s'). | You need a stone ('s').
choice: Jag letar vidare. | I'll keep looking. => END
```

**Runtime 0.21.2:** kartan visar PNG eller en gul samlarpåse. Meny → **Föremål**
visar namn och `antal / tak`, även i filer helt utan dialognoder. Webblivevyn
visar kartresurser och samma innehav. Resurser döljs av dimma/mörker enligt
vanliga regler. Upplockning sker vid varje faktisk landning, även efter
transportband/hopp, inte enbart vid den position där UI-animationen slutar.

Sparning återställer både kvarvarande kartobjekt och storyantal. Checkpoint
är en respawnpunkt; död återställer inte redan plockade resurser.
`carry: items` i filen du **lämnar** för över innehav via id till nästa fil;
deklarera samma id där också. Det nya taket gäller och okända id:n tappas
enligt befintlig carryregel. `reset: items` vinner över carry. **Starta om**
startar som tidigare hela äventyret från första filen, med tomt storyinventory
och återställda resurser. Ingen undo finns eller tillkommer.

Saveformat 7 stöder resursobjekt. Saves från format 1–6 kan fortsatt läsas på
kartor **utan** resurser; en gammal save för en resurskarta avvisas tydligt
och kräver nytt spel (annars skulle det gamla gräslagret tappa resurserna).
Storyformatet och SD-containern är oförändrade.
Kör [resursprovet](adventures/z_resource_test/README.md) för en exakt checklista.

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
  en rektangel som ligger helt utanför kastas bort. Den sjuttonde rektangeln
  ger ett parserfel.
- Mörkret döljer terräng, föremål, skatter och personer i rutan tills det lyses
  upp. Det är tänkt för grottor, källare och nattkartor.

Du kan även skriva rektangeln på samma rad som taggen: `@dark 2,2 9,6`.
Det är fortfarande exakt två koordinatpar (fyra tal), inte en lista med
fristående rutor. En enda mörk ruta skrivs med samma hörn två gånger:
`@dark 4,3 4,3`. I en flerkartsfil kan taggen skrivas `@dark map=2`.

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

Eller sätt en högre nivå via `@treasures` (antals-fältet blir **nivån**, 1–3).
Du kan lägga skatten på **samma ruta** som glyfen `f` – skatten vinner och sätter
nivån (se avsnitt 8, *@treasures har företräde*):

```
# x  y  flash  nivå
@treasures
3 1 flash 2
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

Eller sätt en högre nivå via `@treasures` (antals-fältet blir **nivån**, 1–3).
Glyfen `j` och skatten får ligga på **samma ruta** – skatten vinner och sätter
nivån (se avsnitt 8, *@treasures har företräde*):

```
# x  y  pogo  nivå
@treasures
2 1 pogo 2
```

Hoppet är **manuellt** (fr.o.m. v0.18.3): gå fram mot hindret så att spelaren
vänder sig mot det. Fr.o.m. **v0.18.4** är handlingsknappen **delad**: översta
tredjedelen (**Byt**) växlar vald handling mellan de som går just nu — **Slå**,
**Prata** (när du står vänd mot en person) och **Hoppa** (när du äger en
hoppstylta och står vänd mot ett hinder du klarar) — och nedre två
tredjedelarna utför den valda handlingen. Inget byts automatiskt längre: du
väljer själv, så du kan t.ex. **Slå** i stället för **Prata** om storyn kräver
det. Faller den valda handlingen bort (du går från hindret/personen) återgår
knappen till **Slå**.

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

### 10d. Grupperade Sokoban-plattor och grindar (v0.20.0)

Tryckplattor (`o`) och grindar (`=`) hör som standard till grupp 0, vilket
behåller det äldre globala beteendet. Grupper 1–35 är oberoende: gruppens grindar
öppnas när minst en grupp-platta finns och samtliga dessa plattor bär en sten.
Skriv grupp-id som `1`–`9` eller `A`–`Z` (`A`=10, `Z`=35; skiftläget ignoreras).

Mappa egna, skiftlägeskänsliga karttecken i `@legend`:

```
@legend
a = plate 1
A = gate 1
b = plate 2
B = gate 2
```

Legend-definitioner går före inbyggda tecken (t.ex. `R`, `o` och `=`). `S`,
mellanslag och `@` får inte omdefinieras; dubbletter och ogiltiga grupp-id ger
fel med radnummer.

Alternativt kan gruppen anges för redan placerade `o`-/`=`-rutor med koordinater
(`x,y`, nollbaserat):

```
@plategroup 1: 3,4 5,4 7,8
@gategroup 1: 9,4
@plategroup 2: 12,10 14,10
@gategroup 2: 16,10
```

Om en koordinat inte pekar på rätt ruttyp rapporteras ett fel med radnummer.
Varje direktiv måste stå på en enda rad: efter kolon anges ett eller flera
fristående koordinatpar, inte hörnen i en rektangel. Första raden ovan tilldelar
alltså exakt tre plattor till grupp 1; rutorna mellan dem påverkas inte.
Gruppen har inget fast antal plattor eller grindar. Ett avslutande kart-id
stöds inte i nuvarande format.
Ett spelbart exempel med fyra sekventiella rum i samma `.adv`-fil finns i
[`docs/adventures/sokoban/sokoban.adv`](adventures/sokoban/sokoban.adv).
Rum 1–2 använder `@legend`; rum 3–4 använder standardtecknen `o`/`=` med
`@plategroup` och `@gategroup`. Varje rum har två plattor och två stenar;
grinden öppnas först när båda plattorna i dess grupp täcks. SD-kortsversionen finns i
`/otr/adventure/sokoban/sokoban.adv`.

---

### 10e. S2: spakar, tidsgrindar, rörelseterräng och färgnycklar (v0.20.1)

`@legend` kan även mappa S2-glyfer. Grupp-id skrivs som `0`–`9` eller `A`–`Z`
(`A`=10, `Z`=35; bokstäver är skiftlägesokänsliga). Glyfen till vänster om `=`
är däremot skiftlägeskänslig.

| `@legend`-värde / direktiv | Betydelse |
|---|---|
| `plate <grupp>` / `gate <grupp>` | Tryckplatta/grind i grupp 0–35; se 10d |
| `lever <grupp>` | Solid spak i grupp 0–35 |
| `ice` | Is |
| `belt up`, `belt right`, `belt down`, `belt left` | Transportband som tvingar spelaren i pilens riktning |
| `key <färg>` / `door <färg>` | Färgnyckel/färgdörr: röd (`red`), grön (`green`), blå (`blue`), gul (`yellow`), lila (`purple`) eller cyan (`cyan`) |
| `@timedgate <grupp>: N` | Tidsgrupp, `N`=1–65535 steg |
| `@rules` → `par: N` | Par-mål, `N`=1–65535 steg |

Ett kort, komplett syntaxexempel:

```text
@meta
id: s2_syntax
world: 90
map_id: 90

@rules
par: 20

@legend
a = plate 1
A = gate 1
! = lever 2
i = ice
> = belt right
r = key red
d = door red
@timedgate 1: 12

@map
##############
#S.RaA!i>rd.X#
##############
```

Demot med alla S2-mekanismer finns i
[`docs/adventures/s2_demo/README.md`](adventures/s2_demo/README.md) och
[`s2_demo.adv`](adventures/s2_demo/s2_demo.adv).

#### Spakar och tidsgrindar

En spak är solid och aktiveras bara med en uttrycklig handling; att gå in i den
aktiverar den inte. Flera vanliga toggle-spakar i samma grupp delar latch och
växlar gruppens tillstånd. Utan uttryckligt AND-läge (se 10f) får en grupp inte
blanda plattor och spakar; laddaren avvisar blandningen med radnummer.
På enheten väljer **Byt / Swap** handlingen
**Spak / Lever**; den nedre delen av utförandeknappen aktiverar spaken.

`@timedgate <grupp>: N` konfigurerar en grupp med `N` steg (1–65535). En sådan
grupp måste ha minst en grind och exakt en styrmekanism: en platta **eller** en
spak. Varje lyckad förflyttning tickar timern; när styrmekanismen aktiveras
återställs den till hela `N`. Blockerad rörelse, upplåsning av dörr, **Slå** och
spakhandlingen tickar inte timern. Timergränsen är alltså steg, inte sekunder.
Ingen realtid tickar under paus eller dialog.

#### Is, band och rörelseplanering

Is fortsätter i spelarens ingångsriktning; band tvingar sin angivna riktning.
Bara spelaren påverkas, inte stenar. En input kan ge högst **128 extra**
automatiska steg. Varje förflyttad ruta räknas som ett steg och tickar tidsgrindar.
Blockeringar stoppar kedjan utan att flytta spelaren in i hindret. A*-planeraren
undviker is- och bandrutor i sina rutter; använd styrplattan för att styra genom
sådana områden.

#### Färgnycklar och `par`

Varje färg har en separat nyckelräknare på **0–255**. En färgad dörr förbrukar
bara en nyckel av samma färg och står sedan öppen. De äldre generiska tecknen
`k`/`K` och `D` behåller sin betydelse, liksom storynycklar.

I `@rules` kan `par: N` sätta ett stegsmål, där `N` är 1–65535. Par är en
bedömning, inte ett vinstvillkor: `N` steg eller färre möter par, men spelaren
kan klara banan även över par. Steg räknas per förflyttad ruta, inte per input.
HUD och resultat visar stegen i förhållande till par.

Nycklar förs över mellan kartor med `carry: keys`; `reset: keys` nollställer
dem, och **reset vinner** om samma nyckel finns i båda listorna.

#### Skatter och sparningar

En `@treasures`-rad får inte ligga på en gruppbärande platta, grind eller spak;
laddaren avvisar det med radnummer eftersom cellen använder sitt värde för
grupp-id.

S2 introducerade motorformat **SAVE_VERSION=4**. Efter rutdatan finns
ett separat **86-byte S2-tillägg** med färgnycklar, spak-latches och återstående
grindtider. Motorformat v1/v2/v3 migreras vid läsning; de nya S2-tillstånden
börjar då tomma/av. SD-sessionens yttre container är fortsatt version 1. Äldre
firmware kan inte läsa format 4. För aktuellt S3-format 5, se 10f.

### 10f. S3: skridskor, giftterräng och färgsokoban (v0.21.0)

De nya karttecknen väljs uttryckligen i `@legend`; inga gamla standardtecken
byter betydelse. Produktionsloadern (`main/adv_loader`) är referensen även för
S3. Det äldre hostverktyget `tools/adv_parse.*` stöder inte S2/S3.

| `@legend`-värde | Betydelse |
|---|---|
| `skates` | Permanenta skridskor |
| `poison` | Giftmark |
| `swamp` | Träsk |
| `sharp_rocks` | Uppstickande vassa stenar |
| `box <färg>` | Flyttbar Sokoban-låda, inte en sönderslagbar `crate` |
| `plate <färg>` | Färgplatta, som börjar i grupp 0 |
| `plate <grupp> <färg>` | Färgplatta direkt i angiven grupp |

Färgerna är `red`, `green`, `blue`, `yellow`, `purple`, `cyan`, med samma
färg-/siffermärkning **1–6** som nycklarna. Färg och grindgrupp är oberoende:
flytta en färgplatta till önskad grupp med befintliga `@plategroup`-koordinater.
Exempel:

```text
@legend
i = ice
s = skates
p = poison
w = swamp
h = sharp_rocks
r = box red
a = plate red
! = lever 1
G = gate 1

@andgate 1

@map
###############
#Ssi.pwh......#
#..r.a...G..X.#
#..!..........#
###############

@plategroup 1: 5,2
```

#### Skridskor och rörelse

Upplockade skridskor är alltid aktiva; de förbrukas inte och kräver ingen
separat aktiveringsknapp. På is blir varje riktningsinput ett vanligt steg:
spelaren kan stanna och byta riktning. Utan skridskor gäller tidigare glidning.
Band tvingar fortfarande rörelse, även med skridskor.
Tap-to-walk kan planera över is när skridskor är aktiva; utan skridskor
undviks is som tidigare. Band undviks fortfarande av planeraren.

Skridskor följer samma permanenta förmågemodell som ficklampa/hoppstylta:
de följer med mellan kartor oberoende av `carry`/`reset`-maskerna och sparas
med världen. Start av ett nytt äventyr ger inga skridskor.

#### Energi och skyddsdräkt

| Terräng | Utan dräktladdning | Med dräktladdning |
|---|---:|---:|
| Giftmark | 1 | 0 |
| Träsk | 2 | 1 |
| Vassa stenar | 4 | 2 |

Kostnaden tas en gång per faktisk landning på rutan, inklusive återinträde,
tvångsrörelse och hoppets landning. Att stå stilla, slå, använda spak eller
försöka ett blockerat steg kostar ingen terrängenergi.
Skadan använder samma energi-/döds-/respawnregler som övrig skada.
Skyddet gäller när `suitCharges() > 0`; dessa tre terränger förbrukar inte
dräktladdningar. Lava och minor behåller sina tidigare laddningsregler.
Skridskor skyddar inte mot terrängskada.

#### Lådor, plattor och dolda föremål

Färglådor knuffas som stenar, utan nyckelkrav, men kan inte slås sönder.
En färgplatta kräver en låda med **samma färg**: spelaren, sten och fel färg
aktiverar den inte. Gamla neutrala plattor behåller sina stenregler;
färglådor ersätter inte sten där. Alla plattor i en grupp måste vara rätt täckta.

Det befintliga `@treasures`-lagret kan ligga under sten eller färglåda.
När täckningen knuffas bort lämnas skatten på ursprungsrutan, inte under det
flyttade objektet. Spelaren går samtidigt in där och kan plocka upp skatten
direkt. Exempel: `@treasures` → `3 2 potion 2` under lådan i exemplet ovan.
Sugpropp/Dra tillkommer i **0.21.1**, se 10g.
Skatter på plattor/grindar/spakar är fortsatt ogiltiga, även på färgplattor,
eftersom grupp-id och föremålsmängd inte får kollidera.

#### Uttrycklig spak+platta-AND

`@group <grupp>: and` (eller kortformen `@andgate <grupp>`) anger AND-läge
för gruppen (0–9/A–Z).
Grinden öppnas endast när gruppens spak-latch är ON **och** alla gruppens
plattor är rätt täckta. Spak OFF eller en bortflyttad täckning stänger igen.
Flera spakar delar gruppens latch, precis som tidigare.

| Grupp | Utan `@andgate` | Med `@andgate` |
|---|---|---|
| Bara plattor | Alla måste vara täckta | Stängd: spakvillkoret saknas |
| Bara spak | Spak-latch styr | Stängd: plattvillkoret saknas |
| Både spak och plattor | Parsefel, som tidigare | ON och alla täckta |
| Varken spak eller plattor | Stängd | Stängd |

AND och `@timedgate` får inte användas på samma grupp. Dubbla AND-direktiv,
okänd färg, extra argument och ogiltiga grupp-/koordinatangivelser ger parsefel
med radnummer; befintliga resurs-/glyf- och skattkollisioner förblir fel.

#### Sparformat och verifieringsgräns

Motorformat **5** lägger till exakt **en byte** (skridskor av/på) efter
format 4:s 86-byte S2-tillägg, före CRC. Äldre format 1–4 läses med skridskor
**av**; äldre firmware kan inte läsa format 5. Terräng- och objekt-id:n är
tillagda sist i befintliga enum och ryms i rutornas befintliga bytefält.
Färglådans läge, dolda skatter, dräkt, energi och spak-latch sparas redan där.
Plattfärg och grupp ryms också i befintliga rutbyte; AND-konfigurationen hämtas
från laddad karta. Ingen extra färg-/gruppmatris behöver sparas. SD-sessionens
yttre container är fortsatt version **1**.

Hosttesterna täcker mekanik, loaderfel, gamla sparformat, återställning,
UI-rendering och den externa demots dokumenterade lösning.
Ägaren har bekräftat att 0.21.0-blocket fungerar på hårdvara. Se
[demots README](adventures/s3_demo/README.md) för SD-sökväg,
lösning och manuell kontroll.

### 10g. Sugpropp och turbaserade fiender (v0.21.1)

Produktionsloadern är referens för syntaxen nedan. Fiender överförs som
separata definitioner och tillstånd, inte via nya enbytes-kartglyfer.
Objekt-id för sugpropp har lagts sist; äldre objekt har inte numrerats om.

#### Sugpropp och Dra

```text
@legend
s = suction
```

Använd `s` i kartan för en synlig pickup, eller `X Y suction` i
`@treasures` för en dold sugpropp under exempelvis sten/färglåda. Vanliga
skattregler gäller: skatten hör till rutan, följer inte objektet och får inte
ligga på platta/grind/spak. En framflyttad täckning kan avslöja och omedelbart
ge spelaren utrustningen.

Med sugpropp kan spelaren dra **sten och de sex färglådorna**, aldrig
sönderslagbara lådor eller buskar. Stå vänd mot objektet, välj **Dra** med
**Byt**, och utför med nedre delen av handlingsknappen. Val och utförande är
separata. Utan sugpropp finns inte handlingen. Spelaren backar en ruta,
objektet flyttas till spelarens förra ruta. Båda destinationerna måste vara
giltiga; väggar, objekt, stängda grindar och fiender får inte kringgås.
Bakåtrutan får inte vara upptagen av en fiende. Objektets destination måste
vara tillåten stenmark: inte vatten/lava (inte ens med båt/dräkt), grind,
checkpoint, utgång eller spak. Dolda skatter räknas inte som synliga hinder.
Stänger själva objektflytten grinden bakom spelaren avvisas hela draget utan
ändringar. En tidsgrind följer däremot samma regel som vanlig gång:
inträdet prövas före landningens timertick och grinden kan löpa ut under spelaren.
Färgen följer lådan, medan terrängens plattfärg/grupp och dolda skatter
stannar på sina rutor. Grindvillkor räknas om även när en platta avtäcks.

Bakåtlandning använder vanliga pickup-, fara- och checkpointregler. På is
utan skridskor fortsätter spelaren **bakåt**, inte tillbaka in i det dragna
objektet. Skridskor stoppar isglidning men inte band. Bandets riktning
bestämmer efterföljande rörelse (och fortsatt isglidning); själva dragkedjan
behåller spelarens ursprungliga ansiktsriktning. Varje tvångssteg använder
vanlig kollision, inklusive eventuell fiendekontakt. Mörker påverkar bara
synlighet, inte möjligheten att dra eller kollidera.

Sugproppen förbrukas inte, behålls vid död/respawn och följer med vid
kartbyte i både vanlig världscarry och SD-session. Precis som skridskor,
ficklampa och pogo påverkas den inte av resursmaskerna `carry`/`reset`.
Nystart/omstart genom ny kartladdning återställer kartan och tar bort
utrustningen tills den hittas igen. Ett drag återställer aldrig tid,
energi, fiender eller en tidigare värld.

#### Fiendesyntax och gränser

En fiende per rad i `@enemies`, med nollbaserade koordinater:

```text
@enemies
patrol 3 2 loop area 1 1 8 6 route 3,2 7,2 7,5 3,5
patrol 12 2 pingpong route 12,2 16,2
chaser 20 4 area 18 1 28 8
chaser 30 4
```

- `patrol X Y loop|pingpong [area X0 Y0 X1 Y1] route X,Y ...`
  anger ordnade vägpunkter. `loop` återvänder från sista till första;
  `pingpong` vänder vid ändpunkterna. Segmenten är vågräta/lodräta, även
  start till första punkt och sista till första i en slinga. Patrullen
  går en ruta mot nästa punkt; blockeras nästa ruta väntar den, utan omväg.
  En ruttpunkt ger en stillastående patrull när punkten nåtts.
  Statiska hinder/farlig terräng längs hela segmentet är loaderfel; flyttbara
  objekt, NPC, dörrar och grindar får däremot spärra rutten dynamiskt.
- `chaser X Y [area X0 Y0 X1 Y1]` jagar spelarens aktuella position.
- `area` är en **inklusive** rektangel. Utelämnad rektangel betyder hela
  kartan. Start, rutt, sökning och varje steg måste hålla sig inom området.
  Om spelaren står utanför jagarens område väntar jagaren.
- Högst **8 fiender**, **16 vägpunkter per patrull**, karta högst **80×80**.
  Ogiltiga typer, lägen, argument, koordinater, startkollisioner, områden,
  rutter och överskridna gränser ger loaderfel med radnummer.
- Jagaren använder bredden-först-sökning på den **aktuella** kartan inför
  varje steg, med grannordningen **upp, höger, ned, vänster**. Varje ruta
  besöks högst en gång per sökning. Ingen väg betyder vänta, inte ett
  vägggenomträngande eller girigt reservsteg.
- Sökarbetsytan delas av alla fiender: två 16-bitarsfält om 6400 element,
  totalt **25 600 byte per World**, inte per fiende. Högst åtta sökningar
  per handling (högst **204 800 grannkontroller**, plus initiering och
  vägspårning); inga nya stora stack- eller per-fiende-kartarrayer.

Väggar, träd, spruckna väggar, spakar, stängda grindar, alla dörrar, stenar,
färglådor, sönderslagbara lådor, buskar och NPC blockerar. Andra fiender
blockerar också; fiender uppdateras i deklarationsordning och byter inte
plats med varandra. De knuffar aldrig och plockar inte upp föremål.
Flyttade objekt och ändrade grindar påverkar nästa förflyttning direkt.

Fiender går inte på vatten, lava, giftmark, träsk, vassa stenar eller minor.
Is och band är vanlig mark för dem: ingen glidning/tvångsrörelse eller
terrängskada. De har ingen energi, inget HP och inget stridssystem.
Mörker/dimma ändrar inte kollision eller sökning men döljer fienden i
enhetsvyn och webblivevyn enligt samma synlighetsregler som övriga objekt.
Patrull och jagare har skilda symboler; sugpropp visas både som pickup och
ägd utrustning i HUD.

#### Gemensam turordning

| Input | Timers/steg | Fiender |
|---|---|---|
| Lyckad gång eller knuff | Befintlig tick per landning | En gemensam fiendetur efter hela kedjan |
| Lyckat drag | Som gång, inklusive bakåtlandning | En tur efter hela kedjan |
| Lyckat pogohopp | En tick på landningen, inte överflugna rutor | En tur efter eventuell tvångskedja |
| Is-/bandkedja | En tick per faktisk landning, högst 128 extra | Ingen extra fiendetur per tvångssteg |
| Upplåst dörr | Ingen gång/timertick, som tidigare | En tur |
| Lyckad spakaktivering | Ingen gång/timertick, som tidigare | En tur |
| Blockerad input, slag, bara riktningsändring | Ingen tick | Ingen tur |
| Rendering, paus, dialog, val av handling | Ingen tick | Ingen tur |

1. Kontrollera handlingen; flytta objekt/öppna dörr/ändra spak, avslöja
   eventuell skatt och räkna om grindvillkor. Ett blockerat drag ändrar inget.
2. Vid varje faktisk landning: ticka timers, flytta spelaren och öka steg,
   aktivera spelarens platta, kontrollera fiendekontakt; därefter mina,
   dimavtäckning, lava/övrig terrängskada, checkpoint, utgång och pickup.
   Död avbryter direkt. Ett knuffat/draget objekts platta aktiveras också
   enligt den befintliga knuffordningen efter den manuella landningen.
3. Lös hela is-/bandkedjan med samma landningsregler (högst 128 extra
   landningar). Död/utgång avbryter; befintlig stoppregel för mina/lavabränning
   behålls.
4. Om spelaren fortfarande lever och världen inte lämnats: varje fiende får
   högst ett steg i deklarationsordning, med kontaktkontroll.

Spak- och dörrhandlingar hoppar över landning/timertick men låter fienderna
agera. De ändrar inte äldre timers godtyckligt.

Kontakt ger **2 energi** via befintlig skada/liv/respawn, högst en
kontaktskada under samma handling även med flera fiender/tvångslandningar.
Spelaren kan gå in på en fienderuta; fienden väntar där tills spelaren
lämnar. En fiende som försöker gå in på spelarens ruta gör skada men
stannar på sin gamla ruta. Pogo skadar inte vid överflygning; landningen
kontrolleras som vanlig kontakt. Vid död avbryts kedjan och återstående
fiendeuppdateringar; respawn är ingen ny förflyttning eller återställning.
Utgång avbryter också fortsatta uppdateringar.

#### Save/restore, checkpoint och SD-session

Motorformat **6** behåller format 5 och lägger före CRC till:
sugproppsbyte, fiendeantal, 32-bitars definitionssignatur och fyra byte per
fiende (**x, y, målindex, riktning**). Signaturen förhindrar tyst återläsning
mot andra fiender/rutter/områden. Tillstånd valideras innan världen ändras.
Format **1–5** läses med sugpropp **av** och fiender i kartans initiala
läge/ruttriktning. Äldre enum-id:n behåller sin betydelse. Äldre firmware
kan inte läsa format 6. SD-sessionens yttre container förblir **version 1**.

Checkpoint flyttar bara respawnpunkten. Död behåller flyttade objekt,
fiendernas aktuella ruttillstånd, timers och utrustning; full omstart laddar
kartan på nytt. SD-sparning omsluter världens fiendetillstånd som övrig
världsdata. Vid resa till en ny karta laddas den kartans fiender från
definitionen; de överförs inte som spelarutrustning.

Se [0.21.1-demots README](adventures/s4_demo/README.md) för exakt
lösning, produktionsloader-/motortest och manuella kontroller. Fysisk
ESP32-/skärm-/touchkontroll och SD-hantering på enheten återstår.
Lokalt firmwarebygge kunde inte köras eftersom denna körmiljö saknar ESP-IDF;
aktuell byggstatus i CI visas på PR:n. Releaseworkflow, tags och releases
ändras inte av spelmekaniken.

---

## 11. Egna sprites – 32×32 PNG (nytt i v0.17.0)

Sedan **0.21.2** kan **egna resurser** få **32×32 PNG med alfakanal** i
äventyrets `bitmaps/`-mapp, högst **16 KiB per bild**. Tomt bildfält ger
en procedurritad gul påse. En angiven men saknad/ogiltig bild ger både
loggvarning och skärmmeddelande, därefter samma synliga påse.
PNG avkodas en gång per kartladdning i UI-tasken med LVGL; färg/alpha
cachas i PSRAM och skalas till 16/32 px. Firmware behöver
`CONFIG_LV_USE_LODEPNG=y` (ingår nu i `sdkconfig.defaults`; uppdatera en
befintlig `sdkconfig` via menuconfig eller generera om den enligt projektets
bygginstruktion). Webblivevyn använder samma fil via befintlig SD-filendpoint.
Generell ersättning av standardobjektens sprites och visning av `cover`
är **inte** implementerade av denna leverans; `cover` läses som metadata.

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

## 12. Flerkartor och portaler

En `.adv`-fil kan innehålla en huvudkarta (`@map`, internt id 1) och högst
tre extra kartor (`@map 2` … `@map 255`, med unika id:n). Varje karta är
fortfarande högst 80×80 och måste ha exakt ett `S`. `@meta map_id` fortsätter
vara filens separata identitet. Kartorna i en fil är inte samma sak som flera
separata `.adv`-filer i äventyrsmappen.

Kartval saknas som standard och betyder då alltid huvudkartan, även efter
deklaration av en extrakarta:

```text
@treasures
2 1 pogo 2
map=2: 3 2 flash 1

@legend map=2
g = plate 1

@rules map=2
lava_damage: 3

@npc guard map=2
name: Väktaren | Guard
num: 1

@destinations
home 1,1
map=2: cave 2,1

@portals
2,1 -> cave
map=2: 7,1 -> home

@map
########
#S....X#
########
@map 2
##########
#S......1#
##########
```

`map=N` på taggraden väljer hela sektionen. `map=N:` väljer enskilda rader
i `@rules`, `@treasures`, `@enemies`, `@resources` och `@dark`. `@legend`,
`@npc`, `@plategroup`, `@gategroup`, `@andgate` och `@timedgate` använder
taggradsväljare. Väljaren gäller bara där den skrivs; den skapar inget
"senaste karta"-läge. Id måste vara deklarerat. Koordinaterna är lokala och
nollbaserade.

Legend, terräng, resurser/pickups, skatter, fiender, dimma, mörker och
grupptillstånd är kartlokala. NPC-siffror 1–9 är också lokala; NPC- och
story-id:n/noder delas i filen. Resursglyfen är lokal, men inventory-id:t är
globalt och kan återdeklareras på annan karta bara med samma namn, tak och
bild. Story-inventory, flaggor, räknare och quests delas.

Huvudkartans `start_energy`, `energy_cap`, `lives`, `carry` och `reset` anger
de delade energi-/livreglerna och carry/reset-policyn. Att ange någon av dessa
i en extrakartas `@rules` är ett fel. Övriga regler är lokala; utelämnade
värden använder motorns standard. Intern resa bevarar aktuell energi, liv och
utrustning. `carry`/`reset` påverkar bara resa till nästa separata fil.

Destinationer har filunika namn. En portalrad `x,y -> namn` använder
destinationstabellen; utan `map=` avses huvudkartan. Källa och destination
kontrolleras som säker, gångbar ruta vid start. Om objekt senare blockerar
ankomsten avvisas resan atomiskt och spelaren stannar på portalkällan. Resan
kostar ingen extra tur, och endast aktiv karta tickar. Ankomst spärras från
att genast utlösa en portal till dess att spelaren lämnar ankomstrutan.

`effects: world travel cave` använder samma destinationer. Kartverb kan
rikta sig till vald karta:

```text
effects: world open map=2 4 2
effects: world npc_move map=2 1 4 6
effects: world npc_remove map=2 1
```

Stödda befintliga kartverb är `open`, `npc_move` och `npc_remove`; utan
`map=N` avses huvudkartan. En ej besökt målbild startas från kartans grundläge
före ändring.

### Story-styrda terräng- och objektändringar (0.23.4)

Två nya world-verb kan användas i dialogeffekter:

```text
effects: world tile map=1 6 1 8
effects: world object map=2 6 2 3
```

Syntaxen är `world tile map=N x y tile-id` respektive
`world object map=N x y object-id`. `map=N` är obligatoriskt för båda verb;
N måste vara en deklarerad intern karta i samma fil. `x,y` är nollbaserade
lokala koordinater. ID:t är det numeriska värdet i `Otr::Tile` eller `Otr::Obj`,
inte en glyf eller ett `@legend`-värde.

Tillåtna terräng-ID:n:

| ID | Terräng | ID | Terräng |
|---:|---|---:|---|
| 0–10 | gräs, sand, golv, grus, vägg, sprucken vägg, träd, vatten, bro, lava, basalt | 15 | krater |
| 17–21 | is, transportband upp/höger/ned/vänster | 22–24 | giftmark, träsk, vassa stenar |

Tillåtna objekt-ID:n:

| ID | Objekt | ID | Objekt |
|---:|---|---:|---|
| 1–5 | sten, låda, nyckel, dörr, båt | 7–11 | buske, dryck, hjärta, skyddsdräkt, extraliv |
| 14–15 | bomb 3×3, bomb 4×4 | 18–23 | färgnycklar röd–cyan |
| 24–29 | färgdörrar röd–cyan | 30 | skridskor |
| 31–36 | färglådor röd–cyan | 37 | sugpropp |

Effekterna är avsiktligt begränsade. Plattor, grindar, spakar, checkpoints,
utgångar och färgplattor kan inte skapas med `tile`. Objekt som NPC, mina,
ädelsten, ficklampa, hoppstylta och egna resurser kan inte skapas med
`object`; bland annat undviker detta att tappa mängd-/räknar- eller
storyinventeringsdata. `object` kräver ledig, gångbar mark och kan inte placera
föremål på plattor, grindar, spakar, checkpoints, utgångar, is eller
transportband.

Båda verben avvisas utan mutation om kartan/koordinaten/ID:t är ogiltigt, om
rutan innehåller spelaren, en fiende, ett synligt objekt eller en dold skatt,
eller om rutan är en portal-/destinationskälla eller ankomst. Objektplacering
kontrollerar också marktypen. Avvisad effekt ger ett synligt fel i spelet och
committar inte efterföljande `take`, `give`, flaggor, räknare eller uppdrag.
En effektlista får innehålla högst en sådan terräng-/objektändring och får inte
kombinera den med en annan kartstyrd `world`-åtgärd; andra storyeffekter kan
stå före eller efter den. Lägg ett villkor och en flagga runt en engångsbelöning
så att dialogvalet inte förbrukar material eller belönar spelaren flera gånger:

```text
choice: Bygg bron. | Build the bridge. => END ?? has:plank & !bridge_built !! world tile map=1 6 1 8; take plank; set bridge_built
```

Ändringar på en ännu obesökt karta börjar från dess grundkarta och finns kvar
när den öppnas. De använder befintliga kartbilder/checkpoints och save/restore;
save-formatet har inte ändrats. `restart` börjar om hela äventyret och återställer
story och kartor till startläget. Se
[Motoräventyrets storytest](../sdcard_bundle/otr/adventure/motoraventyr/README.md)
för en aktiv bro, en placerad nyckel på obesökt karta och negativa testfall.

Besökta kartor bevarar ändringar och World-tillstånd. Checkpoints är
äventyrsgemensamma och innehåller kart-id plus koordinat; dödsrespawn kan byta
karta utan att rulla tillbaka andra ändringar. Restart startar om hela
äventyret. Multimap-save v2 innehåller alla besökta kartbilder, aktiv karta,
story, checkpoint och portalskydd. Restore avvisar filsignatur som ändrats
sedan sparning; v1-save stöds fortsatt för äldre enkarts-/separatfiläventyr,
inte som multimap-migrering.

Begränsningar: högst 4 interna kartor, 32 destinationsnamn, 32 portalrader
och 64 KiB per `.adv`-fil. Varje karta måste kunna laddas av motorn; övriga
befintliga begränsningar för kartor, NPC:er, fiender, resurser och storydata
gäller fortfarande. På ESP måste `Adv::Loaded` och `Otr::World` ligga i PSRAM;
UI-save-bufferten måste vara minst `Adv::SdSession::SAVE_CAP` (180,122 byte).
Se [formatreferensen](ADVENTURE_MULTIMAP_FORMAT.md) för minnesmått och
fullständiga kapaciteter.

Körande exempel: [multimap_portals](adventures/multimap_portals/README.md).

---

## 13. Validering

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
  krockar med en inbyggd terräng/objekt, eller resursantalet överskrids.

`Adv::parse()` bygger data men anropar inte dessa två validerare automatiskt.
Kontrollera även resultatet med `Script::validate()` och `World::load()`:
kartan måste ha lika långa rader, vara inom **80×80** och innehålla exakt ett
`S`. `@legend` har som tidigare företräde framför `@resources`; använd
olika glyfer för att faktiskt placera resurser. SD-sessionen kör både
script- och motorvalidering före spelstart.

---

## 14. Import/export-status

`.adv`-formatet läses på enheten med produktionsladdaren. Hostens
referensparser är inte funktionellt likvärdig för alla formatversioner.

- **På enheten:** `main/adv_loader.{h,cpp}` är en firmware-säker parser med
  fast gräns och PSRAM-scratch för `.adv`-förbehandling; `Adv::Loaded`
  (88,088 byte på host) ligger i PSRAM. `main/adv_catalog.{h,cpp}` skannar `/otr/adventure/` och
  bygger menyn; `main/adv_sd_session.{h,cpp}` sköter spara-per-mapp och
  carry/reset. Detta ingår i firmware-bygget (`idf.py build`).
- **På värddator:** `tools/adv_parse.{h,cpp}` är fortfarande en S1-parser och
  validerar inte S2–S4-syntax. Använd produktionsladdaren `Adv::parseFile`.

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

# S2-demo: produktionsladdare, motor, lösning och sparning/återläsning
g++ -std=c++17 -Wall -Wextra -Imain -Icomponents/otr_engine/include \
    -Icomponents/otr_story/include tests/test_s2_demo.cpp main/adv_loader.cpp \
    main/adv_catalog.cpp main/adv_sd_session.cpp components/otr_engine/otr_world.cpp \
    components/otr_story/otr_story.cpp -o tests/test_s2_demo && ./tests/test_s2_demo

# Flerkartor/portaler: parser, portals, blocked arrivals, checkpoint and v1 regression
g++ -std=c++17 -Wall -Wextra -Imain -Icomponents/otr_engine/include \
    -Icomponents/otr_story/include tests/test_adv_multimap.cpp main/adv_loader.cpp \
    main/adv_sd_session.cpp main/adv_live.cpp components/otr_engine/otr_world.cpp \
    components/otr_story/otr_story.cpp -o tests/test_adv_multimap && ./tests/test_adv_multimap

# Motoräventyret: delivered files, production parser/World and story/session traces
g++ -std=c++17 -Wall -Wextra -Werror -Imain -Icomponents/otr_engine/include \
    -Icomponents/otr_story/include tests/test_motoraventyr.cpp main/adv_loader.cpp \
    main/adv_sd_session.cpp components/otr_engine/otr_world.cpp \
    components/otr_story/otr_story.cpp -o /tmp/test_motoraventyr && /tmp/test_motoraventyr
```

> **Hårdvarustatus:** Ägaren har testat PR #11 på hårdvara och rapporterat att
> det fungerar bra. Det påståendet innebär inte att alla format eller samtliga
> kontroller i den här guiden har verifierats fysiskt.

---

## 15. Kompletta exempel

- **Hebron-lägret (Josef, 1 Mos 37):**
  [`docs/adventures/example_joseph_hebron.adv`](adventures/example_joseph_hebron.adv)
  – den verkliga första Josef-kartan: grindvakt (Ruben), nyckel/dörr-pussel och
  flerspråkig dialog.
- **Stenhuggarens stig (nyheterna i v0.17.0):**
  [`docs/adventures/example_resources_demo.adv`](adventures/example_resources_demo.adv)
  – `@about`, `@treasures`, `@resources`, carry/reset och gångbar ram i ett kort,
  körbart äventyr.
- **S2-verkstadsbanan (v0.20.1):**
  [`docs/adventures/s2_demo/README.md`](adventures/s2_demo/README.md) och
  [`docs/adventures/s2_demo/s2_demo.adv`](adventures/s2_demo/s2_demo.adv) –
  spakar, tidsgrind, is, band, färgnycklar och par.
- **S3-provbana (v0.21.0):**
  [`docs/adventures/s3_demo/README.md`](adventures/s3_demo/README.md) och
  [`docs/adventures/s3_demo/s3_demo.adv`](adventures/s3_demo/s3_demo.adv) –
  skridskor, tre skadliga terränger med/utan dräkt, färgsokoban,
  spak+platta-AND och skatt under en flyttbar låda.
- **Sugpropp och fiender (v0.21.1):**
  [S4-demots README](adventures/s4_demo/README.md) och externa SD-karta –
  dold sugpropp, färglåda/platta, drag, patrull, jagare, områden och
  blockering med flyttbara objekt.
- **Alla inbyggda äventyr som `.adv`:**
  [`docs/adventures/generated/`](adventures/generated/) – byn, josef (2 kartor),
  minfaltet och testvarlden, exporterade direkt från den kompilerade
  motordatan. Kopiera en som startpunkt.
