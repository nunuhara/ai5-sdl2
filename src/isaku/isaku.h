/* Copyright (C) 2026 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#ifndef ISAKU_H
#define ISAKU_H

#include <stdbool.h>

union SDL_Event;

void isaku_builtin_se_play(const char *name);

void isaku_item_window_tick(void);
bool isaku_item_window_event(union SDL_Event *e);
void isaku_item_window_use_overlay(void);

bool isaku_item_window_is_enabled(void);
void isaku_item_window_create(void);
void isaku_item_window_toggle(void);
bool isaku_item_window_is_open(void);
void isaku_item_window_get_pos(int *x, int *y, int *w, int *h);
void isaku_item_window_get_cursor_pos(int *x, int *y);
void isaku_item_window_enable(void);
void isaku_item_window_disable(void);
void isaku_item_window_get_mouse_state(bool *lmb_down, bool *rmb_down);
void isaku_item_window_update(void);

#endif // ISAKU_H
