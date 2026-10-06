/* rmtplay.c — RMT player with a visualizer on the Atari screen (ANTIC),
 * in the style of the XEX/SAP export of Raster Music Tracker.
 *
 *   NAME / AUTHOR / DATE of the song (tools/rmtinfo.py: text stored in the
 *   .rmt file, or RMT_NAME= RMT_AUTHOR= RMT_DATE= on the make line)
 *   one volume bar per channel (4 for RMT4, 8 for RMT8): ANTIC mode 4
 *   rows on a black background, coloured row by row by a DLI
 *   AUDF / AUDC of every channel and AUDCTL, in hex
 *   song line, row in the track, speed, play time
 *
 *   SPACE  pause / play          R  restart the song
 *   1 POKEY   2 VERA   3 both    4  hybrid (RMT8: POKEY 1-4, VERA 5-8)
 *   S  VERA stereo on/off        ESC  stop and exit
 *
 * Sound: the same player as TESTRMT (rmtplayr.s + rmtvbi.s + psgrmt.s, POKEY
 * and/or VERA PSG). Standalone: does not need VERA.SYS.
 */

#include <atari.h>
#include <conio.h>
#include <string.h>
#include "vera_detect.h"
#include "rmt.h"
#include "songinfo.h"

/* ---- player state (rmtplayr.s, rmtdli.s, song.s) ---------------------- */

extern volatile unsigned char *rmt_p_song;          /* next song line */
#pragma zpsym ("rmt_p_song")
extern volatile unsigned char rmt_abeat;            /* row in the track */
extern volatile unsigned char rmt_maxtracklen;      /* rows per track */
extern volatile unsigned char rmt_speed;            /* frames per row */
extern volatile unsigned char rmt_chan_audf[RMT_TRACKS];
extern volatile unsigned char rmt_chan_audc[RMT_TRACKS];
extern volatile unsigned char rmt_audctl, rmt_audctl2;
extern const unsigned char rmt_song_end[];

extern void rmt_dli(void);
extern unsigned char rmt_dli_pf0[];
extern unsigned char rmt_dli_count, rmt_dli_idx;

/* ---- screen layout ---------------------------------------------------- */

#define ROW_TITLE   0
#define ROW_NAME    2
#define ROW_AUTHOR  3
#define ROW_DATE    4
#define BAR_TOP     6       /* 8 rows of bars: 64 pixels, 4 per volume step */
#define BAR_ROWS    8
#define ROW_LABELS  (BAR_TOP + BAR_ROWS)
#define ROW_AUDF    16
#define ROW_AUDC    17
#define ROW_AUDCTL  18
#define ROW_POS     20
#define ROW_STATE   21
#define ROW_KEYS1   23

#if RMT_TRACKS > 4
#define BAR_W       3       /* 8 bars: 3 wide, 2 apart */
#define BAR_STEP    5
#define BAR_X0      1
#else
#define BAR_W       6       /* 4 bars: 6 wide, 3 apart */
#define BAR_STEP    9
#define BAR_X0      3
#endif

/* bar glyphs: screen codes $41-$48 (ATASCII ctrl-A..ctrl-H, unused here) */
#define GLYPH_BASE  0x40

#define MODE_POKEY  1
#define MODE_VERA   2
#define MODE_BOTH   3
#define MODE_HYBRID 4

static const char *const mode_name[5] = { "", "POKEY ", "VERA  ", "BOTH  ", "HYBRID" };
static const char hexdig[] = "0123456789ABCDEF";

static unsigned char *scr;
static unsigned char fontbuf[2048];         /* 1 KB font, aligned inside */
static unsigned char bar_px[RMT_TRACKS];    /* height shown, with fall-off */
static unsigned char song_shift;            /* bytes per song line: 1 << shift */
static const unsigned char *song_start;
static unsigned int song_lines;

/* ---- screen helpers --------------------------------------------------- */

static unsigned char screen_code(unsigned char c)
{
    if (c < 32)
        return c + 64;
    if (c < 96)
        return c - 32;
    return c;
}

static void put_text(unsigned char x, unsigned char y, const char *s, unsigned char inv)
{
    unsigned char *p = scr + y * 40 + x;

    while (*s && x < 40) {
        *p++ = screen_code((unsigned char)*s++) | inv;
        x++;
    }
}

static void put_center(unsigned char y, const char *s)
{
    unsigned char n = (unsigned char)strlen(s);

    put_text(n < 40 ? (40 - n) / 2 : 0, y, s, 0);
}

static void put_hex(unsigned char x, unsigned char y, unsigned char v)
{
    unsigned char *p = scr + y * 40 + x;

    p[0] = screen_code(hexdig[v >> 4]);
    p[1] = screen_code(hexdig[v & 15]);
}

static void put_dec(unsigned char x, unsigned char y, unsigned int v, unsigned char digits)
{
    unsigned char *p = scr + y * 40 + x + digits;

    while (digits--) {
        *--p = screen_code('0' + v % 10);
        v /= 10;
    }
}

/* ---- font with the bar glyphs ----------------------------------------- */

static void make_font(void)
{
    unsigned char *font = (unsigned char *)(((unsigned int)fontbuf + 1023) & 0xFC00);
    unsigned char h, r, b;

    memcpy(font, (void *)0xE000, 1024);
    /* glyph h (1..8), drawn in ANTIC mode 4 (4 pixels of 2 bits per byte):
     * the bottom h pixel rows lit, as LED segments of 3 lit rows and 1 dark
     * row (one segment per volume step). Lit row = 3 pixels of colour 01
     * (COLPF0) and 1 of background, so the bar columns stay apart. */
    for (h = 1; h <= 8; h++)
        for (r = 0; r < 8; r++) {
            b = 7 - r;      /* pixel row counted from the bottom */
            font[(GLYPH_BASE + h) * 8 + r] = (b < h && (b & 3) != 3) ? 0x54 : 0x00;
        }
    OS.chbas = (unsigned int)font >> 8;
}

/* ---- DLI colours on the bar rows --------------------------------------- */

static unsigned char *dl_line(unsigned char row)
{
    unsigned char *dl = (unsigned char *)OS.sdlst;

    return dl + (row ? 5 + row : 3);    /* $70 $70 $70 $42 lo hi $02... */
}

/* COLPF0 of each bar row: the bars are ANTIC mode 4 rows, whose background
 * is COLBK (black), so only the bars change colour */
static const unsigned char bar_color[BAR_ROWS] = {
    0x34, 0x36, 0x28, 0x1A, 0xEA, 0xDA, 0xC8, 0xC6     /* top red -> bottom green */
};

static void dli_on(void)
{
    unsigned char i;

    for (i = 0; i < BAR_ROWS; i++)
        rmt_dli_pf0[i] = bar_color[i];
    rmt_dli_count = BAR_ROWS;
    rmt_dli_idx = 0;

    /* bar rows in ANTIC mode 4 (multicolour text, same 40 bytes per row);
     * DLI on the line before each bar row */
    for (i = BAR_TOP; i < BAR_TOP + BAR_ROWS; i++)
        *dl_line(i) = 0x04;
    for (i = BAR_TOP - 1; i < BAR_TOP + BAR_ROWS - 1; i++)
        *dl_line(i) |= 0x80;

    /* start below the bars, so the first DLI is the first of a frame */
    while (ANTIC.vcount < 100)
        ;
    OS.vdslst = rmt_dli;
    ANTIC.nmien = NMIEN_DLI | NMIEN_VBI;
}

static void dli_off(void)
{
    unsigned char i;

    ANTIC.nmien = NMIEN_VBI;
    *dl_line(BAR_TOP - 1) &= 0x7F;
    for (i = BAR_TOP; i < BAR_TOP + BAR_ROWS; i++)
        *dl_line(i) = 0x02;     /* back to the OS text mode */
}

/* ---- static parts of the screen ---------------------------------------- */

static void draw_static(void)
{
    unsigned char i, x;
    char title[41];

    memset(title, ' ', 40);
    title[40] = 0;
    memcpy(title + 1, "RMT PLAYER  POKEY + VERA PSG", 28);
#if RMT_TRACKS > 4
    memcpy(title + 31, "RMT8", 4);
#else
    memcpy(title + 31, "RMT4", 4);
#endif
    memcpy(title + 36, OS.palnts ? "PAL " : "NTSC", 4);
    put_text(0, ROW_TITLE, title, 0x80);

    put_center(ROW_NAME, RMT_INFO_NAME);
    put_center(ROW_AUTHOR, RMT_INFO_AUTHOR);
    put_center(ROW_DATE, RMT_INFO_DATE);

    for (i = 0; i < RMT_TRACKS; i++) {
        x = BAR_X0 + i * BAR_STEP + (BAR_W - 2) / 2;
#if RMT_TRACKS > 4
        scr[ROW_LABELS * 40 + x] = screen_code(i < 4 ? 'L' : 'R');
        scr[ROW_LABELS * 40 + x + 1] = screen_code('1' + (i & 3));
#else
        scr[ROW_LABELS * 40 + x + 1] = screen_code('1' + i);
#endif
    }

    put_text(0, ROW_AUDF, "AUDF", 0);
    put_text(0, ROW_AUDC, "AUDC", 0);
    put_text(0, ROW_AUDCTL, "AUDCTL", 0);
    put_text(0, ROW_POS, "LINE   /      ROW   /     SPEED", 0);
    put_text(0, ROW_STATE, "TIME   :      OUT", 0);
    put_text(0, ROW_KEYS1, "SPC PAUSE R RESTART 123", 0);
#if RMT_TRACKS > 4
    put_text(23, ROW_KEYS1, "4 OUT S ST ESC", 0);
#else
    put_text(23, ROW_KEYS1, " OUT S ST ESC", 0);
#endif
}

/* ---- dynamic parts ----------------------------------------------------- */

static void draw_bars(unsigned char paused)
{
    unsigned char i, r, x, w, h, g, target;
    unsigned char *p;

    for (i = 0; i < RMT_TRACKS; i++) {
        target = paused ? 0 : (unsigned char)((rmt_chan_audc[i] & 0x0F) << 2);
        if (target >= bar_px[i])
            bar_px[i] = target;
        else
            bar_px[i] -= (bar_px[i] - target > 2) ? 2 : bar_px[i] - target;

        h = bar_px[i];
        x = BAR_X0 + i * BAR_STEP;
        for (r = 0; r < BAR_ROWS; r++) {
            /* r = 0 is the bottom row */
            g = h > r * 8 ? (h - r * 8 > 8 ? 8 : h - r * 8) : 0;
            g = g ? GLYPH_BASE + g : 0;
            p = scr + (BAR_TOP + BAR_ROWS - 1 - r) * 40 + x;
            for (w = 0; w < BAR_W; w++)
                p[w] = g;
        }
    }
}

static void draw_regs(void)
{
    unsigned char i, x;

    for (i = 0; i < RMT_TRACKS; i++) {
        x = 5 + i * 3;
#if RMT_TRACKS > 4
        if (i >= 4)
            x += 2;     /* gap between left and right POKEY */
#endif
        put_hex(x, ROW_AUDF, rmt_chan_audf[i]);
        put_hex(x, ROW_AUDC, rmt_chan_audc[i]);
    }
    put_hex(7, ROW_AUDCTL, rmt_audctl);
#if RMT_TRACKS > 4
    put_hex(10, ROW_AUDCTL, rmt_audctl2);
#endif
}

static void draw_position(void)
{
    const unsigned char *a, *b;
    unsigned int line;

    /* the VBI may move the pointer between the two byte reads */
    do {
        a = (const unsigned char *)rmt_p_song;
        b = (const unsigned char *)rmt_p_song;
    } while (a != b);
    line = (unsigned int)(a - song_start) >> song_shift;
    line = line ? line - 1 : 0;     /* p_song already points to the next line */

    put_hex(5, ROW_POS, (unsigned char)line);
    put_hex(8, ROW_POS, (unsigned char)(song_lines - 1));
    put_hex(18, ROW_POS, rmt_abeat);
    put_hex(21, ROW_POS, (unsigned char)(rmt_maxtracklen - 1));
    put_hex(32, ROW_POS, rmt_speed);
}

static void draw_state(unsigned long frames, unsigned char fps, unsigned char mode,
                       unsigned char paused)
{
    unsigned int s = (unsigned int)(frames / fps);

    put_dec(5, ROW_STATE, s / 60, 2);
    put_dec(8, ROW_STATE, s % 60, 2);
    put_text(18, ROW_STATE, mode_name[mode], 0);
    put_text(25, ROW_STATE, psg_stereo ? "STEREO" : "      ", 0);
    put_text(33, ROW_STATE, paused ? "PAUSED" : "      ", 0x80 * paused);
}

static void set_mode(unsigned char mode)
{
    rmt_pokey_mute = (mode == MODE_VERA);
    psg_enable = (mode != MODE_POKEY);
    psg_hybrid = (mode == MODE_HYBRID);
}

/* ---- main --------------------------------------------------------------- */

int main(void)
{
    unsigned char mode = MODE_BOTH;
    unsigned char paused = 0;
    unsigned char fps = OS.palnts ? 50 : 60;
    unsigned char tick, last, k;
    unsigned char old_color1 = OS.color1, old_color2 = OS.color2, old_color4 = OS.color4;
    unsigned char old_chbas = OS.chbas;
    unsigned long frames = 0;

    clrscr();
    vera_require();

#if RMT_TRACKS > 4
    song_shift = 3;     /* 8 bytes per song line */
#else
    song_shift = 2;     /* 4 bytes per song line */
#endif
    song_start = (const unsigned char *)(rmt_song[14] | (rmt_song[15] << 8));
    song_lines = (unsigned int)(rmt_song_end - song_start) >> song_shift;

    OS.crsinh = 1;
    clrscr();
    scr = OS.savmsc;
    OS.color1 = 0x0C;
    OS.color2 = 0x00;
    OS.color4 = 0x00;
    make_font();
    draw_static();

    psg_init();
    set_mode(mode);
    rmt_init(rmt_song);
    rmt_vbi_on();
    dli_on();

    last = OS.rtclok[2];
    for (;;) {
        /* once per frame */
        while ((tick = OS.rtclok[2]) == last)
            ;
        if (!paused)
            frames += (unsigned char)(tick - last);
        last = tick;

        draw_bars(paused);
        draw_regs();
        draw_position();
        draw_state(frames, fps, mode, paused);

        if (!kbhit())
            continue;
        k = cgetc();
        if (k == 27)
            break;
        if (k == ' ') {
            paused = !paused;
            if (paused)
                rmt_vbi_off();      /* silences POKEY and PSG, keeps the position */
            else
                rmt_vbi_on();
        }
        else if (k == 'r' || k == 'R') {
            rmt_vbi_off();
            rmt_init(rmt_song);
            frames = 0;
            paused = 0;
            rmt_vbi_on();
        }
        else if (k >= '1' && k <= '3') {
            mode = (unsigned char)(k - '0');
            set_mode(mode);
        }
#if RMT_TRACKS > 4
        else if (k == '4') {
            mode = MODE_HYBRID;
            set_mode(mode);
        }
#endif
        else if (k == 's' || k == 'S')
            psg_stereo = (unsigned char)!psg_stereo;
    }

    rmt_vbi_off();
    dli_off();
    OS.chbas = old_chbas;
    OS.color1 = old_color1;
    OS.color2 = old_color2;
    OS.color4 = old_color4;
    OS.crsinh = 0;
    clrscr();
    cputs("Music stopped.\r\n");
    return 0;
}
