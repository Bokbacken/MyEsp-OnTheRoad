# Snabbguide: skapa äventyr (.adv)

Den här guiden följer arbetsordningen och samlar standardtecken, `@legend`,
grupperade Sokoban-pussel och minsvepare på ett ställe. För dialoger, skatter,
resurser, grafik och fullständiga regler, se
[den detaljerade författarguiden](AUTHORING_ADVENTURES.md). Fungerande banor
finns i [Sokoban-demot](adventures/sokoban/sokoban.adv) och
[S2-demot](adventures/s2_demo/README.md).

## 1. Börja med en karta

Lägg en UTF-8-fil med ändelsen `.adv` i en egen mapp under
`/otr/adventure/`. En mapp är ett äventyr; flera kartfiler i mappen spelas i
filnamnsordning. Välj stabila `world`- och `map_id`-värden som inte krockar med
andra äventyr.

```text
@meta
id: forsta_banan
world: 90
map_id: 90

@about
title.sv: Första banan
title.en: First level
desc.sv: Hitta utgången.
desc.en: Find the exit.

@rules
start_energy: 8
lives: infinite

@map
#########
#S.....X#
#########
```

Kartan är ett tecken per ruta, högst 80×80, med lika långa rader och exakt ett
`S`. `X` är vanlig utgång. Inuti `@map` är `#` alltid en vägg, inte en kommentar.
En tom rad hoppas över; en rad som efter trimning börjar med `@` avslutar
kartan. Skriv kommentarer utanför `@map`.

## 2. Inbyggda karttecken

| Tecken | Betydelse |
|---|---|
| `.` `,` `_` | Gräs, sand, stengolv |
| `#` `%` `T` | Vägg, sprucken vägg (två slag), träd |
| `~` `^` | Vatten (båt eller bro krävs), lava |
| `o` `=` | Tryckplatta och grupp-0-grind |
| `Q` `S` `X` | Checkpoint, start, utgång |
| `R` / `r` | Knuffbar sten på gräs / golv |
| `c` / `C` | Låda som slås sönder, på gräs / golv |
| `k` / `K` `D` | Generisk nyckel på gräs / golv, generisk låst dörr |
| `B` | Båt på sand |
| `*` `+` `$` | Ädelsten på gräs / golv / sand |
| `b` `p` `h` `u` `l` | Buske, dryck, hjärta, skyddsdräkt, extraliv |
| `f` `j` | Ficklampa nivå 1, hoppstylta nivå 1 |
| `m` `a` `z` | Mina, 3×3-bomb, 4×4-bomb |
| `1`–`9` | Person kopplad till motsvarande `num` i `@npc` |

Stor/liten bokstav spelar roll. Alla andra lediga tecken kan användas som
resurs-glyfer med `@resources`.

## 3. `@legend`: S1- och S2-varianter

Definiera ett eget karttecken per rad:

```text
@legend
a = plate 1
A = gate 1
! = lever 2
i = ice
> = belt right
q = key red
H = door red
```

Laddaren accepterar dessa typer:

| Värde till höger om `=` | Betydelse / argument |
|---|---|
| `plate <grupp>` | Tryckplatta; grupp 0–35 |
| `gate <grupp>` | Grind; grupp 0–35 |
| `lever <grupp>` | Spak; grupp 0–35 |
| `ice` | Is; inget argument |
| `belt up\|right\|down\|left` | Band i en av de fyra riktningarna |
| `key <färg>` / `door <färg>` | Färgnyckel / färgdörr: `red`, `green`, `blue`, `yellow`, `purple`, `cyan` |

Grupp skrivs som `0`–`9` eller `A`–`Z` (`A`=10, `Z`=35); gruppbokstäver och
nyckelord är skiftlägesokänsliga. Karttecknet självt är skiftlägeskänsligt.
Platta och grind utan grupp får grupp 0. `@legend` ersätter även betydelsen av
ett inbyggt tecken, men inte `S`; mellanslag, `@` och radslut är också
reserverade. Dubbletter och ogiltiga värden ger fel med radnummer.

## 4. Grupperade Sokoban-pussel

Grupp 0 behåller det äldre gemensamma platt-/grindbeteendet. Grupper 1–35
fungerar oberoende: gruppens grind öppnas när alla gruppens plattor bär en sten.
En lösning är att ge platta och grind egna glyfer:

```text
@legend
a = plate 1
A = gate 1

@map
#########
#S.RaA.X#
#########
```

Knuffa stenen `R` åt höger på `a`; då öppnas `A`. Alternativt kan gruppen sättas
på vanliga `o`-/`=`-rutor med nollbaserade kartkoordinater (`x,y`):

```text
@map
#########
#S.Ro=.X#
#########

@plategroup 1: 4,1
@gategroup 1: 5,1
```

En koordinat måste peka på rätt ruttyp. Grupper kan inte blanda plattor och
spakar. En tidsgrupp (`@timedgate <grupp>: N`, `N`=1–65535 steg) kräver minst en
grind och exakt en platta eller spak. Timern tickar på lyckade förflyttningar,
inte på blockering, upplåsning eller handling; spelet räknar inga realtidssekunder
under paus eller dialog. Välj **Spak / Lever** med **Byt / Swap** och aktivera
med den nedre delen av handlingsknappen.

Färgnycklar har separata räknare 0–255; en färgdörr förbrukar bara en nyckel av
matchande färg och förblir sedan öppen. `carry: keys` tar med nycklar mellan
kartor; `reset: keys` tömmer dem och vinner om båda anges.
`par: N` i `@rules` (N=1–65535) är ett stegsmål, inte ett vinstkrav; HUD och
resultat visar stegen i förhållande till par. Se
[fyrarumsdemot](adventures/sokoban/sokoban.adv) för flera oberoende rum i samma
karta och [S2-demot](adventures/s2_demo/README.md) för alla S2-objekt tillsammans.

## 5. Minsvepare

Använd `mine_level` i `@rules` för riktigt minsveparläge. `1`–`12` aktiverar
läget; aliaset `minesweeper: true` motsvarar nivå 1. Dimma slås på automatiskt.
Sätt `m` på minrutor och `.` på säkra rutor. `S` är start; `u`, `a` och `z` kan
placera skyddsdräkt och bomber.

```text
@meta
id: minovning
world: 90
map_id: 91

@rules
lives: 3
mine_damage: 4
mine_level: 1

@map
........
S..uaz..
..m.....
...m....
```

Tryck på en ruta för att avtäcka den; håll eller använd flaggknappen för att
flagga en misstänkt mina. Målet är att avtäcka alla rutor som inte innehåller
minor; `X` behövs inte för seger i minsveparläge. Gör en separat `.adv`-fil per
kampanjnivå och ge dem numrerade filnamn. Ett komplett 12-banors exempel finns i
[`docs/adventures/generated/minkampanj/`](adventures/generated/minkampanj/).

## 6. Kontrollera filen

S2-syntax ska kontrolleras med produktionsladdaren. `tools/adv_parse.*` är
fortfarande en S1-referensparser och validerar inte S2. Kör hosttestet för
produktionsloadern eller S2-demot enligt kommandona i
[författarguidens valideringsavsnitt](AUTHORING_ADVENTURES.md#13-importexport-status).
