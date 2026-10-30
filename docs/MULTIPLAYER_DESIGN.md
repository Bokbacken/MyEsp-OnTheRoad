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
- Snapshoten har ett opakt `session_id`; `joined` och `state` skickar samma ID.
  En klient som återansluter skickar ID:t i `join` och får avslag om spelet har
  bytts ut. Gamla klienter utan fältet fungerar som tidigare.
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

**v0.23 status:** `_ontheroad._tcp`-annonsering, lokal **Spela tillsammans**-
vy och ESP Yatzy-klient är implementerade. Tjänstinstansen får en MAC-suffix
så två enheter med samma standardvärdnamn inte annonserar en identisk instans.
Klienten frågar SRV/TXT/A separat när PTR-svaret inte innehåller alla poster.
Vyn filtrerar protokollversion/speltyp från mDNS och läser `/api/game`; den
visar endast aktiva Yatzy-spel med en ledig, PIN-skyddad plats som värden i
förväg markerat för webben. Spelaren väljer en plats och anger dess maskerade
PIN. ESP-klienten skickar join med säkert sessions-ID, tar emot värdens snapshot
och visar dess tärningar, tur, kategorier och poäng. Den skickar endast
värdvaliderade `hold`, `roll` och `choose`-kommandon; den har ingen egen
tärnings- eller poänglogik.

WebSocket-återanslutning stannar vid samma värd-URI, sessions-ID och plats.
Ändrad session eller upptagen plats avslutar den gamla bindningen och kräver
en ny join. Värden inför ingen separat grace-period; en annan spelare kan ta
platsen medan klienten är frånkopplad, vilket visas som ett tydligt fel. PIN
sparas bara temporärt i minnet för återanslutning och skrivs inte i loggar.
`join` utan sessions-ID behåller browserkompatibiliteten.
Targetbyggen är verifierade i CI-run 41051798946 för C6-host, S3 och P4;
S3-jobbets automatiska hostkontroller passerar. Fysisk änd-till-änd-testning
återstår: checklistan för samtidig browser/ESP, join/PIN, kast/håll/välj och
återanslutning är ännu inte avbockad på vare sig S3 eller P4.
