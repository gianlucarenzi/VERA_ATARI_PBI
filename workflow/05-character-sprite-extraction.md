# Extracting the Main Character From the Spritesheet (LibreSprite)

Detailed, hands-on walkthrough for turning
`~/Downloads/Arcade - Rastan _ Rastan Saga - Playable Characters - Rastan.gif`
(800×2010) into a set of 32×32 / 64×64 VERA sprite cels for the hero, with
the sword kept as an independent sprite rather than baked into the body art.

## 0. What the sheet actually looks like

This is **not** a uniform grid — it's a loosely packed atlas (a typical fan
"rip" sheet), confirmed by opening it:

- Real alpha transparency is already present (`identify -verbose` reports a
  1-bit `PaletteAlpha` channel; the corner pixel is `srgba(0,0,0,0)`, fully
  transparent — the black you see in a flattened preview is just how
  transparent pixels render, not an opaque background color to key out).
- Poses are tightly and irregularly packed: spacing between frames varies
  row to row, so **`Import Sprite Sheet` (grid-based import) will not work
  here** — every pose has to be selected by hand.
- Rough content bands, top to bottom: idle/turn frames plus a handful of
  standalone diagonal sword-only glyphs near the top; several full walk
  cycles (left- and right-facing, no sword); several sword-swing/attack rows
  where the **blade is drawn fused into the body art** (hand+hilt+blade as
  one connected shape); crouch and crouch-attack rows; jump and jump-attack
  rows; and, in the last two rows, death/dissolve/burn sequences mixed with
  fire and particle pixels — treat those as effect sprites, out of scope for
  the "hero body/sword" set built here.
- Because a handful of frames already exist as **standalone sword glyphs**
  (top of the sheet), the original art itself supports treating the sword as
  an independent object — this extraction just makes that consistent across
  every attack frame too.

## 1. Set up the workspace

1. `File > Open` the GIF in LibreSprite. The canvas should show a
   checkerboard background (real transparency, not black) — if it renders
   solid black, don't key out black manually; re-check the alpha channel
   import instead, something is off.
2. Zoom in substantially (400–800%) — at native size the individual poses
   are small and easy to mis-select.
3. Enable the pixel grid (`View > Pixel Grid`) so you can eyeball 32px/64px
   boundaries while selecting.

## 2. Extract a sword-less pose (walk/idle/crouch/jump frames)

These are single, self-contained shapes — no separation needed.

1. Select the Rectangular Marquee tool (`M`).
2. Draw a selection tightly around one pose. It's fine to include a pixel or
   two of surrounding transparent margin, but don't overlap the neighboring
   pose — packing is tight in several rows.
3. `Edit > New Sprite from Selection` — this creates a brand-new document
   containing exactly that region (real command, confirmed in
   `data/languages/en.json`/`data/gui.xml`).
4. In the new document, run `Sprite > Trim` (or the `Trim` command) to
   auto-crop the transparent border down to the tight bounding box of the
   opaque pixels.
5. Note the trimmed size, then continue to **Step 4 (canvas padding)**
   before exporting.

## 3. Split a fused body+sword pose (attack/swing frames)

For any pose where the blade is drawn attached to the hand:

1. Select and `Edit > New Sprite from Selection` the **whole pose** (body
   and sword together) into a scratch document, same as Step 2.1–2.3, but
   **do not `Trim` yet** — keep the full original canvas/origin for now,
   that's what keeps the two halves in registration with each other.
2. Duplicate that scratch document twice (or duplicate the frame within it)
   — one copy becomes the body cel, the other becomes the sword cel.
3. **Body copy:** select just the blade+hilt pixels (Lasso or rectangular
   selection) and delete them, leaving transparency where the sword was.
   Touch up the hand/fist with a couple of pixels if the hilt was covering
   part of it in the original art.
4. **Sword copy:** select everything *except* the blade+hilt and delete it,
   leaving only the sword floating in the same canvas, at the same pixel
   coordinates it occupied in the fused pose.
5. Only now run `Trim` on each of the two copies independently, and record
   the offset each one moved by when trimmed (see Step 5, anchor metadata)
   — that offset is what lets the engine put the sword back in the correct
   place relative to the body at render time.

## 4. Pad every cel to a consistent target canvas

VERA sprite width/height only come in 8/16/32/64px steps, and every frame of
one animation needs to share the *same visual anchor* or the character will
visibly jitter as frames swap (VERA positions a sprite by its top-left
corner, so it has no idea where "the feet" are inside the cel — that has to
be consistent by construction).

1. Measure your trimmed cels across the whole walk/attack/jump set. If the
   tallest/widest body-only pose fits under 32px both ways, standardize the
   **body** cels at 32×32; otherwise use 64×64 for all of them — don't mix
   sizes within one animation set. Do the same measurement independently for
   the **sword** cels (a diagonal blade often needs less than 32px on a side
   even when the body needs 64×64).
2. On each trimmed cel, use `Sprite > Canvas Size`. Its anchor picker lets
   you choose which edge/corner the existing pixels stay pinned to as the
   canvas grows — pick the **same anchor for every cel in a set** (bottom-
   center is the natural choice for a standing character: feet stay on the
   bottom row, horizontally centered) so every frame lines up when swapped
   at runtime.
3. Re-check transparency after padding — the new canvas area must stay
   fully transparent (index/alpha 0), not filled with a background color.

## 5. Record the body↔sword anchor offset

Unlike the VRAM-mirroring binary formats in
[`01-vera-asset-format.md`](01-vera-asset-format.md), this is pure game-logic
data (Layer 2 in the [engine roadmap](04-engine-roadmap.md)) — it doesn't
need to match any VERA register layout, so a plain sidecar file is fine, e.g.
one JSON manifest per character:

```json
{
  "character": "rastan_hero",
  "frames": [
    { "name": "walk_r_00",     "body": "char_walk_r_00.vera",   "sword": null },
    { "name": "attack_r_00",   "body": "char_attack_r_00_body.vera",
      "sword": "char_attack_r_00_sword.vera", "sword_dx": 18, "sword_dy": -4 }
  ]
}
```

`sword_dx`/`sword_dy` are the offsets recorded in Step 3.5, measured from the
body cel's own trimmed+padded origin to the sword cel's trimmed+padded
origin. The runtime loader (Layer 1) positions the sword hardware sprite at
`body_x + sword_dx, body_y + sword_dy` each frame; sword-less frames simply
disable/hide the sword sprite (or set its Z-depth to 0, "sprite disabled",
per the VERA sprite attributes table).

## 6. Naming convention

Suggested pattern, matching the bands identified in Step 0:

```
char_idle_r_00.vera
char_walk_r_00.vera .. char_walk_r_05.vera      (mirror _l_ for left-facing)
char_crouch_r_00.vera
char_jump_r_00.vera
char_attack_r_00_body.vera  char_attack_r_00_sword.vera
char_attack_r_01_body.vera  char_attack_r_01_sword.vera
```

Keep body/sword pairs adjacent in the manifest and on disk — it makes the
Step 5 offsets much easier to audit later.

## 7. Export

Once each cel is its own LibreSprite document at the standardized padded
size, `File > Export As...` with the `.vera` extension (VSP1 — see
[`01-vera-asset-format.md`](01-vera-asset-format.md)) once the exporter is
wired into the build per [`02-tool-setup.md`](02-tool-setup.md). One file per
body cel, one file per sword cel — never a merged bitmap.

## Why keep the sword separate at all

- **Fewer unique frames:** fusing the sword into every body pose means every
  body pose × every sword angle needs its own cel. Splitting them means the
  body's walk/idle/crouch/jump set stays sword-free entirely, and only the
  attack set needs paired cels — a much smaller total sprite budget.
- **Independent motion:** a separate hardware sprite can be repositioned
  (and in later attack frames, potentially reused at different angles)
  without redrawing or re-exporting the body.
- **Matches the source material:** the sheet itself already contains a few
  sword-only glyphs near the top, so this isn't inventing a new asset split
  — it's making it consistent across the whole animation set.
