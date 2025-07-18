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

#include <stdbool.h>
#include <stdint.h>

struct cg;

#define GFX_NR_SURFACES 15

struct gfx_view { unsigned w, h; };
extern struct gfx_view gfx_view;

void gfx_init(const char *name);
void gfx_set_icon(void);
void gfx_update(void);
void gfx_dirty(unsigned surface, int x, int y, int w, int h);
void gfx_screen_dirty(void);
void gfx_whole_surface_dirty(unsigned surface);
bool gfx_is_dirty(unsigned surface);
void gfx_clean(unsigned surface);
void gfx_overlay_enable(void);
void gfx_overlay_disable(void);
unsigned gfx_current_surface(void);
void gfx_set_screen_surface(unsigned i);

// dialogs
void gfx_error_message(const char *message);
bool gfx_confirm_quit(void);

// window operations
void gfx_window_toggle_fullscreen(void);
void gfx_window_increase_integer_size(void);
void gfx_window_decrease_integer_size(void);
void gfx_screenshot(void);

// display operations
void gfx_display_hide(uint32_t color);
void gfx_display_unhide(void);
void gfx_display_freeze(void);
void gfx_display_unfreeze(void);
void _gfx_display_fade_out(uint32_t vm_color, unsigned ms, bool(*cb)(void));
void gfx_display_fade_out(uint32_t vm_color, unsigned ms);
void _gfx_display_fade_in(unsigned ms, bool(*cb)(void));
void gfx_display_fade_in(unsigned ms);

// palette operations
void _gfx_palette_set(const uint8_t *data, unsigned start, unsigned n);
void gfx_palette_set(const uint8_t *data, unsigned start, unsigned n);
void gfx_palette_set_color(uint8_t c, uint8_t r, uint8_t g, uint8_t b);
void gfx_palette_copy(uint8_t *dst, unsigned start, unsigned n);
void gfx_crossfade_colors(uint8_t *pal, uint8_t *colors_in, unsigned nr_colors_in,
		unsigned ms, bool (*cb)(float,void*), void *data);
void gfx_palette_crossfade(const uint8_t *data, unsigned start, unsigned n, unsigned ms);
void gfx_palette_crossfade_to(uint8_t r, uint8_t g, uint8_t b, unsigned start, unsigned n,
		unsigned ms);

// draw operations
void gfx_copy(int src_x, int src_y, int src_w, int src_h, unsigned src_i, int dst_x,
		int dst_y, unsigned dst_i);
void gfx_copy_masked(int src_x, int src_y, int src_w, int src_h, unsigned src_i, int dst_x,
		int dst_y, unsigned dst_i, uint32_t mask_color);
void gfx_copy_swap(int src_x, int src_y, int src_w, int src_h, unsigned src_i, int dst_x,
		int dst_y, unsigned dst_i);
void gfx_compose(int fg_x, int fg_y, int w, int h, unsigned fg_i, int bg_x, int bg_y,
		unsigned bg_i, int dst_x, int dst_y, unsigned dst_i, uint16_t mask_color);
void gfx_blend(int src_x, int src_y, int w, int h, unsigned src_i, int dst_x, int dst_y,
		unsigned dst_i, uint8_t alpha);
void gfx_blend_masked(int src_x, int src_y, int w, int h, unsigned src_i, int dst_x,
		int dst_y, unsigned dst_i, uint8_t *mask);
void gfx_blend_with_mask_color_to(int a_x, int a_y, int w, int h, unsigned a_i, int b_x,
		int b_y, unsigned b_i, int dst_x, int dst_y, unsigned dst_i, int mask_w,
		int mask_h, uint8_t *mask);
void gfx_invert_colors(int x, int y, int w, int h, unsigned i);
void gfx_fill(int x, int y, int w, int h, unsigned i, uint32_t c);
void gfx_swap_colors(int x, int y, int w, int h, unsigned i, uint32_t c1, uint32_t c2);
void gfx_blend_fill(int x, int y, int w, int h, unsigned i, uint32_t c, uint8_t rate);
void gfx_draw_cg(unsigned i, struct cg *cg);

// effect.c
void gfx_blink_fade(int x, int y, int w, int h, unsigned dst_i);
void gfx_fade_down(int x, int y, int w, int h, unsigned dst_i, int src_i);
void gfx_fade_right(int x, int y, int w, int h, unsigned dst_i, int src_i);
void gfx_pixelate(int x, int y, int w, int h, unsigned dst_i, unsigned mag);
void gfx_fade_progressive(int x, int y, int w, int h, unsigned dst_i);
void gfx_copy_progressive(int src_x, int src_y, int w, int h, unsigned src_i, int dst_x,
		int dst_y, unsigned dst_i);
void gfx_pixel_crossfade(int src_x, int src_y, int w, int h, unsigned src_i, int dst_x,
		int dst_y, unsigned dst_i, unsigned frame_t, bool(*update)(float t, void*),
		void *data);
void gfx_pixel_crossfade_masked(int src_x, int src_y, int w, int h, unsigned src_i, int dst_x,
		int dst_y, unsigned dst_i, uint32_t mask_color);
void gfx_pixel_crossfade_masked_indexed(int src_x, int src_y, int w, int h, unsigned src_i,
		int dst_x, int dst_y, unsigned dst_i, uint8_t mask_color);
void gfx_pixel_crossfade_masked_indexed_8x8(int src_x, int src_y, int w, int h, unsigned src_i,
		int dst_x, int dst_y, unsigned dst_i, uint8_t mask_color);
void gfx_scale_h(unsigned i, int mag);
void gfx_zoom(int src_x, int src_y, int w, int h, unsigned src_i, unsigned dst_i,
		unsigned ms);

// text.c
void gfx_text_init(const char *font_path, int face);
void gfx_text_set_colors(uint32_t bg, uint32_t fg);
void gfx_text_get_colors(uint32_t *bg, uint32_t *fg);
void gfx_text_set_size(int size, int weight);
void gfx_text_set_weight(int weight);
void gfx_text_fill(int x, int y, int w, int h, unsigned i);
void gfx_text_swap_colors(int x, int y, int w, int h, unsigned i);
unsigned gfx_text_draw_glyph(int x, int y, unsigned i, uint32_t ch);
unsigned gfx_text_size_char(uint32_t ch);

enum text_shadow_type {
	TEXT_SHADOW_NONE = 0,
	TEXT_SHADOW_A = 1,
	TEXT_SHADOW_B = 2,
};

// default: false
extern bool text_antialias;
extern enum text_shadow_type text_shadow;

#endif // AI5_GFX_H
