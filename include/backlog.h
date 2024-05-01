/* Copyright (C) 2024 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#ifndef AI5_BACKLOG_H
#define AI5_BACKLOG_H

#include <stdint.h>

void backlog_clear(void);
void backlog_prepare(void);
void backlog_commit(void);
unsigned backlog_count(void);
uint32_t backlog_get_pointer(unsigned no);
bool backlog_has_voice(unsigned no);
void backlog_set_has_voice(void);
void backlog_push_byte(uint8_t b);

#endif // AI5_BACKLOG_H
