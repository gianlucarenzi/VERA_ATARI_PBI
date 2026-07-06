# Tool Setup: Wiring VERA Export Into Pixelorama / LibreSprite / Tiled

Each tool plays one role in the pipeline. This document lists what already
exists, what's missing, and the concrete files to touch. Format details are
in [`01-vera-asset-format.md`](01-vera-asset-format.md).

## Recommended phasing

- **Phase A (get Rastan on screen fast):** edit/slice art in the tools as-is,
  export plain PNG/tileset PNG, run a small external converter script to
  produce the final `.vera`/`.vts`/`.vtm` binaries. No engine changes needed.
- **Phase B (native export):** finish wiring the LibreSprite `.vera` exporter
  and write the Pixelorama/Tiled counterparts, so binaries come straight out
  of the editors.

Phase A unblocks the case study immediately; Phase B is the durable path and
is what's described below.

## LibreSprite — sprite export (`~/Progetti-CVS/libresprite`)

Role: edit the character spritesheet, slice frames, export each frame as a
VSP1 sprite.

**Status:** `src/app/file/vera_format.cpp` already implements VSP1 `onSave`
(commit `23b359805`). It is a self-registering `FileFormat::Regular<VeraFormat>`,
same pattern as every other format in `src/app/file/` (e.g. `png_format.cpp`,
`bmp_format.cpp`).

**To wire it in:**
1. Check how existing formats reach the build — `src/app/file/file_formats_manager.cpp`
   registers formats by including their headers/translation units; confirm
   `vera_format.cpp` is compiled at all by grepping `CMakeLists.txt` for the
   `file/` source list (the file currently isn't listed there, that's why the
   `.vera` extension doesn't show up in the export dialog).
2. Add `app/file/vera_format.cpp` next to the other `app/file/*_format.cpp`
   entries in `CMakeLists.txt`'s source list.
3. Rebuild (`libresprite/INSTALL.md` has the CMake build steps) and confirm
   `File > Export As...` offers a `.vera` extension for an indexed-color
   sprite.
4. `onLoad` currently returns `false` (export-only) — that's fine for this
   pipeline; round-tripping isn't needed.

## Pixelorama — tileset/palette editing (`~/Progetti-CVS/Pixelorama`)

Role: reduce/curate the shared palette, edit the level tileset once sliced,
optionally export tilesets as indexed PNG for Tiled to consume.

**Status:** pristine upstream (Godot/GDScript project); no VERA-aware export
exists.

Pixelorama's export path runs through `src/Autoload/Export.gd` and
`src/UI/Dialogs/ExportDialog.gd`, with dedicated exporter scripts under
`src/Classes/AnimationExporters/` (e.g. `GIFAnimationExporter.gd`) as the
model to follow for a new exporter class.

**Two viable approaches, in order of effort:**
- *Phase A:* don't touch Pixelorama's code — use it purely for pixel editing
  and palette curation, then export a plain indexed PNG tileset/spritesheet
  and run the same external converter used for Phase A above.
- *Phase B:* add a `VeraExporter` script beside `GIFAnimationExporter.gd`,
  hook it into `ExportDialog.gd`'s format list the same way the GIF exporter
  is registered, and have it emit VSP1 (for a single sprite) directly. Because
  GDScript can't easily be shared with LibreSprite/Tiled, keep the actual
  byte-packing logic in one small shared Python module and call it as an
  external tool from the GDScript exporter, rather than reimplementing the
  packer three times.

## Tiled — level layout (`~/Progetti-CVS/tiled`)

Role: slice the level strip into a tileset, deduplicate repeated tiles, lay
out the tilemap, export VTM1 (+ VTS1).

**Status:** pristine upstream; no VERA plugin exists. Tiled's plugin
architecture (`src/plugins/<name>/`) is exactly built for this: each plugin
implements `Tiled::WritableMapFormat` and is a Qt plugin discovered at
runtime. The `csv` plugin (`src/plugins/csv/`) is the simplest reference:

```
src/plugins/csv/
  plugin.json        # { "defaultEnable": true }
  csv_global.h        # export macros
  csvplugin.h          # class CsvPlugin : public Tiled::WritableMapFormat
  csvplugin.cpp        # write(), outputFiles(), nameFilter(), shortName()
  csv.qbs              # build description
```

**To add a `vera` plugin:**
1. Copy the `csv` plugin skeleton into `src/plugins/vera/`, rename the class
   (`VeraPlugin`), and register it in the plugins build list (wherever `csv`
   is listed — check `src/plugins/plugins.qbs` and the top-level Tiled build
   file for the plugin subdirectory list).
2. In `write()`, replace CSV's per-cell text loop with:
   - one pass over `map->tilesets()` to emit a VTS1 (reusing the loaded tile
     images and palette),
   - one pass per tile layer to emit VTM1 map entries using the exact bit
     layout from `01-vera-asset-format.md` (tile index low/high bits,
     H/V-flip bits, which Tiled already tracks per cell via
     `FlippedHorizontallyFlag`/`FlippedVerticallyFlag`, as seen in
     `csvplugin.cpp`).
   - enforce the `MAP_WIDTH`/`MAP_HEIGHT` <= 256-tile hardware ceiling,
     erroring out (like `CsvPlugin::write` does for file I/O errors) if a
     layer is too wide, so oversized levels are caught in Tiled rather than
     at load time on target hardware.
3. `outputFiles()` should return two paths per exported layer/tileset pair
   (`*.vtm` + `*.vts`), mirroring how `CsvPlugin::outputFiles` returns one
   path per tile layer.

## Shared converter (Phase A stopgap)

A single small Python script (outside these three tools) that takes an
indexed PNG + a JSON tile-grid description and emits VSP1/VTS1/VTM1 exactly
as specified is the fastest way to unblock the Rastan case study before any
editor plugin exists. It should live under `workflow/tools/` in this repo
once written, and its packing logic is what Phase B's native exporters
should eventually converge on, so the on-disk format never drifts between
the "external script" path and the "native plugin" path.
