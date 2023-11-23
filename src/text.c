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

#include "gfx_private.h"
#include "gfx.h"
#include "memory.h"

struct font {
	int size;
	TTF_Font *id;
};

#define MAX_FONTS 256
static struct font fonts[MAX_FONTS];
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
	for (int i = 0; i < MAX_FONTS; i++) {
		if (!fonts[i].size) {
			fonts[i].size = size;
			fonts[i].id = id;
			return &fonts[i];
		}
	}
	ERROR("Font table is full");
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

void gfx_text_fill(unsigned tl_x, unsigned tl_y, unsigned br_x, unsigned br_y)
{
	gfx_fill(tl_x, tl_y, br_x, br_y, gfx.text.bg);
}

unsigned gfx_text_draw_glyph(unsigned x, unsigned y, uint32_t ch)
{
	if (!cur_font)
		return 0;

	assert(gfx.text.fg < gfx.indexed->format->palette->ncolors);
	SDL_Color fg = gfx.indexed->format->palette->colors[gfx.text.fg];
	SDL_Surface *s = TTF_RenderGlyph32_Solid(cur_font->id, ch, fg);
	if (!s)
		ERROR("TTF_RenderGlyph32_Solid: %s", TTF_GetError());

	y -= (TTF_FontHeight(cur_font->id) - cur_font->size) / 2;
	SDL_Rect dst = { x, y, s->w, s->h };
	SDL_CALL(SDL_BlitSurface, s, NULL, gfx.indexed, &dst);
	SDL_FreeSurface(s);
	return dst.w;
}

void gfx_text_set_size(int size)
{
	struct font *font = font_lookup(size);
	if (!font) {
		TTF_Font *f = TTF_OpenFont(AI5_DATA_DIR "/fonts/VL-Gothic-Regular.ttf", size);
		if (!f)
			ERROR("TTF_OpenFont: %s", TTF_GetError());
		font = font_insert(size, f);
	}
	cur_font = font;
}
