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

    # Build a VERA-native palette then apply it to the original canvas with
    # Floyd-Steinberg dithering.  The two-step approach gives better quality
    # than quantising the pre-reduced image directly:
    #
    # Step A — build palette in VERA's 12-bit colour space:
    #   • Map every pixel to the nearest representable VERA colour using a
    #     round-to-nearest LUT: (v+8)>>4 gives the 4-bit index, ×17 restores
    #     the exact 8-bit equivalent.  This prevents truncation artefacts
    #     (e.g. B=15 → b4=0 → yellow instead of b4=1 → near-white).
    #   • Ask Pillow's median-cut to select the 255 most useful colours from
    #     that restricted palette, then always reserve one slot for pure white
    #     (255,255,255) so it is never lost to warm-yellow variants.
    #
    # Step B — apply palette to the ORIGINAL (full 8-bit) canvas with dithering:
    #   Using the un-reduced image for dithering lets Floyd-Steinberg measure
    #   the true quantisation error and spread it accurately over neighbours,
    #   giving visually smoother colour transitions than dithering after the
    #   4-bit reduction.
    vera_lut = [min(15, (v + 8) >> 4) * 17 for v in range(256)]
    canvas_vera = canvas.point(vera_lut * 3)

    # Find the 255 most useful VERA colours via Pillow's median-cut
    temp = canvas_vera.convert("P", palette=Image.ADAPTIVE, colors=255)
    pal_flat = list(temp.getpalette()[:255 * 3])

    # Ensure pure white (the only VERA colour with all channels = 255) is present
    has_white = any(
        pal_flat[i * 3] == 255 and pal_flat[i * 3 + 1] == 255 and pal_flat[i * 3 + 2] == 255
        for i in range(len(pal_flat) // 3)
    )
    if not has_white:
        pal_flat = [255, 255, 255] + pal_flat[:254 * 3]   # prepend white, drop last

    # Pad palette to 256 entries (with black)
    pal_flat += [0, 0, 0] * (256 - len(pal_flat) // 3)

    # Quantise the original full-quality canvas to this VERA palette,
    # with Floyd-Steinberg dithering for smoother colour transitions
    palette_img = Image.new("P", (1, 1))
    palette_img.putpalette(pal_flat)
    indexed = canvas.quantize(palette=palette_img, dither=1)

    pixels = indexed.tobytes()
    assert len(pixels) == CANVAS_W * CANVAS_H, "unexpected pixel byte count"

    # The palette is exactly pal_flat (all values are VERA multiples of 17)
    rgb_palette = pal_flat

    palette_bytes = bytearray()
    for i in range(256):
        r, g, b = rgb_palette[i * 3], rgb_palette[i * 3 + 1], rgb_palette[i * 3 + 2]
        # Multiples of 17: >>4 is lossless (e.g. 255>>4=15, 17>>4=1, 0>>4=0)
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
