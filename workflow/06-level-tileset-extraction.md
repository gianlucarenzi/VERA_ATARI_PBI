# Extracting the Level Tileset From the Field Graphic (Pixelorama + Tiled)

Detailed, hands-on walkthrough for turning
`~/Downloads/RastanRIP/rastan-field-big.png` (8352×448, 133-color indexed, no
alpha) into VERA tileset/tilemap assets. Unlike the character sheet, this
image is a single continuous painting, not a set of discrete poses — the
work here is measuring what's actually tileable, then treating everything
else as a deliberate art/design decision instead of a blind export.

## 0. What the graphic actually contains

Measured directly on the file (autocorrelation + tile-hash scripts), not
eyeballed:

- **Ground band** (brown brick, roughly the bottom ~40% of the image):
  repeats with a **measured, exact (zero pixel-difference) period of 32×32
  px**, both horizontally and vertically. This is genuinely tileable art.
- **Pit/chasm band** (a darker scale-textured hole in the ground, e.g. around
  x≈880–1180): a second, separate repeating ground texture, same story as
  above but a different material.
- **Sky band** (dithered cloud gradient, top ~256px) and **mountain
  silhouette** (painted peaks overlapping the sky, roughly y≈150–260): these
  do **not** tile cleanly — periodicity search only finds a weak, inexact
  ~128px repeat in the sky (nonzero pixel difference, i.e. an approximate
  dither pattern, not a true repeat) and no usable period at all in the
  mountains (they're one continuous hand-painted skyline).
- **At least one embedded object sprite baked into the bitmap**: a small
  thrown dagger/javelin icon sits floating near the pit at roughly
  x≈950–1050px — this is game-object art (an item or projectile), not
  background tile material, and must be pulled out separately (same
  technique as the sword in
  [`05-character-sprite-extraction.md`](05-character-sprite-extraction.md)),
  not left in the tileset. Scan the rest of the strip for more of these
  before finalizing the tileset — arcade background rips routinely have
  stray item/enemy frames pasted into unused space.

## 1. Why a straight 1:1 import doesn't fit VRAM

Hashing the image at 16×16 granularity (8352/16 × 448/16 = 522 × 28 = 14,616
cells) finds **2,464 visually unique tiles** across the whole strip. At 4bpp
(128 bytes/tile) that's already **~308 KB of tile data alone** — more than
VERA's entire 128 KB VRAM, before palette, sprites, or the tilemap itself are
counted.

Splitting the count by band shows exactly where that cost comes from:

| Band split at y= | Sky+mountain unique tiles | Ground unique tiles |
|---:|---:|---:|
| 224px | 1,868 | 754 |
| 256px | 2,132 | 509 |
| 288px | 2,268 | 369 |

**85–92% of the total unique-tile count is the sky/mountain band alone** —
across the entire 8352px-wide level, the ground band only ever needs
**369–509 unique 16×16 tiles**, thanks to its clean 32px repeat. The
sky/mountain band is expensive precisely because it's continuous painted
art with per-pixel dithering: no two 16×16 windows are bit-identical, so
literal deduplication barely helps.

**Conclusion:** the ground/pit bands can be imported close to verbatim; the
sky/mountain band as-ripped cannot, and needs deliberate redesign (Step 3b),
not just a technical export step.

## 2. Tile size: 16×16, not 8×8

Both are valid VERA tile sizes; checked both against this asset, counting
tile data *and* tilemap entry cost together (map entries are 2 bytes each
regardless of tile size, so finer tiles mean more map entries):

| Tile size | Unique tiles (whole strip) | Tile data (4bpp) | Map entries | Map data | Total |
|---|---:|---:|---:|---:|---:|
| 16×16 | 2,464 | 308.0 KB | 14,616 | 28.5 KB | **336.6 KB** |
| 8×8 | 7,620 | 238.1 KB | 58,464 | 114.2 KB | 352.3 KB |

16×16 wins once both costs are counted (and matches the ground texture's
natural 32px = 2×16px repeat unit). Use 16×16 for this level, per
[`01-vera-asset-format.md`](01-vera-asset-format.md)'s VTS1/VTM1 spec — these
totals still assume the naive sky/mountain import, which Step 3b brings
down dramatically.

## 3. Pixelorama pass

1. Import `rastan-field-big.png`.
2. **Curate the shared palette** — see
   [`03-rastan-case-study.md`](03-rastan-case-study.md) Path 3. This image and
   the character sheet must end up on one VERA-legal palette before final
   export.
3. **Pull out embedded object sprites.** Locate the dagger/javelin icon
   (≈x950–1050) and any others found by scanning the full strip. Select each
   tightly, `Edit > New Sprite from Selection` (same LibreSprite-family
   command used in doc 5 — Pixelorama has an equivalent selection-to-new-
   image flow), export it through the sprite pipeline instead of the tileset
   pipeline, then paint over the now-empty spot in the level image with the
   surrounding tile pattern so it doesn't leave a one-off "unique tile" hole
   in the ground/pit band.
4. **Redesign the sky/mountain band.** Don't try to dedupe your way out of
   Step 1's numbers — replace the ripped gradient with a small, deliberately
   repeating tile set instead:
   - A short vertical strip of hand-authored 16×16 cloud/gradient tiles (a
     handful of tiles, tiled horizontally) rather than one continuous
     dithered painting.
   - Reduce the mountain silhouette to a small library of reusable
     peak/valley/base pieces placed like tiles, instead of one continuous
     hand-painted skyline.
   - If parallax motion is wanted, this is also the natural place to use
     VERA's second layer (independent H-scroll) for the sky, scrolling
     slower than the ground layer — a reason to keep sky and ground as
     separate tilesets/layers from the start rather than one merged image.
   This is an art-direction decision, not a mechanical export step — flag it
   explicitly as a task for whoever owns the pixel art, separate from the
   tooling work in this document.
5. Leave the ground/pit bands close to as-ripped (after palette unification)
   — they're already efficiently tileable.

## 4. Tiled pass

1. `File > New > New Tileset...`, choosing the **"Based on Tileset Image"**
   source (the real option in Tiled's New Tileset dialog — confirmed in
   `src/tiled/newtilesetdialog.cpp`), pointing at the cleaned-up ground/pit
   band image (and, once redesigned, the small hand-authored sky tile
   image), tile size **16×16**.
2. **Important:** this only slices the source image into one tileset entry
   per grid cell — it does **not** merge pixel-identical tiles. Checked in
   Tiled's own source: there is no duplicate-tile-detection/merge feature in
   the tileset editor (`tileseteditor.cpp`'s only "duplicate" logic is for
   Wang sets, unrelated). Actual deduplication happens later, in the VTS1
   exporter sketched in [`02-tool-setup.md`](02-tool-setup.md) — it should
   hash each tile's pixel data while writing the tileset, emit only unique
   tiles, and remap the tilemap's tile indices accordingly. Don't expect (or
   try to force) Tiled's editing UI to do this for you.
3. Paint the level using this tileset across the map layer(s) — one tile
   layer for ground/pit, a separate tile layer for the (redesigned) sky, so
   the future VTM1 export and the two-layer VRAM split described in Step 3
   line up naturally.
4. **Segment into screens.** The hardware `MAP_WIDTH` ceiling is 256 tiles
   (see [`01-vera-asset-format.md`](01-vera-asset-format.md)), so 522 tile
   columns must become at least 3 map screens. An even 3-way split lands
   segment boundaries at tile-columns 174 and 348 (x = 2784px, 5568px) — but
   since the dagger landmark sits around tile-column 59–65 (x≈950–1050,
   inside segment 0 either way), double-check no chosen boundary lands mid-
   landmark, and prefer picking exact boundaries at natural level-design
   beats (room transitions, elevation changes) over a blind even cut, per
   the case study.
5. Export each screen as VTM1 + the shared VTS1 once the Tiled plugin from
   [`02-tool-setup.md`](02-tool-setup.md) exists.

## Definition of done for this pass

- [ ] Embedded object sprites (dagger icon, any others found) extracted and
      routed through the sprite pipeline, not the tileset
- [ ] Sky/mountain band redesigned as a small, genuinely repeating tile set
      (or explicitly deferred with the VRAM cost understood)
- [ ] Shared palette curated across level + character (links to case study)
- [ ] Tiled tileset built at 16×16 from the cleaned-up bands
- [ ] Level laid out across >=3 map screens, boundaries chosen by hand
- [ ] VTS1/VTM1 exporter (Tiled plugin or Phase A script) produces
      deduplicated tile data, not one entry per grid cell
- [ ] At least one screen verified rendering on the Atari + PBI VERA target
