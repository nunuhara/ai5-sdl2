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

#ifndef AI5_MEMORY_H
#define AI5_MEMORY_H

#include <stdint.h>

// 16-bit address space
struct mem16 {
	char mes_name[128];
	uint8_t var4[4096]; // TODO: size varies by game
	uint16_t *system_var16_ptr;
	uint16_t var16[26];
	uint16_t system_var16[26];
	uint32_t var32[26];
	uint32_t system_var32[26];
	int8_t heap[3652];
};

// 32-bit address space
struct memory {
	struct mem16 mem16;
	uint8_t file_data[0x20000];
	uint8_t palette[0x400];
	uint32_t menu_entry_addresses[200];
	uint32_t menu_entry_numbers[200];
};

extern struct memory memory;
#define memory_raw ((uint8_t*)&memory)

void memory_init(void);

#endif // AI5_MEMORY_H
