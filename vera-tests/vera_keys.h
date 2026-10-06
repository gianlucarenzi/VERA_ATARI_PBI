#ifndef VERA_KEYS_H
#define VERA_KEYS_H

#include <stdint.h>
#include <atari.h>

/*
 * Keyboard helpers for test programs that run with the VERA driver.
 *
 * The driver owns the POKEY keyboard IRQ (it replaces VKEYBD) and stores the
 * already-translated ATASCII character in OS.ch, so OS.ch is compared
 * directly (kbhit()/cgetc() would run it through the OS key table a second
 * time).  Every key is also queued in the driver's E: key ring; call
 * vera_flush_keys() before exiting so DOS does not receive it as input.
 */

#define VERA_KEY_ESC 27     /* ATASCII ESC */

#define VCTL_FLAGS          4
#define VCTL_REQUEST        5
#define VCTL_ENTRY_LO       10
#define VCTL_ENTRY_HI       11
#define VCTL_FLAG_API_READY 0x80
#define VERA_REQ_FLUSH_KBD  0x05

static unsigned char vera_esc_pressed(void)
{
    return OS.ch == VERA_KEY_ESC;
}

static void vera_flush_keys(void)
{
    uint16_t a, entry;
    volatile unsigned char *p;

    OS.ch = 0xFF;
    for (a = (uint16_t)((uintptr_t)OS.memtop + 1u); a < 0xC000u - 16u; ++a) {
        p = (volatile unsigned char *)(uintptr_t)a;
        if (p[0] == 'V' && p[1] == 'C' && p[2] == 'T' && p[3] == 'L') {
            if (!(p[VCTL_FLAGS] & VCTL_FLAG_API_READY))
                return;
            entry = (uint16_t)p[VCTL_ENTRY_LO] | ((uint16_t)p[VCTL_ENTRY_HI] << 8);
            if (entry < 0x2000u || entry >= 0xC000u)
                return;
            p[VCTL_REQUEST] = VERA_REQ_FLUSH_KBD;
            ((void (*)(void))(uintptr_t)entry)();
            return;
        }
    }
}

#endif /* VERA_KEYS_H */
