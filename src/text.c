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

#include <SDL_ttf.h>

#include "nulib.h"
#include "ai5/mes.h"

#include "ai5.h"
#include "gfx_private.h"
#include "gfx.h"
#include "memory.h"

#ifndef AI5_DATA_DIR
#define AI5_DATA_DIR "."
#endif

struct font {
	int size;
	int y_off;
	TTF_Font *id;
};

#define MAX_FONTS 256
static struct font fonts[MAX_FONTS] = {0};
static int nr_fonts = 0;
static struct font *cur_font = NULL;

static struct font *font_lookup(int size)
{
	for (int i = 0; i < nr_fonts; i++) {
		if (fonts[i].size == size)
			return &fonts[i];
	}
	return NULL;
}

static struct font *font_insert(int size, TTF_Font *id)
{
	int min_x, max_x, min_y, max_y, adv;
	int ascent = TTF_FontAscent(id);
	TTF_GlyphMetrics32(id, 'A', &min_x, &max_x, &min_y, &max_y, &adv);

	// Calculate the y-offset for the font. This is a bit hacky, but it
	// works reasonably well for most fonts.
	int y_off = ascent - size;         // align baseline to point size
	y_off += (size - (max_y - 2)) / 2; // center based on height of 'A'

	if (nr_fonts >= MAX_FONTS)
		ERROR("Font table is full");
	fonts[nr_fonts].size = size;
	fonts[nr_fonts].y_off = y_off;
	fonts[nr_fonts].id = id;
	return &fonts[nr_fonts++];
}

void gfx_text_init(void)
{
	if (TTF_Init() == -1)
		ERROR("TTF_Init: %s", TTF_GetError());
	gfx_text_set_size(memory_system_var16()[MES_SYS_VAR_FONT_HEIGHT]);
}

void gfx_text_set_colors(uint8_t bg, uint8_t fg)
{
	gfx.text.bg = bg;
	gfx.text.fg = fg;
}

void gfx_text_get_colors(uint8_t *bg, uint8_t *fg)
{
	*bg = gfx.text.bg;
	*fg = gfx.text.fg;
}

void gfx_text_fill(int x, int y, int w, int h, unsigned i)
{
	gfx_fill(x, y, w, h, i, gfx.text.bg);
}

void gfx_text_swap_colors(int x, int y, int w, int h, unsigned i)
{
	gfx_swap_colors(x, y, w, h, i, gfx.text.bg, gfx.text.fg);
}

// XXX: We have to blit manually so that the correct foreground index is written.
static void glyph_blit(SDL_Surface *glyph, int dst_x, int dst_y, SDL_Surface *s)
{
	int glyph_x = 0;
	int glyph_y = 0;
	int glyph_w = glyph->w;
	int glyph_h = glyph->h;
	if (unlikely(dst_x < 0)) {
		glyph_w += dst_x;
		glyph_x -= dst_x;
		dst_x = 0;
	}
	if (unlikely(dst_y < 0)) {
		glyph_h += dst_y;
		glyph_y -= dst_y;
		dst_y = 0;
	}
	if (unlikely(dst_x + glyph_w > s->w))
		glyph_w = s->w - dst_x;
	if (unlikely(dst_y + glyph_h > s->h))
		glyph_h = s->h - dst_y;
	if (unlikely(glyph_w <= 0 || glyph_h <= 0))
		return;

	if (SDL_MUSTLOCK(glyph))
		SDL_CALL(SDL_LockSurface, glyph);
	if (SDL_MUSTLOCK(s))
		SDL_CALL(SDL_LockSurface, s);

	uint8_t *src_base = glyph->pixels + glyph_y * glyph->pitch + glyph_x;
	uint8_t *dst_base = s->pixels + dst_y * s->pitch + dst_x;
	for (int row = 0; row < glyph_h; row++) {
		uint8_t *src_p = src_base + row * glyph->pitch;
		uint8_t *dst_p = dst_base + row * s->pitch;
		for (int col = 0; col < glyph_w; col++, dst_p++, src_p++) {
			if (*src_p != 0)
				*dst_p = gfx.text.fg;
		}
	}

	if (SDL_MUSTLOCK(s))
		SDL_UnlockSurface(s);
	if (SDL_MUSTLOCK(glyph))
		SDL_UnlockSurface(glyph);
}

unsigned gfx_text_draw_glyph(int x, int y, unsigned i, uint32_t ch)
{
	if (!cur_font)
		return 0;

	SDL_Surface *dst = gfx_get_surface(i);
	assert(gfx.text.fg < dst->format->palette->ncolors);
	SDL_Color fg = dst->format->palette->colors[gfx.text.fg];
	SDL_Surface *s = TTF_RenderGlyph32_Solid(cur_font->id, ch, fg);
	if (!s)
		ERROR("TTF_RenderGlyph32_Solid: %s", TTF_GetError());

	y -= cur_font->y_off;
	unsigned w = s->w;
	glyph_blit(s, x, y, dst);
	SDL_FreeSurface(s);
	gfx_dirty(i);
	return w;
}

void gfx_text_set_size(int size)
{
	struct font *font = font_lookup(size);
	if (!font) {
		TTF_Font *f;
		if (yuno_eng) {
			f = TTF_OpenFont(AI5_DATA_DIR "/fonts/NotoSansJP-Thin.ttf", size);
		} else if (size <= 18) {
			f = TTF_OpenFont(AI5_DATA_DIR "/fonts/DotGothic16-Regular.ttf", size);
		} else {
			f = TTF_OpenFont(AI5_DATA_DIR "/fonts/Kosugi-Regular.ttf", size);
		}
		if (!f)
			ERROR("TTF_OpenFont: %s", TTF_GetError());
		font = font_insert(size, f);
	}
	cur_font = font;
}

unsigned gfx_text_size_char(uint32_t ch)
{
	int minx, maxx, miny, maxy, advance;
	TTF_GlyphMetrics32(cur_font->id, ch, &minx, &maxx, &miny, &maxy, &advance);
	return advance;
}
