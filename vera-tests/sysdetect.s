; vera-tests/sysdetect.s — Hardware detection routines for RUNCPM.COM
;
; Provides (cc65 C-callable):
;   _detect_machine   returns A = 0 (Atari 600XL), 1 (800XL), 2 (130XE)
;   _detect_pal       returns A = 0 (NTSC / 1.79 MHz), 1 (PAL / 1.77 MHz)
;
; Machine detection is identical to HAS_XE_BANK + RAMTOP check in
; vera_pbi_handler.s.
;
; PAL/NTSC detection reads ANTIC VCOUNT ($D40B) over one full frame and
; compares the peak against a threshold (NTSC ~124, PAL ~156, threshold 140).
; This is hardware-based and works regardless of OS version or emulator flags.

    .setcpu "6502"
    .include "atari.inc"      ; defines RAMTOP, CRITIC, PORTB, VCOUNT, etc.

; ZP scratch — same addresses as TMP0/TMP1/TMP2 in vera_pbi_handler.s ($CD-$CF).
; Safe: PBI handler has finished before RUNCPM.COM starts.  The deferred VERA
; VBI handler uses LOWBSS (relocated high RAM), never these ZP locations.
TMP0    = $CD
TMP1    = $CE
TMP2    = $CF

    .export _detect_machine
    .export _detect_pal

; ============================================================================
; _detect_machine — Atari model detection
;
; Mirrors HAS_XE_BANK exactly:
;   (PORTB & $C3) | $20 maps XE bank 0 at $4000-$7FFF for the CPU while
;   keeping ANTIC on the main bank; sentinel values confirm independent banking.
;   CRITIC is incremented (single INC opcode = NMI-safe) to defer the
;   deferred VBI handler; PHP/PLP restores the exact prior interrupt state.
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
    sta TMP0                ; save PORTB
    lda $4000
    sta TMP1                ; save main-bank $4000

    lda #$5A
    sta $4000               ; write sentinel in main bank

    lda TMP0
    and #$C3
    ora #$20
    sta PORTB               ; map XE bank 0 at $4000 (CPU only, ANTIC stays main)

    lda $4000
    sta TMP2                ; save XE-bank $4000

    lda #$A5
    sta $4000               ; mark XE bank with different value

    lda TMP0
    sta PORTB               ; back to main bank

    lda $4000
    cmp #$5A                ; sentinel still in main bank?
    bne @is_800xl

    ; 130XE confirmed: both banks are independent — restore both
    lda TMP1
    sta $4000
    lda TMP0
    and #$C3
    ora #$20
    sta PORTB
    lda TMP2
    sta $4000
    lda TMP0
    sta PORTB

    plp
    dec CRITIC
    lda #2                  ; 130XE
    rts

@is_800xl:
    lda TMP1
    sta $4000
    lda TMP0
    sta PORTB
    plp
    dec CRITIC
    lda #1                  ; 800XL
    rts

; ============================================================================
; _detect_pal — PAL/NTSC detection via ANTIC VCOUNT
;
; VCOUNT ($D40B) is incremented by ANTIC every 2 scan lines, independently
; of CPU or OS state.  Peak value before the frame resets:
;   NTSC: ~124  (248 active lines / 2)
;   PAL:  ~156  (312 active lines / 2)
;
; CRITIC is incremented so the deferred VERA VBI handler does not corrupt
; TMP0 ($CD) during the scan loop.  VCOUNT is a pure hardware counter and
; advances regardless of CRITIC or SEI.
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

    ; Wait for VCOUNT to advance past 0 (avoids immediate exit on loop re-entry)
@wait_advance:
    lda VCOUNT
    beq @wait_advance
    sta TMP0                ; seed max with first non-zero VCOUNT value

    ; Scan until VCOUNT wraps back to 0 (next frame start)
@scan:
    lda VCOUNT
    beq @scan_done
    cmp TMP0
    bcc @scan               ; current < max: keep going
    sta TMP0                ; new maximum found
    bne @scan               ; A != 0 guaranteed (beq @scan_done guards 0)

@scan_done:
    plp
    dec CRITIC

    ; Threshold: NTSC max ~124 < 140 < 156~ PAL max
    lda TMP0
    cmp #140
    bcc @ntsc
    lda #1                  ; PAL
    rts
@ntsc:
    lda #0                  ; NTSC
    rts
