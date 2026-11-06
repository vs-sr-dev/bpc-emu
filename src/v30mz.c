/* bpc-emu - the NEC V30MZ (see v30mz.h)
 *
 * What MAME's core does and the 80186 may not, kept here because the oracle does it:
 * a word at an odd address and a jump to an odd address cost a cycle; a shift's count is
 * not masked; multi-bit rotates leave OF alone; IMUL's CF/OF look only at the high half;
 * AAM/AAD ignore their operand; REP runs to the end in one go (no interrupt inside);
 * JL/JNL look at Z too. Not modelled: the prefetch queue (no Pocket Challenge cartridge
 * writes into the code it runs or switches the bank under it). */
#include <string.h>

#include "v30mz.h"

static uint8_t s_parity[256];   /* 1: even number of bits */

/* ---- the bus ---- */
static inline uint8_t rb(v30mz *c, uint16_t seg, uint16_t off)
{
	uint32_t a = v30mz_lin(seg, off);
	return c->rd[a >> 12][a & 0xFFF];
}

static inline void wb(v30mz *c, uint16_t seg, uint16_t off, uint8_t v)
{
	uint32_t a = v30mz_lin(seg, off);
	c->wr[a >> 12][a & 0xFFF] = v;
}

/* a word at an odd offset: two bytes (the second wrapping in the segment) and a cycle */
static inline uint16_t rw(v30mz *c, uint16_t seg, uint16_t off)
{
	if (off & 1) {
		c->cyc++;
		return (uint16_t)(rb(c, seg, off) | rb(c, seg, (uint16_t)(off + 1)) << 8);
	}
	uint32_t a = v30mz_lin(seg, off);
	const uint8_t *p = c->rd[a >> 12] + (a & 0xFFF);
	return (uint16_t)(p[0] | p[1] << 8);
}

static inline void ww(v30mz *c, uint16_t seg, uint16_t off, uint16_t v)
{
	if (off & 1) {
		wb(c, seg, off, (uint8_t)v);
		wb(c, seg, (uint16_t)(off + 1), (uint8_t)(v >> 8));
		c->cyc++;
		return;
	}
	uint32_t a = v30mz_lin(seg, off);
	uint8_t *p = c->wr[a >> 12] + (a & 0xFFF);
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static uint16_t port_in(v30mz *c, uint16_t port, int word)
{
	if (word && (port & 1)) {
		c->cyc++;
		return (uint16_t)(c->in(c->bus, port, 0) | c->in(c->bus, (uint16_t)(port + 1), 0) << 8);
	}
	return c->in(c->bus, port, word);
}

static void port_out(v30mz *c, uint16_t port, uint16_t v, int word)
{
	if (word && (port & 1)) {
		c->cyc++;
		c->out(c->bus, port, (uint8_t)v, 0);
		c->out(c->bus, (uint16_t)(port + 1), (uint8_t)(v >> 8), 0);
		return;
	}
	c->out(c->bus, port, word ? v : (uint8_t)v, word);
}

/* ---- registers, the stack ---- */
#define AL (c->r.b[0])
#define AH (c->r.b[1])
#define CL (c->r.b[2])
#define R16(n) (c->r.w[n])

static inline uint8_t r8(const v30mz *c, int n)
{
	return c->r.b[n < 4 ? n * 2 : (n - 4) * 2 + 1];
}

static inline void s8(v30mz *c, int n, uint8_t v)
{
	c->r.b[n < 4 ? n * 2 : (n - 4) * 2 + 1] = v;
}

static inline void push(v30mz *c, uint16_t v)
{
	c->r.w[SP] -= 2;
	ww(c, c->sr[SS], c->r.w[SP], v);
}

static inline uint16_t pop(v30mz *c)
{
	uint16_t v = rw(c, c->sr[SS], c->r.w[SP]);
	c->r.w[SP] += 2;
	return v;
}

/* ---- flags ---- */
#define CF (c->CarryVal != 0)
#define SF (c->SignVal < 0)
#define ZF (c->ZeroVal == 0)
#define PF (s_parity[(uint8_t)c->ParityVal])
#define AF (c->AuxVal != 0)
#define OF (c->OverVal != 0)

uint16_t v30mz_flags(const v30mz *c)
{
	return (uint16_t)(0x7002 | (CF ? 1 : 0) | (PF ? 4 : 0) | (AF ? 0x10 : 0) | (ZF ? 0x40 : 0) |
		(SF ? 0x80 : 0) | c->TF << 8 | c->IF << 9 | c->DF << 10 | (OF ? 0x800 : 0) |
		(c->MF ? 0x8000 : 0));
}

static void set_flags(v30mz *c, uint16_t f)
{
	c->CarryVal = f & 1;
	c->ParityVal = !(f & 4);
	c->AuxVal = f & 16;
	c->ZeroVal = !(f & 64);
	c->SignVal = (f & 128) ? -1 : 0;
	c->TF = (f & 256) != 0;
	c->IF = (f & 512) != 0;
	c->DF = (f & 1024) != 0;
	c->OverVal = f & 2048;
	c->MF = (f & 0x8000) != 0;
}

static inline void szp8(v30mz *c, uint32_t x)
{
	c->SignVal = (int8_t)x;
	c->ZeroVal = c->ParityVal = (uint32_t)(int32_t)(int8_t)x;
}

static inline void szp16(v30mz *c, uint32_t x)
{
	c->SignVal = (int16_t)x;
	c->ZeroVal = c->ParityVal = (uint32_t)(int32_t)(int16_t)x;
}

static int cond(const v30mz *c, int cc)
{
	switch (cc) {
	case 0: return OF;
	case 1: return !OF;
	case 2: return CF;
	case 3: return !CF;
	case 4: return ZF;
	case 5: return !ZF;
	case 6: return CF || ZF;
	case 7: return !(CF || ZF);
	case 8: return SF;
	case 9: return !SF;
	case 10: return PF;
	case 11: return !PF;
	case 12: return (SF != OF) && !ZF;
	case 13: return ZF || (SF == OF);
	case 14: return ZF || (SF != OF);
	default: return (SF == OF) && !ZF;
	}
}

/* ---- the ALU ---- */
static uint32_t add8(v30mz *c, uint32_t d, uint32_t s, uint32_t ci)
{
	uint32_t r = (d & 0xFF) + (s & 0xFF) + ci;
	c->CarryVal = r & 0x100;
	c->OverVal = (r ^ s) & (r ^ d) & 0x80;
	c->AuxVal = (r ^ (s ^ d)) & 0x10;
	szp8(c, r);
	return r & 0xFF;
}

static uint32_t add16(v30mz *c, uint32_t d, uint32_t s, uint32_t ci)
{
	uint32_t r = (d & 0xFFFF) + (s & 0xFFFF) + ci;
	c->CarryVal = r & 0x10000;
	c->OverVal = (r ^ s) & (r ^ d) & 0x8000;
	c->AuxVal = (r ^ (s ^ d)) & 0x10;
	szp16(c, r);
	return r & 0xFFFF;
}

static uint32_t sub8(v30mz *c, uint32_t d, uint32_t s, uint32_t ci)
{
	uint32_t r = (d & 0xFF) - (s & 0xFF) - ci;
	c->CarryVal = r & 0x100;
	c->OverVal = (d ^ s) & (d ^ r) & 0x80;
	c->AuxVal = (r ^ (s ^ d)) & 0x10;
	szp8(c, r);
	return r & 0xFF;
}

static uint32_t sub16(v30mz *c, uint32_t d, uint32_t s, uint32_t ci)
{
	uint32_t r = (d & 0xFFFF) - (s & 0xFFFF) - ci;
	c->CarryVal = r & 0x10000;
	c->OverVal = (d ^ s) & (d ^ r) & 0x8000;
	c->AuxVal = (r ^ (s ^ d)) & 0x10;
	szp16(c, r);
	return r & 0xFFFF;
}

static uint32_t logic8(v30mz *c, uint32_t r)
{
	c->CarryVal = c->OverVal = c->AuxVal = 0;
	szp8(c, r);
	return r & 0xFF;
}

static uint32_t logic16(v30mz *c, uint32_t r)
{
	c->CarryVal = c->OverVal = c->AuxVal = 0;
	szp16(c, r);
	return r & 0xFFFF;
}

/* op: the row (add or adc sbb and sub xor cmp); cmp returns d */
static uint32_t alu8(v30mz *c, int op, uint32_t d, uint32_t s)
{
	switch (op) {
	case 0: return add8(c, d, s, 0);
	case 1: return logic8(c, d | s);
	case 2: return add8(c, d, s, CF);
	case 3: return sub8(c, d, s, CF);
	case 4: return logic8(c, d & s);
	case 5: return sub8(c, d, s, 0);
	case 6: return logic8(c, d ^ s);
	default: sub8(c, d, s, 0); return d & 0xFF;
	}
}

static uint32_t alu16(v30mz *c, int op, uint32_t d, uint32_t s)
{
	switch (op) {
	case 0: return add16(c, d, s, 0);
	case 1: return logic16(c, d | s);
	case 2: return add16(c, d, s, CF);
	case 3: return sub16(c, d, s, CF);
	case 4: return logic16(c, d & s);
	case 5: return sub16(c, d, s, 0);
	case 6: return logic16(c, d ^ s);
	default: sub16(c, d, s, 0); return d & 0xFFFF;
	}
}

static uint32_t inc8(v30mz *c, uint32_t t)
{
	uint32_t r = t + 1;
	c->OverVal = t == 0x7F;
	c->AuxVal = (r ^ (t ^ 1)) & 0x10;
	szp8(c, r);
	return r & 0xFF;
}

static uint32_t dec8(v30mz *c, uint32_t t)
{
	uint32_t r = t - 1;
	c->OverVal = t == 0x80;
	c->AuxVal = (r ^ (t ^ 1)) & 0x10;
	szp8(c, r);
	return r & 0xFF;
}

static uint32_t inc16(v30mz *c, uint32_t t)
{
	uint32_t r = t + 1;
	c->OverVal = t == 0x7FFF;
	c->AuxVal = (r ^ (t ^ 1)) & 0x10;
	szp16(c, r);
	return r & 0xFFFF;
}

static uint32_t dec16(v30mz *c, uint32_t t)
{
	uint32_t r = t - 1;
	c->OverVal = t == 0x8000;
	c->AuxVal = (r ^ (t ^ 1)) & 0x10;
	szp16(c, r);
	return r & 0xFFFF;
}

/* shifts and rotates (op: rol ror rcl rcr shl shr - sar); `one`: the D0/D1 form, which sets
 * OF; returns 0 when nothing is to be stored (a count of 0, the undefined /6) */
static int shift(v30mz *c, int op, uint32_t v, uint32_t cnt, int one, int word, uint32_t *out)
{
	const uint32_t top = word ? 0x8000 : 0x80, carry = top << 1, mask = carry - 1;
	uint32_t d = v & mask;
	if (one)
		cnt = 1;
	else if (cnt == 0)
		return 0;
	switch (op) {
	case 0:
		for (uint32_t k = 0; k < cnt; k++) {
			c->CarryVal = d & top;
			d = ((d << 1) | (CF ? 1 : 0)) & mask;
		}
		break;
	case 1:
		for (uint32_t k = 0; k < cnt; k++) {
			c->CarryVal = d & 1;
			d = (d >> 1) | (CF ? top : 0);
		}
		break;
	case 2:
		for (uint32_t k = 0; k < cnt; k++) {
			d = (d << 1) | (CF ? 1 : 0);
			c->CarryVal = d & carry;
			d &= mask;
		}
		break;
	case 3:
		for (uint32_t k = 0; k < cnt; k++) {
			d |= CF ? carry : 0;
			c->CarryVal = d & 1;
			d >>= 1;
		}
		break;
	case 4:
		c->cyc += cnt;
		d <<= (cnt & 31);
		c->CarryVal = d & carry;
		break;
	case 5:
		c->cyc += cnt;
		d >>= ((cnt - 1) & 31);
		c->CarryVal = d & 1;
		d >>= 1;
		break;
	case 6:
		return 0;
	default:
		c->cyc += cnt;
		d = word ? (uint32_t)((int32_t)(int16_t)d >> ((cnt - 1) & 31))
			: (uint32_t)((int32_t)(int8_t)d >> ((cnt - 1) & 31));
		c->CarryVal = d & 1;
		d >>= 1;
		break;
	}
	if (op >= 4) {
		if (word)
			szp16(c, d);
		else
			szp8(c, d);
	}
	if (one)
		c->OverVal = op == 7 ? 0 : (v ^ d) & top;
	*out = d & mask;
	return 1;
}

/* BCD */
static void adj4(v30mz *c, int p1, int p2)
{
	if (AF || (AL & 0xF) > 9) {
		uint16_t t = (uint16_t)(AL + p1);
		AL = (uint8_t)t;
		c->AuxVal = 1;
		c->CarryVal |= t & 0x100;
	}
	if (CF || AL > 0x9F) {
		AL = (uint8_t)(AL + p2);
		c->CarryVal = 1;
	}
	szp8(c, AL);
}

static void adjb(v30mz *c, int p1, int p2)
{
	if (AF || (AL & 0xF) > 9) {
		AL = (uint8_t)(AL + p1);
		AH = (uint8_t)(AH + p2);
		c->CarryVal = c->AuxVal;
		c->AuxVal = 1;
	} else {
		c->AuxVal = 0;
		c->CarryVal = 0;
	}
	AL &= 0x0F;
}

/* multiply and divide; the divides return 0 when they trap */
static void mul(v30mz *c, uint32_t v, int word, int sign)
{
	if (!word) {
		int32_t r = sign ? (int16_t)(int8_t)AL * (int16_t)(int8_t)v : (int32_t)(AL * (v & 0xFF));
		R16(AX) = (uint16_t)r;
		c->CarryVal = c->OverVal = AH != 0;
	} else {
		uint32_t r = sign ? (uint32_t)((int32_t)(int16_t)R16(AX) * (int32_t)(int16_t)v)
			: (uint32_t)R16(AX) * (v & 0xFFFF);
		R16(AX) = (uint16_t)r;
		R16(DX) = (uint16_t)(r >> 16);
		c->CarryVal = c->OverVal = R16(DX) != 0;
	}
}

static uint32_t imul3(v30mz *c, uint32_t a, uint32_t b)
{
	int32_t r = (int32_t)(int16_t)a * (int32_t)(int16_t)b;
	c->CarryVal = c->OverVal = ((r >> 15) != 0) && ((r >> 15) != -1);
	return (uint32_t)r & 0xFFFF;
}

static int divide(v30mz *c, uint32_t v, int word, int sign)
{
	if (!word && !sign) {
		v &= 0xFF;
		if (!v)
			return 0;
		uint32_t a = R16(AX), q = a / v, r = a % v;
		if (q > 0xFF)
			return 0;
		AL = (uint8_t)q;
		AH = (uint8_t)r;
	} else if (!word) {
		int32_t d = (int16_t)(int8_t)v;
		if (!d)
			return 0;
		int32_t a = (int16_t)R16(AX), q = a / d, r = a % d;
		if (q > 0xFF)        /* MAME tests the quotient against 0xFF only */
			return 0;
		AL = (uint8_t)q;
		AH = (uint8_t)r;
	} else if (!sign) {
		v &= 0xFFFF;
		if (!v)
			return 0;
		uint32_t a = (uint32_t)R16(DX) << 16 | R16(AX), q = a / v, r = a % v;
		if (q > 0xFFFF)
			return 0;
		R16(AX) = (uint16_t)q;
		R16(DX) = (uint16_t)r;
	} else {
		int32_t d = (int16_t)v;
		if (!d)
			return 0;
		int32_t a = (int32_t)((uint32_t)R16(DX) << 16 | R16(AX));
		if (a == INT32_MIN && d == -1)
			return 0;
		int32_t q = a / d, r = a % d;
		if (q > 0xFFFF)
			return 0;
		R16(AX) = (uint16_t)q;
		R16(DX) = (uint16_t)r;
	}
	return 1;
}

/* string operations (6C-6F, A4-A7, AA-AF); seg: the source segment */
static void string_one(v30mz *c, int op, uint16_t seg)
{
	const int d1 = c->DF ? -1 : 1, d2 = c->DF ? -2 : 2;
	uint16_t *si = &c->r.w[SI], *di = &c->r.w[DI];
	switch (op) {
	case 0x6C: wb(c, c->sr[ES], *di, (uint8_t)port_in(c, R16(DX), 0)); *di += d1; c->cyc += 6; break;
	case 0x6D: ww(c, c->sr[ES], *di, port_in(c, R16(DX), 1)); *di += d2; c->cyc += 6; break;
	case 0x6E: port_out(c, R16(DX), rb(c, seg, *si), 0); *si += d1; c->cyc += 7; break;
	case 0x6F: port_out(c, R16(DX), rw(c, seg, *si), 1); *si += d2; c->cyc += 7; break;
	case 0xA4: {
		uint8_t t = rb(c, seg, *si);
		wb(c, c->sr[ES], *di, t);
		*di += d1; *si += d1; c->cyc += 5;
		break;
	}
	case 0xA5: {
		uint16_t t = rw(c, seg, *si);
		ww(c, c->sr[ES], *di, t);
		*di += d2; *si += d2; c->cyc += 5;
		break;
	}
	case 0xA6: {
		uint32_t s = rb(c, c->sr[ES], *di), d = rb(c, seg, *si);
		sub8(c, d, s, 0);
		*di += d1; *si += d1; c->cyc += 6;
		break;
	}
	case 0xA7: {
		uint32_t s = rw(c, c->sr[ES], *di), d = rw(c, seg, *si);
		sub16(c, d, s, 0);
		*di += d2; *si += d2; c->cyc += 6;
		break;
	}
	case 0xAA: wb(c, c->sr[ES], *di, AL); *di += d1; c->cyc += 3; break;
	case 0xAB: ww(c, c->sr[ES], *di, R16(AX)); *di += d2; c->cyc += 3; break;
	case 0xAC: AL = rb(c, seg, *si); *si += d1; c->cyc += 3; break;
	case 0xAD: R16(AX) = rw(c, seg, *si); *si += d2; c->cyc += 3; break;
	case 0xAE: sub8(c, AL, rb(c, c->sr[ES], *di), 0); *di += d1; c->cyc += 4; break;
	default: sub16(c, R16(AX), rw(c, c->sr[ES], *di), 0); *di += d2; c->cyc += 4; break;
	}
}

static void string_op(v30mz *c, int op, int rep, uint16_t seg)
{
	if (!rep) {
		string_one(c, op, seg);
		return;
	}
	c->cyc += 3;
	uint16_t n = R16(CX);
	const int cmp = op == 0xA6 || op == 0xA7 || op == 0xAE || op == 0xAF;
	while (n) {
		string_one(c, op, seg);
		n--;
		if (cmp && (rep == 0xF3 ? !ZF : ZF))
			break;
	}
	R16(CX) = n;
}

/* ---- interrupts ---- */
/* pushes FLAGS, CS and `ret`, then CS:IP from vector n of the table at 0000:0000 */
static void enter_vector(v30mz *c, int n, uint16_t ret)
{
	push(c, v30mz_flags(c));
	c->cyc += 2;
	c->TF = c->IF = 0;
	uint16_t off = rw(c, 0, (uint16_t)(n * 4)), seg = rw(c, 0, (uint16_t)(n * 4 + 2));
	push(c, c->sr[CS]);
	push(c, ret);
	c->sr[CS] = seg;
	c->ip = off;
	c->cyc += off & 1;
}

void v30mz_irq(v30mz *c, int n)
{
	enter_vector(c, n & 0xFF, c->ip);
}

/* ---- decoding ---- */
static const uint8_t s_has_modrm[256] = {
	1,1,1,1,0,0,0,0, 1,1,1,1,0,0,0,0,  1,1,1,1,0,0,0,0, 1,1,1,1,0,0,0,0,   /* 00-1F */
	1,1,1,1,0,0,0,0, 1,1,1,1,0,0,0,0,  1,1,1,1,0,0,0,0, 1,1,1,1,0,0,0,0,   /* 20-3F */
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,   /* 40-5F */
	0,0,1,0,0,0,0,0, 0,1,0,1,0,0,0,0,  0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,   /* 60-7F */
	1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,  0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,   /* 80-9F */
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,   /* A0-BF */
	1,1,0,0,1,1,1,1, 0,0,0,0,0,0,0,0,  1,1,1,1,0,0,0,0, 1,0,0,0,0,0,0,0,   /* C0-DF (D8: its byte only) */
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,  0,0,0,0,0,0,1,1, 0,0,0,0,0,0,1,1,   /* E0-FF */
};

static inline int is_seg_prefix(uint8_t b)
{
	return b == 0x26 || b == 0x2E || b == 0x36 || b == 0x3E;
}

static inline int is_string(uint8_t b)
{
	return (b >= 0x6C && b <= 0x6F) || (b >= 0xA4 && b <= 0xA7) || (b >= 0xAA && b <= 0xAF);
}

/* the instruction being run */
typedef struct insn
{
	v30mz *c;
	uint16_t cs, pos;            /* the fetch position */
	uint32_t clk;                /* cycles of the prefixes and the address */
	int pre;                     /* a segment override */
	uint16_t pbase;
	uint8_t m;                   /* ModRM */
	int isreg;
	uint16_t eo, es;             /* the memory operand */
} insn;

static inline uint8_t fetch(insn *i)
{
	return rb(i->c, i->cs, i->pos++);
}

static inline uint16_t fetchw(insn *i)
{
	uint16_t lo = fetch(i);
	return (uint16_t)(lo | fetch(i) << 8);
}

static void modrm(insn *i, int with_ea)
{
	v30mz *c = i->c;
	i->m = fetch(i);
	i->isreg = i->m >= 0xC0;
	if (i->isreg || !with_ea)
		return;
	const int mod = i->m >> 6, rm = i->m & 7;
	uint16_t disp = 0, eo;
	if (mod == 1)
		disp = (uint16_t)(int16_t)(int8_t)fetch(i);
	else if (mod == 2)
		disp = fetchw(i);
	int seg = DS;
	switch (rm) {
	case 0: eo = (uint16_t)(R16(BX) + R16(SI)); i->clk++; break;
	case 1: eo = (uint16_t)(R16(BX) + R16(DI)); i->clk++; break;
	case 2: eo = (uint16_t)(R16(BP) + R16(SI)); seg = SS; i->clk++; break;
	case 3: eo = (uint16_t)(R16(BP) + R16(DI)); seg = SS; i->clk++; break;
	case 4: eo = R16(SI); break;
	case 5: eo = R16(DI); break;
	case 6:
		if (mod == 0)
			eo = fetchw(i);
		else {
			eo = R16(BP);
			seg = SS;
		}
		break;
	default: eo = R16(BX); break;
	}
	i->eo = (uint16_t)(eo + disp);
	i->es = i->pre ? i->pbase : c->sr[seg];
}

static inline uint32_t rm8(insn *i)
{
	return i->isreg ? r8(i->c, i->m & 7) : rb(i->c, i->es, i->eo);
}

static inline uint32_t rm16(insn *i)
{
	return i->isreg ? i->c->r.w[i->m & 7] : rw(i->c, i->es, i->eo);
}

static inline void wrm8(insn *i, uint32_t v)
{
	if (i->isreg)
		s8(i->c, i->m & 7, (uint8_t)v);
	else
		wb(i->c, i->es, i->eo, (uint8_t)v);
}

static inline void wrm16(insn *i, uint32_t v)
{
	if (i->isreg)
		i->c->r.w[i->m & 7] = (uint16_t)v;
	else
		ww(i->c, i->es, i->eo, (uint16_t)v);
}

/* the instruction's cycles: r with a register operand, mm with memory */
static inline void clkm(insn *i, uint32_t r, uint32_t mm)
{
	i->c->cyc += i->clk + (i->isreg ? r : mm);
}

void v30mz_step(v30mz *c)
{
	insn I = { 0 }, *i = &I;
	i->c = c;
	i->cs = c->sr[CS];
	i->pos = c->ip;
	uint16_t ip = 0;
	int xfer = 0;                /* ip set by a transfer */
#define JUMP(t) do { ip = (uint16_t)(t); xfer = 1; c->cyc += ip & 1; } while (0)

	i->pre = c->pre_on;
	i->pbase = c->pre_base;
	c->pre_on = 0;
	uint8_t op = fetch(i);
	while (is_seg_prefix(op)) {
		i->pre = 1;
		i->pbase = c->sr[(op >> 3) & 3];
		i->clk += 1;
		op = fetch(i);
	}
	int rep = 0;
	if (op == 0xF2 || op == 0xF3) {
		const uint16_t after = i->pos;
		uint8_t nx = fetch(i);
		int rseg = 0;
		uint16_t rbase = 0;
		if (is_seg_prefix(nx)) {
			rseg = 1;
			rbase = c->sr[(nx >> 3) & 3];
			nx = fetch(i);
		}
		if (is_string(nx)) {
			rep = op;
			op = nx;
			if (rseg) {
				i->pre = 1;
				i->pbase = rbase;
				i->clk += 2;
			}
		} else {
			/* the prefix alone (MAME backs IP up over the byte it looked at); a segment
			 * prefix it read goes with the next instruction, which no interrupt comes before */
			if (rseg) {
				c->pre_on = 1;
				c->pre_base = rbase;
				c->no_irq = 1;
				i->clk += 2;
				c->ip = (uint16_t)(i->pos - 1);
			} else
				c->ip = after;
			c->cyc += i->clk;
			return;
		}
	}
	if (s_has_modrm[op])
		modrm(i, op != 0xD8);
	const int rf = (i->m >> 3) & 7;
	const uint16_t dseg = i->pre ? i->pbase : c->sr[DS];

	if (op < 0x40 && (op & 7) < 6) {
		const int a = op >> 3;
		uint32_t r;
		switch (op & 7) {
		case 0:
			clkm(i, 1, a == 7 ? 2 : 3);
			r = alu8(c, a, rm8(i), r8(c, rf));
			if (a != 7)
				wrm8(i, r);
			break;
		case 1:
			clkm(i, 1, a == 7 ? 2 : 3);
			r = alu16(c, a, rm16(i), R16(rf));
			if (a != 7)
				wrm16(i, r);
			break;
		case 2:
			clkm(i, 1, 2);
			r = alu8(c, a, r8(c, rf), rm8(i));
			if (a != 7)
				s8(c, rf, (uint8_t)r);
			break;
		case 3:
			clkm(i, 1, 2);
			r = alu16(c, a, R16(rf), rm16(i));
			if (a != 7)
				R16(rf) = (uint16_t)r;
			break;
		case 4: {
			uint32_t s = fetch(i);
			c->cyc += i->clk + 1;
			r = alu8(c, a, AL, s);
			if (a != 7)
				AL = (uint8_t)r;
			break;
		}
		default: {
			uint32_t s = fetchw(i);
			c->cyc += i->clk + 1;
			r = alu16(c, a, R16(AX), s);
			if (a != 7)
				R16(AX) = (uint16_t)r;
			break;
		}
		}
	} else switch (op) {
	case 0x06: case 0x0E: case 0x16: case 0x1E:
		c->cyc += i->clk + 2;
		push(c, c->sr[op >> 3]);
		break;
	case 0x07: case 0x17: case 0x1F:
		c->cyc += i->clk + 3;
		c->sr[op >> 3] = pop(c);
		if (op == 0x17)
			c->no_irq = 1;
		break;
	case 0x0F: c->cyc += i->clk + 1; break;
	case 0x27: c->cyc += i->clk + 10; adj4(c, 6, 0x60); break;
	case 0x2F: c->cyc += i->clk + 10; adj4(c, -6, -0x60); break;
	case 0x37: c->cyc += i->clk + 9; adjb(c, 6, AL > 0xF9 ? 2 : 1); break;
	case 0x3F: c->cyc += i->clk + 9; adjb(c, -6, AL < 6 ? -2 : -1); break;
	case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
		c->cyc += i->clk + 1;
		R16(op & 7) = (uint16_t)inc16(c, R16(op & 7));
		break;
	case 0x48: case 0x49: case 0x4A: case 0x4B: case 0x4C: case 0x4D: case 0x4E: case 0x4F:
		c->cyc += i->clk + 1;
		R16(op & 7) = (uint16_t)dec16(c, R16(op & 7));
		break;
	case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
		c->cyc += i->clk + 1;
		push(c, R16(op & 7));
		break;
	case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C: case 0x5D: case 0x5E: case 0x5F:
		c->cyc += i->clk + 1;
		R16(op & 7) = pop(c);
		break;
	case 0x60: {
		c->cyc += i->clk + 9;
		uint16_t t = R16(SP);
		push(c, R16(AX)); push(c, R16(CX)); push(c, R16(DX)); push(c, R16(BX));
		push(c, t); push(c, R16(BP)); push(c, R16(SI)); push(c, R16(DI));
		break;
	}
	case 0x61:
		c->cyc += i->clk + 8;
		R16(DI) = pop(c); R16(SI) = pop(c); R16(BP) = pop(c); pop(c);
		R16(BX) = pop(c); R16(DX) = pop(c); R16(CX) = pop(c); R16(AX) = pop(c);
		break;
	case 0x62: {
		c->cyc += i->clk;
		uint16_t lo = rw(c, i->es, i->eo), hi = rw(c, i->es, (uint16_t)(i->eo + 2)), t = R16(rf);
		if (t < lo || t > hi) {
			enter_vector(c, 5, i->pos);
			c->cyc += 20;
			ip = c->ip;
			xfer = 1;
		} else
			c->cyc += 13;
		break;
	}
	case 0x68: { uint16_t v = fetchw(i); c->cyc += i->clk + 1; push(c, v); break; }
	case 0x6A: { uint16_t v = (uint16_t)(int16_t)(int8_t)fetch(i); c->cyc += i->clk + 1; push(c, v); break; }
	case 0x69: {
		uint32_t d = rm16(i);
		uint16_t v = fetchw(i);
		clkm(i, 3, 4);
		R16(rf) = (uint16_t)imul3(c, d, v);
		break;
	}
	case 0x6B: {
		uint32_t d = rm16(i);
		uint16_t v = (uint16_t)(int16_t)(int8_t)fetch(i);
		clkm(i, 3, 4);
		R16(rf) = (uint16_t)imul3(c, d, v);
		break;
	}
	case 0x6C: case 0x6D: case 0x6E: case 0x6F:
	case 0xA4: case 0xA5: case 0xA6: case 0xA7: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xAE: case 0xAF:
		c->cyc += i->clk;
		string_op(c, op, rep, dseg);
		break;
	case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
	case 0x78: case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: {
		int8_t d = (int8_t)fetch(i);
		c->cyc += i->clk + 1;
		if (cond(c, op & 15)) {
			c->cyc += 9;
			JUMP(i->pos + d);
		}
		break;
	}
	case 0x80: case 0x81: case 0x82: case 0x83: {
		const int w = op & 1;
		uint32_t d = w ? rm16(i) : rm8(i);
		uint32_t s = op == 0x81 ? fetchw(i) : op == 0x83 ? (uint16_t)(int16_t)(int8_t)fetch(i) : fetch(i);
		c->cyc += i->clk + (i->isreg ? 1 : rf == 7 ? 2 : 3);
		if (w) {
			uint32_t r = alu16(c, rf, d, s);
			if (rf != 7)
				wrm16(i, r);
		} else {
			uint32_t r = alu8(c, rf, d, s);
			if (rf != 7)
				wrm8(i, r);
		}
		break;
	}
	case 0x84: { clkm(i, 1, 2); uint32_t s = r8(c, rf); logic8(c, rm8(i) & s); break; }
	case 0x85: { clkm(i, 1, 2); uint32_t s = R16(rf); logic16(c, rm16(i) & s); break; }
	case 0x86: { clkm(i, 3, 5); uint32_t s = r8(c, rf), d = rm8(i); s8(c, rf, (uint8_t)d); wrm8(i, s); break; }
	case 0x87: { clkm(i, 3, 5); uint32_t s = R16(rf), d = rm16(i); R16(rf) = (uint16_t)d; wrm16(i, s); break; }
	case 0x88: c->cyc += i->clk + 1; wrm8(i, r8(c, rf)); break;
	case 0x89: c->cyc += i->clk + 1; wrm16(i, R16(rf)); break;
	case 0x8A: c->cyc += i->clk + 1; s8(c, rf, (uint8_t)rm8(i)); break;
	case 0x8B: c->cyc += i->clk + 1; R16(rf) = (uint16_t)rm16(i); break;
	case 0x8C: clkm(i, 1, 3); wrm16(i, c->sr[rf & 3]); break;
	case 0x8D: c->cyc += i->clk + 1; R16(rf) = i->eo; break;
	case 0x8E: {
		uint32_t v = rm16(i);
		clkm(i, 2, 3);
		if (rf < 4)
			c->sr[rf] = (uint16_t)v;
		c->no_irq = 1;
		break;
	}
	case 0x8F: { clkm(i, 1, 3); uint16_t v = pop(c); wrm16(i, v); break; }
	case 0x90: c->cyc += i->clk + 1; break;
	case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
		c->cyc += i->clk + 3;
		uint16_t t = R16(op & 7);
		R16(op & 7) = R16(AX);
		R16(AX) = t;
		break;
	}
	case 0x98: c->cyc += i->clk + 1; AH = (AL & 0x80) ? 0xFF : 0; break;
	case 0x99: c->cyc += i->clk + 1; R16(DX) = (AH & 0x80) ? 0xFFFF : 0; break;
	case 0x9A: {
		uint16_t o = fetchw(i), s = fetchw(i);
		c->cyc += i->clk + 10;
		push(c, c->sr[CS]);
		push(c, i->pos);
		c->sr[CS] = s;
		JUMP(o);
		break;
	}
	case 0x9B: c->cyc += i->clk; break;
	case 0x9C: c->cyc += i->clk + 2; push(c, v30mz_flags(c)); break;
	case 0x9D: c->cyc += i->clk + 3; set_flags(c, pop(c)); break;
	case 0x9E: c->cyc += i->clk + 4; set_flags(c, (uint16_t)((v30mz_flags(c) & 0xFF00) | (AH & 0xD5))); break;
	case 0x9F: c->cyc += i->clk + 2; AH = (uint8_t)v30mz_flags(c); break;
	case 0xA0: { uint16_t a = fetchw(i); c->cyc += i->clk + 1; AL = rb(c, dseg, a); break; }
	case 0xA1: { uint16_t a = fetchw(i); c->cyc += i->clk + 1; R16(AX) = rw(c, dseg, a); break; }
	case 0xA2: { uint16_t a = fetchw(i); c->cyc += i->clk + 1; wb(c, dseg, a, AL); break; }
	case 0xA3: { uint16_t a = fetchw(i); c->cyc += i->clk + 1; ww(c, dseg, a, R16(AX)); break; }
	case 0xA8: { uint32_t s = fetch(i); c->cyc += i->clk + 1; logic8(c, AL & s); break; }
	case 0xA9: { uint32_t s = fetchw(i); c->cyc += i->clk + 1; logic16(c, R16(AX) & s); break; }
	case 0xB0: case 0xB1: case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7: {
		uint8_t v = fetch(i);
		c->cyc += i->clk + 1;
		s8(c, op & 7, v);
		break;
	}
	case 0xB8: case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD: case 0xBE: case 0xBF: {
		uint16_t v = fetchw(i);
		c->cyc += i->clk + 1;
		R16(op & 7) = v;
		break;
	}
	case 0xC0: case 0xC1: case 0xD0: case 0xD1: case 0xD2: case 0xD3: {
		const int w = op & 1, one = op == 0xD0 || op == 0xD1;
		uint32_t v = w ? rm16(i) : rm8(i), r;
		uint32_t cnt = op <= 0xC1 ? fetch(i) : op >= 0xD2 ? CL : 1;
		if (one)
			clkm(i, 1, 3);
		else
			clkm(i, 3, 5);
		if (shift(c, rf, v, cnt, one, w, &r)) {
			if (w)
				wrm16(i, r);
			else
				wrm8(i, r);
		}
		break;
	}
	case 0xC2: case 0xC3: {
		uint16_t n = op == 0xC2 ? fetchw(i) : 0;
		c->cyc += i->clk + 6;
		uint16_t t = pop(c);
		R16(SP) += n;
		JUMP(t);
		break;
	}
	case 0xC4: case 0xC5: {
		c->cyc += i->clk + 6;
		uint16_t v = rw(c, i->es, i->eo), s = rw(c, i->es, (uint16_t)(i->eo + 2));
		R16(rf) = v;
		c->sr[op == 0xC4 ? ES : DS] = s;
		break;
	}
	case 0xC6: { uint8_t v = fetch(i); c->cyc += i->clk + 1; wrm8(i, v); break; }
	case 0xC7: { uint16_t v = fetchw(i); c->cyc += i->clk + 1; wrm16(i, v); break; }
	case 0xC8: {
		uint16_t n = fetchw(i);
		uint8_t lv = fetch(i);
		uint16_t ssb = i->pre ? i->pbase : c->sr[SS];
		c->cyc += i->clk + 8;
		push(c, R16(BP));
		R16(BP) = R16(SP);
		R16(SP) -= n;
		for (int k = 1; k < lv; k++) {
			push(c, rw(c, ssb, (uint16_t)(R16(BP) - k * 2)));
			c->cyc += 4;
		}
		if (lv) {
			push(c, R16(BP));
			c->cyc += lv == 1 ? 2 : 3;
		}
		break;
	}
	case 0xC9: c->cyc += i->clk + 2; R16(SP) = R16(BP); R16(BP) = pop(c); break;
	case 0xCA: case 0xCB: {
		uint16_t n = op == 0xCA ? fetchw(i) : 0;
		c->cyc += i->clk + (op == 0xCA ? 9 : 8);
		uint16_t t = pop(c), s = pop(c);
		R16(SP) += n;
		c->sr[CS] = s;
		JUMP(t);
		break;
	}
	case 0xCC: c->cyc += i->clk + 9; enter_vector(c, 3, i->pos); ip = c->ip; xfer = 1; break;
	case 0xCD: {
		uint8_t n = fetch(i);
		c->cyc += i->clk + 10;
		enter_vector(c, n, i->pos);
		ip = c->ip;
		xfer = 1;
		break;
	}
	case 0xCE:
		c->cyc += i->clk + 6;
		if (OF) {
			enter_vector(c, 4, i->pos);
			c->cyc += 7;
			ip = c->ip;
			xfer = 1;
		}
		break;
	case 0xCF: {
		c->cyc += i->clk + 10;
		uint16_t t = pop(c), s = pop(c);
		c->cyc += 3;
		set_flags(c, pop(c));
		c->sr[CS] = s;
		JUMP(t);
		break;
	}
	case 0xD4: fetch(i); c->cyc += i->clk + 17; AH = AL / 10; AL %= 10; szp16(c, R16(AX)); break;
	case 0xD5: fetch(i); c->cyc += i->clk + 5; AL = (uint8_t)(AH * 10 + AL); AH = 0; szp8(c, AL); break;
	case 0xD6: c->cyc += i->clk + 3; AL = CF ? 0xFF : 0; break;
	case 0xD7: c->cyc += i->clk + 5; AL = rb(c, dseg, (uint16_t)(R16(BX) + AL)); break;
	case 0xD8: c->cyc += i->clk + 1; break;
	case 0xE0: case 0xE1: case 0xE2: {
		int8_t d = (int8_t)fetch(i);
		c->cyc += i->clk + (op == 0xE2 ? 2 : 3);
		R16(CX)--;
		if (R16(CX) && (op == 0xE2 || (op == 0xE1 ? ZF : !ZF))) {
			c->cyc += 3;
			JUMP(i->pos + d);
		}
		break;
	}
	case 0xE3: {
		int8_t d = (int8_t)fetch(i);
		c->cyc += i->clk + 1;
		if (R16(CX) == 0) {
			c->cyc += 3;
			JUMP(i->pos + d);
		}
		break;
	}
	case 0xE4: { uint8_t p = fetch(i); c->cyc += i->clk + 6; AL = (uint8_t)port_in(c, p, 0); break; }
	case 0xE5: { uint8_t p = fetch(i); c->cyc += i->clk + 6; R16(AX) = port_in(c, p, 1); break; }
	case 0xE6: { uint8_t p = fetch(i); c->cyc += i->clk + 6; port_out(c, p, AL, 0); break; }
	case 0xE7: { uint8_t p = fetch(i); c->cyc += i->clk + 6; port_out(c, p, R16(AX), 1); break; }
	case 0xEC: c->cyc += i->clk + 6; AL = (uint8_t)port_in(c, R16(DX), 0); break;
	case 0xED: c->cyc += i->clk + 6; R16(AX) = port_in(c, R16(DX), 1); break;
	case 0xEE: c->cyc += i->clk + 6; port_out(c, R16(DX), AL, 0); break;
	case 0xEF: c->cyc += i->clk + 6; port_out(c, R16(DX), R16(AX), 1); break;
	case 0xE8: {
		int16_t d = (int16_t)fetchw(i);
		c->cyc += i->clk + 5;
		push(c, i->pos);
		JUMP(i->pos + d);
		break;
	}
	case 0xE9: { int16_t d = (int16_t)fetchw(i); c->cyc += i->clk + 4; JUMP(i->pos + d); break; }
	case 0xEA: {
		uint16_t o = fetchw(i), s = fetchw(i);
		c->cyc += i->clk + 7;
		c->sr[CS] = s;
		JUMP(o);
		break;
	}
	case 0xEB: {
		int8_t d = (int8_t)fetch(i);
		c->cyc += i->clk + 4;
		if (d == -2)
			c->idle(c->bus, 1);
		JUMP(i->pos + d);
		break;
	}
	case 0xF0: c->cyc += i->clk + 1; c->no_irq = 1; break;
	case 0xF4: c->cyc += i->clk; c->idle(c->bus, 0); break;
	case 0xF5: c->cyc += i->clk + 4; c->CarryVal = CF ? 0 : 1; break;
	case 0xF6: case 0xF7: {
		const int w = op & 1;
		uint32_t v = w ? rm16(i) : rm8(i);
		switch (rf) {
		case 0: {
			uint32_t s = w ? fetchw(i) : fetch(i);
			clkm(i, 1, 2);
			c->CarryVal = c->OverVal = 0;   /* TEST: AF untouched */
			if (w)
				szp16(c, v & s);
			else
				szp8(c, v & s);
			break;
		}
		case 1: c->cyc += i->clk; break;
		case 2:
			clkm(i, 1, 3);
			if (w)
				wrm16(i, ~v);
			else
				wrm8(i, ~v);
			break;
		case 3: {
			clkm(i, 1, 3);
			c->CarryVal = v != 0;
			uint32_t r = (~v) + 1;
			if (w) {
				szp16(c, r);
				wrm16(i, r);
			} else {
				szp8(c, r);
				wrm8(i, r);
			}
			break;
		}
		case 4: clkm(i, 3, 4); mul(c, v, w, 0); break;
		case 5: clkm(i, 3, 4); mul(c, v, w, 1); break;
		default:
			if (rf == 6) {
				if (w) clkm(i, 23, 24); else clkm(i, 15, 16);
			} else {
				if (w) clkm(i, 24, 25); else clkm(i, 17, 18);
			}
			if (!divide(c, v, w, rf == 7)) {
				enter_vector(c, 0, i->pos);
				ip = c->ip;
				xfer = 1;
			}
			break;
		}
		break;
	}
	case 0xF8: c->cyc += i->clk + 4; c->CarryVal = 0; break;
	case 0xF9: c->cyc += i->clk + 4; c->CarryVal = 1; break;
	case 0xFA: c->cyc += i->clk + 4; c->IF = 0; break;
	case 0xFB: c->cyc += i->clk + 4; c->IF = 1; break;
	case 0xFC: c->cyc += i->clk + 4; c->DF = 0; break;
	case 0xFD: c->cyc += i->clk + 4; c->DF = 1; break;
	case 0xFE: {
		uint32_t v = rm8(i);
		if (rf == 0) {
			clkm(i, 1, 3);
			wrm8(i, inc8(c, v));
		} else if (rf == 1) {
			clkm(i, 1, 3);
			wrm8(i, dec8(c, v));
		} else
			c->cyc += i->clk;
		break;
	}
	case 0xFF: {
		uint32_t v = rm16(i);
		switch (rf) {
		case 0: clkm(i, 1, 3); wrm16(i, inc16(c, v)); break;
		case 1: clkm(i, 1, 3); wrm16(i, dec16(c, v)); break;
		case 2: clkm(i, 5, 6); push(c, i->pos); JUMP(v); break;
		case 3: {
			c->cyc += i->clk + 12;
			uint16_t s = rw(c, i->es, (uint16_t)(i->eo + 2));
			push(c, c->sr[CS]);
			push(c, i->pos);
			c->sr[CS] = s;
			JUMP(v);
			break;
		}
		case 4: clkm(i, 4, 5); JUMP(v); break;
		case 5: {
			c->cyc += i->clk + 10;
			c->sr[CS] = rw(c, i->es, (uint16_t)(i->eo + 2));
			JUMP(v);
			break;
		}
		case 6: c->cyc += i->clk + 1; push(c, (uint16_t)v); break;
		default: c->cyc += i->clk; break;
		}
		break;
	}
	default:                     /* 63 64 65 66 67 D9-DF F1: undefined, 10 cycles */
		c->cyc += i->clk + 10;
		break;
	}
	c->ip = xfer ? ip : i->pos;
#undef JUMP
}

void v30mz_reset(v30mz *c)
{
	if (!s_parity[0])
		for (int v = 0; v < 256; v++)
			s_parity[v] = (uint8_t)!(__builtin_popcount(v) & 1);
	memset(&c->r, 0, sizeof c->r);
	c->sr[ES] = 0;
	c->sr[CS] = 0xFFFF;
	c->sr[SS] = 0;
	c->sr[DS] = 0;
	c->ip = 0;
	c->CarryVal = c->OverVal = c->AuxVal = 0;
	c->SignVal = 0;
	c->ZeroVal = c->ParityVal = 1;
	c->TF = c->IF = c->DF = 0;
	c->MF = 1;
	c->no_irq = 0;
	c->pre_on = 0;
	c->cyc = 0;
}
