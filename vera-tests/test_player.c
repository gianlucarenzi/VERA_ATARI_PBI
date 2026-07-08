/* test_player.c — VTM PSG music player demo for the VERA PBI card.
 *
 * Loads DEMO.VTM from the current default drive/directory and plays it
 * on 4 VERA PSG voices, ticked once per vertical blank (same poll-loop
 * idiom as test_matrix.c). Shows a 4-channel VU meter via Player/Missile
 * graphics + a Display List Interrupt (see vu_pm.s) while playing — this
 * doesn't touch the normal text screen at all, unlike an actual graphics
 * mode switch would. Press any key to stop.
 *
 * Screen: cleared to black background / white text, with the song's
 * TITLE (see vtm_format.md's header field, offset 12) printed centered
 * on the top row in reverse video. No other status text — only genuine
 * errors are printed, so they don't flash past unread.
 *
 * If D1:DEMO.VBM is also present (see vbm.h/vbm_display.s, workflow/
 * 01-vera-asset-format.md's VBM2 format and tools/img2vbm.py), its artwork
 * is shown centered on a black background on VERA's own video output —
 * a completely separate screen from the Atari's own ANTIC/GTIA display
 * used above for the title/VU meter — for as long as the song plays. No
 * image file means no change to VERA's display at all. The image's own
 * embedded name is printed centered on this (the Atari's) screen too,
 * the same way as the song title.
 *
 * Build: standalone, no VERA.SYS needed (PSG registers are poked directly,
 * same as test_matrix.c pokes VERA video registers directly).
 */
#include <stdio.h>
#include <stdlib.h>
#include <atari.h>
#include "vera_detect.h"
#include "vtm.h"
#include "vu_pm.h"
#include "vbm.h"

#define CH_NONE 255
#define CH_REG (*(volatile unsigned char *)0x02FC)
#define N_BARS 4u
#define SCREEN_COLS 40u

/* Bars never fully vanish: level 3 maps to 4 cells = 8px in vu_pm.s's
 * level+1 mapping, matching the requested resting height. Decay is fast
 * (-12/frame) rather than the old -1/frame, since the visible range grew
 * from 24px to 128px and a 1-unit/frame fall would now look sluggish. */
#define VU_FLOOR_LEVEL 3u
#define VU_DECAY_STEP  12u

static unsigned char displayed_level[N_BARS];
static unsigned char bar_filled;
static unsigned char title_shown;
static unsigned char img_bar_filled;
static unsigned char img_bar_started;

static void wait_vbi(void)
{
    unsigned char t = OS.rtclok[2];
    while (OS.rtclok[2] == t)
        ;
}

/* Shared by the song title and the image name below: prints up to
 * SCREEN_COLS bytes centered, in reverse video (ATASCII's bit7-set
 * convention — no special screen mode needed, just the normal E: device),
 * followed by a newline. */
static void print_centered(const unsigned char *text, unsigned char len)
{
    unsigned char pad, i;

    if (len > SCREEN_COLS) len = SCREEN_COLS;   /* clip, don't wrap */
    pad = (SCREEN_COLS - len) / 2;

    for (i = 0; i < pad; i++) putchar(' ');
    for (i = 0; i < len; i++) putchar(text[i] | 0x80);
    putchar('\n');
}

/* Shared by load_progress/image_progress below: grows a one-row, left-to-
 * right bar of reverse-video blocks, one block per SCREEN_COLS-th of
 * loaded/total. *filled tracks how much of THIS bar is already drawn
 * (each caller keeps its own counter) — never needs to erase/redraw,
 * since loaded only ever grows, so the bar only ever gains blocks. */
static void draw_bar(unsigned char *filled, unsigned long loaded, unsigned long total)
{
    unsigned char target = (unsigned char)((loaded * SCREEN_COLS) / total);
    while (*filled < target) {
        putchar(' ' | 0x80);
        (*filled)++;
    }
}

/* Reads the title straight out of the loaded .vtm blob (vtm_format.md:
 * offset 12 = length, offset 13.. = raw ASCII, not null-terminated). */
static void print_title(const void *song)
{
    const unsigned char *buf = (const unsigned char *)song;
    print_centered(buf + 13, buf[12]);
}

/* vtm_load_file()'s progress callback (see vtm.h): fires once as soon as
 * the title is readable (prints it), then again per chunk of pattern
 * data (grows the bar). */
static void load_progress(const void *song, unsigned long loaded, unsigned long total)
{
    if (!title_shown) {
        print_title(song);
        putchar('\n');   /* blank row between title and progress bar */
        title_shown = 1;
    }
    draw_bar(&bar_filled, loaded, total);
}

/* vbm_load_file()'s progress callback (see vbm.h): fires once as soon as
 * the file's embedded name is readable (prints it, same as the song
 * title above), then again per streamed chunk (grows its own bar, on its
 * own row below the song's). */
static void image_progress(const char *name, unsigned char name_len,
                            unsigned long loaded, unsigned long total)
{
    if (!img_bar_started) {
        print_centered((const unsigned char *)name, name_len);
        putchar('\n');   /* blank row between name and progress bar */
        img_bar_started = 1;
    }
    draw_bar(&img_bar_filled, loaded, total);
}

static void vu_tick(void)
{
    unsigned char ch, lvl;
    for (ch = 0; ch < N_BARS; ch++) {
        lvl = vtm_level(ch);
        if (lvl >= displayed_level[ch]) {
            displayed_level[ch] = lvl;
        } else if (displayed_level[ch] > VU_FLOOR_LEVEL + VU_DECAY_STEP) {
            displayed_level[ch] -= VU_DECAY_STEP;
        } else {
            displayed_level[ch] = VU_FLOOR_LEVEL;
        }
        switch (ch) {
            case 0: vu_pm_set0(displayed_level[ch]); break;
            case 1: vu_pm_set1(displayed_level[ch]); break;
            case 2: vu_pm_set2(displayed_level[ch]); break;
            case 3: vu_pm_set3(displayed_level[ch]); break;
        }
    }
}

int main(void)
{
    void *song;
    int rval=0;
    unsigned char ch;
    unsigned char have_image;

    putchar(125);            /* clear screen, home cursor (ATASCII CLEAR) */
    OS.color2 = 0;           /* background: black */
    OS.color1 = 14;          /* text: white */

    vera_require();

    song = vtm_load_file("D1:DEMO.VTM", load_progress);
    if (!song) {
        printf("ERROR: could not load D1:DEMO.VTM\n");
        rval=1;
        goto err;
    }
    putchar('\n');
    putchar('\n');   /* move past the progress bar row */

    if (!vtm_init(song)) {
        printf("ERROR: DEMO.VTM is not a valid VTM3 file\n");
        free(song);
        rval=1;
        goto err;
    }

    /* Artwork is optional: D1:DEMO.VBM (see workflow/01-vera-asset-
     * format.md, tools/img2vbm.py) shows on VERA's own video output —
     * a separate screen from this Atari text/VU-meter display — while
     * the song plays. If it's missing, playback proceeds exactly as
     * before with no VERA display-mode change at all. */
    have_image = vbm_load_file("D1:DEMO.VBM", image_progress);
    if (have_image) {
        putchar('\n');
        putchar('\n');   /* move past the image progress bar row */
        vbm_init();
    }

    CH_REG = CH_NONE;
    for (ch = 0; ch < N_BARS; ch++)
        displayed_level[ch] = VU_FLOOR_LEVEL;

    vu_pm_init();
    while (CH_REG == CH_NONE) {
        wait_vbi();
        vtm_tick();
        vu_tick();
    }
    vu_pm_done();

    if (have_image) vbm_done();

    vtm_stop();
    free(song);

err:
    CH_REG = CH_NONE;
    while (CH_REG == CH_NONE) {
    }

    return rval;
}
