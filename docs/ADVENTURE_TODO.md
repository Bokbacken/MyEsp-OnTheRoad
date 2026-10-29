# Adventure: arbetsplan och återstart av session

## Börja här i en ny session

Repository: `Bokbacken/MyEsp-OnTheRoad`. Läs detta dokument, därefter
`ADVENTURE_MULTIMAP_FORMAT.md`, `AUTHORING_ADVENTURES.md` och
`ADVENTURE_AUTHORING_QUICKSTART.md`. Kontrollera aktuell `main`, öppna PR:er
och deras faktiska innehåll innan du fortsätter. En mergad äldre PR betyder
inte att senare commits på samma branch också finns i `main`.

Detta dokument är projektets beständiga checklista. Uppdatera det i samma
PR som arbetet, med vad som är implementerat, publicerat och hårdvarutestat.
Ocheckade punkter är inte färdiga. Spara kod och testbanor på en pushad
branch och öppna PR för granskning; enbart agentens arbetskopia är inte
åtkomlig för användaren via GitHub. Ändra inte skyddad `main` direkt.

Användaren arbetar via GitHub och provar firmware och testbanor på ESP32-S3.
Varje ny firmwareleverans ska ha en unik patchversion. Testbanor med
instruktioner ska ingå i `sdcard_bundle/otr/adventure/`.

### Verifierad utgångspunkt 2026-10-29

- PR #13: skridskor, tre giftterränger, färgade Sokoban-lådor/plattor och
  kartgrupper med AND-villkor för spak och platta.
- PR #14: sugpropp som drar objekt samt patrull- och jagarfiender.
- PR #15: manuella, SHA-låsta S3-releaser.
- PR #16: Karaoke-volymreglage och kalibrerad batteristatus.
- PR #17: synk av authoring-dokumentationen.
- PR #18, mergad: egna resurser, Resursprovet och firmware `0.21.2`.
  Användaren har provat Resursprovet på ESP. Flerkartsdokumentet beskriver
  fortfarande framtida funktionalitet, inte implementerat runtime-stöd.
- Volymknappens faktiska utseende och batteriindikatorns layout behöver
  ytterligare korrigering trots PR #16.

## I. Flerkartsformat och runtime

Pågående leverans: `feat/adventure-multimap-portals`, PR #19, firmware
`0.21.3`. Kod och testbanor är pushade där men ännu inte mergade till
`main`. Checklistans avbockningar avser implementation på den branchen;
fysisk ESP-verifiering och slutlig firmwarekontroll återstår.
Själva planen publicerades separat på `main` via PR #20.

Bekräftade spelregler för denna leverans:

- Blockerad portalankomst avvisar resan; spelaren stannar på källkartan
  och får ett meddelande. Ingen reservposition väljs.
- Energi-/livregler hämtas från huvudkartan och aktuella värden är
  gemensamma. Portalresa ger ingen påfyllning.
- Bara aktiv karta tickar fiender och timers. Portalresan kostar ingen
  extra tur.

- [x] Lås och dokumentera runtime-reglerna och kapacitetsgränserna.
  Högst fyra kartor och 64 KiB källfil. Savebuffer 180 122 byte i PSRAM.
- [x] Stöd `@map` för huvudkarta, `@map N` för extra kartor och explicit
  kartval enligt formatspecifikationen. Utelämnat kartval betyder alltid
  huvudkartan, aldrig senast deklarerad eller aktiv karta.
- [x] Kartlokala grupper, regler, resurspickups, NPC:er och fiender;
  filgemensamt inventory och storytillstånd.
- [x] Bevara kartornas ändringar vid återbesök utan att dubblera inventory.
- [x] Kartmedvetna checkpoints och beständig save/restore.
- [x] Avvisa saknade kartor, felaktiga referenser och kapacitetsöverskridanden
  med begripliga fel. Bevara gamla enkartsäventyrs beteende.

## II. Portaler, destinationer och spelbar leverans

Genomförs tillsammans med I så flerkartsstödet går att prova på ESP.

- [x] Filunika namngivna destinationer och portaler inom/mellan kartor.
- [x] Dialogstyrd förflyttning använder samma destinationsmodell.
- [x] Skydd mot omedelbar portalstuds, även efter save/restore och respawn.
- [x] Definierat beteende vid dynamiskt blockerad ankomst; inga tysta
  reservpositioner och inga raderade/flyttade objekt för att lyckas.
- [x] Reserverad HUD-remsa ovanför kartan; kartan ska inte döljas av HUD.
  Implementerad som 48 pixlar med titel och en delad rad för extra info.
  Automatisk layoutkontroll klar; fysisk ESP-verifiering återstår.
- [x] Testäventyr på SD med hemlig karta, returresa, lokala grupper,
  flyttade objekt, resurser, checkpoints och spara/ladda.
  `sdcard_bundle/otr/adventure/multimap_portals/`.
- [x] Testinstruktioner och uppdaterade authoring-exempel.
- [x] Relevanta automatiska tester, firmwareversionsbump och publicerad PR.
  Multimapkontroller, ASan/UBSan, tidigare resurser och SD-sessioner samt
  Adventure/Karaoke-layout är kontrollerade. Slutligt firmwarebygge ska
  avse PR:ens senaste SHA, inte en äldre grön commit.
- [ ] Slutlig ESP-IDF-byggkontroll för senaste SHA i PR #19.
- [ ] Användarens verifiering på ESP32-S3.

## Småfixar i samma leverans

- [x] Karaoke: mindre, tydligt rektangulär volymknapp i den verkliga
  renderingen, inte bara ändrade stilvärden. Behåll fungerande touchyta.
  LVGL:s ritrektangel är 30 × 14 pixlar, touchytan minst 52 pixlar bred.
- [x] Karaoke: batteriprocent inne i batterisymbolen, ingen procenttext
  bredvid. Behåll indikatorns nuvarande yttermått och procentstyrda färg.
  Hela indikatorn är fortsatt 62 × 32 pixlar. ESP-verifiering återstår.

## III. Story-styrda kartförändringar

- [ ] Begränsade effekter för terräng och objekt med explicit målkarta.
- [ ] Tydliga regler för upptagna rutor, ej besökta kartor och ogiltiga mål.
- [ ] Testbana där exempelvis en dialog bygger en beständig bro.

## IV. Legend och authoring-underlag inför editorn

- [ ] Slutför legendstöd där editorns palett behöver det.
- [ ] Synka feature-gap-listan, authoring och Quickguide med verkligt stöd.
- [ ] Håll koordinatlistor, mörkerrektanglar och andra funktioner åtskilda.
- [ ] Produktionsparser-/loadertesta dokumenterade exempel och referensbanor.

## V. Editorbackend på ESP32

- [ ] Återanvänd SD-filhantering, sökvägskontroll och befintligt skrivskydd.
- [ ] Strukturerad validering via produktionsparser, story och world-loader.
- [ ] Fel/varningar med rad, kolumn och kartruta där det är möjligt.
- [ ] Kontrollera referenser, bilder, kapacitetsgränser och destinationsmål.
- [ ] Säkra skrivningar och konfliktkontroll; originalet ska överleva fel.

## VI. HTML-editor

- [ ] Hostas av ESP32, i intern flash eller på SD om storleken kräver det.
- [ ] Läsa/skapa/redigera/spara `.adv` på SD.
- [ ] Kartflikar, terräng, objekt, grupper, mörker, skatter, resurser,
  utrustning, fiender, NPC:er, portaler och story: alla stödda funktioner.
- [ ] Textläge och förlustfri öppna/spara för kommentarer och okänd syntax.
- [ ] Visa valideringsfel vid rätt rad/kartruta.

## VII. Provspelning och slutverifiering

- [ ] Provspelning av separat testkopia i riktig motor utan att skriva över
  ordinarie sparläge.
- [ ] Hela flödet öppna → redigera → kontrollera → provspela → spara på S3.
- [ ] P4-bring-up och motsvarande verifiering när hårdvaran finns.

## Avgränsningar och leveranslogg

Ingen generell undo planeras: sugpropp och omstart är avsedd pusseldesign.
Validering ska skilja säkra fel från varningar och får inte lova att alla
syntaktiskt giltiga pussel är lösbara. Crafting, gravitationsblock, rörliga
plattformar och andra nya mekaniker ingår inte i första editorleveransen.
Mät och begränsa minnesbehovet; allokera inte obegränsat många kartor.

| Datum | Leverans | Verifiering/status |
|---|---|---|
| 2026-10-29 | Plan återställd efter tappad session | Utgångspunkt: PR #18, `0.21.2`. I+II och småfixar beställda. |
| 2026-10-29 | I+II och UI-fixar i PR #19 | Kod och testbanor pushade. Första ESP-IDF-bygget grönt; slutlig integration och fysisk ESP-verifiering kvar. |
