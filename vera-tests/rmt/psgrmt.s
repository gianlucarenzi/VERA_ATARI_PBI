; ============================================================================
; psgrmt.s - RMT player output on the VERA PSG (POKEY -> PSG translation)
;
; The Atari RMT player (rmtplayr.s, assembled with -D RMT_VERA) computes the
; four POKEY channels (trackn_audf, trackn_audc, v_audctl) and still writes
; them to POKEY. Once per frame, from the VBI (rmtvbi.s), this code
; turns the same channel state into VERA PSG voices 0-3:
;
;   frequency  from AUDF, AUDCTL (64/15 kHz, 1.79 MHz, 16 bit) and the
;              distortion: word = K / n, n = POKEY divider in machine
;              cycles, K = constant of the kind of sound (psg_ktab, PAL /
;              NTSC, tools/mkpsgtab.py). 64 kHz 8 bit channels, the usual
;              case, read the word from psgtab; the other modes divide, only
;              when the divider changes
;   waveform   pure tone -> pulse 50%; distortion C (poly4) and poly5 tones
;              -> pulse 25%; poly17 / poly5 noise -> VERA noise
;   volume     POKEY volume is linear, VERA volume is logarithmic (0.5 dB per
;              step): vol_log maps 0..15 to the VERA level of the same
;              amplitude
;   channels   one VERA voice per channel (the low channel of a 16 bit pair
;              is silent, as on POKEY). RMT4: voices 0-3; RMT8 (RMT_TRACKS =
;              8, stereo modules for two POKEYs): voices 0-7, channels 1-4
;              from the left POKEY state, 5-8 from the right one (never
;              written to hardware, see rmtplayr.s), each with its own
;              AUDCTL. Pan L+R; with psg_stereo, RMT4 puts ch 1/3 left and
;              2/4 right, RMT8 puts ch 1-4 left and 5-8 right. psg_hybrid
;              silences the voices of channels 1-4 (POKEY plays them, VERA
;              only 5-8)
;
; Not reproduced: volume only mode (AUDC bit 4, sample playback) and the
; high pass filters (AUDCTL bits 2, 1). The noise is VERA's own LFSR at the
; POKEY rate, not POKEY's poly17/poly5 pattern.
;
; The VERA writes happen inside an interrupt: CTRL, FX_CTRL and the port 0
; address are saved and restored, so the interrupted program can be in the
; middle of its own VRAM access sequence. A foreground call (psg_silence)
; sets wbusy and the VBI skips that frame instead of nesting.
;
; C interface: see rmt.h
; ============================================================================

        .setcpu "6502"
        .include "vera_common.inc"

        .export psg_update, psg_silence
        .export _psg_init, _psg_silence := psg_silence
        .export _psg_enable, _psg_stereo, _psg_hybrid, _psg_volume
        .import trackn_audf, trackn_audc, v_audctl
        .import psgtab_lo, psgtab_hi, psg_ktab

.ifdef RMT_TRACKS
NCH             = RMT_TRACKS            ; channels = PSG voices driven
.else
NCH             = 4
.endif
.if NCH > 4
        .import v_audctl2
.endif

PALNTS          = $0062                 ; XL OS: 0 = NTSC
PSG_ADDR        = VERA_PSG_BASE         ; $1F9C0, voice 0
WAVE_PULSE      = $00                   ; PSG reg 3 bits 7:6
WAVE_NOISE      = $C0
PW_50           = 63                    ; duty = (PW + 1) / 128
PW_25           = 31
PAN_LR          = $C0                   ; PSG reg 2: bit 7 right, bit 6 left

        .segment "BSS"

_psg_enable:    .res 1          ; 1 = voices 0..NCH-1 follow the player
_psg_stereo:    .res 1          ; 1 = stereo pan (see header)
_psg_hybrid:    .res 1          ; 1 = channels 1-4 silent on VERA
_psg_volume:    .res NCH        ; VERA level of the voices (0..63)

psg_on:         .res 1          ; voices currently driven
wbusy:          .res 1          ; a VERA write is in progress
tabofs:         .res 1          ; std * 5: page of the table
kofs:           .res 1          ; std * 21: offset in psg_ktab

ch:             .res 1
cx:             .res 1          ; channel within its POKEY (0..3)
abase:          .res 1          ; 0 / 4: index of the POKEY's AUDF1
actl:           .res 1          ; AUDCTL of the channel's POKEY
dist:           .res 1
fast:           .res 1
base:           .res 1
tmp:            .res 1
wlo:            .res 1
whi:            .res 1

c_vol:          .res NCH          ; POKEY volume 0..15
c_wave:         .res NCH
c_pw:           .res NCH
c_simple:       .res NCH          ; 1 = 64 kHz 8 bit: word from psgtab
c_sk:           .res NCH          ; table kind 0..4
c_k:            .res NCH          ; index in psg_ktab
c_n0:           .res NCH          ; divider in machine cycles (24 bit)
c_n1:           .res NCH
c_n2:           .res NCH

v_key0:         .res NCH          ; divider and constant of the cached word
v_key1:         .res NCH
v_key2:         .res NCH
v_keyk:         .res NCH
v_wlo:          .res NCH
v_whi:          .res NCH

dvd:            .res 3          ; dividend / quotient
dsr:            .res 3          ; divisor
rem:            .res 3

buf:            .res 4*NCH      ; per voice: freq lo, freq hi, pan|vol, wave|pw

save_ctrl:      .res 1
save_fx:        .res 1
save_l:         .res 1
save_m:         .res 1
save_h:         .res 1

        .segment "RODATA"

; per distortion (AUDC >> 5): 0 poly5+17, 1 poly5 tone, 2 poly5+4, 3 poly5
; tone, 4 poly17, 5 pure, 6 poly4 (distortion C), 7 pure
dist_wave:
        .byte WAVE_NOISE, WAVE_PULSE, WAVE_NOISE, WAVE_PULSE
        .byte WAVE_NOISE, WAVE_PULSE, WAVE_PULSE, WAVE_PULSE
dist_pw:
        .byte 0, PW_25, 0, PW_25, 0, PW_50, PW_25, PW_50
dist_k:                                 ; $FF: distortion C, from n mod 15
        .byte 6, 4, 6, 4, 5, 0, $FF, 0
dist_sk:                                ; psgtab kind (tools/mkpsgtab.py)
        .byte 4, 2, 4, 2, 3, 0, 1, 0
; n mod 15 -> constant index for distortion C, $FF = no pulses change (silent)
distc_k:
        .byte $FF, 3, 3, 2, 3, 1, 2, 3, 3, 2, 1, 3, 2, 3, 3
; AUDCTL bits that take a channel out of the table: 15 kHz, 16 bit, 1.79 MHz
simple_mask:
        .byte $51, $11, $29, $09
; VERA level with the same amplitude as POKEY volume 0..15 (psg.v log table)
vol_log:
        .byte 0, 17, 28, 35, 40, 44, 47, 50, 52, 54, 56, 58, 59, 61, 62, 63
pan_stereo:                             ; with psg_stereo
.if NCH > 4
        .byte $40, $40, $40, $40, $80, $80, $80, $80    ; left / right POKEY
.else
        .byte $40, $80, $40, $80                        ; 1/3 left, 2/4 right
.endif

        .segment "CODE"

; ----------------------------------------------------------------------------
; void psg_init(void) - PAL/NTSC constants, voices silent
; ----------------------------------------------------------------------------
_psg_init:
        lda PALNTS
        beq @ntsc
        lda #0                          ; PAL
        sta tabofs
        sta kofs
        beq @k
@ntsc:  lda #5
        sta tabofs
        lda #21
        sta kofs
@k:     lda #$FF
        ldx #NCH-1
@c:     sta v_keyk,x                    ; no cached word
        dex
        bpl @c
        jmp psg_silence

; ----------------------------------------------------------------------------
; psg_update - called once per frame from the VBI, after the player
; ----------------------------------------------------------------------------
psg_update:
        cld
        lda wbusy                       ; foreground VERA write running:
        bne @rts                        ; skip this frame
        lda _psg_enable
        bne @on
        lda psg_on                      ; switched off since last frame:
        beq @rts                        ; silence once
        jmp psg_silence
@on:    lda #1
        sta psg_on
        ldx #0
@c:     stx ch
        jsr channel
        ldx ch
        jsr voice
        ldx ch
        inx
        cpx #NCH
        bne @c
        jmp vera_write
@rts:   rts

; ----------------------------------------------------------------------------
; void psg_silence(void) - voices 0-3 at volume 0 (also from the foreground)
; ----------------------------------------------------------------------------
psg_silence:
        lda #0
        sta psg_on
        ldx #4*NCH-1
@b:     sta buf,x
        dex
        bpl @b
        ldx #NCH-1
@v:     sta _psg_volume,x
        dex
        bpl @v
        ; fall through

; ----------------------------------------------------------------------------
; vera_write - copy buf to PSG voices 0-3, preserving the VERA state
; ----------------------------------------------------------------------------
vera_write:
        inc wbusy
        lda VERA_CTRL
        sta save_ctrl
        lda #VERA_DCSEL2
        sta VERA_CTRL
        lda VERA_FX_CTRL                ; cache/transparent writes would
        sta save_fx                     ; change what DATA0 stores
        lda #0
        sta VERA_FX_CTRL
        sta VERA_CTRL                   ; ADDRSEL 0, DCSEL 0
        lda VERA_ADDR_L
        sta save_l
        lda VERA_ADDR_M
        sta save_m
        lda VERA_ADDR_H
        sta save_h
        lda #<PSG_ADDR
        sta VERA_ADDR_L
        lda #>PSG_ADDR
        sta VERA_ADDR_M
        lda #(VERA_INC1 | ^PSG_ADDR)
        sta VERA_ADDR_H
        ldx #0
@w:     lda buf,x
        sta VERA_DATA0
        inx
        cpx #4*NCH
        bne @w
        lda save_l                      ; restoring the address also
        sta VERA_ADDR_L                 ; refreshes the DATA0 prefetch
        lda save_m
        sta VERA_ADDR_M
        lda save_h
        sta VERA_ADDR_H
        lda #VERA_DCSEL2
        sta VERA_CTRL
        lda save_fx
        sta VERA_FX_CTRL
        lda save_ctrl
        sta VERA_CTRL
        dec wbusy
        rts

; ----------------------------------------------------------------------------
; channel - X = POKEY channel: volume, waveform, divider, constant
; ----------------------------------------------------------------------------
channel:
        txa
        and #$03
        sta cx
        txa
        and #$04
        sta abase
        lda v_audctl
.if NCH > 4
        cpx #4
        bcc @lp
        lda v_audctl2                   ; right POKEY
@lp:
.endif
        sta actl
        lda trackn_audc,x
        and #$0F
        sta c_vol,x
        lda trackn_audc,x
        and #$10                        ; volume only: not reproduced
        beq @nvo
        lda #0
        sta c_vol,x
@nvo:   lda trackn_audc,x
        lsr a
        lsr a
        lsr a
        lsr a
        lsr a
        tay                             ; Y = distortion 0..7
        sty dist
        lda dist_wave,y
        sta c_wave,x
        lda dist_pw,y
        sta c_pw,x
        ldy cx
        lda actl
        and simple_mask,y
        bne @full
        lda #1                          ; 64 kHz 8 bit: word from psgtab
        sta c_simple,x
        ldy dist
        lda dist_sk,y
        sta c_sk,x
        rts

@full:  lda #0
        sta c_simple,x
        sta dvd+1
        sta dvd+2
        sta fast
        lda trackn_audf,x
        sta dvd
        ldy abase                       ; Y = AUDF1 of this POKEY
        lda cx
        bne @n1
        lda actl                        ; channel 1: low byte of 1+2?
        and #$10
        beq @f1
        lda #0
        sta c_vol,x
        rts
@f1:    lda actl
        and #$40
        sta fast
        jmp @div
@n1:    cmp #1
        bne @n2
        lda actl                        ; channel 2: 16 bit 1+2
        and #$10
        beq @div
        lda trackn_audf+0,y
        sta dvd
        lda trackn_audf+1,y
        sta dvd+1
        lda actl
        and #$40
        sta fast
        jmp @div16
@n2:    cmp #2
        bne @n3
        lda actl                        ; channel 3: low byte of 3+4?
        and #$08
        beq @f3
        lda #0
        sta c_vol,x
        rts
@f3:    lda actl
        and #$20
        sta fast
        jmp @div
@n3:    lda actl                        ; channel 4: 16 bit 3+4
        and #$08
        beq @div
        lda trackn_audf+2,y
        sta dvd
        lda trackn_audf+3,y
        sta dvd+1
        lda actl
        and #$20
        sta fast
@div16: lda fast
        beq @base
        lda #7                          ; 16 bit at 1.79 MHz: v + 7 cycles
        bne @addc
@div:   lda fast
        beq @base
        lda #4                          ; 8 bit at 1.79 MHz: v + 4 cycles
@addc:  clc
        adc dvd
        sta c_n0,x
        lda dvd+1
        adc #0
        sta c_n1,x
        lda #0
        adc #0
        sta c_n2,x
        jmp @sound
@base:  inc dvd                         ; (v + 1) * 28 or * 114
        bne @nc
        inc dvd+1
        bne @nc
        inc dvd+2
@nc:    lda actl
        and #$01
        beq @b64
        lda #114
        .byte $2C                       ; BIT abs: skip the next LDA
@b64:   lda #28
        sta base
        lda #0
        sta c_n0,x
        sta c_n1,x
        sta c_n2,x
        ldy #8
@mul:   lsr base
        bcc @nadd
        clc
        lda c_n0,x
        adc dvd
        sta c_n0,x
        lda c_n1,x
        adc dvd+1
        sta c_n1,x
        lda c_n2,x
        adc dvd+2
        sta c_n2,x
@nadd:  asl dvd
        rol dvd+1
        rol dvd+2
        dey
        bne @mul

@sound: ldy dist
        lda dist_k,y
        bpl @k
        ; distortion C: the period depends on n mod 15 (nibble sum, 16 = 1)
        lda c_n0,x
        and #$0F
        sta tmp
        lda c_n0,x
        lsr a
        lsr a
        lsr a
        lsr a
        clc
        adc tmp
        sta tmp
        lda c_n1,x
        and #$0F
        adc tmp
        sta tmp
        lda c_n1,x
        lsr a
        lsr a
        lsr a
        lsr a
        adc tmp
        sta tmp
        lda c_n2,x
        and #$0F
        adc tmp
        sta tmp
        lda c_n2,x
        lsr a
        lsr a
        lsr a
        lsr a
        adc tmp
@m15:   cmp #15
        bcc @r15
        sbc #15
        bcs @m15
@r15:   tay
        lda distc_k,y
        bpl @k
        lda #0                          ; n multiple of 15: no sound
        sta c_vol,x
        lda #3
@k:     sta c_k,x
        rts

; ----------------------------------------------------------------------------
; voice - X = channel: frequency word, then the 4 PSG bytes in buf
; ----------------------------------------------------------------------------
voice:
        lda _psg_hybrid                 ; hybrid: POKEY plays channels 1-4
        beq @nh
        cpx #4
        bcs @nh
        jmp @silent
@nh:    lda c_vol,x
        bne @on
        jmp @silent
@on:    lda c_simple,x
        beq @slow
        lda c_sk,x                      ; word = psgtab[(std*5+kind)*256+AUDF]
        clc
        adc tabofs
        adc #>psgtab_lo
        sta @tl+2
        adc #10                         ; psgtab_hi follows, 10 pages later
        sta @th+2
        lda #$FF
        sta v_keyk,x                    ; the divide cache is no longer valid
        ldy trackn_audf,x
@tl:    lda psgtab_lo,y
        sta wlo
@th:    lda psgtab_hi,y
        sta whi
        jmp @have

@slow:  lda c_n0,x                      ; recompute only when divider or
        cmp v_key0,x                    ; constant change
        bne @calc
        lda c_n1,x
        cmp v_key1,x
        bne @calc
        lda c_n2,x
        cmp v_key2,x
        bne @calc
        lda c_k,x
        cmp v_keyk,x
        beq @cached
@calc:  lda c_n0,x
        sta v_key0,x
        sta dsr
        lda c_n1,x
        sta v_key1,x
        sta dsr+1
        lda c_n2,x
        sta v_key2,x
        sta dsr+2
        lda c_k,x
        sta v_keyk,x
        sta tmp
        asl a
        adc tmp                         ; 3 * k
        adc kofs
        tay
        lda psg_ktab,y
        sta dvd
        lda psg_ktab+1,y
        sta dvd+1
        lda psg_ktab+2,y
        sta dvd+2
        jsr div24
        ldx ch
        lda dvd+2
        beq @fit
        lda #$FF                        ; above the PSG range (> ~24 kHz)
        sta dvd
        sta dvd+1
@fit:   lda dvd
        sta v_wlo,x
        lda dvd+1
        sta v_whi,x
@cached:
        lda v_wlo,x
        sta wlo
        lda v_whi,x
        sta whi

@have:  lda wlo                         ; word 0: silent
        ora whi
        beq @silent
        txa
        asl a
        asl a
        tay                             ; Y = 4 * channel
        lda wlo
        sta buf,y
        lda whi
        sta buf+1,y
        lda c_vol,x
        sty tmp
        tay
        lda vol_log,y
        sta _psg_volume,x
        ldy _psg_stereo
        beq @mono
        ora pan_stereo,x
        .byte $2C                       ; BIT abs: skip the next ORA
@mono:  ora #PAN_LR
        ldy tmp
        sta buf+2,y
        lda c_wave,x
        ora c_pw,x
        sta buf+3,y
        rts

@silent:
        txa
        asl a
        asl a
        tay
        lda #0
        sta buf,y
        sta buf+1,y
        sta buf+2,y
        sta buf+3,y
        sta _psg_volume,x
        rts

; ----------------------------------------------------------------------------
; div24 - dvd (24 bit) / dsr (24 bit) -> dvd quotient, rem remainder
; ----------------------------------------------------------------------------
div24:
        lda #0
        sta rem
        sta rem+1
        sta rem+2
        ldx #24
@l:     asl dvd
        rol dvd+1
        rol dvd+2
        rol rem
        rol rem+1
        rol rem+2
        lda rem
        sec
        sbc dsr
        tay
        lda rem+1
        sbc dsr+1
        sta tmp
        lda rem+2
        sbc dsr+2
        bcc @n
        sta rem+2
        lda tmp
        sta rem+1
        sty rem
        inc dvd
@n:     dex
        bne @l
        rts
