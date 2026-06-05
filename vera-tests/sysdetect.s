; vera-tests/sysdetect.s — Hardware detection routines for RUNCPM.COM
;
; Provides (cc65 C-callable):
;   _detect_machine   returns A = 0 (Atari 600XL), 1 (800XL), 2 (130XE)
;   _detect_pal       returns A = 0 (NTSC / 1.79 MHz), 1 (PAL / 1.77 MHz)
;
; This version avoids Zero Page usage ($82-$FF) to prevent collisions with the 
; cc65 runtime and uses the stack/registers for temporary storage.

    .setcpu "6502"
    .include "atari.inc"      ; defines RAMTOP, CRITIC, PORTB, VCOUNT, etc.

    .export _detect_machine
    .export _detect_pal

    .segment "CODE"

; ============================================================================
; _detect_machine — Atari model detection
;
; Returns A: 0 = Atari 600XL,  1 = Atari 800XL,  2 = Atari 130XE
; ============================================================================

_detect_machine:
    lda RAMTOP              ; RAMTOP < $80 -> 600XL (< 32 KB user RAM)
    cmp #$80
    bcs @maybe_xe
    lda #0
    rts

@maybe_xe:
    inc CRITIC              ; defer deferred-VBI (single INC = NMI-safe)
    php
    sei                     ; block maskable IRQs

    lda PORTB
    tax                     ; X = original PORTB
    lda $4000
    tay                     ; Y = original main-bank $4000

    lda #$5A
    sta $4000               ; write sentinel in main bank

    txa
    and #$C3
    ora #$20
    sta PORTB               ; map XE bank 0 at $4000 (CPU only, ANTIC stays main)

    lda $4000
    pha                     ; push original XE-bank $4000 to stack

    lda #$A5
    sta $4000               ; mark XE bank with different value

    stx PORTB               ; back to main bank

    lda $4000
    cmp #$5A                ; sentinel still in main bank?
    beq @is_130xe

    ; 800XL confirmed: both writes hit the same physical RAM
    pla                     ; discard XE value from stack
    tya
    sta $4000               ; restore main-bank $4000
    stx PORTB               ; restore original PORTB
    plp
    dec CRITIC
    lda #1                  ; 800XL
    rts

@is_130xe:
    ; 130XE confirmed: restore both banks
    txa
    and #$C3
    ora #$20
    sta PORTB               ; map XE bank 0 again
    pla
    sta $4000               ; restore original XE-bank $4000
    stx PORTB               ; restore original PORTB (main bank)

    tya
    sta $4000               ; restore original main-bank $4000

    plp
    dec CRITIC
    lda #2                  ; 130XE
    rts

; ============================================================================
; _detect_pal — PAL/NTSC detection via ANTIC VCOUNT
;
; Returns A: 0 = NTSC (1.79 MHz),  1 = PAL (1.77 MHz)
; ============================================================================

_detect_pal:
    inc CRITIC
    php
    sei

    ; Wait for VCOUNT = 0 (start of a new frame)
@wait_start:
    lda VCOUNT
    bne @wait_start

    ; Wait for VCOUNT to advance past 0
@wait_advance:
    lda VCOUNT
    beq @wait_advance
    tax                     ; X = current maximum VCOUNT

    ; Scan until VCOUNT wraps back to 0
@scan:
    lda VCOUNT
    beq @scan_done
    stx @tmp                ; use local storage instead of ZP
    cmp @tmp
    bcc @scan               ; current < max: keep going
    tax                     ; new maximum found
    bcs @scan               ; always loops (Carry set by CMP A>=TMP)

@scan_done:
    plp
    dec CRITIC

    ; Threshold: NTSC max ~124 < 140 < 156~ PAL max
    txa                     ; A = max VCOUNT
    cmp #140
    bcc @ntsc
    lda #1                  ; PAL
    rts
@ntsc:
    lda #0                  ; NTSC
    rts

@tmp: .byte 0
