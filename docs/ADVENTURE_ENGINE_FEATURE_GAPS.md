# Äventyrsmotorn – funktionsskanning & framtida uppgraderingar

Jämförelse mot klassiska 2D rutnätsmotorer (Chip's Challenge, Boulder Dash,
Sokoban-varianter, Zelda-likes, Baba Is You, enkla roguelikes). Listan tar **bara**
upp sådant motorn *inte* redan har. Statusen är kontrollerad mot parsern och
motorn i repot, som anger firmwareversion 0.21.3; versionsnummer i listan
beskriver när befintliga funktioner introducerades.

**Svårighetsskala (för just denna kodbas):**
`1` = trivialt, en glyph/regel. `2` = liten motor- eller loaderändring, host-testbar.
`3` = ny mekanik men ryms i befintliga strukturer. `4` = rör sparformat / UI / flera
system. `5` = stort, ny delsystemsarkitektur (t.ex. realtid/AI).

## Motorn har redan
Rutnätsgång, knuffbara stenar (sokoban), sten→bro/basalt, neutrala och
grupperade tryckplattor/grindar, spakar, tidsgrindar och spak+platta-AND,
is och transportband, generiska och färgade nycklar/dörrar, färglådor med
matchande plattor, par-bedömning, dimma/minsvepare, minor, bomber, båt/vatten,
lava, skyddsdräkt, giftmark/träsk/vassa stenar, energi/liv/checkpoints, dolda
skatter, mörker+ficklampa, hoppstylta, skridskor, NPC-dialog med
flaggor/räknare/uppdrag/villkor/effekter och utgång.

Från v0.21.1 stöds också sugpropp/Dra och turbaserade patruller samt
BFS-jagare med valfria områden; se
[syntax och gränser](AUTHORING_ADVENTURES.md#10g-sugpropp-och-turbaserade-fiender-v0211).
Sugpropp är inte generell undo. `@legend` kan mappa ett urval av befintliga
mekaniker, men är ännu inte en generell tabell för valfri terräng eller
objekt-id.

Från **0.21.2** fungerar egna `@resources` i spelaren: kartpickup till
storyinventory, tak även för `give`/carry, `has`/`take`, valfri PNG/påse,
föremålslista, checkpoint/save/restore/omstart och carry/reset mellan filer.
Webblivevyn speglar resurser och innehav. Se
[resursprovet](adventures/z_resource_test/README.md).
Från **0.21.3** stöds [flerkartor och namngivna portaldestinationer](ADVENTURE_MULTIMAP_FORMAT.md)
inom samma fil, även för dialogstyrd förflyttning. Karttillstånd bevaras vid
återbesök, checkpoints är kartmedvetna och bara aktiv karta tickar.
Destinationer är förflyttningsmål, inte namngivna checkpoints. Kartövergång
över en kant ingår inte. HUD har en reserverad remsa ovanför spelplanen.
Den [beständiga checklistan](ADVENTURE_TODO.md) anger återstående leverans-
och hårdvaruverifiering samt planen fram till en komplett editor.

---

## Rörelse & terräng

| # | Funktion | Vad det ger | Svårighet |
|---|----------|-------------|:--------:|
| 3 | **Enkelriktade rutor / hål** | Kan bara passeras åt ett håll, eller faller ned (nivåskifte/fälla) | 2 |
| 5 | **Rörlig plattform på vatten/lava** | Flytande yta som bär spelaren – mer dynamik än statisk bro | 4 |

## Pusselmekanik

| # | Funktion | Vad det ger | Svårighet |
|---|----------|-------------|:--------:|
| 8 | **Räknardörr** | Dörr som kräver N ädelstenar/föremål istället för nyckel | 2 |
| 9 | **Riktade/tyngdstyrda block** | Boulder Dash-stenar som faller när stödet tas bort | 4 |

## Fiender & fara

| # | Funktion | Vad det ger | Svårighet |
|---|----------|-------------|:--------:|
| 13 | **Projektiler / fällor** | Pilfälla, eldpelare som tänds i intervall | 3 |
| 14 | **Enkel strid / HP på fiende** | Slå-knappen finns redan; ge fiender träfftålighet | 3 |

## Föremål & ekonomi

| # | Funktion | Vad det ger | Svårighet |
|---|----------|-------------|:--------:|
| 16 | **Bärbart/aktiverbart föremål (inventory-slot)** | Plocka upp, använd aktivt senare (ej bara passivt) | 4 |
| 17 | **Huvudnyckel** *(redan påtänkt)* | Öppnar vilket lås som helst – liten påbyggnad på färgnycklar | 2 |
| 18 | **Köp/sälj hos NPC (valuta)** | Ädelstenar som valuta; dialogsystemet kan redan ge/ta | 3 |
| 19 | **Krukor/kärl med slumpdropp** | Slå sönder → slump på innehåll (variation) | 2 |
| 20 | **Kombinerbara föremål** | Craft 2→1 i dialog/meny | 4 |

## Värld & författande

| # | Funktion | Vad det ger | Svårighet |
|---|----------|-------------|:--------:|
| 21 | **Kartövergång över en kant** | Portalbaserade interna kartbyten finns; automatiska kantövergångar saknas | 3 |
| 22 | **Namngivna checkpoints** | Namngivna destinationer och storyförflyttning finns; checkpointnamn och val mellan flera återkomstpunkter saknas | 2 |
| 23 | **Villkorliga rutor** | Ruta ändras när en flagga sätts (t.ex. bro byggs av story) – `world VERB` finns redan | 3 |
| 24 | **Slumpmässig utplacering** | Markera ett område, motorn strösslar föremål/fiender – rejält omspelsvärde | 3 |
| 25 | **Generell kartlegend** | Utöka nuvarande legend för ett urval av mekaniker till valfri terräng/objekt och egna id:n | 2 |

## Presentation & känsla

| # | Funktion | Vad det ger | Svårighet |
|---|----------|-------------|:--------:|
| 26 | **Animerade fiender/objekt** | Vatten och lava har redan två bildrutor; fiender och fler objekt saknar egna rörelse-/idle-animationer | 2 |
| 27 | **Mjuk kamerapanorering** | Glid mellan rutor istället för hopp – ren UI-polish | 3 |
| 28 | **Väder/ljuslägen per karta** | Natt/dis utöver `@dark` – stämning | 3 |
| 29 | **Rikare HUD (minikarta/quest-logg)** | Översikt i stora banor | 3 |
| 30 | **Fler dedikerade ljudeffekter** | Vanliga rörelser och händelser har redan ljud; ge fler händelser, exempelvis fiendekontakt och drag, egna ljud | 1 |

## Progression & meta

| # | Funktion | Vad det ger | Svårighet |
|---|----------|-------------|:--------:|
| 32 | **Tidtagning/bestatid** | Spara bästa tid per bana | 2 |
| 33 | **Samlarmål / bonusstjärnor** | Hitta alla skatter = stjärna; delmål utöver utgången | 2 |
| 34 | **Ångra drag (undo), ej prioriterat** | Sugpropp/Dra är avsiktligt alternativ; omstart finns kvar. Ingen undo planeras i aktuell leverans | 4 |
| 35 | **Nivåväljare med framsteg** | Lås upp banor i följd, visa klarade | 3 |

---

## Möjliga nästa steg (värde för insatsen)
- **Story-styrda kartförändringar (#23)** – nästa avgränsade motorsteg efter
  fysisk verifiering av flerkartor och portaler.
- **Legend och authoring-underlag (#25)** – därefter editorbackend,
  HTML-editor och provspelning enligt [checklistan](ADVENTURE_TODO.md).

Motorlogik kan i regel host-testas utan hårdvara. UI- och presentationsändringar
behöver dessutom relevanta UI-/enhetskontroller.
