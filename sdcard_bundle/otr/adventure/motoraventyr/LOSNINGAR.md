# Motoräventyret – lösningsspår (spoilers)

Alla koordinater är nollbaserade. Spåren nedan är också automatiserade i
`tests/test_motoraventyr.cpp`; använd dem om du fastnar eller vill återskapa
testet.

## 01 – Motvind över myren

Tryck på säkra rutor för att avtäcka dem och flagga rutorna med minor. Målet är
att avtäcka alla säkra rutor; `X` är varken nödvändig eller en övergång. Testet
går från `S` till varje säker ruta utan att gå på minor, kontrollerar
`safeLeft()==0` och bekräftar att sessionen inte har en nästa `.adv`-fil.
Efter segern väljer du kapitel 02 manuellt.

## 02 – Stenporten

1. **Karta 1:** gå ned, åt höger och knuffa stenen österut på plattan. Gå runt
   genom raden ovanför, korsa den öppnade grinden och ta portalen längst till
   höger.
2. **Karta 2:** gå ned och knuffa den gröna lådan österut på den gröna plattan.
   Gå tillbaka via övre raden och ta portalen.
3. **Karta 3:** knuffa den röda lådan två steg till röd platta. Gå ned och runt
   blå låda; knuffa den upp på blå platta. Stå vid spaken och aktivera den:
   AND-grinden kräver både rätt lådor och spaken.
4. Gå igenom grinden, plocka den röda nyckeln och öppna dörren. Plocka
   hoppstyltan. Ställ dig vänd mot stenmurens öppning och använd **Hoppa**.
   Landningsrutan på andra sidan är säker; sugproppen finns där. Gå genom
   portalen till draghallen.
5. **Karta 4:** utgången är stängd av en sten på en platta. Stå till höger om
   stenen, vänd vänster mot den och välj **Dra**; spelaren backar och stenen
   följer till plattan. Gå sedan åt höger genom grinden till `X`.

Hoppstyltan fungerar inte som genväg in i draghallen: kartbytet sker via en
separat namngiven portal, och testet kontrollerar att hopp inte öppnar eller
når utgången. Om du knuffar en låda åt fel håll, använd **Starta om**; kapitel 2
återställs till sin första karta och startläge.

## 03 – Fyrarnas sång

1. På karta 1 plockar du plankan och pratar med Signe. Välj **Bygg bron**:
   plankan förbrukas och vattenrutan vid sundet blir bro.
2. Prata med Olof och välj **Skicka fyrlyktan**. En nyckel placeras då på den
   ännu obesökta arkivkartan; flagga, räknare och uppdrag uppdateras samtidigt.
3. Gå till portalen vid sundet. I arkivet hämtar du ficklampan, letar upp
   nyckeln och checkpointen och undviker patrull/jagare. Prata med arkivarien,
   ta den engångsbelöning som villkoret erbjuder och välj **Tillbaka till
   fyren** för dialogresan.
4. Gå tillbaka genom portalen när du är redo och nå utgången på arkivkartan.
   Signe- och Olof-valen kan inte ge samma belöning en gång till. Om du lämnar
   arkivet innan den fjärrplacerade nyckeln hittas ligger den kvar vid återbesök.

## 04 – Vindverkstaden

1. **Karta 1:** knuffa stenen på plattan. Gå ned till spaken, ställ dig framför
   den och aktivera. Både spak och platta behövs för grinden.
2. **Karta 2:** gå ned och åt höger till skridskorna, plocka upp dem och gå
   tillbaka. Knuffa stenen på tidsplattan. Från stenen går du ned, höger två
   gånger, upp genom den öppna grinden och sedan höger till portalen innan de
   åtta stegen löpt ut.
3. **Karta 3:** plocka skyddsdräkten och båten. Gå ut på vattnet, återvänd
   söderut till checkpointen och fortsätt över vatten/strandens gift, träsk,
   vassa stenar och lava mot portalen.
4. **Karta 4:** ta stigen söderut, gå till ädelstenen och knuffa stenen åt
   höger in i lavan. Den blir basalt. Gå upp till checkpointen och fortsätt
   till utgången.

## 05 – Hemresan och 06 – Resurskedjan

I kapitel 05 pratar du med fyrvakten och väljer **Till hamnen**; dialogen
använder samma namngivna destination som portalerna. Avsluta sedan
hamnkartan.

Kapitel 06 är ett tekniskt sidoprov: plocka en stenresurs i fil 1 och gå till
utgången. Den bärs till fil 2, där du plockar nästa sten och lämnar filen.
Fil 3 har `reset: items`, så den startar med noll stenar. Dessa övergångar
sker automatiskt inom samma äventyrsmapp.
