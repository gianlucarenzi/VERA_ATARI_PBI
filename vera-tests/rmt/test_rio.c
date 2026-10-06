//
// TESTRIO - RMT music on POKEY + VERA PSG while loading assets from disk
// (from AT2019/ATARI-Driver/PokeyATest, main.c)
//
// Files of 2K..16K are loaded from disk, verified (size + checksum) and
// loaded again, forever, while the RMT player runs on the VBI.
//
// During a transfer POKEY channels 3+4 and AUDCTL are the SIO baud rate
// generator: the player leaves them alone and keeps them at volume 0, so
// POKEY plays channels 1+2 only. The VERA PSG has no such limit: it keeps
// playing every channel.
//
// Two loaders (key L):
//   RBL IRQ SIO  own IRQ driven SIO driver (sio.s, default). It leaves
//                CRITIC at 0, so the transfer is bracketed by
//                rmt_io_begin()/rmt_io_end().
//   OS SIOV      sets CRITIC: the player notices the transfer by itself
//                (as with DOS). It can lose the first data byte of a sector
//                when the VBI player runs right after the drive "Complete"
//                byte (see sio.s): BADLNK / checksum errors are expected.
//
// Keys: L loader   N OS SIO noise (SOUNDR)
//       1 POKEY only   2 VERA only   3 both   4 hybrid (RMT8: POKEY 1-4,
//       VERA 5-8)      S VERA stereo          ESC quit
//

#include <stdio.h>
#include <string.h>
#include <conio.h>
#include <atari.h>
#include <peekpoke.h>
#include "vera_detect.h"
#include "rmt.h"
#include "dos2fs.h"
#include "assets.h"

extern unsigned char secbuf[];

#ifndef RMT_SONG_NAME
#define RMT_SONG_NAME "?"
#endif

#define SOUNDR      0x41
#define ATRACT      0x4D

#define MODE_POKEY  1
#define MODE_VERA   2
#define MODE_BOTH   3
#define MODE_HYBRID 4

#define ROW_MODE    5
#define ROW_VU      6           // POKEY row 6, VERA row 7
#define ROW_PLAYER  8
#define ROW_PASS    10
#define ROW_FIRST   12
#define ASSET_ROWS  10      // more assets than rows: lines are reused
#define ROW_MSG     (ROW_FIRST + ASSET_ROWS + 1)
#define VU_ON       0xA0    // inverse space

static const char *const mode_name[5] = { "", "POKEY ", "VERA  ", "BOTH  ", "HYBRID" };

static unsigned char buffer[ASSET_MAXSIZE];
static unsigned char fps;
static unsigned char mode = MODE_BOTH;
static unsigned int  pass, ok_count, err_count;
static unsigned char cur_row;

// OS RTCLOK low 16 bits ($13 hi, $14 lo), incremented by VBI stage 1
static unsigned int frames_now(void)
{
	unsigned char lo, hi;

	do {
		lo = PEEK(0x14);
		hi = PEEK(0x13);
	} while (lo != PEEK(0x14));
	return (hi << 8) | lo;
}

static unsigned int checksum(const unsigned char *p, unsigned int n)
{
	unsigned char s1 = 0, s2 = 0;

	while (n--) {
		s1 += *p++;
		s2 += s1;
	}
	return (s2 << 8) | s1;
}

// drive the program was booted/loaded from (the DCB still describes it)
static unsigned char load_drive(void)
{
	if (OS.dcb.ddevic == 0x31 && OS.dcb.dunit >= 1 && OS.dcb.dunit <= 8)
		return OS.dcb.dunit;
	return 1;
}

static void set_mode(void)
{
	rmt_pokey_mute = (mode == MODE_VERA);
	psg_enable = (mode != MODE_POKEY);
	psg_hybrid = (mode == MODE_HYBRID);
}

static void show_setup(void)
{
	gotoxy(0, 2);
	cprintf("Loader: %s  [L]", fs_loader == FS_LOADER_OS ? "OS SIOV (CRITIC)" : "RBL IRQ SIO     ");
	gotoxy(0, 3);
	cprintf("Output: %s Stereo: %s [1-4 S]", mode_name[mode], psg_stereo ? "on " : "off");
	gotoxy(0, 4);
	cprintf("OS SIO noise: %s  [N]  [ESC]quit", PEEK(SOUNDR) ? "ON " : "OFF");
}

static void show_mode(unsigned char io)
{
	gotoxy(0, ROW_MODE);
	if (io)
		cputs("Disk: POKEY CH1+2 (SIO owns CH3+4)");
	else
		cputs("Idle: POKEY CH1..4                ");
}

// one VU row: POKEY volume 0..15 or VERA level 0..63 (max = 15 / 63,
// also the mask)
static void vu_row(unsigned char row, char label, const volatile unsigned char *lvl,
                   unsigned char max, unsigned char n, unsigned char w)
{
	unsigned char ch, v, i;

	gotoxy(0, row);
	cputc(label);
	cputc(' ');
	for (ch = 0; ch < n; ch++) {
		v = lvl[ch] & max;
		v = (unsigned char)(((unsigned int)v * w + max - 1) / max);
		cputc('1' + ch);
		if (w == 6)
			cputc(':');
		for (i = 0; i < w; i++)
			cputc(i < v ? VU_ON : '.');
		cputc(' ');
	}
}

static void show_vu(void)
{
	// 4 channels: 6 cells each; 8 channels: 2 cells each
	unsigned char w = rmt_tracks > 4 ? 2 : 6;

	vu_row(ROW_VU, 'P', rmt_audc, 15, 4, w);
	vu_row(ROW_VU + 1, 'V', psg_volume, 63, rmt_tracks, w);
	gotoxy(0, ROW_PLAYER);
	cprintf("Player:%3u ln (max%3u) late%5u lost%u", rmt_lines, rmt_maxlines, rmt_deferred, rmt_dropped);
}

static void progress(unsigned int bytes)
{
	gotoxy(18, cur_row);
	cprintf("%5u", bytes);
	show_vu();
}

static unsigned char handle_keys(void)
{
	char c;

	if (!kbhit())
		return 0;
	c = cgetc();
	if (c == CH_ESC)
		return 1;
	if (c == 'n' || c == 'N')
		POKE(SOUNDR, PEEK(SOUNDR) ? 0 : 3);
	else if (c == 'l' || c == 'L')
		fs_loader ^= 1;
	else if (c >= '1' && c <= '3')
		mode = (unsigned char)(c - '0');
	else if (c == '4' && rmt_tracks > 4)
		mode = MODE_HYBRID;
	else if (c == 's' || c == 'S')
		psg_stereo = (unsigned char)!psg_stereo;
	set_mode();
	show_setup();
	return 0;
}

// 1 = ESC
static unsigned char wait_frames(unsigned int n)
{
	unsigned int t0 = frames_now();

	while ((unsigned int)(frames_now() - t0) < n) {
		show_vu();
		if (handle_keys())
			return 1;
	}
	return 0;
}

static void load_asset(unsigned char n)
{
	const asset_t *a = &assets[n];
	fs_entry_t e;
	fs_result_t r;
	unsigned char st, manual;
	unsigned int t0, frames, sum;
	unsigned long bps;

	cur_row = ROW_FIRST + n % ASSET_ROWS;
	gotoxy(0, cur_row);
	cprintf("%.8s.%.3s ", a->name83, a->name83 + 8);
	cclear(40 - 13);
	gotoxy(13, cur_row);
	cputs("LOAD ");

	memset(buffer, 0, sizeof(buffer));

	// the OS SIOV sets CRITIC and the player sees it; the own IRQ driver
	// leaves CRITIC at 0 and has to tell the player
	manual = (fs_loader == FS_LOADER_RBL);
	if (manual)
		rmt_io_begin();
	show_mode(1);
	t0 = frames_now();
	st = fs_find(a->name83, &e);
	if (st == FS_OK)
		st = fs_load(&e, buffer, sizeof(buffer), &r);
	frames = frames_now() - t0;
	if (manual)
		rmt_io_end();
	show_mode(0);

	gotoxy(13, cur_row);
	if (st != FS_OK) {
		++err_count;
		switch (st) {
		case FS_NOTFOUND: cputs("NOT FOUND");                  break;
		case FS_SIOERR:   cprintf("SIO ERR %3u @%u", r.sio_status, r.bad_sector); break;
		case FS_TOOBIG:   cputs("TOO BIG");                    break;
		default:
			cprintf("BADLNK@%u %02X%02X%02X %02X%02X", r.bad_sector,
			        secbuf[125], secbuf[126], secbuf[127], secbuf[0], secbuf[1]);
			break;
		}
		return;
	}

	sum = checksum(buffer, r.bytes);
	if (r.bytes != a->size || sum != a->sum) {
		++err_count;
		cprintf("%5u %04X!=%04X", r.bytes, sum, a->sum);
		return;
	}
	++ok_count;
	bps = frames ? ((unsigned long)r.bytes * fps) / frames : 0;
	cprintf("OK   %5u %4ufr %4lub/s", r.bytes, frames, bps);
}

static void show_pass(void)
{
	gotoxy(0, ROW_PASS);
	cprintf("Pass %u  OK %u  ERR %u  RETRY %u   ", pass, ok_count, err_count, fs_retries);
}

int main(void)
{
	unsigned char n, speed, oldsoundr;

	fps = OS.palnts ? 50 : 60;
	clrscr();
	cputs("TESTRIO  RMT on POKEY+VERA + SIO loader\r\n");
	vera_require();
	fs_unit = load_drive();
	oldsoundr = PEEK(SOUNDR);
	POKE(SOUNDR, 0);                // silence the SIO noise while loading

	cprintf("%s RMT%u %s D%u: %u assets", RMT_SONG_NAME, (unsigned int)rmt_tracks,
	        fps == 50 ? "PAL" : "NTSC", fs_unit, ASSET_COUNT);
	show_setup();

	psg_init();
	set_mode();
	speed = rmt_init(rmt_song);
	gotoxy(0, ROW_MSG);
	cprintf("Instrument speed %u", speed);
#ifndef TEST_NOMUSIC
	rmt_vbi_on();
#endif
	show_mode(0);

	if (wait_frames(fps * 2))       // some music alone before loading
		goto quit;

	for (;;) {
		++pass;
		show_pass();

		for (n = 0; n < ASSET_COUNT; n++) {
			fs_progress = progress;
			load_asset(n);
#ifdef STOP_ON_ERROR
			if (err_count)
				for (;;)
					show_vu();
#endif
			show_pass();
			POKE(ATRACT, 0);        // no attract color cycling
			if (wait_frames(fps))   // one second of full 4 channel music
				goto quit;
		}
	}

quit:
	rmt_vbi_off();                  // silences POKEY and the PSG voices
	POKE(SOUNDR, oldsoundr);
	gotoxy(0, ROW_MSG + 1);
	cputs("Bye.\r\n");
	return 0;
}
