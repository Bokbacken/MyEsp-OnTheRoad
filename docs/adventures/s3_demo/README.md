# S3: Six halls of the ice station (v0.21.0)

Copy this folder to **`/otr/adventure/s3_demo/`** on SD, including
`s3_demo.adv`. A byte-identical ready-to-copy mirror is in
`sdcard_bundle/otr/adventure/s3_demo/`. Select **Six halls of the ice
station / Isstationens sex salar**, then start a **new game**.
This external 49×21 adventure is not an embedded firmware map.
Hardware play and rendering are **unverified**; host validation is not a
hardware-test claim.

## Controls and itinerary

Use individual joystick inputs: **U/D/L/R** move up/down/left/right.
**A** means select **Lever / Spak** using **Swap / Byt**, then press the
lower action button once. Consecutive A inputs are separate action taps,
not selector taps. Automatic sliding needs no extra inputs. Use the
joystick rather than tap-to-walk for the ice and belt exhibits.
Coordinates below are zero-based. The optional keeper above the start
explains the exhibits in Swedish/English; the solution does not open dialogue.

1. Follow the upper corridor east. Without skates, entering ice at `(5,2)`
   slides through `(8,2)` to `(9,2)`. Collect skates at `(12,2)`:
   ice `(16,2)`–`(19,2)` now takes four separate inputs. The right belts
   `(24,2)`–`(25,2)` still force movement to `(26,2)`.
2. Descend at the east end and follow the hazard corridor west.
   Poison/swamp/sharp rocks at x=40/36/32 cost **1/2/4** energy.
   Collect the suit at `(28,6)`, then x=24/20/16 cost **0/1/2**.
   Start energy is **32**, leaving **22**, with no death.
   These three terrains do not consume suit charges (the demo retains all
   eight); existing lava/mine behavior is unchanged. Skates are permanent
   equipment, not consumable charges.
3. Descend at the west end to the six-box gallery. At x=4/10/16/22/28
   on row 10, push **down once**, then return **up**. Each box lands
   on its matching plate on row 12 and opens its own gate.
   Pushing the red box reveals and immediately collects the hidden
   **6-energy potion** at `(4,11)`, raising energy from **22 to 28**.
4. Cyan at x=34 requires **both** its plate and the lever `(35,9)`.
   The sequence deliberately turns the lever on with an empty plate:
   the gate stays closed. Turn it off, push cyan onto its plate:
   still closed. Turn on/off/on: the gate opens/closes/opens.
   Push cyan farther down off its plate: it closes even with the
   lever on. Walk around via x=33, push it back up: it opens again.
5. Cross the cyan gate `(38,10)`, descend east, and return west through
   the rest galleries, collecting three gems before the exit `(2,18)`.
   No suction item is needed.

## Exact reproducible solution

Ignore whitespace; do not hold/repeat buttons. From a fresh start, execute
this entire block in order. The first line has 39 R inputs, the hazard line
44 L inputs, and the final line 44 L inputs. The five identical DU lines
solve red, green, blue, yellow and purple, respectively.

<!-- solution:start -->
```text
RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR
DDDD
LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL
DDDDRR
DURRRRRR
DURRRRRR
DURRRRRR
DURRRRRR
DURRRRRR
RUAALDURUAAALDDLDDRUDLUUUURRRRRRRRRRRRR
DDDDDDDD
LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL
```
<!-- solution:end -->

Expected finish: **224 inputs / 223 displaced cells**, **28/32 energy**,
no lives lost, skates owned,
three gems collected, all six matching plates occupied, all six
gates open and the cyan lever on. The host test also saves/restores
mid-solution, then actually completes the remaining commands.

## Authoring and validation

`a`–`f` are red/green/blue/yellow/purple/cyan boxes; `A`–`F` are their
matching plates. Color **and 1–6 markers** identify pairs.
Colored boxes need **no keys** and are pushable boxes, not destructible crates.
Legend entries override built-ins: in this map `p` means poison, not a potion, and `D`
means the yellow plate, not a locked door. Hidden energy uses the standard
`@treasures` entry `4 11 potion 6`, independent of the visible glyph legend.
`i`/`s` mean ice/skates, `w` swamp, `z` sharp rocks, `>` right belt,
`u` the built-in suit, `!` lever and `*` gems.

Explicit legend entries such as `A = plate 1 red` assign one colored plate
to each group 1–6. `g/h/j/k/m/n` are those groups' gates. `@group 6: and` explicitly
combines cyan's plate with its group-6 lever; neither alone suffices.
The equivalent `@andgate 6` and `plate red` plus `@plategroup 1: 4,12`
forms remain supported. There are no comments inside `@map`.

Run from the repository root:

```sh
g++ -std=c++17 -Wall -Wextra -Imain -Icomponents/otr_engine/include \
  -Icomponents/otr_story/include tests/test_s3_demo.cpp main/adv_loader.cpp \
  components/otr_engine/otr_world.cpp components/otr_story/otr_story.cpp \
  -o tests/test_s3_demo && ./tests/test_s3_demo
```

The test reads this exact solution block (no hardcoded second route),
loads both real `.adv` files with the production loader/engine, checks
mirrors, ice/belts, damage, pickups, every box/plate, gate interactions,
and continuation after save/restore.

Before calling this hardware-verified, repeat in SV/EN at 320×480 and
480×800: verify SD discovery, keeper text, skates/suit inventory, visible
ice/belt motion, all six colored numbered pairs, lever action selection,
open/closed gate graphics, energy updates, save/reload and final exit.
