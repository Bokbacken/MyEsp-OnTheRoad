# Skriva egna äventyr i ASCII (.adv)

Det här dokumentet visar hur du skriver ett helt äventyr till MyEsp-OnTheRoad
i ett enkelt, läsbart textformat. Tanken är att man ska kunna **importera**
färdiga äventyr och **redigera/skapa egna** utan att röra C++-koden: du ritar
kartan med ASCII-tecken och skriver dialogen som text på svenska och engelska.

Formatet är avsiktligt **nära motorns verkliga datastrukturer** så att det är
realistiskt importbart:

| Del i `.adv`-filen | Motsvarar i koden | Fil |
|--------------------|-------------------|-----|
| `@map` + `@rules`  | `struct MapDef` / `struct Rules` | `components/otr_engine/include/otr_world.h`, `otr_maps*.cpp` |
| teckenlegenden     | `decode()` (tecken -> `Tile`/`Obj`) | `components/otr_engine/otr_world.cpp` |
| `@flags/@counters/@items/@quests/@npc/@node` | `struct Script` m.fl. | `components/otr_story/include/otr_story.h`, `otr_story_*.cpp` |

> Ett komplett, fungerande exempel finns i
> [`docs/adventures/example_joseph_hebron.adv`](adventures/example_joseph_hebron.adv)
> och återges i sin helhet längst ner i det här dokumentet. Det är den **verkliga**
> Hebron-kartan (Jakobs läger) ur Josef-kapitlet (1 Mosebok 37) uttryckt i formatet.

---

## 1. Filens uppbyggnad

En `.adv`-fil är vanlig text (UTF-8) och består av **sektioner** som börjar med
`@`. Rader som börjar med `#` är kommentarer. Tomma rader ignoreras överallt
*utom* inuti `@map`, där varje rad är en kartrad.

Sektionerna, i ordning:

```
@meta        namn, värld-id, kart-id, beskrivning (SV/EN)
@rules       spelregler (energi, liv, lava, dimma ...)
@map         själva kartan, ritad med ASCII-tecken
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
id: joseph_camp          # internt id för äventyret/kartan
world: 4                 # WorldId 1..255 (Josef = 4)
map_id: 4                # stabilt kart-id som sparfiler använder
name.sv: Jakobs läger
name.en: Jacob's camp
desc.sv: Hebron. Hämta manteln hos Jakob och ge den till Josef.
desc.en: Hebron. Fetch the robe from Jacob and give it to Joseph.
```

`world` är världens id (`enum WorldId`). `map_id` är kartans stabila id som
sparfilen refererar till - varje karta i samma värld måste ha ett unikt
`map_id`. (I Josef-världen: lägret = 4, vägen = 5.)

---

## 3. @rules - spelregler

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
fog: false               # true = minsvepar-värld (marken dold tills avtäckt)
```

---

## 4. @map - rita kartan

Kartan är ett rutnät, en rad per textrad, **ett tecken per ruta**. Alla rader
måste vara **lika långa** (det blir kartans bredd) och antalet rader blir
höjden.

**Gränser (från motorn, `otr_world.h`):**
- Max bredd **64**, max höjd **64** (`MAX_W` / `MAX_H`).
- Exakt **ett** `S` (startruta) krävs.
- Minst ett `X` (utgång/mål) om kartan ska kunna klaras.
- Högst **9 talbara personer** per karta (siffrorna `1`..`9`).

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
| `a` / `z` | bomb (3x3 / 4x4, minvärldar) |
| `m` | dold mina (minvärldar) |

**Personer**

| Tecken | Betydelse |
|:------:|-----------|
| `1`..`9` | en talbar person; siffran kopplas till en `@npc` via dess `num` |

En person står alltid på gräs och **blockerar** sin ruta. Det är så man bygger
"grindvakter": placera en person i enda öppningen (som Ruben i gapet i
trädraden), så släpper hen ingen förbi förrän en dialogeffekt flyttar hen åt
sidan (`world npc_move ...`).

---

## 5. Story - deklarera namn först

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

Gränser (från `otr_story.h`): max 256 flaggor, 16 räknare, 32 föremål, 16
uppdrag.

---

## 6. @npc - en person

```
@npc jakob               # internt id (används som "speaker" i noder)
name: Jakob | Jacob      # visningsnamn: svenska | engelska
look: 0                  # sprite-/porträttindex i UI:t
num: 1                   # matchar siffran '1' i @map
```

`num` kopplar personen till rätt siffra på kartan. Varje `num` måste vara unikt
(1..9).

---

## 7. @node - en dialograd

En nod är en replik. **Flera noder får dela id** - den första vars `cond`
stämmer används. Så får en person "minne": en variant för första mötet, en för
senare, osv. En nod `<npc>.start` är ingången när man pratar med personen.

```
@node jakob.start
cond: !met_jakob                 # villkor (utelämna = alltid). Se nedan.
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
- `??` villkor - raden visas bara om villkoret stämmer
- `!!` effekter - tillämpas när man väljer raden
- `~~` tips - om satt visas raden **gråad med tipset** när villkoret inte
  stämmer (annars döljs den helt)

Exempel (från josef.start - att ge bort manteln):

```
choice: Här, den är din. | Here, it's yours. => josef.getrobe ?? has:robe !! take robe; set robe_given; set josef_ready; quest deliver_robe done; sound quest
```

### Gränser för text (från `otr_story.h`)

- Nodtext: max **200 tecken**.
- En valrad: max **36 tecken** (en knapprad vid typsnitt 14).
- Max **4 val** per nod.
- Saknas en engelsk text faller motorn tillbaka på den svenska.

---

## 8. Villkor och effekter (manuskriptsyntaxen)

Detta är exakt samma minisyntax som motorn tolkar (se kommentaren överst i
`otr_story.h`).

**Villkor** - termer sammanfogade med `&`, var och en kan negeras med `!`:

| Term | Sant när |
|------|----------|
| `flagga` | flaggan är satt |
| `!flagga` | flaggan är inte satt |
| `has:föremål` | man har minst ett av föremålet |
| `counter>=N` `counter<=N` `counter=N` | räknarvärdet jämförs |
| `quest:q=none\|active\|done` | uppdraget har det läget |

Exempel: `josef_ready & !met_ruben`

**Effekter** - sammanfogade med `;`:

| Effekt | Gör |
|--------|-----|
| `set f` / `clear f` | sätter/nollar flagga |
| `add c N` | räknare += N |
| `cnt c N` | räknare = N |
| `give i [N]` / `take i [N]` | ger/tar föremål |
| `quest q active\|done\|none` | ändrar uppdragsläge |
| `world VERB [a [b [c]]]` | skickas till spelet (se nedan) |
| `sound NAMN` | spelar ett ljud |

**world-verb** skickas vidare till världen/enheten. De vanligaste:

| Effekt | Gör |
|--------|-----|
| `world npc_move <num> <x> <y>` | flyttar person `num` till rutan (x,y) - så öppnar man en grindvakt |
| `world npc_remove <num>` | tar bort person `num` |
| `world open <x> <y>` | tar bort föremålet på (x,y) (t.ex. en dörr) |

---

## 9. Pussel: så låter du en person öppna vägen

Mönstret (använt för både Ruben och köpmannen i Josef-kapitlet):

1. Placera personen i enda öppningen på kartan (t.ex. `3` i gapet i trädraden).
   Så länge hen står där kan spelaren inte passera (personen blockerar rutan).
2. Låt en nod avslutas med en berättarnod vars effekt flyttar personen:
   `effects: world npc_move 3 18 6` flyttar person 3 till (18,6), bort från
   gapet.
3. Nu är vägen till `X` (utgången) öppen.

Villkoren styr NÄR det får hända: Ruben-noden kräver `josef_ready & !met_ruben`,
dvs. spelaren måste först ha gett manteln till Josef (som sätter `josef_ready`).

### Låst dörr och nyckel

Ett andra, fristående pussel bygger på motorns inbyggda nyckel/dörr-mekanik:

1. Placera en **låst dörr** `D` (på stengolv) som enda vägen in till ett rum
   eller tält, med en person bakom (t.ex. Josef i sitt hus).
2. Göm en **nyckel** `k`/`K` någon annanstans på kartan.
3. Spelaren måste hitta nyckeln först; att gå mot dörren utan nyckel ger
   "dörren är låst", och med nyckel öppnas den (en nyckel förbrukas).

Detta kräver ingen story-effekt - motorn räknar nycklar och dörrar själv, och
det sparas i sparsystem v2.

---

## 10. Validering

Motorns `Script::validate()` kontrollerar äventyret och är den bästa
checklistan när du skrivit klart. Den ger fel om:

- en flagga/räknare/föremål/uppdrag används utan att vara deklarerad,
- ett choice-mål (`=>`) pekar på en nod som inte finns,
- en nod inte går att nå från någon `<npc>.start`,
- `speaker` pekar på en okänd npc,
- två personer har samma `num`, eller `num` inte är 1..9,
- nodtext > 200 tecken eller valtext > 36 tecken,
- villkor/effekt har felaktig syntax.

Kartan kontrolleras av `World::load()`: lika långa rader, inom 64x64, exakt ett
`S`.

---

## 11. Status för import/export

`.adv`-formatet är **specificerat, dokumenterat och har en fungerande
host-parser**. Parsern (`tools/adv_parse.h` / `tools/adv_parse.cpp`) läser en
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
med `World::load()` och kör `Script::validate()` - det bevisar att formatet
importeras till den **faktiska** motorn, inte en leksaksmodell. Bygg och kör:

```
g++ -std=c++17 -Wall -Wextra -Itools -Icomponents/otr_engine/include \
    -Icomponents/otr_story/include tests/test_adv_parser.cpp tools/adv_parse.cpp \
    components/otr_engine/otr_world.cpp components/otr_story/otr_story.cpp \
    -o tests/test_adv_parser && ./tests/test_adv_parser
```

Parsern är ett **host-verktyg** och ingår inte i firmware-bygget. Nästa steg
(se `ROADMAP.md`) är att läsa `.adv` även på enheten (från SD/flash), så att
färdiga äventyr kan importeras direkt där.

---

## 12. Komplett exempel: Hebron-lägret (Josef, 1 Mos 37)

Nedan är hela `docs/adventures/example_joseph_hebron.adv` - den verkliga
första Josef-kartan uttryckt i formatet. Öppna filen för den aktuella versionen;
den är kort nog att läsa i sin helhet och är ett riktigt, fungerande exempel
(inte en leksak).

Se: [`docs/adventures/example_joseph_hebron.adv`](adventures/example_joseph_hebron.adv)
