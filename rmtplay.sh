#!/usr/bin/env bash
# rmtplay.sh — build RMTPLAY.COM for an RMT song and play it in atari800
# with the VeraX16 PBI card.
#
#   ./rmtplay.sh SONG.rmt [antic|vera|both]
#
#   SONG.rmt   RMT4 or RMT8 module (any path)
#   antic      visualizer on the Atari screen only
#   vera       visualizer on the VERA screen only (Atari screen off)
#   both       on both screens (default)
#
# Environment (optional):
#   ATARI800       emulator to run (default: atari800 from PATH)
#   ATARI800_OPTS  extra emulator options, e.g. "-pal -verax16-scale 2"
#   RMT_NAME, RMT_AUTHOR, RMT_DATE   song info shown by the player instead
#                  of the text stored in the .rmt (see tools/rmtinfo.py)
#
# Sound always plays on POKEY + VERA PSG; keys in the player: SPACE pause,
# R restart, 1/2/3 POKEY/VERA/both, 4 hybrid (RMT8), S stereo, ESC exit.

set -euo pipefail

usage() {
    sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
}

[ $# -ge 1 ] && [ $# -le 2 ] || usage

song=$1
screen=${2:-both}

case $screen in
    antic|vera|both) ;;
    *) echo "rmtplay.sh: screen must be antic, vera or both, not '$screen'" >&2; usage ;;
esac

if [ ! -f "$song" ]; then
    echo "rmtplay.sh: $song: no such file" >&2
    exit 1
fi
if [ "$(head -c 2 "$song" | od -An -tx1 | tr -d ' \n')" != "ffff" ]; then
    echo "rmtplay.sh: $song: not an RMT module (no Atari binary header)" >&2
    exit 1
fi

dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
emu=${ATARI800:-atari800}
if ! command -v "$emu" >/dev/null; then
    echo "rmtplay.sh: emulator '$emu' not found (set ATARI800=...)" >&2
    exit 1
fi

# make cannot handle spaces in file names: such a song is copied first
song_abs=$(realpath "$song")
case $song_abs in
    *[[:space:]]*)
        mkdir -p "$dir/vera-tests/rmt/gen"
        copy="$dir/vera-tests/rmt/gen/$(basename "$song_abs" | tr '[:space:]' '_')"
        cp "$song_abs" "$copy"
        song_abs=$copy
        ;;
esac

echo "rmtplay.sh: building RMTPLAY.COM for $(basename "$song") (screen: $screen)"
make -C "$dir" --no-print-directory RMTPLAY.COM RMT_SONG="$song_abs" RMT_SCREEN="$screen"

echo "rmtplay.sh: starting $emu"
# shellcheck disable=SC2086 # ATARI800_OPTS is a list of options
exec "$emu" -xl -nobasic -verax16 -verax16-rom "$dir/vera_pbi_handler.rom" \
     ${ATARI800_OPTS:-} -run "$dir/RMTPLAY.COM"
