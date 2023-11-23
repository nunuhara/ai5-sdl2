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

#define MEMORY_MES_NAME_SIZE 128
#define MEMORY_VAR4_OFFSET MEMORY_MES_NAME_SIZE
#define MEMORY_FILE_DATA_SIZE 0x200000

#define MEMORY_MEM16_MAX_SIZE 0x2000
#define MEMORY_VAR4_MAX_SIZE 0x1000

/* 16-bit address space. We can't use this struct because the size of var4
 * varies by game. The inline functions below should be used to access values
 * in this area.
struct mem16 {
	char mes_name[MEMORY_MES_NAME_SIZE];
	uint8_t var4[4096];
	uint32_t system_var16_ptr;
	uint16_t var16[26];
	uint16_t system_var16[26];
	uint32_t var32[26];
	uint32_t system_var32[26];
	int8_t heap[3652];
} __attribute__((packed));
_Static_assert(sizeof(struct mem16) == 0x2000);
*/

// 32-bit address space
struct memory {
	uint8_t mem16[MEMORY_MEM16_MAX_SIZE];
	uint8_t file_data[MEMORY_FILE_DATA_SIZE];
	uint8_t palette[0x400];
	uint32_t menu_entry_addresses[200];
	uint32_t menu_entry_numbers[200];
};

struct memory_ptr {
	uint32_t *system_var16_ptr;
	uint16_t *var16;
	uint16_t *system_var16;
	uint32_t *var32;
	uint32_t *system_var32;
};

extern struct memory memory;
#define memory_raw ((uint8_t*)&memory)
extern struct memory_ptr memory_ptr;

static inline uint32_t memory_var4_size(void)
{
	// TODO: varies by game
	return 4096;
}

static inline uint16_t *memory_system_var16_ptr(void)
{
	return (uint16_t*)(memory_raw + *(memory_ptr.system_var16_ptr));
}

static inline void memory_system_var16_ptr_set(uint32_t ptr)
{
	uint32_t *p = (uint32_t*)(memory_raw + MEMORY_MES_NAME_SIZE + memory_var4_size());
	*p = ptr;
}

static inline char *memory_mes_name(void)
{
	return (char*)memory_raw;
}

static inline uint8_t *memory_var4(void)
{
	return (uint8_t*)(memory_raw + MEMORY_MES_NAME_SIZE);
}

static inline uint16_t *memory_var16(void)
{
	return memory_ptr.var16;
}

static inline uint16_t *memory_system_var16(void)
{
	return memory_ptr.system_var16;
}

static inline uint32_t *memory_var32(void)
{
	return memory_ptr.var32;
}

static inline uint32_t *memory_system_var32(void)
{
	return memory_ptr.system_var32;
}

void memory_init(void);
void memory_restore(void);

#endif // AI5_MEMORY_H
