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

### Try a wrong key during play

After collecting the **red / 1** key at `(14,5)`, move **one step left** to
`(13,5)`, then press **up** against the **green / 2** side door at `(13,4)`.
It stays locked: the red key remains in inventory, and neither steps nor the
gate timer change. Continue left toward the red door to resume the main route.
This optional blocked attempt does not change the 56-step solution below.
The instructor also describes this test in SV/EN.

The side door is an optional dead-end, not a bypass. If you return later with a
green key and open it, a replacement green key inside at `(13,3)` prevents
the optional experiment from consuming the key needed on the main route.

Use the **joystick** for ice/belts: A* tap-to-walk excludes ice and belt cells
from its route, so it cannot plan through them, even to a destination beyond them.
It can detour around them or leave a stopped player already on one; it does
not simulate automatic chains.
Face a lever; use **Byt / Swap** (upper button) to choose **Spak / Lever**, then
press the lower action button to activate it. Walking into it only turns/blocks.
An unlock consumes the matching key **without moving**: repeat the direction
to enter the cleared door cell.

Exact solution from a fresh start: `R/U/L/D` are single right/up/left/down
joystick inputs; for `A`, use **Byt / Swap → Spak / Lever** if needed, then
tap the **lower action button once**. The selection persists for `AAA`, so
those are three lower-button taps, not three selector taps.
Ignore whitespace, do not repeat held inputs.
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
`@timedgate 2: 36` opens group 2 for 36 subsequent successful cell
displacements after player entry onto the plate (rock entry also triggers).
Old timers tick before a newly entered plate starts its timer. The gate is
three steps beyond the plate: a 33-step margin. Blocking, unlocking,
Activate and Hit never tick timers. A timed group has exactly one plate
**or** one lever. Non-timed groups may share toggle levers, but may not mix
plates with levers. Entering the timed plate again restarts the full timer;
a timed lever activation likewise restarts its timer rather than toggling off.
An ordinary grouped plate still needs a rock, not just the player.

A timer reaching zero closes its gates **even if the player occupies one**.
Closure does not crush or relocate the player: leaving is allowed, but
re-entry is blocked until the group opens again. Saving/reloading while
standing on that closed gate is supported.

Ice continues in the entry direction until non-ice or an obstacle; belts
force their arrow direction. Ice/belts force only the player. A wall,
closed gate, rock, locked door or NPC stops automatic motion at the last
reachable cell: it never automatically pushes, unlocks, vaults or starts dialogue.
Use a fresh manual input/action for those interactions. Every displaced
cell, including automatic movement, counts as a step and ticks timers;
the failed automatic attempt adds neither. Automatic movement is capped at
128 extra steps per input.

The player visibly slides along each cell of an ice/belt chain, including
corners. This is presentation of the already-resolved deterministic move:
animation frames do not spend extra steps or advance timers. Further movement
waits for the glide to finish; releasing the joystick does not cancel forced
motion. Opening the menu snaps to the resolved endpoint before pausing.

Timed groups have a separate HUD line, **Portsteg / Gate steps**.
For example `2:33` means group 2 has 33 successful displacements remaining;
`2:0` means closed, not zero seconds. The line remains visible after expiry,
updates on reactivation and scrolls horizontally if several groups need space.

`@rules` → `par: N` accepts 1–65535; omit it for no target.
`par: 56` is a displacement target, not a command count:
56 or fewer steps meets it, 57 does not. It does not prevent completion.

New saves use **engine save format 4**, preserving colored inventory, lever
latches and remaining timers (the enclosing SD-session container remains
version 1). The engine still reads legacy formats 1–3
for legacy state; their generic keys and existing grouped plates/gates retain
their meaning, with new colored inventories, lever latches and timers initially
empty/off. Loading a legacy save does not invent S2 progress; the next save
writes format 4. Older firmware cannot read format 4. Keep the adventure folder
and map ID/dimensions stable when retaining saves. Par and configured timer
durations come from the freshly loaded `.adv`, not from the save. Legacy saves
restore their old terrain/object snapshot; they cannot preserve new S2 content.

## Validation

Run the compile command at the top of `tests/test_s2_demo.cpp` from repo root.
It parses both real files, checks byte equality and executes the solution.
It also starts a real SD session, saves with the red door open, a green key
held and 11 timer steps remaining, then restores into a fresh session/world
and completes the same README solution at 56 steps. An intermediate restore
must remain incomplete; restoring the final exit save must restore completion.
Use the production `Adv::parseFile` loader for S2; the older reference parser
in `tools/adv_parse.*` remains S1-only and is not an S2 validator.

Manual device checklist — **not hardware-tested here**. Repeat on **320×480**
and **480×800**, in SV and EN:

- Find the SD adventure; read the instructor without clipped controls/text.
- Face the lever; select Spak/Lever with Byt/Swap, then verify the lower button
  toggles its lamp/gate. Walking into the lever must not activate it.
- Cross the timed gate; verify the HUD's group-2 step countdown, expiry at
  `2:0`, and that waiting or pausing does not decrement it.
- Use the joystick across ice and all four arrows; verify visible gliding
  through the cells/corners at both zooms, release/STOP, and menu interruption.
- Match all six key/door colors **and 1–6 markers**, including HUD counts;
  try the green side door with the red key as above, and verify unlocking
  requires another movement input.
- Reach the exit at 56/par 56; restart and make a detour to exceed par.
- Save/reload mid-route and verify inventory, lever and timer state persist.
