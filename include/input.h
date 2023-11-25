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
	INPUT_NONE,
	INPUT_ACTIVATE,
	INPUT_CANCEL,
	INPUT_LEFT,
	INPUT_RIGHT,
	INPUT_UP,
	INPUT_DOWN,
};

void input_init(void);
void handle_events(void);
enum input_event_type input_keywait(void);
enum input_event_type input_poll(void);
bool input_check(enum input_event_type type);

extern uint32_t cursor_swap_event;

#endif // AI5_INPUT_H
