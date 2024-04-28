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

void sys_set_font_size(struct param_list *params);
void sys_display_number(struct param_list *params);
void sys_cursor_save_pos(struct param_list *params);
void sys_cursor(struct param_list *params);
void sys_anim(struct param_list *params);
void sys_savedata(struct param_list *params);
void sys_audio(struct param_list *params);
void sys_file(struct param_list *params);
void sys_load_file(struct param_list *params);
void sys_load_image(struct param_list *params);
void sys_palette_set(struct param_list *params);
void sys_palette_crossfade1(struct param_list *params);
void sys_palette_crossfade2(struct param_list *params);
void sys_palette(struct param_list *params);
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
void sys_graphics_classics(struct param_list *params);
void sys_wait(struct param_list *params);
void sys_set_text_colors_indexed(struct param_list *params);
void sys_set_text_colors_direct(struct param_list *params);
void sys_farcall(struct param_list *params);
void sys_get_cursor_segment(struct param_list *params);
void sys_get_cursor_segment_classics(struct param_list *params);
void sys_menu_get_no(struct param_list *params);
void sys_check_input(struct param_list *params);
void sys_strlen(struct param_list *params);
void sys_set_screen_surface(struct param_list *params);

#endif // AI5_SDL2_SYS_H
