#!/usr/bin/env python3
"""fix_atr_vtoc.py — patch an Enhanced Density (130K) .atr's VTOC free-
sector bitmap after dir2atr (AtariSIO) has built it.

dir2atr's Enhanced Density VTOC1 free-sector bitmap has a bug when a disk's
files use enough sectors to approach the range beyond what plain DOS 2.0
can represent without a second VTOC sector (VTOC2, a DOS 2.5 feature):
instead of correctly marking sectors 720+ as free-or-used per their real
allocation, it marks the entire 720-1009 range (and the bitmap's overall
free count) as if fully used — DOS then reports "0 FREE SECTORS" even
though the disk has plenty of room. The directory/file-allocation data
itself is unaffected (files remain perfectly readable); only the VTOC's
own bookkeeping is wrong.

This script re-derives the correct picture directly from the directory
(the start/count sector-allocation fields are unaffected — only the VTOC
and, seemingly the same bug family, the LAST directory entry's flags byte
come out wrong) and rewrites VTOC1's bitmap/free-count/total-sectors
fields to match — declaring total_sectors=943 rather than the density's
nominal 1010, since 943 is the most VTOC1 (a single 128-byte sector, 118
bytes = 944 bits, sector 0 unused) can ever represent on its own; sectors
944-1009 are simply left out of the addressable range rather than
implementing a full VTOC2. Fine as long as everything on the disk fits
under sector 943 (true for a DOS + DUP + a handful of program/data files)
— this script raises an error rather than writing a wrong bitmap if
that's not the case.

Also repairs a directory entry's flags byte if it's neither 0 (unused
slot) nor $42 (the normal "in use" value every correctly-written entry
has) — seen in practice on the last file added to a disk that also
triggered the VTOC bug above, so it's very likely the same root cause
(some internal free-sector search running dry) rather than a separate
issue worth a separate script.

Usage:
  python3 fix_atr_vtoc.py disk.atr
"""
import sys

VTOC1_SECTOR = 360
DIR_SECTORS = range(361, 369)
VTOC1_TRACKABLE = 943   # bytes 10-127 of the 128-byte VTOC sector = 118*8 bits


def sector_offset(n):
    """Byte offset of sector n within the .atr file (16-byte header, then
    128-byte sectors — sectors 1-3 are always 128 bytes even on densities
    with larger sectors elsewhere, which doesn't apply here since we only
    ever deal with 128-byte-sector densities)."""
    if n <= 3:
        return 16 + (n - 1) * 128
    return 16 + 3 * 128 + (n - 4) * 128


def used_sectors_from_directory(data):
    used = set(range(0, 4))             # sector 0 (unused) + boot sectors 1-3
    used.add(VTOC1_SECTOR)
    used.update(DIR_SECTORS)

    for dirsec in DIR_SECTORS:
        off = sector_offset(dirsec)
        for i in range(0, 128, 16):
            entry_off = off + i
            flags = data[entry_off]
            if flags == 0:
                continue
            if flags != 0x42:
                name = data[entry_off + 5:entry_off + 13].decode("ascii", "replace")
                ext = data[entry_off + 13:entry_off + 16].decode("ascii", "replace")
                print(f"fixing directory entry flags for '{name.strip()}.{ext.strip()}': "
                      f"{flags:#04x} -> 0x42")
                data[entry_off] = 0x42
            count = data[entry_off + 1] | (data[entry_off + 2] << 8)
            start = data[entry_off + 3] | (data[entry_off + 4] << 8)
            used.update(range(start, start + count))
    return used


def fix(path):
    with open(path, "rb") as f:
        data = bytearray(f.read())

    used = used_sectors_from_directory(data)
    if max(used) >= VTOC1_TRACKABLE:
        raise SystemExit(
            f"{path}: files reach sector {max(used)}, beyond what a single "
            f"VTOC sector can track ({VTOC1_TRACKABLE}) — this disk needs a "
            f"real VTOC2 (DOS 2.5), which this script doesn't implement. "
            f"Trim the disk's contents instead."
        )

    bitmap = bytearray(118)
    for s in range(VTOC1_TRACKABLE):
        if s not in used:
            bitmap[s // 8] |= (1 << (7 - (s % 8)))

    free_count = VTOC1_TRACKABLE - len(used)

    vtoc_off = sector_offset(VTOC1_SECTOR)
    data[vtoc_off + 1] = VTOC1_TRACKABLE & 0xFF
    data[vtoc_off + 2] = (VTOC1_TRACKABLE >> 8) & 0xFF
    data[vtoc_off + 3] = free_count & 0xFF
    data[vtoc_off + 4] = (free_count >> 8) & 0xFF
    data[vtoc_off + 10:vtoc_off + 10 + 118] = bitmap

    with open(path, "wb") as f:
        f.write(data)

    print(f"{path}: VTOC fixed — {free_count} free sectors (of {VTOC1_TRACKABLE} trackable, "
          f"{len(used)} used)")


def main():
    if len(sys.argv) != 2:
        sys.exit(f"Usage: python3 {sys.argv[0]} disk.atr")
    fix(sys.argv[1])


if __name__ == "__main__":
    main()
