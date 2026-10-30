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

**v0.23 status:** `_ontheroad._tcp`-annonsering och en lokal **Spela tillsammans**
vy är implementerade. Tjänstinstansen får en MAC-suffix så två enheter med
samma standardvärdnamn inte annonserar en identisk instans. Klienten frågar
SRV/TXT/A separat när PTR-svaret inte innehåller alla poster. Vyn filtrerar
protokollversion/speltyp från mDNS och läser
`/api/game`; den visar endast aktiva Yatzy-spel med en ledig, PIN-skyddad plats
som värden i förväg markerat för webben. Den skickar ingen PIN och reserverar
ingen plats.

**Fjärrspel är inte implementerat.** Värden har en fungerande serverväg för
browserns `/ws/game`, men firmware saknar en ESP WebSocket-klient och den lokala
Yatzy-vyn kan inte rendera/tillämpa en annan värds snapshot eller skicka dess
kommandon. Dessutom finns värdens sessions-ID bara internt: `/api/game` och
WebSocket-`state`/`joined` exponerar inget stabilt ID som en frånkopplad klient
kan jämföra innan den återansluter. En säker änd-till-änd-join med den beslutade
återanslutningsregeln kan därför inte verifieras ännu. Nästa steg är att lägga
till ett additivt, opakt sessions-ID i API/WebSocket-state, en ESP
WebSocket-klient för samma join/PIN- och `hold`/`roll`/`choose`-format, en
touch-styrd fjärr-Yatzy-vy som använder värdens state, samt återanslutning som
kontrollerar samma värd/session/plats innan gamla kommandon skickas. Provning
mot en samtidig browserklient och fysisk verifiering på S3/P4 återstår.
