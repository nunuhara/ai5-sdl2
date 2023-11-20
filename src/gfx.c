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
#include "gfx.h"

struct gfx gfx = { .dirty = true };
struct gfx_view gfx_view = { 640, 400 };

void gfx_dirty(void)
{
	gfx.dirty = true;
}

static void _gfx_set_window_size(unsigned w, unsigned h)
{

	gfx_view.w = w;
	gfx_view.h = h;

	SDL_CALL(SDL_RenderSetLogicalSize, gfx.renderer, w, h);
	SDL_FreeSurface(gfx.indexed);
	SDL_FreeSurface(gfx.display);
	SDL_DestroyTexture(gfx.texture);
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, gfx.indexed, 0, w, h, 8, SDL_PIXELFORMAT_INDEX8);
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, gfx.display, 0, w, h, 32, SDL_PIXELFORMAT_RGB888);
	SDL_CTOR(SDL_CreateTexture, gfx.texture, gfx.renderer, gfx.display->format->format,
			SDL_TEXTUREACCESS_STATIC, w, h);
	SDL_CALL(SDL_FillRect, gfx.indexed, NULL, 0);
	SDL_CALL(SDL_FillRect, gfx.display, NULL, SDL_MapRGB(gfx.display->format, 0, 0, 0));
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
		SDL_FreeSurface(gfx.indexed);
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
	SDL_CALL(SDL_BlitSurface, gfx.indexed, &rect, gfx.display, &rect);
	SDL_CALL(SDL_UpdateTexture, gfx.texture, NULL, gfx.display->pixels, gfx.display->pitch);
	SDL_CALL(SDL_RenderClear, gfx.renderer);
	SDL_CALL(SDL_RenderCopy, gfx.renderer, gfx.texture, NULL, NULL);
	SDL_RenderPresent(gfx.renderer);
	gfx.dirty = false;
}

void gfx_set_palette(const uint8_t *data)
{
	SDL_Color colors[256];
	for (int i = 0; i < 256; i++) {
		colors[i].b = data[i*4+0];
		colors[i].g = data[i*4+1];
		colors[i].r = data[i*4+2];
		colors[i].a = 255;
	}
	SDL_CALL(SDL_SetPaletteColors, gfx.indexed->format->palette, colors, 0, 256);
	gfx.dirty = true;
}

void gfx_fill(unsigned tl_x, unsigned tl_y, unsigned br_x, unsigned br_y, uint8_t c)
{
	SDL_Rect rect = { tl_x, tl_y, br_x - tl_x, br_y - tl_y };
	SDL_CALL(SDL_FillRect, gfx.indexed, &rect, c);
	gfx.dirty = true;
}

void gfx_draw_cg(struct cg *cg)
{
	if (SDL_MUSTLOCK(gfx.indexed))
		SDL_CALL(SDL_LockSurface, gfx.indexed);

	unsigned screen_w = gfx.indexed->w;
	unsigned screen_h = gfx.indexed->h;
	unsigned cg_x = cg->metrics.x;
	unsigned cg_y = cg->metrics.y;
	unsigned cg_w = cg->metrics.w;
	unsigned cg_h = cg->metrics.h;
	assert(cg_x + cg_w <= screen_w);
	assert(cg_y + cg_h <= screen_h);

	uint8_t *base = gfx.indexed->pixels + cg_y * screen_w + cg_x;
	for (int row = 0; row < cg_h; row++) {
		uint8_t *dst = base + row * screen_w;
		uint8_t *src = cg->pixels + row * cg_w;
		memcpy(dst, src, cg_w);
	}

	if (SDL_MUSTLOCK(gfx.indexed))
		SDL_UnlockSurface(gfx.indexed);

	gfx.dirty = true;
}
