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

#ifndef AI5_SDL2_DUNGEON_H
#define AI5_SDL2_DUNGEON_H

#include <stdint.h>

enum dungeon_direction {
	DUNGEON_EAST  = 0,
	DUNGEON_NORTH = 1,
	DUNGEON_WEST  = 2,
	DUNGEON_SOUTH = 3,
};

enum dungeon_move_command {
	DUNGEON_MOVE_FORWARD  = 0,
	DUNGEON_MOVE_BACKWARD = 1,
	DUNGEON_ROTATE_LEFT   = 2,
	DUNGEON_ROTATE_RIGHT  = 3,
};

void dungeon_load(uint8_t *mp3, uint8_t *kabe1, uint8_t *kabe2, uint8_t *kabe3,
		uint8_t *kabe_pal, uint8_t *dun_a6);
void dungeon_set_pos(unsigned x, unsigned y, enum dungeon_direction dir);
void dungeon_get_pos(uint16_t *x, uint16_t *y, uint16_t *dir);
uint16_t dungeon_move(enum dungeon_move_command cmd);
void dungeon_draw(void);

#endif // AI5_SDL2_DUNGEON_H
