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

#define SDL_CALL(f, ...) if (f(__VA_ARGS__) < 0) { ERROR(#f ": %s", SDL_GetError()); }
#define SDL_CTOR(f, dst, ...) if (!(dst = f(__VA_ARGS__))) { ERROR(#f ": %s", SDL_GetError()); }

struct gfx {
	SDL_Window *window;
	SDL_Renderer *renderer;
	// XXX: 'indexed' is needed because indexed pixel formats can't be used
	//      to create textures (...why?)
	SDL_Surface *indexed;
	SDL_Surface *display;
	SDL_Texture *texture;
	struct {
		uint8_t bg;
		uint8_t fg;
	} text;
	bool dirty;
};
extern struct gfx gfx;


#endif // AI5_GFX_PRIVATE_H
