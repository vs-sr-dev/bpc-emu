/* bpc-emu - the ASWAN: the bus, the ports, the interrupt controller, the events, the
 * cartridge's bank registers and SRAM (see aswan.h) */
#include <string.h>

#include "aswan.h"

static uint8_t s_zero[4096], s_sink[4096];

/* ---- the bus: 4 KB pages of the 1 MB ---- */
static void map(aswan *m)
{
	v30mz *c = &m->cpu;
	/* window 0: the internal RAM, 16 KB (above: nothing, read 0) */
	for (int p = 0; p < 16; p++) {
		if (p < 4) {
			c->rd[p] = m->ram + p * 4096;
			c->wr[p] = m->ram + p * 4096;
		} else {
			c->rd[p] = s_zero;
			c->wr[p] = s_sink;
		}
	}
	/* window 1: the SRAM, 64 KB a bank, mirrored */
	for (int p = 0; p < 16; p++) {
		if (m->sram_size) {
			uint8_t *q = m->sram + ((m->sram_base + p * 4096) & (m->sram_size - 1));
			c->rd[16 + p] = q;
			c->wr[16 + p] = q;
		} else {
			c->rd[16 + p] = s_zero;
			c->wr[16 + p] = s_sink;
		}
	}
	/* windows 2 and 3: a 64 KB bank each; 4-15: the 1 MB block register $C0 picks */
	for (int n = 2; n < 16; n++) {
		uint32_t off = n == 2 ? m->base20 : n == 3 ? m->base30
			: (m->base40 + (uint32_t)(n - 4) * 0x10000) & (m->rom_size - 1);
		for (int p = 0; p < 16; p++) {
			c->rd[n * 16 + p] = m->rom + off + p * 4096;
			c->wr[n * 16 + p] = s_sink;
		}
	}
}

/* ---- the cartridge's ports ($C0-$CF): the Bandai 2001 with SRAM ---- */
static void cart_write(aswan *m, int off, uint16_t data, uint16_t mask)
{
	const int lo = mask & 0x00FF, hi = mask & 0xFF00;
	if (off == 0) {
		if (lo)
			m->base40 = (((((data & 0x0F) << 4) | 4) & m->bank_mask) << 16) & (m->rom_size - 1);
		if (hi && m->sram_size)
			m->sram_base = ((uint32_t)(data >> 8) * 0x10000) & (m->sram_size - 1);
	} else if (off == 1) {
		if (lo)
			m->base20 = (((uint32_t)(data & 0xFF) & m->bank_mask) << 16) & (m->rom_size - 1);
		if (hi)
			m->base30 = (((uint32_t)(data >> 8) & m->bank_mask) << 16) & (m->rom_size - 1);
	}
	m->cart_io[off] = (uint16_t)((m->cart_io[off] & ~mask) | (data & mask));
	if (off <= 1)
		map(m);
}

/* ---- interrupts: the highest active and enabled source sets the vector and the CPU's
 * line, which the CPU clears as it takes the interrupt ---- */
static void handle_irqs(aswan *m)
{
	uint8_t a = m->ie & m->iact;
	if (a) {
		int n = 7;
		while (!(a >> n & 1))
			n--;
		m->vector = (uint16_t)(m->ivbase + n);
		m->irq_line = 1;
	} else
		m->irq_line = 0;
}

void aswan_irq_set(aswan *m, uint8_t bit)
{
	if (m->ie & bit) {
		m->iact |= bit;
		handle_irqs(m);
	}
}

/* ---- the ports: a 16-bit bus (offset = port / 2, mask = the bytes accessed) ---- */
static uint16_t port_r16(aswan *m, int off, uint16_t mask)
{
	if (off < 0x20 || (off > 0x50 && off < 0x58) || off == 0x30)
		return video_read(m, off);
	if (off >= 0x40 && off <= 0x4F)
		return audio_read(m, off);
	switch (off) {
	case 0x50: return (uint16_t)(m->sys_ctl | 0x80);
	case 0x58: return (uint16_t)(m->ivbase | m->ser_data << 8);
	case 0x59: return (uint16_t)(m->ie | m->ser_ctl << 8);
	case 0x5A: {
		/* $B5: the rows selected (bits 4-6), read in bits 0-3 */
		uint16_t v = (uint16_t)((m->keysel << 8) & 0xF000);
		if (m->keysel & 0x10)
			v |= (uint16_t)((m->keys & 0xF) << 8);
		if (m->keysel & 0x20)
			v |= (uint16_t)(((m->keys >> 4) & 0xF) << 8);
		if (m->keysel & 0x40)
			v |= (uint16_t)(((m->keys >> 8) & 0xF) << 8);
		return v;
	}
	case 0x5B: return m->iact;
	case 0x5D: return m->iee_data;
	case 0x5E: return m->iee_addr;
	case 0x5F: return m->iee_cmd;
	}
	if (off >= 0x60 && off <= 0x67)
		return m->cart_io[off & 7];
	(void)mask;
	return m->portram[off];
}

static void port_w16(aswan *m, int off, uint16_t data, uint16_t mask)
{
	const int lo = mask & 0x00FF, hi = mask & 0xFF00;
	if (off < 0x20 || (off > 0x50 && off < 0x58)) {
		video_write(m, off, data, mask);
		return;
	}
	switch (off) {
	case 0x30: video_write(m, off, data, mask); break;
	case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
	case 0x48: case 0x49: case 0x4A: case 0x4F:
		audio_write(m, off, data, mask);
		break;
	case 0x50:
		if (lo)
			m->sys_ctl = (uint8_t)(data & 0xFD);
		break;
	case 0x58:
		if (lo)
			m->ivbase = (uint8_t)data;
		if (hi)
			m->ser_data = (uint8_t)(data >> 8);
		break;
	case 0x59:
		if (lo)
			m->ie = (uint8_t)data;
		if (hi) {
			/* the serial port, nothing plugged in: no byte ever comes */
			m->ser_data = 0xFF;
			m->ser_ctl = (uint8_t)(data >> 8);
			if (m->ser_ctl & 0x80)
				m->ser_ctl |= 0x04;
		}
		break;
	case 0x5A:
		if (hi)
			m->keysel = (uint8_t)((data & 0xF000) >> 8);
		break;
	case 0x5B:
		if (lo) {                /* $B6: acknowledge */
			m->iact &= (uint8_t)~data;
			handle_irqs(m);
			data = m->iact;
		}
		break;
	case 0x5D: m->iee_data = (uint16_t)((m->iee_data & ~mask) | (data & mask)); break;
	case 0x5E: m->iee_addr = (uint16_t)((m->iee_addr & ~mask) | (data & mask)); break;
	case 0x5F:
		if (lo) {                /* the internal EEPROM, 64 words */
			m->iee_cmd = (uint8_t)(data & 0xFC);
			uint16_t a = (uint16_t)((m->iee_addr & 0x3F) << 1);
			if (m->iee_cmd & 0x20) {
				m->iee[a] = (uint8_t)m->iee_data;
				m->iee[a + 1] = (uint8_t)(m->iee_data >> 8);
				m->iee_cmd |= 2;
			} else if (m->iee_cmd & 0x10) {
				m->iee_data = (uint16_t)(m->iee[a] | m->iee[a + 1] << 8);
				m->iee_cmd |= 1;
			}
		}
		break;
	default:
		if (off >= 0x60 && off <= 0x67)
			cart_write(m, off & 7, data, mask);
		break;
	}
	m->portram[off] = (uint16_t)((m->portram[off] & ~mask) | (data & mask));
}

uint8_t aswan_in(aswan *m, uint16_t port)
{
	if (port >= 0x100)
		return 0;
	int sh = (port & 1) * 8;
	return (uint8_t)(port_r16(m, port >> 1, (uint16_t)(0xFF << sh)) >> sh);
}

void aswan_out(aswan *m, uint16_t port, uint8_t v)
{
	if (port >= 0x100)
		return;
	int sh = (port & 1) * 8;
	port_w16(m, port >> 1, (uint16_t)(v << sh), (uint16_t)(0xFF << sh));
}

/* the CPU's side: a word access at an even port is one access of the 16-bit bus */
static uint16_t bus_in(void *bus, uint16_t port, int word)
{
	aswan *m = bus;
	if (!word)
		return aswan_in(m, port);
	return port >= 0x100 ? 0 : port_r16(m, port >> 1, 0xFFFF);
}

static void bus_out(void *bus, uint16_t port, uint16_t v, int word)
{
	aswan *m = bus;
	if (!word)
		aswan_out(m, port, (uint8_t)v);
	else if (port < 0x100)
		port_w16(m, port >> 1, v, 0xFFFF);
}

/* HLT: time goes on to the next event; JMP $ with no interrupt pending: to within 12
 * cycles of it (MAME's timeslice skip) */
static void bus_idle(void *bus, int jmp_self)
{
	aswan *m = bus;
	uint64_t t = m->ev_at, *cyc = &m->cpu.cyc;
	if (!jmp_self) {
		if (t > *cyc)
			*cyc = t;
	} else if (!m->irq_line && t > *cyc)
		*cyc = t - (t - *cyc) % 12;
}

/* ---- the machine ---- */
void aswan_init(aswan *m, const uint8_t *rom, uint32_t rom_size, uint8_t *sram, uint32_t sram_size)
{
	memset(m, 0, sizeof *m);
	m->rom = rom;
	m->rom_size = rom_size;
	m->bank_mask = (rom_size >> 16) ? (rom_size >> 16) - 1 : 0;
	m->sram = sram;
	m->sram_size = sram_size;
	m->cpu.bus = m;
	m->cpu.in = bus_in;
	m->cpu.out = bus_out;
	m->cpu.idle = bus_idle;
}

void aswan_reset(aswan *m)
{
	v30mz_reset(&m->cpu);
	memset(m->ram, 0, sizeof m->ram);
	for (int i = 0; i < 8; i++)
		m->cart_io[i] = 0xFFFF;
	m->base20 = m->base30 = ((0xFFu & m->bank_mask) << 16) & (m->rom_size - 1);
	m->base40 = ((((0xF0u & m->bank_mask) | 4) << 16)) & (m->rom_size - 1);
	m->sram_base = 0;
	memset(m->portram, 0, sizeof m->portram);
	m->sys_ctl = m->ivbase = m->ser_data = m->ser_ctl = m->ie = m->iact = m->keysel = 0;
	m->iee_data = m->iee_addr = 0;
	m->iee_cmd = 0;
	m->vector = 0;
	m->irq_line = 0;
	video_reset(m);
	audio_reset(m, m->headphones);
	m->frame = 0;
	m->ev_at = LINE_CYC;
	map(m);
}

void aswan_run_frame(aswan *m)
{
	v30mz *c = &m->cpu;
	for (;;) {
		if (c->cyc >= m->ev_at || c->no_irq || (m->irq_line && c->IF)) {
			while (c->cyc >= m->ev_at) {
				video_line(m);
				m->ev_at += LINE_CYC;
				if (m->v.line == 0) {
					m->frame++;
					audio_sync(m);
					return;      /* before the boundary's interrupt */
				}
			}
			if (c->no_irq)
				c->no_irq = 0;
			else if (m->irq_line && c->IF) {
				m->irq_line = 0;
				v30mz_irq(c, m->vector);
				continue;        /* the handler's first boundary polls too */
			}
		}
		v30mz_step(c);
	}
}

int aswan_audio_take(aswan *m, int16_t *out, int max)
{
	int n = m->s.out_n / 2;
	if (n > max)
		n = max;
	memcpy(out, m->s.out, (size_t)n * 4);
	memmove(m->s.out, m->s.out + n * 2, (size_t)(m->s.out_n - n * 2) * 2);
	m->s.out_n -= n * 2;
	return n;
}
