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
#include "vm.h"

#define X 0xff
#define _ 0x00

#define FADE_PATTERN_SIZE 4
#define FADE_SIZE (sizeof(fade_pattern_vert)/FADE_PATTERN_SIZE)

#define F1A X,X,X,_
#define F1B X,X,X,X
#define F1C X,_,X,X
#define F1D X,X,X,X
#define FADE_1 F1A,F1B,F1C,F1D

#define F2A X,X,X,_
#define F2B X,X,X,X
#define F2C X,_,X,_
#define F2D X,X,X,X
#define FADE_2 F2A,F2B,F2C,F2D

#define F3A X,_,X,_
#define F3B X,X,X,X
#define F3C X,_,X,_
#define F3D X,X,X,X
#define FADE_3 F3A,F3B,F3C,F3D

#define F4A X,_,X,_
#define F4B X,X,X,X
#define F4C X,_,X,_
#define F4D _,X,X,X
#define FADE_4 F4A,F4B,F4C,F4D

#define F5A X,_,X,_
#define F5B X,X,X,X
#define F5C X,_,X,_
#define F5D _,X,_,X
#define FADE_5 F5A,F5B,F5C,F5D

#define F6A X,_,X,_
#define F6B _,X,_,X
#define F6C X,_,X,_
#define F6D _,X,_,X
#define FADE_6 F6A,F6B,F6C,F6D

#define F7A X,_,X,_
#define F7B _,X,_,X
#define F7C X,_,X,_
#define F7D _,_,_,X
#define FADE_7 F7A,F7B,F7C,F7D

#define F8A X,_,X,_
#define F8B _,X,_,_
#define F8C X,_,X,_
#define F8D _,_,_,_
#define FADE_8 F8A,F8B,F8C,F8D

#define F9A X,_,X,_
#define F9B _,_,_,_
#define F9C X,_,X,_
#define F9D _,_,_,_
#define FADE_9 F9A,F9B,F9C,F9D

#define F10A X,_,_,_
#define F10B _,_,_,_
#define F10C X,_,X,_
#define F10D _,_,_,_
#define FADE_10 F10A,F10B,F10C,F10D

#define F11A X,_,_,_
#define F11B _,_,_,_
#define F11C _,_,X,_
#define F11D _,_,_,_
#define FADE_11 F11A,F11B,F11C,F11D

#define F12A X,_,_,_
#define F12B _,_,_,_
#define F12C _,_,_,_
#define F12D _,_,_,_
#define FADE_12 F12A,F12B,F12C,F12D

#define R4(x) x, x, x, x
#define R8(x) x, x, x, x, x, x, x, x

static const uint8_t fade_pattern_hori[] = {
	R4(F1A),R4(F2A),R8(F3A),R4(F4A),R4(F5A),R8(F6A),R4(F7A),R4(F8A),R4(F9A),R4(F10A),R4(F11A),R4(F12A),
	R4(F1B),R4(F2B),R8(F3B),R4(F4B),R4(F5B),R8(F6B),R4(F7B),R4(F8B),R4(F9B),R4(F10B),R4(F11B),R4(F12B),
	R4(F1C),R4(F2C),R8(F3C),R4(F4C),R4(F5C),R8(F6C),R4(F7C),R4(F8C),R4(F9C),R4(F10C),R4(F11C),R4(F12C),
	R4(F1D),R4(F2D),R8(F3D),R4(F4D),R4(F5D),R8(F6D),R4(F7D),R4(F8D),R4(F9D),R4(F10D),R4(F11D),R4(F12D),
};

static const uint8_t fade_pattern_vert[] = {
	FADE_1,  FADE_1,  FADE_1,  FADE_1,
	FADE_2,  FADE_2,  FADE_2,  FADE_2,
	FADE_3,  FADE_3,  FADE_3,  FADE_3,
	FADE_3,  FADE_3,  FADE_3,  FADE_3,
	FADE_4,  FADE_4,  FADE_4,  FADE_4,
	FADE_5,  FADE_5,  FADE_5,  FADE_5,
	FADE_6,  FADE_6,  FADE_6,  FADE_6,
	FADE_6,  FADE_6,  FADE_6,  FADE_6,
	FADE_7,  FADE_7,  FADE_7,  FADE_7,
	FADE_8,  FADE_8,  FADE_8,  FADE_8,
	FADE_9,  FADE_9,  FADE_9,  FADE_9,
	FADE_10, FADE_10, FADE_10, FADE_10,
	FADE_11, FADE_11, FADE_11, FADE_11,
	FADE_12, FADE_12, FADE_12, FADE_12,
};

_Static_assert(sizeof(fade_pattern_hori) == sizeof(fade_pattern_vert));

#undef X
#undef _

static unsigned pixel_off(SDL_Surface *s, int x, int y)
{
	return y * s->pitch + x;
}

static uint8_t get_pixel(SDL_Surface *s, int x, int y)
{
	return ((uint8_t*)s->pixels)[pixel_off(s, x, y)];
}

static uint8_t *get_pixel_p(SDL_Surface *s, int x, int y)
{
	return &((uint8_t*)s->pixels)[pixel_off(s, x, y)];
}

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

void gfx_fade_down(int x, int y, int w, int h, unsigned dst_i, int src_i)
{
	GFX_LOG("gfx_fade_down %d -> %u{%d,%d} @ (%d,%d)", src_i, dst_i, x, y, w, h);
	SDL_Surface *s = gfx_get_surface(dst_i);
	SDL_Surface *src_s = src_i < 0 ? NULL : gfx_get_surface(src_i);
	SDL_Rect r = { x, y, w, h };
	if (!fill_clip(s, &r)) {
		WARNING("Invalid fade");
		return;
	}

	uint32_t frame_timer = vm_timer_create();
	uint8_t *base = s->pixels + r.y * s->pitch + r.x;
	for (int i = 0; i < FADE_SIZE + r.h; i += FADE_PATTERN_SIZE * 2) {
		int row = 0;
		int fade_start = FADE_SIZE - i;

		// solid fill above fade pattern
		for (; fade_start < 0; fade_start++, row++) {
			uint8_t *dst = base + row * s->pitch;
			if (src_s) {
				uint8_t *src = get_pixel_p(src_s, r.x, r.y + row);
				memcpy(dst, src, r.w);
			} else {
				memset(dst, 0, r.w);
			}
		}

		// fill with pattern
		const uint8_t *fade = fade_pattern_vert + fade_start * FADE_PATTERN_SIZE;
		for (int fade_row = 0; row < r.h && fade_row < FADE_SIZE - fade_start; fade_row++, row++) {
			uint8_t *dst = base + row * s->pitch;
			const uint8_t *src = fade + fade_row * FADE_PATTERN_SIZE;
			for (int col = 0; col < r.w; col++, dst++) {
				if (src[col % FADE_PATTERN_SIZE]) {
					if (src_s) {
						*dst = get_pixel(src_s, r.x + col, r.y + row);
					} else {
						*dst = 0;
					}
				}
			}
		}

		// update
		gfx.dirty = true;
		vm_peek();

		// wait until next frame
		vm_timer_tick(&frame_timer, 10);
	}
}

void gfx_fade_right(int x, int y, int w, int h, unsigned dst_i, int src_i)
{
	GFX_LOG("gfx_fade_right %d -> %u(%d,%d) @ (%d,%d)", src_i, dst_i, x, y, w, h);
	SDL_Surface *s = gfx_get_surface(dst_i);
	SDL_Surface *src_s = src_i < 0 ? NULL : gfx_get_surface(src_i);
	SDL_Rect r = { x, y, w, h };
	if (!fill_clip(s, &r)) {
		WARNING("Invalid fade");
		return;
	}

	uint32_t frame_timer = vm_timer_create();
	uint8_t *base = s->pixels + r.y * s->pitch + r.x;
	for (int i = 0; i < FADE_SIZE + r.w; i += FADE_PATTERN_SIZE * 2) {
		for (int row = 0; row < r.h; row++) {
			int col = 0;
			uint8_t *dst = base + row * s->pitch;
			int fade_start = FADE_SIZE - i;

			// solid fill left of fade pattern
			if (fade_start < 0) {
				int nr_cols = abs(fade_start);
				if (src_s) {
					uint8_t *p = get_pixel_p(src_s, r.x, r.y + row);
					memcpy(dst, p, nr_cols);
				} else {
					memset(dst, 0, nr_cols);
				}
				dst += nr_cols;
				col += nr_cols;
				fade_start = 0;
			}

			// fill with pattern
			const uint8_t *fade = fade_pattern_hori
				+ (row % FADE_PATTERN_SIZE) * FADE_SIZE + fade_start;
			for (int fade_col = 0; col < r.w && fade_col < FADE_SIZE - fade_start;
					fade_col++, col++, dst++, fade++) {
				if (*fade) {
					if (src_s) {
						*dst = get_pixel(src_s, r.x + col, r.y + row);
					} else {
						*dst = 0;
					}
				}
			}
		}

		// update
		gfx.dirty = true;
		vm_peek();

		// wait until next frame
		vm_timer_tick(&frame_timer, 10);
	}
}
