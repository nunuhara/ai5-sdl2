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

#ifndef AI5_INPUT_H
#define AI5_INPUT_H

#include <stdint.h>

enum input_event_type {
	INPUT_NONE = -1,
	INPUT_ACTIVATE = 0,
	INPUT_CANCEL = 1,
	INPUT_UP = 2,
	INPUT_DOWN = 3,
	INPUT_LEFT = 4,
	INPUT_RIGHT = 5,
	INPUT_SHIFT = 6,
	INPUT_CTRL = 7,
};
#define INPUT_NR_INPUTS (INPUT_CTRL+1)

void input_init(void);
void handle_events(void);
bool input_down(enum input_event_type type);
void input_wait_until_up(enum input_event_type type);
void input_get_cursor_pos(int *x, int *y);

extern uint32_t cursor_swap_event;

#endif // AI5_INPUT_H
