/* bpc-emu - the ASWAN's display controller: two tile planes (the background, SCR1, and the
 * foreground, SCR2, with its window), 128 sprites with their own window, eight LCD levels
 * and sixteen palettes of four of them; and the line's event: the picture, the HBlank and
 * VBlank timers, VBlank, the line compare. A line is drawn whole at its event, the sprite
 * table is copied at line 144. The mono machine only: a Pocket Challenge has no colour. */
#include <string.h>

#include "aswan.h"

/* ---- registers (offset = port / 2) ---- */
uint16_t video_read(aswan *m, int off)
{
	aswan_video *v = &m->v;
	uint16_t val = v->regs[off & 0x7F];
	switch (off) {
	case 0x01: return (uint16_t)((val & 0xFF00) | v->line);
	case 0x54: return v->hbl_count;
	case 0x55: return v->vbl_count;
	}
	return val;
}

void video_write(aswan *m, int off, uint16_t data, uint16_t mask)
{
	aswan_video *v = &m->v;
	const int lo = mask & 0x00FF, hi = mask & 0xFF00;
	switch (off) {
	case 0x00:
		if (lo) {
			v->bg_on = data & 1;
			v->fg_on = data >> 1 & 1;
			v->spr_on = data >> 2 & 1;
			v->sprwin_on = data >> 3 & 1;
			v->fgwin_mode = (data >> 4) & 3;
		}
		if (hi)
			v->bg_color = (uint8_t)(data >> 8);
		break;
	case 0x01:
		if (hi)
			v->line_cmp = (uint8_t)(data >> 8);
		break;
	case 0x02:
		if (lo)
			v->spr_table = (uint16_t)((data & 0x1F) << 8);
		if (hi)
			v->spr_first_l = (uint8_t)(data >> 8);
		break;
	case 0x03:
		if (lo)
			v->spr_count_l = (uint8_t)data;
		if (hi) {
			v->bg_map = (uint16_t)((data & 0x0700) << 2);
			v->fg_map = (uint16_t)((data & 0x7000) >> 2);
		}
		break;
	case 0x04: if (lo) v->fg_l = (uint8_t)data; if (hi) v->fg_t = (uint8_t)(data >> 8); break;
	case 0x05: if (lo) v->fg_r = (uint8_t)data; if (hi) v->fg_b = (uint8_t)(data >> 8); break;
	case 0x06: if (lo) v->sp_l = (uint8_t)data; if (hi) v->sp_t = (uint8_t)(data >> 8); break;
	case 0x07: if (lo) v->sp_r = (uint8_t)data; if (hi) v->sp_b = (uint8_t)(data >> 8); break;
	case 0x08: if (lo) v->bg_x = (uint8_t)data; if (hi) v->bg_y = (uint8_t)(data >> 8); break;
	case 0x09: if (lo) v->fg_x = (uint8_t)data; if (hi) v->fg_y = (uint8_t)(data >> 8); break;
	case 0x0A:
		if (lo)
			v->lcd = (uint8_t)data;
		if (hi)
			v->icons = (uint8_t)(data >> 8);
		break;
	case 0x0E:
		if (lo) { v->shade[0] = data & 0xF; v->shade[1] = (data >> 4) & 0xF; }
		if (hi) { v->shade[2] = (data >> 8) & 0xF; v->shade[3] = (data >> 12) & 0xF; }
		break;
	case 0x0F:
		if (lo) { v->shade[4] = data & 0xF; v->shade[5] = (data >> 4) & 0xF; }
		if (hi) { v->shade[6] = (data >> 8) & 0xF; v->shade[7] = (data >> 12) & 0xF; }
		break;
	case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
	case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
		v->pal[off & 0x0F] = (uint16_t)((v->pal[off & 0x0F] & ~mask) | (data & 0x7777 & mask));
		break;
	case 0x51:
		if (lo) {
			v->hbl_on = data & 1;
			v->hbl_auto = data >> 1 & 1;
			v->vbl_on = data >> 2 & 1;
			v->vbl_auto = data >> 3 & 1;
		}
		break;
	case 0x52:
		v->hbl_reload = (uint16_t)((v->hbl_reload & ~mask) | (data & mask));
		v->hbl_count = v->hbl_reload;
		break;
	case 0x53:
		v->vbl_reload = (uint16_t)((v->vbl_reload & ~mask) | (data & mask));
		v->vbl_count = v->vbl_reload;
		break;
	}
	v->regs[off & 0x7F] = (uint16_t)((v->regs[off & 0x7F] & ~mask) | (data & mask));
}

void video_reset(aswan *m)
{
	memset(&m->v, 0, sizeof m->v);   /* all levels white until written */
}

/* ---- drawing a line ---- */
static inline uint16_t vram(const aswan *m, uint32_t w)
{
	w &= 0x7FFF;
	return (uint16_t)(m->ram[w * 2] | m->ram[w * 2 + 1] << 8);
}

/* a tile's row: two bit planes, the leftmost pixel in bit 7 */
static inline uint16_t tile_row(const aswan *m, uint32_t num, int row)
{
	return vram(m, 0x1000 + num * 8 + row);
}

/* pens: palette << 2 | colour, 0x100 the background colour */
static inline void pixel(uint16_t *pen, int x, int pal, int px)
{
	/* colour 0 is transparent in palettes 4-7 and 12-15 */
	if (px || !(pal & 4))
		pen[x] = (uint16_t)(pal << 2 | px);
}

/* a tile plane; window: 0 everywhere, 2 inside [l, r], 3 outside it */
static void plane(const aswan *m, uint16_t *pen, int y, uint16_t map, uint8_t sx, uint8_t sy,
	int window, uint8_t wl, uint8_t wr)
{
	const uint16_t base = (uint16_t)((map & 0x1FFF) + (((y + sy) & 0xF8) << 2));
	const uint8_t col0 = sx >> 3;
	for (int col = 0; col < 29; col++) {
		uint16_t d = vram(m, base + ((col0 + col) & 0x1F));
		int row = (y + sy) & 7;
		if (d & 0x8000)
			row = 7 - row;
		uint16_t w = tile_row(m, d & 0x1FF, row);
		uint32_t p0 = w & 0xFF, p1 = (w & 0xFF00) >> 7;
		const int pal = (d >> 9) & 0x0F;
		for (int x = 0; x < 8; x++) {
			int px = (int)((p1 & 2) | (p0 & 1));
			p0 >>= 1;
			p1 >>= 1;
			int xo = (d & 0x4000) ? x + (col << 3) - (sx & 7) : 7 - x + (col << 3) - (sx & 7);
			int ok = xo >= 0 && xo < SCREEN_W;
			if (window == 2)
				ok = ok && xo >= wl && xo <= wr;
			else if (window == 3)
				ok = (xo >= 0 && xo < wl) || (xo > wr && xo < SCREEN_W);
			if (ok)
				pixel(pen, xo, pal, px);
		}
	}
}

static void sprites(const aswan *m, uint16_t *pen, int y, int front)
{
	const aswan_video *v = &m->v;
	if (v->spr_count == 0)
		return;
	for (int i = v->spr_first + v->spr_count - 1; i >= v->spr_first; i--) {
		const int k = (i & 0x7F) * 2;   /* the table wraps at 128 sprites */
		const uint16_t d = v->spr_buf[k];
		const int pri = d >> 13 & 1;
		const uint8_t sy = (uint8_t)v->spr_buf[k + 1], sx = (uint8_t)(v->spr_buf[k + 1] >> 8);
		int row = (y - sy) & 0xFF;
		if (row >= 8 || pri != front)
			continue;
		const int pal = 8 + ((d >> 9) & 7);
		const int win = d >> 12 & 1;
		int clip = 0;
		if (d & 0x8000)
			row = 7 - row;
		uint16_t w = tile_row(m, d & 0x1FF, row);
		uint32_t p0 = w & 0xFF, p1 = (w & 0xFF00) >> 7;
		if (v->sprwin_on) {
			if (win) {
				if (y >= v->sp_t && y <= v->sp_b)
					clip = 1;
			} else if (y < v->sp_t || y > v->sp_b)
				continue;
		}
		for (int j = 0; j < 8; j++) {
			int px = (int)((p1 & 2) | (p0 & 1));
			p0 >>= 1;
			p1 >>= 1;
			int xo = ((d & 0x4000) ? sx + j : sx + 7 - j) & 0xFF;
			if (v->sprwin_on && win && clip && xo >= v->sp_l && xo <= v->sp_r)
				continue;
			if (xo < SCREEN_W)
				pixel(pen, xo, pal, px);
		}
	}
}

static void draw(aswan *m, int y)
{
	const aswan_video *v = &m->v;
	uint8_t *out = m->fb[y];
	if (!v->lcd) {
		memset(out, 15, SCREEN_W);
		return;
	}
	uint16_t pen[SCREEN_W];
	for (int x = 0; x < SCREEN_W; x++)
		pen[x] = 0x100;
	if (v->bg_on)
		plane(m, pen, y, v->bg_map, v->bg_x, v->bg_y, 0, 0, 0);
	if (v->spr_on)
		sprites(m, pen, y, 0);
	if (v->fg_on) {
		switch (v->fgwin_mode) {
		case 0: plane(m, pen, y, v->fg_map, v->fg_x, v->fg_y, 0, 0, 0); break;
		case 1: break;
		case 2:
			if (y >= v->fg_t && y <= v->fg_b)
				plane(m, pen, y, v->fg_map, v->fg_x, v->fg_y, 2, v->fg_l, v->fg_r);
			break;
		default:
			if (y < v->fg_t || y > v->fg_b)
				plane(m, pen, y, v->fg_map, v->fg_x, v->fg_y, 0, 0, 0);
			else
				plane(m, pen, y, v->fg_map, v->fg_x, v->fg_y, 3, v->fg_l, v->fg_r);
			break;
		}
	}
	if (v->spr_on)
		sprites(m, pen, y, 1);
	/* pens to levels: the palette's 3-bit shade, the shade's 4-bit level (0 = white) */
	for (int x = 0; x < SCREEN_W; x++) {
		uint16_t p = pen[x];
		int sh = p == 0x100 ? (v->bg_color & 7) : (v->pal[(p >> 2) & 0x0F] >> ((p & 3) * 4)) & 7;
		out[x] = (uint8_t)(15 - v->shade[sh]);
	}
}

void video_line(aswan *m)
{
	aswan_video *v = &m->v;
	if (v->line < SCREEN_H)
		draw(m, v->line);
	if (v->hbl_on && v->hbl_reload != 0) {
		v->hbl_count--;
		if (v->hbl_count == 0) {
			if (v->hbl_auto)
				v->hbl_count = v->hbl_reload;
			else
				v->hbl_reload = 0;
			aswan_irq_set(m, IRQ_HBLTMR);
		}
	}
	if (v->line == 144) {
		uint16_t a = (uint16_t)(v->spr_table & 0x1FFF);
		for (int i = 0; i < 256; i++)
			v->spr_buf[i] = vram(m, (a + i) & 0x7FFF);
		v->spr_first = v->spr_first_l;
		v->spr_count = v->spr_count_l;
		aswan_irq_set(m, IRQ_VBL);
		if (v->vbl_on && v->vbl_reload != 0) {
			v->vbl_count--;
			if (v->vbl_count == 0) {
				if (v->vbl_auto)
					v->vbl_count = v->vbl_reload;
				else
					v->vbl_reload = 0;
				aswan_irq_set(m, IRQ_VBLTMR);
			}
		}
	}
	if (v->line == v->line_cmp)
		aswan_irq_set(m, IRQ_LCMP);
	v->line = (uint8_t)((v->line + 1) % FRAME_LINES);
}
