/* vu_pm.h — 4-channel VU meter via Player/Missile graphics + a Display
 * List Interrupt (see vu_pm.s for the hardware-level design notes).
 * Doesn't touch the active text screen's content, so it can run
 * alongside normal printf() output before/after playback.
 */
#ifndef VU_PM_H
#define VU_PM_H

/* vu_pm_init() — set up P/M DMA, colors, our display list, and the DLI.
 * Call once, after any printf()s you want visible are done. */
void vu_pm_init(void);

/* vu_pm_set0..3(level) — set channel N's bar height, 0-63. */
void vu_pm_set0(unsigned char level);
void vu_pm_set1(unsigned char level);
void vu_pm_set2(unsigned char level);
void vu_pm_set3(unsigned char level);

/* vu_pm_done() — disable the DLI/P-M DMA and restore the original
 * display list. Call before printf()ing again. */
void vu_pm_done(void);

#endif /* VU_PM_H */
