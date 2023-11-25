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
#include <SDL.h>

#include "nulib.h"
#include "ai5/cg.h"

#include "gfx_private.h"
#include "memory.h"
#include "vm.h"

struct gfx gfx = { .dirty = true };
struct gfx_view gfx_view = { 640, 400 };

void gfx_dirty(void)
{
	gfx.dirty = true;
}

// FIXME: AI5WIN.EXE can access 5 surfaces without crashing, but only
//        surfaces 0 and 1 behave as expected... surfaces 2-3 seem to
//        have the same behavior, and surface 4 is different again.
SDL_Surface *gfx_dst_surface(void)
{
	unsigned i = memory_system_var16()[1];
	if (i >= GFX_NR_SURFACES) {
		WARNING("Invalid destination surface index: %u", i);
		i = 0;
	}
	return gfx.surface[i];
}

static SDL_Surface *gfx_screen(void)
{
	return gfx.surface[gfx.screen];
}

static void _gfx_set_window_size(unsigned w, unsigned h)
{

	gfx_view.w = w;
	gfx_view.h = h;

	SDL_CALL(SDL_RenderSetLogicalSize, gfx.renderer, w, h);

	// free old surfaces/texture
	for (int i = 0; i < GFX_NR_SURFACES; i++) {
		SDL_FreeSurface(gfx.surface[i]);
	}
	SDL_FreeSurface(gfx.display);
	SDL_DestroyTexture(gfx.texture);

	// recreate and initialize surfaces/texture
	for (int i = 0; i < GFX_NR_SURFACES; i++) {
		SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, gfx.surface[i], 0, w, h, 8,
				SDL_PIXELFORMAT_INDEX8);
		SDL_CALL(SDL_FillRect, gfx.surface[i], NULL, 0);
	}

	SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, gfx.display, 0, w, h, 32, SDL_PIXELFORMAT_RGB888);
	SDL_CALL(SDL_FillRect, gfx.display, NULL, SDL_MapRGB(gfx.display->format, 0, 0, 0));

	SDL_CTOR(SDL_CreateTexture, gfx.texture, gfx.renderer, gfx.display->format->format,
			SDL_TEXTUREACCESS_STATIC, w, h);
}

void gfx_set_window_size(unsigned w, unsigned h)
{
	if (w == gfx_view.w && h == gfx_view.h)
		return;
	_gfx_set_window_size(w, h);
}

static void gfx_fini(void)
{
	if (gfx.display) {
		for (int i = 0; i < GFX_NR_SURFACES; i++) {
			SDL_FreeSurface(gfx.surface[i]);
		}
		SDL_FreeSurface(gfx.display);
		SDL_DestroyTexture(gfx.texture);
		SDL_DestroyRenderer(gfx.renderer);
		SDL_DestroyWindow(gfx.window);
		SDL_Quit();
	}
	gfx = (struct gfx){ .dirty = true };
}

void gfx_init(void)
{
	SDL_CALL(SDL_Init, SDL_INIT_VIDEO | SDL_INIT_TIMER);
	SDL_CTOR(SDL_CreateWindow, gfx.window, "ai5-sdl2",
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, gfx_view.w, gfx_view.h,
			SDL_WINDOW_RESIZABLE);
	SDL_CTOR(SDL_CreateRenderer, gfx.renderer, gfx.window, -1, 0);
	SDL_CALL(SDL_SetRenderDrawColor, gfx.renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
	_gfx_set_window_size(gfx_view.w, gfx_view.h);
	gfx_text_init();
	atexit(gfx_fini);
}

void gfx_update(void)
{
	if (!gfx.dirty)
		return;
	SDL_Rect rect = { 0, 0, gfx_view.w, gfx_view.h };
	SDL_CALL(SDL_BlitSurface, gfx_screen(), &rect, gfx.display, &rect);
	SDL_CALL(SDL_UpdateTexture, gfx.texture, NULL, gfx.display->pixels, gfx.display->pitch);
	SDL_CALL(SDL_RenderClear, gfx.renderer);
	SDL_CALL(SDL_RenderCopy, gfx.renderer, gfx.texture, NULL, NULL);
	SDL_RenderPresent(gfx.renderer);
	gfx.dirty = false;
}

static SDL_Color palette[256];

static void read_palette(SDL_Color *dst, const uint8_t *src)
{
	for (int i = 0; i < 256; i++) {
		dst[i].b = src[i*4+0];
		dst[i].g = src[i*4+1];
		dst[i].r = src[i*4+2];
		dst[i].a = 255;
	}
}

static void update_palette(void)
{
	SDL_CALL(SDL_SetPaletteColors, gfx_screen()->format->palette, palette, 0, 256);
	gfx.dirty = true;
}

void gfx_palette_set(const uint8_t *data)
{
	read_palette(palette, data);
	update_palette();
}

static uint8_t u8_interp(uint8_t a, uint8_t b, float rate)
{
	int d = b - a;
	return a + d * rate;
}

static void _gfx_palette_crossfade(SDL_Color *new, unsigned ms)
{
	SDL_Color old[256];
	memcpy(old, palette, sizeof(old));

	// get a list of palette indices that need to be interpolated
	uint8_t fading[256];
	int nr_fading = 0;
	for (int i = 0; i < 256; i++) {
		if (old[i].r != new[i].r || old[i].g != new[i].g || old[i].b != new[i].b)
			fading[nr_fading++] = i;
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
			palette[c].r = u8_interp(old[c].r, new[c].r, rate);
			palette[c].g = u8_interp(old[c].g, new[c].g, rate);
			palette[c].b = u8_interp(old[c].b, new[c].b, rate);
		}
		update_palette();

		// update
		gfx_update();
		SDL_Delay(16);
		t = vm_get_ticks() - start_t;
	}
}

void gfx_palette_crossfade(const uint8_t *data, unsigned ms)
{
	SDL_Color new[256];
	read_palette(new, data);
	_gfx_palette_crossfade(new, ms);
}

void gfx_palette_crossfade_to(uint8_t r, uint8_t g, uint8_t b, unsigned ms)
{
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
	assert(i < GFX_NR_SURFACES);
	gfx.screen = i;
	update_palette();
}

void gfx_fill(unsigned tl_x, unsigned tl_y, unsigned br_x, unsigned br_y, uint8_t c)
{
	SDL_Rect rect = { tl_x, tl_y, br_x - tl_x, br_y - tl_y };
	SDL_CALL(SDL_FillRect, gfx_dst_surface(), &rect, c);
	gfx.dirty = true;
}

void gfx_swap_colors(unsigned tl_x, unsigned tl_y, unsigned br_x, unsigned br_y,
		uint8_t c1, uint8_t c2)
{
	SDL_Surface *s = gfx_dst_surface();
	if (SDL_MUSTLOCK(s))
		SDL_CALL(SDL_LockSurface, s);

	unsigned screen_w = s->w;
	unsigned screen_h = s->h;
	unsigned x = tl_x;
	unsigned y = tl_y;
	unsigned w = br_x - tl_x;
	unsigned h = br_y - tl_y;
	assert(x + w <= screen_w);
	assert(y + h <= screen_h);

	uint8_t *base = s->pixels + y * screen_w + x;
	for (int row = 0; row < h; row++) {
		uint8_t *p = base + row * screen_w;
		for (int col = 0; col < w; col++) {
			if (*p == c1)
				*p = c2;
			else if (*p == c2)
				*p = c1;
			p++;
		}
	}

	if (SDL_MUSTLOCK(s))
		SDL_UnlockSurface(s);

	gfx.dirty = true;
}

void gfx_draw_cg(struct cg *cg)
{
	SDL_Surface *s = gfx_dst_surface();
	if (SDL_MUSTLOCK(s))
		SDL_CALL(SDL_LockSurface, s);

	unsigned screen_w = s->w;
	unsigned screen_h = s->h;
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

	if (SDL_MUSTLOCK(s))
		SDL_UnlockSurface(s);

	gfx.dirty = true;
}
