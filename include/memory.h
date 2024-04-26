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

#include "nulib/little_endian.h"

#define MEMORY_MES_NAME_SIZE 128
#define MEMORY_VAR4_OFFSET MEMORY_MES_NAME_SIZE
#define MEMORY_FILE_DATA_SIZE 0x630d40

#define MEMORY_MEM16_MAX_SIZE 0x2000
#define MEMORY_VAR4_MAX_SIZE 0x1000

#define MEMORY_MENU_ENTRY_MAX 200

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
	uint32_t menu_entry_addresses[MEMORY_MENU_ENTRY_MAX];
	uint32_t menu_entry_numbers[MEMORY_MENU_ENTRY_MAX];
};

struct memory_ptr {
	uint8_t *system_var16_ptr;
	uint8_t *var16;
	uint8_t *system_var16;
	uint8_t *var32;
	uint8_t *system_var32;
};

extern struct memory memory;
#define memory_raw ((uint8_t*)&memory)
#define memory_end (memory_raw+sizeof(struct memory))
extern struct memory_ptr memory_ptr;

static inline bool mem_ptr_valid(uint8_t *p, int size)
{
	return p >= memory_raw && (p + size) <= memory_end;
}

static inline void mem_set_sysvar16_ptr(uint32_t ptr)
{
	le_put32(memory_ptr.system_var16_ptr, 0, ptr);
}

static inline char *mem_mes_name(void)
{
	return (char*)memory_raw;
}

static inline uint8_t *mem_var4(void)
{
	return (uint8_t*)(memory_raw + MEMORY_MES_NAME_SIZE);
}

static inline uint8_t mem_get_var4(unsigned i)
{
	return (memory_raw + MEMORY_MES_NAME_SIZE)[i];
}

static inline void mem_set_var4(unsigned i, uint8_t v)
{
	(memory_raw + MEMORY_MES_NAME_SIZE)[i] = v & 0xf;
}

static inline uint16_t mem_get_var16(unsigned i)
{
	return le_get16(memory_ptr.var16, i*2);
}

static inline void mem_set_var16(unsigned i, uint16_t v)
{
	le_put16(memory_ptr.var16, i*2, v);
}

static inline uint16_t mem_get_sysvar16(unsigned i)
{
	return le_get16(memory_ptr.system_var16, i*2);
}

static inline void mem_set_sysvar16(unsigned i, uint16_t v)
{
	le_put16(memory_ptr.system_var16, i*2, v);
}

static inline uint32_t mem_get_var32(unsigned i)
{
	return le_get32(memory_ptr.var32, i*4);
}

static inline void mem_set_var32(unsigned i, uint32_t v)
{
	le_put32(memory_ptr.var32, i*4, v);
}

static inline uint32_t mem_get_sysvar32(unsigned i)
{
	return le_get32(memory_ptr.system_var32, i*4);
}

static inline void mem_set_sysvar32(unsigned i, uint32_t v)
{
	le_put32(memory_ptr.system_var32, i*4, v);
}

#endif // AI5_MEMORY_H
