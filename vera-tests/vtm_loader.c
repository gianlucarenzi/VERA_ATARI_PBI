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

/* Generous cap for a compact PSG tracker song — way more than any
 * reasonable .vtm will need (the bundled demo.vtms compiles to ~280 bytes). */
#define VTM_MAX_FILE_SIZE 8192

void *vtm_load_file(const char *filename)
{
    FILE *f;
    void *buf;
    size_t n;

    f = fopen(filename, "rb");
    if (!f) return NULL;

    buf = malloc(VTM_MAX_FILE_SIZE);
    if (!buf) { fclose(f); return NULL; }

    n = fread(buf, 1, VTM_MAX_FILE_SIZE, f);
    fclose(f);

    if (n == 0) { free(buf); return NULL; }

    return buf;
}
