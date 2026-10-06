#include <stdio.h>
#include <atari.h>
#include "vera_detect.h"
#include "vera_keys.h"

int main(void) {
    int i;
    printf("VeraX16 detected (ID: 0x%04X)\n", vera_require());
    printf("Starting character test loop (ESC sequence)...\n");
    printf("Press ESC to exit.\n");
    OS.ch = 0xFF;
    while(1) {
        for (i = 0; i < 256; i++) {
            printf("%c%c", 27, i);
            if (vera_esc_pressed())
                goto done;
        }
    }
done:
    vera_flush_keys();          /* do not leave the ESC in the E: key ring */
    printf("\nTest ended.\n");
    return 0;
}
