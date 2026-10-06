#include <stdio.h>
#include <stdint.h>
#include <atari.h>
#include "vera_detect.h"

#define EXIT_KEY 27     /* ATASCII ESC */

/* The VERA driver owns the keyboard (it replaces VKEYBD) and stores the
 * already-translated ATASCII char in OS.ch, so OS.ch is compared directly
 * (kbhit()/cgetc() would run it through the OS key table a second time). */

/* --- VERA driver control block: only what is needed to flush the keys --- */
#define VCTL_REQUEST        5
#define VCTL_ENTRY_LO       10
#define VCTL_ENTRY_HI       11
#define VCTL_FLAGS          4
#define VCTL_FLAG_API_READY 0x80
#define VERA_REQ_FLUSH_KBD  0x05

static void flush_vera_keyboard(void)
{
    uint16_t a, entry;
    volatile unsigned char *p;

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

int main(void) {
    int i;
    printf("VeraX16 detected (ID: 0x%04X)\n", vera_require());
    printf("Starting character test loop (ESC sequence)...\n");
    printf("Press ESC to exit.\n");
    OS.ch = 0xFF;
    while(1) {
        for (i = 0; i < 256; i++) {
            printf("%c%c", 27, i);
            if (OS.ch == EXIT_KEY)
                goto done;
        }
    }
done:
    OS.ch = 0xFF;
    flush_vera_keyboard();      /* do not leave the ESC in the E: key ring */
    printf("\nTest ended.\n");
    return 0;
}
