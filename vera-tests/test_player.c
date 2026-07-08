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
 * Build: standalone, no VERA.SYS needed (PSG registers are poked directly,
 * same as test_matrix.c pokes VERA video registers directly).
 */
#include <stdio.h>
#include <stdlib.h>
#include <atari.h>
#include "vera_detect.h"
#include "vtm.h"
#include "vu_pm.h"

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

static void wait_vbi(void)
{
    unsigned char t = OS.rtclok[2];
    while (OS.rtclok[2] == t)
        ;
}

/* Reads the title straight out of the loaded .vtm blob (vtm_format.md:
 * offset 12 = length, offset 13.. = raw ASCII, not null-terminated) and
 * prints it centered, in reverse video (ATASCII's bit7-set convention —
 * no special screen mode needed, just the normal E: device). */
static void print_title(const void *song)
{
    const unsigned char *buf = (const unsigned char *)song;
    unsigned char len = buf[12];
    unsigned char pad, i;

    if (len > SCREEN_COLS) len = SCREEN_COLS;   /* clip, don't wrap */
    pad = (SCREEN_COLS - len) / 2;

    for (i = 0; i < pad; i++) putchar(' ');
    for (i = 0; i < len; i++) putchar(buf[13 + i] | 0x80);
    putchar('\n');
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

    putchar(125);            /* clear screen, home cursor (ATASCII CLEAR) */
    OS.color2 = 0;           /* background: black */
    OS.color1 = 14;          /* text: white */

    vera_require();

    song = vtm_load_file("D1:DEMO.VTM");
    if (!song) {
        printf("ERROR: could not load D1:DEMO.VTM\n");
        rval=1;
        goto err;
    }

    if (!vtm_init(song)) {
        printf("ERROR: DEMO.VTM is not a valid VTM1 file\n");
        free(song);
        rval=1;
        goto err;
    }

    print_title(song);

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

    vtm_stop();
    free(song);

err:
    CH_REG = CH_NONE;
    while (CH_REG == CH_NONE) {
    }

    return rval;
}
