/* bpc-emu - the ASWAN: the mono WonderSwan's system-on-chip, which the Pocket Challenge V2
 * is built on (docs/RECON.md), with the cartridge on its bus
 *
 * Time is counted in CPU cycles (3.072 MHz): a line is 256 cycles, a frame 159 lines
 * (75.47 Hz). A line is drawn whole at its event, as MAME's wswan_v does (the hardware
 * reference, read through wskit; see THIRD_PARTY_NOTICES.md). */
#ifndef BPC_ASWAN_H
#define BPC_ASWAN_H

#include <stddef.h>
#include <stdint.h>

#include "v30mz.h"

enum {
	LINE_CYC = 256,
	FRAME_LINES = 159,
	SCREEN_W = 224,
	SCREEN_H = 144,
	CPU_HZ = 3072000,
	AUDIO_HZ = CPU_HZ / 128,     /* a sample every 128 cycles: 24 kHz */
};

/* interrupt sources (ports $B2 / $B6 bits) */
enum { IRQ_STX = 0x01, IRQ_KEY = 0x02, IRQ_RTC = 0x04, IRQ_SRX = 0x08, IRQ_LCMP = 0x10,
	IRQ_VBLTMR = 0x20, IRQ_VBL = 0x40, IRQ_HBLTMR = 0x80 };

/* the key matrix as a word: row $10 in bits 0-3, row $20 in 4-7, row $40 in 8-11 -- the
 * packing every Pocket Challenge cartridge makes of it (pcv2.h names the bits) */

typedef struct aswan_video
{
	uint16_t regs[128];          /* ports $00-$3F, $A2-$AF as written (by word) */
	uint16_t pal[16];            /* $20-$3F: the palettes (3-bit shades) */
	uint8_t shade[8];            /* $1C-$1F: the eight LCD levels (4 bits, 0 = white) */
	uint8_t bg_on, fg_on, spr_on, sprwin_on, fgwin_mode, bg_color;
	uint8_t line, line_cmp;
	uint16_t spr_table;          /* word index */
	uint16_t spr_buf[256];       /* the sprite table, copied at line 144 */
	uint8_t spr_first, spr_count, spr_first_l, spr_count_l;
	uint16_t bg_map, fg_map;     /* word indexes */
	uint8_t fg_l, fg_t, fg_r, fg_b, sp_l, sp_t, sp_r, sp_b;
	uint8_t bg_x, bg_y, fg_x, fg_y;
	uint8_t lcd, icons;
	uint8_t hbl_on, hbl_auto, vbl_on, vbl_auto;
	uint16_t hbl_reload, hbl_count, vbl_reload, vbl_count;
} aswan_video;

typedef struct aswan_audio
{
	struct { uint16_t freq, period; uint32_t pos; uint8_t vol_l, vol_r, on, offset, signal; } ch[4];
	int8_t sweep_step;
	uint32_t sweep_time, sweep_count;
	uint8_t noise_type, noise_on, noise_out;
	uint16_t noise_shift;
	uint16_t sample_addr;
	uint8_t voice, sweep, noise, spk_on, spk_vol, hp_on, hp_conn;
	uint8_t sample_vol, sys_vol;
	uint16_t lout, rout;
	uint64_t done;               /* samples run so far */
	/* the output, stereo int16, until the frontend takes it */
	int16_t out[2 * 8192];
	int out_n;
} aswan_audio;

typedef struct aswan
{
	v30mz cpu;
	uint64_t ev_at;              /* the next line event */
	uint64_t frame;
	uint8_t irq_line;            /* the CPU's interrupt line (cleared as it is taken) */
	uint16_t vector;

	uint8_t ram[0x10000];        /* 16 KB used; the video reads the 64 KB (the rest stays 0) */

	/* the cartridge: ROM, SRAM, and the Bandai 2001's bank registers */
	const uint8_t *rom;
	uint32_t rom_size, bank_mask;
	uint8_t *sram;
	uint32_t sram_size;
	uint16_t cart_io[8];         /* ports $C0-$CF as written */
	uint32_t base20, base30, base40, sram_base;   /* byte offsets */

	/* the system */
	uint16_t portram[128];
	uint8_t sys_ctl, ivbase, ser_data, ser_ctl, ie, iact, keysel;
	uint16_t iee_data, iee_addr;
	uint8_t iee_cmd;
	uint8_t iee[128];            /* the internal EEPROM */
	uint16_t keys;               /* the key matrix (1 = held) */
	uint8_t headphones;          /* plugged in (else the speaker); kept across resets */

	aswan_video v;
	aswan_audio s;
	uint8_t fb[SCREEN_H][SCREEN_W];   /* the picture: LCD levels, 0 black .. 15 white */
} aswan;

/* rom and sram stay the caller's; sram_size a power of two (0: none) */
void aswan_init(aswan *m, const uint8_t *rom, uint32_t rom_size, uint8_t *sram, uint32_t sram_size);
/* the CPU and the chip as MAME's reset leaves them, the banks at $FF; the boot code's
 * state is the caller's (pcv2.c) */
void aswan_reset(aswan *m);
/* to the next frame: the event of the line that wraps to 0 */
void aswan_run_frame(aswan *m);
void aswan_out(aswan *m, uint16_t port, uint8_t v);
uint8_t aswan_in(aswan *m, uint16_t port);
/* audio produced so far, stereo frames; returns how many */
int aswan_audio_take(aswan *m, int16_t *out, int max);

/* aswan_video.c */
uint16_t video_read(aswan *m, int off);
void video_write(aswan *m, int off, uint16_t data, uint16_t mask);
void video_line(aswan *m);
void video_reset(aswan *m);
void aswan_irq_set(aswan *m, uint8_t bit);

/* aswan_audio.c */
void audio_sync(aswan *m);
uint16_t audio_read(aswan *m, int off);
void audio_write(aswan *m, int off, uint16_t data, uint16_t mask);
void audio_reset(aswan *m, int headphones);

#endif
