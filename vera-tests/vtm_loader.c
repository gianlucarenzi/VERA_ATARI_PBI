/* vtm_loader.c — load a compiled .vtm file into RAM for vtm_player.s.
 * Kept separate from the asm player so it can be reused by other
 * VTM-playing programs without pulling in playback logic.
 *
 * Deliberately avoids fseek(SEEK_END)/ftell() to size the file: the Atari
 * CIO-backed stdio in cc65 doesn't reliably support seeking past the
 * current position, so that idiom silently fails there (fopen() succeeds,
 * a later fseek()/ftell() doesn't, and the file looks "missing" to the
 * caller).
 *
 * Instead of guessing a buffer size and growing it while watching for a
 * zero-byte fread() to mean "that's the whole file" — which sounds
 * reasonable but isn't reliable here: a real-hardware trace showed
 * fread() keep delivering "more" data past a compiled file's true end,
 * up to some sector-chain-related boundary, with no short/zero read to
 * signal it. So growing-by-guess can overshoot the actual content by
 * nearly a full extra chunk right when free RAM is tightest — the exact
 * moment it can least afford to.
 *
 * The .vtm format avoids all of this by being self-describing (see
 * vtm_format.md): the fixed 13-byte header gives title_len, order_len,
 * n_instruments and n_patterns, which is enough to know exactly how many
 * bytes the title/order/instruments/pattern_table "prefix" occupies —
 * and the pattern_table itself then gives each pattern's exact
 * data_offset/n_rows, so the true total file size falls out of that
 * without ever having to ask CIO "is that everything?". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vtm.h"

#define HEADER_SIZE        13u   /* magic(4) + n_channels + frames_per_row +
                                   * n_instruments + n_patterns + order_len +
                                   * loop_pos + reserved(2) + title_len */
#define INSTR_SIZE          3u
#define PATTBL_ENTRY_SIZE   3u
#define READ_CHUNK        512u   /* just I/O granularity now — no longer tied
                                   * to any EOF-detection concern, since every
                                   * read is bounded by a known target size */

void *vtm_load_file(const char *filename, vtm_progress_cb progress)
{
    FILE *f;
    unsigned char hdr[HEADER_SIZE];
    unsigned char *buf, *grown;
    unsigned char n_channels, n_instruments, n_patterns;
    unsigned order_len, title_len, pt_off, i;
    unsigned long prefix_size, total_size, len, want, got;

    f = fopen(filename, "rb");
    if (!f) return NULL;

    if (fread(hdr, 1, HEADER_SIZE, f) != HEADER_SIZE) { fclose(f); return NULL; }
    if (hdr[0] != 'V' || hdr[1] != 'T' || hdr[2] != 'M' || hdr[3] != '3') {
        fclose(f);
        return NULL;
    }

    n_channels    = hdr[4];
    n_instruments = hdr[6];
    n_patterns    = hdr[7];
    order_len     = hdr[8];
    title_len     = hdr[12];

    prefix_size = (unsigned long)HEADER_SIZE + title_len + order_len
                + (unsigned long)n_instruments * INSTR_SIZE
                + (unsigned long)n_patterns * PATTBL_ENTRY_SIZE;

    buf = malloc(prefix_size);
    if (!buf) { fclose(f); return NULL; }
    memcpy(buf, hdr, HEADER_SIZE);

    /* Loop on got == 0, not "got < want": cc65's CIO-backed fread() on the
     * Atari target can return fewer bytes than asked for a single call
     * even mid-file, so a short read must not be taken as "no more data"
     * — only a genuine zero-byte read means that (and here, since len
     * never exceeds the known prefix_size, a zero read really is an
     * unexpected truncated-file error). */
    len = HEADER_SIZE;
    while (len < prefix_size) {
        want = prefix_size - len;
        got = fread(buf + len, 1, want, f);
        if (got == 0) { free(buf); fclose(f); return NULL; }
        len += got;
    }

    /* Pattern table starts right after instruments; each entry is
     * n_rows(1) + data_offset(2, little-endian). The true file size is
     * the furthest byte any pattern's data reaches. */
    pt_off = HEADER_SIZE + title_len + order_len + (unsigned)n_instruments * INSTR_SIZE;
    total_size = prefix_size;
    for (i = 0; i < n_patterns; i++) {
        unsigned char *e = buf + pt_off + i * PATTBL_ENTRY_SIZE;
        unsigned n_rows = e[0];
        unsigned long data_offset = e[1] | ((unsigned long)e[2] << 8);
        unsigned long end = data_offset + (unsigned long)n_rows * n_channels * 2;
        if (end > total_size) total_size = end;
    }

    grown = realloc(buf, total_size);
    if (!grown) { free(buf); fclose(f); return NULL; }
    buf = grown;

    if (progress) progress(buf, len, total_size);   /* title is valid now */

    while (len < total_size) {
        want = total_size - len;
        if (want > READ_CHUNK) want = READ_CHUNK;
        got = fread(buf + len, 1, want, f);
        if (got == 0) { free(buf); fclose(f); return NULL; }
        len += got;
        if (progress) progress(buf, len, total_size);
    }

    fclose(f);
    return buf;
}
