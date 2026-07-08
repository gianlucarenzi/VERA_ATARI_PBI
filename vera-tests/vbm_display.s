; vbm_display.s — switch VERA to its own 320x240 8bpp bitmap layer (layer 0)
; and stream a .vbm file's palette/pixel data straight into VRAM. Used by
; test_player.c to show artwork on VERA's own video output (a separate
; screen from the Atari's own ANTIC/GTIA display used for the title/VU
; meter) while a song plays — see workflow/01-vera-asset-format.md for the
; VBM1 file layout and tools/img2vbm.py for the PC-side converter.
;
; Layer 0 is not used anywhere else in this project (every other test
; program drives layer 1's tile/text mode), so this claims it exclusively
; and disables layer 1 while active — the screen goes solid black (no
; image streamed yet) the moment _vbm_init runs, then fills in as
; vbm_loader.c streams a file's bytes in via _vbm_seek_palette/_pixels and
; _vbm_stream_len.
;
; PBI bus discipline: every VERA register transaction is wrapped in
; CRITIC++/CRITIC-- (matches vtm_player.s / vera_sys_vbi.s / test_matrix.c).
;
; C API (see vbm.h):
;   void vbm_init(void);   switch to bitmap mode, screen goes black
;   void vbm_done(void);   restore whatever vbm_init() found
; vbm_loader.c-internal (not in vbm.h — see its extern declarations):
;   void vbm_seek_palette(void);       VRAM address -> $1:FA00, auto-incr+1
;   void vbm_seek_pixels(void);        VRAM address -> $0:0000, auto-incr+1
;   void vbm_stream_len(unsigned char n);  write vbm_iobuf[0..n) to VERA_DATA0
;   unsigned char vbm_iobuf[VBM_IOBUF_SIZE];  shared read/stream buffer

    .setcpu "6502"

    .export _vbm_init, _vbm_done
    .export _vbm_seek_palette, _vbm_seek_pixels, _vbm_stream_len
    .export _vbm_iobuf

    .include "atari.inc"
    .include "vera_common.inc"

VBM_IOBUF_SIZE = 128     ; kept well under 256 so _vbm_stream_len's 8-bit
                          ; length parameter is never ambiguous at 0=256

    .segment "BSS"

saved_dc_video: .res 1
saved_hscale:   .res 1
saved_vscale:   .res 1
tmp_len:        .res 1

_vbm_iobuf:     .res VBM_IOBUF_SIZE

    .segment "CODE"

; ============================================================================
; _vbm_init — claim layer 0 for a 320x240 8bpp bitmap, disable layer 1,
; save the prior display composer state for _vbm_done to restore.
; ============================================================================

_vbm_init:
    inc CRITIC
    lda #DEVICE_ID_MASK
    sta PBI_LATCH
    lda #VERA_DCSEL0
    sta VERA_CTRL

    lda VERA_DC_VIDEO
    sta saved_dc_video
    lda VERA_DC_HSCALE
    sta saved_hscale
    lda VERA_DC_VSCALE
    sta saved_vscale

    lda #VERA_L0_BITMAP_8BPP
    sta VERA_L0_CONFIG
    lda #VERA_L0_TILEBASE_320
    sta VERA_L0_TILEBASE

    lda #VERA_SCALE_320X240
    sta VERA_DC_HSCALE
    sta VERA_DC_VSCALE
    lda #(VERA_VIDEO_VGA | VERA_LAYER0_EN)
    sta VERA_DC_VIDEO

    lda #0
    sta PBI_LATCH
    dec CRITIC
    rts

; ============================================================================
; _vbm_done — restore the display composer state _vbm_init saved.
; ============================================================================

_vbm_done:
    inc CRITIC
    lda #DEVICE_ID_MASK
    sta PBI_LATCH
    lda #VERA_DCSEL0
    sta VERA_CTRL

    lda saved_dc_video
    sta VERA_DC_VIDEO
    lda saved_hscale
    sta VERA_DC_HSCALE
    lda saved_vscale
    sta VERA_DC_VSCALE

    lda #0
    sta PBI_LATCH
    dec CRITIC
    rts

; ============================================================================
; _vbm_seek_palette / _vbm_seek_pixels — point the auto-incrementing VRAM
; address port at one of the two fixed destinations a .vbm file streams
; to. No parameters (cc65's stack-based multi-byte-argument convention
; isn't worth the complexity for two fixed addresses) — matches this
; project's existing preference for fixed/shared state over passed
; pointers in performance-facing asm (see vtm_player.s's zero-page
; pointers).
; ============================================================================

_vbm_seek_palette:
    inc CRITIC
    lda #DEVICE_ID_MASK
    sta PBI_LATCH
    lda #0
    sta VERA_CTRL

    lda #$00
    sta VERA_ADDR_L
    lda #$FA
    sta VERA_ADDR_M
    lda #(VERA_INC1 | $01)          ; bank 1 -> VRAM $1:FA00 (palette)
    sta VERA_ADDR_H

    lda #0
    sta PBI_LATCH
    dec CRITIC
    rts

_vbm_seek_pixels:
    inc CRITIC
    lda #DEVICE_ID_MASK
    sta PBI_LATCH
    lda #0
    sta VERA_CTRL

    lda #$00
    sta VERA_ADDR_L
    lda #$00
    sta VERA_ADDR_M
    lda #(VERA_INC1 | $00)          ; bank 0 -> VRAM $0:0000 (bitmap pixels)
    sta VERA_ADDR_H

    lda #0
    sta PBI_LATCH
    dec CRITIC
    rts

; ============================================================================
; _vbm_stream_len(n) — A = byte count (1-128, see VBM_IOBUF_SIZE). Writes
; vbm_iobuf[0..n) to VERA_DATA0; VERA's own auto-increment (set up by
; whichever _vbm_seek_* ran last) advances the VRAM pointer, including
; across the bank-0/bank-1 boundary a 320x240 bitmap crosses partway
; through — no need to re-touch ADDR_L/M/H between calls.
; ============================================================================

_vbm_stream_len:
    sta tmp_len
    inc CRITIC
    lda #DEVICE_ID_MASK
    sta PBI_LATCH
    lda #0
    sta VERA_CTRL

    ldx #0
@loop:
    lda _vbm_iobuf,x
    sta VERA_DATA0
    inx
    cpx tmp_len
    bne @loop

    lda #0
    sta PBI_LATCH
    dec CRITIC
    rts
