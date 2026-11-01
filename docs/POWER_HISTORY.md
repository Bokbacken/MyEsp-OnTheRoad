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

Filerna ligger i `/sdcard/otr/power/power-00.csv` till `power-15.csv`
och kan laddas ned via filhanteraren. Vid varje boot väljs boot-numret
modulo 16 som startfil. Den filen ersätts vid första posten för den nya
körningen. Vid 1 MiB roteras till nästa fil, som också ersätts.
Detta begränsar historiken till högst 16 MiB; det är inte 16 kalenderdagars
garanterad retention. Boot-numret och drifttiden anger ordningen när UTC är fel.

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
