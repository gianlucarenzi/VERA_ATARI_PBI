#ifndef VERA_DETECT_H
#define VERA_DETECT_H

#include <stdio.h>
#include <stdlib.h>

/*
 * Unique identifier for the VeraX16 PBI video card.
 * 'V' (0x56) | 'X' (0x58) — returned by vera_detect() on success.
 */
#define VERA_CARD_ID  ((unsigned int)0x5658)

/*
 * The card is a PBI device: its registers answer only while its bit ($80)
 * is set in $D1FF (Atari PBI rule), and while it is set the card's ROM
 * replaces the Math Pack at $D800-$DFFF (no floating point, cc65 does not
 * use it).  vera_select() selects it for the rest of the program: the card
 * out of PDVMSK, so the OS no longer deselects it in SIO / CIO / IRQ (its
 * ROM handles none of them), and SHPDVS (the OS shadow of $D1FF) = $80, so
 * VERA.SYS and the VBI keep it selected.  vera_release() (at exit) gives it
 * back to the OS with the Math Pack.
 */
#define VERA_PDVMSK   (*(volatile unsigned char *)0x0247)
#define VERA_SHPDVS   (*(volatile unsigned char *)0x0248)
#define VERA_PBI_SEL  (*(volatile unsigned char *)0xD1FF)
#define VERA_PBI_BIT  0x80

static unsigned char vera_pdvmsk_saved;

static void vera_release(void)
{
    VERA_PDVMSK = vera_pdvmsk_saved;
    VERA_PBI_SEL = VERA_SHPDVS = 0;
}

/* once per program (vera_detect): the second call would save the cleared mask */
static void vera_select(void)
{
    vera_pdvmsk_saved = VERA_PDVMSK;
    VERA_PDVMSK = vera_pdvmsk_saved & (unsigned char)~VERA_PBI_BIT;
    VERA_PBI_SEL = VERA_SHPDVS = VERA_PBI_BIT;
}

/*
 * vera_detect() — probe the VeraX16 PBI video card at $D100.
 *
 * Selects DCSEL=63 (CTRL=$7E) and checks the identity bytes: $D109 = 'V',
 * $D10A = major version >= 47 (the FX firmware generation: 47.0.2 reads
 * 'V',47,0,0, 48.0.1 reads 'V',48,0,1).  Unlike a write/read-back probe
 * this cannot be fooled by an undriven bus echoing the last written value
 * (the written $7E never equals 'V').  CTRL is restored to 0 afterwards.
 * The card is selected first (vera_select) and stays selected when found.
 *
 * Returns VERA_CARD_ID (0x5658, 'VX') when the card responds correctly,
 * 0 otherwise (card absent, still configuring, or emulator not started
 * with -verax16).
 */
static unsigned int vera_detect(void)
{
    volatile unsigned char * const ctrl = (volatile unsigned char *)0xD105;
    unsigned char v, m;

    vera_select();
    *ctrl = 0x7E;
    v = *(volatile unsigned char *)0xD109;
    m = *(volatile unsigned char *)0xD10A;
    *ctrl = 0x00;
    if (v != 'V' || m < 47) {
        vera_release();
        return 0;
    }
    atexit(vera_release);

    /* Restore VRAM address registers to a safe state */
    *(volatile unsigned char *)0xD100 = 0x00;
    *(volatile unsigned char *)0xD101 = 0x00;
    *(volatile unsigned char *)0xD102 = 0x00;

    return VERA_CARD_ID;
}

/*
 * vera_require() — fatal guard for programs that need the VeraX16 card.
 *
 * Prints an error and calls exit(1) when the card is absent.
 * Returns VERA_CARD_ID on success.
 */
static unsigned int vera_require(void)
{
    unsigned int id = vera_detect();
    if (!id) {
        printf("ERROR: VeraX16 PBI card not found ($D100 not responding).\n");
        printf("Launch with: -verax16 -verax16-rom vera_pbi_handler.rom\n");
        exit(1);
    }
    return id;
}

#endif /* VERA_DETECT_H */
