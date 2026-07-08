; vu_pm.s — 4-channel VU meter using Player/Missile graphics + a Display
; List Interrupt, deliberately NOT touching the active text screen's
; content or display list structure beyond adding one DLI flag on a fresh
; display list that renders the SAME screen memory the OS was already
; using (so printf() before/after the meter keeps working normally).
;
; Each of the 4 hardware players is one channel's vertical bar (bottom-
; anchored, growing upward with level, up to 128 scanlines/64 double-line
; cells tall). All 4 share the same vertical span, so a single DLI firing
; once at the top of that span and stepping through 8 bands of 16
; scanlines each (one STA WSYNC per scanline) via COLPM0-3 paints every
; bar with the same green->red gradient — a bar just doesn't reach the
; upper bands when its level is low, so only the colors up to its own
; height are ever visible.
;
; C API (see vu_pm.h):
;   void vu_pm_init(void);              set up P/M DMA, colors, DL, DLI
;   void vu_pm_set0..3(unsigned char);  channel N's level, 0-63
;   void vu_pm_done(void);              restore normal display, no DLI
;
; NOTE: PM_VSTART (below) picks which double-line P/M memory index lines
; up with the bottom of the meter's 128-scanline band. This is the one
; genuinely empirical part of P/M graphics (exact vertical timing varies
; a little by machine/TV standard) — nudge it up/down if the bars don't
; sit where the DLI-colored band is.

    .setcpu "6502"

    .export _vu_pm_init, _vu_pm_done
    .export _vu_pm_set0, _vu_pm_set1, _vu_pm_set2, _vu_pm_set3

    .include "atari.inc"

; ============================================================================
; Hardware/OS registers — DMACTL, PMBASE, WSYNC, NMIEN, HPOSP0-3, SIZEP0-3,
; COLPM0-3, GRACTL, SDMCTL, SAVMSC and VDSLST all come from atari.inc
; (via its nested atari_antic.inc/atari_gtia.inc) — no need to redefine them.
; ============================================================================

DMACTL_WITH_PM  = $2E           ; normal playfield + missile/player DMA, double-line res
GRACTL_PM_ON    = $02           ; enable player graphics generation
NMIEN_VBI_ONLY  = $40
NMIEN_VBI_DLI   = $C0

; Bar geometry
BAR_MAX_CELLS = 64               ; 128 scanlines / 2 (double-line) = 64 cells
PM_VSTART     = 116              ; TUNE ME if bars land on the wrong rows
PM_WSTART     = PM_VSTART - BAR_MAX_CELLS  ; index of the window's top cell

DLI_ROW       = 8                ; text row where the 128-scanline band starts
                                  ; (16 rows x 8 lines = 128, ending at row 23)

; ============================================================================
; P/M memory — double-line resolution needs a 1K-aligned 1024-byte block.
; Declared here (not a hardcoded page) so the linker places it wherever it
; actually fits, with no risk of colliding with our own code/data/heap.
; ============================================================================

    .segment "ZEROPAGE"

pm_ptr: .res 2

; Dedicated, linker-aligned segment (see atari_nosyschk.cfg's PMGFX entry) —
; double-line P/M resolution needs PMBASE's low byte to be zero, which only
; a real segment `align` guarantees (an in-segment .align is only relative
; to wherever BSS itself lands, which isn't necessarily 1K-aligned).
    .segment "PMGFX"

pm_mem:       .res $0400
pm_player0 = pm_mem + $0200
pm_player1 = pm_mem + $0280
pm_player2 = pm_mem + $0300
pm_player3 = pm_mem + $0380

    .segment "BSS"

; Our own display list: 3 blank + LMS/mode2 + 23 more mode2 (one flagged
; for the DLI) + JVB. Screen address operand is patched in at init from
; whatever SAVMSC already points to.
dlist:        .res (3 + 3 + 23 + 3)

saved_dmactl: .res 1
saved_sdlstl: .res 1
saved_sdlsth: .res 1

    .segment "RODATA"

; 8 bands x 16 scanlines = 128 scanlines, weighted toward the "safe" end
; (green/green-yellow) like a real VU meter, with orange/red only at top.
; The DLI fires near the TOP of the screen (row DLI_ROW) and scans DOWN
; toward the baseline (row 23) as it executes, so band 0 here paints the
; TOP of the meter band (only reached by the tallest/loudest bars) and
; the last band paints the BOTTOM (the baseline every non-empty bar shows) —
; hence red first, green last.
band_colors:
    .byte $4A                   ; red          (hue  4, lum 10)
    .byte $38, $38               ; orange-red   (hue  3, lum 8)
    .byte $18                   ; yellow-orange(hue  1, lum 8)
    .byte $D8, $D8              ; green-yellow (hue 13, lum 8)
    .byte $C8, $C8              ; green        (hue 12, lum 8)
BAND_COUNT = 8
BAND_LINES = 16

    .segment "CODE"

; ============================================================================
; _vu_pm_init — build the display list, enable P/M DMA, install the DLI.
; ============================================================================

_vu_pm_init:
    ; --- clear all 1024 bytes of P/M memory first: only PM_WSTART..PM_VSTART
    ; of each player is ever written afterward, so whatever RAM garbage was
    ; already sitting in the rest of the block would otherwise show up as
    ; stray/random pixels above and below the meter band. ---
    lda #0
    ldy #0
@clr_pm:
    sta pm_mem,y
    sta pm_mem+256,y
    sta pm_mem+512,y
    sta pm_mem+768,y
    iny
    bne @clr_pm

    ; --- build our display list ---
    ldy #0
    lda #$70
    sta dlist,y             ; dlist[0]
    iny
    sta dlist,y             ; dlist[1]
    iny
    sta dlist,y             ; dlist[2]  (3 blank instructions = 24 lines)
    iny                     ; y=3

    lda #$42                ; LMS + mode 2 (row 0)
    sta dlist,y             ; dlist[3]
    iny
    lda SAVMSC
    sta dlist,y             ; dlist[4] = screen address low
    iny
    lda SAVMSC+1
    sta dlist,y             ; dlist[5] = screen address high
    iny                     ; y=6, next free slot

    ldx #(DLI_ROW - 1)       ; rows 1..(DLI_ROW-1): plain mode-2 instructions
@fill1:
    lda #$02
    sta dlist,y
    iny
    dex
    bne @fill1

    lda #$82                ; row DLI_ROW: mode-2 WITH the DLI flag (bit 7) —
    sta dlist,y             ; our 128-scanline meter band starts here
    iny

    ldx #(23 - DLI_ROW)      ; remaining rows: plain mode-2
@fill2:
    lda #$02
    sta dlist,y
    iny
    dex
    bne @fill2

    lda #$41                ; JVB back to the top of our own list
    sta dlist,y
    iny
    lda #<dlist
    sta dlist,y
    iny
    lda #>dlist
    sta dlist,y

    ; --- install DLI handler ---
    lda #<dli_handler
    sta VDSLST
    lda #>dli_handler
    sta VDSLST+1

    ; --- swap in our display list (SEI while the two-byte shadow pointer
    ; is inconsistent, so the OS's VBI can't copy a torn value to hardware) ---
    sei
    lda SDLSTL
    sta saved_sdlstl
    lda SDLSTH
    sta saved_sdlsth
    lda #<dlist
    sta SDLSTL
    lda #>dlist
    sta SDLSTH
    cli

    lda SDMCTL
    sta saved_dmactl
    lda #DMACTL_WITH_PM
    sta SDMCTL

    lda #GRACTL_PM_ON
    sta GRACTL

    lda #>pm_mem
    sta PMBASE

    lda #56                  ; 4 bars, 32px (quad) wide, 8px gaps, +8px right
    sta HPOSP0
    lda #96
    sta HPOSP1
    lda #136
    sta HPOSP2
    lda #176
    sta HPOSP3

    lda #3                   ; SIZEP: quad width (32px) on all 4 players
    sta SIZEP0
    sta SIZEP1
    sta SIZEP2
    sta SIZEP3

    lda #0                   ; start with every bar empty
    jsr _vu_pm_set0
    lda #0
    jsr _vu_pm_set1
    lda #0
    jsr _vu_pm_set2
    lda #0
    jsr _vu_pm_set3

    lda #NMIEN_VBI_DLI       ; enable DLI last, once everything else is ready
    sta NMIEN
    rts

; ============================================================================
; _vu_pm_done — disable DLI/P-M DMA and restore the original display list.
; ============================================================================

_vu_pm_done:
    lda #NMIEN_VBI_ONLY
    sta NMIEN

    lda saved_dmactl
    sta SDMCTL

    sei
    lda saved_sdlstl
    sta SDLSTL
    lda saved_sdlsth
    sta SDLSTH
    cli
    rts

; ============================================================================
; _vu_pm_set0..3(level) — A = 0-63 (cc65 default convention: sole byte
; argument in A). Points pm_ptr at the channel's 65-cell window (indices
; PM_WSTART..PM_VSTART within that player's array) and fills it via the
; shared fill_bar routine.
; ============================================================================

_vu_pm_set0:
    pha
    lda #<(pm_player0 + PM_WSTART)
    sta pm_ptr
    lda #>(pm_player0 + PM_WSTART)
    sta pm_ptr+1
    pla
    jsr level_to_cells
    jmp fill_bar

_vu_pm_set1:
    pha
    lda #<(pm_player1 + PM_WSTART)
    sta pm_ptr
    lda #>(pm_player1 + PM_WSTART)
    sta pm_ptr+1
    pla
    jsr level_to_cells
    jmp fill_bar

_vu_pm_set2:
    pha
    lda #<(pm_player2 + PM_WSTART)
    sta pm_ptr
    lda #>(pm_player2 + PM_WSTART)
    sta pm_ptr+1
    pla
    jsr level_to_cells
    jmp fill_bar

_vu_pm_set3:
    pha
    lda #<(pm_player3 + PM_WSTART)
    sta pm_ptr
    lda #>(pm_player3 + PM_WSTART)
    sta pm_ptr+1
    pla
    jsr level_to_cells
    jmp fill_bar

; A (0-63) -> X = filled cell count (1..64). level maps almost 1:1 onto
; our 64-cell/128-scanline range, so cells = level+1 (hits exactly 64
; cells = 128 scanlines at the loudest level, 63).
level_to_cells:
    clc
    adc #1
    tax
    rts

; Input: X = filled cell count (0..BAR_MAX_CELLS), pm_ptr = window base
; (index PM_WSTART of the target player). Window is BAR_MAX_CELLS+1 cells;
; index BAR_MAX_CELLS is the baseline (PM_VSTART), index 0 is the top.
fill_bar:
    ldy #0
@clr:
    lda #0
    sta (pm_ptr),y
    iny
    cpy #(BAR_MAX_CELLS+1)
    bne @clr

    txa
    beq @done                ; level 0 -> bar stays fully cleared

    ldy #BAR_MAX_CELLS
@fill:
    lda #$FF
    sta (pm_ptr),y
    dey
    dex
    bne @fill
@done:
    rts

; ============================================================================
; dli_handler — fires once, at the top of the shared 128-scanline meter
; band (row DLI_ROW of our display list). Steps COLPM0-3 through 8 bands
; of 16 scanlines each via COLPM0-3, using a loop (WSYNC re-syncs to the
; next scanline regardless of the loop overhead, so exact cycle counting
; between WSYNCs isn't needed). A throwaway WSYNC right after entry
; absorbs the NMI's variable dispatch latency.
; ============================================================================

dli_handler:
    pha
    txa
    pha
    tya
    pha
    sta WSYNC                ; absorb NMI dispatch jitter

    ldx #0
@band_loop:
    lda band_colors,x
    sta COLPM0
    sta COLPM1
    sta COLPM2
    sta COLPM3
    ldy #(BAND_LINES - 1)
@wsync_loop:
    sta WSYNC
    dey
    bne @wsync_loop
    inx
    cpx #BAND_COUNT
    bne @band_loop

    pla
    tay
    pla
    tax
    pla
    rti
