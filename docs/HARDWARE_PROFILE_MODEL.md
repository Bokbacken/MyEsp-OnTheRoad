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

These are approximate starting values, marked as such in the respective
hardware references. Confirm them against an authoritative panel datasheet or
a careful measurement before using them to promise exact physical dimensions.
`pixelPitchXUm()`, `pixelPitchYUm()`, `ppiX()` and `ppiY()` are derived from
the profile values; they must not be stored as a second independent set of
numbers. Unknown active dimensions are zero and the derived helpers return
zero. PPI (pixels per inch) is the accurate display term; the colloquial
"DPI" is often used for the same idea. These helpers report axis PPI, not
LVGL's diagonal-DPI convention.

The immediate purpose is to make the physical information available and
testable. Existing `UiMetrics` still scales from logical resolution. Do not
silently switch the entire UI to physical scaling: a later change should
choose which dimensions benefit from density scaling, preserve usable touch
targets, and verify both reference screens.

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
