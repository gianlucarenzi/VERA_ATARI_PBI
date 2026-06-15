/* test_matrix.c — Digital-rain screensaver for VERA PBI (bundled driver).
 *
 * Requires VERA.SYS bundled (see Makefile: make_test_variants TESTMTX).
 * Screen dimensions are detected at runtime from DC_HSCALE / DC_VSCALE,
 * so the same source works for all three driver configurations.
 *
 * Algorithm: each column runs one vertical "string drop" at a time.
 * The head advances TWO rows per VBI frame.  Four colour-transition rows
 * are written per step, keeping PBI bus traffic to ≈8 × screen_width
 * writes per frame (2 steps × 4 writes).
 *
 *   head        → white  (COL_WHITE)
 *   head – 1    → white→light-green transition
 *   head – ZONE_LIGHT  → light-green→dark-green transition
 *   head – ZONE_TOTAL  → dark-green→black (clear)
 *
 * PRNG: 8-bit Galois LFSR (~15 cycles) instead of cc65 rand() (~100 cycles).
 *
 * Build: bundled via make_test_variants in the Makefile.
 */

#include <stdio.h>
#include <atari.h>
#include "vera_detect.h"

/* --- VERA PBI register block at $D100 (DCSEL=0) --- */
#define VERA_ADDR_LO  (*(volatile unsigned char*)0xD100)
#define VERA_ADDR_MID (*(volatile unsigned char*)0xD101)
#define VERA_ADDR_HI  (*(volatile unsigned char*)0xD102)
#define VERA_DATA0    (*(volatile unsigned char*)0xD103)
#define VERA_CTRL     (*(volatile unsigned char*)0xD105)
#define DC_HSCALE     (*(volatile unsigned char*)0xD10A)
#define DC_VSCALE     (*(volatile unsigned char*)0xD10B)
#define DC_BORDER     (*(volatile unsigned char*)0xD10C)

/* PBI device select: $80 = VERA on, $00 = VERA off */
#define PBI_SELECT    (*(volatile unsigned char*)0xD1FF)
#define VERA_ON()     (PBI_SELECT = 0x80)
#define VERA_OFF()    (PBI_SELECT = 0x00)

/* Tilemap: Layer 1 at SCREEN_ADDR=$01B000 (vera_common.inc).
 * Row stride = 128 cells × 2 bytes = 256 bytes.
 * All VRAM addresses are in bank 1, so ADDR_HI bit 0 is always 1. */
#define VRAM_MAP     0x01B000UL
#define ROW_STRIDE   256u
#define ADDR_H_INC1  0x11   /* inc=+1, bank=1 */
#define ADDR_H_NOINC 0x01   /* inc=0,  bank=1 */

/* We program the VERA palette ourselves so colours are independent of
 * whatever VERA.SYS loaded.  Palette VRAM starts at $1FA00 (bank 1).
 * Entry format: byte0 = (green4<<4)|blue4, byte1 = red4.
 *
 * VERA.SYS VBI handler resets entry 0 to its own colour (blue) every
 * frame, so we use entry 4 as the cell background (black).  Entries
 * 1-3 are not touched by VERA.SYS and hold our green shades.
 *
 * Index  Name     #rrggbb   byte0  byte1
 *   0    (VERA.SYS-owned, we set it but it may be overridden)
 *   1    white    #FFFFFF    0xFF   0x0F
 *   2    lt-grn   #00FF00    0xF0   0x00
 *   3    dk-grn   #004400    0x40   0x00
 *   4    black    #000000    0x00   0x00   ← cell bg (safe slot)
 */
#define VERA_PAL_BASE  0x1FA00UL
#define COL_BG_IDX   4           /* palette index used for black cell background */

/* Tilemap colour byte = (bg_nibble << 4) | fg_nibble.
 * bg nibble = COL_BG_IDX = 4 for all cells so cell backgrounds are
 * palette[4] = black, avoiding palette[0] which VERA.SYS resets. */
#define COL_BLACK    0x44   /* space: bg=4(blk), fg=4(blk) */
#define COL_WHITE    0x41   /* head:  bg=4(blk), fg=1(wht) */
#define COL_LGRE     0x42   /* zone:  bg=4(blk), fg=2(lgr) */
#define COL_DGRE     0x43   /* zone:  bg=4(blk), fg=3(dgr) */

/* Trail geometry (rows behind the white head) */
#define ZONE_LIGHT   5
#define ZONE_DARK    8
#define ZONE_TOTAL   (ZONE_LIGHT + ZONE_DARK)

/* Steps per VBI frame: 2 = double visual speed */
#define STEPS_PER_FRAME 2

/* Spawn: fast_rand() < SPAWN_THRESH ≈ 13/256 ≈ 5 % */
#define SPAWN_THRESH 13

/* Atari hardware sources for seeding */
#define RTCLOK_L     (*(volatile unsigned char*)0x0014)
#define POKEY_RANDOM (*(volatile unsigned char*)0xD20A)

/* ANTIC DMA control: zero both HW reg and shadow to blank native display.
 * The OS immediate-VBI copies SDMCTL→DMACTL every frame, so SDMCTL must
 * be cleared too or ANTIC re-enables itself on the next VBI. */
#define DMACTL  (*(volatile unsigned char*)0xD400)
#define SDMCTL  (*(volatile unsigned char*)0x022F)

#define MAX_COLS  80

static int           screen_width  = 80;
static int           screen_height = 30;

static unsigned char col_head[MAX_COLS];
static unsigned char col_active[MAX_COLS];

/* ------------------------------------------------------------------ */
/* 8-bit Galois LFSR — ~15 cycles vs ~100 for cc65 rand()             */
/* Polynomial x^8+x^6+x^5+x^2+1 = 0xB4 (maximal-length, period 255) */
/* ------------------------------------------------------------------ */
static unsigned char prng8;

static unsigned char fast_rand(void)
{
    unsigned char lsb = prng8 & 1;
    prng8 >>= 1;
    if (lsb) prng8 ^= 0xB4;
    return prng8;
}

/* ------------------------------------------------------------------ */
/* Palette setup — call with VERA already selected                     */
/* ------------------------------------------------------------------ */

static void setup_matrix_palette(void)
{
    /* Write 5 consecutive palette entries starting at $1FA00.
     * Auto-increment=1 streams all 10 bytes in one sequential burst. */
    VERA_CTRL     = 0;       /* ensure DCSEL=0 */
    VERA_ADDR_LO  = 0x00;
    VERA_ADDR_MID = 0xFA;
    VERA_ADDR_HI  = ADDR_H_INC1;   /* bank=1, inc=+1 */
    VERA_DATA0 = 0x00; VERA_DATA0 = 0x00;   /* 0: (VERA.SYS may override) */
    VERA_DATA0 = 0xFF; VERA_DATA0 = 0x0F;   /* 1: white    #FFFFFF */
    VERA_DATA0 = 0xF0; VERA_DATA0 = 0x00;   /* 2: lt-grn   #00FF00 */
    VERA_DATA0 = 0x40; VERA_DATA0 = 0x00;   /* 3: dk-grn   #004400 */
    VERA_DATA0 = 0x00; VERA_DATA0 = 0x00;   /* 4: black    #000000 (cell bg) */

    /* DC_BORDER (DCSEL=0, $D10C): palette index for the display border/
     * background area outside the tilemap.  Point it to our black entry. */
    DC_BORDER = COL_BG_IDX;
}

/* ------------------------------------------------------------------ */
/* Low-level VERA helpers (call with VERA already selected)            */
/* ------------------------------------------------------------------ */

static void vera_put_char(int x, int y, unsigned char ch, unsigned char color)
{
    unsigned long off = VRAM_MAP + (unsigned long)y * ROW_STRIDE + (unsigned int)x * 2;
    VERA_ADDR_LO  = (unsigned char)(off);
    VERA_ADDR_MID = (unsigned char)(off >> 8);
    VERA_ADDR_HI  = ADDR_H_INC1;   /* bank=1 always, inc=1 for char+color pair */
    VERA_DATA0 = ch;
    VERA_DATA0 = color;
}

static void vera_clear_screen(void)
{
    int x, y;
    OS.critic++;
    VERA_ON();
    for (y = 0; y < screen_height; ++y) {
        unsigned long row = VRAM_MAP + (unsigned long)y * ROW_STRIDE;
        VERA_ADDR_LO  = (unsigned char)(row);
        VERA_ADDR_MID = (unsigned char)(row >> 8);
        VERA_ADDR_HI  = ADDR_H_INC1;
        for (x = 0; x < screen_width; ++x) {
            VERA_DATA0 = 32;
            VERA_DATA0 = COL_BLACK;
        }
    }
    VERA_OFF();
    OS.critic--;
}

/* Returns a printable char from the 94-char range [33..126] */
static unsigned char matrix_char(void)
{
    unsigned char r = fast_rand() % 94;
    return 33 + r;
}

static void wait_vbi(void)
{
    unsigned char t = OS.rtclok[2];
    while (OS.rtclok[2] == t)
        ;
}

/* ------------------------------------------------------------------ */
/* One animation step for column col at head position h.              */
/* Writes at most 4 cells; caller increments col_head after.          */
/* ------------------------------------------------------------------ */
static void column_step(int col, int h)
{
    int r;

    if (h < screen_height)
        vera_put_char(col, h, matrix_char(), COL_WHITE);

    r = h - 1;
    if (r >= 0 && r < screen_height)
        vera_put_char(col, r, matrix_char(), COL_LGRE);

    r = h - ZONE_LIGHT;
    if (r >= 0 && r < screen_height)
        vera_put_char(col, r, matrix_char(), COL_DGRE);

    r = h - ZONE_TOTAL;
    if (r >= 0 && r < screen_height)
        vera_put_char(col, r, 32, COL_BLACK);
}

/* ------------------------------------------------------------------ */
/* Main animation update — called once per VBI frame.                 */
/* Each active column advances STEPS_PER_FRAME rows.                  */
/* ------------------------------------------------------------------ */
static void update_matrix(void)
{
    int col;
    unsigned char step;
    unsigned char done_thresh;

    OS.critic++;
    VERA_ON();

    done_thresh = (unsigned char)(screen_height + ZONE_TOTAL);

    for (col = 0; col < screen_width; ++col) {

        if (!col_active[col]) {
            if (fast_rand() < SPAWN_THRESH) {
                col_head[col]   = 0;
                col_active[col] = 1;
            }
            continue;
        }

        for (step = 0; step < STEPS_PER_FRAME; ++step) {
            column_step(col, (int)(unsigned char)col_head[col]);
            col_head[col]++;
            if (col_head[col] >= done_thresh) {
                col_active[col] = 0;
                break;
            }
        }
    }

    VERA_OFF();
    OS.critic--;
}

/* ------------------------------------------------------------------ */

int main(void)
{
    printf("VERA Matrix\n");

    VERA_ON();
    vera_require();
    VERA_CTRL     = 0;
    screen_width  = (DC_HSCALE >= 128) ? 80 : 40;
    screen_height = (DC_VSCALE >= 128) ? 60 : 30;
    setup_matrix_palette();
    VERA_OFF();

    printf("Mode: %dx%d\n", screen_width, screen_height);

    /* Seed the LFSR from Atari hardware entropy */
    prng8 = RTCLOK_L ^ POKEY_RANDOM;
    if (!prng8) prng8 = 0xA5;   /* LFSR must not be zero */

    vera_clear_screen();

    /* Blank ANTIC: zero shadow first so the OS VBI keeps it off */
    SDMCTL = 0;
    DMACTL = 0;

    while (1) {
        wait_vbi();
        update_matrix();
    }

    return 0;
}
