# Batteri- och användningshistorik

Aktivera **Femminutershistorik på SD** under Inställningar → Display
eller i webbinställningarna. Funktionen är av som standard. Diagnostik
visar senaste filen, skrivna/tappade poster och vilolägets faktiska status.

En separat lågprioriterad uppgift samplar varje sekund och skriver en CSV-post
efter fem minuter. Avstängd loggning sparar ett sista kortare intervall.
Om SD saknas eller skrivningen misslyckas räknas posten som tappad och ett
meddelande loggas; inget skrivs till intern flash som reserv.
Ett abrupt strömavbrott eller en krasch tappar det ännu osparade intervallet.

## Filer och innehåll

Från v0.24.3 ligger filerna i `/sdcard/otr/power/power-ÅÅÅÅMMDD.csv`
och kan laddas ned via filhanteraren. Datumet är UTC; första posten efter
midnatt öppnar nästa dags fil. Ett femminutersintervall över midnatt hamnar
i filen för dagen då intervallet avslutas. Om tiden inte är säker används
`power-bootNNNNNNNN.csv` tills säker UTC-tid finns. Befintliga dagsfiler
fortsätter fyllas på efter omstart; de skrivs inte över.

Högst 150 power-loggfiler behålls. När en ny fil behövs vid gränsen raderas
filen med äldst ändringstid först. Äldre `power-NN.csv`-filer räknas också
in i gränsen, men döps inte om och kan därför ligga kvar efter uppdateringen.
Det finns ingen separat storleksgräns per fil eller totalt; 150 filer är inte
en garanti om 150 dagars historik. Andra filer och kataloger lämnas orörda.
Fel vid katalogläsning, gallring eller skrivning ger en tappad post och loggas.

CSV-schema 1 innehåller:

- Kort, firmware, boot/resetorsak, UTC-epoch, tidskälla, säker tid och drifttid.
  Tidskälla: 0 ej satt, 1 osäker återställd, 2 manuell, 3 NTP, 4 RTC.
- Intervallets längd i ms, batteriets min/medel/max-mV, antal giltiga prover
  och spänningsdelarens skalfaktor. Ogiltig/okalibrerad spänning ger tomma
  batterifält, inte 0 mV.
- Skärmens tända tid och tidsvägd effektiv ljusstyrka (0 när släckt),
  uppkopplad Wi-Fi-tid och RSSI vid periodslut.
- Förstärkarens på-tid, tidsvägda inställda SFX-/musikvolymer och vilotid.
  Förstärkare på är inte samma sak som hörbar uppspelning.
- Tid med pågående Yatzy, Pig, Farkle, SCC, äventyr eller spelande karaoke.
  Övriga lägen/menyer grupperas som `idle_other_ms`. Det är sessionstid,
  inte ett mått på hur aktivt personen trycker på skärmen.
- Lyckade blocköverföringar till/från SD och lyckade partitionsläsningar/
  skrivningar för FFat. Dessa inkluderar filsystemets metadata/cachetrafik;
  de är inte filstorlekar. FFat-raderingar, NVS, OTA och core dump ingår inte.
  Loggens egna SD-skrivningar syns i nästa intervall.
- Lägsta fria interna RAM och största fria interna minnesblock, förändring
  av SD-fel och ljudunderruns, DS3231-temperatur om tillgänglig och tidigare
  tappade poster.

Wi-Fi-byteantal, CPU-belastning, laddnings-/USB-status och faktisk ström
mäts inte av detta schema. De ska inte tolkas som noll förbrukning.
Wi-Fi-, skärm- och boot-händelser finns fortsatt i den vanliga loggen.

## Jämförande batteritest

Använd samma kort, batteri, firmware, kalibrering och arbetsbelastning.
Jämför minst flera femminutersperioder med samma startspänning och liknande
temperatur. Ändra en sak i taget, exempelvis ljusstyrka eller viloläge.
Ta bort extern laddning/matning manuellt; firmware detekterar den inte.

Batterispänningens fall kan jämföras men ger inte mA, mWh eller exakt
återstående batteritid. LiPo-kurvan är olinjär och spänningen återhämtas när
belastningen minskar. DS3231-temperaturen är modulens temperatur, inte
en verifierad batteritemperatur. En framtida strömsensor kräver känd modell,
kalibrering och rätt placering i batteriets matningsväg.

## Valbart viloläge

**Viloläge: Wi-Fi och ljud av** är separat från loggningen och av som
standard. Det använder samma inaktivitetstid och manuella släckknapp.
Utan viloläge fortsätter befintlig skärmsläckning som tidigare.

Viloläge tillåts inte under spel, äventyr, karaoke, filöverföring eller OTA.
Det släcker bakgrundsbelysningen, stoppar Wi-Fi-drivrutinen och pausar
ljudmixern/I2S samt stänger förstärkaren. Inställningar, sparade nätverk och
musikposition bevaras. Touch väcker och återställer ljud/Wi-Fi; första
trycket skickas inte till spelet. Släpp och vänta minst 250 ms före nästa tryck.

På P4 från v0.23.19 skickar ekoläget även ST7701:s display-off/sleep-in.
LVGL:s bildöverföringar till panelens framebuffer hoppas över medan panelen
sover. Vid touch skickas sleep-out, 120 ms väntan och display-on innan ljuset
tänds; skärmen invalidieras för ny uppritning. Första väcktrycket förbrukas
som tidigare. Fel loggas; vid misslyckad väckning hålls ljuset släckt och
väckningen försöks igen högst en gång per sekund. Vid misslyckad insomning
avbryts ekoläget och panelen försöks återställas.
Från v0.23.20 sover P4-panelen vid all skärmsläckning (tidsgräns eller
manuell), även utan viloläge. Misslyckad panelvila vid vanlig släckning
loggas och ljuset förblir släckt.
S3 behåller tidigare beteende med enbart bakgrundsbelysning av.
Diagnostik visar om panelvila stöds och panelens aktuella vilostatus.

Detta är **inte light/deep sleep**. CPU, touch, RTC, SD-loggning
och delar av codec/radiokortets elektronik är fortsatt matade. P4:s
panelmatning, DSI/DPI-videoström och PHY-LDO stängs inte av; controller-sleep
är inte samma sak som att göra displayelektroniken strömlös.
På P4 stänger det inte av C6-kretsens matning.
Webben kan inte väcka en stoppad Wi-Fi-radio.
Fel vid stopp/återstart loggas och faktisk status visas i Diagnostik.
Den ytterligare besparingen från panelvila är ännu inte mätt på hårdvara.

## P4: fortsatt strömsparutredning 2026-11-02

Detta är en utredning, inte nya firmwarefunktioner. Ägaren rapporterar mycket
svagt kvarvarande skärmljus efter panelvila. Tidigare bekräftad display-off
betyder därför inte att bakljuset är helt avstängt.

### Nattkörningen på v0.23.22

Ägarens `otr_0.log` är en textlogg, inte femminutershistorikens CSV.
Batteriet har enligt ägaren deklarerad kapacitet **2000 mAh**; faktisk
kapacitet, startladdning och extern matning mäts inte av firmware.
Råloggen ska inte checkas in.

| Urval ur loggen | Observation |
|---|---|
| Samma körning, boot 62, 00:51:14–08:31:15 | 4038 → 3543 mV, fall 495 mV på cirka 7 h 40 min |
| Oavbrutet rapporterad skärmvila 00:56:01–08:06:13 | Cirka 7 h 10 min utan loggad touchväckning |
| Batteriprover inom denna skärmvila, 00:56:14–08:01:15 | 4026 → 3588 mV, fall 438 mV på cirka 7 h 5 min |
| Sista provet i filen, efter ytterligare uppstarter | 3513 mV vid 08:54:00 |

Wi-Fi är fortsatt anslutet under natten, med flera av appen initierade
roamingbyten. Boot 62 har tolv sådana byten; fem ligger mellan 00:56 och
08:06. Mottagningsfel från HTTP-sockets sammanfaller med flera byten.
Det motiverar att även mäta skanning/roaming vid vila, inte att stänga av
återanslutning eller låsa enheten till en försvunnen accesspunkt.
Ingen `idle Wi-Fi stopped`/ekolägesaktivering syns.

Loggen visar ingen panic/watchdog under nattintervallet. Vid cirka 08:33
finns flera nya uppstarter med `reset: poweron`; orsaken framgår inte.
`core dump stored` i en bootrad är inte bevis för en ny nattlig krasch.
Spänningskurvan och 2000 mAh räcker inte för att beräkna mA, förbrukad
kapacitetsandel eller återstående timmar. CSV behövs för att jämföra
spel-/ljud-/lagringsaktivitet; verklig effekt kräver strömmätning.

### Kompletterande CSV från samma natt

Ägarens `power-14.csv` innehåller 93 hela femminutersposter från boot 62,
v0.23.22: totalt 7 h 45 min 9 s. Tiderna är UTC i CSV; loggens lokala
tider ligger en timme före. Intervallens slut går från 23:51:15 UTC
2026-11-01 till 07:31:24 UTC 2026-11-02.

| Fält summerat över filen | Resultat |
|---|---|
| Skärm tänd | 221085 ms, cirka 3 min 41 s, 0,79 % |
| Wi-Fi anslutet | 99,82 % av tiden |
| Förstärkare på (`audio_ms`) | 100 %; anger inte hörbar uppspelning |
| Ekoläge | 0 % |
| Spel/äventyr/spelande karaoke | 0; hela tiden kategoriseras som `idle_other` |
| Batteriets intervallmedel | 4039 → 3547 mV; ändpunkterna ligger 7 h 40 min 9 s isär |
| SD-fel / ljudunderruns / tappade historikposter | Samtliga 0 |
| Lägsta fria interna RAM | 323431 byte; största fria block minst 278528 byte |
| SD läst / skrivet | 1174016 / 272384 byte |
| FFat läst / skrivet | 1388544 / 979392 byte |

Detta bekräftar nästan helt släckt skärm, inga aktiva spel och att det
separata ekoläget inte användes. Ljudelektronikens förstärkare var ändå
på hela tiden, med inställd SFX-/musikvolym 10 %. CSV visar inte om musik
faktiskt hördes. Ett nätanslutet viloläge bör därför även utreda avstängning
av mixer/I2S/förstärkare utan att stoppa Wi-Fi, men bevara avsiktligt
fortsatt ljud och larm. Det är en kandidat, inte uppmätt energivinst.
Lagringsräknarna inkluderar metadata och bakgrundsloggning, inte bara
användarens filöverföringar. CSV mäter fortfarande varken CPU-last,
faktisk klockfrekvens, radioaktiv tid eller ström.

### Panel och separat bakljusdriver

ST7701-vägen använder redan standardkommandona display-off `0x28` och
sleep-in `0x10`, samt sleep-out `0x11` och display-on `0x29` vid väckning.
Samma kommandon finns i Espressifs ST7701-MIPI-driver. Panelens analoga
spännings-/gammaregister ska inte ändras på chans för att släcka LED-ljus.

Waveshares schema har en **separat AP3032KTR-G1 (U9)** för bakljuset:

| Signal | Anslutning enligt schemat | Nuvarande firmware |
|---|---|---|
| `LCD_BL_PWM` | GPIO26 via R43, ljusstyrkereglering | Inverterad LEDC-PWM; 0 % vid vila |
| `BL_EN` | GPIO33 via R32 till U9 EN, pinne 4 | Inte definierad eller styrd |
| LCD reset | GPIO27 | Används vid panelinitiering |

Även Waveshares BSP:s `backlight_off()` sätter bara PWM till noll;
det är inte samma sak som att använda AP3032:s EN-ingång.
**Nästa avgränsade ändring bör vara att styra BL_EN lågt vid släckning**,
behålla noll ljusstyrka medan panelen väcks och återaktivera bakljuset sist.
GPIO33 är inte samma signal som den aktivt låga PWM-styrningen.
Schemat ger en stark kandidat till restljuset, inte ett hårdvarubevis för
orsaken. Prova fullständigt mörker, upprepad väckning och sparad ljusstyrka;
om ljus kvarstår behövs kontroll av EN-nivån och LED-matningen på kortet.
ST7701-kommandon kan inte ersätta den separata bakljusavstängningen.

S3-kontroll samma dag: ES3C35P använder GPIO41, aktiv hög, och firmware
sätter redan PWM till noll vid vila. Publicerad schemagenomgång beskriver
en BSS138 som lågsidebrytare för LED-ljuset; ingen separat enable-pin
identifieras. Originalschemat kunde inte hämtas, så topologin är ännu
sekundärkälluppgift. GPIO26/33 får inte kopieras från P4: de används av
flash/PSRAM på S3-kortet. Se [S3-referensens bakljusavsnitt](hardware_reference.md#7-backlight-control).

### Wi-Fi med bibehållen webbåtkomst

| Läge | Egenskap och begränsning |
|---|---|
| `WIFI_PS_NONE` | Nuvarande vaket-läge; lägst mottagningslatens |
| `WIFI_PS_MIN_MODEM` | Redan begärt vid vanlig skärmsläckning på S3/P4; anslutningen behålls och radion vaknar vid AP:ns DTIM |
| `WIFI_PS_MAX_MODEM` | Vaknar enligt stationens `listen_interval`; kan ge längre vila, men mer latens och förlorad broadcast/multicast |
| C6 iTWT, 802.11ax | Förhandlade väckfönster; kräver HE-anslutning och AP som faktiskt accepterar iTWT |
| Wi-Fi stoppat | Befintligt separat ekoläge; webben är inte nåbar och kan inte väcka enheten |

MIN_MODEM kräver inte Wi-Fi 6. MAX_MODEM kräver att `listen_interval`
annonseras före anslutningen, inte bara ett byte med `esp_wifi_set_ps()`.
Exempelvis 3/5/10 beaconintervall är rimliga jämförelsepunkter, inte
garanterade millisekunder eller ett beslutat standardval. Testa direkt-IP,
ARP/DHCP, mDNS, WebSocket, roaming och filöverföring; sänk sparnivån under
latenskänslig trafik. Långa intervall får inte göra webben opålitlig.

ESP-Hosted 3.0.10 har iTWT-RPC:er för setup/teardown och status/event.
Även projektets låsta C6-källrevision
`78b1c1c43c02b0c4e898cf87f94679278b1ddaff` innehåller iTWT-utökningen,
som är på som standard för HE-kretsar. Nattloggen visar att värdens
iTWT-utökning initieras, **inte** att ett TWT-avtal har slutits.
Appen förhandlar ännu inget avtal. Visa förhandlat läge och faktiskt
godkänt avtal; enbart AP:ns `phy_11ax` räcker inte som TWT-bevis.
Vid avslag/timeout ska skälet synas och MIN_MODEM behållas som uttrycklig
reservnivå. AP-stödet i ägarens nät är inte fastställt.
OFDMA/BSS-coloring är inte ersättningar för ett förhandlat viloschema.

### P4:s klocknivåer och DSI:s lås

Kortet är verifierat som P4 revision 3.2 med 40 MHz kristall.
IDF v6.1:s revisionsspecifika klockkod stöder **400, 200 och 100 MHz**
från CPLL samt **40 MHz** från kristallen. Lägre kristalldelade nivåer
finns i lågnivåkoden men rekommenderas inte som första appnivå.
Äldre P4-revisioners 360/180/90 MHz ska inte användas som denna boards meny.

Frekvenssänkning via `CONFIG_PM_ENABLE` och `esp_pm_configure()` är möjlig
utan light sleep. Men IDF v6.1:s DPI-driver tar ett **CPU_FREQ_MAX-lås
(`dsi_dpi`) under panelobjektets livstid**, och DSI-bussen tar
**NO_LIGHT_SLEEP (`dsi_phy`)**. Våra ST7701-sleep-kommandon frigör inget av
dessa objekt. Att bara aktivera DFS lämnar därför CPU:n låst till vald
maxfrekvens medan DPI-objektet finns kvar.

Rekommenderad ordning: stoppa/frigör DPI säkert vid panelvila via stödd
drivrutinslivscykel, och återskapa framebuffer/stream före väckning.
IDF:s DPI-driver exponerar ingen vanlig display-off/sleep-callback som
pausar dess DMA och släpper låset. Frigör inte drivrutinens privata PM-lås
och forcera inte en låg klocka medan videoströmmen är aktiv.
Om även DSI-buss/PHY/LDO stoppas måste DBI-kommandovägen återskapas före
sleep-out. Panelens och touchens matning får inte kopplas bort på chans.

Prova därefter DFS med 400 MHz max och först 100 MHz min; utvärdera
40 MHz som ytterligare tomgångsnivå. Detta är min/max-gränser, inte löfte om
att drivrutiner/schemaläggare tillåter varje nivå hela tiden.
Spel, karaoke, OTA och filöverföring ska behålla nödvändig prestanda.
Redovisa verklig CPU-frekvens och PM-lås, inte bara önskad inställning.
Automatisk light sleep är ett senare steg: DSI:s lås, SDIO-väckning från
C6, I2S, loggning och den pollade touchen måste hanteras först.
Att endast C6 är uppkopplad räcker inte för webbåtkomst; HTTP-servern kör
på P4. Hosted-exemplet med sovande värd/network split kräver en separat
arkitektur och väcksignal, inte bara en inställningsändring.

### Källor för utredningen

Waveshares [schema, display/backlight på sida 1][p4-power-schema] och
[BSP:s bakljusstyrning][p4-power-bsp] skiljer PWM från EN.
Espressifs [ST7701-MIPI-driver][p4-power-st7701] visar panelkommandona.
Klocknivåerna kommer från [IDF v6.1:s P4-klockkod][p4-power-clock],
inte den generiska PM-tabellens äldre 360 MHz-uppgift.
Se även [DPI-driverns lås/livscykel][p4-power-dpi],
[DSI-bussens lås][p4-power-dsi], [modem-sleep-dokumentationen][p4-power-wifi],
[iTWT-exemplet][p4-power-itwt] och [Hosted iTWT-API][p4-power-hosted].
Externa dokumentations-/databladsvärdar kunde inte DNS-upplösas vid
utredningen; ingen elektrisk AP3032-läckström eller ny registersekvens
påstås verifierad från ett hämtat datablad.

[p4-power-schema]: https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3/blob/main/schematic/ESP32-P4-WIFI6-Touch-LCD-4.3-schematic.pdf
[p4-power-bsp]: https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3/blob/main/firmware/brookesia/components/esp32_p4_wifi6_touch_lcd_4_3/esp32_p4_wifi6_touch_lcd_4_3.c
[p4-power-st7701]: https://github.com/espressif/esp-iot-solution/blob/master/components/display/lcd/esp_lcd_st7701/esp_lcd_st7701_mipi.c
[p4-power-clock]: https://github.com/espressif/esp-idf/blob/v6.1/components/esp_hw_support/port/esp32p4/rtc_clk.c
[p4-power-dpi]: https://github.com/espressif/esp-idf/blob/v6.1/components/esp_lcd/dsi/esp_lcd_panel_dpi.c
[p4-power-dsi]: https://github.com/espressif/esp-idf/blob/v6.1/components/esp_lcd/dsi/esp_lcd_mipi_dsi_bus.c
[p4-power-wifi]: https://github.com/espressif/esp-idf/blob/v6.1/docs/en/api-guides/wifi-driver/wifi-performance-and-power-save.rst
[p4-power-itwt]: https://github.com/espressif/esp-idf/blob/v6.1/examples/wifi/itwt/README.md
[p4-power-hosted]: https://github.com/espressif/esp-hosted-mcu/blob/v3.0.10/host/features/eh_host_feat_wifi_ext_itwt/include/eh_host_wifi_itwt.h

## Kontrollerad omstart

**Starta om enheten** finns längst ned i den lokala nätverksdelen under
Inställningar och använder samma fördröjda mjukvaruomstart som webben.
Bekräftelsen varnar att osparat spel och osparade inställningar förloras;
den sparar inte automatiskt inställningarnas arbetskopia.
Omstart nekas medan filöverföring eller OTA pågår. Om en överföring börjar
under fördröjningen väntar omstartsuppgiften tills den avslutats.
Misslyckad skapning av omstartsuppgiften visas som fel, inte som lyckad
omstart. FFat-händelseloggningen stoppas under dess lås före omstart så
en pågående sådan skrivning får avslutas.
Detta är inte en garanterad transaktionell avstängning av alla bakgrundsuppgifter;
det ännu osparade historikintervallet kan fortfarande gå förlorat.
