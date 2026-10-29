# Resursprovet — firmware 0.21.2

Kopiera **hela denna mapp**, inklusive `bitmaps/`, till
**`/otr/adventure/z_resource_test/`** på SD-kortet. En identisk kopia ligger
i `sdcard_bundle/otr/adventure/z_resource_test/`. Välj **Resursprovet** i
Äventyr och starta nytt spel med en **registrerad spelare**, inte Gäst,
för att testa sparning. Alla tre kartorna är separata `.adv`-filer:
ingen framtida flerkarts-/portalsyntax används.

## Fysisk ESP32-S3-checklista (ännu inte utförd)

Koordinater är nollbaserade. R/L/U/D betyder ett joysticksteg.

1. Karta 1: från `(1,1)`, gå **RRR**. Stenarna `(2,1)` och `(3,1)`
   försvinner; `(4,1)` ligger kvar eftersom taket är **2**.
   Meny → **Föremål** visar `Sten: 2 / 2`.
   Stenarna använder `bitmaps/stone.png` (standardmotorns stenbild som prov).
2. **RD**: du står på `(5,1)` och D börjar samtal med Provaren `(5,2)`.
   Välj **Ge en sten**. Kontrollera innehav 1. **L** går till `(4,1)`,
   plockar sista stenen och återställer innehav 2.
   **RD** pratar igen: **Få nio stenar** stannar på 2, inte 11.
3. Prata igen, ge en sten och stäng samtalet. **R** når checkpoint
   `(6,1)`. Vänta tills spelaren står still; välj även **Meny → Spara**.
   Lämna spelet och välj **Fortsätt**: position `(6,1)`, innehav 1,
   alla tre stenarna fortfarande borta. En omstart av enheten kan också
   användas för detta prov. Återkomst efter död använder checkpoint men
   rullar inte tillbaka inventariet eller redan plockade stenar.
4. **RRRR** når utgången `(10,1)` och nästa **fil**.
   På karta 2 är innehavet 1 (carry via id, trots ändrad itemordning).
   Bildfältet är tomt: stenen `(2,0)` visas som en gul påse.
   **RR** lämnar stenen kvar eftersom det nya taket är 1.
   **RRR** når utgången. Karta 2 har både carry och reset för items:
   **reset vinner**; karta 3 börjar med tomt innehav.
5. Karta 3: **R** plockar stenen, **R** når checkpoint.
   Spara/Fortsätt ska behålla innehav 1 och bortplockad sten.
   **Meny → Starta om** startar enligt befintlig regel hela äventyret
   från första filen: innehav 0 och samtliga stenar återställda.
   Det är också vägen ur ett fastnat pussel; ingen undo har lagts till.
6. Öppna webbsidan **Äventyr live** samtidigt. Kontrollera samma
   synliga/upphämtade stenar och innehav, även efter dialog och filbyte.
   Testa svenska/engelska och båda zoomnivåerna på enheten.
   Saknad/ogiltig angiven PNG ska ge varning och påse, inte osynligt objekt.

För att särskilt testa nedklampning vid carry: gå till karta 1:s utgång
med **två** stenar. Karta 2 ska börja med **en**.

## Hostkontroll

Från reporoten (binären skrivs i projektets ignorerade byggkatalog):

```sh
mkdir -p build_host_resources
g++ -std=c++17 -Wall -Wextra -Werror -Imain -Icomponents/otr_engine/include -Icomponents/otr_story/include tests/test_adv_resources.cpp main/adv_loader.cpp main/adv_sd_session.cpp main/adv_live.cpp components/otr_engine/otr_world.cpp components/otr_story/otr_story.cpp -o build_host_resources/resources
build_host_resources/resources
```

Testet kör produktionsloader, `Script::validate`, motor, dialog, SD-session,
save/restore/checkpoints, carry/reset, live-JSON och felvägar mot båda kopiorna.
Hosttest är inte fysisk verifiering eller ett ESP-IDF-firmwarebygge.
