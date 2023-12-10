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

#ifndef AI5_ANIM_H
#define AI5_ANIM_H

void anim_execute(void);
bool anim_running(void);
bool anim_stream_running(unsigned slot);
void anim_init_stream(unsigned slot, unsigned stream);
void anim_start(unsigned slot);
void anim_stop(unsigned slot);
void anim_halt(unsigned slot);
void anim_stop_all(void);
void anim_halt_all(void);
void anim_set_offset(unsigned slot, unsigned x, unsigned y);

#endif // AI5_ANIM_H
