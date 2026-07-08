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
song.

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
cc -O2 -o vtm_play vera-tests/tools/vtm_play.c $(sdl2-config --cflags --libs) -lm
./vtm_play DEMO.VTM              # Ctrl+C to stop
./vtm_play DEMO.VTM --pal        # tick at 50 Hz instead of 60 Hz
./vtm_play DEMO.VTM --wav out.wav --seconds 8   # render to a WAV file instead
```

This runs the *exact same* row/pattern/envelope logic as `vtm_player.s`,
just with textbook-shape waveforms instead of real VERA hardware quirks
(pulse-width XOR into saw/triangle, LFSR noise timing) — good enough to
catch a wrong note or bad tempo before doing a much slower
emulator/hardware round trip. It is not a substitute for a final listen on
`atari800` or real hardware.

## 5. Build `TESTPLR.COM`

```sh
make TESTPLR.COM
```

This links `test_player.c` + `vtm_loader.c` + `vtm_player.o` + `vu_pm.o`
against `vera-tests/atari_nosyschk.cfg` (a low-memory-footprint linker
config, load address `$3000`, no VERA.SYS needed — the program pokes PSG
registers directly). The VU meter (`vu_pm.s`) is always linked in; nothing
to toggle.

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

## Troubleshooting

- **Disk boots into a reset loop**: density mismatch — rebuild with
  `dir2atr -E` (see step 6), not `-D`.
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
- **Notes sound right but the song visibly runs at the wrong speed**: NTSC
  vs PAL mismatch between the `--pal` flag used in step 2 and the `-pal`
  flag used in step 7 — one row is always one VBI frame, so the two must
  agree.
