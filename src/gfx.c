/* Copyright (C) 2023 Nunuhara Cabbage <nunuhara@haniwa.technology>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <SDL.h>

#include "nulib.h"
#include "nulib/file.h"
#include "ai5/cg.h"

#include "ai5.h"
#include "game.h"
#include "gfx_private.h"
#include "vm.h"

#define gfx_decode_direct(color) _gfx_decode_direct(color, __func__)
static inline SDL_Color _gfx_decode_direct(uint32_t color, const char *func)
{
	if (game->bpp == 16)
		return gfx_decode_bgr555(color);
	if (game->bpp == 24)
		return gfx_decode_bgr(color);
	VM_ERROR("Invalid bpp for %s", func);
}

void gfx_error_message(const char *message)
{
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fatal Error - AI5-SDL2", message, gfx.window);
}

bool gfx_confirm_quit(void)
{
	static const SDL_MessageBoxButtonData buttons[] = {
		{ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "OK" },
		{ SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel" },
	};
	const SDL_MessageBoxData mbox = {
		SDL_MESSAGEBOX_INFORMATION,
		NULL,
		"Really Quit? - AI5-SDL2",
		"Are you sure you want to quit?\n"
		"Any unsaved progress will be lost.\n"
		"Quit game?",
		SDL_arraysize(buttons),
		buttons,
		NULL
	};
	int result = 1;
	if (SDL_ShowMessageBox(&mbox, &result))
		WARNING("Error displaying message box");
	return result;
}

struct gfx gfx = {0};
struct gfx_view gfx_view = { 640, 400 };

void gfx_dirty(unsigned surface, int x, int y, int w, int h)
{
	gfx.surface[surface].dirty = true;
	SDL_Rect r = { x, y, w, h };
	SDL_UnionRect(&gfx.surface[surface].damaged, &r, &gfx.surface[surface].damaged);
}

bool gfx_is_dirty(unsigned surface)
{
	return gfx.surface[surface].dirty;
}

void gfx_clean(unsigned surface)
{
	gfx.surface[surface].dirty = false;
	gfx.surface[surface].damaged = (SDL_Rect){0};
}

void gfx_screen_dirty(void)
{
	gfx.surface[gfx.screen].dirty = true;
	gfx.surface[gfx.screen].damaged = gfx.surface[gfx.screen].src;
}

void gfx_whole_surface_dirty(unsigned surface)
{
	struct gfx_surface *s = &gfx.surface[surface];
	gfx_dirty(surface, s->src.x, s->src.y, s->src.w, s->src.h);
}

static bool gfx_overlay_enabled = true;

void gfx_overlay_enable(void)
{
	gfx_overlay_enabled = true;
	gfx_screen_dirty();
}

void gfx_overlay_disable(void)
{
	gfx_overlay_enabled = false;
	gfx_screen_dirty();
}

SDL_Surface *gfx_get_surface(unsigned i)
{
	if (unlikely(i >= GFX_NR_SURFACES || !gfx.surface[i].s)) {
		WARNING("Invalid surface index: %u", i);
		i = 0;
	}
	return gfx.surface[i].s;
}

static SDL_Surface *gfx_screen(void)
{
	return gfx.surface[gfx.screen].s;
}

unsigned gfx_current_surface(void)
{
	return gfx.screen;
}

static SDL_Surface *gfx_create_surface(unsigned w, unsigned h, uint8_t r, uint8_t g, uint8_t b)
{
	SDL_Surface *s;
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, s, 0, w, h,
			game->bpp == 8 ? GFX_INDEXED_BPP : GFX_DIRECT_BPP,
			game->bpp == 8 ? GFX_INDEXED_FORMAT : GFX_DIRECT_FORMAT);
	SDL_CALL(SDL_FillRect, s, NULL, SDL_MapRGB(s->format, r, g, b));
	return s;
}

static SDL_Texture *gfx_create_texture(unsigned w, unsigned h)
{
	SDL_Texture *t;
	SDL_CTOR(SDL_CreateTexture, t, gfx.renderer, gfx.display->format->format,
			SDL_TEXTUREACCESS_STATIC, w, h);
	return t;
}

SDL_Surface *gfx_get_overlay(void)
{
	if (gfx.overlay)
		return gfx.overlay;
	SDL_Surface *s;
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, s, 0, gfx_view.w, gfx_view.h,
			32, SDL_PIXELFORMAT_RGBA32);
	SDL_CALL(SDL_FillRect, s, NULL, SDL_MapRGBA(s->format, 0, 0, 0, 0));
	gfx.overlay = s;
	return s;
}

void gfx_window_toggle_fullscreen(void)
{
	uint32_t flag = SDL_WINDOW_FULLSCREEN_DESKTOP;
	bool fullscreen = SDL_GetWindowFlags(gfx.window) & flag;
	SDL_SetWindowFullscreen(gfx.window, fullscreen ? 0 : flag);
}

void gfx_window_increase_integer_size(void)
{
	int w, h;
	SDL_GetWindowSize(gfx.window, &w, &h);

	int i = 1;
	int target_w, target_h;
	do {
		target_w = gfx_view.w * i;
		target_h = gfx_view.h * i;
		i++;
	} while (target_w <= w && target_h <= h);

	SDL_SetWindowSize(gfx.window, target_w, target_h);
}

void gfx_window_decrease_integer_size(void)
{
	int w, h;
	SDL_GetWindowSize(gfx.window, &w, &h);

	int i = 1;
	int target_w, target_h;
	do {
		target_w = gfx_view.w * i;
		target_h = gfx_view.h * i;
		i++;
	} while (target_w < w && target_h < h);

	i = max(1, i - 2);
	SDL_SetWindowSize(gfx.window, gfx_view.w * i, gfx_view.h * i);
}

static struct cg *gfx_surface_to_cg(SDL_Surface *s)
{
	struct cg *cg = xcalloc(1, sizeof(struct cg));
	cg->metrics.w = s->w;
	cg->metrics.h = s->h;
	cg->metrics.bpp = 24;
	cg->ref = 1;

	cg->pixels = xcalloc(cg->metrics.w * cg->metrics.h, 4);
	if (game->bpp == 8) {
		for (int row = 0; row < cg->metrics.h; row++) {
			uint8_t *src = s->pixels + s->pitch * row;
			uint8_t *dst = cg->pixels + cg->metrics.w * 4 * row;
			for (int col = 0; col < cg->metrics.w; col++, src++, dst += 4) {
				SDL_Color *c = &gfx.palette[*src];
				dst[0] = c->r;
				dst[1] = c->g;
				dst[2] = c->b;
				dst[3] = 255;
			}
		}
	} else {
		for (int row = 0; row < cg->metrics.h; row++) {
			uint8_t *src = s->pixels + s->pitch * row;
			uint8_t *dst = cg->pixels + cg->metrics.w * 4 * row;
			for (int col = 0; col < cg->metrics.w; col++, src += 3, dst += 4) {
				memcpy(dst, src, 3);
				dst[3] = 255;
			}
		}
	}
	return cg;
}

void gfx_screenshot(void)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	char name[1024];
	strftime(name, 1024, "Screenshots/%Y%m%d_%H%M%S.png", tm);

	if (mkdir_p("Screenshots") < 0) {
		WARNING("mkdir_p: %s", strerror(errno));
		return;
	}

	struct cg *cg = gfx_surface_to_cg(gfx_screen());
	FILE *f = file_open_utf8(name, "wb");
	if (!f) {
		WARNING("file_open_utf8: %s", strerror(errno));
		return;
	}
	cg_write(cg, f, CG_TYPE_PNG);
	fclose(f);
	cg_free(cg);
	NOTICE("Saved screenshot to \"%s\"", name);
}

static void gfx_init_window(void)
{
	gfx_view.w = game->surface_sizes[0].w;
	gfx_view.h = game->surface_sizes[0].h;

	SDL_CALL(SDL_RenderSetLogicalSize, gfx.renderer, gfx_view.w, gfx_view.h);

	// free old surfaces/texture
	for (int i = 0; i < GFX_NR_SURFACES && gfx.surface[i].s; i++) {
		SDL_FreeSurface(gfx.surface[i].s);
	}
	SDL_FreeSurface(gfx.display);
	SDL_FreeSurface(gfx.scaled_display);
	SDL_DestroyTexture(gfx.texture);

	// recreate and initialize surfaces/texture
	for (int i = 0; i < GFX_NR_SURFACES; i++) {
		unsigned w = game->surface_sizes[i].w;
		unsigned h = game->surface_sizes[i].h;
		if (!w || !h) {
			for (; i < GFX_NR_SURFACES; i++) {
				gfx.surface[i].s = NULL;
			}
			break;
		}

		gfx.surface[i].s = gfx_create_surface(w, h, 0, 0, 0);
		gfx.surface[i].src = (SDL_Rect) { 0, 0, w, h };
		gfx.surface[i].dst = (SDL_Rect) { 0, 0, w, h };
		gfx.surface[i].scaled = false;
		gfx.surface[i].dirty = false;
		gfx.surface[i].damaged = (SDL_Rect) {0};
	}
	gfx_screen_dirty();

	SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, gfx.display, 0, gfx_view.w, gfx_view.h,
			GFX_DIRECT_BPP, GFX_DIRECT_FORMAT);
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, gfx.scaled_display, 0, gfx_view.w,
			gfx_view.h, GFX_DIRECT_BPP, GFX_DIRECT_FORMAT);
	SDL_CALL(SDL_FillRect, gfx.display, NULL, SDL_MapRGB(gfx.display->format, 0, 0, 0));
	SDL_CALL(SDL_FillRect, gfx.scaled_display, NULL,
			SDL_MapRGB(gfx.scaled_display->format, 0, 0, 0));

	gfx.texture = gfx_create_texture(gfx_view.w, gfx_view.h);
}

void gfx_set_icon(void)
{
	SDL_Surface *icon = icon_get(0);
	if (!icon) {
		WARNING("Failed to load window icon");
		return;
	}
	SDL_SetWindowIcon(gfx.window, icon);
}

static void gfx_fini(void)
{
	if (gfx.display) {
		for (int i = 0; i < GFX_NR_SURFACES && gfx.surface[i].s; i++) {
			SDL_FreeSurface(gfx.surface[i].s);
		}
		SDL_FreeSurface(gfx.display);
		SDL_FreeSurface(gfx.scaled_display);
		if (gfx.overlay)
			SDL_FreeSurface(gfx.overlay);
		SDL_DestroyTexture(gfx.texture);
		SDL_DestroyRenderer(gfx.renderer);
		SDL_DestroyWindow(gfx.window);
		SDL_Quit();
	}
	gfx_screen_dirty();
}

void gfx_init(const char *name)
{
	char title[2048];
	if (name) {
		snprintf(title, 2048, "%s - AI5-SDL2", name);
	} else {
		strcpy(title, "AI5-SDL2");
	}
	gfx_view.w = game->surface_sizes[0].w;
	gfx_view.h = game->surface_sizes[0].h;
	SDL_CALL(SDL_Init, SDL_INIT_VIDEO | SDL_INIT_TIMER);
#ifndef USE_SDL_MIXER
	SDL_CALL(SDL_InitSubSystem, SDL_INIT_AUDIO);
#endif
	SDL_CTOR(SDL_CreateWindow, gfx.window, title,
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, gfx_view.w, gfx_view.h,
			SDL_WINDOW_RESIZABLE);
	gfx.window_id = SDL_GetWindowID(gfx.window);
	SDL_CTOR(SDL_CreateRenderer, gfx.renderer, gfx.window, -1, 0);
	SDL_CALL(SDL_SetRenderDrawColor, gfx.renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
	gfx_init_window();
	atexit(gfx_fini);
}

void gfx_update(void)
{
	struct gfx_surface *screen = &gfx.surface[gfx.screen];
	if (gfx.hidden || !screen->dirty)
		return;
	SDL_CALL(SDL_BlitSurface, screen->s, &screen->damaged, gfx.display, &screen->damaged);
	if (gfx.overlay && gfx_overlay_enabled)
		SDL_CALL(SDL_BlitSurface, gfx.overlay, &screen->damaged, gfx.display, &screen->damaged);
	if (screen->scaled) {
		SDL_Rect src = screen->src;
		SDL_Rect dst = screen->dst;
		SDL_CALL(SDL_BlitScaled, gfx.display, &src, gfx.scaled_display, &dst);
		SDL_CALL(SDL_UpdateTexture, gfx.texture, NULL, gfx.scaled_display->pixels,
				gfx.scaled_display->pitch);
	} else {
		uint8_t *p = gfx.display->pixels + screen->damaged.y * gfx.display->pitch
			+ screen->damaged.x * gfx.display->format->BytesPerPixel;
		SDL_CALL(SDL_UpdateTexture, gfx.texture, &screen->damaged, p, gfx.display->pitch);
	}
	SDL_CALL(SDL_RenderClear, gfx.renderer);
	SDL_CALL(SDL_RenderCopy, gfx.renderer, gfx.texture, NULL, NULL);
	SDL_RenderPresent(gfx.renderer);
	gfx_clean(gfx.screen);
}

void gfx_display_freeze(void)
{
	GFX_LOG("gfx_display_freeze");
	gfx.hidden = true;
}

void gfx_display_unfreeze(void)
{
	GFX_LOG("gfx_display_unfreeze");
	gfx.hidden = false;
	gfx_screen_dirty();
}

#define FADE_FRAME_TIME 16

void _gfx_display_fade_out(uint32_t vm_color, unsigned ms, bool(*cb)(void))
{
	GFX_LOG("gfx_display_fade_out(%u,%u)", vm_color, ms);
	if (gfx.hidden)
		return;
	gfx.hidden = true;

	int step = roundf(256.f / ((ms * config.transition_speed) / FADE_FRAME_TIME));

	// create mask texture with solid color
	SDL_Color c;
	if (game->bpp == 8) {
		c = gfx.palette[vm_color];
	} else {
		c = gfx_decode_direct(vm_color);
	}
	SDL_CALL(SDL_FillRect, gfx.display, NULL, SDL_MapRGB(gfx.display->format, c.r, c.g, c.b));
	SDL_Texture *mask = gfx_create_texture(gfx_view.w, gfx_view.h);
	SDL_CALL(SDL_UpdateTexture, mask, NULL, gfx.display->pixels, gfx.display->pitch);
	SDL_CALL(SDL_SetTextureBlendMode, mask, SDL_BLENDMODE_BLEND);

	vm_timer_t timer = vm_timer_create();
	for (int i = 0; i < 256; i += step) {
		SDL_CALL(SDL_SetTextureAlphaMod, mask, i);
		SDL_CALL(SDL_RenderClear, gfx.renderer);
		SDL_CALL(SDL_RenderCopy, gfx.renderer, gfx.texture, NULL, NULL);
		SDL_CALL(SDL_RenderCopy, gfx.renderer, mask, NULL, NULL);
		SDL_RenderPresent(gfx.renderer);

		vm_peek();
		vm_timer_tick(&timer, FADE_FRAME_TIME);
		if (cb && !cb())
			break;
	}

	SDL_CALL(SDL_SetTextureAlphaMod, mask, 255);
	SDL_CALL(SDL_RenderClear, gfx.renderer);
	SDL_CALL(SDL_RenderCopy, gfx.renderer, mask, NULL, NULL);
	SDL_RenderPresent(gfx.renderer);
}

void gfx_display_fade_out(uint32_t vm_color, unsigned ms)
{
	_gfx_display_fade_out(vm_color, ms, NULL);
}

void _gfx_display_fade_in(unsigned ms, bool(*cb)(void))
{
	GFX_LOG("gfx_display_fade_in(%u)", ms);

	int step = roundf(256.f / ((ms * config.transition_speed) / FADE_FRAME_TIME));
	SDL_Texture *mask = gfx_create_texture(gfx_view.w, gfx_view.h);
	SDL_CALL(SDL_UpdateTexture, mask, NULL, gfx.display->pixels, gfx.display->pitch);
	SDL_CALL(SDL_SetTextureBlendMode, mask, SDL_BLENDMODE_BLEND);

	SDL_CALL(SDL_BlitSurface, gfx.surface[gfx.screen].s, NULL, gfx.display, NULL);
	SDL_CALL(SDL_UpdateTexture, gfx.texture, NULL, gfx.display->pixels, gfx.display->pitch);

	vm_timer_t timer = vm_timer_create();
	for (int i = 255; i >= 0; i -= step) {
		SDL_CALL(SDL_SetTextureAlphaMod, mask, i);
		SDL_CALL(SDL_RenderClear, gfx.renderer);
		SDL_CALL(SDL_RenderCopy, gfx.renderer, gfx.texture, NULL, NULL);
		SDL_CALL(SDL_RenderCopy, gfx.renderer, mask, NULL, NULL);
		SDL_RenderPresent(gfx.renderer);

		vm_peek();
		vm_timer_tick(&timer, FADE_FRAME_TIME);
		if (cb && !cb())
			break;
	}

	SDL_CALL(SDL_RenderClear, gfx.renderer);
	SDL_CALL(SDL_RenderCopy, gfx.renderer, gfx.texture, NULL, NULL);
	SDL_RenderPresent(gfx.renderer);

	gfx.hidden = false;
	gfx_screen_dirty();
}

void gfx_display_fade_in(unsigned ms)
{
	_gfx_display_fade_in(ms, NULL);
}

void gfx_display_hide(uint32_t color)
{
	GFX_LOG("gfx_hide_screen");
	// fill screen with color and prevent updates
	if (game->bpp != 8) {
		SDL_Color c = gfx_decode_direct(color);
		SDL_CALL(SDL_SetRenderDrawColor, gfx.renderer, c.r, c.g, c.b, 255);
	}
	SDL_CALL(SDL_RenderClear, gfx.renderer);
	SDL_RenderPresent(gfx.renderer);
	gfx.hidden = true;
}

void gfx_display_unhide(void)
{
	GFX_LOG("gfx_unhide_screen");
	// allow updates
	gfx.hidden = false;
	gfx_screen_dirty();
	// restore clear color
	if (game->bpp != 8) {
		SDL_CALL(SDL_SetRenderDrawColor, gfx.renderer, 0, 0, 0, 0);
	}
}

static void read_palette(SDL_Color *dst, const uint8_t *src)
{
	for (int i = 0; i < 256; i++) {
		dst[i].b = src[i*4+0];
		dst[i].g = src[i*4+1];
		dst[i].r = src[i*4+2];
		dst[i].a = 255;
	}
}

void gfx_update_palette(int n)
{
	if (game->bpp != 8)
		return;
	SDL_CALL(SDL_SetPaletteColors, gfx_screen()->format->palette, gfx.palette, 0, n);
	gfx_screen_dirty();
}

void gfx_palette_set(const uint8_t *data)
{
	GFX_LOG("gfx_palette_set (...)");
	read_palette(gfx.palette, data);
	gfx_update_palette(256);
}

void gfx_palette_set_color(uint8_t c, uint8_t r, uint8_t g, uint8_t b)
{
	gfx.palette[c].r = r;
	gfx.palette[c].g = g;
	gfx.palette[c].b = b;
	gfx_update_palette(256);
}

static void pal_set_color(uint8_t *pal, uint8_t i, uint8_t r, uint8_t g, uint8_t b)
{
	pal[i*4+0] = b;
	pal[i*4+1] = g;
	pal[i*4+2] = r;
	pal[i*4+3] = 0;
}

static void gfx_dump_surface_direct(SDL_Surface *s, const char *filename)
{
	struct cg *cg = gfx_surface_to_cg(s);
	FILE *f = file_open_utf8(filename, "wb");
	cg_write(cg, f, CG_TYPE_PNG);
	fclose(f);
	cg_free(cg);
}

/*
 * Write a surface to a PNG file, altering the palette so that every color
 * index is identifiable.
 */
void gfx_dump_surface(unsigned i, const char *filename)
{
	SDL_Surface *s = gfx_get_surface(i);
	if (game->bpp > 8) {
		gfx_dump_surface_direct(s, filename);
		return;
	}

	struct cg *cg = xcalloc(1, sizeof(struct cg));
	cg->metrics.w = s->w;
	cg->metrics.h = s->h;
	cg->metrics.bpp = 8;
	cg->ref = 1;

	cg->palette = xcalloc(4, 256);
	pal_set_color(cg->palette, 0,  0,   0,   0);
	pal_set_color(cg->palette, 1,  127, 127, 127);
	pal_set_color(cg->palette, 2,  255, 255, 255);

	pal_set_color(cg->palette, 3,  255, 0,   0);
	pal_set_color(cg->palette, 4,  255, 255, 0);
	pal_set_color(cg->palette, 5,  255, 0,   255);

	pal_set_color(cg->palette, 6,  0,   255, 0);
	pal_set_color(cg->palette, 7,  255, 255, 0);
	pal_set_color(cg->palette, 8,  0,   255, 255);

	pal_set_color(cg->palette, 9,  0,   0,   255);
	pal_set_color(cg->palette, 10, 255, 0,   255);
	pal_set_color(cg->palette, 11, 0,   255, 255);

	pal_set_color(cg->palette, 12, 255, 127, 63);
	pal_set_color(cg->palette, 13, 127, 255, 63);
	pal_set_color(cg->palette, 14, 127, 63, 255);
	pal_set_color(cg->palette, 14, 31,  63, 255);

	cg->pixels = xcalloc(cg->metrics.w, cg->metrics.h);
	for (int row = 0; row < cg->metrics.h; row++) {
		uint8_t *src = s->pixels + s->pitch * row;
		uint8_t *dst = cg->pixels + cg->metrics.w * row;
		memcpy(dst, src, cg->metrics.w);
	}

	FILE *f = file_open_utf8(filename, "wb");
	cg_write(cg, f, CG_TYPE_PNG);
	fclose(f);
	cg_free(cg);
}

static uint8_t u8_interp(uint8_t a, uint8_t b, float rate)
{
	int d = b - a;
	return a + d * rate;
}

static void _gfx_palette_crossfade(SDL_Color *new, unsigned ms)
{
	SDL_Color old[256];
	memcpy(old, gfx.palette, sizeof(old));

	ms *= config.transition_speed;

	// get a list of palette indices that need to be interpolated
	uint8_t fading[256];
	int nr_fading = 0;
	int max_fading = 0;
	for (int i = 0; i < 256; i++) {
		if (old[i].r != new[i].r || old[i].g != new[i].g || old[i].b != new[i].b) {
			fading[nr_fading++] = i;
			max_fading = max(max_fading, i);
		}
	}
	if(unlikely(nr_fading == 0))
		return;

	// interpolate between new and old palette over given ms
	unsigned start_t = vm_get_ticks();
	unsigned t = 0;
	while (t < ms) {
		float rate = (float)t / (float)ms;
		for (int i = 0; i < nr_fading; i++) {
			uint8_t c = fading[i];
			gfx.palette[c].r = u8_interp(old[c].r, new[c].r, rate);
			gfx.palette[c].g = u8_interp(old[c].g, new[c].g, rate);
			gfx.palette[c].b = u8_interp(old[c].b, new[c].b, rate);
		}
		gfx_update_palette(max_fading + 1);

		// update
		vm_peek();
		SDL_Delay(16);
		t = vm_get_ticks() - start_t;
	}

	for (int i = 0; i < nr_fading; i++) {
		uint8_t c = fading[i];
		gfx.palette[c].r = new[c].r;
		gfx.palette[c].g = new[c].g;
		gfx.palette[c].b = new[c].b;
	}
	gfx_update_palette(max_fading);
}

void gfx_palette_crossfade(const uint8_t *data, unsigned ms)
{
	GFX_LOG("gfx_palette_crossfade[%u] (...)", ms);
	SDL_Color new[256];
	read_palette(new, data);
	_gfx_palette_crossfade(new, ms);
}

void gfx_palette_crossfade_to(uint8_t r, uint8_t g, uint8_t b, unsigned ms)
{
	GFX_LOG("gfx_palette_crossfade_to[%u] (%u,%u,%u)", ms, r, g, b);
	SDL_Color new[256];
	for (int i = 0; i < 256; i++) {
		new[i].r = r;
		new[i].g = g;
		new[i].b = b;
		new[i].a = 255;
	}
	_gfx_palette_crossfade(new, ms);
}

void gfx_set_screen_surface(unsigned i)
{
	GFX_LOG("gfx_set_screen_surface %u", i);
	if (unlikely(i >= GFX_NR_SURFACES || !gfx.surface[i].s))
		VM_ERROR("Invalid surface number: %u", i);
	gfx.screen = i;
	gfx_update_palette(256);
}

/*
 * XXX: AI5WIN.EXE doesn't clip. If the rectangle exceeds the bounds of the destination
 *      surface, it just writes to buggy addresses.
 */
bool gfx_copy_clip(SDL_Surface *src, SDL_Rect *src_r, SDL_Surface *dst, SDL_Point *dst_p)
{
	if (unlikely(src_r->x < 0)) {
		src_r->w += src_r->x;
		dst_p->x -= src_r->x;
		src_r->x = 0;
	}
	if (unlikely(src_r->y < 0)) {
		src_r->h += src_r->y;
		dst_p->y -= src_r->y;
		src_r->y = 0;
	}
	if (unlikely(dst_p->x < 0)) {
		src_r->w += dst_p->x;
		src_r->x -= dst_p->x;
		dst_p->x = 0;
	}
	if (unlikely(dst_p->y < 0)) {
		src_r->h += dst_p->y;
		src_r->y -= dst_p->y;
		dst_p->y = 0;
	}
	if (unlikely(src_r->x + src_r->w > src->w)) {
		src_r->w = src->w - src_r->x;
	}
	if (unlikely(src_r->y + src_r->h > src->h)) {
		src_r->h = src->h - src_r->y;
	}
	if (unlikely(dst_p->x + src_r->w > dst->w)) {
		src_r->w = dst->w - dst_p->x;
	}
	if (unlikely(dst_p->y + src_r->h > dst->h)) {
		src_r->h = dst->h - dst_p->y;
	}
	return src_r->w > 0 && src_r->h > 0;
}

bool gfx_fill_clip(SDL_Surface *s, SDL_Rect *r)
{
	if (unlikely(r->x < 0)) {
		r->w += r->x;
		r->x = 0;
	}
	if (unlikely(r->y < 0)) {
		r->h += r->y;
		r->y = 0;
	}
	if (unlikely(r->x + r->w > s->w)) {
		r->w = s->w - r->x;
	}
	if (unlikely(r->y + r->h > s->h)) {
		r->h = s->h - r->y;
	}
	return r->w > 0 && r->h > 0;
}

static bool gfx_copy_begin(SDL_Surface *src, SDL_Rect *src_r, SDL_Surface *dst,
		SDL_Point *dst_p)
{
	if (!gfx_copy_clip(src, src_r, dst, dst_p)) {
		WARNING("Invalid copy");
		return false;
	}

	if (SDL_MUSTLOCK(src))
		SDL_CALL(SDL_LockSurface, src);
	if (SDL_MUSTLOCK(dst))
		SDL_CALL(SDL_LockSurface, dst);
	return true;
}

static bool gfx_fill_begin(SDL_Surface *dst, SDL_Rect *r)
{
	if (!gfx_fill_clip(dst, r)) {
		WARNING("Invalid fill");
		return false;
	}

	if (SDL_MUSTLOCK(dst))
		SDL_CALL(SDL_LockSurface, dst);
	return true;
}

static void gfx_copy_end(SDL_Surface *src, SDL_Surface *dst)
{
	if (SDL_MUSTLOCK(src))
		SDL_UnlockSurface(src);
	if (SDL_MUSTLOCK(dst))
		SDL_UnlockSurface(dst);
}

static void gfx_fill_end(SDL_Surface *dst)
{
	if (SDL_MUSTLOCK(dst))
		SDL_UnlockSurface(dst);
}

#define PIXEL_P(s, x, y, byte_pp) \
	((s)->pixels + (y) * (s)->pitch + (x) * (byte_pp))
#define INDEXED_PIXEL_P(s, x, y) PIXEL_P(s, x, y, 1)
#define DIRECT_PIXEL_P(s, x, y) PIXEL_P(s, x, y, 3)

#define indexed_foreach_row2(src_row, dst_row, src, src_r, dst, dst_p, ...) \
	for (int ifer2_row = 0; ifer2_row < (src_r)->h; ifer2_row++) { \
		uint8_t *src_row = INDEXED_PIXEL_P(src, (src_r)->x, (src_r)->y + ifer2_row); \
		uint8_t *dst_row = INDEXED_PIXEL_P(dst, (dst_p)->x, (dst_p)->y + ifer2_row); \
		__VA_ARGS__ \
	}

#define indexed_foreach_px2(src_px, dst_px, src, src_r, dst, dst_p, ...) \
	for (int ifep2_row = 0; ifep2_row < (src_r)->h; ifep2_row++) { \
		uint8_t *src_px = INDEXED_PIXEL_P(src, (src_r)->x, (src_r)->y + ifep2_row); \
		uint8_t *dst_px = INDEXED_PIXEL_P(dst, (dst_p)->x, (dst_p)->y + ifep2_row); \
		for (int ifep2_col = 0; ifep2_col < (src_r)->w; ifep2_col++, src_px++, dst_px++) { \
			__VA_ARGS__ \
		} \
	}

#define indexed_foreach_px(px, dst, dst_r, ...) \
	for (int ifep_row = 0; ifep_row < (dst_r)->h; ifep_row++) { \
		uint8_t *px = INDEXED_PIXEL_P(dst, (dst_r)->x, (dst_r)->y + ifep_row); \
		for (int ifep_col = 0; ifep_col < (dst_r)->w; ifep_col++, px++) { \
			__VA_ARGS__ \
		} \
	}

#define direct_foreach_px2(src_px, dst_px, src, src_r, dst, dst_p, ...) \
	for (int dfep2_row = 0; dfep2_row < (src_r)->h; dfep2_row++) { \
		uint8_t *src_px = DIRECT_PIXEL_P(src, (src_r)->x, (src_r)->y + dfep2_row); \
		uint8_t *dst_px = DIRECT_PIXEL_P(dst, (dst_p)->x, (dst_p)->y + dfep2_row); \
		for (int dfep2_col = 0; dfep2_col < (src_r)->w; dfep2_col++, src_px += 3, dst_px += 3) { \
			__VA_ARGS__ \
		} \
	}

#define direct_foreach_px(px, dst, dst_r, ...) \
	for (int dfep_row = 0; dfep_row < (dst_r)->h; dfep_row++) { \
		uint8_t *px = DIRECT_PIXEL_P(dst, (dst_r)->x, (dst_r)->y + dfep_row); \
		for (int dfep_col = 0; dfep_col < (dst_r)->w; dfep_col++, px += 3) { \
			__VA_ARGS__ \
		} \
	}

static void gfx_indexed_copy(int src_x, int src_y, int w, int h, SDL_Surface *src, int dst_x,
		int dst_y, SDL_Surface *dst)
{
	SDL_Rect src_r = { src_x, src_y, w, h };
	SDL_Point dst_p = { dst_x, dst_y };
	if (!gfx_copy_begin(src, &src_r, dst, &dst_p))
		return;

	indexed_foreach_row2(src_row, dst_row, src, &src_r, dst, &dst_p,
		memcpy(dst_row, src_row, src_r.w);
	);

	gfx_copy_end(src, dst);
}

static void gfx_direct_copy(int src_x, int src_y, int w, int h, SDL_Surface *src, int dst_x,
		int dst_y, SDL_Surface *dst)
{
	SDL_Rect src_r = { src_x, src_y, w, h };
	SDL_Rect dst_r = { dst_x, dst_y, w, h };
	SDL_CALL(SDL_BlitSurface, src, &src_r, dst, &dst_r);
}

void gfx_copy(int src_x, int src_y, int w, int h, unsigned src_i, int dst_x, int dst_y,
		unsigned dst_i)
{
	GFX_LOG("gfx_copy %u(%d,%d) -> %u(%d,%d) @ (%d,%d)",
			src_i, src_x, src_y, dst_i, dst_x, dst_y, w, h);
	SDL_Surface *src = gfx_get_surface(src_i);
	SDL_Surface *dst = gfx_get_surface(dst_i);
	if (game->bpp == 8)
		gfx_indexed_copy(src_x, src_y, w, h, src, dst_x, dst_y, dst);
	else
		gfx_direct_copy(src_x, src_y, w, h, src, dst_x, dst_y, dst);
	gfx_dirty(dst_i, dst_x, dst_y, w, h);
}

static void gfx_indexed_copy_masked(int src_x, int src_y, int w, int h, SDL_Surface *src,
		int dst_x, int dst_y, SDL_Surface *dst, uint8_t mask_color)
{
	SDL_Rect src_r = { src_x, src_y, w, h };
	SDL_Point dst_p = { dst_x, dst_y };
	if (!gfx_copy_begin(src, &src_r, dst, &dst_p))
		return;

	indexed_foreach_px2(src_px, dst_px, src, &src_r, dst, &dst_p,
		if (*src_px != mask_color)
			*dst_px = *src_px;
	);

	gfx_copy_end(src, dst);
}

static void gfx_direct_copy_masked(int src_x, int src_y, int w, int h, SDL_Surface *src,
		int dst_x, int dst_y, SDL_Surface *dst, uint32_t mask_color)
{
	SDL_Color mask = gfx_decode_direct(mask_color);
	SDL_Rect src_r = { src_x, src_y, w, h };
	SDL_Rect dst_r = { dst_x, dst_y, w, h };
	SDL_CALL(SDL_SetColorKey, src, SDL_TRUE, SDL_MapRGB(src->format, mask.r, mask.g, mask.b));
	SDL_CALL(SDL_BlitSurface, src, &src_r, dst, &dst_r);
	SDL_CALL(SDL_SetColorKey, src, SDL_FALSE, 0);
}

void gfx_copy_masked(int src_x, int src_y, int w, int h, unsigned src_i, int dst_x,
		int dst_y, unsigned dst_i, uint32_t mask_color)
{
	GFX_LOG("gfx_copy_masked[%u] %u(%d,%d) -> %u(%d,%d) @ (%d,%d)", mask_color,
			src_i, src_x, src_y, dst_i, dst_x, dst_y, w, h);
	SDL_Surface *src = gfx_get_surface(src_i);
	SDL_Surface *dst = gfx_get_surface(dst_i);
	if (game->bpp == 8)
		gfx_indexed_copy_masked(src_x, src_y, w, h, src, dst_x, dst_y, dst, mask_color);
	else
		gfx_direct_copy_masked(src_x, src_y, w, h, src, dst_x, dst_y, dst, mask_color);
	gfx_dirty(dst_i, dst_x, dst_y, w, h);
}

static void gfx_indexed_copy_swap(int src_x, int src_y, int w, int h, SDL_Surface *src,
		int dst_x, int dst_y, SDL_Surface *dst)
{
	SDL_Rect src_r = { src_x, src_y, w, h };
	SDL_Point dst_p = { dst_x, dst_y };
	if (!gfx_copy_begin(src, &src_r, dst, &dst_p))
		return;

	indexed_foreach_px2(src_px, dst_px, src, &src_r, dst, &dst_p,
		uint8_t tmp = *dst_px;
		*dst_px = *src_px;
		*src_px = tmp;
	);

	gfx_copy_end(src, dst);
}

static void gfx_direct_copy_swap(int src_x, int src_y, int w, int h, SDL_Surface *src,
		int dst_x, int dst_y, SDL_Surface *dst)
{
	SDL_Rect src_r = { src_x, src_y, w, h };
	SDL_Point dst_p = { dst_x, dst_y };
	if (!gfx_copy_begin(src, &src_r, dst, &dst_p))
		return;

	direct_foreach_px2(src_px, dst_px, src, &src_r, dst, &dst_p,
		uint8_t c[GFX_DIRECT_BPP / 8];
		memcpy(c, dst_px, GFX_DIRECT_BPP / 8);
		memcpy(dst_px, src_px, GFX_DIRECT_BPP / 8);
		memcpy(src_px, c, GFX_DIRECT_BPP / 8);
	);

	gfx_copy_end(src, dst);
}

void gfx_copy_swap(int src_x, int src_y, int w, int h, unsigned src_i, int dst_x,
		int dst_y, unsigned dst_i)
{
	GFX_LOG("gfx_copy_swap %u(%d,%d) -> %u(%d,%d) @ (%d,%d)",
			src_i, src_x, src_y, dst_i, dst_x, dst_y, w, h);
	SDL_Surface *src = gfx_get_surface(src_i);
	SDL_Surface *dst = gfx_get_surface(dst_i);
	if (game->bpp == 8)
		gfx_indexed_copy_swap(src_x, src_y, w, h, src, dst_x, dst_y, dst);
	else
		gfx_direct_copy_swap(src_x, src_y, w, h, src, dst_x, dst_y, dst);
	gfx_dirty(src_i, src_x, src_y, w, h);
	gfx_dirty(dst_i, dst_x, dst_y, w, h);
}

static void gfx_indexed_compose(int fg_x, int fg_y, int w, int h, SDL_Surface *fg, int bg_x,
		int bg_y, SDL_Surface *bg, int dst_x, int dst_y, SDL_Surface *dst,
		uint8_t mask_color)
{
	gfx_indexed_copy(bg_x, bg_y, w, h, bg, dst_x, dst_y, dst);
	gfx_indexed_copy_masked(fg_x, fg_y, w, h, fg, dst_x, dst_y, dst, mask_color);
}

static void gfx_direct_compose(int fg_x, int fg_y, int w, int h, SDL_Surface *fg, int bg_x,
		int bg_y, SDL_Surface *bg, int dst_x, int dst_y, SDL_Surface *dst,
		uint16_t mask_color)
{
	gfx_direct_copy(bg_x, bg_y, w, h, bg, dst_x, dst_y, dst);
	gfx_direct_copy_masked(fg_x, fg_y, w, h, fg, dst_x, dst_y, dst, mask_color);
}

void gfx_compose(int fg_x, int fg_y, int w, int h, unsigned fg_i, int bg_x, int bg_y,
		unsigned bg_i, int dst_x, int dst_y, unsigned dst_i, uint16_t mask_color)
{
	GFX_LOG("gfx_compose[%u] %u(%d,%d) + %u(%d,%d) -> %u(%d,%d) @ (%d,%d)", mask_color,
			fg_i, fg_x, fg_y, bg_i, bg_x, bg_y, dst_i, dst_x, dst_y, w, h);
	SDL_Surface *fg = gfx_get_surface(fg_i);
	SDL_Surface *bg = gfx_get_surface(bg_i);
	SDL_Surface *dst = gfx_get_surface(dst_i);
	if (game->bpp == 8)
		gfx_indexed_compose(fg_x, fg_y, w, h, fg, bg_x, bg_y, bg, dst_x, dst_y, dst,
				mask_color);
	else
		gfx_direct_compose(fg_x, fg_y, w, h, fg, bg_x, bg_y, bg, dst_x, dst_y, dst,
				mask_color);
	gfx_dirty(dst_i, dst_x, dst_y, w, h);
}

void gfx_blend(int src_x, int src_y, int w, int h, unsigned src_i, int dst_x, int dst_y,
		unsigned dst_i, uint8_t alpha)
{
	GFX_LOG("gfx_blend %u(%d,%d) -> %u(%d,%d) @ (%d,%d) @ %u",
			src_i, src_x, src_y, dst_i, dst_x, dst_y, w, h, alpha);
	if (game->bpp == 8)
		VM_ERROR("Invalid bpp for gfx_blend");

	SDL_Surface *src = gfx_get_surface(src_i);
	SDL_Surface *dst = gfx_get_surface(dst_i);
	SDL_Rect src_r = { src_x, src_y, w, h };
	SDL_Rect dst_r = { dst_x, dst_y, w, h };

	SDL_CALL(SDL_SetSurfaceBlendMode, src, SDL_BLENDMODE_BLEND);
	SDL_CALL(SDL_SetSurfaceAlphaMod, src, alpha);
	SDL_CALL(SDL_BlitSurface, src, &src_r, dst, &dst_r);
	SDL_CALL(SDL_SetSurfaceAlphaMod, src, 255);
	SDL_CALL(SDL_SetSurfaceBlendMode, src, SDL_BLENDMODE_NONE);

	gfx_dirty(dst_i, dst_x, dst_y, w, h);
}

_Static_assert(GFX_DIRECT_FORMAT == SDL_PIXELFORMAT_RGB24);
static void alpha_blend(uint8_t *bg, uint8_t *fg, uint8_t alpha)
{
	uint32_t a = (uint32_t)alpha + 1;
	uint32_t inv_a = 256 - (uint32_t)alpha;
	bg[0] = (uint8_t)((a * fg[0] + inv_a * bg[0]) >> 8);
	bg[1] = (uint8_t)((a * fg[1] + inv_a * bg[1]) >> 8);
	bg[2] = (uint8_t)((a * fg[2] + inv_a * bg[2]) >> 8);
}

void gfx_blend_masked(int src_x, int src_y, int w, int h, unsigned src_i, int dst_x,
		int dst_y, unsigned dst_i, uint8_t *mask)
{
	GFX_LOG("gfx_blend_masked %u(%d,%d) -> %u(%d,%d) @ (%d,%d)",
			src_i, src_x, src_y, dst_i, dst_x, dst_y, w, h);
	if (game->bpp == 8)
		VM_ERROR("Invalid bpp for gfx_blend_masked");
	SDL_Surface *src = gfx_get_surface(src_i);
	SDL_Surface *dst = gfx_get_surface(dst_i);
	SDL_Rect src_r = { src_x, src_y, w, h };
	SDL_Point dst_p = { dst_x, dst_y };
	// FIXME: mask pointer not adjusted here
	if (!gfx_copy_begin(src, &src_r, dst, &dst_p))
		return;

	direct_foreach_px2(src_px, dst_px, src, &src_r, dst, &dst_p,
		if (*mask == 0) {
			// nothing
		} else if (*mask > 15) {
			memcpy(dst_px, src_px, GFX_DIRECT_BPP / 8);
		} else {
			alpha_blend(dst_px, src_px, *mask * 16 - 8);
		}
		mask++;
	);

	gfx_dirty(dst_i, dst_x, dst_y, w, h);
}

void gfx_invert_colors(int x, int y, int w, int h, unsigned i)
{
	GFX_LOG("gfx_invert_colors %u(%d,%d) @ (%d,%d)", i, x, y, w, h);
	if (game->bpp != 8)
		VM_ERROR("Invalid bpp for gfx_invert_colors");
	SDL_Surface *s = gfx_get_surface(i);
	SDL_Rect r = { x, y, w, h };
	if (!gfx_fill_begin(s, &r))
		return;

	indexed_foreach_px(p, s, &r,
		*p ^= 0xf;
	);

	gfx_fill_end(s);
	gfx_dirty(i, x, y, w, h);
}

static void gfx_indexed_fill(int x, int y, int w, int h, SDL_Surface *dst, uint8_t c)
{
	SDL_Rect rect = { x, y, w, h };
	SDL_CALL(SDL_FillRect, dst, &rect, c);
}

static void gfx_direct_fill(int x, int y, int w, int h, SDL_Surface *dst, uint32_t color)
{
	SDL_Color c = gfx_decode_direct(color);
	SDL_Rect rect = { x, y, w, h };
	SDL_CALL(SDL_FillRect, dst, &rect, SDL_MapRGB(dst->format, c.r, c.g, c.b));
}

void gfx_fill(int x, int y, int w, int h, unsigned i, uint32_t c)
{
	GFX_LOG("gfx_fill[%u] %u(%d,%d) @ (%d,%d)", c, i, x, y, w, h);
	SDL_Surface *dst = gfx_get_surface(i);
	if (game->bpp == 8)
		gfx_indexed_fill(x, y, w, h, dst, c);
	else
		gfx_direct_fill(x, y, w, h, dst, c);
	gfx_dirty(i, x, y, w, h);
}

static void gfx_indexed_swap_colors(SDL_Rect r, SDL_Surface *dst, uint8_t c1,
		uint8_t c2)
{
	if (!gfx_fill_begin(dst, &r))
		return;

	indexed_foreach_px(p, dst, &r,
		if (*p == c1)
			*p = c2;
		else if (*p == c2)
			*p = c1;
	);

	gfx_fill_end(dst);
}

// XXX: we assume pixel format is RGB24
//      this must change if alpha channel is needed in the future
_Static_assert(GFX_DIRECT_FORMAT == SDL_PIXELFORMAT_RGB24);
static uint32_t gfx_direct_get_pixel(uint8_t *p)
{
	if (SDL_BYTEORDER == SDL_BIG_ENDIAN)
		return (p[0] << 16) | (p[1] << 8) || p[2];
	else
		return p[0] | (p[1] << 8) | (p[2] << 16);
}

static void gfx_direct_set_pixel(uint8_t *dst, uint32_t c)
{
	dst[0] = c & 0xff;
	dst[1] = (c >> 8) & 0xff;
	dst[2] = (c >> 16) & 0xff;
}

static void gfx_direct_swap_colors(SDL_Rect r, SDL_Surface *dst, uint32_t _c1,
		uint32_t _c2)
{
	if (!gfx_fill_begin(dst, &r))
		return;

	// transcode color to RGB24
	SDL_Color color1 = gfx_decode_direct(_c1);
	SDL_Color color2 = gfx_decode_direct(_c2);
	uint32_t c1 = color1.r | (color1.g << 8) | (color1.b << 16);
	uint32_t c2 = color2.r | (color2.g << 8) | (color2.b << 16);

	direct_foreach_px(p, dst, &r,
		uint32_t c = gfx_direct_get_pixel(p);
		if (c == c2)
			gfx_direct_set_pixel(p, c1);
		else
			gfx_direct_set_pixel(p, c2);
	);

	gfx_fill_end(dst);
}

void gfx_swap_colors(int x, int y, int w, int h, unsigned i, uint32_t c1, uint32_t c2)
{
	GFX_LOG("gfx_swap_colors[%u,%u] %u(%d,%d) @ (%d,%d)", c1, c2, i, x, y, w, h);
	SDL_Surface *s = gfx_get_surface(i);
	SDL_Rect r = { x, y, w, h };
	if (game->bpp == 8)
		gfx_indexed_swap_colors(r, s, c1, c2);
	else
		gfx_direct_swap_colors(r, s, c1, c2);
	gfx_dirty(i, x, y, w, h);
}

void gfx_blend_fill(int x, int y, int w, int h, unsigned i, uint32_t c, uint8_t rate)
{
	GFX_LOG("gfx_blend_fill[%u,%u] %u(%d,%d) @ (%d,%d)", c, rate, i, x, y, w, h);
	SDL_Surface *s = gfx_get_surface(i);
	SDL_Rect r = { x, y, w, h };
	if (game->bpp == 8)
		VM_ERROR("Invalid bpp for gfx_blend_fill");

	if (!gfx_fill_begin(s, &r))
		return;

	uint32_t a = (uint32_t)rate + 1;
	uint32_t inv_a = 256 - (uint32_t)rate;
	SDL_Color color = gfx_decode_direct(c);
	color.r *= a;
	color.g *= a;
	color.b *= a;
	direct_foreach_px(p, s, &r,
		p[0] = (uint8_t)((color.r + inv_a * p[0]) >> 8);
		p[1] = (uint8_t)((color.g + inv_a * p[1]) >> 8);
		p[2] = (uint8_t)((color.b + inv_a * p[2]) >> 8);
	);

	gfx_fill_end(s);
	gfx_dirty(i, x, y, w, h);
}

static void gfx_indexed_draw_cg(SDL_Surface *s, struct cg *cg)
{
	unsigned screen_w = s->w;
	attr_unused unsigned screen_h = s->h;
	unsigned cg_x = cg->metrics.x;
	unsigned cg_y = cg->metrics.y;
	unsigned cg_w = cg->metrics.w;
	unsigned cg_h = cg->metrics.h;
	assert(cg_x + cg_w <= screen_w);
	assert(cg_y + cg_h <= screen_h);

	uint8_t *base = s->pixels + cg_y * screen_w + cg_x;
	for (int row = 0; row < cg_h; row++) {
		uint8_t *dst = base + row * s->pitch;
		uint8_t *src = cg->pixels + row * cg_w;
		memcpy(dst, src, cg_w);
	}
}

static void gfx_direct_draw_cg(SDL_Surface *dst, struct cg *cg)
{
	SDL_Surface *src;
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormatFrom, src, cg->pixels, cg->metrics.w,
			cg->metrics.h, 32, cg->metrics.w * 4, SDL_PIXELFORMAT_RGBA32);
	SDL_Rect dst_r = { cg->metrics.x, cg->metrics.y, cg->metrics.w, cg->metrics.h };
	SDL_CALL(SDL_BlitSurface, src, NULL, dst, &dst_r);
	SDL_FreeSurface(src);
}

void gfx_draw_cg(unsigned i, struct cg *cg)
{
	GFX_LOG("gfx_draw_cg[%u] (%u,%u,%u,%u)", i, cg->metrics.x, cg->metrics.y,
			cg->metrics.w, cg->metrics.h);
	SDL_Surface *s = gfx_get_surface(i);
	if (SDL_MUSTLOCK(s))
		SDL_CALL(SDL_LockSurface, s);

	if (game->bpp == 8) {
		if (!cg->palette)
			VM_ERROR("Tried to draw direct-color CG to indexed surface");
		gfx_indexed_draw_cg(s, cg);
	} else {
		if (cg->palette) {
			// FIXME: depalettize?
			VM_ERROR("Tried to draw indexed CG to direct-color surface");
		}
		gfx_direct_draw_cg(s, cg);
	}

	if (SDL_MUSTLOCK(s))
		SDL_UnlockSurface(s);

	gfx_dirty(i, cg->metrics.x, cg->metrics.y, cg->metrics.w, cg->metrics.h);
}
