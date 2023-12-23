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

#ifndef AI5_SDL2_GAME_H
#define AI5_SDL2_GAME_H

#include <stdint.h>

#define GAME_MAX_UTIL 256
#define GAME_MAX_SYS 32

struct param_list;

struct game {
	struct { uint16_t w, h; } surface_sizes[10];
	unsigned bpp;
	unsigned x_mult;
	uint32_t var4_size;
	uint32_t mem16_size;
	void (*mem_init)(void);
	void (*mem_restore)(void);
	void (*util[GAME_MAX_UTIL])(struct param_list*);
	void (*sys[GAME_MAX_SYS])(struct param_list*);
};

extern struct game game_isaku;
extern struct game game_shangrlia;
extern struct game game_yuno;

extern struct game *game;

#endif
