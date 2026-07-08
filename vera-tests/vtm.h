/* vtm.h — C-side API for the VTM PSG music player (vtm_player.s).
 * See vtm_format.md for the on-disk/in-memory .vtm layout.
 */
#ifndef VTM_H
#define VTM_H

/* vtm_init() — validate and start playing a .vtm blob already in RAM
 * (as returned by vtm_load_file()). Returns 1 on success, 0 if the blob
 * isn't a recognised VTM1 file or asks for an unsupported channel count. */
unsigned char vtm_init(const void *song);

/* vtm_tick() — advance playback by one VBI frame. Call once per frame from
 * the host's own vblank-poll loop (see test_player.c). No-op if not
 * currently playing. */
void vtm_tick(void);

/* vtm_stop() — silence all channels and stop playback. */
void vtm_stop(void);

/* vtm_level(ch) — current live volume (0-63) of channel ch (0-3). For a
 * VU meter: call once per channel after each vtm_tick(). */
unsigned char vtm_level(unsigned char ch);

/* vtm_load_file() — read a whole .vtm file into a malloc'd buffer.
 * Returns NULL on any I/O error; caller owns the buffer (free() when done). */
void *vtm_load_file(const char *filename);

#endif /* VTM_H */
