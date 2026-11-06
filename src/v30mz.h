/* bpc-emu - the NEC V30MZ: an 80186 in real mode, as the WonderSwan's ASWAN runs it
 *
 * The flags are kept as MAME's v30mz core keeps them -- values whose being non-zero (or
 * negative, for the sign) is the flag -- and the cycle counts are MAME's, read from wskit
 * (same author), so that a frame here is a frame there: wskit's interpreter is the oracle
 * (tools/oracle). */
#ifndef BPC_V30MZ_H
#define BPC_V30MZ_H

#include <stdint.h>

/* registers, in encoding order: AW CW DW BW SP BP IX IY = ax cx dx bx sp bp si di */
enum { AX, CX, DX, BX, SP, BP, SI, DI };
enum { ES, CS, SS, DS };

typedef struct v30mz
{
	union { uint16_t w[8]; uint8_t b[16]; } r;
	uint16_t sr[4];
	uint16_t ip;
	uint32_t CarryVal, OverVal, AuxVal, ZeroVal, ParityVal;
	int32_t SignVal;
	uint8_t TF, IF, DF, MF;
	uint8_t no_irq;              /* after POP SS, MOV sreg, LOCK: none at the next boundary */
	uint8_t pre_on;              /* a segment prefix left by a REP that had no string op */
	uint16_t pre_base;
	uint64_t cyc;                /* CPU cycles (3.072 MHz) */

	/* the 1 MB in 4 KB pages; a write page may be a sink */
	const uint8_t *rd[256];
	uint8_t *wr[256];

	/* the ports (word: a 16-bit access at an even port) and idling */
	void *bus;
	uint16_t (*in)(void *bus, uint16_t port, int word);
	void (*out)(void *bus, uint16_t port, uint16_t v, int word);
	/* HLT (0), or JMP $ (1): the machine moves time on */
	void (*idle)(void *bus, int jmp_self);
} v30mz;

void v30mz_reset(v30mz *c);
/* one instruction (with its prefixes) */
void v30mz_step(v30mz *c);
/* a hardware interrupt through vector n, as the boundary before CS:IP takes it */
void v30mz_irq(v30mz *c, int n);
uint16_t v30mz_flags(const v30mz *c);

static inline uint32_t v30mz_lin(uint16_t seg, uint16_t off)
{
	return (((uint32_t)seg << 4) + off) & 0xFFFFF;
}

#endif
