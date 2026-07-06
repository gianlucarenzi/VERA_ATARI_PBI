# Rastan-on-VERA Porting Workflow

This folder documents the asset pipeline used to port **Rastan** (Taito, 1987)
to the VERA video chip as integrated by this repository, and the roadmap for
turning that pipeline into a portable, data-driven game engine — in the spirit
of **Scorpion Engine** / **RedPill** — that targets several 6502-family hosts
sharing the same VERA hardware.

## Documents

| # | Document | Covers |
|---|----------|--------|
| 1 | [`01-vera-asset-format.md`](01-vera-asset-format.md) | Binary asset formats (sprite, tileset, tilemap, palette) that mirror VERA's VRAM layout |
| 2 | [`02-tool-setup.md`](02-tool-setup.md) | What to change in Pixelorama, LibreSprite and Tiled to produce those formats |
| 3 | [`03-rastan-case-study.md`](03-rastan-case-study.md) | Concrete step-by-step for the actual downloaded Rastan assets |
| 4 | [`04-engine-roadmap.md`](04-engine-roadmap.md) | Multi-target engine architecture (X16, Apple IIe, C64, Atari XL/XE) |
| 5 | [`05-character-sprite-extraction.md`](05-character-sprite-extraction.md) | Hands-on LibreSprite walkthrough: cutting the hero out of the spritesheet, splitting the sword into its own sprite |
| 6 | [`06-level-tileset-extraction.md`](06-level-tileset-extraction.md) | Hands-on Pixelorama + Tiled walkthrough: measuring what's tileable in the level graphic, sizing the tileset, segmenting into screens |

## Tool sources (local checkouts)

| Tool | Path | Role | Current state |
|------|------|------|----------------|
| LibreSprite | `~/Progetti-CVS/libresprite` | Sprite/character editing, VERA sprite export | `vera_format.cpp` already committed (`23b359805`), **not yet wired into the build** |
| Pixelorama | `~/Progetti-CVS/Pixelorama` | Pixel art editing, palette work, tileset PNG export | Pristine upstream, no VERA-specific changes yet |
| Tiled | `~/Progetti-CVS/tiled` | Level/tilemap layout | Pristine upstream, no VERA-specific changes yet |

## Source assets for the case study

| Asset | Path | Dimensions | Notes |
|-------|------|-----------|-------|
| Character spritesheet (provisional) | `~/Downloads/Arcade - Rastan _ Rastan Saga - Playable Characters - Rastan.gif` | 800×2010, 256-color indexed | Placeholder art, frame grid not yet measured |
| Level strip | `~/Downloads/RastanRIP/rastan-field-big.png` | 8352×448, 256-color indexed | Single continuous horizontal background |

> **Note on `~/Downloads/RastanRIP/*.py`:** `vera_converter.py`, `vera_compose.py`
> and `preview_strips.py` in that folder predate this workflow and pull data
> directly from arcade ROM dumps. Per project decision they are kept only as
> loose reference material — do **not** assume they work or that their output
> format matches what is defined in `01-vera-asset-format.md`.

## Scope note

Steps 1–3 (asset pipeline) target this repository's existing Atari 8-bit +
PBI VERA implementation and can be validated today. Step 4 (portable engine)
is a roadmap: no C64/Apple IIe VERA hardware or HAL code exists yet.
