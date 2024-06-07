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
#include "ai5.h"
#include "ai5/cg.h"

#include "game.h"
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

void gfx_fade_down(int x, int y, int w, int h, unsigned dst_i, int src_i)
{
	GFX_LOG("gfx_fade_down %d -> %u{%d,%d} @ (%d,%d)", src_i, dst_i, x, y, w, h);
	if (unlikely(game->bpp != 8))
		VM_ERROR("Invalid bpp for gfx_fade_down");

	SDL_Surface *s = gfx_get_surface(dst_i);
	SDL_Surface *src_s = src_i < 0 ? NULL : gfx_get_surface(src_i);
	SDL_Rect r = { x, y, w, h };
	if (!gfx_fill_clip(s, &r)) {
		WARNING("Invalid fade");
		return;
	}

	uint32_t frame_timer = vm_timer_create();
	uint8_t *base = s->pixels + r.y * s->pitch + r.x;
	for (int i = 0; i < FADE_SIZE + r.h + FADE_PATTERN_SIZE * 2; i += FADE_PATTERN_SIZE * 2) {
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
		gfx_dirty(dst_i);
		vm_peek();

		// wait until next frame
		vm_timer_tick(&frame_timer, 10);
	}
}

void gfx_fade_right(int x, int y, int w, int h, unsigned dst_i, int src_i)
{
	GFX_LOG("gfx_fade_right %d -> %u(%d,%d) @ (%d,%d)", src_i, dst_i, x, y, w, h);
	if (unlikely(game->bpp != 8))
		VM_ERROR("Invalid bpp for gfx_fade_right");

	SDL_Surface *s = gfx_get_surface(dst_i);
	SDL_Surface *src_s = src_i < 0 ? NULL : gfx_get_surface(src_i);
	SDL_Rect r = { x, y, w, h };
	if (!gfx_fill_clip(s, &r)) {
		WARNING("Invalid fade");
		return;
	}

	uint32_t frame_timer = vm_timer_create();
	uint8_t *base = s->pixels + r.y * s->pitch + r.x;
	for (int i = 0; i < FADE_SIZE + r.w + FADE_PATTERN_SIZE * 2; i += FADE_PATTERN_SIZE * 2) {
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
		gfx_dirty(dst_i);
		vm_peek();

		// wait until next frame
		vm_timer_tick(&frame_timer, 10);
	}
}

void gfx_pixelate(int x, int y, int w, int h, unsigned dst_i, unsigned mag)
{
	GFX_LOG("gfx_pixelate[%u] %u(%d,%d) @ (%d,%d)", mag, dst_i, x, y, w, h);
	if (unlikely(game->bpp != 8))
		VM_ERROR("Invalid bpp for gfx_pixelate");

	SDL_Surface *s = gfx_get_surface(dst_i);
	SDL_Rect r = { x, y, w, h };
	if (!gfx_fill_clip(s, &r)) {
		WARNING("Invalid pixelate");
		return;
	}

	unsigned band_size = min(w, 2 << mag);
	if (band_size < 2) {
		WARNING("Invalid magnitude");
		return;
	}

	uint8_t *base = s->pixels + r.y * s->pitch + r.x;
	for (int row = 0; row < r.h; row++) {
		uint8_t *p = base + row * s->pitch;
		for (int col = 0; col < r.w; col += band_size, p += band_size) {
			// FIXME: this sampling method doesn't give the same result as the
			//        original implementation.
			uint8_t c = p[min(band_size/2, (r.w - 1) - col)];
			memset(p, c, min(band_size, r.w - col));
		}
	}
	gfx_dirty(dst_i);
}

static void fade_row(uint8_t *base, unsigned row, unsigned w, unsigned h, unsigned pitch)
{
	if (row >= h)
		return;
	uint8_t *dst = base + row * pitch;
	memset(dst, 0, w);
}

void progressive_update(vm_timer_t *timer, unsigned dst_i)
{
	gfx_dirty(dst_i);
	vm_peek();
	vm_timer_tick(timer, config.progressive_frame_time);
}

/*
 * Fill with color 7, then fill from top and bottom progressively with color 0.
 */
void gfx_blink_fade(int x, int y, int w, int h, unsigned dst_i)
{
	GFX_LOG("gfx_blink_fade %u(%d,%d) @ (%d,%d)", dst_i, x, y, w, h);
	if (unlikely(game->bpp != 8))
		VM_ERROR("Invalid bpp for gfx_blink_fade");

	SDL_Surface *s = gfx_get_surface(dst_i);
	SDL_Rect r = { x, y, w, h };
	if (!gfx_fill_clip(s, &r)) {
		WARNING("Invalid blink_fade");
		return;
	}

	vm_timer_t timer = vm_timer_create();
	uint8_t *base = s->pixels + r.y * s->pitch + r.x;
	for (int row = 0; row < r.h; row++) {
		uint8_t *dst = base + row * s->pitch;
		memset(dst, 7, r.w);
	}
	progressive_update(&timer, dst_i);

	unsigned logical_h = ((unsigned)r.h + 3u) & ~3u;
	for (int row = 0; row < logical_h / 2; row += 4) {
		unsigned row_top = row;
		unsigned row_bot = (logical_h - row) - 4;
		uint8_t *top = base + row_top * s->pitch;
		uint8_t *bot = base + row_bot * s->pitch;
		for (int i = 0; i < 4; i++) {
			memset(top + i * s->pitch, 0, r.w);
			if (row_bot + i < r.h)
				memset(bot + i * s->pitch, 0, r.w);
		}
		progressive_update(&timer, dst_i);
	}
}

void gfx_fade_progressive(int x, int y, int w, int h, unsigned dst_i)
{
	GFX_LOG("gfx_fade_progressive %u(%d,%d) @ (%d,%d)", dst_i, x, y, w, h);
	if (unlikely(game->bpp != 8))
		VM_ERROR("Invalid bpp for gfx_fade_progressive");

	SDL_Surface *s = gfx_get_surface(dst_i);
	SDL_Rect r = { x, y, w, h };
	if (!gfx_fill_clip(s, &r)) {
		WARNING("Invalid fade_progressive");
		return;
	}

	vm_timer_t timer = vm_timer_create();
	unsigned logical_h = ((unsigned)r.h + 3u) & ~3u;
	uint8_t *base = s->pixels + r.y * s->pitch + r.x;
	for (int row = 0; row <= logical_h; row += 4) {
		fade_row(base, row, r.w, r.h, s->pitch);
		fade_row(base, (logical_h - row) + 2, r.w, r.h, s->pitch);
		progressive_update(&timer, dst_i);
	}

	for (int row = 0; row <= logical_h; row += 4) {
		fade_row(base, row + 1, r.w, r.h, s->pitch);
		fade_row(base, (logical_h - row) + 3, r.w, r.h, s->pitch);
		progressive_update(&timer, dst_i);
	}
}

static void copy_row(uint8_t *src_base, uint8_t *dst_base, unsigned row, unsigned w,
		unsigned h, unsigned src_pitch, unsigned dst_pitch, unsigned bytes_pp)
{
	if (row >= h)
		return;
	uint8_t *src = src_base + row * src_pitch;
	uint8_t *dst = dst_base + row * dst_pitch;
	memcpy(dst, src, w * bytes_pp);
}

void gfx_copy_progressive(int src_x, int src_y, int w, int h, unsigned src_i, int dst_x,
		int dst_y, unsigned dst_i)
{
	GFX_LOG("gfx_copy_progressive %u(%d,%d) -> %u(%d,%d) @ (%d,%d)", src_i, src_x, src_y,
			dst_i, dst_x, dst_y, w, h);

	SDL_Surface *src = gfx_get_surface(src_i);
	SDL_Surface *dst = gfx_get_surface(dst_i);
	SDL_Rect src_r = { src_x, src_y, w, h };
	SDL_Point dst_p = { dst_x, dst_y };

	if (!gfx_copy_clip(src, &src_r, dst, &dst_p)) {
		WARNING("Invalid copy");
		return;
	}

	vm_timer_t timer = vm_timer_create();
	unsigned bytes_pp = src->format->BytesPerPixel;
	unsigned logical_h = ((unsigned)src_r.h + 3u) & ~3u;
	uint8_t *src_base = src->pixels + src_r.y * src->pitch + src_r.x * bytes_pp;
	uint8_t *dst_base = dst->pixels + dst_p.y * dst->pitch + dst_p.x * bytes_pp;
	for (int row = 0; row <= logical_h; row += 4) {
		unsigned row_top = row;
		unsigned row_bot = (logical_h - row) + 2;
		copy_row(src_base, dst_base, row_top, src_r.w, src_r.h, src->pitch, dst->pitch, bytes_pp);
		copy_row(src_base, dst_base, row_bot, src_r.w, src_r.h, src->pitch, dst->pitch, bytes_pp);
		progressive_update(&timer, dst_i);
	}

	for (int row = 0; row <= logical_h; row += 4) {
		unsigned row_top = row + 1;
		unsigned row_bot = (logical_h - row) + 3;
		copy_row(src_base, dst_base, row_top, src_r.w, src_r.h, src->pitch, dst->pitch, bytes_pp);
		copy_row(src_base, dst_base, row_bot, src_r.w, src_r.h, src->pitch, dst->pitch, bytes_pp);
		progressive_update(&timer, dst_i);
	}
}

void gfx_pixel_crossfade(int src_x, int src_y, int w, int h, unsigned src_i, int dst_x,
		int dst_y, unsigned dst_i)
{
	const SDL_Point offsets[] = {
		{ 0, 0 }, { 1, 2 }, { 2, 1 }, { 3, 3 },
		{ 0, 3 }, { 1, 0 }, { 2, 3 }, { 3, 0 },
		{ 0, 1 }, { 1, 3 }, { 2, 0 }, { 3, 2 },
		{ 0, 2 }, { 1, 1 }, { 2, 2 }, { 3, 1 },
	};

	SDL_Surface *src = gfx_get_surface(src_i);
	SDL_Surface *dst = gfx_get_surface(dst_i);
	SDL_Rect src_r = { src_x, src_y, w, h };
	SDL_Point dst_p = { dst_x, dst_y };

	if (!gfx_copy_clip(src, &src_r, dst, &dst_p)) {
		WARNING("Invalid copy");
		return;
	}

	vm_timer_t timer = vm_timer_create();
	unsigned bytes_pp = src->format->BytesPerPixel;
	uint8_t *src_base = src->pixels + src_r.y * src->pitch + src_r.x * bytes_pp;
	uint8_t *dst_base = dst->pixels + dst_p.y * dst->pitch + dst_p.x * bytes_pp;
	for (unsigned off_i = 0; off_i < ARRAY_SIZE(offsets); off_i++) {
		const SDL_Point *off = &offsets[off_i];
		for (int chunk_y = 0; chunk_y < src_r.h; chunk_y += 4) {
			unsigned row = chunk_y + off->y;
			if (row >= src_r.h)
				break;
			for (int chunk_x = 0; chunk_x < src_r.w; chunk_x += 4) {
				unsigned col = chunk_x + off->x;
				if (col >= src_r.w)
					break;
				uint8_t *src_p = src_base + row * src->pitch + col * bytes_pp;
				uint8_t *dst_p = dst_base + row * dst->pitch + col * bytes_pp;
				memcpy(dst_p, src_p, bytes_pp);
			}
		}
		gfx_dirty(dst_i);
		vm_peek();
		vm_timer_tick(&timer, 30);
	}
}

void gfx_pixel_crossfade_masked(int src_x, int src_y, int w, int h, unsigned src_i, int dst_x,
		int dst_y, unsigned dst_i, uint32_t mask_color)
{
	const SDL_Point offsets[] = {
		{ 0, 0 }, { 1, 2 }, { 2, 1 }, { 3, 3 },
		{ 0, 3 }, { 1, 0 }, { 2, 3 }, { 3, 0 },
		{ 0, 1 }, { 1, 3 }, { 2, 0 }, { 3, 2 },
		{ 0, 2 }, { 1, 1 }, { 2, 2 }, { 3, 1 },
	};

	SDL_Surface *src = gfx_get_surface(src_i);
	SDL_Surface *dst = gfx_get_surface(dst_i);
	SDL_Rect src_r = { src_x, src_y, w, h };
	SDL_Point dst_p = { dst_x, dst_y };

	if (!gfx_copy_clip(src, &src_r, dst, &dst_p)) {
		WARNING("Invalid copy");
		return;
	}

	SDL_Color mask;
	if (game->bpp == 16)
		mask = gfx_decode_bgr555(mask_color);
	if (game->bpp == 24)
		mask = gfx_decode_bgr(mask_color);

	vm_timer_t timer = vm_timer_create();
	unsigned bytes_pp = src->format->BytesPerPixel;
	uint8_t *src_base = src->pixels + src_r.y * src->pitch + src_r.x * bytes_pp;
	uint8_t *dst_base = dst->pixels + dst_p.y * dst->pitch + dst_p.x * bytes_pp;
	for (unsigned off_i = 0; off_i < ARRAY_SIZE(offsets); off_i++) {
		const SDL_Point *off = &offsets[off_i];
		for (int chunk_y = 0; chunk_y < src_r.h; chunk_y += 4) {
			unsigned row = chunk_y + off->y;
			if (row >= src_r.h)
				break;
			for (int chunk_x = 0; chunk_x < src_r.w; chunk_x += 4) {
				unsigned col = chunk_x + off->x;
				if (col >= src_r.w)
					break;
				uint8_t *src_p = src_base + row * src->pitch + col * bytes_pp;
				uint8_t *dst_p = dst_base + row * dst->pitch + col * bytes_pp;
				if (src_p[0] != mask.r || src_p[1] != mask.g || src_p[2] != mask.b)
					memcpy(dst_p, src_p, bytes_pp);
			}
		}
		gfx_dirty(dst_i);
		vm_peek();
		vm_timer_tick(&timer, 30);
	}

}

void gfx_scale_h(unsigned i, int mag)
{
	if (unlikely(i >= GFX_NR_SURFACES || !gfx.surface[i].s)) {
		WARNING("Invalid surface index: %u", i);
		i = 0;
	}

	struct gfx_surface *s = &gfx.surface[i];
	if (mag == 0) {
		s->src.y = 0;
		s->src.h = s->s->h;
		s->dst.y = 0;
		s->scaled = !SDL_RectEquals(&s->src, &s->dst);
	} else if (mag < 0) {
		s->src.y = 0;
		s->src.h = s->s->h + mag;
		s->dst.y = mag;
		s->scaled = true;
	} else {
		s->src.y = 0;
		s->src.h = s->s->h - mag;
		s->dst.y = mag;
		s->scaled = true;
	}

	gfx_dirty(i);
	gfx_update();
}

void gfx_zoom(int src_x, int src_y, int w, int h, unsigned src_i, unsigned dst_i,
		unsigned ms)
{
	unsigned steps = roundf((float)ms / 32.f);

	vm_timer_t timer = vm_timer_create();
	SDL_Surface *dst = gfx_get_surface(dst_i);
	SDL_Surface *src = gfx_get_surface(src_i);
	float step_x = (float)src_x * (1.f / (float)steps);
	float step_y = (float)src_y * (1.f / (float)steps);
	float step_w = (640 - w) * (1.f / (float)steps);
	float step_h = (480 - h) * (1.f / (float)steps);
	for (unsigned i = 1; i < steps; i++) {
		SDL_Rect src_r = { 0, 0, 640, 480 };
		SDL_Rect dst_r = {
			.x = src_x - step_x * i,
			.y = src_y - step_y * i,
			.w = w + step_w * i,
			.h = h + step_h * i
		};
		SDL_CALL(SDL_BlitScaled, src, &src_r, dst, &dst_r);
		gfx_dirty(dst_i);
		vm_peek();
		vm_timer_tick(&timer, 32);
	}
	SDL_CALL(SDL_BlitSurface, src, NULL, dst, NULL);
	gfx_dirty(dst_i);
}
