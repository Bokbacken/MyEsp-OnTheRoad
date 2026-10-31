# Flerkartor och portaler i `.adv`

**Status:** runtime-stöd från äventyrsmotorn 0.21.2. En fil kan innehålla
huvudkartan (`@map`, internt id 1) och upp till tre extra kartor. Portaler,
namngivna destinationer, kartlokala regler/aktörer, gemensamt story-innehav,
återbesök och sparning stöds. Hostens `tools/adv_parse.*` är fortfarande en
äldre referensparser; använd produktionsladdaren och hosttestet i
[författarguiden](AUTHORING_ADVENTURES.md#14-importexport-status).

Ett litet körbart exempel finns i
[multimap_portals](adventures/multimap_portals/README.md). Kopiera mappen
`multimap_portals` till `/otr/adventure/` på SD-kortet.

## 1. Kartidentiteter och väljarregler

En `.adv`-fil har exakt en huvudkarta med id 1, deklarerad med vanlig `@map`,
och högst tre ytterligare kartor med explicita, unika id:n 2–255:

```text
@map
...huvudkartans rader...
@map 7
...karta 7:s rader...
```

Varje karta måste vara högst 80×80 och innehålla exakt ett `S`. `@meta
map_id` är fortsatt filens vanliga separata kartidentitet och ändras inte av
internkart-id:n. Deklarationsordningen för extrakartor kan väljas fritt.

Utelämnat kartval betyder **alltid huvudkartan**, inte aktiv eller senast
deklarerad karta. Det gäller även om sektionen står efter `@map 7`.
Koordinater är lokala och nollbaserade. Väljare stöds som `map=7` på
sektionstaggen, eller som `map=7:` på enskilda rader i `@rules`,
`@treasures`, `@enemies`, `@resources` och `@dark`. `@legend`, `@npc`,
`@plategroup`, `@gategroup`, `@andgate` och `@timedgate` använder väljaren
på sin taggrad:

```text
@legend map=7
g = plate 2
@plategroup 2 map=7: 3,3
@timedgate 2 map=7: 12

@treasures
2 1 pogo 2
map=7: 3 2 flash 1
```

Koordinatrader väljer karta endast för den raden; valet spiller inte över
till nästa rad. Referens till odeklarerat kart-id, duplikat
kartadeklaration eller för många kartor ger fel.

## 2. Kartlokala sektioner och delade identiteter

Varje karta får egna terräng-/objektglyfer, legend, grupper, timer/spakläge,
skatter, resurspickups, fiender, dimma, mörker och NPC-placeringar.
NPC-siffrorna 1–9 är lokala per karta; `@npc id map=7` binder definitionen
till karta 7. NPC-id och story-noder är gemensamma för filen.

Resursens glyf och pickup är lokal, men dess inventory-id är gemensamt.
Samma resurs-id får deklareras på flera kartor endast om svenskt/engelskt
namn, tak och bildfil är identiska. Bildfilen ligger i äventyrsmappens
`bitmaps/`. Olika kartor får använda olika glyfer för samma id.

`@rules map=7` och radval av regler gäller bara vald karta. Saknade lokala
regler använder motorns standardvärden, inte huvudkartans lokala värden.
`start_energy`, `energy_cap`, `lives`, `carry` och `reset` är däremot
huvudkartspolicy: samma energi-/livregler och aktuella värden delas under
intern resa. En sådan regel i en extrakarta avvisas. `carry`/`reset` fortsätter
bara styra övergångar mellan separata `.adv`-filer.

Grupper är lokala: grupp 2 på karta 1 och grupp 2 på karta 7 är separata.
En fiende, timer eller annan kartaktör följer inte spelaren genom portaler.
Endast aktiv karta uppdateras/tickar. Kartpåverkande story-effekter kan ange
`map=N`; utan väljare avses huvudkartan för de äldre kartverben:

```text
effects: world open map=7 4 2
effects: world npc_move map=7 1 4 6
effects: world npc_remove map=7 1
effects: world travel cave
```

Sedan 0.23.4 kan story även ändra terräng eller placera ett objekt:

```text
effects: world tile map=7 4 2 8
effects: world object map=7 6 2 3
```

`tile`/`object` kräver uttryckligt `map=N`, koordinaterna är lokala och
nollbaserade, och ID är det numeriska värdet från motorns Tile-/Obj-enum.
Endast säkra allowlistor accepteras; plattor/grindar/spakar, färgplattor,
NPC:er, minor, ädelstenar, mängdbärande utrustning och storyresurser skapas
inte av dessa verb. Mål med spelare, aktör, synligt/dolt objekt, portal eller
destination avvisas innan kartan ändras. Vid fel visas ett dialogfel och andra
storyeffekter i samma val committas inte. Högst en `tile`/`object`-ändring får
finnas i en effektlista, utan någon annan kartstyrd `world`-åtgärd i samma
lista. Fullständiga ID-tabeller och detaljer finns i
[författarguiden](AUTHORING_ADVENTURES.md#story-styrda-terräng--och-objektändringar-0234).

`world travel NAME` använder samma destinationstabell som portaler. Stödda
kartverb är `open`, `npc_move`, `npc_remove`, `tile` och `object`. Effekter på ej
besökt karta utgår från dess grundläge och sparar ändringen; de tickar inte dess
fiender. Global story, flaggor, räknare, quests och inventory fortsätter vara
delade. Kartändringar följer befintlig checkpoint- och save/restore-semantik.
Restart återställer hela äventyret till grundläget; save-formatet har inte
ändrats.

## 3. Destinationer och portaler

Destinationsnamn är unika inom filen. Destinationer och portaler kan välja
karta på taggen eller på enskild rad; om väljare saknas används karta 1:

```text
@destinations
home 1,1
map=7: cave 2,1

@portals
2,1 -> cave
map=7: 7,1 -> home
```

En portal är enkelriktad om ingen returportal anges. Källa och destination
måste vara säker, gångbar mark vid laddning. Om ett rörligt objekt blockerar
ankomsten senare avvisas resan atomiskt: spelaren stannar på källrutan och
objektet flyttas/raderas inte. Portalresan ingår i den handling som gick in
på källan och kostar ingen extra tur. En portal på ankomstrutan utlöses inte
omedelbart; ankomstspärren släpps när spelaren lämnar rutan.

## 4. Återbesök, checkpoints, restart och saves

Besökta kartor behåller sina ändringar: flyttade objekt, pickups, grindar,
timer-/spakläge, fiendepositioner och andra sparade World-data. Endast aktiv
karta tickar. Delat inventory följer med utan carry/reset och energi/liv
fylls inte på eller klampas av destinationens regler.

`Q`-checkpoint sparar karta och lokala koordinater. Dödsrespawn kan därför
byta tillbaka till checkpointens karta. Checkpoint är inte undo: andra
världsändringar och inventory rullas inte tillbaka. Ankomstspärren mot
portalstuds överlever save/restore och checkpointrespawn. **Restart** behåller
det befintliga beteendet: hela äventyret börjar om från första kartan.

Multimap-save v2 innehåller aktiv karta, besökta kartor, gemensam StoryState,
checkpoint och portalspärr. Restore är transaktionell och kontrollerar
äventyrsmapp, kart-id:n, save-innehåll och filsignatur; redigerad källfil ger
tydligt fel i stället för att tillämpa gammalt karttillstånd på nya data.
Legacy v1-save återställs fortsatt för gamla enkarts-/separatfiläventyr; v1
kan inte användas för ett multimap-äventyr.

## 5. Kapacitet och validering

- Högst **4 interna kartor** inklusive huvudkartan; kart-id 1–255.
- Högst **32 destinationsnamn** och **32 portalrader** per fil.
- `.adv`-fil högst **64 KiB**; en källrad högst 1199 byte.
- Varje karta högst **80×80**, en `S`; högst 36 plattgrupper, 48 skatter,
  8 fiender, 16 mörkerrektanglar, 16 resursglyfdefinitioner och 160
  resursplaceringar per karta.
- Per fil gäller även 16 NPC-definitioner, 128 story-noder, 64 flaggor,
  16 räknare, 32 inventory-id:n och 16 quests. Befintliga gränser för
  patrol-rutter och andra sektioner gäller också.
- Varje karta valideras av `World::load()`. Okända sektioner/direktiv,
  saknade destinationer, ogiltiga/odeklarerade kartval, motstridiga
  resursdefinitioner, felaktiga koordinater och överskridna kapaciteter ska
  ge tydliga laddningsfel; de hoppas inte tyst över.

På ESP ligger `Adv::Loaded` och de serialiserade kartbilderna i PSRAM. Allokera
inte `Loaded` eller `Otr::World` på taskstacken. Hoststorlekar i denna
version: `sizeof(Loaded)=88,088`, `sizeof(World)=71,544`,
`sizeof(SdSession)=1,032`; en serialiserad World-bild ryms inom 44,977 byte.
`SdSession::SAVE_CAP` är **180,122 byte** för fyra maximalt stora bilder.
UI:ts save-buffer måste minst vara `Adv::SdSession::SAVE_CAP`; den gamla
65,536-bytegränsen räcker inte för multimap.

## 6. Spelbart prov

Kopiera [`docs/adventures/multimap_portals/`](adventures/multimap_portals/)
till SD-kortets `/otr/adventure/multimap_portals/`. Det provar portal till
gömd grotta, retur, blockerad ankomst, gemensam resurs, kartlokala grupper,
NPC/fiende/mörker, story travel, tvärkart-checkpoint samt save/restore.
Hosttestet `tests/test_adv_multimap.cpp` kör både källmappen och SD-bundle.
