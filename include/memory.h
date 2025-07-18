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

#define MEMORY_FILE_DATA_SIZE 0x630d40

#define MEMORY_MEM16_MAX_SIZE 0x2000
#define MEMORY_VAR4_MAX_SIZE 0x1000

#define MEMORY_MENU_ENTRY_MAX 200

#define MEMORY_BACKLOG_DATA_SIZE 2048
#define MEMORY_BACKLOG_NR_ENTRIES 64

/* 16-bit address space. We can't use this struct because the memory layout
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
	uint8_t backlog[MEMORY_BACKLOG_DATA_SIZE * MEMORY_BACKLOG_NR_ENTRIES];
	uint8_t map_data[52];
};

struct memory_ptr {
	uint8_t *mes_name;
	uint8_t *var4;
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
	return memory_ptr.var4;
}

static inline uint8_t mem_get_var4(unsigned i)
{
	return memory_ptr.var4[i];
}

static inline uint8_t mem_get_var4_packed(unsigned no)
{
	uint8_t flag = mem_get_var4(no/2);
	if (no % 2) {
		return flag & 0xf;
	}
	return flag >> 4;
}

static inline void mem_set_var4(unsigned i, uint8_t v)
{
	memory_ptr.var4[i] = v;
}

static inline void mem_set_var4_packed(unsigned no, uint8_t val)
{
	unsigned i = no / 2;
	uint8_t b = mem_get_var4(i);
	if (no % 2) {
		mem_set_var4(i, (b & 0xf0) | val);
	} else {
		mem_set_var4(i, (b & 0x0f) | (val << 4));
	}
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

static inline char *mem_get_cstring(uint32_t ptr)
{
	uint8_t *p = memory_raw + ptr;
	for (int i = 0; true; i++) {
		if (p >= memory_end)
			return NULL;
		if (!p[i])
			break;
	}
	return (char*)p;
}

#endif // AI5_MEMORY_H
