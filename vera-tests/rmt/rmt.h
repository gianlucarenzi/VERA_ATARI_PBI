#ifndef __RMT_H__
#define __RMT_H__

/* RMT module linked in the program (tools/rmt2ca65.py) */
extern const unsigned char rmt_song[];

/* Init the player on a module; returns the instrument speed (1 = 1x/frame) */
unsigned char __fastcall__ rmt_init(const void *module);

/* Install/remove the player on the immediate VBI */
void rmt_vbi_on(void);
void rmt_vbi_off(void);

/* Bracket every SIO transfer: while active the player only uses channels 1+2 */
void rmt_io_begin(void);
void rmt_io_end(void);

extern volatile unsigned int  rmt_frames;     /* VBI counter */
extern volatile unsigned char rmt_lines;      /* last player time (scanlines) */
extern volatile unsigned char rmt_maxlines;   /* worst player time (scanlines) */
extern volatile unsigned int  rmt_deferred;   /* ticks postponed (VBI with I=1), caught up next frame */
extern volatile unsigned int  rmt_dropped;    /* ticks lost: song tempo error */
extern volatile unsigned char rmt_audc[4];    /* AUDC values computed by the player */

/* RMT_VERA build: output on the VERA PSG as well (psgrmt.s) */
void psg_init(void);                           /* PAL/NTSC constants, voices 0-3 silent */
void psg_silence(void);                        /* voices 0-3 at volume 0 */
extern volatile unsigned char psg_enable;      /* 1 = voices 0-3 follow the player */
extern volatile unsigned char psg_stereo;      /* 1 = ch 1/3 left, ch 2/4 right */
extern volatile unsigned char psg_volume[4];   /* VERA level of the voices (0..63) */
extern volatile unsigned char rmt_pokey_mute;  /* 1 = POKEY kept silent */

#endif
