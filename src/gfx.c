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

static bool copy_clip(SDL_Surface *src, SDL_Rect *src_r, SDL_Surface *dst, SDL_Point *dst_p)
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

static void foreach_pixel2(SDL_Surface *src, SDL_Rect *src_r, SDL_Surface *dst,
		SDL_Point *dst_p, void (*f)(uint8_t*,uint8_t*,void*), void *data)
{
	if (!copy_clip(src, src_r, dst, dst_p))
		return;

	if (SDL_MUSTLOCK(src))
		SDL_CALL(SDL_LockSurface, src);
	if (SDL_MUSTLOCK(dst))
		SDL_CALL(SDL_LockSurface, dst);

	uint8_t *src_base = src->pixels + src_r->y * src->pitch + src_r->x;
	uint8_t *dst_base = dst->pixels + dst_p->y * dst->pitch + dst_p->x;
	for (int row = 0; row < src_r->h; row++) {
		uint8_t *src_px = src_base + row * src->pitch;
		uint8_t *dst_px = dst_base + row * dst->pitch;
		for (int col = 0; col < src_r->w; col++, src_px++, dst_px++) {
			f(src_px, dst_px, data);
		}
	}

	if (SDL_MUSTLOCK(src))
		SDL_UnlockSurface(src);
	if (SDL_MUSTLOCK(dst))
		SDL_UnlockSurface(dst);
}

static void gfx_copy_cb(uint8_t *src, uint8_t *dst, void *_)
{
	*dst = *src;
}

void gfx_copy(int src_x, int src_y, int src_w, int src_h, unsigned src_i, int dst_x,
		int dst_y, unsigned dst_i)
{
	assert(src_i < GFX_NR_SURFACES);
	assert(dst_i < GFX_NR_SURFACES);
	SDL_Surface *src = gfx.surface[src_i];
	SDL_Surface *dst = gfx.surface[dst_i];
	SDL_Rect src_r = { src_x, src_y, src_w, src_h };
	SDL_Point dst_p = { dst_x, dst_y };
	foreach_pixel2(src, &src_r, dst, &dst_p, gfx_copy_cb, NULL);
	gfx.dirty = true;
}

static void gfx_copy_masked_cb(uint8_t *src, uint8_t *dst, void *_mask)
{
	uint8_t mask = (uintptr_t)_mask;
	if (*src != mask)
		*dst = *src;
}

void gfx_copy_masked(int src_x, int src_y, int src_w, int src_h, unsigned src_i, int dst_x,
		int dst_y, unsigned dst_i, uint8_t mask_color)
{
	assert(src_i < GFX_NR_SURFACES);
	assert(dst_i < GFX_NR_SURFACES);
	SDL_Surface *src = gfx.surface[src_i];
	SDL_Surface *dst = gfx.surface[dst_i];
	SDL_Rect src_r = { src_x, src_y, src_w, src_h };
	SDL_Point dst_p = { dst_x, dst_y };
	foreach_pixel2(src, &src_r, dst, &dst_p, gfx_copy_masked_cb, (void*)(uintptr_t)mask_color);
	gfx.dirty = true;
}

static void gfx_copy_swap_cb(uint8_t *src, uint8_t *dst, void *_)
{
	uint8_t tmp = *dst;
	*dst = *src;
	*src = tmp;
}

void gfx_copy_swap(int src_x, int src_y, int src_w, int src_h, unsigned src_i, int dst_x,
		int dst_y, unsigned dst_i)
{
	assert(src_i < GFX_NR_SURFACES);
	assert(dst_i < GFX_NR_SURFACES);
	SDL_Surface *src = gfx.surface[src_i];
	SDL_Surface *dst = gfx.surface[dst_i];
	SDL_Rect src_r = { src_x, src_y, src_w, src_h };
	SDL_Point dst_p = { dst_x, dst_y };
	foreach_pixel2(src, &src_r, dst, &dst_p, gfx_copy_swap_cb, NULL);
	gfx.dirty = true;
}

void gfx_copy_sprite_bg(int fg_x, int fg_y, int w, int h, unsigned fg_i, int bg_x, int bg_y,
		unsigned bg_i, int dst_x, int dst_y, unsigned dst_i, uint8_t mask_color)
{
	assert(fg_i < GFX_NR_SURFACES);
	assert(bg_i < GFX_NR_SURFACES);
	assert(dst_i < GFX_NR_SURFACES);
	SDL_Surface *fg = gfx.surface[fg_i];
	SDL_Surface *bg = gfx.surface[bg_i];
	SDL_Surface *dst = gfx.surface[dst_i];
	SDL_Rect fg_r = { fg_x, fg_y, w, h };
	SDL_Rect bg_r = { bg_x, bg_y, w, h };
	SDL_Point dst_p = { dst_x, dst_y };
	foreach_pixel2(bg, &bg_r, dst, &dst_p, gfx_copy_cb, NULL);
	foreach_pixel2(fg, &fg_r, dst, &dst_p, gfx_copy_masked_cb, (void*)(uintptr_t)mask_color);
	gfx.dirty = true;
}

/*
 * XXX: AI5WIN.EXE doesn't clip. If the rectangle exceeds the bounds of the destination
 *      surface, it just writes to buggy addresses.
 */
static bool fill_clip(SDL_Surface *s, SDL_Rect *r)
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

static void foreach_pixel(SDL_Surface *s, SDL_Rect *r, void (*f)(uint8_t*,void*), void *data)
{
	if (!fill_clip(s, r))
		return;

	if (SDL_MUSTLOCK(s))
		SDL_CALL(SDL_LockSurface, s);

	uint8_t *base = s->pixels + r->y * s->pitch + r->x;
	for (int row = 0; row < r->h; row++) {
		uint8_t *p = base + row * s->pitch;
		for (int col = 0; col < r->w; col++, p++) {
			f(p, data);
		}
	}

	if (SDL_MUSTLOCK(s))
		SDL_UnlockSurface(s);
}

static void gfx_invert_colors_cb(uint8_t *p, void *_)
{
	*p ^= 0xf;
}

void gfx_invert_colors(int x, int y, int w, int h, unsigned i)
{
	assert(i < GFX_NR_SURFACES);
	SDL_Surface *s = gfx.surface[i];
	SDL_Rect r = { x, y, w, h };
	foreach_pixel(s, &r, gfx_invert_colors_cb, NULL);
	gfx.dirty = true;
}

void gfx_fill(int x, int y, int w, int h, uint8_t c)
{
	SDL_Rect rect = { x, y, w, h };
	SDL_CALL(SDL_FillRect, gfx_dst_surface(), &rect, c);
	gfx.dirty = true;
}

struct swap_colors_data {
	uint8_t c1, c2;
};

void gfx_swap_colors_cb(uint8_t *p, void *data)
{
	struct swap_colors_data *colors = data;
	if (*p == colors->c1)
		*p = colors->c2;
	else if (*p == colors->c2)
		*p = colors->c1;
}

void gfx_swap_colors(int x, int y, int w, int h, uint8_t c1, uint8_t c2)
{
	SDL_Surface *s = gfx_dst_surface();
	SDL_Rect r = { x, y, w, h };
	struct swap_colors_data colors = { c1, c2 };
	foreach_pixel(s, &r, gfx_swap_colors_cb, &colors);
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
