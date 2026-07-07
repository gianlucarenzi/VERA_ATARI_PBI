/* vtm_loader.c — load a compiled .vtm file into RAM for vtm_player.s.
 * Kept separate from the asm player so it can be reused by other
 * VTM-playing programs without pulling in playback logic.
 *
 * Deliberately avoids fseek(SEEK_END)/ftell() to size the file first: the
 * Atari CIO-backed stdio in cc65 doesn't reliably support seeking past the
 * current position, so that idiom silently fails there (fopen() succeeds,
 * a later fseek()/ftell() doesn't, and the file looks "missing" to the
 * caller). Reading straight into a capped buffer sidesteps that.
 */
#include <stdio.h>
#include <stdlib.h>
#include "vtm.h"

/* Read in growing chunks rather than guessing a cap up front: hand-authored
 * songs (see vera-tests/songs) are a few hundred bytes, but songs produced
 * by tools/vgm2vtms.py can run tens of KB for a multi-minute tune. A fixed
 * cap is either too small (truncates the big ones) or, allocated eagerly
 * before the actual size is known, too big to fit free RAM on a real Atari
 * (especially with BASIC enabled) even for a tiny file. Growing on demand
 * uses only as much memory as the file actually needs. */
#define VTM_CHUNK_SIZE 4096u

void *vtm_load_file(const char *filename)
{
    FILE *f;
    unsigned char *buf, *grown;
    size_t cap, len, n;

    f = fopen(filename, "rb");
    if (!f) return NULL;

    cap = VTM_CHUNK_SIZE;
    buf = malloc(cap);
    if (!buf) { fclose(f); return NULL; }

    /* Loop on n == 0, not "n < requested": cc65's CIO-backed fread() on the
     * Atari target can return fewer bytes than asked for a single call even
     * mid-file (not just at EOF), so a short read must not be taken as "no
     * more data" — only a genuine zero-byte read means that. */
    len = 0;
    for (;;) {
        n = fread(buf + len, 1, cap - len, f);
        if (n == 0) break;
        len += n;

        if (len == cap) {
            cap += VTM_CHUNK_SIZE;
            grown = realloc(buf, cap);
            if (!grown) { free(buf); fclose(f); return NULL; }
            buf = grown;
        }
    }
    fclose(f);

    if (len == 0) { free(buf); return NULL; }

    grown = realloc(buf, len);   /* trim to the exact size read */
    if (grown) buf = grown;

    return buf;
}
