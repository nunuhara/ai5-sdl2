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

#ifndef AI5_SDL2_SYS_H
#define AI5_SDL2_SYS_H

#include <stdbool.h>
#include <stdint.h>

struct param_list;

void util_warn_unimplemented(struct param_list *params);
void util_noop(struct param_list *params);

const char *_sys_save_name_fmt(const char *fmt, unsigned save_no);
const char *_sys_save_name(unsigned save_no);
const char *sys_save_name(struct param_list *params);

void sys_set_font_size(struct param_list *params);
const char *_sys_number_to_string(uint32_t n, unsigned display_digits, bool halfwidth);
const char *sys_number_to_string(uint32_t n);
void sys_display_number(struct param_list *params);
void sys_cursor_save_pos(struct param_list *params);
void sys_file(struct param_list *params);
void sys_load_file(struct param_list *params);
void _sys_load_image(const char *name, unsigned i, unsigned x_mult);
void sys_load_image(struct param_list *params);
void sys_graphics_copy(struct param_list *params);
void sys_graphics_copy_masked(struct param_list *params);
void sys_graphics_copy_masked24(struct param_list *params);
void sys_graphics_fill_bg(struct param_list *params);
void sys_graphics_copy_swap(struct param_list *params);
void sys_graphics_swap_bg_fg(struct param_list *params);
void sys_graphics_compose(struct param_list *params);
void sys_graphics_blend(struct param_list *params);
void sys_graphics_blend_masked(struct param_list *params);
void sys_graphics_invert_colors(struct param_list *params);
void sys_graphics_copy_progressive(struct param_list *params);
void sys_wait(struct param_list *params);
void sys_set_text_colors_indexed(struct param_list *params);
void sys_set_text_colors_direct(struct param_list *params);
void sys_farcall(struct param_list *params);
void _sys_get_cursor_segment(unsigned x, unsigned y, uint32_t off);
void sys_get_cursor_segment(struct param_list *params);
void sys_menu_get_no(struct param_list *params);
void sys_check_input(struct param_list *params);
void sys_strlen(struct param_list *params);
void sys_get_time(struct param_list *params);

#endif // AI5_SDL2_SYS_H
