/* bpc-emu - the ASWAN's sound: four channels playing 32-sample 4-bit waveforms from the
 * internal RAM, channel 2's voice (8-bit samples written to its volume port), channel 3's
 * sweep, channel 4's noise. A sample every 128 CPU cycles (24 kHz), run up to the present
 * when a sound port is read or written and at every frame, so that what a game reads back
 * (the noise register, the output levels) is exact to the cycle.
 *
 * The output: the headphones' stereo, or the speaker's mono mix (the Pocket Challenge V2
 * has a speaker; whether it has a headphone jack is not known). */
#include <string.h>

#include "aswan.h"

static uint8_t fetch_sample(const aswan *m, int ch, int off)
{
	uint32_t a = (uint32_t)(m->s.sample_addr + ((ch & 3) << 4) + ((off >> 1) & 0x0E)) & 0x3FFE;
	uint16_t w = (uint16_t)(m->ram[a] | m->ram[a + 1] << 8);
	return (uint8_t)((w >> ((off & 3) * 4)) & 0x0F);
}

static int16_t clip(int32_t v)
{
	return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

static void one_sample(aswan *m)
{
	aswan_audio *s = &m->s;
	int32_t L = 0, R = 0;
	s->lout = s->rout = 0;
	if (s->hp_conn ? s->hp_on : s->spk_on) {
		for (int n = 0; n < 4; n++) {
			__typeof__(s->ch[0]) *h = &s->ch[n];
			if (!h->on)
				continue;
			if (n == 1 && s->voice) {
				uint8_t vd = (uint8_t)(h->vol_l << 4 | h->vol_r);
				if (s->sample_vol & 0xC)
					s->lout += (s->sample_vol & 8) ? vd : (s->sample_vol & 4) ? vd >> 1 : 0;
				if (s->sample_vol & 0x3)
					s->rout += (s->sample_vol & 2) ? vd : (s->sample_vol & 1) ? vd >> 1 : 0;
				continue;
			}
			const uint8_t smp = h->signal;
			h->pos += 128;
			while (h->pos >= h->period) {
				h->pos -= h->period;
				if (n < 3) {
					h->signal = fetch_sample(m, n, h->offset++);
					continue;
				}
				h->signal = s->noise ? (s->noise_out ? 0xF : 0) : fetch_sample(m, 3, h->offset++);
				if (s->noise_on) {
					static const int tap[8] = { 14, 10, 13, 4, 8, 6, 9, 11 };
					s->noise_out = (uint8_t)((1 ^ (s->noise_shift >> 7) ^
						(s->noise_shift >> tap[s->noise_type])) & 1);
					s->noise_shift = (uint16_t)(s->noise_shift << 1 | s->noise_out);
				}
			}
			if (n == 2 && s->sweep && s->sweep_time) {
				s->sweep_count += 128;
				while (s->sweep_count >= s->sweep_time) {
					s->sweep_count -= s->sweep_time;
					h->freq = (uint16_t)((h->freq + s->sweep_step) & 0x7FF);
					h->period = (uint16_t)(2048 - h->freq);
				}
			}
			s->lout += h->vol_l * smp;
			s->rout += h->vol_r * smp;
		}
		if (s->hp_conn) {
			L = s->lout << 5;
			R = s->rout << 5;
		} else {
			int mix = (((s->lout & 0x3FF) + (s->rout & 0x3FF)) >> s->spk_vol) & 0xFF;
			L = R = mix << 7;
		}
	}
	if (s->out_n + 2 > (int)(sizeof s->out / sizeof s->out[0]))
		return;                  /* nobody takes it: dropped */
	s->out[s->out_n++] = clip(L);
	s->out[s->out_n++] = clip(R);
}

void audio_sync(aswan *m)
{
	const uint64_t to = m->cpu.cyc / 128;
	while (m->s.done < to) {
		one_sample(m);
		m->s.done++;
	}
}

void audio_reset(aswan *m, int headphones)
{
	aswan_audio *s = &m->s;
	memset(s, 0, sizeof *s);
	for (int n = 0; n < 4; n++)
		s->ch[n].period = 2048;
	s->sweep_time = 8192;
	s->hp_conn = (uint8_t)(headphones != 0);
}

uint16_t audio_read(aswan *m, int off)
{
	audio_sync(m);
	const aswan_audio *s = &m->s;
	switch (off) {
	case 0x40: case 0x41: case 0x42: case 0x43: return s->ch[off & 3].freq;
	case 0x44:
		return (uint16_t)(s->ch[0].vol_l << 4 | s->ch[0].vol_r | s->ch[1].vol_l << 12 | s->ch[1].vol_r << 8);
	case 0x45:
		return (uint16_t)(s->ch[2].vol_l << 4 | s->ch[2].vol_r | s->ch[3].vol_l << 12 | s->ch[3].vol_r << 8);
	case 0x46: return (uint16_t)((uint8_t)s->sweep_step | ((s->sweep_time / 8192) - 1) << 8);
	case 0x47: return (uint16_t)(s->noise_type | (s->noise_on ? 0x10 : 0) | ((s->sample_addr << 2) & 0xFF00));
	case 0x48:
		return (uint16_t)((s->ch[0].on ? 1 : 0) | (s->ch[1].on ? 2 : 0) | (s->ch[2].on ? 4 : 0) |
			(s->ch[3].on ? 8 : 0) | (s->voice ? 0x20 : 0) | (s->sweep ? 0x40 : 0) | (s->noise ? 0x80 : 0) |
			(s->spk_on ? 0x100 : 0) | s->spk_vol << 9 | (s->hp_on ? 0x800 : 0) | (s->hp_conn ? 0x8000 : 0));
	case 0x49: return s->noise_shift;
	case 0x4A: return s->sample_vol;
	case 0x4B: return s->lout & 0x3FF;
	case 0x4C: return s->rout & 0x3FF;
	case 0x4D: return (uint16_t)((s->lout & 0x3FF) + (s->rout & 0x3FF));
	case 0x4F: return s->sys_vol;
	}
	return 0;
}

void audio_write(aswan *m, int off, uint16_t data, uint16_t mask)
{
	audio_sync(m);
	aswan_audio *s = &m->s;
	const int lo = mask & 0x00FF, hi = mask & 0xFF00;
	switch (off) {
	case 0x40: case 0x41: case 0x42: case 0x43: {
		__typeof__(s->ch[0]) *h = &s->ch[off & 3];
		h->freq = (uint16_t)(((h->freq & ~mask) | (data & mask)) & 0x7FF);
		h->period = (uint16_t)(2048 - h->freq);
		break;
	}
	case 0x44:
		if (lo) { s->ch[0].vol_l = (data >> 4) & 0xF; s->ch[0].vol_r = data & 0xF; }
		if (hi) { s->ch[1].vol_l = (data >> 12) & 0xF; s->ch[1].vol_r = (data >> 8) & 0xF; }
		break;
	case 0x45:
		if (lo) { s->ch[2].vol_l = (data >> 4) & 0xF; s->ch[2].vol_r = data & 0xF; }
		if (hi) { s->ch[3].vol_l = (data >> 12) & 0xF; s->ch[3].vol_r = (data >> 8) & 0xF; }
		break;
	case 0x46:
		if (lo)
			s->sweep_step = (int8_t)(data & 0xFF);
		if (hi)
			s->sweep_time = 8192u * (((data >> 8) & 0x1F) + 1);
		break;
	case 0x47:
		if (lo) {
			s->noise_type = data & 7;
			s->noise_on = data >> 4 & 1;
			if (data & 8)
				s->noise_shift = s->noise_out = 0;
		}
		if (hi)
			s->sample_addr = (uint16_t)((data & 0xFF00) >> 2);
		break;
	case 0x48:
		if (lo) {
			for (int n = 0; n < 4; n++)
				s->ch[n].on = data >> n & 1;
			s->voice = data >> 5 & 1;
			s->sweep = data >> 6 & 1;
			s->noise = data >> 7 & 1;
		}
		if (hi) {
			s->spk_on = data >> 8 & 1;
			s->spk_vol = (data >> 9) & 3;
			s->hp_on = data >> 11 & 1;
		}
		break;
	case 0x49: s->noise_shift = (uint16_t)(((s->noise_shift & ~mask) | (data & mask)) & 0x7FFF); break;
	case 0x4A: if (lo) s->sample_vol = data & 0x0F; break;
	case 0x4F: if (lo) s->sys_vol = data & 3; break;
	}
}
