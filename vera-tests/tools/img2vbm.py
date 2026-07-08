#!/usr/bin/env python3
"""img2vbm.py — convert any Pillow-readable image (PNG/JPEG/BMP/GIF/...)
into a VBM1 file: a direct dump of VERA's 320x240 8bpp bitmap-mode layer,
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
"""
import sys
import struct

try:
    from PIL import Image
except ImportError:
    sys.exit("img2vbm.py requires Pillow: pip install Pillow")

CANVAS_W = 320
CANVAS_H = 240


def build_vbm(src_path):
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

    indexed = canvas.convert("P", palette=Image.ADAPTIVE, colors=256)
    pixels = indexed.tobytes()
    assert len(pixels) == CANVAS_W * CANVAS_H, "unexpected pixel byte count"

    rgb_palette = indexed.getpalette() or []
    rgb_palette += [0, 0, 0] * (256 - len(rgb_palette) // 3)   # pad to 256 entries

    palette_bytes = bytearray()
    for i in range(256):
        r, g, b = rgb_palette[i * 3:i * 3 + 3]
        r4, g4, b4 = r >> 4, g >> 4, b >> 4
        palette_bytes.append((g4 << 4) | b4)    # byte0 = GGGGBBBB
        palette_bytes.append(r4)                # byte1 = 0000RRRR

    header = struct.pack("<4sHHBxxx", b"VBM1", CANVAS_W, CANVAS_H, 8)
    return header + bytes(palette_bytes) + pixels


def main():
    if len(sys.argv) != 3:
        sys.exit(f"Usage: python3 {sys.argv[0]} image.png out.vbm")

    src_path, out_path = sys.argv[1], sys.argv[2]
    data = build_vbm(src_path)
    with open(out_path, "wb") as f:
        f.write(data)

    print(f"{out_path}: {len(data)} bytes ({CANVAS_W}x{CANVAS_H}, 8bpp)")


if __name__ == "__main__":
    main()
