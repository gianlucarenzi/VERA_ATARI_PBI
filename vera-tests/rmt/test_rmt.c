/* test_rmt.c — RMT music on POKEY and/or the VERA PSG.
 *
 * The Atari RMT player (rmtplayr.s, -D RMT_VERA) runs on the VBI
 * (rmtvbi.s: deferred VBI, immediate while SIO has CRITIC set) and keeps writing POKEY; psgrmt.s turns the same channels into
 * VERA PSG voices every frame. RMT4 modules (mono, 4 channels) use voices
 * 0-3; RMT8 modules (stereo, made for two POKEYs) use voices 0-7: channels
 * 1-4 also play on the real POKEY, 5-8 exist only on VERA.
 *
 *   1  POKEY only     2  VERA only     3  both
 *   4  hybrid: POKEY channels 1-4, VERA channels 5-8 (RMT8)
 *   S  VERA stereo on/off              ESC  stop and exit
 *
 * The song is linked in at build time (Makefile: RMT_SONG=..., converted by
 * tools/rmt2ca65.py; RMT_TRACKS follows the module). Standalone: does not
 * need VERA.SYS.
 */

#include <stdio.h>
#include <conio.h>
#include <atari.h>
#include "vera_detect.h"
#include "rmt.h"

#ifndef RMT_SONG_NAME
#define RMT_SONG_NAME "?"
#endif

#define MODE_POKEY  1
#define MODE_VERA   2
#define MODE_BOTH   3
#define MODE_HYBRID 4

static const char *const mode_name[5] = { "", "POKEY ", "VERA  ", "BOTH  ", "HYBRID" };

static void set_mode(unsigned char mode)
{
    rmt_pokey_mute = (mode == MODE_VERA);
    psg_enable = (mode != MODE_POKEY);
    psg_hybrid = (mode == MODE_HYBRID);
}

static void show_levels(unsigned char row, const char *label, unsigned char first)
{
    unsigned char i;

    gotoxy(0, row);
    cputs(label);
    for (i = first; i < first + 4; i++)
        cprintf(" %2u", rmt_audc[i] & 0x0F);
    cputs("  VERA");
    for (i = first; i < first + 4; i++)
        cprintf(" %2u", psg_volume[i]);
}

int main(void)
{
    unsigned char mode = MODE_BOTH;
    unsigned char k;

    clrscr();
    cputs("RMT player: POKEY + VERA PSG\r\n");
    cputs("============================\r\n");
    vera_require();
    cprintf("Song: %s  RMT%u  %s\r\n\r\n", RMT_SONG_NAME, (unsigned int)rmt_tracks,
            OS.palnts ? "PAL" : "NTSC");
    cputs("1 POKEY  2 VERA  3 both\r\n");
    cputs(rmt_tracks > 4 ? "4 hybrid (POKEY 1-4, VERA 5-8)\r\n" : "\r\n");
    cputs("S VERA stereo    ESC exit\r\n");

    psg_init();
    set_mode(mode);
    cprintf("\r\nInstrument speed: %u\r\n", rmt_init(rmt_song));
    rmt_vbi_on();

    for (;;) {
        gotoxy(0, 11);
        cprintf("Output: %s  Stereo: %s", mode_name[mode], psg_stereo ? "on " : "off");
        gotoxy(0, 13);
        cprintf("Frames %5u  player %3u/%3u lines", rmt_frames, rmt_lines, rmt_maxlines);
        gotoxy(0, 14);
        cprintf("Deferred %5u  dropped %5u", rmt_deferred, rmt_dropped);
        show_levels(16, rmt_tracks > 4 ? "L POKEY" : "  POKEY", 0);
#if RMT_TRACKS > 4
        show_levels(17, "R (n/a)", 4);
#endif

        if (!kbhit())
            continue;
        k = cgetc();
        if (k == 27)
            break;
        if (k >= '1' && k <= '3') {
            mode = (unsigned char)(k - '0');
            set_mode(mode);
        }
        else if (k == '4' && rmt_tracks > 4) {
            mode = MODE_HYBRID;
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
