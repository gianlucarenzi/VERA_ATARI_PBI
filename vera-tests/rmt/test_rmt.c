/* test_rmt.c — RMT music on POKEY and/or the VERA PSG.
 *
 * The Atari RMT player (rmtplayr.s, -D RMT_VERA) runs on the immediate VBI
 * (rmtvbi.s) and keeps writing POKEY; psgrmt.s turns the same four channels
 * into VERA PSG voices 0-3 every frame. The output is switched live:
 *
 *   1  POKEY only     2  VERA only     3  both     S  VERA stereo on/off
 *   ESC  stop and exit
 *
 * The song is linked in at build time (Makefile: RMT_SONG=..., converted by
 * tools/rmt2ca65.py). Standalone: does not need VERA.SYS.
 */

#include <stdio.h>
#include <conio.h>
#include <atari.h>
#include "vera_detect.h"
#include "rmt.h"

#ifndef RMT_SONG_NAME
#define RMT_SONG_NAME "?"
#endif

#define MODE_POKEY 1
#define MODE_VERA  2
#define MODE_BOTH  3

static const char *const mode_name[4] = { "", "POKEY", "VERA ", "BOTH " };

static void set_mode(unsigned char mode)
{
    rmt_pokey_mute = (mode == MODE_VERA);
    psg_enable = (mode != MODE_POKEY);
}

int main(void)
{
    unsigned char mode = MODE_BOTH;
    unsigned char i, k;

    clrscr();
    cputs("RMT player: POKEY + VERA PSG\r\n");
    cputs("============================\r\n");
    vera_require();
    cprintf("Song: %s  (%s)\r\n\r\n", RMT_SONG_NAME, OS.palnts ? "PAL" : "NTSC");
    cputs("1 POKEY  2 VERA  3 both\r\n");
    cputs("S VERA stereo    ESC exit\r\n");

    psg_init();
    set_mode(mode);
    cprintf("\r\nInstrument speed: %u\r\n", rmt_init(rmt_song));
    rmt_vbi_on();

    for (;;) {
        gotoxy(0, 10);
        cprintf("Output: %s  Stereo: %s", mode_name[mode], psg_stereo ? "on " : "off");
        gotoxy(0, 12);
        cprintf("Frames %5u  player %3u/%3u lines", rmt_frames, rmt_lines, rmt_maxlines);
        gotoxy(0, 14);
        cputs("POKEY vol:");
        for (i = 0; i < 4; i++)
            cprintf(" %2u", rmt_audc[i] & 0x0F);
        gotoxy(0, 15);
        cputs("VERA  lvl:");
        for (i = 0; i < 4; i++)
            cprintf(" %2u", psg_volume[i]);

        if (!kbhit())
            continue;
        k = cgetc();
        if (k == 27)
            break;
        if (k >= '1' && k <= '3') {
            mode = (unsigned char)(k - '0');
            set_mode(mode);
        }
        else if (k == 's' || k == 'S')
            psg_stereo = (unsigned char)!psg_stereo;
    }

    rmt_vbi_off();          /* silences POKEY and the PSG voices */
    clrscr();
    cputs("Music stopped.\r\n");
    return 0;
}
