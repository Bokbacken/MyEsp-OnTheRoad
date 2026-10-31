# Snabbguide: skapa äventyr (.adv)

Den här guiden följer arbetsordningen och samlar standardtecken, `@legend`,
grupperade Sokoban-pussel och minsvepare på ett ställe. Tabellerna är uppdelade
efter terräng, föremål och personer så att tecken med olika funktion inte blandas.
För dialoger, skatter, resurser, grafik och fullständiga regler, se
[den detaljerade författarguiden](AUTHORING_ADVENTURES.md). Fungerande banor
finns i [Sokoban-demot](adventures/sokoban/sokoban.adv) och
[S2-demot](adventures/s2_demo/README.md),
[0.21.0-demot](adventures/s3_demo/README.md) och
[0.21.1-demot](adventures/s4_demo/README.md).

Versionsnumren i demohänvisningarna anger när respektive mekanik introducerades,
inte att den bara stöds i den versionen. Firmware 0.23.4 innehåller
storystyrda terräng-/objektändringar; tidigare mekanik finns kvar.

**0.21.2:** `@resources` är spelbara pickups, inte bara metadata.
[Resursprovet](adventures/z_resource_test/README.md) kan kopieras direkt till SD.
[Flerkartor och portaler](ADVENTURE_MULTIMAP_FORMAT.md) stöds också i
produktionsladdaren. Upp till fyra interna kartor kan dela story/inventory,
ha egna grupper/fiender/regler och bevara ändringar vid återbesök. Se det
[spelbara portalprovet](adventures/multimap_portals/README.md). Hostens
referensparser `tools/adv_parse.*` är äldre och stöder inte syntaxen.

Exempel på en fristående resurskarta i dagens format:

```text
@resources
s : stone : Sten : Stone : 2 :
@map
SsssX
```

Varje `s` ger en sten; tredje stenen ligger kvar vid taket två.
Tomt bildfält ger en gul påse; valfritt `stone.png` hämtas ur mappens
`bitmaps/` (32×32, högst 16 KiB). Meny → **Föremål** visar namn och antal/tak.
Resurs-id:t fungerar direkt i `has:stone`, `give stone` och `take stone`.
`give` respekterar samma tak. Sparning omfattar både innehav och bortplockade
objekt. Mellan **separata filer** krävs `carry: items` i filen du lämnar
och samma id i nästa; `reset: items` vinner och nya filens tak gäller.
**Starta om** börjar om hela äventyret från första filen.

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
`S` (kontrolleras när motorn laddar kartan). `X` är vanlig utgång men krävs inte
av själva motorn; minsveparläget vinner utan att nå `X`. Inuti `@map` är `#`
alltid en vägg, inte en kommentar. En tom rad hoppas över; en rad som efter
trimning börjar med `@` avslutar kartan. Skriv kommentarer utanför `@map`.

## 2. Inbyggda karttecken

### Terräng

| Tecken | Betydelse |
|---|---|
| `.` `,` `_` | Gräs, sand, stengolv |
| `#` `%` `T` | Vägg, sprucken vägg (två slag), träd |
| `~` `^` | Vatten (båt eller bro krävs), lava |
| `o` `=` | Neutral tryckplatta och grupp-0-grind |
| `Q` `S` `X` | Checkpoint, start och utgång |

### Föremål och upphämtningar

| Tecken | Betydelse |
|---|---|
| `R` / `r` | Knuffbar sten på gräs / golv |
| `c` / `C` | Låda som slås sönder, på gräs / golv |
| `k` / `K` | Generisk nyckel på gräs / golv |
| `D` | Generisk låst dörr |
| `B` | Båt på sand |
| `*` `+` `$` | Ädelsten på gräs / golv / sand |
| `b` `p` `h` `u` `l` | Buske, dryck, hjärta, skyddsdräkt, extraliv |
| `f` `j` | Ficklampa nivå 1, hoppstylta nivå 1 |
| `m` `a` `z` | Mina, 3×3-bomb, 4×4-bomb |

### Personer

| Tecken | Betydelse |
|---|---|
| `1`–`9` | Person kopplad till motsvarande `num` i `@npc` |

Stor/liten bokstav spelar roll. Tecken som inte är inbyggda, `@` eller
mellanslag kan bindas som resurs-glyfer med `@resources`; använd inte
blanktecken eller `@` som glyf.

## 3. `@legend`: egna tecken för stödda mekaniker

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

Laddaren accepterar följande grupper av typer:

**Gruppmekanik och rörelseterräng**

| Värde till höger om `=` | Betydelse |
|---|---|
| `plate [<grupp>]` / `gate [<grupp>]` | Tryckplatta / grind i grupp 0–35; utan grupp används 0 |
| `lever <grupp>` | Spak i grupp 0–35 |
| `ice` | Is |
| `belt up\|right\|down\|left` | Transportband i angiven riktning |

**Färgmekanik, utrustning och farlig terräng**

| Värde till höger om `=` | Betydelse |
|---|---|
| `key <färg>` / `door <färg>` | Färgnyckel / färgdörr |
| `box <färg>` / `plate <färg>` | Flyttbar färglåda / färgmatchande platta |
| `plate <grupp> <färg>` | Färgplatta i grupp 0–35 |
| `skates` / `suction` | Permanent skridsko- / sugproppsutrustning |
| `poison` / `swamp` / `sharp_rocks` | Giftmark / träsk / vassa stenar |

Färgerna är `red`, `green`, `blue`, `yellow`, `purple`, `cyan`.

Grupp skrivs som `0`–`9` eller `A`–`Z` (`A`=10, `Z`=35); gruppbokstäver och
nyckelord är skiftlägesokänsliga. Karttecknet självt är skiftlägeskänsligt.
Platta och grind utan grupp får grupp 0. `@legend` ersätter även betydelsen av
ett inbyggt tecken, men inte `S`; mellanslag, `@` och radslut är också
reserverade. Detta är inte en godtycklig tile/object-id-tabell: bara typerna
ovan kan skapas med `@legend`. Dubbletter och ogiltiga värden ger fel med
radnummer.

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

Gruppdirektivet ska stå på en enda rad. Efter kolon anges en lista med en
eller flera enskilda rutor, inte en rektangel:

```text
@plategroup 1: 3,4 5,4 7,8
@gategroup 1: 9,4 9,5
```

Detta tilldelar tre plattor och två grindar till grupp 1. Koordinaterna måste
peka på redan placerade plattor respektive grindar på din karta. Exemplen
pekar på vald karta. Ange `map=2` på taggraden för en annan karta:

```text
@plategroup 1 map=2: 4,3
@gategroup 1 map=2: 5,3
```

För att blanda plattor och spakar krävs
`@andgate <grupp>` (även `@group <grupp>: and`): spaken måste vara ON **och** alla plattor rätt täckta.
Utan detta direktiv är blandningen ett fel. En tidsgrupp
(`@timedgate <grupp>: N`, `N`=1–65535 steg) kräver minst en
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

### Mörka områden: två hörn, inte en rutlista

Till skillnad från gruppdirektiven beskriver varje mörkerrektangel exakt två
hörn, inklusive båda:

```text
@dark
2,2 9,6
12,8 14,10
```

Här blir två rektanglar mörka. En enda mörk ruta anges som `4,3 4,3`.
En rektangel kan också skrivas direkt på taggraden: `@dark 2,2 9,6`.
Koordinaterna är nollbaserade. I en flerkartsfil kan sektionen anges med
`@dark map=2` eller enstaka rad med `map=2:`. Se
[mörker och ficklampa](AUTHORING_ADVENTURES.md#10a-dark--mörka-områden)
för fullständiga regler.

### Sugpropp och fiender (0.21.1)

Placera sugproppen med `s = suction` i `@legend`, eller lägg `X Y suction`
i `@treasures`, exempelvis under en sten eller färglåda. Ägandet sparas och
följer med mellan kartor, liksom skridskor, även om `reset` används för andra
resurser. Ett nytt äventyr börjar utan utrustning.

Stå vänd mot en sten/färglåda och välj **Dra** med **Byt**. Utför med nedre
delen av handlingsknappen: spelaren backar och objektet följer till den gamla
spelarrutan. Valet utför aldrig draget. Sönderslagbara lådor går inte att dra.
Drag är en ny handling, inte undo; terräng, timers och fiender fortsätter framåt.

Fiender placeras med separata koordinater, inte nya kartglyfer. Se
[fullständig syntax och turordning](AUTHORING_ADVENTURES.md#10g-sugpropp-och-turbaserade-fiender-v0211)
och [demots exakta testsekvens](adventures/s4_demo/README.md).
Högst åtta fiender och sexton vägpunkter per patrull stöds. Utan uttryckligt
område gäller hela kartan. En blockerad patrull eller jagare utan väg väntar.
Fiender kan inte knuffa stenar eller färglådor.

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

## 6. Flerkartor och portaler

En fil kan ha huvudkartan `@map` (id 1) och upp till tre extra kartor:
`@map 2`, `@map 3` osv. Varje karta är fortfarande max 80×80 och kräver en
`S`. Väljare som saknas betyder alltid huvudkartan, oavsett var direktivet
står. Sektioner som `@rules`, `@enemies`, `@resources` och `@dark` kan välja
karta på taggen; flera listor accepterar även `map=2:` per rad.

Destinationer namnges i `@destinations`, och rutor i `@portals` länkar till
namnen. Portalresor kostar ingen extra tur; en blockerad ankomst avvisas utan
att ändra hindret. Besökta kartor och tvärkart-checkpoint sparas tillsammans.
Energi/liv och resursinventory delas; kartlokala grupper, fiender och
mörkerlag behåller separata tillstånd. Fullständig syntax, kapaciteter och
save-buffer-krav finns i [flerkartsreferensen](ADVENTURE_MULTIMAP_FORMAT.md).

## 7. Storystyrd ändring av en karta

En dialog kan göra en begränsad ändring på en uttryckligt vald karta:

```text
choice: Bygg bron. | Build the bridge. => END ?? has:plank & !bridge_built !! world tile map=1 6 1 8; take plank; set bridge_built
choice: Skicka nyckeln. | Send the key. => END ?? has:beacon & !archive_lit !! world object map=2 6 2 3; take beacon; set archive_lit
```

Syntax: `world tile map=N x y tile-id` eller
`world object map=N x y object-id`. `map=N` krävs; kartan måste finnas i samma
`.adv`. Koordinaterna är nollbaserade, ID är befintliga numeriska enum-ID:n
(inte tecken från `@legend`). Bara allowlistade typer kan ändras. Effekten
avvisas utan sidoeffekter om målet är ogiltigt, upptaget av spelare/aktör/objekt
eller dold skatt, eller reserverat för portal/destination. Kartbilden sparas
för återbesök, checkpoint och save/restore; restart återställer startläget.
Se [säkerhetsregler och fullständiga ID-listor](AUTHORING_ADVENTURES.md#story-styrda-terräng--och-objektändringar-0234).

## 8. Kontrollera filen

Ny syntax ska kontrolleras med produktionsladdaren. `tools/adv_parse.*` är
fortfarande en S1-referensparser och validerar inte S2–S4. Kör hosttestet för
produktionsloadern eller motsvarande demo enligt kommandona i
[författarguidens valideringsavsnitt](AUTHORING_ADVENTURES.md#14-importexport-status).
