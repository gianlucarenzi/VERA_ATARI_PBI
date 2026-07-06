# Case Study: Rastan Round 1

Concrete walkthrough for the two assets already downloaded. Treat this as the
first pass through the pipeline — several steps below have an explicit open
question because the source art hasn't been measured/curated yet.

## Inventory

| Asset | Path | Format | Dimensions |
|-------|------|--------|-----------|
| Character spritesheet | `~/Downloads/Arcade - Rastan _ Rastan Saga - Playable Characters - Rastan.gif` | GIF, 256-color indexed | 800 × 2010 |
| Level strip | `~/Downloads/RastanRIP/rastan-field-big.png` | PNG, 256-color indexed | 8352 × 448 |

Both are provisional/placeholder art per the project's own description — the
goal here is to validate the pipeline end to end, not to ship final assets.

> Ignore `~/Downloads/RastanRIP/*.py` (`vera_converter.py`, `vera_compose.py`,
> `preview_strips.py`) — pre-existing scripts from a separate, unverified
> experiment. Don't reuse their output format or assume they run.

## Path 1 — Character sprite (LibreSprite)

1. Open the GIF in LibreSprite (`~/Progetti-CVS/libresprite`).
2. The sheet is a loosely packed, non-uniform atlas, not a fixed grid (poses
   vary in spacing row to row), and the sword is drawn fused into the body
   art on every attack frame. Full step-by-step for selecting each pose,
   splitting the sword out, and padding to a consistent 32×32/64×64 canvas
   is in [`05-character-sprite-extraction.md`](05-character-sprite-extraction.md)
   — use that instead of a grid-based import.
3. Confirm/reduce the sheet to <= 256 indexed colors (already indexed at 256
   per `identify`, but the *palette contents* still need curation — see
   Path 3 below for palette sharing with the level).
4. Slice into individual frame cels (body and, for attack frames, a paired
   sword cel), one export per cel, per doc 5's naming convention.
5. Export each cel as `.vera` (VSP1) once the LibreSprite build is patched
   per [`02-tool-setup.md`](02-tool-setup.md).

## Path 2 — Level graphic (Pixelorama + Tiled)

The image isn't uniformly tileable — measuring it directly shows the ground
band repeats cleanly but the sky/mountain band doesn't and blows the VRAM
tile budget if imported verbatim. Full analysis, numbers, and step-by-step
is in [`06-level-tileset-extraction.md`](06-level-tileset-extraction.md); use
that instead of a straight grid-slice. Summary:

1. Import `rastan-field-big.png` into Pixelorama for palette clean-up, pull
   out embedded object sprites baked into the strip (e.g. a dagger/javelin
   icon near x≈950–1050), and redesign the sky/mountain band as a small,
   genuinely repeating tile set instead of the ripped painted gradient.
2. Tile size: **16×16** — chosen by comparing tile-data + tilemap-entry cost
   against 8×8 on the actual asset, not just picking VERA's larger option by
   default. At 16px tiles: 8352 / 16 = **522 tile columns**, 448 / 16 =
   **28 tile rows**.
3. **Hardware ceiling check:** `MAP_WIDTH` maxes out at 256 tiles (see
   [`01-vera-asset-format.md`](01-vera-asset-format.md)), so 522 columns
   cannot be one map. Split into at least **3 screens** — e.g. by picking
   natural level-design boundaries (room transitions, elevation changes)
   near the ~174/~348 tile-column marks rather than a blind even cut, since
   those boundaries will later become the engine's room-load points (see
   [`04-engine-roadmap.md`](04-engine-roadmap.md)).
4. In Tiled, import the cleaned-up bands as a tileset source image (Tiled
   only grid-slices — it does not merge identical tiles, so deduplication
   happens in the VTS1 exporter, not in Tiled's editing UI), then build the
   3 map screens from that tileset.
5. Export each screen as a VTM1 + the shared VTS1, once the Tiled plugin
   from [`02-tool-setup.md`](02-tool-setup.md) exists (or via the Phase A
   external converter in the meantime).

## Path 3 — Shared palette

Both assets are independently 256-color indexed today, which almost
certainly means their palettes don't match. Before final export:
1. Pick one canonical VERA-legal palette (curated in Pixelorama).
2. Re-index both the character frames and the level tiles against it.
3. Use palette-offset banks (the 4-bit palette-offset field in tile/sprite
   attributes) if the character needs a different 16-color bank than the
   background, rather than maintaining two full 256-entry palettes.

This step is what makes a standalone `VPL1` palette file (see format spec)
worth having: one palette asset referenced by both the sprite and the level
tileset, instead of a redundant copy embedded in each `.vera`/`.vts` file.

## Definition of done for this pass

- [ ] Body/sword cels extracted and padded per `05-character-sprite-extraction.md`
- [ ] LibreSprite `.vera` export wired in and producing valid VSP1 files
- [ ] Level split into named screens with tile boundaries chosen by hand
- [ ] Tiled VTM1/VTS1 export producing files that respect the 256-tile ceiling
- [ ] Shared palette curated and both assets re-indexed against it
- [ ] At least one screen + the character idle frame verified rendering on
      the Atari + PBI VERA target in this repo
