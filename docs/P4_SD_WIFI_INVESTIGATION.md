# P4: felsökningsresan för SD och Wi-Fi

Felsökning 2026-10-30–31 på Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3,
P4 revision 3.x, ESP-IDF v6.1 och ESP-Hosted 3.0.10 med C6.
Syftet är att kunna återanvända erfarenheterna på fler P4-baserade kort.
Aktuella inställningar och testinstruktioner finns i
[filhanteringsreferensen](FILE_MANAGEMENT.md).

## Symtom och första hypoteser

Stora filuppladdningar över Wi-Fi gav SD-skrivfel, ibland skärmblinkning,
och kortet kunde bli oanvändbart tills det återmonterades. Windows
diskkontroll hittade återvunna filer/kataloger efter felen.
Ägaren rapporterade också problem med Wi-Fi-anslutningen.

Återmontering på 10 MHz verkade hjälpa i vissa försök. Det bevisade inte
att frekvensen var grundorsaken: återhämtningen ändrade samtidigt kortets
tillstånd och strömcyklade det. Uppladdningar begränsades till en session
som försiktighetsåtgärd. Samtidighet, kortet, signalintegritet och delad
controller var fortfarande möjliga förklaringar.

De vanliga SD-loggarna saknade det viktigaste felögonblicket: samma kort
som skulle spara loggen hade slutat fungera. Watchdog-utskriften gick
dessutom direkt till serieporten och saknades i RAM-loggen.

## Diagnostiken som gjorde förloppet synligt

v0.23.8 lade till frame pointers för avkodning mot exakt ELF och
en oberoende sampler på CPU 1. Den rapporterar aktuellt SD-kommando,
föregående datakommando, statusfrågornas antal, R1-svar och tidsåtgång.
Samplern tar inte SD-låset och använder inte kortet.

SD-loggning kunde pausas utan att stoppa seriell/RAM-insamling, och
RAM-loggen kunde hämtas utan att läsa kortet. PuTTY sparade hela
serieflödet från start genom fel och återhämtning.

Detta visade ett reproducerbart förlopp:

1. En skrivning av flera block (CMD25) följs av
   `SD_TRANS: handle_idle_state_events unhandled: 00000100`.
   Bitten är ett svars-timeout-avbrott (RTO).
2. CMD13-statusfrågor visar `r1=00000c00`: kortet är kvar i
   mottagningsläge, RCV, och är inte redo för nästa överföring.
3. Antalet statusfrågor växer till över 100 000 under cirka fem sekunder.
   ESP-IDF:s `sdmmc_wait_for_idle` gör bara glest fördelade
   `vTaskDelay(1)`, vilket kan svälta IDLE0 på skrivtaskens kärna.
4. Watchdogen rapporterar IDLE0 medan CPU 0 kör `fs_upload_sd`.
   Den fångade v0.23.8-stacken pekar vid utskriftsögonblicket på
   UART-loggning; den är inte ensam ett fullständigt stackbevis för loopen.
   Kommandospårningen och den lästa IDF-koden förklarar däremot väntan.
5. Fortsatta skrivförsök misslyckas och återhämtningen dröjer.

Samma fel uppstod när SD-loggningen var pausad. Pausen eliminerade alltså
inte utlösaren; loggern var inte en nödvändig förutsättning för felet.

## v0.23.9: stoppa följdfelen utan att ändra startklockan

SD-transaktionswrappern skickar ett explicit stoppkommando (CMD12) när
CMD13 visar RCV. Försök begränsas till högst ett per 500 ms. När kortet
är upptaget längre än 2 ms släpps CPU:n en tick mellan statusfrågorna.
Watchdog-gränsen höjdes inte.

Ett tätt kopplat fel i ESP-IDF v6.1 hittades också:
`sdmmc_write_sectors_dma` skriver över resultatet från en misslyckad CMD25
med resultatet från uppföljande ACMD22. Efter lyckad återhämtning skulle
en delvis skrivning då kunna rapporteras som lyckad. Wrappern bevarar
skrivfelet; ett lyckat stoppkommando innebär aldrig att filen är färdig.

Uppladdningshanteraren börjar återmontera kortet före tömningen av
återstående HTTP-kropp, inte efter. Den misslyckade filen ersätter inte
en befintlig destination.

**Ägarens test:** skrivningen misslyckades fortfarande på 20 MHz, men
CMD12 lyckades, den gamla femsekundersloopen uteblev och ingen watchdog
syntes i försöket. Återhämtningen startade inom cirka 0,1 s från avbrottet.
Själva återmonteringen tog cirka 27 s; spårningen visade fortlöpande
FAT-läsningar under den tiden, inte samma gamla status-spinn.

## Den gemensamma controllern och v0.23.10

Separata pinnar betyder inte separata SDMMC-controllers. På denna P4
använder SD-kortet slot 0 och C6-SDIO slot 1 i samma controller.
ESP-IDF serialiserar deras transaktioner med ett gemensamt mutex.
Controllern har en gemensam klockdelare och en extra delare per slot.

Läsning av v6.1:s `sd_host_sdmmc.c` och `sd_trans_sdmmc.c` visade att
drivrutinen programmerar om klockan när nästa slots faktiska frekvens
skiljer sig från den begärda. SD på 20 MHz och C6 på 40 MHz medförde
sådana byten under Wi-Fi-uppladdning. SD på 10 MHz gav också byten.

**Hypotesen:** ett klockbyte kan störa ett ännu pågående automatiskt
stoppkommando efter dataöverföringen. Det passar RTO och RCV i loggen,
men vi har inte mätt signalerna med logikanalysator eller bevisat
vilket kommando som orsakade det asynkrona avbrottet.

v0.23.10 använder därför 40 MHz på både SD och C6, även vid normal
återhämtning. Klockinställningarna kontrolleras vid kompilering.
Om SD-kortet inte kan monteras på den hastigheten finns fallback till
20 MHz med varning om skilda klockor.

**Ägarens resultat, samma uppdaterade körning över Wi-Fi:**

| Filstorlek | Total tid | Genomströmning |
|---:|---:|---:|
| 29 258 148 byte | 34,438 s | cirka 0,85 MB/s |
| 265 847 632 byte | 328,958 s | cirka 0,81 MB/s |

Inga SD-fel, watchdog-utslag eller återmonteringar syns efter
v0.23.10-starten i den inskickade loggen. Äldre fel före den starten
får inte tillskrivas det nya bygget. Resultatet stärker klockhypotesen
men bevisar inte full stabilitet, ökad hastighet från just klockändringen
eller att tidigare Wi-Fi-/skärmproblem har samma orsak.

## v0.23.11: separat 50 MHz-experiment

Vanliga bygget behåller 40 MHz och en samtidig SD-uppladdning.
Det separata `50mhz-experimental`-artifactet begär 50 MHz på båda slotar.

MyPi-Hotspot hade redan visat varför endast ett konfigurationsvärde
på 50000 inte räcker: ESP-IDF begränsar normalt SDIO-high-speed till
40 MHz. Dess linker-wrapper höjer gränsen först efter lyckad
high-speed-förhandling. Samma princip används här för C6; SD-minneskortet
kan redan förhandla 50 MHz. P4 använder då en 200 MHz-klockkälla delad
med fyra. Det är inte UHS SDR50 eller ett byte till 1,8 V.

Experimentet loggar begärd, förhandlad och faktisk klocka på båda slotar
och avvisar en 50 MHz-initiering som inte når 50 MHz. MyPi-Hotspot använde
C5, så dess tester verifierar inte denna C6, detta kort eller denna layout.
50 MHz är ännu inte hårdvaruverifierat.

## Att återanvända på nästa P4-kort

Kontrollera först kortrevision, schema, slotfördelning och drivrutinernas
faktiska controllerdelning. GPIO för strömcykel, polaritet och LDO-kanal
är kortspecifika; kopiera inte Waveshare-pinnarna till en annan layout.

Mät faktisk klocka efter båda kortens förhandling. Lika inställningsvärden
räcker inte om SDIO blir begränsat till 40 MHz eller SD-kortet faller
tillbaka till 20 MHz. Lika överföringsklockor eliminerar inte klockbyten
under initiering eller återmontering.

Spara seriell logg och exakt ELF/version. Isolera varje uppstart i en lång
logg; tidsstämpeln kan ändras vid NTP. Använd RAM-insamling när SD är trasigt,
och behåll inte nätverksuppgifter eller kompletta privata loggar i repot.

Ändra en variabel åt gången: först gemensam klocka, sedan 50 MHz, därefter
samtidighet. Testa upprepade stora filer, verifiera innehållet med hash efter
återläsning, och testa långvarig Wi-Fi-belastning samt återhämtning.
Slutförd HTTP-uppladdning ensam bevisar inte dataintegritet.

Avregistrera endast SD-kortets slot. Global controller-deinit skulle
även slå ut radiolänken. En återmontering får inte ske medan en annan
uppladdningssession fortfarande har en fil öppen.

## Kvar att verifiera

50/50 MHz, längre stabilitetstest och hashkontroll av överförda filer
återstår. Flera samtidiga sessioner behöver samordnad återhämtning innan
de slås på igen. GPIO42-varningen vid återmontering och orsaken till
skärmblinkningen är ännu inte förklarade. Kopplingen mellan SD-felen och
Wi-Fi-avbrotten behöver tidskorreleras; förbättrat SD är inte bevis för
att alla nätverksproblem är lösta.

Arbetets status följs i [multi-board-planen](MULTI_BOARD_TODO.md).
