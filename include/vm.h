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

#ifndef AI5_VM_H
#define AI5_VM_H

#include <stdint.h>

#include "nulib.h"
#include "ai5/mes.h"

#include "game.h"
#include "memory.h"

#define VM_STACK_SIZE 1024
#define VM_MAX_PROCEDURES 150

struct archive_data;

struct vm_pointer {
	uint32_t ptr;
	uint8_t *code;
};

struct vm_mes_call {
	struct vm_pointer ip;
	char mes_name[13];
	struct vm_pointer procedures[VM_MAX_PROCEDURES];
};

struct vm {
	struct vm_pointer ip;
	unsigned scope_counter;
	// stack for expressions
	uint16_t stack_ptr;
	uint32_t stack[VM_STACK_SIZE];
	// stack for CALL statement
	uint16_t mes_call_stack_ptr;
	struct vm_mes_call mes_call_stack[128];
	// procedures defined with PROCD
	struct vm_pointer procedures[VM_MAX_PROCEDURES];
};
extern struct vm vm;

_Noreturn void _vm_error(const char *file, const char *func, int line, const char *fmt, ...);
#define VM_ERROR(fmt, ...) _vm_error(__FILE__, __func__, __LINE__, fmt "\n", ##__VA_ARGS__)

void vm_init(void);
void vm_exec(void);
void vm_peek(void);
void vm_load_file(struct archive_data *file, uint32_t offset);
void vm_load_mes(char *name);
void vm_call_procedure(unsigned no);

// input.c
void vm_delay(int ms);
uint32_t vm_get_ticks(void);

attr_warn_unused_result static inline bool vm_flag_is_on(enum game_flag flag)
{
	if (game->flags[flag] == FLAG_ALWAYS_ON)
		return true;
	return (mem_get_sysvar16(MES_SYS_VAR_FLAGS) & game->flags[flag]);
}

static inline void vm_flag_on(enum game_flag flag)
{
	if (game->flags[flag] == FLAG_ALWAYS_ON)
		return;
	mem_set_sysvar16(MES_SYS_VAR_FLAGS, mem_get_sysvar16(MES_SYS_VAR_FLAGS) | game->flags[flag]);
}

static inline void vm_flag_off(enum game_flag flag)
{
	mem_set_sysvar16(MES_SYS_VAR_FLAGS, mem_get_sysvar16(MES_SYS_VAR_FLAGS) & ~(game->flags[flag]));
}

typedef uint32_t vm_timer_t;

static inline void vm_timer_tick(vm_timer_t *timer, unsigned ms)
{
	uint32_t t = vm_get_ticks();
	uint32_t delta_t = t - *timer;
	if (delta_t < ms) {
		vm_delay(ms - delta_t);
		*timer = t + (ms - delta_t);
	} else {
		*timer = t;
	}
}

static inline bool vm_timer_tick_async(vm_timer_t *timer, unsigned ms)
{
	uint32_t t = vm_get_ticks();
	uint32_t delta_t = t - *timer;
	if (delta_t < ms)
		return false;
	*timer = t;
	return true;
}

static inline uint32_t vm_timer_create(void)
{
	return vm_get_ticks();
}

#endif // AI5_VM_H
