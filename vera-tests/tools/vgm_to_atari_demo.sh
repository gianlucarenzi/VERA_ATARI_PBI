#!/bin/bash
# vgm_to_atari_demo.sh — end-to-end wrapper around the pipeline documented in
# vera-tests/vgm-to-atari-workflow.md: turn one or more VGM/VGZ PSG rips plus
# a cover image into a PC preview (vtm_play, audio + artwork + VU meter) and,
# if the compiled song is small enough, a self-booting Atari disk (DOS 2.0S,
# AUTORUN.SYS, VERA artwork).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if REPO_ROOT="$(cd "$SCRIPT_DIR" && git rev-parse --show-toplevel 2>/dev/null)"; then
    :
else
    REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
fi
VERA_TESTS="$REPO_ROOT/vera-tests"
TOOLS="$VERA_TESTS/tools"

# 22K, the size a compiled DEMO.VTM stays under in the worked Bubble Bobble
# example (vgm-to-atari-workflow.md, "How big can the audio file be?") once
# DOS.SYS + AUTORUN.SYS + a 320x240 DEMO.VBM also share the 130K disk. This
# is a fast go/no-go gate, not the exact per-disk budget: fix_atr_vtoc.py
# still re-derives and enforces the real sector math (VTOC1, 943 sectors
# max) once the actual AUTORUN.SYS/DEMO.VBM sizes for *this* build are known,
# and fails loudly if a particular image name pushes it over.
MAX_AUDIO_BYTES=$((22 * 1024))

PAL=0
PLAY=0
OUT_DIR=""
IMAGE=""
NAME=""
IMAGE_TITLE=""
AUDIO_FILES=()

usage() {
    cat <<EOF
Usage: $(basename "$0") [options] --image cover.png song1.vgm [song2.vgz ...]

Options:
  --image PATH   Cover artwork (any Pillow-readable format). Required.
  -o, --out DIR  Output directory (default: build/vgm-atari/<name>)
  -n, --name NAME
                 Base name for generated files (default: derived from
                 the first audio file's basename)
  --title NAME   Display name embedded in the .vbm artwork file
                 (default: img2vbm.py's own default, the image's filename)
  --pal          50 Hz row rate (matches a PAL target/atari800 -pal).
                 Default is NTSC (60 Hz) — must be the same choice used
                 later when booting with/without atari800 -pal.
  --play         Actually launch the PC preview (vtm_play) at the end
                 instead of just printing the command.
  -h, --help     Show this help.

Always produced:
  <out>/<name>.vtms, <out>/DEMO.VTM, <out>/vtm_play (PC preview binary)

Produced only if the compiled DEMO.VTM is <= 22K:
  <out>/DEMO.VBM, <out>/TESTPLR.COM, <out>/<name>.atr (bootable, AUTORUN.SYS)
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --image) IMAGE="$2"; shift 2 ;;
        -o|--out) OUT_DIR="$2"; shift 2 ;;
        -n|--name) NAME="$2"; shift 2 ;;
        --title) IMAGE_TITLE="$2"; shift 2 ;;
        --pal) PAL=1; shift ;;
        --play) PLAY=1; shift ;;
        -h|--help) usage; exit 0 ;;
        --) shift; while [ $# -gt 0 ]; do AUDIO_FILES+=("$1"); shift; done ;;
        -*) echo "unknown option: $1" >&2; usage >&2; exit 1 ;;
        *) AUDIO_FILES+=("$1"); shift ;;
    esac
done

if [ -z "$IMAGE" ]; then
    echo "error: --image is required" >&2
    exit 1
fi
if [ ! -f "$IMAGE" ]; then
    echo "error: image not found: $IMAGE" >&2
    exit 1
fi
if [ "${#AUDIO_FILES[@]}" -eq 0 ]; then
    echo "error: at least one .vgm/.vgz file is required" >&2
    exit 1
fi
for f in "${AUDIO_FILES[@]}"; do
    if [ ! -f "$f" ]; then
        echo "error: audio file not found: $f" >&2
        exit 1
    fi
done

if [ -z "$NAME" ]; then
    base="$(basename "${AUDIO_FILES[0]}")"
    base="${base%.*}"
    NAME="$(echo "$base" | tr -cs 'A-Za-z0-9_-' '_')"
fi
if [ -z "$OUT_DIR" ]; then
    OUT_DIR="$REPO_ROOT/build/vgm-atari/$NAME"
fi
mkdir -p "$OUT_DIR"

PAL_FLAG=()
[ "$PAL" -eq 1 ] && PAL_FLAG=(--pal)

echo "== 1/4: VGM/VGZ -> .vtms =="
VTMS="$OUT_DIR/$NAME.vtms"
python3 "$TOOLS/vgm2vtms.py" "${AUDIO_FILES[@]}" "$VTMS" "${PAL_FLAG[@]}"

echo "== 2/4: .vtms -> DEMO.VTM =="
DEMO_VTM="$OUT_DIR/DEMO.VTM"
python3 "$TOOLS/vtm_compile.py" "$VTMS" "$DEMO_VTM"
VTM_BYTES=$(stat -c%s "$DEMO_VTM")

echo "== 3/4: build PC preview (vtm_play) =="
VTM_PLAY="$OUT_DIR/vtm_play"
cc -O2 -o "$VTM_PLAY" "$TOOLS/vtm_play.c" $(sdl2-config --cflags --libs) -lSDL2_image -lm

PLAY_CMD=("$VTM_PLAY" "$DEMO_VTM" --image "$IMAGE")
[ "$PAL" -eq 1 ] && PLAY_CMD+=(--pal)
if [ "$PLAY" -eq 1 ]; then
    echo "== 4/4: playing on PC (close the window or Ctrl+C to stop) =="
    "${PLAY_CMD[@]}"
else
    echo "PC preview ready. Run it with:"
    printf '  %q ' "${PLAY_CMD[@]}"; echo
fi

if [ "$VTM_BYTES" -gt "$MAX_AUDIO_BYTES" ]; then
    echo
    echo "DEMO.VTM is $VTM_BYTES bytes (> ${MAX_AUDIO_BYTES} = 22K) —" \
         "skipping the Atari disk (see vgm-to-atari-workflow.md's" \
         "'How big can the audio file be?' section: re-run with --pal for" \
         "a smaller file, or trim the source VGM)."
    exit 0
fi

echo
echo "DEMO.VTM is $VTM_BYTES bytes (<= 22K) — building the Atari disk too."

echo "== converting artwork for VERA (img2vbm.py) =="
if python3 -c "import wand.image" >/dev/null 2>&1; then
    echo "   using ImageMagick (Wand) for color quantization/dithering"
else
    echo "   Wand not installed — using img2vbm.py's internal Pillow-only conversion"
    echo "   For optimal results, install it: sudo apt install python3-wand"
fi
DEMO_VBM="$OUT_DIR/DEMO.VBM"
if [ -n "$IMAGE_TITLE" ]; then
    python3 "$TOOLS/img2vbm.py" "$IMAGE" "$DEMO_VBM" "$IMAGE_TITLE"
else
    python3 "$TOOLS/img2vbm.py" "$IMAGE" "$DEMO_VBM"
fi

echo "== building TESTPLR.COM (make) =="
make -C "$REPO_ROOT" .dos20/DOS.SYS TESTPLR.COM
cp "$REPO_ROOT/TESTPLR.COM" "$OUT_DIR/TESTPLR.COM"

echo "== staging disk contents =="
DISK_DIR="$OUT_DIR/disk"
rm -rf "$DISK_DIR"
mkdir -p "$DISK_DIR"
cp "$REPO_ROOT/.dos20/DOS.SYS" "$DISK_DIR/DOS.SYS"
cp "$OUT_DIR/TESTPLR.COM" "$DISK_DIR/AUTORUN.SYS"
cp "$DEMO_VTM" "$DISK_DIR/DEMO.VTM"
cp "$DEMO_VBM" "$DISK_DIR/DEMO.VBM"

echo "== building .atr (dir2atr -E -b Dos20) =="
ATR="$OUT_DIR/$NAME.atr"
rm -f "$ATR"
dir2atr -E -b Dos20 "$ATR" "$DISK_DIR"

echo "== repairing VTOC/flags/boot pointer (fix_atr_vtoc.py) =="
python3 "$TOOLS/fix_atr_vtoc.py" "$ATR"

echo
echo "Done: $ATR"
ROM="$REPO_ROOT/vera_pbi_handler.rom"
BOOT_CMD=(atari800 -verax16 -verax16-rom "$ROM" -volume 100 -xe "$ATR")
[ "$PAL" -eq 1 ] && BOOT_CMD=(atari800 -verax16 -verax16-rom "$ROM" -pal -volume 100 -xe "$ATR")
echo "Boot it with:"
printf '  %q ' "${BOOT_CMD[@]}"; echo
