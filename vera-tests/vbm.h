/* vbm.h — C-side API for showing a VBM1 bitmap on VERA's own video output
 * (vbm_display.s). See workflow/01-vera-asset-format.md for the VBM1 file
 * layout and tools/img2vbm.py for the PC-side converter.
 */
#ifndef VBM_H
#define VBM_H

/* vbm_init() — switch VERA to 320x240 8bpp bitmap mode on layer 0 (VGA
 * output, layer 1/text disabled) — the screen goes solid black until
 * vbm_load_file() streams an image in. Saves the prior display state for
 * vbm_done() to restore. */
void vbm_init(void);

/* vbm_done() — restore whatever display state vbm_init() found (layer 1
 * back on, 1:1 scale) — call before returning to DOS/another VERA-graphics
 * program. */
void vbm_done(void);

/* vbm_progress_cb — optional progress callback for vbm_load_file(), called
 * after every streamed chunk with (bytes streamed so far, total bytes to
 * stream = 512-byte palette + 320x240 pixel data). Unlike vtm_load_file()'s
 * callback there's no "title becomes readable" milestone to report — a
 * .vbm file carries no metadata — so this is just a plain byte counter. */
typedef void (*vbm_progress_cb)(unsigned long loaded, unsigned long total);

/* vbm_load_file() — stream a compiled .vbm file's palette and 320x240 8bpp
 * pixel data straight into VRAM (no full-file buffering: at most a few
 * hundred bytes of Atari RAM are used regardless of image size). Can be
 * called before or after vbm_init() — it only writes VRAM, it doesn't
 * touch the display composer itself. progress may be NULL. Returns 1 on
 * success, 0 on any I/O error or bad magic (caller should treat 0 as "no
 * image available", not corrupt state — VRAM is simply left however far
 * the stream got). */
unsigned char vbm_load_file(const char *filename, vbm_progress_cb progress);

#endif /* VBM_H */
