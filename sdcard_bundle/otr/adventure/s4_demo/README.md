# S4: Suction and sentry gallery (v0.21.1)

Copy this folder, including `s4_demo.adv`, to **`/otr/adventure/s4_demo/`**
on SD. A byte-identical mirror is provided at
`sdcard_bundle/otr/adventure/s4_demo/`. Select **Suction and sentry gallery /
Sugkoppens vaktgalleri** and start a **new game**. This external **49×25**
adventure is not compiled into the firmware. Hardware rendering/play is
**unverified**; the checks below use the production loader and engine on a host.

## Controls and itinerary

Coordinates are zero-based. **U/D/L/R** are individual joystick moves.
**A** selects **Lever / Spak** with **Swap / Byt**, then taps the lower
action button once. **P** selects **Pull / Dra**, then taps that action
button once; selector taps are not additional moves. Pull uses the current
facing: the adjacent rock/colored box moves toward you while you step
backwards. Do not hold buttons or substitute tap-to-walk.

1. Push the rock at `(4,3)` down. This uncovers and immediately collects
   the hidden suction equipment; the rock remains at `(4,4)`.
2. Walk to `(12,3)`, face the lever with U (a blocked move), then A.
   The lever turns on, but the red plate is empty, so the AND gate stays shut.
3. From `(8,4)` push the red box right onto its matching plate `(10,4)`.
   The gate `(16,4)` opens. Move U then D to deliberately change facing;
   R now faces the box but cannot push it into the wall `(11,4)`.
   That blocked R changes facing **without an enemy turn**. P pulls the
   box off the plate and closes the gate; R pushes it back and reopens it.
4. Take the upper corridor east, then descend to row 11. Walk west through
   the bounded chaser's room, `(18,11)`–`(28,16)`. The blue box `(23,12)`
   blocks its direct approach; it replans around the box. This exact route
   avoids contact; other timings can cost energy. Leaving its area stops
   its pursuit.
5. At `(7,11)`, push the rock down into `(7,13)` to block the bounded
   ping-pong patrol's narrow route. It cannot push the rock or leave
   its area `(4,13)`–`(10,13)`. The separate patrol on row 17 has **no
   area declaration**; its surrounding walls, not an implicit rectangle,
   keep it in its gallery.
6. Continue west to x=2, descend to row 21, then walk east to `(23,21)`.
   U faces the green box `(23,20)` but cannot push it into the wall.
   P pulls it into the only opening between the lower chaser's two rooms,
   while you retreat into the safe alcove `(23,22)`. Walk L U L R:
   the chaser now has **no path**, despite your standing inside its area.
   D R U pushes the green box back up, reopening the choke; the chaser
   immediately resumes its pursuit. Continue east, collecting all three
   gems and a spare suction pickup before the exit `(46,21)`.
   Start with 64 energy: ordinary walking costs none and contact costs 2.

## Exact reproducible solution

Ignore whitespace; execute this entire block from a fresh start. The
host test reads this very fence rather than maintaining a second route.

<!-- solution:start -->
```text
RRD
RRRRRRRRUA
LLLLDR
UDRPR
URRRD
RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR
DDDDDDD
LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL
DU
LLLLL
DDDDDDDDDD
RRRRRRRRRRRRRRRRRRRRR
UPLULRDRU
RRRRRRRRRRRRRRRRRRRRRRR
```
<!-- solution:end -->

Expected finish: **179 inputs / 175 displaced cells**, **58/64 energy**
(three 2-energy contacts in the lower chaser room),
no lives lost, all three gems collected, suction owned, the red plate
occupied, its AND gate open and the lever on.

The test saves/restores after pulling the box off its plate, then
actually continues to reopen the gate. It also saves/restores during the
chaser's detour, immediately after blocking the patrol, and with the lower
chaser blocked by the pulled green box, then completes
the remaining commands and checks the final exit save.
The initial suction pickup, AND condition,
gate re-closing/reopening, orientation-only blocked moves, patrol blocking,
chaser detour, all enemy bounds/state, and single enemy turns are checked.

## Authoring and host validation

`s = suction` is a visible equipment pickup; hidden suction is instead
`4 3 suction` in `@treasures`, underneath the movable rock. `a`/`A` are
the red box/plate, `b` is a blue box, `c` a green box, `!` the group-1 lever,
and `g` its gate.
`@group 1: and` requires both the matching plate and the lever.
The four `@enemies` lines demonstrate ping-pong patrol plus area, two chasers
with areas, and loop patrol without area. Route waypoints use `X,Y`;
rectangle coordinates are inclusive. Enemies do not move rocks/boxes.

Run from the repository root:

```sh
g++ -std=c++17 -Wall -Wextra -Imain -Icomponents/otr_engine/include \
  -Icomponents/otr_story/include tests/test_s4_demo.cpp main/adv_loader.cpp \
  components/otr_engine/otr_world.cpp components/otr_story/otr_story.cpp \
  -o /tmp/test_s4_demo && /tmp/test_s4_demo
```

Both real `.adv` files are parsed by `Adv::parseFile`, loaded into `World`,
and played to completion. The test also checks byte-identical README/map
mirrors. Before claiming hardware verification, repeat in SV/EN at
320×480 and 480×800: SD discovery, hidden pickup, Pull selection/facing,
colored plate/AND gate rendering, all four enemy sprites and movement,
contact energy, save/reload, and final exit.
