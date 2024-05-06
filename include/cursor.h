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

#ifndef AI5_CURSOR_H
#define AI5_CURSOR_H

void cursor_init(const char *exe_path);
void cursor_load(unsigned no, unsigned nr_frames, unsigned *frame_time);
void cursor_unload(void);
void cursor_reload(void);
void cursor_show(void);
void cursor_hide(void);
void cursor_set_pos(unsigned x, unsigned y);
void cursor_get_pos(unsigned *x, unsigned *y);
void cursor_swap(void);

enum cursor_direction {
	CURSOR_DIR_NONE = 0,
	CURSOR_DIR_UP   = 1,
	CURSOR_DIR_DOWN = 2,
};

void cursor_set_direction(enum cursor_direction dir);
enum cursor_direction cursor_get_direction(void);

#endif // AI5_CURSOR_H
