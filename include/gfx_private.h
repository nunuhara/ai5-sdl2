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

#ifndef AI5_GFX_PRIVATE_H
#define AI5_GFX_PRIVATE_H

#include <SDL.h>

#include "gfx.h"

#define SDL_CALL(f, ...) if (f(__VA_ARGS__) < 0) { ERROR(#f ": %s", SDL_GetError()); }
#define SDL_CTOR(f, dst, ...) if (!(dst = f(__VA_ARGS__))) { ERROR(#f ": %s", SDL_GetError()); }

#if 0
#define GFX_LOG(...) NOTICE(__VA_ARGS__)
#else
#define GFX_LOG(...)
#endif

#define GFX_INDEXED_BPP 8
#define GFX_INDEXED_FORMAT SDL_PIXELFORMAT_INDEX8

#define GFX_DIRECT_BPP 24
#define GFX_DIRECT_FORMAT SDL_PIXELFORMAT_RGB24

struct gfx_surface {
	SDL_Surface *s;
	SDL_Rect src;   // source rectangle for BlitScaled
	SDL_Rect dst;   // destination rectangle for BlitScaled
	bool scaled;    // if true, `src` and `rect` differ
	bool dirty;
	SDL_Rect damaged;
};

struct gfx {
	SDL_Window *window;
	SDL_Renderer *renderer;
	uint32_t window_id;

	struct gfx_surface surface[GFX_NR_SURFACES];

	// index of the currently displayed surface
	unsigned screen;

	// XXX: we need a non-indexed surface because textures can't be created
	//      directly from indexed surfaces (...why?)
	SDL_Surface *display;
	SDL_Surface *scaled_display;
	SDL_Surface *overlay;
	SDL_Texture *texture;
	SDL_Color palette[256];
	struct {
		uint32_t bg;
		uint32_t fg;
		SDL_Color bg_color;
		SDL_Color fg_color;
		unsigned size;
	} text;
	bool hidden;
};
extern struct gfx gfx;

SDL_Surface *gfx_get_surface(unsigned i);
SDL_Surface *gfx_get_overlay(void);
void _gfx_update_palette(int start, int n);
void gfx_update_palette(int start, int n);
bool gfx_fill_clip(SDL_Surface *s, SDL_Rect *r);
bool gfx_copy_clip(SDL_Surface *src, SDL_Rect *src_r, SDL_Surface *dst, SDL_Point *dst_p);
void ui_draw_text(SDL_Surface *s, int x, int y, const char *text, SDL_Color color);
int ui_measure_text(const char *text);
SDL_Surface *icon_get(unsigned no);

void gfx_dump_surface(unsigned i, const char *filename);

static inline SDL_Surface *gfx_lock_surface(unsigned i)
{
	SDL_Surface *s = gfx_get_surface(i);
	if (SDL_MUSTLOCK(s))
		SDL_CALL(SDL_LockSurface, s);
	return s;
}

static inline void gfx_unlock_surface(SDL_Surface *s)
{
	if (SDL_MUSTLOCK(s))
		SDL_UnlockSurface(s);
}

static inline SDL_Color gfx_decode_bgr555(uint16_t c)
{
	return (SDL_Color) {
		.r = (c & 0x7c00) >> 7,
		.g = (c & 0x03e0) >> 2,
		.b = (c & 0x001f) << 3,
		.a = 255
	};
}

static inline SDL_Color gfx_decode_bgr(uint32_t c)
{
	return (SDL_Color) {
		.r = c & 0xff,
		.g = (c >> 8) & 0xff,
		.b = (c >> 16) & 0xff,
	};
}

#endif // AI5_GFX_PRIVATE_H
