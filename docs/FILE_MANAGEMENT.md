# Filhantering och SD-prestanda

## Förbättringar

- FAT Long File Name-gränsen höjdes från 64 till 255 UTF-16-kodenheter. Webbfilsystemet validerar UTF-8, komponent- och fullständiga sökvägsgränser.
- Uppladdningar skriver först till en unik, exklusivt skapad temporär syskonfil. En befintlig destination sparas tills den nya filen installerats; avbrutna eller misslyckade skrivningar ersätter inte originalet.
- Konflikter kan stoppas, hoppas över eller skrivas över. Hoppa över är standard och en hoppad fil avbryter inte resten av batchen.
- SD-uppladdningar körs en i taget både i webbläsarens batch och på servern. En uppladdning dubbelbuffrar mottagningen i 32 KiB-block och försöker använda en 32 KiB DMA-kapabel skrivcache, med PSRAM som reserv. Skrivningarna till själva SD-kortet är fortsatt serialiserade.
- Kataloger kan väljas eller släppas från webbläsaren; deras struktur återskapas upp till tre mappnivåer.
- SD-kort kan formateras som FAT från filhanteraren. Enheten använder inte exFAT; FAT-formatering kan därför göra större kort användbara. Två separata bekräftelser krävs, och en konfigurerad admin-PIN måste anges. Formatering raderar allt på kortet. Den uttryckliga åtgärden kan också formatera ett kort vars filsystem inte gick att montera; vanlig start och automatisk återmontering formaterar aldrig kortet. Webbläsaren visar status medan formateringen pågår och ett tydligt resultat efteråt.

## Inställningsminnet NVS

NVS är en separat flashpartition med typade nycklar/värden, inte JSON-filer
på intern flash (FFat). Wi-Fi, spel- och karaokeinställningar med mera lagras
där. Formatering/reparation av FFat raderar inte NVS.

Öppna **NVS – inställningsminne och backup** i filhanteraren. Ange Admin-PIN
om den är konfigurerad och välj **Visa minnesinformation** för partitionens
storlek, antal namnrymder och använda/lediga poster. **Ladda ner NVS-backup**
hämtar hela `nvs`-partitionen som `otr-nvs.bin` direkt till datorn; ingen
extra kopia med lösenord lämnas på FFat eller SD.

Backupen kan innehålla Wi-Fi-lösenord och Admin-PIN. Förvara den säkert och
ändra inga inställningar medan backupen läses. Den är en rå partitionsbild,
inte ett portabelt JSON-inställningsformat. Ladda inte upp den som en vanlig
fil till FFat. Webbaserad återställning ingår inte; eventuell återställning
kräver rätt partitionslayout och kompatibel NVS-konfiguration. Krypterad NVS
avvisas med ett tydligt fel eftersom också krypteringsnycklarna behövs.

API: admin-skyddad `GET /api/nvs` (storlek och poststatistik) och
`GET /api/nvs/backup` (binär nedladdning, `Cache-Control: no-store`).

Reparera intern flash ligger, liksom Formatera SD-kort, uppe till höger.
Den befintliga reparationen säkerhetskopierar läsbara `players.bin` och
`events.log` till SD när ett kort finns; den är inte en fullständig filbackup.

## Ägarrapporterade mätvärden

Alla 144 MB-test avsåg samma fil (151 001 312 byte), men körningarna gjordes under olika förhållanden. Kort, radioförhållanden och konfiguration är inte dokumenterade som identiska; jämförelserna visar därför observerade resultat och bevisar inte ensamma att en viss ändring orsakade skillnaden.

| Körning | Total tid | Mottagning | Buffertväntan | SD-skrivning | Ungefärlig genomströmning |
|---|---:|---:|---:|---:|---:|
| Tidigare P4 | 457,7 s | 182,3 s | – | 273,8 s uppmätt SD-tid | 330 kB/s |
| Tidigare S3 | 342,5 s | cirka 165 s | – | cirka 175,5 s uppmätt SD-tid | 441 kB/s |
| P4 efter byte till fungerande kort | 326,9 s | 77,3 s | – | 247,4 s uppmätt SD-tid | 462 kB/s |
| P4 med dubbelbuffring och 16 KiB DMA-cache | 245,755 s | 220,745 s | 24,501 s | 48,875 s `write`; 0,225 s SD-låsväntan | 614 kB/s |
| P4-build med stöd för fyra sessioner och 32 KiB cache | 262,117 s | 241,187 s | 20,430 s | 35,729 s `write`; 0,222 s SD-låsväntan | 576 kB/s |

Mottagning, buffertväntan och skrivning körs delvis parallellt och ska inte summeras som väggklocktid. I de två senare körningarna var flush/stängning cirka 5/11 ms respektive 8/16 ms. En stor fil i bygget med större cache gav inte en tydlig förbättring; ägaren rapporterade däremot att flera filer samtidigt upplevdes mycket snabbare. En fullständig batchtid och total bytevolym saknas, så batchvinsten har ännu ingen jämförbar genomströmningssiffra.

Äldre manuella tester tog ungefär nio minuter på P4 och 5,5 minuter på S3 för en stor fil före den nya uppladdningspipen. SD-kortet som gav skrivfel bedömdes senare vara trasigt; de resultaten är därför inte ett rent kort-mot-kort-prestandatest.

## Tolkning och nästa verifiering

I de senaste stora filtesten stod mottagningen för merparten av den totala tiden. Buffertar kan hjälpa till att hålla flera överföringar i gång, men förbättrar inte nödvändigtvis en enskild TCP-ström eller radioförbindelsen. Mätvärdena stödjer att batchuppladdning förbättrats subjektivt, inte någon specifik garanterad hastighet.

Hårdvarutest återstår för v0.23.3: verifiera PIN-skydd och bekräftelser före formatering, formatering av ett kort med omonterbart/exFAT-filsystem, FAT-montering efteråt, synlig status vid lång formatering, kataloger med en till tre nivåer, gränsfallet fyra nivåer, konflikthantering och jämförbar total tid för samma filbatch. Inga hårdvarutester markeras som slutförda här.

## Uppföljning: SD-skrivfel vid uppladdning

Ägaren rapporterade 2026-10-31 att en lyckad uppladdning på 64 013 byte följdes av SDMMC-fel `0x107` vid skrivning, upprepade skrivfel och en watchdog-varning där `fs_upload_sd` kördes på CPU 0. SD-kortet blev därefter otillgängligt tills enheten återställdes. Loggen fastställer inte om grundorsaken var kortet, kortgränssnittet eller samtidig belastning.

Som försiktighetsåtgärd körs nu SD-uppladdningar en i taget. Vid ett SD-I/O-fel under skrivning, flush, stängning eller installation försöker servern använda den befintliga återmonterings-/återställningsvägen; den gör ingen usage-refresh efter misslyckad uppladdning. Detta är en programvarumitigering, inte bevis på att kort eller hårdvara är felfria. Reproduktion, återhämtning och watchdog-beteende behöver verifieras på den berörda enheten.

### Diagnostikbygge v0.23.8

En senare seriell logg bekräftar `IDLE0`-watchdog medan CPU 0 kör
`fs_upload_sd`, följd av SD-timeouter. Adresserna i v0.23.7 pekar på
FreeRTOS kömottagning; ett annat register pekar på SD-statuskommandot.
Utan full stack är det inte bevis för en specifik loop eller grundorsak.

P4-bygget aktiverar frame pointers och `CONFIG_OTR_SD_IO_DIAGNOSTICS`.
En separat task på CPU 1 samplar SD-kortets kommandon en gång per sekund
utan att ta SD-låset eller använda kortet. `sdtrace` visar aktuellt
`cmd`/`arg`, föregående datakommando (`data_cmd`, `block`, `bytes`),
antal efterföljande CMD13-statusfrågor (`polls`), pågående kommando
(`active`, `age_ms`), senaste avslutade kommandots tid/felkod/R1-svar
(`last_ms`, `err`, `r1`) och den anropande kärnan (`core`).
`block` är kommandoargumentet (sektoradress för SDHC, byteadress för SDSC);
föregående datakommando kan även vara ett initieringskommando.
Samplingen ändrar inte returvärden, hastighet, bussbredd, watchdog eller
uppladdningens prioritet. C6-radions slot instrumenteras inte.

1. Flasha P4-bygget och spara hela seriella loggen från start genom ett
   kopieringsförsök med samma fil som tidigare. Spara även byggets ELF.
2. Öppna Diagnostik och välj **Ladda ner bara RAM (utan SD)** direkt efter
   felet, före omstart. Detta ZIP läser inga SD-filer. Watchdog-utskriften
   kan fortfarande vara enbart seriell, så RAM-loggen ersätter inte serial.
3. Gör ett jämförande försök med **Pausa SD-loggning**, admin-PIN om den
   är konfigurerad. Vänta tills eventuell pågående loggskrivning avslutats
   innan försöket; pausen avbryter inte ett redan blockerat SD-anrop.
   RAM och seriell logg fortsätter, och senaste RAM-backlog skrivs när
   SD-loggningen återupptas. Pausen sparas inte över omstart.
4. Jämför stack och `sdtrace` innan klocka, bussbredd eller schemaläggning
   ändras. Ett lyckat enskilt försök bevisar inte stabilitet.

### Återhämtning vid SD-skrivfel (v0.23.9)

Två P4-loggar med v0.23.8 visar samma förlopp vid 20 MHz:

1. Under en skrivning med flera block (CMD25) kommer ett svars-timeout-avbrott
   (`SD_TRANS: handle_idle_state_events unhandled: 00000100`).
2. Kortet blir kvar i mottagningsläge (`r1=00000c00`).
3. ESP-IDF frågar kortets status (CMD13) i ungefär 5 s utan att släppa CPU 0.
4. Watchdogen för `IDLE0` slår till. Felet kom även med SD-loggningen pausad.

Hela kedjan är aktiv i alla byggen, inte bara i diagnostikbygget:

- Om CMD13 visar mottagningsläge skickar firmware `STOP_TRANSMISSION` (CMD12)
  så att kortet återgår till normalläge. Nytt försök görs tidigast efter 500 ms.
  Loggraden `card left in receive state` visar varje försök.
- Om kortet är upptaget längre än 2 ms släpps CPU:n en tick mellan
  statusfrågorna.
- En misslyckad CMD25 förblir misslyckad. ESP-IDF v6.1 returnerar annars
  resultatet från uppföljande ACMD22, så att en delvis skrivning kan se ut
  att ha lyckats när kortet väl har återhämtat sig.
- Vid I/O-fel under uppladdning monteras kortet om innan resten av
  HTTP-kroppen tas emot och slängs. Uppladdningen räknas fortfarande som
  misslyckad och den temporära filen tas bort.

Ägarens test av v0.23.9: ingen watchdog. Felet upptäcktes på cirka 0,1 s
och kortet återmonterades, men själva skrivningen misslyckades fortfarande.

### Gemensam klocka för SD och C6 (v0.23.10)

SD-kortet (slot 0) och C6-radion via ESP-Hosted (slot 1) har egna pinnar men
delar samma SDMMC-controller i P4. Controllern har en gemensam klockdelare
och bara en extra delare per slot. ESP-IDF programmerar om den gemensamma
delaren varje gång nästa kommando gäller en slot med annan frekvens.

Med SD på 20 MHz och SDIO på 40 MHz hände det vid varje växling under en
uppladdning via Wi-Fi. Läget 10 MHz vid återhämtning gav samma växlingar.
Det stämmer med att kortets automatiska CMD12 tappas, men sambandet är en
hypotes tills det har verifierats på hårdvara.

Från v0.23.10 körs P4:s SD-kort på 40 MHz, samma som
`CONFIG_ESP_HOSTED_HOST_SDIO_CLK_KHZ`. Inställningen är låst i
`sdkconfig.defaults.esp32p4`, och bygget stoppas om värdena skiljer sig.
Även återhämtning sker på 40 MHz efter en strömcykel av kortet.

Om kortet inte går att montera på 40 MHz provas 20 MHz med 4 och sedan 1
datalinje. Loggen varnar då för att delarna åter skiljer sig. Kontrollera att
startloggen visar `mounted ...: 4-bit 40000 kHz`.

API: `GET /api/log/sd` ger `{"enabled":true|false}`;
adminskyddat `POST /api/log/sd?enabled=0|1&pin=...` ändrar SD-loggning.
`GET /api/log/download?ram=1` ger enbart `info.txt` och `ram.log`.
