# Engine Roadmap: A Portable VERA Game Engine

Vision, not implementation: no code exists yet for this part. The goal is a
data-driven engine — in the spirit of **Scorpion Engine** / **RedPill** —
where game logic and content (rooms, sprites, palettes, scripts) are
platform-agnostic, named here only as the *inspiration* for that split, not
as code to import.

## Why this is feasible: one chip, four buses

The four target hosts don't each need their own graphics engine — they all
drive the *same* VERA chip. What differs is only how the host CPU talks to
it:

| Target | Bus | Status in this repo |
|--------|-----|----------------------|
| Commander X16 | native, memory-mapped | reference platform; VERA registers directly on the bus |
| Atari 8-bit (400/800/XL/XE) | PBI, device space `$D1xx` | **implemented** — `vera_pbi_handler.s`, `vera_driver.s`, `vera_common.inc` |
| Commodore 64 | cartridge port, `IO1` (`$DE00-$DEFF`) or `IO2` (`$DF00-$DFFF`) | not started — no hardware or HAL yet |
| Apple IIe | peripheral slot bus (`$Cn00`/`$C0n0` I/O select) | not started — no hardware or HAL yet |

Because the chip-level register map and VRAM layout are identical across all
four, the asset formats in [`01-vera-asset-format.md`](01-vera-asset-format.md)
and everything built on top of them are shared as-is; only the lowest layer
changes per target.

## Layered architecture

```
Layer 2  Game logic        entity/level state machine, collision, scripts
                            -- zero VERA/bus knowledge --
Layer 1  Asset runtime      loads VSP1/VTS1/VTM1/VPL1, drives Layer 0 primitives
Layer 0  VERA HAL           SET_ADDR / DATA0-1 read-write / palette / sprite-attr
                            -- one implementation per target bus --
```

- **Layer 0 (VERA HAL):** the only target-specific code. On Atari it's
  already implemented (`vera_common.inc` register offsets relative to
  `PBI_ADDR`, `vera_driver.s`). A C64 HAL replaces the PBI device-space
  access with `IO1`/`IO2` reads/writes at fixed zero-page-adjacent addresses;
  an Apple IIe HAL replaces it with slot I/O-select addressing. The X16
  native HAL is the trivial case (register access, no indirection at all).
- **Layer 1 (asset runtime):** because the binary formats already mirror
  VRAM layout, this is close to a copy loop everywhere — read the file,
  stream it through Layer 0's `DATA0`/`DATA1` write primitive with
  auto-increment. The only per-target variance is transfer speed tricks;
  this repo's VERA FX-accelerated scroll/fill (see `README.md`, "Driver di
  Sistema") is the kind of optimization that stays behind the Layer 0/1
  boundary and doesn't leak into game logic.
- **Layer 2 (game logic):** level/room state, entity behavior, input
  handling. Should compile unchanged across targets. `cc65` is the natural
  common toolchain (already the de facto standard across X16/C64/Apple
  II/Atari 6502 homebrew) with a per-target linker config, the same pattern
  this repo already uses for the Atari target (`vera_link.cfg`,
  `vera_pbi.cfg`).

## Rooms as the portability unit

The screen-splitting forced by the `MAP_WIDTH` <= 256-tile hardware ceiling
(see the case study, Path 2) is not just an export detail — it's the natural
unit for level streaming: Layer 1 loads one room's VTM1/VTS1 at a time, so
VRAM budget (which is the same 128KB on every target, since it's the same
chip) is the only thing Layer 2 needs to reason about, independent of host
RAM/CPU speed differences.

## Suggested phasing

1. **Phase 1 (this repo, in progress):** finish the asset pipeline (docs
   1–3) and get one Rastan room + the character sprite rendering on real
   Atari + PBI-VERA hardware using the existing Layer 0 (`vera_driver.s`)
   directly — proves the asset format end to end before abstracting anything.
2. **Phase 2:** factor Phase 1's Layer 0/1 code into a target-agnostic
   `libvera` API, then implement the X16-native Layer 0 against it (should
   be the easiest port, since it's the reference chip wiring).
3. **Phase 3:** design and build the C64 `IO1`/`IO2` HAL — this needs real
   hardware decisions (which I/O range, cartridge signal handling, VIC-II
   bus contention) that don't exist yet.
4. **Phase 4:** design and build the Apple IIe slot HAL — same caveat, no
   hardware spec exists yet.

Phases 3 and 4 are placeholders until the corresponding VERA daughterboards
are designed; nothing in Phases 1–2 should assume their final register
addresses.
