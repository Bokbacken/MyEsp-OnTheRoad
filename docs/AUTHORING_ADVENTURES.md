# Skriva egna aventyr i ASCII (.adv)

Det har dokumentet visar hur du skriver ett helt aventyr till MyEsp-OnTheRoad
i ett enkelt, lasbart textformat. Tanken ar att man ska kunna **importera**
fardiga aventyr och **redigera/skapa egna** utan att roera C++-koden: du ritar
kartan med ASCII-tecken och skriver dialogen som text pa svenska och engelska.

Formatet ar avsiktligt **nara motorns verkliga datastrukturer** sa att det ar
realistiskt importbart:

| Del i `.adv`-filen | Motsvarar i koden | Fil |
|--------------------|-------------------|-----|
| `@map` + `@rules`  | `struct MapDef` / `struct Rules` | `components/otr_engine/include/otr_world.h`, `otr_maps*.cpp` |
| teckenlegenden     | `decode()` (tecken -> `Tile`/`Obj`) | `components/otr_engine/otr_world.cpp` |
| `@flags/@counters/@items/@quests/@npc/@node` | `struct Script` m.fl. | `components/otr_story/include/otr_story.h`, `otr_story_*.cpp` |

> Ett komplett, fungerande exempel finns i
> [`docs/adventures/example_joseph_hebron.adv`](adventures/example_joseph_hebron.adv)
> och aterges i sin helhet langst ner i det har dokumentet. Det ar den **verkliga**
> Hebron-kartan (Jakobs lager) ur Josef-kapitlet (1 Mosebok 37) uttryckt i formatet.

---

## 1. Filens uppbyggnad

En `.adv`-fil ar vanlig text (UTF-8) och bestar av **sektioner** som borjar med
`@`. Rader som borjar med `#` ar kommentarer. Tomma rader ignoreras overallt
*utom* inuti `@map`, dar varje rad ar en kartrad.

Sektionerna, i ordning:

```
@meta        namn, varld-id, kart-id, beskrivning (SV/EN)
@rules       spelregler (energi, liv, lava, dimma ...)
@map         sjalva kartan, ritad med ASCII-tecken
@flags       deklarera alla flaggor du anvander
@counters    deklarera alla raknare
@items       deklarera foremal (id + namn SV/EN)
@quests      deklarera uppdrag (id + namn SV/EN)
@npc <id>    en person (upprepas per person)
@node <id>   en dialograd (upprepas manga ganger)
@legend      informativ teckenreferens (valfri, parsas ej)
```

---

## 2. @meta

```
@meta
id: joseph_camp          # internt id for aventyret/kartan
world: 4                 # WorldId 1..255 (Josef = 4)
map_id: 4                # stabilt kart-id som sparfiler anvander
name.sv: Jakobs lager
name.en: Jacob's camp
desc.sv: Hebron. Hamta manteln hos Jakob och ge den till Josef.
desc.en: Hebron. Fetch the robe from Jacob and give it to Joseph.
```

`world` ar varldens id (`enum WorldId`). `map_id` ar kartans stabila id som
sparfilen refererar till - varje karta i samma varld maste ha ett unikt
`map_id`. (I Josef-varlden: lagret = 4, vagen = 5.)

---

## 3. @rules - spelregler

Faltnamnen motsvarar exakt `struct Rules` i `otr_world.h`:

```
@rules
start_energy: 8          # energi vid start (och forsta maxvardet)
energy_cap: 20           # hur hogt max-energin kan vaxa med hjartan
lives: infinite          # "infinite" eller ett tal 1..9
lava_damage: 3           # energi per lava-steg utan draktladdning
mine_damage: 4           # energi per mina
suit_base_steps: 3       # draktladdningar vid upplockning ...
suit_per_extra_max: 1    # ... + per permanent extra energienhet
potion_default: 2        # energi en trolldryck ger utan eget varde
heart_default: 1         # max-energi ett hjarta ger utan eget varde
fog: false               # true = minsvepar-varld (marken dold tills avtackt)
```

---

## 4. @map - rita kartan

Kartan ar ett rutnat, en rad per textrad, **ett tecken per ruta**. Alla rader
maste vara **lika langa** (det blir kartans bredd) och antalet rader blir
hojden.

**Granser (fran motorn, `otr_world.h`):**
- Max bredd **64**, max hojd **64** (`MAX_W` / `MAX_H`).
- Exakt **ett** `S` (startruta) kravs.
- Minst ett `X` (utgang/mal) om kartan ska kunna klaras.
- Hogst **9 talbara personer** per karta (siffrorna `1`..`9`).

### Teckenlegend

Varje tecken avkodas till en terrang (`Tile`) och ev. ett foremal (`Obj`),
exakt som `decode()` i `otr_world.cpp`:

**Terrang**

| Tecken | Betydelse | Gangbar? |
|:------:|-----------|:--------:|
| `.` | gras | ja |
| `,` | sand | ja |
| `_` | stengolv | ja |
| `#` | vagg | nej |
| `%` | sprucken vagg (ga sonder efter 2 slag) | nej |
| `T` | trad | nej |
| `~` | vatten | nej (bro/bat kravs) |
| `^` | lava | ja men skadar |
| `o` | tryckplatta | ja |
| `=` | grind | oppnas av tryckplatta |
| `Q` | checkpoint (aterkomstpunkt) | ja |
| `X` | utgang (mal) | ja |
| `S` | start (star pa gras) | ja |

**Foremal** (ligger pa en terrang; stor/liten bokstav = gras/stengolv)

| Tecken | Betydelse |
|:------:|-----------|
| `R` / `r` | sten pa gras / golv (knuffbar) |
| `c` / `C` | lada pa gras / golv (slas sonder) |
| `k` / `K` | nyckel pa gras / golv |
| `D` | last dorr (pa golv) |
| `B` | bat (pa sand) |
| `*` / `+` / `$` | adelsten pa gras / golv / sand |
| `b` | buske (ett slag) |
| `p` | trolldryck (energi) |
| `h` | hjarta (hojer max-energi) |
| `u` | skyddsdrakt (lava) |
| `l` | extra liv |
| `a` / `z` | bomb (3x3 / 4x4, minvarldar) |
| `m` | dold mina (minvarldar) |

**Personer**

| Tecken | Betydelse |
|:------:|-----------|
| `1`..`9` | en talbar person; siffran kopplas till en `@npc` via dess `num` |

En person star alltid pa gras och **blockerar** sin ruta. Det ar sa man bygger
"grindvakter": placera en person i enda oppningen (som Ruben i gapet i
tradraden), sa slapper hen ingen forbi forran en dialogeffekt flyttar hen at
sidan (`world npc_move ...`).

---

## 5. Story - deklarera namn forst

Allt som anvands i villkor och effekter maste forst **deklareras**, annars
underkanner motorns `validate()` aventyret.

```
@flags                   # en flagga per rad (av/pa-varden)
met_jakob
robe_given

@counters                # en raknare per rad (heltal)
trust

@items                   # id : namn.sv : namn.en
robe : Brokig mantel : Ornate robe

@quests                  # id : namn.sv : namn.en
deliver_robe : Manteln till Josef : The robe for Joseph
dotan : Mot Dotan : To Dothan
```

Granser (fran `otr_story.h`): max 256 flaggor, 16 raknare, 32 foremal, 16
uppdrag.

---

## 6. @npc - en person

```
@npc jakob               # internt id (anvands som "speaker" i noder)
name: Jakob | Jacob      # visningsnamn: svenska | engelska
look: 0                  # sprite-/portrattindex i UI:t
num: 1                   # matchar siffran '1' i @map
```

`num` kopplar personen till ratt siffra pa kartan. Varje `num` maste vara unikt
(1..9).

---

## 7. @node - en dialograd

En nod ar en replik. **Flera noder far dela id** - den forsta vars `cond`
stammer anvands. Sa far en person "minne": en variant for forsta motet, en for
senare, osv. En nod `<npc>.start` ar ingangen nar man pratar med personen.

```
@node jakob.start
cond: !met_jakob                 # villkor (utelamna = alltid). Se nedan.
speaker: jakob                   # npc-id. Utelamna = berattare.
ref: 1 Mos 37:3                  # bibelreferens under texten (valfri)
quote: 1Mo37:3                   # nyckel till citatfil (valfri)
effects: set met_jakob; give robe; quest deliver_robe active
text.sv: Du maste vara den resande. Jag ar Jakob ...
text.en: You must be the traveller. I am Jacob ...
choice: Sa klart, var ar Josef? | Of course, where is Joseph? => jakob.where
choice: Varfor just Josef? | Why Joseph especially? => jakob.why
choice: Hej da! | Bye! => END
```

### Valbara rader (choice)

Grundform:

```
choice: <svensk text> | <engelsk text> => <mal-nod eller END>
```

Med villkor och effekter (valfritt, i den har ordningen):

```
choice: <sv> | <en> => <mal> ?? <villkor> !! <effekter> ~~ <tips.sv> | <tips.en>
```

- `=>` malnoden (`END` avslutar samtalet)
- `??` villkor - raden visas bara om villkoret stammer
- `!!` effekter - tillampas nar man valjer raden
- `~~` tips - om satt visas raden **gragad med tipset** nar villkoret inte
  stammer (annars doljs den helt)

Exempel (fran josef.start - att ge bort manteln):

```
choice: Har, den ar din. | Here, it's yours. => josef.getrobe ?? has:robe !! take robe; set robe_given; set josef_ready; quest deliver_robe done; sound quest
```

### Granser for text (fran `otr_story.h`)

- Nodtext: max **200 tecken**.
- En valrad: max **36 tecken** (en knapprad vid typsnitt 14).
- Max **4 val** per nod.
- Saknas en engelsk text faller motorn tillbaka pa den svenska.

---

## 8. Villkor och effekter (manuskriptsyntaxen)

Detta ar exakt samma minisyntax som motorn tolkar (se kommentaren overst i
`otr_story.h`).

**Villkor** - termer sammanfogade med `&`, var och en kan negeras med `!`:

| Term | Sant nar |
|------|----------|
| `flagga` | flaggan ar satt |
| `!flagga` | flaggan ar inte satt |
| `has:foremal` | man har minst ett av foremalet |
| `counter>=N` `counter<=N` `counter=N` | raknarvardet jamfors |
| `quest:q=none\|active\|done` | uppdraget har det laget |

Exempel: `josef_ready & !met_ruben`

**Effekter** - sammanfogade med `;`:

| Effekt | Gor |
|--------|-----|
| `set f` / `clear f` | satter/nollar flagga |
| `add c N` | raknare += N |
| `cnt c N` | raknare = N |
| `give i [N]` / `take i [N]` | ger/tar foremal |
| `quest q active\|done\|none` | andrar uppdragslage |
| `world VERB [a [b [c]]]` | skickas till spelet (se nedan) |
| `sound NAMN` | spelar ett ljud |

**world-verb** skickas vidare till varlden/enheten. De vanligaste:

| Effekt | Gor |
|--------|-----|
| `world npc_move <num> <x> <y>` | flyttar person `num` till rutan (x,y) - sa oppnar man en grindvakt |
| `world npc_remove <num>` | tar bort person `num` |
| `world open <x> <y>` | tar bort foremalet pa (x,y) (t.ex. en dorr) |

---

## 9. Pussel: sa later du en person oppna vagen

Monstret (anvant for bade Ruben och kopmannen i Josef-kapitlet):

1. Placera personen i enda oppningen pa kartan (t.ex. `3` i gapet i tradraden).
   Sa lange hen star dar kan spelaren inte passera (personen blockerar rutan).
2. Lat en nod avslutas med en berattarnod vars effekt flyttar personen:
   `effects: world npc_move 3 18 6` flyttar person 3 till (18,6), bort fran
   gapet.
3. Nu ar vagen till `X` (utgangen) oppen.

Villkoren styr NAR det far hanta: Ruben-noden kraver `josef_ready & !met_ruben`,
dvs. spelaren maste forst ha gett manteln till Josef (som satter `josef_ready`).

---

## 10. Validering

Motorns `Script::validate()` kontrollerar aventyret och ar den basta
checklistan nar du skrivit klart. Den ger fel om:

- en flagga/raknare/foremal/uppdrag anvands utan att vara deklarerad,
- ett choice-mal (`=>`) pekar pa en nod som inte finns,
- en nod inte gar att na fran nagon `<npc>.start`,
- `speaker` pekar pa en okand npc,
- tva personer har samma `num`, eller `num` inte ar 1..9,
- nodtext > 200 tecken eller valtext > 36 tecken,
- villkor/effekt har felaktig syntax.

Kartan kontrolleras av `World::load()`: lika langa rader, inom 64x64, exakt ett
`S`.

---

## 11. Status for import/export

`.adv`-formatet ar **specificerat, dokumenterat och har en fungerande
host-parser**. Parsern (`tools/adv_parse.h` / `tools/adv_parse.cpp`) laser en
`.adv`-fil rakt in i motorns verkliga strukturer (`Otr::MapDef` + rader och
`Otr::Story::Script`):

```c++
AdvDoc doc; std::string err;
if (parseAdvFile("example.adv", doc, err)) {
    Otr::World w;  w.load(doc.map);          // kartan laddas i motorn
    char e[1024]; doc.script.validate(e, sizeof e);   // storyn valideras
}
```

Ett host-test (`tests/test_adv_parser.cpp`) parsar exempelfilen, laddar kartan
med `World::load()` och kor `Script::validate()` - det bevisar att formatet
importeras till den **faktiska** motorn, inte en leksaksmodell. Bygg och kor:

```
g++ -std=c++17 -Wall -Wextra -Itools -Icomponents/otr_engine/include \
    -Icomponents/otr_story/include tests/test_adv_parser.cpp tools/adv_parse.cpp \
    components/otr_engine/otr_world.cpp components/otr_story/otr_story.cpp \
    -o tests/test_adv_parser && ./tests/test_adv_parser
```

Parsern ar ett **host-verktyg** och ingar inte i firmware-bygget. Nasta steg
(se `ROADMAP.md`) ar att lasa `.adv` aven pa enheten (fran SD/flash), sa att
fardiga aventyr kan importeras direkt dar.

---

## 12. Komplett exempel: Hebron-lagret (Josef, 1 Mos 37)

Nedan ar hela `docs/adventures/example_joseph_hebron.adv` - den verkliga
forsta Josef-kartan uttryckt i formatet. Oppna filen for den aktuella versionen;
den ar kort nog att lasa i sin helhet och ar ett riktigt, fungerande exempel
(inte en leksak).

Se: [`docs/adventures/example_joseph_hebron.adv`](adventures/example_joseph_hebron.adv)
