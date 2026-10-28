# Idé: XSB → .adv-import (fristående parser)

> Hör hemma i **äventyrsskapar-repot**, inte i motor-/uppspelar-repot.
> Status: idé, ej planerad. "Främst för eget intresse" (Kent).

## Mål
En fristående parser som läser Sokoban-`*.xsb`-filer och skriver körbara `.adv`-kartor
direkt — ingen motorändring krävs, filerna spelas i befintlig uppspelare.

## Varför XSB först
- Rent ASCII-rutnät, samma princip som `@map` → nästan 1:1.
- Public domain-samlingar finns i tusental (Skinner m.fl.).
- Passar hela sokoban-spåret (grupperade plattor/grindar, Steg 1).

## Teckenmappning (utgångspunkt)
| XSB | Betydelse | .adv |
|-----|-----------|------|
| `#` | vägg | `#` |
| `@` | spelarstart | `S` |
| `$` | låda | `c`/`C` (crate) |
| `.` | mål | tryckplatta `o` |
| `*` | låda på mål | crate + plate |
| `+` | spelare på mål | `S` på plate |
| (blank) | golv | `.` eller `_` |

## Två saker parsern måste lösa
1. **Padding till rektangel.** XSB-rader har ojämn längd; loadern kräver lika långa
   rader. Padda med vägg eller golv till kartans maxbredd.
2. **Låda vs sten mot platta.** Motorn utlöser plattor med **sten** (`R`), XSB:s mål
   matchar **lådor**. Antingen mappa `$`→sten, eller låta plattan trigga på crate.

## Bonus (med Steg 1)
Flera XSB-banor → en enda `.adv` med grupp-id på plattor/grindar: nästa bana låses
upp först när den förra är löst (ingen fil per del-bana).

## Övriga format (lägre prioritet)
- PuzzleScript: har redan en legend-sektion i samma anda som `@legend`; bara layouten
  konverterar automatiskt, reglerna är spelspecifika.
- Tiled .tmx/.json: kräver tile-id→glyph-tabell per tileset.
- Chip's Challenge .ccl / Boulder Dash .bd: binärt/egna mekaniker, dyrare.
