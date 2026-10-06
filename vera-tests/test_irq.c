/* test_irq.c — VERA IRQ hook (VIMIRQ) test.
 *
 * Standalone binary: does NOT require VERA.SYS.  Output goes through the
 * standard Atari E: handler.  Exercises vera_irq.s:
 *
 *  1. hook installed on VIMIRQ, previous vector kept for chaining
 *  2. VSYNC IRQ rate ~ 59.94 Hz, independent of the Atari frame rate
 *  3. LINE IRQ at raster line 240, once per VERA frame
 *  4. AUDIO FIFO LOW is masked after the first IRQ (cannot be acknowledged)
 *     and the machine keeps running (no IRQ storm)
 *  5. the OS still gets its own IRQs through the chain (RTCLOK keeps running)
 *  6. vera_irq_remove() disables the sources and restores VIMIRQ
 *
 * Build: see Makefile (TESTIRQ.COM).
 */

#include <stdio.h>
#include <conio.h>
#include <atari.h>
#include "vera_detect.h"
#include "vera_irq.h"

#define VERA_IEN   (*(volatile unsigned char *)0xD106)
#define VERA_ISR   (*(volatile unsigned char *)0xD107)

#define MEASURE_JIFFIES 120u   /* ~2 s NTSC, ~2.4 s PAL */

static unsigned char g_pass = 0;
static unsigned char g_fail = 0;
static unsigned char g_lines = 0;
static const char *g_section = "";

static void pause_page(void)
{
    if (g_lines >= 20)
    {
        printf("-- press a key --\n");
        cgetc();
        g_lines = 0;
    }
}

static void check(const char *name, unsigned char ok, unsigned int got, unsigned int exp)
{
    if (ok)
    {
        g_pass++;
        return;
    }
    pause_page();
    printf("FAIL %s %s\n     got %u exp %u\n", g_section, name, got, exp);
    g_lines += 2;
    g_fail++;
}

/* Wait n jiffies (1/60 or 1/50 s) using the OS clock. */
static void wait_jiffies(unsigned char n)
{
    unsigned char start = OS.rtclok[2];
    while ((unsigned char)(OS.rtclok[2] - start) < n)
        ;
}

/* Expected VERA events in 'jiffies' Atari frames: VERA = 59.94 Hz fixed. */
static unsigned int expected_events(unsigned char jiffies)
{
    unsigned long atari_x100 = OS.palnts ? 4986ul : 5992ul;
    return (unsigned int)(((unsigned long)jiffies * 5994ul) / atari_x100);
}

static unsigned char near_enough(unsigned int got, unsigned int exp)
{
    unsigned int tol = exp / 20u + 1u;   /* 5 % + 1 */
    return got + tol >= exp && got <= exp + tol;
}

int main(void)
{
    unsigned char before, jiff0, jiff1, flags;
    unsigned int exp, got;
    unsigned int old_vec;

    vera_require();

    printf("VERA IRQ hook test\n");
    printf("==================\n");

    old_vec = *(volatile unsigned int *)0x0216;

    /* Start from a known VERA state: all IRQs off, status clear */
    VERA_IEN = 0;
    VERA_ISR = 0x07;

    /* [1] install */
    g_section = "[1]";
    vera_irq_install();
    check("VIMIRQ changed", *(volatile unsigned int *)0x0216 != old_vec,
          *(volatile unsigned int *)0x0216, old_vec);
    check("no IRQ enabled yet", (VERA_IEN & 0x0F) == 0, VERA_IEN & 0x0F, 0);

    /* [2] VSYNC */
    g_section = "[2] VSYNC";
    vera_irq_take();
    before = vera_irq_count[0];
    jiff0 = OS.rtclok[2];
    vera_irq_enable(VERA_IRQ_VSYNC);
    wait_jiffies(MEASURE_JIFFIES);
    vera_irq_disable(VERA_IRQ_VSYNC);
    jiff1 = OS.rtclok[2];
    got = (unsigned char)(vera_irq_count[0] - before);
    exp = expected_events((unsigned char)(jiff1 - jiff0));
    check("count ~ 59.94 Hz", near_enough(got, exp), got, exp);
    flags = vera_irq_take();
    check("flag VSYNC set", (flags & VERA_IRQ_VSYNC) != 0, flags, VERA_IRQ_VSYNC);
    check("IEN VSYNC off after disable", (VERA_IEN & VERA_IRQ_VSYNC) == 0, VERA_IEN & 1, 0);
    

    /* [3] LINE */
    g_section = "[3] LINE";
    vera_irq_set_line(240);
    vera_irq_take();
    before = vera_irq_count[1];
    jiff0 = OS.rtclok[2];
    vera_irq_enable(VERA_IRQ_LINE);
    wait_jiffies(MEASURE_JIFFIES);
    vera_irq_disable(VERA_IRQ_LINE);
    jiff1 = OS.rtclok[2];
    got = (unsigned char)(vera_irq_count[1] - before);
    exp = expected_events((unsigned char)(jiff1 - jiff0));
    check("count ~ one per VERA frame", near_enough(got, exp), got, exp);
    flags = vera_irq_take();
    check("flag LINE set", (flags & VERA_IRQ_LINE) != 0, flags, VERA_IRQ_LINE);

    /* [4] AFLOW: FIFO is empty after reset, so the IRQ is pending at once */
    g_section = "[4] AFLOW";
    before = vera_irq_count[3];
    jiff0 = OS.rtclok[2];
    vera_irq_enable(VERA_IRQ_AFLOW);
    wait_jiffies(6);
    got = (unsigned char)(vera_irq_count[3] - before);
    check("serviced exactly once (then masked)", got == 1, got, 1);
    check("IEN AFLOW masked by the hook", (VERA_IEN & VERA_IRQ_AFLOW) == 0, VERA_IEN & 8, 0);
    flags = vera_irq_take();
    check("flag AFLOW set", (flags & VERA_IRQ_AFLOW) != 0, flags, VERA_IRQ_AFLOW);

    /* [5] the OS clock kept running through the chain */
    g_section = "[5] chain";
    jiff1 = OS.rtclok[2];
    check("RTCLOK advanced during the test", (unsigned char)(jiff1 - jiff0) != 0, jiff1 - jiff0, 1);

    /* [6] remove */
    g_section = "[6] remove";
    vera_irq_remove();
    check("VIMIRQ restored", *(volatile unsigned int *)0x0216 == old_vec,
          *(volatile unsigned int *)0x0216, old_vec);
    check("all sources disabled", (VERA_IEN & 0x0F) == 0, VERA_IEN & 0x0F, 0);

    printf("\n==================\n");
    printf("PASS: %d   FAIL: %d\n", (unsigned int)g_pass, (unsigned int)g_fail);
    printf("Press any key...\n");
    cgetc();
    return (g_fail == 0) ? 0 : 1;
}
