#!/usr/bin/env python3
"""img2vbm.py — convert any Pillow-readable image (PNG/JPEG/BMP/GIF/...)
into a VBM2 file: a direct dump of VERA's 320x240 8bpp bitmap-mode layer,
ready for a straight copy loop into VRAM (see ../vbm_display.s and
../../workflow/01-vera-asset-format.md for the binary layout).

The source image is scaled down to fit within 320x240 if it's larger in
either dimension (never scaled up — a smaller image stays crisp with black
letterboxing instead of blurring to fill the frame) and centered on a black
320x240 canvas, which is then quantized to a 256-color palette. This means
all the framing/scaling work happens here, once, on the PC — the Atari-side
loader never computes anything, it just streams bytes to VRAM.

Usage:
  python3 img2vbm.py cover.png cover.vbm
  python3 img2vbm.py cover.png cover.vbm "Bubble Bobble"   # explicit name
"""
import os
import sys
import struct

try:
    from PIL import Image
except ImportError:
    sys.exit("img2vbm.py requires Pillow: pip install Pillow")

CANVAS_W = 320
CANVAS_H = 240
NAME_MAX = 255


def build_vbm(src_path, name=None):
    if name is None:
        name = os.path.splitext(os.path.basename(src_path))[0]
    name_bytes = name.encode("ascii", "replace")[:NAME_MAX]

    src = Image.open(src_path).convert("RGBA")

    # Composite onto black now (not after resizing) so semi-transparent
    # edges blend with the same black the letterboxing uses, not whatever
    # was behind them in the original file.
    black_src = Image.new("RGB", src.size, (0, 0, 0))
    black_src.paste(src, mask=src.split()[3])

    scale = min(CANVAS_W / black_src.width, CANVAS_H / black_src.height, 1.0)
    new_w = max(1, round(black_src.width * scale))
    new_h = max(1, round(black_src.height * scale))
    if scale < 1.0:
        black_src = black_src.resize((new_w, new_h), Image.LANCZOS)

    canvas = Image.new("RGB", (CANVAS_W, CANVAS_H), (0, 0, 0))
    canvas.paste(black_src, ((CANVAS_W - new_w) // 2, (CANVAS_H - new_h) // 2))

    # Pre-reduce to VERA's 12-bit (4 bits per channel) colour space before
    # quantising.  Without this step, Pillow's adaptive quantiser may pick
    # palette entries whose 8-bit channel values are not exact multiples of 17
    # (e.g. B=15), which then truncate to a completely different 4-bit value
    # (15 >> 4 = 0, i.e. blue disappears, so white becomes yellow).  Mapping
    # everything to the 4096 representable VERA colours first ensures the final
    # 4-bit extraction is lossless and the quantisation error is minimised.
    #
    # LUT: round each 8-bit value to the nearest representable 4-bit step.
    # (v + 8) >> 4 gives the nearest 4-bit index (0-15), capped at 15, then
    # x17 restores an 8-bit value that round-trips perfectly through >> 4.
    vera_lut = [min(15, (v + 8) >> 4) * 17 for v in range(256)]
    canvas = canvas.point(vera_lut * 3)  # apply identically to R, G, B

    indexed = canvas.convert("P", palette=Image.ADAPTIVE, colors=256)
    pixels = indexed.tobytes()
    assert len(pixels) == CANVAS_W * CANVAS_H, "unexpected pixel byte count"

    rgb_palette = indexed.getpalette() or []
    rgb_palette += [0, 0, 0] * (256 - len(rgb_palette) // 3)   # pad to 256 entries

    palette_bytes = bytearray()
    for i in range(256):
        r, g, b = rgb_palette[i * 3:i * 3 + 3]
        # All values are exact multiples of 17 after the LUT, so >> 4 is lossless.
        r4, g4, b4 = r >> 4, g >> 4, b >> 4
        palette_bytes.append((g4 << 4) | b4)    # byte0 = GGGGBBBB
        palette_bytes.append(r4)                # byte1 = 0000RRRR

    header = struct.pack("<4sHHBBxx", b"VBM2", CANVAS_W, CANVAS_H, 8, len(name_bytes))
    return header + name_bytes + bytes(palette_bytes) + pixels


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(f'Usage: python3 {sys.argv[0]} image.png out.vbm ["Display Name"]')

    src_path, out_path = sys.argv[1], sys.argv[2]
    name = sys.argv[3] if len(sys.argv) == 4 else None
    data = build_vbm(src_path, name)
    with open(out_path, "wb") as f:
        f.write(data)

    print(f"{out_path}: {len(data)} bytes ({CANVAS_W}x{CANVAS_H}, 8bpp)")


if __name__ == "__main__":
    main()
