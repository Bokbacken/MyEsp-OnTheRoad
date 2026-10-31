# Filhantering och SD-prestanda

## Förbättringar

- FAT Long File Name-gränsen höjdes från 64 till 255 UTF-16-kodenheter. Webbfilsystemet validerar UTF-8, komponent- och fullständiga sökvägsgränser.
- Uppladdningar skriver först till en unik, exklusivt skapad temporär syskonfil. En befintlig destination sparas tills den nya filen installerats; avbrutna eller misslyckade skrivningar ersätter inte originalet.
- Konflikter kan stoppas, hoppas över eller skrivas över. Hoppa över är standard och en hoppad fil avbryter inte resten av batchen.
- SD-uppladdningar körs en i taget både i webbläsarens batch och på servern. En uppladdning dubbelbuffrar mottagningen i 32 KiB-block och försöker använda en 32 KiB DMA-kapabel skrivcache, med PSRAM som reserv. Skrivningarna till själva SD-kortet är fortsatt serialiserade.
- Kataloger kan väljas eller släppas från webbläsaren; deras struktur återskapas upp till tre mappnivåer.
- SD-kort kan formateras som FAT från filhanteraren. Enheten använder inte exFAT; FAT-formatering kan därför göra större kort användbara. Två separata bekräftelser krävs, och en konfigurerad admin-PIN måste anges. Formatering raderar allt på kortet. Den uttryckliga åtgärden kan också formatera ett kort vars filsystem inte gick att montera; vanlig start och automatisk återmontering formaterar aldrig kortet. Webbläsaren visar status medan formateringen pågår och ett tydligt resultat efteråt.

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
