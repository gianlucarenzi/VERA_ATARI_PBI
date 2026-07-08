# From a VGM/VGZ rip to a playing VU-meter demo on Atari

End-to-end pipeline: take a VGM (or gzipped VGZ) log of an AY-3-8910/YM2149
PSG chip (MSX and many other 8-bit machines use one), turn it into a VTM
module, and get it running — VU meter included — on `atari800` or real
Atari hardware via the VERA PBI card. Every tool named below lives under
`vera-tests/`; see `vtm_format.md` for the binary format itself.

```
song.vgm/.vgz --[vgm2vtms.py]--> song.vtms --[vtm_compile.py]--> DEMO.VTM
                                     |
                                     +--[vtm_play.c, optional]--> PC audio preview
                                                                        |
DOS.SYS + DUP.SYS + TESTPLR.COM + DEMO.VTM --[dir2atr]--> disk.atr --> atari800 / real HW
```

`TESTPLR.COM` (built from `test_player.c`) already contains the 4-channel
Player/Missile VU meter and the title-banner screen — there is no separate
"enable VU meter" step, it runs automatically whenever the program plays a
song. Optionally pair it with a `DEMO.VBM` picture (step 8) shown on
VERA's own video output while the song plays, and/or rename it to
`AUTORUN.SYS` for a disk that boots straight into the demo with no typing
— see "Complete example" near the end for both together in one 130K disk.

## Worked example: Bubble Bobble (MSX intro + main theme)

`vera-tests/songs/examples/` has two real VGZ rips (MSX AY-3-8910, from
Bubble Bobble) checked in as a concrete, always-available example of every
step below:

- `bubblebobble-01-intro.vgz` — the short intro jingle, plays once
- `bubblebobble-02-maintheme.vgz` — the looping main theme

They're meant to be converted together, intro-then-loop, exactly like
`vgm2vtms.py`'s multi-file mode was designed for. Pass `--pal` since that's
what the worked boot command in step 7 uses (see the NTSC/PAL note in
step 2 — it also makes the file noticeably smaller, which matters below):

```sh
cd vera-tests
python3 tools/vgm2vtms.py songs/examples/bubblebobble-01-intro.vgz \
                           songs/examples/bubblebobble-02-maintheme.vgz \
                           songs/bubblebobble.vtms --pal
```
```
songs/bubblebobble.vtms: 2728 rows across 12 pattern(s), 50 rows/sec, from 2 input file(s)
```

```sh
python3 tools/vtm_compile.py songs/bubblebobble.vtms songs/DEMO.VTM
```
```
songs/DEMO.VTM: 21955 bytes, 12 pattern(s), 15 instrument(s), 12 order entries
```

That's a real, playable `DEMO.VTM` — drop it on a disk per step 6 below
(or `make TESTPLR.COM` once and reuse it) and it boots with the title
"Introduction + Main Theme" centered in reverse video, a loading progress
bar underneath, then VU meters live, intro playing once into the looping
theme. `bubblebobble.vtms` itself is gitignored (regenerate it with the
command above) — only the two source `.vgz` files are checked in.

This example is also the one that shaped several details elsewhere in this
guide: at ~22KB it's big enough to bump into real free-RAM limits on a
64K Atari (see step 5's load address and the loader notes in
Troubleshooting) and slow enough to load that the progress bar is worth
having.

## 1. Get a source VGM/VGZ

Only AY-3-8910/YM2149 register writes (VGM command `0xA0`) are decoded.
MSX rips are the most common source. If a rip drives its melody through
another chip in the same log (YM2151 FM, SN76489, Konami SCC, ...), that
part is skipped over safely but not converted — check the result actually
has notes before assuming the conversion "worked".

## 2. Convert to `.vtms` tracker source

```sh
python3 vera-tests/tools/vgm2vtms.py song.vgm song.vtms
python3 vera-tests/tools/vgm2vtms.py song.vgz song.vtms          # gzip ok too
python3 vera-tests/tools/vgm2vtms.py intro.vgm theme.vgm out.vtms  # concatenate
```

Notes:
- Add `--pal` if the *target machine* runs PAL (50 Hz VBI) — omit it for
  NTSC (60 Hz, the default). One output row = one VBI frame, so this choice
  must match whatever `-pal`/no-`-pal` you pass to `atari800` later, or the
  tempo will be wrong on playback (the row rate is baked into the pattern
  data, not stored as a "BPM" the player can adjust).
- When given multiple files (e.g. an intro + a looping main theme), only
  the *last* file's loop point becomes the final `LOOP` target; earlier
  files play once, straight through.
- Long songs are automatically split into multiple `PATTERN` blocks (255
  rows/pattern max) chained by `ORDER`.
- Volume and pitch are quantised (AY's 0-15 volume to VERA's 0-63, AY's
  arbitrary Hz to the nearest of VERA's 96 equal-tempered notes) — expect
  a close but not sample-exact match to the original.
- The `.vtms` output gets a `TITLE "..."` line auto-filled from the VGM's
  GD3 tag (or the input filename if there's no tag) — this is what ends up
  centered on the Atari screen in reverse video.

Open the `.vtms` in a text editor if you want to hand-tweak anything
(instrument decay, a wrong note, `TEMPO`, `LOOP`) — it's the same plain
tracker-source language documented in `vtm_format.md` that hand-authored
songs use.

## 3. Compile to the binary `.vtm`

```sh
python3 vera-tests/tools/vtm_compile.py song.vtms DEMO.VTM
```

The player (`vtm_player.s`, loaded via `vtm_loader.c`) always opens the
file `D1:DEMO.VTM` — that name is hardcoded in `test_player.c`. Name the
compiled output `DEMO.VTM` (or edit `test_player.c`'s `vtm_load_file(...)`
call and rebuild `TESTPLR.COM` if you want a different name/drive).

## 4. Optional: preview on the PC before touching the emulator

```sh
cc -O2 -o vtm_play vera-tests/tools/vtm_play.c $(sdl2-config --cflags --libs) -lSDL2_image -lm
./vtm_play DEMO.VTM                             # Ctrl+C to stop
./vtm_play DEMO.VTM --pal                       # tick at 50 Hz instead of 60 Hz
./vtm_play DEMO.VTM --image cover.png           # + artwork and a VU meter in a window
./vtm_play DEMO.VTM --wav out.wav --seconds 8   # render to a WAV file instead
```

This runs the *exact same* row/pattern/envelope logic as `vtm_player.s`,
just with textbook-shape waveforms instead of real VERA hardware quirks
(pulse-width XOR into saw/triangle, LFSR noise timing) — good enough to
catch a wrong note or bad tempo before doing a much slower
emulator/hardware round trip. It is not a substitute for a final listen on
`atari800` or real hardware.

`--image` loads the artwork straight through SDL2_image — **no
conversion step, unlike the Atari side** (`img2vbm.py`/`.vbm`, step 8):
any PNG/JPEG/BMP/GIF works as-is, since the PC has no 256-color-palette
hardware constraint to work around. A `.vbm` file itself is *not* a valid
`--image` argument — it's a raw VRAM dump (header + RGB444 palette +
indexed pixels), not a format SDL2_image understands; always point
`--image` at the original source picture, before `img2vbm.py` ever
touches it. The window also grows a 128px-tall, 4-channel VU meter below
the artwork (green-to-red bars, same idea as `vu_pm.s`'s Player/Missile
meter on the Atari side) — one PC window standing in for the Atari's own
screen (VU meter) and VERA's own separate video output (artwork), which
on real hardware are two different monitors.

### Verification checklist

A fast, no-emulator sanity check that the whole audio+artwork pipeline
behaves before spending time on the slower Atari/disk-image round trip.
Using the checked-in Bubble Bobble example end to end:

1. **Pick a VGM/VGZ.** `vera-tests/songs/examples/bubblebobble-01-intro.vgz`
   and `...-02-maintheme.vgz` are already in the repo — any other
   AY-3-8910/YM2149 rip works the same way (step 1).
2. **Convert it** (step 2 + step 3):
   ```sh
   python3 vera-tests/tools/vgm2vtms.py \
       vera-tests/songs/examples/bubblebobble-01-intro.vgz \
       vera-tests/songs/examples/bubblebobble-02-maintheme.vgz \
       /tmp/check.vtms --pal
   python3 vera-tests/tools/vtm_compile.py /tmp/check.vtms /tmp/check.vtm
   ```
   Confirm: both commands print a size/row/pattern summary line and exit
   0 — no traceback, no "line N: ..." syntax error.
3. **Pick a bitmap.** `vera-tests/pictures/bubble-bob.png` is already in
   the repo. Any PNG/JPEG/BMP/GIF works — **no conversion needed for this
   PC check** (that's an Atari-only step, see step 8); pass it straight
   to `--image` below.
4. **Play both together:**
   ```sh
   cc -O2 -o /tmp/vtm_play vera-tests/tools/vtm_play.c \
       $(sdl2-config --cflags --libs) -lSDL2_image -lm
   /tmp/vtm_play /tmp/check.vtm --pal --image vera-tests/pictures/bubble-bob.png
   ```
   Confirm, in order:
   - the terminal prints `Playing <title> (...) — close the window or
     Ctrl+C to stop.` (the title comes from the `.vtm`'s embedded `TITLE`)
   - a window opens showing the artwork centered in the top 320x240 area
     on a black background
   - audio plays through the PC's speakers, matching the artwork's song
   - up to 4 vertical bars animate in the 320x128 strip below the artwork,
     rising and falling with each channel's volume, never fully
     disappearing once a channel has sounded (a small green floor stays)
   - closing the window (or Ctrl+C in the terminal) exits cleanly, no
     hang, no leftover process
5. If all five hold, the `.vtm`/artwork pair is good to carry through
   steps 5-8 onto an actual Atari disk — any problem specific to *real*
   hardware from here on (RAM budget, disk density, VTOC quirks) is
   covered in Troubleshooting, not in the audio/artwork data itself.

## 5. Build `TESTPLR.COM`

```sh
make TESTPLR.COM
```

This links `test_player.c` + `vtm_loader.c` + `vtm_player.o` + `vu_pm.o`
against `vera-tests/atari_nosyschk.cfg` (a low-memory-footprint linker
config, load address `$2500`, no VERA.SYS needed — the program pokes PSG
registers directly). The VU meter (`vu_pm.s`) is always linked in; nothing
to toggle.

While loading, the title appears first (as soon as it's readable — it's
near the start of the file), then a one-row bar of reverse-video blocks
underneath fills in left to right as the rest of the file streams in
(`vtm_loader.c`'s `vtm_progress_cb`, wired up in `test_player.c`'s
`load_progress()`). For a small hand-authored song this flashes past in a
fraction of a second; for a multi-minute VGM conversion like the worked
example above it's genuinely useful feedback that the machine hasn't
hung.

If you just want to hear your own song without rebuilding the executable,
point the Makefile's demo-song variable at your file and let it (re)compile
`DEMO.VTM` for you:

```sh
make DEMO_SONG_SRC=path/to/song.vtms vera-tests/songs/DEMO.VTM
```

## 6. Put it on a bootable disk

Minimal disk — just DOS + the player + the song, no other test programs:

```sh
mkdir -p /tmp/mydisk
cp .dos20/DOS.SYS .dos20/DUP.SYS TESTPLR.COM DEMO.VTM /tmp/mydisk/
dir2atr -E -b Dos20 /tmp/mydisk.atr /tmp/mydisk
```

**`-E` (Enhanced Density, 130K) matters** — DOS 2.0S expects single/enhanced
density sectors; a `-D` (Double Density) image boots into a continuous
reset loop instead of the DUP.SYS menu. Match whatever density the rest of
the project's `.atr` files use (`grep dir2atr Makefile` — currently `-E`
everywhere).

Alternatively, to bundle the song into the project's full multi-program
test disk (rebuilds `TESTPLR.COM`, `DEMO.VTM` and everything else on that
disk):

```sh
make DEMO_SONG_SRC=path/to/song.vtms disk2-veratests-80x60.atr
```

## 7. Boot it

```sh
atari800 -verax16 -verax16-rom /path/to/vera_pbi_handler.rom \
          -pal -volume 100 -xe /tmp/mydisk.atr
```

- `-volume 100` matters — VERA PSG output through the PBI bridge is quiet
  at the emulator's default mixer level.
- Match `-pal`/omit it to whatever frame rate you converted the song for
  in step 2.
- At the DOS 2.0S `D1:` prompt (after DUP.SYS's menu loads), type
  `TESTPLR` and press Return.

On start, `TESTPLR.COM` clears the screen, prints the song's embedded
title centered in reverse video, then plays — the 4 vertical VU-meter bars
(one per channel, P/M graphics + a Display List Interrupt, see `vu_pm.s`)
animate live under the title without touching the text screen itself.
Press any key to stop and return to DOS.

## 8. Optional: show artwork on VERA's own screen

VERA drives its own separate video output (a second monitor/window,
nothing to do with the Atari's own ANTIC/GTIA display used above) —
`test_player.c` will show a picture there, centered on black, for as long
as the song plays, if `D1:DEMO.VBM` is present alongside `D1:DEMO.VTM`.
No `.VBM` file means no change to VERA's display at all; this step is
entirely optional and independent of everything above.

```sh
python3 vera-tests/tools/img2vbm.py cover.png vera-tests/songs/DEMO.VBM
```
```
vera-tests/songs/DEMO.VBM: 77334 bytes (320x240, 8bpp)
```

Or, to pick the source image the same way step 5 lets you pick the song:

```sh
make DEMO_IMAGE_SRC=path/to/cover.png vera-tests/songs/DEMO.VBM
```

Any Pillow-readable format works (PNG/JPEG/BMP/GIF/...). The image is
scaled down to fit within 320x240 if it's larger in either dimension
(never scaled up), centered, and letterboxed in black — the output file
is always exactly 320x240, 8bpp, regardless of the source's own
dimensions or aspect ratio. Its byte size is therefore always the same,
give or take the embedded name (see next paragraph): `12 + name_len + 512
+ 76800`, i.e. **77324 + name_len bytes** — `img2vbm.py` prints the exact
total every time, so there's never a need to compute it by hand.

The file also carries a short name (same idea as `DEMO.VTM`'s `TITLE`) —
defaults to the source image's filename, or pass one explicitly:

```sh
python3 vera-tests/tools/img2vbm.py cover.png DEMO.VBM "Bubble Bobble"
```

`test_player.c` prints this name centered in reverse video on the Atari's
own screen (same treatment as the song title) as soon as it's read, then
shows a loading progress bar underneath while the rest of the image
streams into VRAM — the image itself never touches Atari main RAM (see
`vbm_loader.c`: it streams straight to VRAM in 128-byte chunks), only
VERA's own video memory, so it doesn't compete with `DEMO.VTM`'s own RAM
budget covered in the Troubleshooting section below.

## Complete example: a self-booting disk with audio *and* artwork in 130K

Putting everything together — the Bubble Bobble audio example from above,
its cover art (`vera-tests/pictures/bubble-bob.png`), and a disk that
boots straight into the demo with no typing required — while still
fitting the whole thing on a single 130K disk.

**Rename `TESTPLR.COM` to `AUTORUN.SYS`** and DOS 2.0 runs it automatically
right after `DOS.SYS` loads, with no `DUP.SYS` menu step at all — which
also means `DUP.SYS` itself doesn't need to be on the disk (it's only
ever loaded when *no* `AUTORUN.SYS` is present, or once a running program
exits back to it), reclaiming the ~41 sectors it would otherwise cost.
That headroom matters: this combination is large enough to need it.

```sh
# audio: intro + looping main theme, PAL row rate
python3 vera-tests/tools/vgm2vtms.py vera-tests/songs/examples/bubblebobble-01-intro.vgz \
                                       vera-tests/songs/examples/bubblebobble-02-maintheme.vgz \
                                       /tmp/bb.vtms --pal
python3 vera-tests/tools/vtm_compile.py /tmp/bb.vtms /tmp/DEMO.VTM

# artwork: already 320x240, so img2vbm.py only needs to quantize/pack it
python3 vera-tests/tools/img2vbm.py vera-tests/pictures/bubble-bob.png /tmp/DEMO.VBM

# build the player and stage the disk contents
make TESTPLR.COM
mkdir -p /tmp/bbdisk
cp .dos20/DOS.SYS /tmp/bbdisk/
cp TESTPLR.COM /tmp/bbdisk/AUTORUN.SYS
cp /tmp/DEMO.VTM /tmp/DEMO.VBM /tmp/bbdisk/

# build the disk, then repair dir2atr's large-disk bugs (see Troubleshooting)
dir2atr -E -b Dos20 /tmp/bbdisk.atr /tmp/bbdisk
python3 vera-tests/tools/fix_atr_vtoc.py /tmp/bbdisk.atr
```
```
fixing directory entry flags for 'DEMO.VBM': 0x03 -> 0x42
fixing directory entry flags for 'DEMO.VTM': 0x03 -> 0x42
fixing directory entry flags for 'DOS.SYS': 0x03 -> 0x42
patching boot sector's DOS.SYS pointer: sector 906 -> 905
/tmp/bbdisk.atr: VTOC fixed — 0 free sectors (of 944 trackable, 944 used)
```

```sh
atari800 -verax16 -verax16-rom /path/to/vera_pbi_handler.rom \
          -pal -volume 100 -xe /tmp/bbdisk.atr
```

Boots straight into the demo: title + progress bar + VU meters on the
Atari's own screen, the cover art on VERA's, no keypresses needed.
`0 free sectors` here is a correct, accurate statement (not the dir2atr
bug — see Troubleshooting) — this particular combination fills a 130K
disk essentially exactly.

### How big can the audio file be?

The image's size is fixed and self-reported (previous section); DOS.SYS
is a constant ~4875 bytes; `AUTORUN.SYS`'s size moves a little as the
player's code changes. That leaves the audio file as the one size that
varies per-song and needs a budget:

```
944 total sector numbers a single VTOC can ever represent (0-943)
- 13 reserved (sector 0 itself + boot sectors 1-3 + VTOC sector 360 + directory 361-368)
= 931 sectors available for actual file data

available_for_audio_sectors = 931 - ceil(DOS.SYS_bytes / 125)
                                   - ceil(AUTORUN.SYS_bytes / 125)
                                   - ceil(DEMO.VBM_bytes / 125)

max_audio_bytes = available_for_audio_sectors * 125
```

Each sector holds 125 usable data bytes (128 minus 3 link/bookkeeping
bytes) — this is DOS 2.0's on-disk format, not something any of this
project's tools choose.

With this example's actual sizes (`DOS.SYS`=4875B/39 sectors,
`AUTORUN.SYS`=12005B/97 sectors, `DEMO.VBM`=77334B/619 sectors):

```
931 - 39 - 97 - 619 = 176 sectors available
176 * 125 = 22000 bytes maximum for DEMO.VTM
```

The actual `DEMO.VTM` above is 21955 bytes — comfortably inside that
budget (176 sectors either way, since 21955 and 22000 both round up to
176 × 125-byte sectors; there just happens to be no 177th sector free to
spare). If a bigger conversion doesn't fit, re-run `vgm2vtms.py` with
`--pal` (fewer rows/sec, see step 2) or trim the source VGM — the image
can't be shrunk to make room, its size is always 320x240x8bpp regardless
of content.

If *no image* is on the disk at all, skip the `DEMO.VBM` term entirely —
`931 - dos_sectors - autorun_sectors` sectors are available for audio
alone, which is why a 22KB `DEMO.VTM` fit comfortably on its own in the
step 6 disk earlier in this guide, without needing any of this section's
`AUTORUN.SYS`/space-budget machinery at all.

## Troubleshooting

- **Disk boots into a reset loop**: density mismatch — rebuild with
  `dir2atr -E` (see step 6), not `-D`.
- **`DIR` shows "000 FREE SECTORS" even though the disk clearly isn't
  full, or a file that's definitely on the disk doesn't show up in `DIR`
  at all**: `dir2atr` (AtariSIO) has a real bug on Enhanced Density disks
  once the files use enough sectors to approach the ~720-943 range a
  single VTOC sector can represent — it also corrupts the flags byte on
  one or more directory entries. `tools/fix_atr_vtoc.py disk.atr` re-
  derives the correct picture from the files' actual sector chains and
  patches the disk in place; run it after every `dir2atr` invocation on a
  disk with a `DEMO.VBM` on it (small disks without one rarely hit this).
- **Emulator/hardware locks up or resets immediately on boot, before
  anything from the program ever appears** (as opposed to a normal DOS
  2.0 reset loop from a density mismatch, above): if the disk uses
  `AUTORUN.SYS` and was patched by `fix_atr_vtoc.py`, make sure you're
  running the version of that script that patches boot sector 1's
  DOS.SYS pointer (byte offset 15) — it needs updating whenever `DOS.SYS`
  itself gets relocated to close the wasted-sector gap, since the boot
  process reads that raw sector number directly, before it's able to look
  anything up by name. An older copy of the script that only fixed the
  VTOC/flags would build a disk that looks fine on paper but crashes at
  the earliest possible moment.
- **Whole song plays but sounds too quiet**: pass `-volume 100` to
  `atari800`, and/or check the `.vtms` instruments have `VOL=63`.
- **A whole pattern/section is silent, in isolation it "shouldn't be"**:
  this exact symptom was once caused by a P/M graphics memory block
  (`PMGFX` segment) that the cc65 heap didn't know about, so `malloc()`
  handed out song-buffer memory that overlapped it — fixed by linker
  segment ordering in `atari_nosyschk.cfg` (`PMGFX` must stay linked
  *before* `BSS`, since cc65's heap starts right after whatever segment is
  literally named `BSS`). If you ever add another `align`-ed static BSS
  segment to that config, put it before `BSS` too, or it'll silently
  become "free" heap memory.
- **`could not load D1:DEMO.VTM`**: check the file is actually named
  `DEMO.VTM` on the disk (case matters to `dir2atr`/DOS 2.0S 8.3 names)
  and that it compiled (`vtm_compile.py`'s output line reports size/pattern
  count — an empty or truncated file usually means the `.vtms` source had
  a syntax error caught earlier, or wasn't the file you thought you edited).
  It can also mean genuine RAM exhaustion for a large VGM-converted song —
  `vtm_loader.c` sizes its buffer exactly from the file's own header/
  pattern_table (see vtm_format.md), so there's no guessing overhead left
  to trim, but a 64K Atari with `TESTPLR.COM` loaded at `$2500` only has
  so much free RAM for a single file. If a big conversion won't fit,
  re-run `vgm2vtms.py` with `--pal` (fewer rows/sec = a smaller file, see
  the worked example above) or trim the source VGM.
- **Notes sound right but the song visibly runs at the wrong speed**: NTSC
  vs PAL mismatch between the `--pal` flag used in step 2 and the `-pal`
  flag used in step 7 — one row is always one VBI frame, so the two must
  agree.
