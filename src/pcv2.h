/* bpc-emu - the Benesse Pocket Challenge V2: an ASWAN, nine buttons, a cartridge, and a
 * boot that goes straight into the cartridge at 4000:0000 (docs/RECON.md) */
#ifndef BPC_PCV2_H
#define BPC_PCV2_H

#include <stddef.h>
#include <stdint.h>

#include "aswan.h"

/* the buttons: their bits in the key matrix (aswan.h), as the cartridges pack it */
enum {
	PCV2_CLEAR = 0x001, PCV2_CIRCLE = 0x004, PCV2_PASS = 0x008,   /* row $10 */
	PCV2_VIEW = 0x010, PCV2_ESC = 0x040, PCV2_RIGHT = 0x080,      /* row $20 */
	PCV2_LEFT = 0x100, PCV2_DOWN = 0x400, PCV2_UP = 0x800,        /* row $40 */
};

/* the cartridge's footer (its last 16 bytes) */
typedef struct pcv2_footer
{
	uint16_t entry_cs, entry_ip; /* the far jump at FFFF:0000 */
	uint8_t publisher, color, game_id, version, rom_code, save_code, flags, mapper;
	uint16_t checksum;
	uint32_t sram_size;          /* from save_code; 0: none (or an EEPROM, not supported) */
} pcv2_footer;

typedef struct pcv2
{
	aswan m;
	uint8_t *rom;
	uint32_t rom_size;
	pcv2_footer foot;
	uint8_t sram[0x40000];
	int ws_boot;                 /* start as a WonderSwan does, at the footer's jump */
} pcv2;

void pcv2_footer_read(const uint8_t *rom, uint32_t size, pcv2_footer *f);
/* checks the sum the footer holds: the 16-bit sum of every byte but the last two */
int pcv2_checksum_ok(const uint8_t *rom, uint32_t size);
/* a dump (copied; a power of two, 64 KB to 16 MB); 0 and a message in err if it is not one */
int pcv2_load(pcv2 *p, const uint8_t *data, size_t size, char *err, size_t errn);
/* the power-on: the machine reset, then the state the boot ROM hands over */
void pcv2_reset(pcv2 *p);
void pcv2_free(pcv2 *p);

#endif
