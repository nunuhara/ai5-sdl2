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

struct gfx {
	SDL_Window *window;
	SDL_Renderer *renderer;

	SDL_Surface *surface[GFX_NR_SURFACES];

	// index of the currently displayed surface
	unsigned screen;

	// XXX: we need a non-indexed surface because textures can't be created
	//      directly from indexed surfaces (...why?)
	SDL_Surface *display;
	SDL_Texture *texture;
	struct {
		uint8_t bg;
		uint8_t fg;
	} text;
	bool dirty;
	bool hidden;
};
extern struct gfx gfx;

SDL_Surface *gfx_get_surface(unsigned i);
bool gfx_fill_clip(SDL_Surface *s, SDL_Rect *r);
bool gfx_copy_clip(SDL_Surface *src, SDL_Rect *src_r, SDL_Surface *dst, SDL_Point *dst_p);

#endif // AI5_GFX_PRIVATE_H
