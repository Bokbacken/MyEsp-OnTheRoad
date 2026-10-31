# Motoräventyret

En svensk testkampanj i sex numrerade kapitel. Leta efter skyltar och prata
med personerna; lösningstips utan spoilers finns i berättelserna. De flesta
kapitel är separata äventyr med egen start, så spelaren behöver själv välja
nästa kapitel i äventyrsmenyn.

## Installera och spela

Kopiera **kapitelmapparna var för sig** från
`sdcard_bundle/otr/adventure/motoraventyr/` till SD-kortets
`/otr/adventure/`. Exempel:

```text
sdcard_bundle/otr/adventure/motoraventyr/02_stenporten/
    -> /otr/adventure/02_stenporten/
```

Kopiera alltså inte bara den överordnade `motoraventyr`-mappen. Äventyrsmenyn
skannar kapitelmapparna direkt under `/otr/adventure/`, och motorn tillåter
inte att en mapp blandar en flerkartsfil med flera separata `.adv`-filer.
Starta **Äventyr**, välj ett installerat kapitel och börja spela.

Spela gärna `01_motvind`, `02_stenporten`, `03_fyrarnas_sang`,
`04_vindverkstaden` och `05_hemresan` i nummerordning. När ett av dessa
fristående kapitel tar slut väljer du nästa kapitel manuellt. Inventory,
flaggor och berättelseval förs **inte** över mellan dessa kapitel. Kapitel
`06_resurskedjan` är ett separat, automatiskt tre-filsprov av `carry` och
`reset`; håll dess tre filer tillsammans i samma mapp och starta bara den
första.

Minsveparkapitlet vinner när alla säkra rutor har avtäckts, inte när en
utgång nås. Det går inte automatiskt vidare till ett annat kapitel eftersom
det är en egen en-filsmapp; välj `02_stenporten` i menyn efter segern.

## Kapitel och innehåll

| Kapitel | Spelarlöfte och mekaniker |
|---|---|
| `01_motvind/01_motvind.adv` | Röj myren med flaggor, skyddsdräkt och bomber. Alla säkra rutor är målet. |
| `02_stenporten/01_stenporten.adv` | Fyra kartor: sten/tryckplatta, grön låda, färglådor med AND-spak, röd nyckel/dörr, hoppstylta över mur och sugproppsdrag. |
| `03_fyrarnas_sang/01_fyrarnas_sang.adv` | Hjälp Signe och Olof, bygg en bro och skicka en nyckel till arkivet innan det besöks. Utforska mörker, skatter, patrull/jagare, dialogresa och returportal. |
| `04_vindverkstaden/01_vindverkstaden.adv` | Fyra kartor med spak+platta, tidsgrind, skridskor, is/transportband, båt, lava/skyddsdräkt, giftterräng och sten→basalt. |
| `05_hemresan/01_hemresan.adv` | Kort epilog med NPC-ledtråd, dialogstyrd resa till hamnen, uppdragsstatus och avslut. |
| `06_resurskedjan/01_lamna.adv` … `03_nollstall.adv` | Tre filer som demonstrerar resurs-pickup, carry och att `reset: items` tömmer resurser. |

Kapitel 2 och 4 använder maxgränsen fyra interna kartor per `.adv`-fil.
Alla kartor är under 80×80 och kräver ingen extern bild eller annan resurs.

## Funktionsmatris

`tests/test_motoraventyr.cpp` laddar de levererade filerna genom produktions-
parsern och `World::load()` och kör spelspår i den riktiga motorn. Den testar
även avsiktliga misslyckanden och karttillstånd; matrisen pekar ut vad spelaren
gör och vilken del av spåret som verifierar funktionen.

| Mekanik | Fil/karta/plats | Spelarens uppgift | Verifiering |
|---|---|---|---|
| Minor, flaggor, bomber, dräkt, minsveparvinst | `01_motvind`, huvudkarta | Avtäck alla säkra rutor; använd förnödenheter klokt. | Testet verifierar `safeLeft()==0`, seger utan `X` och att nästa kapitel väljs manuellt. |
| Sten, platta/grind, storybro, lava→basalt | `02_stenporten` karta 1; `03_fyrarnas_sang` karta 1; `04_vindverkstaden` karta 4 | Knuffa sten på plattan; lämna plankan till Signe; knuffa stenen i lavan. | Testet kontrollerar öppnad grind, beständig bro, `EV_SIZZLE` och `Tile::BASALT`. |
| Färglådor, matchande plattor, grindgrupper, AND | `02_stenporten` kartor 2–3 | Matcha grön, röd och blå låda; slå sedan på spaken. | Motortrace kontrollerar täckta plattor, stängd grind före spak och öppen grind efteråt. |
| Färgnyckel/dörr, hoppstylta och sugpropp | `02_stenporten` kartor 3–4 | Ta röd nyckel, öppna dörren, hoppa över muren och dra stenen bakåt. | Testet följer portalankomst, säker landning, blockerad genväg och faktisk `pull()` som öppnar utgången. |
| Skridskor, is, transportband, spak/AND, tidsgrind | `04_vindverkstaden` kartor 1–2 | Aktivera spaken, plocka skridskor, lägg stenen på plattan och korsa grinden innan timern löper ut. | Testet kontrollerar is/band, utrustningspickup, timerstart och portalövergång. |
| Båt, vatten, lava, skyddsdräkt och giftterränger | `04_vindverkstaden`, karta 3 | Ta dräkt och båt; korsa vattnet och nå portalen genom terrängfarorna. | Testet kontrollerar båt, segling, skydd och checkpoint på vägen. |
| Energi/liv, checkpoints, skatt, mörker/ficklampa, fiender | `03_fyrarnas_sang`, karta 2 | Hitta arkivet, gå genom mörkret och undvik patrull/jagare. | Testet verifierar checkpoint, fiender, ficklampspickup, dold skatt och återresa. |
| Egna resurser, take/has/give, dialogflaggor/räknare/uppdrag | `03_fyrarnas_sang`, karta 1 | Lämna planka till Signe och fyrlykta till Olof; hämta en engångsbelöning. | Testet kontrollerar resursförbrukning, villkor, räknare, quests och att dialogvalet inte återutlöses. |
| Story-styrd bro på aktiv karta och föremål på obesökt karta | `03_fyrarnas_sang`, Signe/Olof → karta 2 | Bygg bron och skicka nyckeln innan arkivet öppnas. | Testet kontrollerar båda mutationerna, negativa upptagna/ogiltiga mål, save/restore, återbesök, checkpoint och restart. |
| Portaler, dialogresa, returer och karttillstånd | `02`, `03`, `04` | Res mellan namngivna kartor och återvänd utan portalstuds. | Motortrace verifierar ankomst, retur, flyttade objekt/resurser och sparade kartbilder. |
| Carry/reset mellan separata filer | `06_resurskedjan`, filer 1–3 | Ta med sten genom de två första filerna; kontrollera att den nollställs i tredje. | Testet kör `SdSession::travel()` och kontrollerar inventory efter varje fil. |

## Avgränsningar

- Kapitelmapparna är avsiktligt självständiga; de delar inte state med varandra.
  Endast filerna i `06_resurskedjan` är en automatisk flerfilssekvens.
- Minsveparläget är inte tillåtet i en intern flerkartsfil. Kapitlet är separat,
  och dess seger innebär inte automatisk kampanjprogression.
- Ingen ny runtime-, editor- eller ljudfunktion ingår utöver story-verben för
  terräng-/objektändring. Hårdvarutestning görs separat från hosttesterna.

## Hårdvaruchecklista (ännu ej fysiskt verifierad)

- [ ] ESP32-S3: kopiera kapitelmapparna, välj dem i menyn och spela igenom
  portal/drag- och dialogbanorna med touchkontrollerna.
- [ ] ESP32-S3: kontrollera minsveparseger, checkpoint, spara/ladda och
  filövergången i `06_resurskedjan`.
- [ ] ESP32-P4: upprepa installation, urval, dialoger, portaler, spara/ladda
  och resurskedjan; kontrollera layout och touch i P4-panelens rotation.
