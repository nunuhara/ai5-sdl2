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

#ifndef AI5_GFX_H
#define AI5_GFX_H

struct cg;

#define DEFAULT_VIEW_WIDTH 640
#define DEFAULT_VIEW_HEIGHT 400

struct gfx_view { unsigned w, h; };
extern struct gfx_view gfx_view;

void gfx_init(void);
void gfx_update(void);
void gfx_dirty(void);
void gfx_set_window_size(unsigned w, unsigned h);
void gfx_set_palette(const uint8_t *data);
void gfx_fill(unsigned tl_x, unsigned tl_y, unsigned br_x, unsigned br_y, uint8_t c);

void gfx_draw_cg(struct cg *cg);

#define DEFAULT_FONT_SIZE 16

void gfx_text_init(void);
void gfx_text_set_colors(uint8_t bg, uint8_t fg);
void gfx_text_set_size(int size);
void gfx_text_fill(unsigned tl_x, unsigned tl_y, unsigned br_x, unsigned br_y);
unsigned gfx_text_draw_glyph(unsigned x, unsigned y, uint32_t ch);

#endif // AI5_GFX_H
