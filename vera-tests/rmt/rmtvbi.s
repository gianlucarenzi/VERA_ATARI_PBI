;
; RMT player driven by the vertical blank interrupt
;
; Where the player runs depends on what the OS does with this VBI:
;
; - CRITIC ($42) = 0 and the VBI did not hit an IRQ handler: the OS runs
;   stage 2 (shadow registers -> hardware, including the display list
;   pointer DLISTL/H) and then the DEFERRED VBI (VVBLKD). The player runs
;   there, AFTER stage 2. In the immediate VBI it would delay stage 2 by the
;   whole player time (POKEY + VERA: 30-60 lines); on NTSC the vertical
;   blank is only ~20 lines, so DLISTL/H were rewritten while ANTIC was
;   already drawing the screen: ANTIC restarted the display list in the
;   middle of the frame and the picture jumped.
;
; - CRITIC <> 0 (every SIO operation): the OS skips stage 2 and the
;   deferred VBI, so the player runs in the IMMEDIATE VBI (VVBLKI): the
;   music keeps playing while loading. Nothing touches the display list in
;   that frame, so a long immediate VBI is harmless.
;
; The OS NMI handler has already pushed A, X, Y; the immediate handler exits
; with JMP SYSVBV (stage 1: RTCLOK, CDTMV1 used by SIO timeouts...), the
; deferred one through the previous VVBLKD (normally XITVBV).
;
; The player can take a good part of a frame. While it runs we re-enable
; IRQs (CLI): the SIO receive/transmit is IRQ driven (VSERIN/VSEROR/VSEROC)
; and a byte arrives every ~930 CPU cycles at 19200 baud. Keeping IRQs
; masked for the whole player time would overrun the serial port.
;
; BUT the CLI is allowed only if the interrupted code had IRQs enabled
; (I flag of the P register stacked by the NMI). If the VBI hit inside an
; IRQ handler (e.g. the SIO serial input one, before it acknowledged IRQST)
; a CLI would re-enter it: bytes stored twice, corrupted sectors. In that
; case (the OS skips stage 2 and the deferred VBI too) the player tick is
; postponed: it is executed at the end of the next SIO serial input IRQ
; (rmt_irqtick, a few microseconds later, when the IRQ work is done and CLI
; is safe) or at the next VBI (two calls in the same frame), so the song
; tempo is unchanged.
;
; Stack at VBI entry: $101,X = Y  $102,X = X  $103,X = A  $104,X = P
;
; C interface (see rmt.h)
;
        .export _rmt_init, _rmt_vbi_on, _rmt_vbi_off
        .export _rmt_io_begin, _rmt_io_end
        .export _rmt_frames, _rmt_lines, _rmt_maxlines, _rmt_deferred, _rmt_dropped

        .export rmt_irqtick
        .import rmt_init, rmt_play, rmt_silence
.ifdef RMT_VERA
        .import psg_update, psg_silence         ; psgrmt.s: VERA PSG output
.endif
        .import rmt_ioactive

SETVBV  = $E45C
SYSVBV  = $E45F
VVBLKI  = $0222
VVBLKD  = $0224
CRITIC  = $42
PALNTS  = $62                   ; XL/XE OS: 0 = NTSC, 1 = PAL
VCOUNT  = $D40B
AUDC3   = $D205
AUDC4   = $D207

        .segment "BSS"
_rmt_frames:    .res 2          ; frame counter incremented by the VBI
_rmt_lines:     .res 1          ; last player duration (scanlines)
_rmt_maxlines:  .res 1          ; worst player duration (scanlines)
oldvbi:         .res 2
oldvbd:         .res 2
due:            .res 1          ; immediate VBI -> deferred VBI: play this frame
busy:           .res 1
vstart:         .res 1
vbion:          .res 1
pending:        .res 1          ; player ticks postponed (VBI inside an IRQ)
_rmt_deferred:  .res 2          ; statistics: how many ticks were postponed
_rmt_dropped:   .res 2          ; statistics: ticks lost (tempo error)
ispeed:         .res 1          ; player calls per frame = instrument speed (1..4)
ticks:          .res 1          ; calls left in the current play_tick

        .segment "CODE"

; unsigned char __fastcall__ rmt_init(const void* module)
; returns the module instrument speed (1 = once per frame)
_rmt_init:
        pha                     ; X = lo, Y = hi for the player
        txa
        tay
        pla
        tax
        lda #0                  ; start from song line 0
        jsr rmt_init
        pha                     ; A = instrument speed of the module
        cmp #1                  ; clamp to 1..4 (RMT format range)
        bcs @lo
        lda #1
@lo:    cmp #5
        bcc @hi
        lda #4
@hi:    sta ispeed
        pla
        ldx #0
        rts

; void rmt_vbi_on(void)
_rmt_vbi_on:
        lda vbion
        bne @done
        lda #0
        sta busy
        sta pending
        sta due
        sta _rmt_maxlines
        lda VVBLKI
        sta oldvbi
        lda VVBLKI+1
        sta oldvbi+1
        lda VVBLKD
        sta oldvbd
        sta vbd_next+1
        lda VVBLKD+1
        sta oldvbd+1
        sta vbd_next+2
        ldy #<dvbi
        ldx #>dvbi
        lda #7                  ; 7 = deferred VBI vector
        jsr SETVBV
        ldy #<vbi
        ldx #>vbi
        lda #6                  ; 6 = immediate VBI vector
        jsr SETVBV
        inc vbion
@done:  rts

; void rmt_vbi_off(void)
_rmt_vbi_off:
        lda vbion
        beq @done
        ldy oldvbi
        ldx oldvbi+1
        lda #6
        jsr SETVBV
        ldy oldvbd
        ldx oldvbd+1
        lda #7
        jsr SETVBV
        lda #0
        sta vbion
        jsr rmt_silence
.ifdef RMT_VERA
        jsr psg_silence
.endif
@done:  rts

; void rmt_io_begin(void)
; From now on the player leaves POKEY channels 3/4 and AUDCTL to the SIO.
; Channels 3/4 are muted right away, otherwise the last music volume would
; stay set while SIO reprograms AUDF3/AUDF4 as baud rate: an audible whine.
_rmt_io_begin:
        lda #1
        sta rmt_ioactive
        lda #0
        sta AUDC3
        sta AUDC4
        rts

; void rmt_io_end(void)
; Next VBI rewrites all 4 channels and AUDCTL.
_rmt_io_end:
        lda #0
        sta rmt_ioactive
        rts

; ---------------------------------------------------------------------------
; play_tick: one frame of music = ispeed calls of rmt_play (method 1 of
; "instrument speed": the calls are back to back instead of spread over the
; frame). The player counts them itself (v_ainstrspeed): the song advances
; once every ispeed calls, the other calls only step the instruments, so
; tempo and instrument speed are right; the sound still changes once per
; frame. A postponed tick (pending) also stands for ispeed calls.
play_tick:
        lda ispeed
        bne @n
        lda #1                  ; not initialised yet: one call
@n:     sta ticks
@l:     jsr rmt_play
        dec ticks
        bne @l
        rts

; ---------------------------------------------------------------------------
; rmt_irqtick: run a postponed player tick from the tail of an IRQ handler.
; Call with I=1 once the IRQ has been fully served. Preserves X and Y.
rmt_irqtick:
        lda pending
        beq @none
        lda busy
        bne @none
        inc busy
        dec pending
        txa
        pha
        tya
        pha
        cli                     ; the IRQ is done: nesting is safe now
        jsr play_tick
        sei
        pla
        tay
        pla
        tax
        lda #0
        sta busy
@none:  rts

;---------------------------------------------------------------------------
; run: one frame of music, timed. Call with busy = 0 and I = 1 from an
; interrupt that may CLI (the interrupted code had IRQs enabled).
run:
        inc busy
        lda VCOUNT
        sta vstart
        cli                     ; let SIO serial IRQs in while we play
        jsr play_tick
        lda pending             ; catch up one postponed tick
        beq @nocatch
        dec pending
        jsr play_tick
@nocatch:
.ifdef RMT_VERA
        jsr psg_update          ; channel state -> VERA PSG voices; IRQs
.endif                          ; still on: it takes longer than a serial byte
        sei
        lda VCOUNT
        sec
        sbc vstart
        bcs @nowrap             ; VCOUNT wrapped around the frame end:
        ldx PALNTS              ; add the line pairs of a frame
        beq @ntsc
        adc #156                ; PAL: 312 lines
        jmp @nowrap
@ntsc:  adc #131                ; NTSC: 262 lines
@nowrap:
        asl a                   ; VCOUNT counts line pairs
        sta _rmt_lines
        cmp _rmt_maxlines
        bcc @nomax
        sta _rmt_maxlines
@nomax:
        lda #0
        sta busy
        rts

; postpone: a tick that cannot run now (IRQ handler interrupted, or the
; previous tick still running)
postpone:
        inc _rmt_deferred
        bne @nohi
        inc _rmt_deferred+1
@nohi:  lda pending
        cmp #4                  ; do not accumulate forever
        bcc @keep
        inc _rmt_dropped
        bne @exit
        inc _rmt_dropped+1
        rts
@keep:  inc pending
@exit:  rts

; ---------------------------------------------------------------------------
; Immediate VBI handler
; ---------------------------------------------------------------------------
vbi:
        inc _rmt_frames
        bne @nohi
        inc _rmt_frames+1
@nohi:
        tsx
        lda $0104,x             ; P of the interrupted code
        and #$04                ; I flag set: we are inside an IRQ handler
        bne @defer              ; or a SEI section, no CLI allowed
        lda CRITIC              ; 0: stage 2 and the deferred VBI follow,
        beq @later              ; the player runs there
        lda busy                ; previous player call still running
        bne @defer              ; (can only happen with IRQs enabled)
        jsr run
        jmp SYSVBV
@later: lda #1
        sta due
        jmp SYSVBV
@defer: jsr postpone
        jmp SYSVBV

; ---------------------------------------------------------------------------
; Deferred VBI handler (after OS stage 2)
; ---------------------------------------------------------------------------
dvbi:
        lda due
        beq @out
        lda #0
        sta due
        sei                     ; run expects I = 1
        lda busy                ; a long tick of the previous frame still
        bne @busy               ; running underneath
        jsr run
        jmp vbd_next
@busy:  jsr postpone
@out:   jmp vbd_next

; Exit to the previous deferred VBI. A plain JMP whose operand is set by
; _rmt_vbi_on, not JMP (oldvbd): an indirect jump through a vector at $xxFF
; reads its high byte from $xx00 on the 6502, and where the linker puts
; oldvbd depends on the program.
        .segment "DATA"
vbd_next:
        jmp $FFFF
        .segment "CODE"
