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
	char mes_name[32];
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

void _vm_break(const char *file, const char *func, int line, const char *fmt, ...);
_Noreturn void _vm_error(const char *file, const char *func, int line, const char *fmt, ...);
#define VM_BREAK(fmt, ...) _vm_break(__FILE__, __func__, __LINE__, fmt "\n", ##__VA_ARGS__)
#define VM_ERROR(fmt, ...) _vm_error(__FILE__, __func__, __LINE__, fmt "\n", ##__VA_ARGS__)

struct aiw_menu_entry {
	uint32_t cond_addr;
	uint32_t body_addr;
};

#define AIW_MAX_MENUS 5
#define AIW_MAX_MENU_ENTRIES 100

extern struct aiw_menu_entry aiw_menu_entries[AIW_MAX_MENUS][AIW_MAX_MENU_ENTRIES];
extern unsigned aiw_menu_nr_entries[AIW_MAX_MENUS];

void vm_init(void);
void vm_exec(void);
void vm_exec_aiw(void);
void vm_peek(void);
void vm_load_file(struct archive_data *file, uint32_t offset);
void vm_load_mes(char *name);
void vm_call_procedure(unsigned no);

uint32_t vm_eval(void);
uint32_t vm_eval_aiw(void);

// generic stack operations
void vm_expr_plus(void);
void vm_expr_minus(void);
void vm_expr_minus_unsigned(void);
void vm_expr_mul(void);
void vm_expr_div(void);
void vm_expr_mod(void);
void vm_expr_and(void);
void vm_expr_or(void);
void vm_expr_bitand(void);
void vm_expr_bitior(void);
void vm_expr_bitxor(void);
void vm_expr_lt(void);
void vm_expr_gt(void);
void vm_expr_lte(void);
void vm_expr_gte(void);
void vm_expr_eq(void);
void vm_expr_neq(void);
void vm_expr_rand(void);
void vm_expr_rand_with_imm_range(void);
void vm_expr_imm16(void);
void vm_expr_imm32(void);

// flag/register/memory operations
void vm_expr_cflag(void);
void vm_expr_cflag_packed(void);
void vm_expr_eflag(void);
void vm_expr_eflag_packed(void);
void vm_expr_var16(void);
void vm_expr_var16_const16(void);
void vm_expr_var16_expr(void);
void vm_expr_var32(void);
void vm_expr_sysvar16_const16(void);
void vm_expr_sysvar16_expr(void);
void vm_expr_ptr16_get8(void);
void vm_expr_ptr16_get16(void);
void vm_expr_ptr32_get8(void);
void vm_expr_ptr32_get16(void);
void vm_expr_ptr32_get32(void);

void vm_stmt_txt_no_log(void);
void vm_stmt_txt_new_log(void);
void vm_stmt_txt_old_log(void);
void vm_stmt_str_no_log(void);
void vm_stmt_str_new_log(void);
void vm_unprefixed_txt_new_log(void);
void vm_unprefixed_str_new_log(void);

void vm_stmt_set_flag_const16(void);
void vm_stmt_set_flag_const16_aiw(void);
void vm_stmt_set_flag_const16_4bit_wrap(void);
void vm_stmt_set_flag_const16_4bit_saturate(void);
void vm_stmt_set_flag_expr(void);
void vm_stmt_set_flag_expr_aiw(void);
void vm_stmt_set_flag_expr_4bit_wrap(void);
void vm_stmt_set_flag_expr_4bit_saturate(void);
void vm_stmt_set_var16_const8(void);
void vm_stmt_set_var16_const16_aiw(void);
void vm_stmt_set_var16_expr_aiw(void);
void vm_stmt_set_var32_const8(void);
void vm_stmt_set_var32_const8_aiw(void);
void vm_stmt_set_sysvar16_const16_aiw(void);
void vm_stmt_set_sysvar16_expr_aiw(void);
void vm_stmt_ptr16_set8(void);
void vm_stmt_ptr16_set16(void);
void vm_stmt_ptr32_set32(void);
void vm_stmt_ptr32_set16(void);
void vm_stmt_ptr32_set8(void);
void vm_stmt_jz(void);
void vm_stmt_jmp(void);
void vm_stmt_sys(void);
void vm_stmt_sys_old_log(void);
void vm_stmt_mesjmp(void);
void vm_stmt_mesjmp_aiw(void);
void vm_stmt_mescall(void);
void vm_stmt_mescall_save_procedures(void);
void vm_stmt_mescall_aiw(void);
void vm_stmt_defmenu(void);
void vm_stmt_defmenu_aiw(void);
void vm_stmt_menuexec(void);
void vm_stmt_defproc(void);
void vm_stmt_call(void);
void vm_stmt_call_old_log(void);
void vm_stmt_util(void);
void vm_stmt_line(void);

#define DEFAULT_EXPR_OP \
	[0x80] = vm_expr_var16, \
	[0xa0] = vm_expr_ptr16_get16, \
	[0xc0] = vm_expr_ptr16_get8, \
	[0xe0] = vm_expr_plus, \
	[0xe1] = vm_expr_minus, \
	[0xe2] = vm_expr_mul, \
	[0xe3] = vm_expr_div, \
	[0xe4] = vm_expr_mod, \
	[0xe5] = vm_expr_rand, \
	[0xe6] = vm_expr_and, \
	[0xe7] = vm_expr_or, \
	[0xe8] = vm_expr_bitand, \
	[0xe9] = vm_expr_bitior, \
	[0xea] = vm_expr_bitxor, \
	[0xeb] = vm_expr_lt, \
	[0xec] = vm_expr_gt, \
	[0xed] = vm_expr_lte, \
	[0xee] = vm_expr_gte, \
	[0xef] = vm_expr_eq, \
	[0xf0] = vm_expr_neq, \
	[0xf1] = vm_expr_imm16, \
	[0xf2] = vm_expr_imm32, \
	[0xf3] = vm_expr_cflag, \
	[0xf4] = vm_expr_eflag, \
	[0xf5] = vm_expr_ptr32_get32, \
	[0xf6] = vm_expr_var32

#define DEFAULT_STMT_OP \
	[0x01] = vm_stmt_txt_new_log, \
	[0x02] = vm_stmt_str_new_log, \
	[0x03] = vm_stmt_set_flag_const16, \
	[0x04] = vm_stmt_set_var16_const8, \
	[0x05] = vm_stmt_set_flag_expr, \
	[0x06] = vm_stmt_ptr16_set8, \
	[0x07] = vm_stmt_ptr16_set16, \
	[0x08] = vm_stmt_ptr32_set32, \
	[0x09] = vm_stmt_jz, \
	[0x0a] = vm_stmt_jmp, \
	[0x0b] = vm_stmt_sys, \
	[0x0c] = vm_stmt_mesjmp, \
	[0x0d] = vm_stmt_mescall, \
	[0x0e] = vm_stmt_defmenu, \
	[0x0f] = vm_stmt_call, \
	[0x10] = vm_stmt_util, \
	[0x11] = vm_stmt_line, \
	[0x12] = vm_stmt_defproc, \
	[0x13] = vm_stmt_menuexec, \
	[0x14] = vm_stmt_set_var32_const8

// input.c
void vm_delay(int ms);
uint32_t vm_get_ticks(void);

attr_warn_unused_result static inline bool vm_flag_is_on(enum game_flag flag)
{
	if (game->flags[flag] == FLAG_ALWAYS_ON)
		return true;
	return (mem_get_sysvar16(mes_sysvar16_flags) & game->flags[flag]);
}

static inline void vm_flag_on(enum game_flag flag)
{
	if (game->flags[flag] == FLAG_ALWAYS_ON)
		return;
	mem_set_sysvar16(mes_sysvar16_flags, mem_get_sysvar16(mes_sysvar16_flags) | game->flags[flag]);
}

static inline void vm_flag_off(enum game_flag flag)
{
	mem_set_sysvar16(mes_sysvar16_flags, mem_get_sysvar16(mes_sysvar16_flags) & ~(game->flags[flag]));
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
