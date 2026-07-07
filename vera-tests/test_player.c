/* test_player.c — VTM PSG music player demo for the VERA PBI card.
 *
 * Loads DEMO.VTM from the current default drive/directory and plays it
 * on 4 VERA PSG voices, ticked once per vertical blank (same poll-loop
 * idiom as test_matrix.c). Press any key to stop.
 *
 * Build: standalone, no VERA.SYS needed (PSG registers are poked directly,
 * same as test_matrix.c pokes VERA video registers directly).
 */
#include <stdio.h>
#include <stdlib.h>
#include <atari.h>
#include "vera_detect.h"
#include "vtm.h"

#define CH_NONE 255
#define CH_REG (*(volatile unsigned char *)0x02FC)

static void wait_vbi(void)
{
    unsigned char t = OS.rtclok[2];
    while (OS.rtclok[2] == t)
        ;
}

int main(void)
{
    void *song;
    int rval=0;

    printf("VTM Player\n");
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

    printf("Playing... press any key to stop.\n");
    CH_REG = CH_NONE;
    while (CH_REG == CH_NONE) {
        wait_vbi();
        vtm_tick();
    }

    vtm_stop();
    free(song);

err:
    printf("Press any key to DOS.\n");
    CH_REG = CH_NONE;
    while (CH_REG == CH_NONE) {
    }

    return rval;
}
