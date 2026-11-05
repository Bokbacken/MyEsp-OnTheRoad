# Hardware profiles: a maintainable model

This note describes how this project records hardware now and how to extend
the approach without tying application behavior to one processor or board.
It is also intended as a reusable starting point for future embedded projects,
including a possible home-automation HMI.

## Decisions

- Keep the firmware's authoritative, build-time hardware profile in typed C++
  (`main/hw_profile.h/.cpp`). No second hand-edited YAML, TOML or XML file
  should repeat values used by firmware.
- Keep a readable hardware reference for each physical board model. A profile
  is the actual supported build combination, selected in Kconfig and matched
  to its ESP-IDF target; it is not a claim that the display is intrinsic to
  that SoC.
- Put pin and bus assignments in `main/board_pins_<profile>.h`; put driver
  setup behind the existing `board_*.h` APIs. Keep measured facts, source
  links, revisions and unresolved questions in the board's reference.
- Add profile data when firmware or a shared UI policy consumes it. Do not
  grow a universal hardware database of speculative fields.
- Distinguish *present*, *connected*, *firmware-supported*, *controllable*
  and *physically verified*. A capability flag should not imply all of them.
  Keep unknown values explicit (zero only when the field documents that
  convention); never guess a value to complete a table.

## Current profile and display geometry

`HwProfile::Info` currently holds logical portrait resolution, coarse
touch/audio/SD/radio capabilities and resource policy (flash, PSRAM, karaoke
limits). It has been extended with the physical active display width/height
in micrometres. The active area is the lit pixel area, not the glass bezel or
module outline.

The two boards' initial geometry comes from the owner's reported pixel pitch,
not independent panel measurements or a newly verified manufacturer drawing:

| Profile | Logical resolution | Active area used | Derived pixel pitch | Derived axis density |
|---|---:|---:|---:|---:|
| `ES3C35P` | 320 × 480 | 49.312 × 73.968 mm | about 154 µm | about 165 PPI |
| `P4_43` | 480 × 800 | 56.208 × 93.680 mm | about 117 µm | about 217 PPI |
| `S3_35B` | 320 × 480 | 49.312 × 73.968 mm | about 154 µm | about 165 PPI |

`S3_35B` uses a nominal active area derived from a 3.5-inch 2:3 diagonal,
not an independently verified measurement.

These are approximate starting values, marked as such in the respective
hardware references. Confirm them against an authoritative panel datasheet or
a careful measurement before using them to promise exact physical dimensions.
`pixelPitchXUm()`, `pixelPitchYUm()`, `ppiX()` and `ppiY()` are derived from
the profile values; they must not be stored as a second independent set of
numbers. Unknown active dimensions are zero and the derived helpers return
zero. PPI (pixels per inch) is the accurate display term; the colloquial
"DPI" is often used for the same idea. These helpers report axis PPI, not
LVGL's diagonal-DPI convention.

`MyFonts::apply()` uses **P4's existing physical Normal sizes** as the reference:
117 µm per pixel, with 18/24/32/48 px small/body/title/display fonts at Normal
text size and Normal zoom. Screen zoom (85/100/115%) changes physical text,
menu icons, spacing and controls; the independent text-size setting changes
font roles further. Each panel selects the nearest generated font by its
actual capital-A bitmap height, rather than its nominal pixel size.
Existing P4 integer `UiMetrics` dimensions are the Normal geometry baseline;
text containers can grow to avoid clipping. Old settings without `screen_zoom`
migrate to Normal, independently of saved text size.

Device changes apply on Save; Cancel preserves both choices. Web changes
apply when returning to the device's main menu, not during an active game.
Scrollable menus/results remain reachable at large settings. Game boards,
joysticks and art stay fitted to available space rather than receiving a
global magnification transform. Game and karaoke text uses physical font
roles, including Sudoku digits (bounded by their fixed cells); adventure HUD
rows reserve the selected line height before allocating the viewport canvas.
Host checks compare actual capital-A bitmap heights (within 12% of the P4
physical target across zoom/text combinations) across generated font
steps and exercise boot, Wi-Fi, Sudoku, adventure, menu and karaoke
layouts. Physical panel measurement and on-device readability checks remain
necessary before promising exact millimetres. Smaller panels still cap text
size at Large; P4 additionally offers Extra Large.

For square visual assets, a future UI helper may calculate pixels for a
target physical length from `pixelPitchXUm()`; horizontal and vertical
dimensions should use their own pitch where a distinction matters. Apply
physical scaling selectively to visual size, spacing and gesture thresholds.
Keep the clickable region at least as large as the current accessibility /
finger-target policy, even when the icon itself gets smaller. Do not infer
touch-target size from pixel pitch alone.

## What goes in which file

**`main/hw_profile.*` — compiled facts and policies consumed by code**

- Stable profile ID and human-readable model name.
- Default logical resolution and measured/estimated active display area.
- Capabilities only where common code branches on them.
- Shared resource budgets and defaults that are genuinely board-dependent.
- Future typed component descriptors only when a component differs across
  supported profiles and the common driver/UI needs the difference.

**`main/board_pins_<profile>.h` and `main/board_*.cpp` — electrical mapping**

- GPIOs, buses, addresses, lane counts, frequencies, polarity and reset.
- Pin sharing, strapping restrictions and unavailable signals.
- Driver-specific initialization that cannot be common.

**`docs/hardware_reference.md` or the model-specific hardware reference —
human evidence and service information**

- Exact vendor, model, board revision/SKU and SoC.
- Display module/controller, physical active size, resolution, source and
  whether physical dimensions are datasheet values, owner estimates or
  measured values.
- A button/control inventory: physical label and location, electrical
  connection or non-GPIO circuit, active level, reset/boot/strapping
  implications, supported firmware action and wake/sleep behavior.
- Audio chain (codec, ADC/DAC, amplifier, speakers/microphones and routing),
  power/charging/battery path, storage/radio/USB, connectors and limitations.
- For each assertion: source (schematic, datasheet, vendor BSP, physical
  measurement or owner report), date/revision when relevant, and verification
  status. Keep conflicting evidence and uncertainty visible.

**README and `docs/MULTI_BOARD_TODO.md` — navigation and current status**

- README links to the profile-model guide and each board reference.
- The multi-board checklist says what builds, what has been tested on hardware
  and what remains; it is not a duplicate pin database.

For a reproducible profile change, update code, pin map, relevant board
reference and profile/driver tests together. Keep the existing per-board
references; a common index or comparison table should link to them, not
replace them with one giant specification file.

## Buttons and other components: stage their modeling

Button pins are already recorded where known (`PIN_BOOT_BTN`), and the board
references describe RESET, BOOT and the P4 POWER circuit. That is sufficient
while application code does not consume different button capabilities through
a common API. Do not add a generic `ButtonInfo[]` just to catalogue controls.
When firmware starts using a physical control on more than one board, add a
typed control descriptor containing at least semantic role, presence,
electrical source (GPIO or external circuit), active level, wake capability,
and any reset/strapping caveat. Keep debounce and action policy in the shared
input layer rather than board pin definitions.

Use the same rule for audio, power, radio, camera, storage and connectors:
document their complete physical path now; add a shared typed descriptor or
capability only when a portable caller needs to select behavior. Avoid a
single `hasAudio`-style flag when products expose materially different
microphone, DAC, amplifier or routing behavior. A later `DisplayInfo`,
`TouchInfo`, `AudioInfo`, or `ControlInfo` can group coherent facts once
multiple profiles actually share and consume that model; do not make a
premature schema for every imaginable module.

## File formats and possible standard migration

There is no one portable, universally adopted board-description format that
ESP-IDF consumes automatically today. Device Tree is the established
declarative hardware description used by Linux and Zephyr, with component
nodes and connections. DTS uses braces and semicolons rather than indentation
for hierarchy, but adopting it in this project would require bindings,
validation, build integration and generated C++/Kconfig; Zephyr's bindings
also use YAML. Do not introduce it only to replace a few C++ profile rows.

TOML is a readable, whitespace/tab-tolerant configuration syntax, but it does
not define hardware-component semantics or provide this project's typed
validation/build integration. A TOML file plus handwritten C++ values would
create two sources of truth unless generation and equivalence checks are
added. XML is unnecessary for the current model. If the project reaches many
board/display combinations or needs runtime-selectable modules, first
prototype Device Tree with the existing ESP-IDF build, and require generated
profiles to be validated against independent fixtures in CI before migration.

- [Device Tree Specification: source language](https://devicetree-specification.readthedocs.io/en/stable/source-language.html)
- [Zephyr: board porting and board variants](https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html)
- [ESP-BSP: common APIs for display, touch, audio and storage](https://github.com/espressif/esp-bsp)
- [TOML v1.0 specification](https://toml.io/en/v1.0.0)
- [LVGL display API: DPI](https://lvgl.io/docs/open/api/public/display/lv_display_h)
