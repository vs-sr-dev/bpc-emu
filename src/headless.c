/* bpc-emu - the headless runner: a cartridge run for some frames, with the buttons from a
 * script, pictures at chosen frames and the machine's state at every frame (the same lines
 * tools/oracle writes from wskit's interpreter, so that the two can be compared)
 *
 *   bpc-cli ROM.pc2 [--frames N] [--snap F1,F2,..] [--out DIR] [--keys FILE] [--log FILE]
 *                   [--ws-boot] [--headphones] [--wav FILE]
 *
 * --keys FILE: lines "FRAME BUTTONS", BUTTONS a hex key matrix word or names joined by '+'
 * (up down left right circle clear pass view esc, or none), held from that frame on. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pcv2.h"
#include "png.h"

static uint8_t *read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *d = n > 0 ? malloc((size_t)n) : NULL;
	if (d && fread(d, 1, (size_t)n, f) != (size_t)n) {
		free(d);
		d = NULL;
	}
	fclose(f);
	*size = (size_t)n;
	return d;
}

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
	const uint8_t *b = p;
	for (size_t i = 0; i < n; i++) {
		h ^= b[i];
		h *= 1099511628211ull;
	}
	return h;
}

/* ---- the button script ---- */
typedef struct { long frame; uint16_t keys; } key_ev;
static key_ev *s_ev;
static int s_nev;

static int parse_buttons(const char *s, uint16_t *out)
{
	static const struct { const char *name; uint16_t bit; } names[] = {
		{ "up", PCV2_UP }, { "down", PCV2_DOWN }, { "left", PCV2_LEFT }, { "right", PCV2_RIGHT },
		{ "circle", PCV2_CIRCLE }, { "clear", PCV2_CLEAR }, { "pass", PCV2_PASS },
		{ "view", PCV2_VIEW }, { "esc", PCV2_ESC }, { "none", 0 },
	};
	char *end;
	unsigned long v = strtoul(s, &end, 16);
	if (*end == 0 && end != s) {
		*out = (uint16_t)v;
		return 1;
	}
	uint16_t k = 0;
	char buf[128];
	snprintf(buf, sizeof buf, "%s", s);
	for (char *t = strtok(buf, "+"); t; t = strtok(NULL, "+")) {
		size_t i;
		for (i = 0; i < sizeof names / sizeof names[0]; i++)
			if (!strcmp(t, names[i].name))
				break;
		if (i == sizeof names / sizeof names[0])
			return 0;
		k |= names[i].bit;
	}
	*out = k;
	return 1;
}

static int load_keys(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return 0;
	char line[256];
	while (fgets(line, sizeof line, f)) {
		long fr;
		char b[128];
		if (line[0] == '#' || sscanf(line, "%ld %127s", &fr, b) != 2)
			continue;
		uint16_t k;
		if (!parse_buttons(b, &k)) {
			fprintf(stderr, "%s: unknown buttons '%s'\n", path, b);
			fclose(f);
			return 0;
		}
		s_ev = realloc(s_ev, sizeof *s_ev * (s_nev + 1));
		s_ev[s_nev].frame = fr;
		s_ev[s_nev].keys = k;
		s_nev++;
	}
	fclose(f);
	return 1;
}

static uint16_t keys_at(long frame)
{
	uint16_t k = 0;
	for (int i = 0; i < s_nev && s_ev[i].frame <= frame; i++)
		k = s_ev[i].keys;
	return k;
}

/* ---- output ---- */
static void snap(const aswan *m, const char *dir, long frame)
{
	static uint32_t px[SCREEN_W * SCREEN_H];
	for (int y = 0; y < SCREEN_H; y++)
		for (int x = 0; x < SCREEN_W; x++) {
			uint32_t g = m->fb[y][x] * 17u;
			px[y * SCREEN_W + x] = g << 16 | g << 8 | g;
		}
	char path[1024];
	snprintf(path, sizeof path, "%s/f%05ld.png", dir, frame);
	png_write(path, px, SCREEN_W, SCREEN_W, SCREEN_H);
}

static void log_state(FILE *f, const pcv2 *p)
{
	const aswan *m = &p->m;
	const v30mz *c = &m->cpu;
	uint64_t h0 = 1469598103934665603ull;
	fprintf(f, "%llu %llu", (unsigned long long)m->frame, (unsigned long long)c->cyc);
	for (int i = 0; i < 8; i++)
		fprintf(f, " %04X", c->r.w[i]);
	fprintf(f, " %04X %04X %04X %04X %04X %04X", c->sr[ES], c->sr[CS], c->sr[SS], c->sr[DS], c->ip,
		v30mz_flags(c));
	fprintf(f, " %016llx %016llx %016llx\n", (unsigned long long)fnv(h0, m->ram, 0x4000),
		(unsigned long long)fnv(h0, p->sram, p->foot.sram_size),
		(unsigned long long)fnv(h0, m->fb, sizeof m->fb));
}

static void write_wav(const char *path, const int16_t *smp, size_t frames, int rate)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return;
	uint32_t data = (uint32_t)frames * 4, riff = 36 + data, br = (uint32_t)rate * 4, fmt = 16;
	uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
	fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
	fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
	fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f);
	fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
	fwrite(smp, 4, frames, f);
	fclose(f);
}

int main(int argc, char **argv)
{
	const char *rom_path = NULL, *out = ".", *keys = NULL, *log_path = NULL, *wav = NULL;
	long frames = 600, snaps[256];
	int nsnap = 0, ws_boot = 0, headphones = 0;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *v = i + 1 < argc ? argv[i + 1] : "";
		if (!strcmp(a, "--frames")) { frames = atol(v); i++; }
		else if (!strcmp(a, "--out")) { out = v; i++; }
		else if (!strcmp(a, "--keys")) { keys = v; i++; }
		else if (!strcmp(a, "--log")) { log_path = v; i++; }
		else if (!strcmp(a, "--wav")) { wav = v; i++; }
		else if (!strcmp(a, "--ws-boot")) ws_boot = 1;
		else if (!strcmp(a, "--headphones")) headphones = 1;
		else if (!strcmp(a, "--snap")) {
			for (const char *s = v; *s && nsnap < 256;) {
				snaps[nsnap++] = atol(s);
				while (*s && *s != ',')
					s++;
				if (*s)
					s++;
			}
			i++;
		} else if (a[0] != '-')
			rom_path = a;
		else {
			fprintf(stderr, "unknown option %s\n", a);
			return 2;
		}
	}
	if (!rom_path) {
		fprintf(stderr, "bpc-cli ROM.pc2 [--frames N] [--snap F1,F2,..] [--out DIR] [--keys FILE] "
			"[--log FILE] [--ws-boot] [--headphones] [--wav FILE]\n");
		return 2;
	}
	if (keys && !load_keys(keys)) {
		fprintf(stderr, "cannot read %s\n", keys);
		return 1;
	}
	size_t size;
	uint8_t *data = read_file(rom_path, &size);
	if (!data) {
		fprintf(stderr, "cannot read %s\n", rom_path);
		return 1;
	}
	static pcv2 p;
	char err[256];
	p.ws_boot = ws_boot;
	p.m.headphones = (uint8_t)headphones;
	if (!pcv2_load(&p, data, size, err, sizeof err)) {
		fprintf(stderr, "%s: %s\n", rom_path, err);
		return 1;
	}
	free(data);
	FILE *lf = log_path ? fopen(log_path, "w") : NULL;
	int16_t *audio = NULL;
	size_t audio_n = 0, audio_cap = 0;
	while ((long)p.m.frame < frames) {
		aswan_run_frame(&p.m);
		long f = (long)p.m.frame;
		if (lf)
			log_state(lf, &p);
		for (int k = 0; k < nsnap; k++)
			if (snaps[k] == f)
				snap(&p.m, out, f);
		p.m.keys = keys_at(f);
		int16_t tmp[2 * 1024];
		int n;
		while ((n = aswan_audio_take(&p.m, tmp, 1024)) > 0) {
			if (!wav)
				continue;
			if (audio_n + (size_t)n > audio_cap) {
				audio_cap = (audio_cap + (size_t)n) * 2;
				audio = realloc(audio, audio_cap * 4);
			}
			memcpy(audio + audio_n * 2, tmp, (size_t)n * 4);
			audio_n += (size_t)n;
		}
	}
	if (lf)
		fclose(lf);
	if (wav)
		write_wav(wav, audio, audio_n, AUDIO_HZ);
	printf("frames %llu cycles %llu  CS:IP %04X:%04X\n", (unsigned long long)p.m.frame,
		(unsigned long long)p.m.cpu.cyc, p.m.cpu.sr[CS], p.m.cpu.ip);
	return 0;
}
