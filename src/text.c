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
#include "nulib/file.h"
#include "ai5/mes.h"

#include "ai5.h"
#include "game.h"
#include "gfx_private.h"
#include "gfx.h"
#include "memory.h"

#ifndef AI5_DATA_DIR
#define AI5_DATA_DIR "."
#endif

#ifdef EMBED_DOTGOTHIC
extern unsigned char font_dotgothic[];
extern unsigned int  font_dotgothic_len;
#endif
#ifdef EMBED_KOSUGI
extern unsigned char font_kosugi[];
extern unsigned int  font_kosugi_len;
#endif
#ifdef EMBED_NOTO
extern unsigned char font_noto[];
extern unsigned int  font_noto_len;
#endif
#ifdef EMBED_TAHOMA
extern unsigned char font_tahoma[];
extern unsigned int  font_tahoma_len;
#endif

struct font {
	int size;
	int y_off;
	TTF_Font *id;
	TTF_Font *id_outline;
};

#define MAX_FONTS 256
static struct font fonts[MAX_FONTS] = {0};
static int nr_fonts = 0;
static struct font *cur_font = NULL;

enum font_type {
	FONT_SMALL,
	FONT_LARGE,
	FONT_ENG,
	FONT_UI,
#define NR_FONT_TYPES (FONT_UI+1)
};

struct font_spec {
	bool embedded;
	union {
		struct {
			SDL_RWops *rwops;
			SDL_RWops *rwops_outline;
		};
		char *path;
	};
	unsigned face;
} font_spec[NR_FONT_TYPES] = {0};

bool text_antialias = false;

static struct font *font_lookup(int size)
{
	for (int i = 0; i < nr_fonts; i++) {
		if (fonts[i].size == size)
			return &fonts[i];
	}
	return NULL;
}

static struct font *font_insert(int size, TTF_Font *id, TTF_Font *id_outline)
{
	int min_x, max_x, min_y, max_y, adv;
	int ascent = TTF_FontAscent(id);
	TTF_GlyphMetrics32(id, 'A', &min_x, &max_x, &min_y, &max_y, &adv);

	// Calculate the y-offset for the font. This is a bit hacky, but it
	// works reasonably well for most fonts.
	int y_off = ascent - size;         // align baseline to point size
	y_off += (size - (max_y - 2)) / 2; // center based on height of 'A'
	y_off -= 1;

	if (nr_fonts >= MAX_FONTS)
		ERROR("Font table is full");
	fonts[nr_fonts].size = size;
	fonts[nr_fonts].y_off = y_off;
	fonts[nr_fonts].id = id;
	fonts[nr_fonts].id_outline = id_outline;
	return &fonts[nr_fonts++];
}

#define EMBEDDED_FONT(name) (struct font_spec) { \
	.embedded = true, \
	.rwops = SDL_RWFromConstMem(font_##name, font_##name##_len), \
	.rwops_outline = SDL_RWFromConstMem(font_##name, font_##name##_len), \
}

static void init_ui_font(void)
{
#ifdef EMBED_TAHOMA
	font_spec[FONT_UI] = EMBEDDED_FONT(tahoma);
#else
	font_spec[FONT_UI].path = xstrdup(AI5_DATA_DIR "/fonts/wine_tahoma.ttf");
#endif
}

static void init_fonts_standard(void)
{
#ifdef EMBED_DOTGOTHIC
	font_spec[FONT_SMALL] = EMBEDDED_FONT(dotgothic);
#else
	font_spec[FONT_SMALL].path = xstrdup(AI5_DATA_DIR "/fonts/DotGothic16-Regular.ttf");
#endif
#ifdef EMBED_KOSUGI
	font_spec[FONT_LARGE] = EMBEDDED_FONT(kosugi);
#else
	font_spec[FONT_LARGE].path = xstrdup(AI5_DATA_DIR "/fonts/Kosugi-Regular.ttf");
#endif
#ifdef EMBED_NOTO
	font_spec[FONT_ENG] = EMBEDDED_FONT(noto);
#else
	font_spec[FONT_ENG].path = xstrdup(AI5_DATA_DIR "/fonts/NotoSansJP-Thin.ttf");
#endif
}

void gfx_text_init(const char *font_path, int face)
{
	if (TTF_Init() == -1)
		ERROR("TTF_Init: %s", TTF_GetError());

	init_ui_font();
	if (font_path) {
		// XXX: we override the default face for msgothic on yuno-eng
		int face_eng = face;
		if (!strcasecmp(path_basename(font_path), "msgothic.ttc")) {
			if (face < 0)
				face_eng = 1; // MS PGothic
		}
		if (face < 0) face = 0;
		if (face_eng < 0) face_eng = 0;

		font_spec[FONT_SMALL].path = xstrdup(font_path);
		font_spec[FONT_SMALL].face = face;
		font_spec[FONT_LARGE].path = xstrdup(font_path);
		font_spec[FONT_LARGE].face = face;
		font_spec[FONT_ENG].path = xstrdup(font_path);
		font_spec[FONT_ENG].face = face_eng;
	} else {
#ifdef _WIN32
		if (game->bpp == 8) {
			// XXX: We only use MS Gothic for indexed color, since direct color
			//      games render text with an outline and SDL_ttf can't render
			//      an outline on MS Gothic for some reason.
			font_spec[FONT_SMALL].path = xstrdup("C:/Windows/Fonts/msgothic.ttc");
			font_spec[FONT_LARGE].path = xstrdup(font_spec[FONT_SMALL].path);
			font_spec[FONT_ENG].path = xstrdup(font_spec[FONT_SMALL].path);
			font_spec[FONT_ENG].face = 1;
		} else {
			init_fonts_standard();
		}
#else
		init_fonts_standard();
#endif // _WIN32
	}
	gfx_text_set_size(mem_get_sysvar16(mes_sysvar16_font_height),
			mem_get_sysvar16(mes_sysvar16_font_weight));
}

void gfx_text_set_colors(uint32_t bg, uint32_t fg)
{
	gfx.text.bg = bg;
	gfx.text.fg = fg;
	if (game->bpp == 16) {
		gfx.text.bg_color = gfx_decode_bgr555(bg);
		gfx.text.fg_color = gfx_decode_bgr555(fg);
	}
	else if (game->bpp == 24) {
		gfx.text.bg_color = gfx_decode_bgr(bg);
		gfx.text.fg_color = gfx_decode_bgr(fg);
	}
}

void gfx_text_get_colors(uint32_t *bg, uint32_t *fg)
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
static void glyph_blit_indexed(SDL_Surface *glyph, int dst_x, int dst_y, SDL_Surface *s)
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

	// XXX: prevent text from overflowing at bottom
	glyph_h = min(cur_font->y_off + cur_font->size, glyph_h);

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

static unsigned gfx_text_draw_glyph_indexed(int i, int x, int y, uint32_t ch)
{
	SDL_Surface *dst = gfx_get_surface(i);
	assert(gfx.text.fg < dst->format->palette->ncolors);
	SDL_Color fg = dst->format->palette->colors[gfx.text.fg];
	SDL_Surface *s = TTF_RenderGlyph32_Solid(cur_font->id, ch, fg);
	if (!s)
		ERROR("TTF_RenderGlyph32_Solid: %s", TTF_GetError());

	y -= cur_font->y_off;
	unsigned w = s->w;
	glyph_blit_indexed(s, x, y, dst);
	gfx_dirty(i, x, y, s->w, s->h);
	SDL_FreeSurface(s);
	return w;
}

static unsigned gfx_text_draw_glyph_direct(int i, int x, int y, uint32_t ch)
{
	SDL_Surface *outline, *glyph;
	SDL_Surface *dst = gfx_get_surface(i);
	if (!text_antialias) {
		// XXX: Antialiasing can cause issues if the text is rendered to a surface
		//      filled with the mask color and then copied to the main surface with
		//      copy_masked (e.g. Doukyuusei does this).
		outline = TTF_RenderGlyph32_Solid(cur_font->id_outline, ch, gfx.text.bg_color);
		glyph = TTF_RenderGlyph32_Solid(cur_font->id, ch, gfx.text.fg_color);
	} else {
		outline = TTF_RenderGlyph32_Blended(cur_font->id_outline, ch, gfx.text.bg_color);
		glyph = TTF_RenderGlyph32_Blended(cur_font->id, ch, gfx.text.fg_color);
	}
	if (!outline || !glyph)
		ERROR("TTF_RenderGlyph32_Blended: %s", TTF_GetError());

	y -= cur_font->y_off;

	SDL_Rect outline_r = { x-1, y-1, outline->w, outline->h };
	SDL_Rect glyph_r = { x, y, glyph->w, glyph->h };
	SDL_CALL(SDL_BlitSurface, outline, NULL, dst, &outline_r);
	SDL_CALL(SDL_BlitSurface, glyph, NULL, dst, &glyph_r);
	gfx_dirty(i, x-1, y-1, outline->w, outline->h);
	SDL_FreeSurface(glyph);
	SDL_FreeSurface(outline);
	return glyph_r.w;
}

unsigned gfx_text_draw_glyph(int x, int y, unsigned i, uint32_t ch)
{
	if (!cur_font)
		return 0;

	unsigned r;
	if (game->bpp == 8)
		r = gfx_text_draw_glyph_indexed(i, x, y, ch);
	else
		r = gfx_text_draw_glyph_direct(i, x, y, ch);
	return r;
}

#define UI_FONT_SIZE 12
static TTF_Font *ui_font = NULL;
int ui_ascent = 0;

static bool ui_font_init(void)
{
	if (ui_font)
		return true;
	struct font_spec *spec = &font_spec[FONT_UI];
	if (spec->embedded)
		ui_font = TTF_OpenFontIndexRW(spec->rwops, false, UI_FONT_SIZE, spec->face);
	else {
		ui_font = TTF_OpenFontIndex(spec->path, UI_FONT_SIZE, spec->face);
	}
	if (!ui_font) {
		WARNING("TTF_OpenFont: %s", TTF_GetError());
		return false;
	}
	// calculate ASCII ascent based on height of 'A' character.
	int min_x, max_x, min_y, max_y, adv;
	TTF_GlyphMetrics32(ui_font, 'A', &min_x, &max_x, &min_y, &max_y, &adv);
	ui_ascent = max_y;
	return true;
}

void ui_draw_text(SDL_Surface *s, int x, int y, const char *text, SDL_Color color)
{
	if (!ui_font && !ui_font_init()) {
		return;
	}
	SDL_Surface *text_s = TTF_RenderUTF8_Solid(ui_font, text, color);
	SDL_Rect text_r = { x, y - (TTF_FontAscent(ui_font) - ui_ascent) - ui_ascent / 2, text_s->w, text_s->h };
	SDL_CALL(SDL_BlitSurface, text_s, NULL, s, &text_r);
}

int ui_measure_text(const char *text)
{
	if (!ui_font && !ui_font_init())
		return 0;

	int extent, count;
	if (TTF_MeasureUTF8(ui_font, text, 10000, &extent, &count)) {
		WARNING("TTF_MeasureUTF8: %s", TTF_GetError());
		return 0;
	}
	return extent;
}

static void open_font(struct font_spec *spec, int size, TTF_Font **out, TTF_Font **outline_out)
{
	if (spec->embedded) {
		*out = TTF_OpenFontIndexRW(spec->rwops, false, size, spec->face);
		*outline_out = TTF_OpenFontIndexRW(spec->rwops_outline, false, size, spec->face);
	} else {
		*out = TTF_OpenFontIndex(spec->path, size, spec->face);
		*outline_out = TTF_OpenFontIndex(spec->path, size, spec->face);
	}
	if (!*out || !*outline_out)
		ERROR("TTF_OpenFont: %s", TTF_GetError());
	TTF_SetFontOutline(*outline_out, 1);
}

void gfx_text_set_size(int size, int weight)
{
	struct font *font = font_lookup(size);
	if (!font) {
		TTF_Font *f;
		TTF_Font *f_outline = NULL;
		enum font_type type = yuno_eng ? FONT_ENG : (size <= 18 ? FONT_SMALL : FONT_LARGE);
		open_font(&font_spec[type], size, &f, &f_outline);
		font = font_insert(size, f, f_outline);
	}
	int style = weight ? TTF_STYLE_BOLD : TTF_STYLE_NORMAL;
	TTF_SetFontStyle(font->id, style);
	TTF_SetFontStyle(font->id_outline, style);
	cur_font = font;
	gfx.text.size = size;
}

void gfx_text_set_weight(int weight)
{
	int style = weight ? TTF_STYLE_BOLD : TTF_STYLE_NORMAL;
	if (TTF_GetFontStyle(cur_font->id) != style) {
		TTF_SetFontStyle(cur_font->id, style);
		TTF_SetFontStyle(cur_font->id_outline, style);
	}
}

unsigned gfx_text_size_char(uint32_t ch)
{
	int minx, maxx, miny, maxy, advance;
	TTF_GlyphMetrics32(cur_font->id, ch, &minx, &maxx, &miny, &maxy, &advance);
	return advance;
}
