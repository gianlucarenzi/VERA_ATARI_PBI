#!/usr/bin/env python3
"""Fail if the files of a directory do not fit on a DOS 2.0S disk.

dir2atr writes an enhanced-density image, but DOS 2.0S only addresses the
first 720 sectors (707 free for files): anything beyond is silently
invisible to DOS.  Each data sector holds 125 bytes of file data.
"""
import os
import sys

DOS20_FREE_SECTORS = 707
BYTES_PER_SECTOR = 125

def main(path):
    total = 0
    for name in sorted(os.listdir(path)):
        full = os.path.join(path, name)
        if not os.path.isfile(full):
            continue
        sectors = max(1, -(-os.path.getsize(full) // BYTES_PER_SECTOR))
        total += sectors
        print("  %-12s %4d sectors" % (name, sectors))
    print("  %-12s %4d / %d sectors" % ("total", total, DOS20_FREE_SECTORS))
    if total > DOS20_FREE_SECTORS:
        print("ERROR: %s does not fit on a DOS 2.0S disk (%d sectors over)"
              % (path, total - DOS20_FREE_SECTORS), file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
