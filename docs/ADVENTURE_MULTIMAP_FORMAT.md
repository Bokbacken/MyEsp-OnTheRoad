# Framtida flerkartsformat — specifikation, INTE runtime-stöd

**Status 0.21.2:** steg 1 är denna formatspecifikation. Spelaren stöder
fortfarande exakt **en karta per `.adv`-fil**. Steg 3+4 ska implementera
flerkartor och portaler tillsammans i nästa leverans. Alla exempel med
kartval, extra `@map`, destinationer och portaler nedan är **ännu inte
stödda** och får inte kopieras till dagens körbara banor.
Det spelbara [resursprovet](adventures/z_resource_test/README.md) använder
enbart dagens syntax och separata filer.

## 1. Identiteter, kartval och bakåtkompatibilitet

- En fil har en huvudkarta, deklarerad med vanlig `@map`; dess **interna
  kart-id är 1**. `@map 2`, `@map 3` osv deklarerar extra kartor med explicita,
  unika positiva heltals-id:n. Deklarationsordning ändrar inte identiteten.
- `@meta map_id` behåller sin befintliga betydelse för den separata filens
  identitet. Det är inte ett implicit val av intern karta. Framtida save
  behöver både filidentitet och internt kart-id; dessa får inte blandas ihop.
- **Utelämnat kartval betyder ALLTID huvudkartan**, även efter `@map 2`
  eller en kartkvalificerad sektion/rad. Inget globalt ”senaste karta”-läge.
  Kartkoordinater är fortsatt nollbaserade, lokala `x,y`.
- Sektionsbundet kartval skrivs `map=2` på taggraden när hela sektionen är
  lokal. För blandade koordinatrader används prefixet `map=2:` på den
  enskilda raden. Grupper behåller gruppnumret före kartvalet.
- Gamla filer utan kartval ska bete sig exakt som förr: en huvudkarta,
  samma grupper, koordinater, NPC-siffror, story och övergång mellan filer.
  Dagens loader avvisar extra/numrerade `@map` i stället för att låtsas stöd.
- Referens till saknad karta, dubbel kartdeklaration, fel ruttyp, ogiltig
  koordinat eller överskriden kapacitet ska ge ett tydligt valideringsfel.
  Ingen tyst klippning, felkarta eller tillbakafall till kartan man står på.

## 2. Överenskommen grundsyntax — ännu inte stödd

```text
@map
...huvudkarta...
@map 2
...extrakarta...

@plategroup 1 map=2: 4,1 8,8
@gategroup 1 map=2: 9,4

@treasures
2 0 pogo 3
map=2: 3 1 flash 2

@dark
2,2 9,6
map=2: 3,4 4,6
```

Karttexterna ovan är platshållare, inte giltiga kartor. Skatten på första
raden och den första mörkerrektangeln hör till **huvudkartan** trots att
`@map 2` står tidigare. `@plategroup`/`@gategroup` anger listor med enskilda
rutor. `@dark` anger exakt två hörn per rektangel, inklusive båda.

Grupper är **kartlokala**: `(karta 1, grupp 1)` och `(karta 2, grupp 1)`
är oberoende. Samma gäller lokala timer-/spak-/AND-tillstånd. Förslaget:

```text
@timedgate 1 map=2: 8
@andgate 3 map=2
@group 3 map=2: and
```

Ovanstående tillägg är också **ännu inte stödda**. Befintliga regler för
vilka plattor/spakar en timer-/AND-grupp kräver ska gälla inom vald karta.

## 3. Fiender, NPC, resurser, legend, regler och story

### Fiender — föreslagen, ännu inte stödd syntax

```text
@enemies
patrol 2 3 loop route 2,3 8,3
map=2: chaser 4 5 area 1 1 9 8
```

Position, område och samtliga ruttpunkter på samma rad hör till samma
valda karta. En rutt får inte korsa kartgräns/portal. Första raden hör till
huvudkartan. Fiender är lokala aktörer, inte automatiskt portalresenärer.

### NPC — föreslagen, ännu inte stödd syntax

```text
@npc guard map=2
name: Vakten | Guard
num: 1
look: 0
```

`num: 1` binds till siffran `1` på karta 2. `@npc guard` utan kartval
binds alltid till huvudkartan. Siffrorna 1–9 är lokala per karta; det globala
story-id:t `guard` är unikt inom filen. En NPC-definition har en statisk
kartbindning i detta enkla förslag. Samma siffra på en annan karta måste
bindas till en annan NPC-definition; samma person på flera kartor eller
NPC-resor är inte ett implicit extrastöd.

### Resurser och legend — föreslagen, ännu inte stödd syntax

```text
@resources map=2
s : stone : Sten : Stone : 9 : stone.png
@legend map=2
a = plate 1
g = gate 1
```

Glyfbindningar gäller bara vald karta. Definition utan kartval gäller
**bara huvudkartan**, inte alla kartor eller nästa `@map`.
Resursens inventory-id är däremot filgemensamt, så `stone` på två kartor
är samma innehav. Återdeklaration av samma id måste ha samma namn, tak och
bild; motstridiga definitioner är fel, inte en implicit omdefiniering.
Vanliga `@items` behöver inte en glyfbindning. Bildsökvägen är fortsatt
äventyrsmappens `bitmaps/`, inte en ny mapp per intern karta.

### Regler — föreslagen, ännu inte stödd syntax

```text
@rules map=2
lava_damage: 3
fog: false
par: 20
```

Lokala spelregler hör till vald karta. Utelämnade lokala regler använder
motorns standardvärden, inte närmast föregående `@rules` eller huvudkartans
värden. Spelarens aktuella innehav och permanenta utrustning är gemensamma
inom filen; portalresa ska inte återställa dem.
`carry`/`reset` avser dagens resa till **nästa separata fil**. I förslaget
deklareras de på huvudkartan och är filpolicy; att placera dem i
`@rules map=2` är ett valideringsfel. Exakta regler för olika energi-/livtak
vid interna kartbyten måste beslutas, se öppna frågor.

### Story — föreslagen, ännu inte stödd kartkvalificering

Flaggor, räknare, item-id:n, quests, NPC-id:n och nod-id:n är gemensamma
inom filen; `has:stone`, `give stone` och `take stone` väljer inget kartlager.
Nodens placering i textfilen ändrar aldrig kartval.

Kartpåverkande `world`-effekter behöver däremot ett explicit val:

```text
effects: world npc_move map=2 1 4 6
effects: world open map=2 4 6
```

Förslaget behåller befintliga verb/argument efter valfri `map=N`.
Utan `map=N` avses **huvudkartan**, inte aktiv karta eller NPC:ns hemkarta.
Varje befintligt kartpåverkande verb måste få dokumenterad mål-/kartkontroll
i implementationen; dessa exempel introducerar inga nya verb i 0.21.2.
Globala effekter som energi/innehav accepterar inte meningslösa kartval.
Första effekten ovan flyttar NPC-siffra 1 *inom* karta 2; den flyttar inte
personen mellan kartor. Effekter på en ej besökt karta måste initiera
kartans grundtillstånd innan ändringen sparas — aldrig tappas bort.

## 4. Namngivna portaldestinationer — förslag, ännu inte stödda

```text
@destinations
home 3,1
map=2: cave 2,3

@portals
8,4 -> cave
map=2: 1,3 -> home
```

En destination är ett **filunikt namn** bundet till `(kart-id, x, y)`.
Prefixet väljer destinationens karta respektive portalens källkarta.
Ingen automatisk namnkoppling eller implicit returportal; en enkelriktad
portal är en rad, tvåvägsresa är två uttryckliga rader.

Källan är en koordinatmarkering på gångbar mark. Destinationer kräver
säker, gångbar ankomst; saknat namn, dubbelt källfält eller blockerad
destination ger fel. Portalen får varken radera objekt eller skjuta bort
en aktör för att lyckas.

**Ingen portalstuds:** ankomst ska inte utlösa en portal som ligger under
spelaren. Portalutlösning återaktiveras först efter att spelaren lämnat
ankomstrutan. Detta tillstånd måste överleva save/restore och checkpoint-
respawn; det får inte gå att skapa en oändlig returloop genom Fortsätt.

## 5. Tillstånd, återbesök, checkpoints och sparning

- Varje besökt karta behåller sina ändrade rutor, kvarvarande resurser,
  skatter, grindar/spakar/timers, fiender, dimma och NPC-placeringar.
  Återbesök är **inte omladdning till ursprungsläge**.
- Innehavet finns fortsatt bara en gång i story/inventory-systemet.
  Internt kartbyte får inte göra name-keyed carry/reset som mellan filer.
- Checkpointen innehåller minst **filidentitet + kart-id + x,y**.
  Dödsrespawn kan därför återvända till en annan intern karta.
  Checkpoint är som idag en respawnpunkt, **inte undo** av värld/innehav.
- Save måste omfatta aktiv karta, alla bevarade karttillstånd, gemensam
  story/spelare, checkpoint och ankomstspärr mot portalstuds.
  Fil-/kartreferenser valideras innan save tas i bruk. Äldre enkartsfiler
  och saves utan nya funktioner behöver en uttryckligt testad migrering.
- Ingen stor kartstack på ESP-taskstacken: fasta gränser och PSRAM, med
  tydligt fel vid överskridande. Slutliga kapaciteter och save-version är
  ännu inte beslutade och får inte ”lösas” med tysta bortfall.

## 6. Verkliga öppna beslut inför steg 3+4

Detta dokument är tillräckligt för att författa grundreferenser, men följande
är **inte färdigbeslutade runtime-regler**:

1. **Budget:** högsta antal interna kartor, sammanlagd rut-/aktörsbudget,
   residenta kontra serialiserade PSRAM-bilder och övre save-/filstorlek.
   Dagens kapaciteter per fil kan inte bara multipliceras obegränsat.
2. **Turordning:** kostar portalresa en ytterligare tur, tickar inaktiva
   kartors timers/fiender, och i vilken ordning sker fiendekontakt,
   transportband, checkpoint och portal vid samma handling? Förslag:
   endast aktiv karta tickar, ingen extra tur, men detta måste beslutas
   och testas tillsammans — inte antas i implementeringen.
3. **Ankomst som blockeras efter validering:** en rörlig fiende/sten kan
   stå på destinationen vid återbesök. Ska resan avvisas atomiskt, vänta,
   eller använda en explicit reservdestination? Ingen tyst teleportering
   till ”närmaste fria ruta”.
4. **Liv/energi och start:** hur hanteras olika `energy_cap`, startenergi
   och livregler mellan interna kartor? Behöver extrakartor egna `S`,
   eller enbart namngivna ankomster? Gamla kartors exakta ett `S` gäller
   oförändrat tills detta avgjorts.
5. **Omstartens gräns:** dagens SD-knapp startar **hela äventyret från
   första filen**. Ska framtida flerkartsvy dessutom erbjuda omstart av
   aktiv karta eller aktuell fil, och vad händer då med delat inventory
   och en checkpoint på annan karta? Befintlig omstart får inte försvinna.
6. **Kompatibilitet med redigerat innehåll:** bindning av sparade interna
   kartor till reviderade filer samt atomisk restore av alla kartor måste
   få ett uttalat kontrakt (identitet/signatur, migrering eller tydligt fel).

NPC-resor, flera placeringar av samma person och kartövergång över en kant
ingår inte automatiskt i detta portalformat. Utökningar behöver egna beslut.
