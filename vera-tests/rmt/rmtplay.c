/* rmtplay.c — RMT player with a visualizer in the style of the XEX/SAP
 * export of Raster Music Tracker, shown at the same time on the Atari screen
 * (ANTIC) and on the VERA screen.
 *
 *   NAME / AUTHOR / DATE of the song (tools/rmtinfo.py: text stored in the
 *   .rmt file, or RMT_NAME= RMT_AUTHOR= RMT_DATE= on the make line); a line
 *   longer than 40 characters scrolls left (coarse, one character at a time)
 *   after 5 seconds of play
 *   one volume bar per channel (4 for RMT4, 8 for RMT8): ANTIC mode 4
 *   rows on a black background, coloured row by row by a DLI
 *   AUDF / AUDC of every channel and AUDCTL, in hex
 *   song line, row in the track, speed, play time
 *
 *   SPACE  pause / play          R  restart the song
 *   1 POKEY   2 VERA   3 both    4  hybrid (RMT8: POKEY 1-4, VERA 5-8)
 *   S  VERA stereo on/off        ESC  stop and exit
 *
 * VERA screen: layer 1, 40x30 text (320x240) in 1bpp tiles with 256-colour
 * foreground (T256C). The Atari font goes into VRAM (with inverse copies in
 * 128-255 and the bar glyphs), the same screen codes are written to both
 * screens; the bar rows get a red..green colour each. Map at $00000 and font
 * at $01000, away from the PBI ROM screen ($1B000/$1F000): on exit the layer
 * registers are put back and the ROM screen shows again.
 *
 * Build option RMT_SCREEN (Makefile) chooses the screens: RMTPLAY_ANTIC and
 * RMTPLAY_VERA, both 1 by default. With VERA only the ANTIC display DMA is
 * turned off (black Atari screen, more CPU time for the player).
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

#ifndef RMTPLAY_ANTIC
#define RMTPLAY_ANTIC 1
#endif
#ifndef RMTPLAY_VERA
#define RMTPLAY_VERA 1
#endif
#if !RMTPLAY_ANTIC && !RMTPLAY_VERA
#error "RMTPLAY needs at least one screen (RMTPLAY_ANTIC or RMTPLAY_VERA)"
#endif

/* ---- VERA ----------------------------------------------------------------- */

#define VREG(n)         (*(volatile unsigned char *)(0xD100 + (n)))
#define VERA_ADDR_L     VREG(0x00)
#define VERA_ADDR_M     VREG(0x01)
#define VERA_ADDR_H     VREG(0x02)
#define VERA_DATA0      VREG(0x03)
#define VERA_CTRL       VREG(0x05)
#define VERA_DC_VIDEO   VREG(0x09)
#define VERA_DC_HSCALE  VREG(0x0A)
#define VERA_DC_VSCALE  VREG(0x0B)
#define VERA_DC_BORDER  VREG(0x0C)
#define VERA_L1_FIRST   0x14            /* CONFIG MAPBASE TILEBASE HSCROLL VSCROLL */
#define VERA_L1_COUNT   7
#define PBI_SELECT      (*(volatile unsigned char *)0xD1FF)

#define VERA_INC1       0x10
#define VERA_INC2       0x20

#define VMAP            0x0000u         /* 64x32 map, 128 bytes per row */
#define VFONT           0x1000u         /* 256 glyphs of 8 bytes */
#define VPAL_BANK       1               /* palette at $1FA00 */
#define VPAL            0xFA00u
#define V_ROW_OFS       3               /* 24 ANTIC rows centred in 30 */

#define L1_CONFIG_VAL   0x18            /* map 64x32, T256C, 1bpp tiles */
#define L1_MAPBASE_VAL  (VMAP >> 9)
#define L1_TILEBASE_VAL ((VFONT >> 11) << 2)   /* 8x8 tiles */

/* palette entries used (default palette 0 = black stays the background) */
#define COL_TEXT        0x21
#define COL_HEAD        0x22
#define COL_BAR         0x28            /* + bar row, 0 = top */

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
static unsigned char *font;                 /* ANTIC font (CHBAS) */
static unsigned char bar_px[RMT_TRACKS];    /* height shown, with fall-off */
static unsigned char bar_shown[RMT_TRACKS]; /* height drawn on screen */
static unsigned char line_buf[40];
#if RMTPLAY_VERA
static unsigned char vera_saved[4 + VERA_L1_COUNT];
#endif

/* value shown in each field: a field is written again only when it changes
 * (VERA writes cost CPU time; $FFFF = not drawn yet) */
enum {
    F_AUDF = 0, F_AUDC = 8, F_CTL = 16, F_LINE = 18, F_LINES, F_ROW, F_ROWS,
    F_SPEED, F_SEC, F_MODE, F_STEREO, F_PAUSE, F_COUNT
};
static unsigned int shown[F_COUNT];

static unsigned char changed(unsigned char f, unsigned int v)
{
    if (shown[f] == v)
        return 0;
    shown[f] = v;
    return 1;
}
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

#if RMTPLAY_VERA
static void vera_seek(unsigned char bank, unsigned int addr, unsigned char inc)
{
    VERA_ADDR_L = (unsigned char)addr;
    VERA_ADDR_M = (unsigned char)(addr >> 8);
    VERA_ADDR_H = inc | bank;
}

#endif

/* Every screen write goes through these two: the same screen codes on the
 * ANTIC screen and in the VERA map (characters only, step 2: the colours of
 * the map are set once). */
static void put_codes(unsigned char x, unsigned char y, const unsigned char *c, unsigned char n)
{
#if RMTPLAY_ANTIC
    memcpy(scr + y * 40 + x, c, n);
#endif
#if RMTPLAY_VERA
    {
        unsigned char i;

        vera_seek(0, VMAP + (y + V_ROW_OFS) * 128 + x * 2, VERA_INC2);
        for (i = 0; i < n; i++)
            VERA_DATA0 = c[i];
    }
#endif
}

static void fill_codes(unsigned char x, unsigned char y, unsigned char c, unsigned char n)
{
#if RMTPLAY_ANTIC
    memset(scr + y * 40 + x, c, n);
#endif
#if RMTPLAY_VERA
    {
        unsigned char i;

        vera_seek(0, VMAP + (y + V_ROW_OFS) * 128 + x * 2, VERA_INC2);
        for (i = 0; i < n; i++)
            VERA_DATA0 = c;
    }
#endif
}

static void put_text(unsigned char x, unsigned char y, const char *s, unsigned char inv)
{
    unsigned char n = 0;

    while (s[n] && x + n < 40) {
        line_buf[n] = screen_code((unsigned char)s[n]) | inv;
        n++;
    }
    put_codes(x, y, line_buf, n);
}

static void put_center(unsigned char y, const char *s)
{
    unsigned char n = (unsigned char)strlen(s);

    put_text(n < 40 ? (40 - n) / 2 : 0, y, s, 0);
}

static void put_hex(unsigned char x, unsigned char y, unsigned char v)
{
    line_buf[0] = screen_code(hexdig[v >> 4]);
    line_buf[1] = screen_code(hexdig[v & 15]);
    put_codes(x, y, line_buf, 2);
}

static void put_dec(unsigned char x, unsigned char y, unsigned int v, unsigned char digits)
{
    unsigned char i = digits;

    while (i--) {
        line_buf[i] = screen_code('0' + v % 10);
        v /= 10;
    }
    put_codes(x, y, line_buf, digits);
}

/* ---- font with the bar glyphs ----------------------------------------- */

static void make_font(void)
{
    unsigned char h, r, b;

    font = (unsigned char *)(((unsigned int)fontbuf + 1023) & 0xFC00);
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
#if RMTPLAY_ANTIC
    OS.chbas = (unsigned int)font >> 8;
#endif
}

/* ---- VERA screen -------------------------------------------------------- */

#if RMTPLAY_VERA
static void vera_color(unsigned char idx, unsigned char r, unsigned char g, unsigned char b)
{
    vera_seek(VPAL_BANK, VPAL + idx * 2, VERA_INC1);
    VERA_DATA0 = (unsigned char)((g << 4) | b);
    VERA_DATA0 = r;
}

/* red at the top, yellow in the middle, green at the bottom (4-bit RGB) */
static const unsigned char bar_rgb[BAR_ROWS][3] = {
    { 15, 2, 2 }, { 15, 6, 0 }, { 15, 10, 0 }, { 15, 14, 0 },
    { 11, 15, 0 }, { 7, 14, 0 }, { 3, 13, 0 }, { 0, 12, 0 }
};

static void vera_screen_on(void)
{
    unsigned int i;
    unsigned char c, r, b, attr;

    PBI_SELECT = 0x80;
    VERA_CTRL = 0;
    vera_saved[0] = VERA_DC_VIDEO;
    vera_saved[1] = VERA_DC_HSCALE;
    vera_saved[2] = VERA_DC_VSCALE;
    vera_saved[3] = VERA_DC_BORDER;
    for (c = 0; c < VERA_L1_COUNT; c++)
        vera_saved[4 + c] = VREG(VERA_L1_FIRST + c);

    /* font: the ANTIC one (bar glyphs redrawn for 1bpp), then inverse copies */
    vera_seek(0, VFONT, VERA_INC1);
    for (i = 0; i < 1024; i++) {
        c = font[i];
        if (i >= (GLYPH_BASE + 1) * 8 && i < (GLYPH_BASE + 9) * 8) {
            /* same LED segments as the ANTIC glyphs: 6 pixels lit, 2 dark */
            r = (unsigned char)(i & 7);
            b = 7 - r;
            c = ((b < (i >> 3) - GLYPH_BASE) && (b & 3) != 3) ? 0xFC : 0x00;
        }
        VERA_DATA0 = c;
    }
    for (i = 0; i < 1024; i++)
        VERA_DATA0 = (unsigned char)~font[i];

    vera_color(COL_TEXT, 12, 12, 12);
    vera_color(COL_HEAD, 6, 10, 15);
    for (r = 0; r < BAR_ROWS; r++)
        vera_color(COL_BAR + r, bar_rgb[r][0], bar_rgb[r][1], bar_rgb[r][2]);

    /* map: blank, with the colour of each row */
    vera_seek(0, VMAP, VERA_INC1);
    for (r = 0; r < 32; r++) {
        attr = COL_TEXT;
        if (r == ROW_TITLE + V_ROW_OFS)
            attr = COL_HEAD;
        else if (r >= BAR_TOP + V_ROW_OFS && r < BAR_TOP + BAR_ROWS + V_ROW_OFS)
            attr = COL_BAR + (r - BAR_TOP - V_ROW_OFS);
        for (c = 0; c < 64; c++) {
            VERA_DATA0 = 0;
            VERA_DATA0 = attr;
        }
    }

    VREG(VERA_L1_FIRST + 0) = L1_CONFIG_VAL;
    VREG(VERA_L1_FIRST + 1) = L1_MAPBASE_VAL;
    VREG(VERA_L1_FIRST + 2) = L1_TILEBASE_VAL;
    for (c = 3; c < VERA_L1_COUNT; c++)
        VREG(VERA_L1_FIRST + c) = 0;        /* no scroll */
    VERA_DC_HSCALE = 64;                    /* 320x240 */
    VERA_DC_VSCALE = 64;
    VERA_DC_BORDER = 0;
    VERA_DC_VIDEO = (vera_saved[0] & 0x0F) | 0x20;   /* layer 1 only */
}

static void vera_screen_off(void)
{
    unsigned char c;

    VERA_CTRL = 0;
    VERA_DC_VIDEO = vera_saved[0];
    VERA_DC_HSCALE = vera_saved[1];
    VERA_DC_VSCALE = vera_saved[2];
    VERA_DC_BORDER = vera_saved[3];
    for (c = 0; c < VERA_L1_COUNT; c++)
        VREG(VERA_L1_FIRST + c) = vera_saved[4 + c];
}

#endif

/* ---- DLI colours on the bar rows --------------------------------------- */

#if RMTPLAY_ANTIC
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

#endif

/* ---- static parts of the screen ---------------------------------------- */

#define SCROLL_WAIT     5       /* seconds of play before a long line scrolls */
#define SCROLL_SHIFT    3       /* one character every 8 frames */
#define SCROLL_GAP      40      /* blanks between the end and the start again:
                                 * a whole row, so the text leaves the screen
                                 * before it comes back in from the right */
#define SCROLL_SEEK     3       /* VERA writes of a new address: unchanged
                                 * gaps up to this long are written over */

/* NAME, AUTHOR, DATE: on rows ROW_NAME, ROW_NAME + 1, ROW_NAME + 2 */
static const char *const info_text[3] = { RMT_INFO_NAME, RMT_INFO_AUTHOR, RMT_INFO_DATE };
static unsigned char info_len[3];
static unsigned char info_shown[3];         /* scroll offset drawn */
static unsigned char info_cells[3][40];     /* screen codes on the row */

/* the 40 cells of a long info line, from scroll offset k */
static void info_render(unsigned char i, unsigned char k, unsigned char *buf)
{
    const char *s = info_text[i];
    unsigned char len = info_len[i];
    unsigned char period = len + SCROLL_GAP;
    unsigned char n;

    for (n = 0; n < 40; n++) {
        buf[n] = k < len ? screen_code((unsigned char)s[k]) : 0;
        if (++k == period)
            k = 0;
    }
}

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

    /* a line longer than the screen shows its first 40 characters until
     * draw_info() starts scrolling it */
    for (i = 0; i < 3; i++) {
        info_len[i] = (unsigned char)strlen(info_text[i]);
        if (info_len[i] <= 40) {
            put_center(ROW_NAME + i, info_text[i]);
            continue;
        }
        info_shown[i] = 0;
        info_render(i, 0, info_cells[i]);
        put_codes(0, ROW_NAME + i, info_cells[i], 40);
    }

    for (i = 0; i < RMT_TRACKS; i++) {
        x = BAR_X0 + i * BAR_STEP + (BAR_W - 2) / 2;
#if RMT_TRACKS > 4
        line_buf[0] = screen_code(i < 4 ? 'L' : 'R');
        line_buf[1] = screen_code('1' + (i & 3));
        put_codes(x, ROW_LABELS, line_buf, 2);
#else
        line_buf[0] = screen_code('1' + i);
        put_codes(x + 1, ROW_LABELS, line_buf, 1);
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

/* Coarse scroll of the info lines longer than 40 characters: the text and
 * SCROLL_GAP blanks go round in the 40 columns of the row. The offset comes
 * from the play time, so it stops in pause and starts again from 0 (after
 * the wait) when the song is restarted.
 *
 * Only the cells that change are written (info_cells[] keeps the row): no
 * writes while the row is blank, none for repeated characters. Changed cells
 * close together go out as one run, so VERA gets a new address only when
 * the unchanged gap is longer than the SCROLL_SEEK writes of a seek. */
static void draw_info(unsigned long frames, unsigned char fps)
{
    unsigned int wait = fps * SCROLL_WAIT;
    unsigned int step;
    unsigned char i, k, x, start, end;
    unsigned char *old;

    step = frames < wait ? 0 : (unsigned int)((frames - wait) >> SCROLL_SHIFT);
    for (i = 0; i < 3; i++) {
        if (info_len[i] <= 40)
            continue;
        k = (unsigned char)(step % (unsigned char)(info_len[i] + SCROLL_GAP));
        if (k == info_shown[i])
            continue;
        info_shown[i] = k;
        info_render(i, k, line_buf);
        old = info_cells[i];
        x = 0;
        while (x < 40) {
            if (line_buf[x] == old[x]) {
                x++;
                continue;
            }
            /* run from the first changed cell to the last changed one
             * before a gap longer than SCROLL_SEEK */
            start = x;
            end = ++x;
            for (; x < 40; x++) {
                if (line_buf[x] != old[x])
                    end = x + 1;
                else if (x - end >= SCROLL_SEEK)
                    break;
            }
            put_codes(start, ROW_NAME + i, line_buf + start, end - start);
            memcpy(old + start, line_buf + start, end - start);
        }
    }
}

static void draw_bars(unsigned char paused)
{
    unsigned char i, r, x, h, g, target;

    for (i = 0; i < RMT_TRACKS; i++) {
        target = paused ? 0 : (unsigned char)((rmt_chan_audc[i] & 0x0F) << 2);
        if (target >= bar_px[i])
            bar_px[i] = target;
        else
            bar_px[i] -= (bar_px[i] - target > 2) ? 2 : bar_px[i] - target;

        h = bar_px[i];
        if (h == bar_shown[i])
            continue;       /* VERA writes are slow: only bars that moved */
        bar_shown[i] = h;
        x = BAR_X0 + i * BAR_STEP;
        for (r = 0; r < BAR_ROWS; r++) {
            /* r = 0 is the bottom row */
            g = h > r * 8 ? (h - r * 8 > 8 ? 8 : h - r * 8) : 0;
            g = g ? GLYPH_BASE + g : 0;
            fill_codes(x, BAR_TOP + BAR_ROWS - 1 - r, g, BAR_W);
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
        if (changed(F_AUDF + i, rmt_chan_audf[i]))
            put_hex(x, ROW_AUDF, rmt_chan_audf[i]);
        if (changed(F_AUDC + i, rmt_chan_audc[i]))
            put_hex(x, ROW_AUDC, rmt_chan_audc[i]);
    }
    if (changed(F_CTL, rmt_audctl))
        put_hex(7, ROW_AUDCTL, rmt_audctl);
#if RMT_TRACKS > 4
    if (changed(F_CTL + 1, rmt_audctl2))
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

    if (changed(F_LINE, (unsigned char)line))
        put_hex(5, ROW_POS, (unsigned char)line);
    if (changed(F_LINES, (unsigned char)(song_lines - 1)))
        put_hex(8, ROW_POS, (unsigned char)(song_lines - 1));
    if (changed(F_ROW, rmt_abeat))
        put_hex(18, ROW_POS, rmt_abeat);
    if (changed(F_ROWS, (unsigned char)(rmt_maxtracklen - 1)))
        put_hex(21, ROW_POS, (unsigned char)(rmt_maxtracklen - 1));
    if (changed(F_SPEED, rmt_speed))
        put_hex(32, ROW_POS, rmt_speed);
}

static void draw_state(unsigned long frames, unsigned char fps, unsigned char mode,
                       unsigned char paused)
{
    unsigned int s = (unsigned int)(frames / fps);

    if (changed(F_SEC, s)) {
        put_dec(5, ROW_STATE, s / 60, 2);
        put_dec(8, ROW_STATE, s % 60, 2);
    }
    if (changed(F_MODE, mode))
        put_text(18, ROW_STATE, mode_name[mode], 0);
    if (changed(F_STEREO, psg_stereo))
        put_text(25, ROW_STATE, psg_stereo ? "STEREO" : "      ", 0);
    if (changed(F_PAUSE, paused))
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
    unsigned char old_sdmctl = OS.sdmctl;
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
    memset(shown, 0xFF, sizeof shown);
    make_font();            /* the VERA font is made from it too */
#if RMTPLAY_VERA
    vera_screen_on();
#endif
#if !RMTPLAY_ANTIC
    OS.sdmctl = 0;          /* VERA only: no ANTIC display DMA */
#endif
    draw_static();

    psg_init();
    set_mode(mode);
    rmt_init(rmt_song);
    rmt_vbi_on();
#if RMTPLAY_ANTIC
    dli_on();
#endif

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
        draw_info(frames, fps);

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
#if RMTPLAY_ANTIC
    dli_off();
#endif
#if RMTPLAY_VERA
    vera_screen_off();
#endif
    OS.sdmctl = old_sdmctl;
    OS.chbas = old_chbas;
    OS.color1 = old_color1;
    OS.color2 = old_color2;
    OS.color4 = old_color4;
    OS.crsinh = 0;
    clrscr();
    cputs("Music stopped.\r\n");
    return 0;
}
