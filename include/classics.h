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

#ifndef AI5_SDL2_CLASSICS_H
#define AI5_SDL2_CLASSICS_H

struct param_list;

void classics_audio(struct param_list *params);
void classics_cursor(struct param_list *params);
void classics_anim(struct param_list *params);
void classics_savedata(struct param_list *params);
void classics_palette(struct param_list *params);
void classics_graphics(struct param_list *params);
void classics_get_cursor_segment(struct param_list *params);

void classics_get_text_colors(struct param_list *params);

#endif // AI5_SDL2_CLASSICS_H
