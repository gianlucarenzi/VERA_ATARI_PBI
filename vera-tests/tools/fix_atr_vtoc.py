#!/usr/bin/env python3
"""fix_atr_vtoc.py — repair a dir2atr (AtariSIO)-built Enhanced Density
(130K) .atr: fix corrupted directory-entry flags, compact away a wasted
sector dir2atr's allocator leaves behind, and rewrite VTOC1's free-sector
bitmap to match reality.

Bugs observed in practice, all from the same tool/root cause (some
internal free-sector search misbehaving once a disk's files get large):

1. A directory entry's flags byte sometimes comes out as something other
   than $42 (the normal "in use" value every correctly-written entry has),
   most often the last entry(ies) written — DOS then doesn't recognize the
   file at all (it's invisible in a DIR listing even though its data is
   perfectly intact).
2. Sector 720 — the total sector count of a Single/Enhanced-Density-alike
   90K disk, so probably a hardcoded density-boundary check gone wrong —
   is always left out of every file's sector chain even when nothing
   actually needs to avoid it, wasting exactly one sector.
3. VTOC1's free-sector bitmap and free-count fields come out as if the
   entire disk beyond that same boundary were used, when most of it isn't
   — DOS then reports "0 FREE SECTORS" even though the files are all
   present and readable (their chains are otherwise correct — CIO follows
   sector-to-sector links to read a file, it doesn't trust the directory's
   byte/sector count for that, only for statistics).

This script re-derives the truth by *walking each file's actual sector
chain* (not trusting the directory's start/count, which can undercount
when a chain detours around a reserved area) via each sector's own
next-sector link bytes, then:
  - fixes any non-$42 flags byte it finds,
  - compacts every file's data to close the sector-720 gap (relocating
    sector content and fixing up next-sector links and each directory
    entry's start sector to match — everything on the disk shifts down
    by at most one sector, so this is a small, mechanical rewrite, not a
    general defragmenter),
  - patches boot sector 1's own hardcoded pointer to DOS.SYS's start
    sector (byte offset 15 within that sector) if DOS.SYS moved — the
    boot process reads this raw sector number directly, before it's
    capable of any directory lookup, so relocating DOS.SYS without fixing
    this crashes the machine at boot instead of just failing to find a
    file (confirmed the hard way: this is what actually happened before
    this patch step existed),
  - rewrites VTOC1's bitmap/free-count/total-sectors fields against the
    compacted, now gap-free layout.

VTOC1 (a single 128-byte sector, 10 header bytes + 118 bitmap bytes = 944
bits) can only ever represent sectors 0-943 on its own — a real VTOC2
(DOS 2.5) would be needed to track more, which this script doesn't
implement. It raises an error rather than writing a wrong bitmap if the
disk's real (post-compaction) sector usage doesn't fit.

Usage:
  python3 fix_atr_vtoc.py disk.atr
"""
import sys

VTOC1_SECTOR = 360
DIR_SECTORS = range(361, 369)
RESERVED = frozenset({0, 1, 2, 3, VTOC1_SECTOR, *DIR_SECTORS})
VTOC1_MAX_SECTOR = 943   # highest sector number VTOC1's 944-bit bitmap can represent (0-943)


def sector_offset(n):
    """Byte offset of sector n within the .atr file (16-byte header, then
    128-byte sectors — this project only ever builds 128-byte-sector
    densities, so no need to handle a larger post-boot sector size)."""
    if n <= 3:
        return 16 + (n - 1) * 128
    return 16 + 3 * 128 + (n - 4) * 128


def next_sector_of(data, n):
    off = sector_offset(n)
    return ((data[off + 125] & 0x03) << 8) | data[off + 126]


def walk_chain(data, start):
    sectors = []
    n = start
    seen = set()
    while n != 0 and n not in seen:
        seen.add(n)
        sectors.append(n)
        n = next_sector_of(data, n)
    return sectors


def read_directory(data):
    """Returns [(entry_off, chain), ...], fixing bad flags bytes in place
    as a side effect (data is a bytearray)."""
    files = []
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
            start = data[entry_off + 3] | (data[entry_off + 4] << 8)
            files.append((entry_off, start))
    files.sort(key=lambda f: f[1])   # preserve current on-disk order
    return [(entry_off, walk_chain(data, start)) for entry_off, start in files]


def compact(data, files):
    """Builds an old-sector -> new-sector map that packs every file's real
    chain back-to-back right after the reserved sectors, in the same
    relative order, skipping over sector 720 (and any other reserved
    sector) instead of leaving a hole for it. Returns (old_to_new, new_data)."""
    old_to_new = {}
    pos = 4   # first sector after the boot sectors
    for _, chain in files:
        for old_sec in chain:
            while pos in RESERVED:
                pos += 1
            old_to_new[old_sec] = pos
            pos += 1

    if old_to_new and max(old_to_new.values()) > VTOC1_MAX_SECTOR:
        raise SystemExit(
            f"files need sector {max(old_to_new.values())} even after compaction, "
            f"beyond what a single VTOC sector can track ({VTOC1_MAX_SECTOR}) — this "
            f"disk needs a real VTOC2 (DOS 2.5), which this script doesn't implement. "
            f"Trim the disk's contents instead."
        )

    new_data = bytearray(data)
    old_snapshot = bytes(data)

    for entry_off, chain in files:
        for idx, old_sec in enumerate(chain):
            new_sec = old_to_new[old_sec]
            next_new = old_to_new[chain[idx + 1]] if idx + 1 < len(chain) else 0

            old_off = sector_offset(old_sec)
            sector_bytes = bytearray(old_snapshot[old_off:old_off + 128])
            file_num_bits = sector_bytes[125] & 0xFC       # preserve, only next-ptr changes
            sector_bytes[125] = file_num_bits | ((next_new >> 8) & 0x03)
            sector_bytes[126] = next_new & 0xFF

            new_off = sector_offset(new_sec)
            new_data[new_off:new_off + 128] = sector_bytes

        new_start = old_to_new[chain[0]] if chain else 0
        new_data[entry_off + 3] = new_start & 0xFF
        new_data[entry_off + 4] = (new_start >> 8) & 0xFF

    used = set(RESERVED) | set(old_to_new.values())
    return new_data, used


def patch_boot_dos_pointer(data):
    """Boot sector 1, byte offset 15 (2 bytes, little-endian): the raw
    sector number the boot process jumps to next to keep loading DOS.SYS,
    written by dir2atr at disk-creation time and never revisited — if
    compact() moved DOS.SYS, this stale pointer would send the boot
    process to whatever now occupies DOS.SYS's *old* sectors instead."""
    for dirsec in DIR_SECTORS:
        off = sector_offset(dirsec)
        for i in range(0, 128, 16):
            entry_off = off + i
            if data[entry_off] == 0:
                continue
            name = data[entry_off + 5:entry_off + 13]
            ext = data[entry_off + 13:entry_off + 16]
            if name == b"DOS     " and ext == b"SYS":
                start = data[entry_off + 3] | (data[entry_off + 4] << 8)
                boot_off = sector_offset(1) + 15
                old = data[boot_off] | (data[boot_off + 1] << 8)
                if old != start:
                    print(f"patching boot sector's DOS.SYS pointer: sector {old} -> {start}")
                    data[boot_off] = start & 0xFF
                    data[boot_off + 1] = (start >> 8) & 0xFF
                return


def write_vtoc(data, used):
    total = VTOC1_MAX_SECTOR + 1   # 944 representable sector numbers, 0-943
    bitmap = bytearray(118)
    for s in range(total):
        if s not in used:
            bitmap[s // 8] |= (1 << (7 - (s % 8)))
    free_count = total - len(used & set(range(total)))

    vtoc_off = sector_offset(VTOC1_SECTOR)
    data[vtoc_off + 1] = total & 0xFF
    data[vtoc_off + 2] = (total >> 8) & 0xFF
    data[vtoc_off + 3] = free_count & 0xFF
    data[vtoc_off + 4] = (free_count >> 8) & 0xFF
    data[vtoc_off + 10:vtoc_off + 10 + 118] = bitmap
    return free_count, total


def fix(path):
    with open(path, "rb") as f:
        data = bytearray(f.read())

    files = read_directory(data)
    data, used = compact(data, files)
    patch_boot_dos_pointer(data)
    free_count, total = write_vtoc(data, used)

    with open(path, "wb") as f:
        f.write(data)

    print(f"{path}: VTOC fixed — {free_count} free sectors (of {total} trackable, "
          f"{len(used)} used)")


def main():
    if len(sys.argv) != 2:
        sys.exit(f"Usage: python3 {sys.argv[0]} disk.atr")
    fix(sys.argv[1])


if __name__ == "__main__":
    main()
