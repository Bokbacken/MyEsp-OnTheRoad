# S2 workshop demo

Copy `s2_demo.adv` to **`/otr/adventure/s2_demo/s2_demo.adv`** on SD
(the ready-to-copy mirror is in `sdcard_bundle/otr/adventure/s2_demo/`).
Select “Workshop training circuit” / “Verkstadens provbana”. Start a new game.
The optional instructor above the start explains the controls in SV/EN.
The existing S1 `sokoban` adventure is unchanged.

## Route and controls

The upper passage teaches the toggle lever, timed plate/gate, ice, then
right/down/left/up belts. Follow the lower passage left, down, then right:
each key precedes its matching door; the exit is at the bottom right.

Use the **joystick** for ice/belts: A* tap-to-walk excludes ice and belt cells
from its route, so it cannot plan through them, even to a destination beyond them.
Face a lever and use **Activate**; walking into it only turns/blocks.
An unlock consumes the matching key **without moving**: repeat the direction
to enter the cleared door cell.

Exact solution from a fresh start: `R/U/L/D` are single right/up/left/down
joystick inputs; `A` is Activate. Ignore whitespace, do not repeat held inputs.
The initial `U` deliberately faces the lever without moving; three activations
demonstrate on/off/on. Automatic motion needs no extra inputs.

<!-- solution:start -->
```text
RUAAA
RRRRRRRR
RRRRDLLDD
LLLLLLLLLLLLLLLL
DD
RRRRRRRRRRRRRRRRRRR
```
<!-- solution:end -->

Expected: exit reached in **56 displaced cells**, meeting **par 56**.
Blocked inputs, activation and the six unlock attempts do not add steps.
The host integration test reads this exact command block, not a second solution.

## Authoring reference

`@legend` lines are `glyph = lever <group>`, `plate <group>`, `gate <group>`,
`ice`, `belt up|right|down|left`, `key <color>` or `door <color>`.
Groups are `0`–`9`, `A`–`Z` (0–35). Legend definitions override built-ins;
`S`, space and `@` are reserved. This demo uses free glyphs, preserving stock
symbols. There are **no comments inside `@map`**; `#` there is a wall.

| Color / UI marker | Key glyph | Door glyph |
|---|---|---|
| red / 1 | q | H |
| green / 2 | g | J |
| blue / 3 | t | L |
| yellow / 4 | y | N |
| purple / 5 | s | O |
| cyan / 6 | e | V |

Color **and number** identify each pair; generic legacy keys do not substitute.
`!` / `I` are group-1 lever/gate; `:` / `]` are group-2 plate/gate;
`i` is ice and `{ > v <` are up/right/down/left belts.

`@timedgate <group>: <N>` accepts `N` from 1 to 65535.
`@timedgate 2: 24` opens group 2 for 24 subsequent successful cell
displacements after player entry onto the plate (rock entry also triggers).
Old timers tick before a newly entered plate starts its timer. The gate is
three steps beyond the plate: a 21-step margin. Blocking, unlocking,
Activate and Hit never tick timers. A timed group has exactly one plate
**or** one lever. Non-timed groups may share toggle levers, but may not mix
plates with levers. Ice continues in the entry direction until non-ice or an
obstacle; belts force their arrow direction. Ice/belts force only the player, never automatic pushing,
unlocking or talking; automatic movement is capped at 128 steps per input.
`@rules` → `par: 56` is a displacement target, not a command count.

## Validation

Run the compile command at the top of `tests/test_s2_demo.cpp` from repo root.
It parses both real files, checks byte equality and executes the solution.

Manual device checklist — **not hardware-tested here**. Repeat on **320×480**
and **480×800**, in SV and EN:

- Find the SD adventure; read the instructor without clipped controls/text.
- Face the lever; verify Activate toggles its lamp/gate, not a movement attempt.
- Cross the timed gate; check timer feedback and eventual closure.
- Use the joystick across ice and all four arrows; verify camera/controls remain usable.
- Match all six key/door colors **and 1–6 markers**, including HUD counts;
  verify wrong colors fail and unlock requires another movement input.
- Reach the exit at 56/par 56; restart and make a detour to exceed par.
- Save/reload mid-route and verify inventory, lever and timer state persist.
