/* vbm_loader.c — read a compiled .vbm file (see workflow/01-vera-asset-
 * format.md) and stream it straight into VERA VRAM via vbm_display.s.
 * Deliberately never buffers the whole 320x240 image in Atari RAM (77KB —
 * far more than this machine has free at once): reads a few hundred bytes
 * at a time into vbm_iobuf and streams each chunk to VRAM before reading
 * the next, the same "know the exact size, never guess" spirit as
 * vtm_loader.c but streamed instead of accumulated, since the destination
 * here is VRAM (plenty of room, no CPU-side random access needed after
 * upload) rather than a buffer vtm_player.s has to read patterns back out
 * of.
 */
#include <stdio.h>
#include "vbm.h"

#define HEADER_SIZE   12u    /* magic(4) + width(2) + height(2) + bpp(1) + reserved(3) */
#define PALETTE_SIZE 512u
#define PIXEL_SIZE   (320ul * 240ul)
#define IOBUF_SIZE   128u    /* must match vbm_display.s's VBM_IOBUF_SIZE */

extern unsigned char vbm_iobuf[IOBUF_SIZE];
extern void vbm_seek_palette(void);
extern void vbm_seek_pixels(void);
extern void vbm_stream_len(unsigned char n);

static unsigned char stream_from(FILE *f, unsigned long remaining,
                                  unsigned long *loaded, unsigned long total,
                                  vbm_progress_cb progress)
{
    unsigned char n;

    while (remaining) {
        n = (remaining > IOBUF_SIZE) ? IOBUF_SIZE : (unsigned char)remaining;
        if (fread(vbm_iobuf, 1, n, f) != n) return 0;
        vbm_stream_len(n);
        remaining -= n;
        *loaded += n;
        if (progress) progress(*loaded, total);
    }
    return 1;
}

unsigned char vbm_load_file(const char *filename, vbm_progress_cb progress)
{
    FILE *f;
    unsigned char hdr[HEADER_SIZE];
    unsigned long loaded = 0;
    unsigned long total = PALETTE_SIZE + PIXEL_SIZE;

    f = fopen(filename, "rb");
    if (!f) return 0;

    if (fread(hdr, 1, HEADER_SIZE, f) != HEADER_SIZE) { fclose(f); return 0; }
    if (hdr[0] != 'V' || hdr[1] != 'B' || hdr[2] != 'M' || hdr[3] != '1') {
        fclose(f);
        return 0;
    }
    /* width/height/bpp are fixed at 320x240x8 for this player (see
     * vbm.h/workflow doc) — no need to check them beyond the magic, since
     * we always stream a fixed byte count regardless of what's declared. */

    vbm_seek_palette();
    if (!stream_from(f, PALETTE_SIZE, &loaded, total, progress)) { fclose(f); return 0; }

    vbm_seek_pixels();
    if (!stream_from(f, PIXEL_SIZE, &loaded, total, progress)) { fclose(f); return 0; }

    fclose(f);
    return 1;
}
