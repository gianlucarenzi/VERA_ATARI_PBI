; ============================================================================
; vera_irq.s - VERA IRQ hook on VIMIRQ ($0216)
;
; See vera_irq.h for the rationale and the C interface.
;
; OS contract: the OS reaches this code by "JMP (VIMIRQ)" with nothing saved
; except what the 6502 pushed (P, PC).  A, X and Y must be preserved; to
; pass an IRQ on, restore A and JMP (old VIMIRQ) with the registers as at entry.
; ============================================================================

    .setcpu "6502"
    .include "vera_common.inc"
    .include "atari.inc"

VERA_IRQLINE_L  = PBI_ADDR+$08          ; IRQ_LINE[7:0] (bit 8 is IEN bit 7)
IRQ_MASK        = $0F                   ; IEN/ISR bits 3:0 = AFLOW|SPRCOL|LINE|VSYNC

    .export _vera_irq_install
    .export _vera_irq_remove
    .export _vera_irq_enable
    .export _vera_irq_disable
    .export _vera_irq_take
    .export _vera_irq_set_line
    .export _vera_irq_set_callback
    .export _vera_irq_count

    .segment "BSS"

old_vimirq:     .res 2
callback:       .res 2
installed:      .res 1
flags:          .res 1
pending:        .res 1
tmp:            .res 1
tmp2:           .res 1
_vera_irq_count: .res 4

    .segment "CODE"

; ----------------------------------------------------------------------------
; The IRQ handler
; ----------------------------------------------------------------------------
irq_handler:
    pha
    lda VERA_ISR
    and VERA_IEN
    and #IRQ_MASK
    bne @ours
    pla
    jmp (old_vimirq)            ; not the VERA: let the OS handle it

@ours:
    sta pending
    and #$07
    beq @aflow
    sta VERA_ISR                ; ack VSYNC/LINE/SPRCOL (write 1 clears only these)
@aflow:
    lda pending
    and #$08
    beq @record
    lda VERA_IEN                ; AFLOW cannot be cleared: stop the level IRQ
    and #$F7
    sta VERA_IEN
@record:
    lda pending
    ora flags
    sta flags

    lda pending                 ; per-source counters
    lsr a
    bcc @c1
    inc _vera_irq_count+0
@c1:
    lsr a
    bcc @c2
    inc _vera_irq_count+1
@c2:
    lsr a
    bcc @c3
    inc _vera_irq_count+2
@c3:
    lsr a
    bcc @cb
    inc _vera_irq_count+3
@cb:
    lda callback+1
    beq @done
    txa
    pha
    tya
    pha
    lda pending                 ; A = serviced sources
    jsr call_cb
    pla
    tay
    pla
    tax
@done:
    pla
    rti

call_cb:
    jmp (callback)

; ----------------------------------------------------------------------------
; void vera_irq_install(void)
; ----------------------------------------------------------------------------
_vera_irq_install:
    lda installed
    bne @done
    inc CRITIC
    php
    sei
    lda VIMIRQ
    sta old_vimirq
    lda VIMIRQ+1
    sta old_vimirq+1
    lda #0
    sta flags
    sta callback
    sta callback+1
    ldx #3
@clr:
    sta _vera_irq_count,x
    dex
    bpl @clr
    lda #<irq_handler
    sta VIMIRQ
    lda #>irq_handler
    sta VIMIRQ+1
    lda #1
    sta installed
    plp
    dec CRITIC
@done:
    rts

; ----------------------------------------------------------------------------
; void vera_irq_remove(void)
; ----------------------------------------------------------------------------
_vera_irq_remove:
    lda installed
    beq @done
    inc CRITIC
    php
    sei
    lda VERA_IEN                ; all four sources off, keep IRQ_LINE[8]
    and #$80
    sta VERA_IEN
    lda VIMIRQ                  ; restore only if the vector is still ours
    cmp #<irq_handler
    bne @keep
    lda VIMIRQ+1
    cmp #>irq_handler
    bne @keep
    lda old_vimirq
    sta VIMIRQ
    lda old_vimirq+1
    sta VIMIRQ+1
    lda #0
    sta installed
@keep:                          ; someone hooked after us: stay (now inert, IEN = 0)
    plp
    dec CRITIC
@done:
    rts

; ----------------------------------------------------------------------------
; void __fastcall__ vera_irq_enable(unsigned char mask)    A = mask
; ----------------------------------------------------------------------------
_vera_irq_enable:
    and #IRQ_MASK
    sta tmp
    inc CRITIC
    php
    sei
    lda tmp
    and #$07
    sta VERA_ISR                ; drop stale status of the sources being enabled
    lda VERA_IEN
    and #$8F                    ; keep IRQ_LINE[8] and the current enables
    ora tmp
    sta VERA_IEN
    plp
    dec CRITIC
    rts

; ----------------------------------------------------------------------------
; void __fastcall__ vera_irq_disable(unsigned char mask)   A = mask
; ----------------------------------------------------------------------------
_vera_irq_disable:
    and #IRQ_MASK
    eor #$FF                    ; bits 7:4 stay 1: IRQ_LINE[8] preserved
    sta tmp
    inc CRITIC
    php
    sei
    lda VERA_IEN
    and tmp
    sta VERA_IEN
    plp
    dec CRITIC
    rts

; ----------------------------------------------------------------------------
; unsigned char vera_irq_take(void)   returns and clears the serviced flags
; ----------------------------------------------------------------------------
_vera_irq_take:
    php
    sei
    lda flags
    ldx #0
    stx flags
    plp
    rts

; ----------------------------------------------------------------------------
; void __fastcall__ vera_irq_set_line(unsigned int line)   A = low, X = high
; ----------------------------------------------------------------------------
_vera_irq_set_line:
    sta tmp
    txa
    and #$01
    sta tmp2
    inc CRITIC
    php
    sei
    lda tmp
    sta VERA_IRQLINE_L
    lda VERA_IEN
    and #IRQ_MASK               ; current enables, IRQ_LINE[8] rebuilt below
    ldx tmp2
    beq @b8
    ora #$80
@b8:
    sta VERA_IEN
    plp
    dec CRITIC
    rts

; ----------------------------------------------------------------------------
; void __fastcall__ vera_irq_set_callback(void (*cb)(void))  A = low, X = high
; ----------------------------------------------------------------------------
_vera_irq_set_callback:
    php
    sei
    sta callback
    stx callback+1
    plp
    rts
