# Spela tillsammans – design

## Mål och auktoritet

En OnTheRoad-enhet är spelvärd och ensam auktoritet över spelregler, tärningskast,
poäng och sittplatser. Andra enheter och telefoner är spelare/visare, inte
replicerade masters. Börja med Yatzy eftersom `GameLive`, `/api/game`, `/ws/game`
och browserflödet `/play` redan finns.

Ingen automatisk master-election behövs i första versionen: värden väljs manuellt
i enhetens UI. Det undviker delade eller dubbla spel när flera enheter är på
samma nät.

## Upptäckt och protokoll

Värden annonserar `_ontheroad._tcp` på HTTP-port 80. TXT-data innehåller
protokollversion (`proto=1`), stödda spel (`games=yatzy`) och enhetsnamn. TXT
innehåller aldrig PIN eller annan hemlighet. Annonseringen betyder att enheten
kan vara spelvärd; klienten läser `/api/game` för att avgöra om en aktiv Yatzy-
session faktiskt finns.

Återanvänd i första hand befintliga HTTP/WebSocket-format:

- `GET /api/game` ger den aktuella snapshoten.
- `/ws/game` accepterar samma `join`, `hold`, `roll`, `choose`, `bank` och `end`
  som browserklienterna, och skickar `state`/`error`.
- Värdens befintliga session- och platsbindning är auktoritativ. Ingen ny
  tärnings-/poängrepresentation skapas för ESP-klienter.

Protokollversion och speltyp ska finnas i capability-/state-data så att klienter
kan avvisa inkompatibla värdar innan de reserverar en plats.

## UI-flöde på en andra enhet

1. Spelaren öppnar **Spela tillsammans** och söker mDNS efter
   `_ontheroad._tcp`.
2. Visa namn, adress, speltyp och anslutningsstatus. Fråga varje värd om
   `/api/game`; visa endast aktiva, joinbara spel.
3. Spelaren väljer värd, skriver sitt spelarnamn/PIN och väljer eller får en
   ledig plats. PIN skickas endast över WebSocket till värden och aldrig via
   mDNS.
4. Enheten visar den vanliga Yatzy-snapshoten och skickar befintliga
   WebSocket-kommandon. Värden validerar alltid plats, tur och spelregel.

## Säkerhet

mDNS är upptäckt, inte autentisering. Spelet stannar på det lokala nätet och
värden validerar befintlig personlig spelar-PIN samt administratörs-PIN för
administrativa ändringar. PIN får inte skrivas i loggar, TXT-poster eller
felmeddelanden. En framtida version bör överväga en särskild spel-PIN och
rate-limit för felaktiga försök; admin-PIN ska inte skickas för vanliga
spelkommandon.

## Återanslutning och livscykel

Vid WebSocket-avbrott behåll spelplatsen på värden under en kort grace-period.
Klienten söker upp samma värd/session igen och återansluter med samma plats/PIN.
Om värdens session-ID ändrats eller platsen tilldelats någon annan ska klienten
visa att den måste gå med på nytt i stället för att skicka gamla kommandon.
Värdbyte är manuellt; klienten återupptar aldrig ett gammalt spel på en annan
värd automatiskt.

## Stegordning

1. mDNS-kapabilitetsannonsering och dokumentation.
2. ESP-klientens upptäckt, aktiv-session-lista och Yatzy join-UI.
3. Reconnect/grace-period och test av samtidig browser-/ESP-klient.
4. Därefter övervägs andra befintliga spel.

**v0.23-avgränsning:** annonsering och design är levererade. ESP-klientens mDNS-
upptäckt, enhets-UI och WebSocket-klient lämnas till nästa steg; den delen är
större än en säker ändring av den befintliga browserprotokollvägen.
