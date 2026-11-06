/* bpc-emu - the window: the picture, the sound, the buttons from the keyboard or a game
 * controller, the cartridge's SRAM kept in a file
 *
 *   bpc-emu [ROM.pc2] [--scale N]     or drop a cartridge on the window
 *
 * Keys: arrows = the pad; Z or Enter = Circle; X or Backspace = Clear; C = Pass; V = View;
 * Esc = Esc; F2 reset, P pause, F11 full screen, F12 a screenshot (PNG). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "pcv2.h"
#include "png.h"

static pcv2 s_p;
static int s_loaded;
static char s_rom_path[1024], s_save_path[1100];
static SDL_Window *s_win;
static SDL_Renderer *s_ren;
static SDL_Texture *s_tex;
static SDL_AudioDeviceID s_adev;
static SDL_GameController *s_pad;
static uint32_t s_px[SCREEN_W * SCREEN_H];
static uint32_t s_lcd[16];       /* the LCD's sixteen levels as colours */
static uint32_t s_sram_sum;

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

static uint32_t sram_sum(void)
{
	uint32_t h = 2166136261u;
	for (uint32_t i = 0; i < s_p.foot.sram_size; i++)
		h = (h ^ s_p.sram[i]) * 16777619u;
	return h;
}

/* the SRAM beside the cartridge, as NAME.sav */
static void sram_save(void)
{
	if (!s_loaded || !s_p.foot.sram_size)
		return;
	FILE *f = fopen(s_save_path, "wb");
	if (!f)
		return;
	fwrite(s_p.sram, 1, s_p.foot.sram_size, f);
	fclose(f);
	s_sram_sum = sram_sum();
}

static void set_title(const char *extra)
{
	char t[1200];
	if (!s_loaded)
		snprintf(t, sizeof t, "bpc-emu - drop a Pocket Challenge V2 cartridge here");
	else {
		const char *base = strrchr(s_rom_path, '/');
		const char *b2 = strrchr(s_rom_path, '\\');
		if (!base || (b2 && b2 > base))
			base = b2;
		snprintf(t, sizeof t, "bpc-emu - %s%s", base ? base + 1 : s_rom_path, extra);
	}
	SDL_SetWindowTitle(s_win, t);
}

static int load(const char *path)
{
	size_t size;
	uint8_t *data = read_file(path, &size);
	if (!data) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "bpc-emu", "Cannot read the file.", s_win);
		return 0;
	}
	sram_save();
	char err[256];
	int ok = pcv2_load(&s_p, data, size, err, sizeof err);
	free(data);
	if (!ok) {
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "bpc-emu", err, s_win);
		return 0;
	}
	snprintf(s_rom_path, sizeof s_rom_path, "%s", path);
	snprintf(s_save_path, sizeof s_save_path, "%s", path);
	char *dot = strrchr(s_save_path, '.');
	char *sl = strrchr(s_save_path, '/'), *bs = strrchr(s_save_path, '\\');
	if (dot && dot > (sl ? sl : s_save_path) && dot > (bs ? bs : s_save_path))
		*dot = 0;
	strcat(s_save_path, ".sav");
	FILE *f = fopen(s_save_path, "rb");
	if (f) {
		if (fread(s_p.sram, 1, s_p.foot.sram_size, f) != s_p.foot.sram_size)
			memset(s_p.sram, 0, sizeof s_p.sram);
		fclose(f);
	}
	s_sram_sum = sram_sum();
	pcv2_reset(&s_p);
	s_loaded = 1;
	set_title("");
	return 1;
}

static uint16_t buttons(void)
{
	const uint8_t *k = SDL_GetKeyboardState(NULL);
	uint16_t b = 0;
	if (k[SDL_SCANCODE_UP]) b |= PCV2_UP;
	if (k[SDL_SCANCODE_DOWN]) b |= PCV2_DOWN;
	if (k[SDL_SCANCODE_LEFT]) b |= PCV2_LEFT;
	if (k[SDL_SCANCODE_RIGHT]) b |= PCV2_RIGHT;
	if (k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER]) b |= PCV2_CIRCLE;
	if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_BACKSPACE]) b |= PCV2_CLEAR;
	if (k[SDL_SCANCODE_C]) b |= PCV2_PASS;
	if (k[SDL_SCANCODE_V]) b |= PCV2_VIEW;
	if (k[SDL_SCANCODE_ESCAPE]) b |= PCV2_ESC;
	if (s_pad) {
		SDL_GameController *p = s_pad;
		const int dz = 16000;
		if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_UP) ||
			SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY) < -dz) b |= PCV2_UP;
		if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_DOWN) ||
			SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY) > dz) b |= PCV2_DOWN;
		if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_LEFT) ||
			SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX) < -dz) b |= PCV2_LEFT;
		if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ||
			SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX) > dz) b |= PCV2_RIGHT;
		if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_A)) b |= PCV2_CIRCLE;
		if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_B)) b |= PCV2_ESC;
		if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_X)) b |= PCV2_CLEAR;
		if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_Y)) b |= PCV2_PASS;
		if (SDL_GameControllerGetButton(p, SDL_CONTROLLER_BUTTON_BACK)) b |= PCV2_VIEW;
	}
	return b;
}

static void picture(void)
{
	for (int y = 0; y < SCREEN_H; y++)
		for (int x = 0; x < SCREEN_W; x++)
			s_px[y * SCREEN_W + x] = s_lcd[s_p.m.fb[y][x] & 15];
}

static void screenshot(void)
{
	char path[1200];
	snprintf(path, sizeof path, "%s", s_save_path);
	char *dot = strrchr(path, '.');
	if (dot)
		*dot = 0;
	char tail[48];
	snprintf(tail, sizeof tail, "_%06llu.png", (unsigned long long)s_p.m.frame);
	strcat(path, tail);
	if (png_write(path, s_px, SCREEN_W, SCREEN_W, SCREEN_H))
		printf("screenshot: %s\n", path);
}

int main(int argc, char **argv)
{
	const char *rom = NULL;
	int scale = 4;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--scale") && i + 1 < argc)
			scale = atoi(argv[++i]);
		else
			rom = argv[i];
	}
	if (scale < 1)
		scale = 1;
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
		fprintf(stderr, "SDL: %s\n", SDL_GetError());
		return 1;
	}
	s_win = SDL_CreateWindow("bpc-emu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
		SCREEN_W * scale, SCREEN_H * scale, SDL_WINDOW_RESIZABLE);
	s_ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_ACCELERATED);
	SDL_RenderSetLogicalSize(s_ren, SCREEN_W, SCREEN_H);
	SDL_RenderSetIntegerScale(s_ren, SDL_TRUE);
	s_tex = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
	SDL_AudioSpec want = { 0 }, have;
	want.freq = AUDIO_HZ;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 512;
	s_adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (s_adev)
		SDL_PauseAudioDevice(s_adev, 0);
	/* the LCD: level 0 a dark grey-green, 15 the pale background */
	for (int i = 0; i < 16; i++) {
		int r = 0x30 + (0xC8 - 0x30) * i / 15, g = 0x38 + (0xD0 - 0x38) * i / 15, b = 0x30 + (0xB8 - 0x30) * i / 15;
		s_lcd[i] = (uint32_t)(r << 16 | g << 8 | b);
	}
	set_title("");
	if (rom)
		load(rom);

	int paused = 0, run = 1;
	const double frame_s = (double)LINE_CYC * FRAME_LINES / CPU_HZ;   /* 75.47 Hz */
	const uint64_t freq = SDL_GetPerformanceFrequency();
	uint64_t next = SDL_GetPerformanceCounter();
	uint32_t last_save = SDL_GetTicks();
	while (run) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			switch (e.type) {
			case SDL_QUIT: run = 0; break;
			case SDL_DROPFILE:
				load(e.drop.file);
				SDL_free(e.drop.file);
				break;
			case SDL_CONTROLLERDEVICEADDED:
				if (!s_pad)
					s_pad = SDL_GameControllerOpen(e.cdevice.which);
				break;
			case SDL_CONTROLLERDEVICEREMOVED:
				if (s_pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_pad))) {
					SDL_GameControllerClose(s_pad);
					s_pad = NULL;
				}
				break;
			case SDL_KEYDOWN:
				if (e.key.repeat)
					break;
				switch (e.key.keysym.sym) {
				case SDLK_F2:
					if (s_loaded) {
						sram_save();
						pcv2_reset(&s_p);
					}
					break;
				case SDLK_p:
					paused = !paused;
					set_title(paused ? " (paused)" : "");
					break;
				case SDLK_F11: {
					uint32_t fs = SDL_GetWindowFlags(s_win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
					SDL_SetWindowFullscreen(s_win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
					break;
				}
				case SDLK_F12:
					if (s_loaded)
						screenshot();
					break;
				}
				break;
			}
		}
		if (s_loaded && !paused) {
			s_p.m.keys = buttons();
			aswan_run_frame(&s_p.m);
			int16_t buf[2 * 2048];
			int n = aswan_audio_take(&s_p.m, buf, 2048);
			/* keep about 4 frames queued; drop rather than drift */
			if (s_adev && n > 0 && SDL_GetQueuedAudioSize(s_adev) < (uint32_t)(AUDIO_HZ / 75 * 4 * 4))
				SDL_QueueAudio(s_adev, buf, (uint32_t)n * 4);
			picture();
		} else if (!s_loaded)
			for (int i = 0; i < SCREEN_W * SCREEN_H; i++)
				s_px[i] = s_lcd[15];
		SDL_UpdateTexture(s_tex, NULL, s_px, SCREEN_W * 4);
		SDL_SetRenderDrawColor(s_ren, 0x20, 0x20, 0x20, 255);
		SDL_RenderClear(s_ren);
		SDL_RenderCopy(s_ren, s_tex, NULL, NULL);
		SDL_RenderPresent(s_ren);
		if (s_loaded && SDL_GetTicks() - last_save > 5000) {
			last_save = SDL_GetTicks();
			if (sram_sum() != s_sram_sum)
				sram_save();
		}
		next += (uint64_t)(frame_s * freq);
		uint64_t now = SDL_GetPerformanceCounter();
		if (next > now)
			SDL_Delay((uint32_t)((next - now) * 1000 / freq));
		else if (now - next > freq / 4)
			next = now;          /* far behind: start again from now */
	}
	sram_save();
	SDL_Quit();
	return 0;
}
