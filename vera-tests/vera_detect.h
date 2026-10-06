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
 * vera_detect() — probe the VeraX16 PBI video card at $D100.
 *
 * Selects DCSEL=63 (CTRL=$7E) and checks the identity bytes: $D109 = 'V',
 * $D10A = 47.  Unlike a write/read-back probe this cannot be fooled by an
 * undriven bus echoing the last written value (the written $7E never equals
 * 'V').  CTRL is restored to 0 afterwards.
 *
 * Returns VERA_CARD_ID (0x5658, 'VX') when the card responds correctly,
 * 0 otherwise (card absent, still configuring, or emulator not started
 * with -verax16).
 */
static unsigned int vera_detect(void)
{
    volatile unsigned char * const ctrl = (volatile unsigned char *)0xD105;
    unsigned char v, m;

    *ctrl = 0x7E;
    v = *(volatile unsigned char *)0xD109;
    m = *(volatile unsigned char *)0xD10A;
    *ctrl = 0x00;
    if (v != 'V' || m != 47) return 0;

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
