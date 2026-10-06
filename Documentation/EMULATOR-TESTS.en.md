---
title: "VERA X16 for Atari — Running the tests in the emulator"
subtitle: "atari800 fork with the VeraX16 PBI card"
lang: en
---

# Overview

The test programs in `vera-tests/` run on a fork of the **atari800** emulator
(version 5.2.0) that emulates the VeraX16 card on the Atari PBI bus:
VERA registers at `$D100-$D11F`, the 2 KB handler ROM at `$D800-$DFFF`
(selected through the PBI latch `$D1FF`).

The emulator lives next to this repository:

```
~/Progetti/ATARI/
├── atari800/                 emulator (src/atari800 is the binary)
│   └── vera_pbi_rom -> ../VERA_ATARI_PBI
└── VERA_ATARI_PBI/           this repository (ROM, drivers, tests)
```

All the commands below are run **from the root of this repository**
(`VERA_ATARI_PBI`), so the emulator is `../atari800/src/atari800`.
For brevity:

```sh
EMU=../atari800/src/atari800
```

# Prerequisites

## Building the emulator

```sh
cd ../atari800
./autogen.sh
./configure --enable-pbi-verax16
make
cd -
```

The VERA emulation is in `src/pbi_verax16.c` (registers, FX, audio, SPI)
and `src/vera_video.c` (rendering). Without `--enable-pbi-verax16` the
`-verax16` options do not exist.

No Atari OS ROM is required: when none is configured in `~/.atari800.cfg`
atari800 uses the built-in AltirraOS and Altirra BASIC.

## Building the ROM, the drivers and the test disks

Required tools: `cc65` (`ca65`, `ld65`, `cl65`), `dir2atr`, `python3`.

```sh
make            # ROM, VERA*.SYS drivers, test programs, disk images
```

Main outputs:

| File | Content |
|---|---|
| `vera_pbi_handler.rom` | PBI handler ROM, 2 KB (boots in 80×60) |
| `VERA4030.SYS`, `VERA8030.SYS`, `VERA8060.SYS` | RAM driver for 40×30, 80×30, 80×60 |
| `disk1-runcpm.atr` | DOS 2.0S, `RUNCPM.COM`, `VERA8030.SYS` |
| `disk2-veratests-40x30.atr` | `TEST4`, `TESTGS4`, `TESTMAZ4`, `TESTMTX4`, `TESTRMT`, `VERA4030.SYS` |
| `disk2-veratests-80x30.atr` | `TEST8`, `TESTGS8`, `TESTMAZ8`, `TESTMTX8`, `TESTRMT`, `VERA8030.SYS` |
| `disk2-veratests-80x60.atr` | `TEST6`, `TESTGS6`, `TESTMAZ6`, `TESTMTX6`, `TESTRMT`, `VERA8060.SYS` |
| `disk3-standalone.atr` | `TESTFX`, `TESTIRQ`, `TESTPLR` + `DEMO.VTM` (tests without the driver) |
| `disk4-rmtio.atr` | `TESTRIO` with MyPicoDos autorun and the test files |

The `TESTn`/`TESTGSn`/`TESTMAZn`/`TESTMTXn` programs on the `disk2` disks
already contain the driver of their resolution (`n` = 4: 40×30, 8: 80×30,
6: 80×60).

# The basic command

```sh
$EMU -xl -pal -nopatch -verax16 -verax16-rom vera_pbi_handler.rom \
     disk3-standalone.atr
```

- `-verax16` plugs the card in; `-verax16-rom` gives the handler ROM.
- `-xl` (800XL) or `-xe` (130XE): the driver needs an XL/XE machine.
- `-pal` or `-ntsc`: the tests must pass in both.
- `-nopatch`: no high-speed SIO patch, disk I/O runs at the real serial
  speed (the H: device keeps working). To make it permanent, set
  `ENABLE_SIO_PATCH=0` in `~/.atari800.cfg`.
- The last argument is the disk in `D1:`; more `.atr` files go to `D2:`, `D3:`...

The emulator opens two outputs: the normal ANTIC/GTIA screen and the VERA
screen (640×480 VGA).

## Starting a program from DOS 2.0S

The disks boot DOS 2.0S. From the DUP menu:

1. press `L` (BINARY LOAD);
2. type the file name, for example `TESTFX.COM`, and press Return.

## Starting a program without a disk

`-run` loads an executable straight from the host file system:

```sh
$EMU -xl -pal -nopatch -verax16 -verax16-rom vera_pbi_handler.rom \
     -run TESTIRQ.COM
```

# Options

## VeraX16 card options

| Option | Meaning |
|---|---|
| `-verax16` | enable the card (alias `--use-verax16`) |
| `-verax16-rom F` | handler ROM, 2 KB at `$D800-$DFFF` |
| `-verax16-pbi-id N` | PBI device bit 0-7 (default 7, mask `$80`) |
| `-verax16-config-ms N` | FPGA configuration time after power-on, bus dead meanwhile (default 100); `0` = the board holds Atari RESET until CONFIG_DONE |
| `-verax16-debuglevel N` | log level (default 0) |
| `-verax16-psg-volume N` | VERA PSG level in percent, 0-400 (default 100 = one voice as loud as a POKEY channel at volume 15) |
| `-verax16-sdcard F` | raw SD image (e.g. from `dd`) exposed through the VERA SPI |

## Useful atari800 options

| Option | Use |
|---|---|
| `-xl`, `-xe` | 800XL / 130XE machine |
| `-pal`, `-ntsc` | video standard |
| `-basic`, `-nobasic` | BASIC ROM on/off |
| `-run F` | run a COM/EXE/XEX/BAS file |
| `-nopatch` | no high-speed SIO patch: real serial timing, H: still works |
| `-nopatchall` | no OS patches: real SIO serial timing (H: does not work) |
| `-volume N` | output volume 0-100 |
| `-stereo` | two POKEYs |
| `-turbo` | as fast as possible |
| `-netsio [port]` | NetSIO for FujiNet-PC (default UDP 9997) |
| `-config F` | alternate configuration file |

# Running each test

## TESTFX — FX coprocessor

```sh
$EMU -xl -pal -nopatch -verax16 -verax16-rom vera_pbi_handler.rom \
     disk3-standalone.atr
```

From DUP: `L`, `TESTFX.COM`. It checks every FX register and measures
VRAM copy/fill throughput. Expected result: **PASS 36, FAIL 0**.

## TESTIRQ — VERA interrupts

```sh
$EMU -xl -pal  -nopatch -verax16 -verax16-rom vera_pbi_handler.rom \
     -run TESTIRQ.COM
$EMU -xl -ntsc -nopatch -verax16 -verax16-rom vera_pbi_handler.rom \
     -run TESTIRQ.COM
```

It checks the `VIMIRQ` hook: VSYNC at 59.94 Hz, line IRQ, AFLOW masking,
chaining to the OS and removal. Expected result: **PASS 13, FAIL 0**, both
in PAL and in NTSC. See `Documentation/VERA-IRQ.md`.

## TESTPLR — VTM player

```sh
$EMU -xl -pal -nopatch -volume 100 -verax16 -verax16-rom vera_pbi_handler.rom \
     disk3-standalone.atr
```

From DUP: `L`, `TESTPLR.COM`. Use `-pal` or `-ntsc` according to the frame
rate the song was converted for. `-volume 100` is needed: the VERA PSG is
quiet at the default mixer level (or raise `-verax16-psg-volume`).

## TEST / TESTGS / TESTMAZ / TESTMTX — driver and screen modes

```sh
$EMU -xl -pal -nopatch -verax16 -verax16-rom vera_pbi_handler.rom \
     disk2-veratests-80x30.atr
```

Use the disk of the resolution under test (`40x30`, `80x30`, `80x60`) and
start, from DUP, `TEST8.COM`, `TESTGS8.COM`, `TESTMAZ8.COM`, `TESTMTX8.COM`
(digit 4 or 6 on the other disks): font loading, gradient, scrolling, maze
and matrix demos.

## TESTRMT — RMT player on POKEY + VERA

On every `disk2-veratests-*.atr` disk, it does not need `VERA.SYS`. From
DUP: `L`, `TESTRMT.COM`.

| Key | Output |
|---|---|
| `1` | POKEY only |
| `2` | VERA only |
| `3` | both (default) |
| `4` | hybrid, RMT8 only: POKEY channels 1-4, VERA channels 5-8 |
| `S` | VERA stereo on/off |
| `ESC` | stop and exit |

## TESTRIO — music during disk I/O

```sh
make disk4-rmtio.atr
$EMU -xl -pal -nopatchall -nobasic \
     -verax16 -verax16-rom vera_pbi_handler.rom disk4-rmtio.atr
```

The disk starts `TESTRIO.COM` by itself (MyPicoDos autorun). `-nopatchall`
is mandatory: without it the emulator intercepts SIO and there is no real
serial timing. Expected (PAL, RBL loader): ~720 bytes/s, 0 errors,
0 lost ticks.

## RUNCPM — ANSI terminal through FujiNet

Terminal 1, FujiNet-PC:

```sh
cd FujiNet/fujinet-pc-ATARI
./run-fujinet -c fnconfig.ini -s SD/
```

Wait for `### NetSIO stopped ###` in the log, then in terminal 2:

```sh
$EMU -xl -pal -nopatch -netsio -verax16 -verax16-rom vera_pbi_handler.rom
```

Details in `README-fujinet.md`.

# Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `ERROR: VeraX16 PBI card not found ($D100 not responding).` | `-verax16` is missing: every test calls `vera_require()` at start |
| Three beeps from the console speaker at boot | the ROM did not see the VERA within ~0.7 s; check `-verax16-config-ms` |
| Unknown option `-verax16` | emulator built without `--enable-pbi-verax16` |
| No sound or very quiet VERA | add `-volume 100` or raise `-verax16-psg-volume` |
| A file is on the disk but DOS does not list it, or "0 FREE SECTORS" | `dir2atr` bug; rebuild with `make`, which runs `vera-tests/tools/fix_atr_vtoc.py` |
| SIO errors or wrong speed in `TESTRIO` | `-nopatchall` is missing |
