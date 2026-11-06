/* bpc-emu - the Pocket Challenge V2 (see pcv2.h) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pcv2.h"

void pcv2_footer_read(const uint8_t *rom, uint32_t size, pcv2_footer *f)
{
	const uint8_t *b = rom + size - 16;
	memset(f, 0, sizeof *f);
	f->entry_ip = (uint16_t)(b[1] | b[2] << 8);
	f->entry_cs = (uint16_t)(b[3] | b[4] << 8);
	f->publisher = b[6];
	f->color = b[7];
	f->game_id = b[8];
	f->version = b[9];
	f->rom_code = b[10];
	f->save_code = b[11];
	f->flags = b[12];
	f->mapper = b[13];
	f->checksum = (uint16_t)(b[14] | b[15] << 8);
	switch (f->save_code) {
	case 0x01: f->sram_size = 0x2000; break;
	case 0x02: f->sram_size = 0x8000; break;
	case 0x03: f->sram_size = 0x20000; break;
	case 0x04: f->sram_size = 0x40000; break;
	case 0x05: f->sram_size = 0x10000; break;
	}
}

int pcv2_checksum_ok(const uint8_t *rom, uint32_t size)
{
	uint16_t sum = 0;
	for (uint32_t i = 0; i + 2 < size; i++)
		sum = (uint16_t)(sum + rom[i]);
	return sum == (uint16_t)(rom[size - 2] | rom[size - 1] << 8);
}

int pcv2_load(pcv2 *p, const uint8_t *data, size_t size, char *err, size_t errn)
{
	if (size < 0x10000 || size > 0x1000000 || (size & (size - 1))) {
		snprintf(err, errn, "not a cartridge dump: %zu bytes (64 KB to 16 MB, a power of two)", size);
		return 0;
	}
	uint8_t *rom = malloc(size);
	if (!rom) {
		snprintf(err, errn, "out of memory");
		return 0;
	}
	memcpy(rom, data, size);
	pcv2_free(p);
	p->rom = rom;
	p->rom_size = (uint32_t)size;
	pcv2_footer_read(rom, p->rom_size, &p->foot);
	memset(p->sram, 0, sizeof p->sram);
	aswan_init(&p->m, p->rom, p->rom_size, p->sram, p->foot.sram_size);
	pcv2_reset(p);
	return 1;
}

void pcv2_reset(pcv2 *p)
{
	aswan *m = &p->m;
	aswan_reset(m);
	v30mz *c = &m->cpu;
	/* the boot ROM's hand-over: in the PCV2's mode it skips the splash and starts the
	 * cartridge at 4000:0000, DS still on its own segment (WSdev wiki; the cartridges' code
	 * agrees, see docs/RECON.md). What it leaves in the ports is not known: they are as the
	 * reset leaves them, which every cartridge's start-up code sets anyway. */
	if (p->ws_boot) {
		c->sr[CS] = 0xFFFF;
		c->ip = 0x0000;
	} else {
		c->sr[CS] = 0x4000;
		c->ip = 0x0000;
		c->sr[DS] = 0xFF00;
	}
	c->r.w[SP] = 0x2000;
}

void pcv2_free(pcv2 *p)
{
	free(p->rom);
	p->rom = NULL;
	p->rom_size = 0;
}
