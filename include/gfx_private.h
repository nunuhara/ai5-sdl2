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
	SDL_Texture *texture;
	SDL_Color palette[256];
	struct {
		uint32_t bg;
		uint32_t fg;
		SDL_Color bg_color;
		SDL_Color fg_color;
	} text;
	bool dirty;
	bool hidden;
};
extern struct gfx gfx;

SDL_Surface *gfx_get_surface(unsigned i);
void gfx_update_palette(void);
bool gfx_fill_clip(SDL_Surface *s, SDL_Rect *r);
bool gfx_copy_clip(SDL_Surface *src, SDL_Rect *src_r, SDL_Surface *dst, SDL_Point *dst_p);

void gfx_dump_surface(unsigned i, const char *filename);

static inline SDL_Color gfx_decode_bgr555(uint16_t c)
{
	return (SDL_Color) {
		.r = (c & 0x7c00) >> 7,
		.g = (c & 0x03e0) >> 2,
		.b = (c & 0x001f) << 3,
		.a = 255
	};
}

#endif // AI5_GFX_PRIVATE_H
