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

#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#include "nulib.h"
#include "nulib/little_endian.h"
#include "nulib/port.h"
#include "nulib/string.h"
#include "nulib/utfsjis.h"
#include "ai5/arc.h"
#include "ai5/game.h"
#include "ai5/mes.h"

#include "ai5.h"
#include "anim.h"
#include "asset.h"
#include "audio.h"
#include "backlog.h"
#include "debug.h"
#include "game.h"
#include "gfx.h"
#include "input.h"
#include "memory.h"
#include "menu.h"
#include "texthook.h"
#include "vm_private.h"

struct vm vm = {0};
struct memory memory = {0};
struct memory_ptr memory_ptr = {0};
struct game *game = NULL;

void _vm_error(const char *file, const char *func, int line, const char *fmt, ...)
{
	va_list ap;
	char buf[1024];
	va_start(ap, fmt);
	vsnprintf(buf, 1024, fmt, ap);
	va_end(ap);

	sys_warning("ERROR(%s:%s:%d): %s", file, func, line, buf);
	gfx_error_message(buf);

	if (debug_on_error)
		dbg_repl();
	sys_exit(1);
}

void vm_init(void)
{
	vm.ip.code = memory.file_data;
}

static uint8_t vm_read_byte(void)
{
	uint8_t c = vm.ip.code[vm.ip.ptr++];
	if (vm_flag_is_on(FLAG_LOG))
		backlog_push_byte(c);
	return c;
}

static uint8_t vm_peek_byte(void)
{
	return vm.ip.code[vm.ip.ptr];
}

static void vm_rewind_byte(void)
{
	vm.ip.ptr--;
}

static uint16_t vm_read_word(void)
{
	uint16_t v = le_get16(vm.ip.code, vm.ip.ptr);
	vm.ip.ptr += 2;
	return v;
}

static uint32_t vm_read_dword(void)
{
	uint32_t v = le_get32(vm.ip.code, vm.ip.ptr);
	vm.ip.ptr += 4;
	return v;
}

static void vm_stack_push(uint32_t val)
{
	vm.stack[vm.stack_ptr++] = val;
	if (unlikely(vm.stack_ptr >= VM_STACK_SIZE))
		VM_ERROR("Stack overflow");
}

static uint32_t vm_stack_pop(void)
{
	if (!vm.stack_ptr)
		VM_ERROR("Tried to pop from empty stack");
	return vm.stack[--vm.stack_ptr];
}

void vm_load_file(struct archive_data *file, uint32_t offset)
{
	dbg_invalidate(offsetof(struct memory, file_data) + offset, file->size);
	memcpy(memory.file_data + offset, file->data, file->size);
	dbg_load_file(file->name, offsetof(struct memory, file_data) + offset, file->size);
}

void vm_load_data_file(const char *name, uint32_t offset)
{
	struct archive_data *data = asset_data_load(name);
	if (!data) {
		WARNING("Failed to read data file \"%s\"", name);
		return;
	}
	if (offset + data->size > MEMORY_FILE_DATA_SIZE) {
		WARNING("Tried to read file beyond end of buffer");
		goto end;
	}
	vm_load_file(data, offset);
end:
	archive_data_release(data);
}

void vm_load_mes(char *name)
{
	strcpy(mem_mes_name(), name);
	for (int i = 0; memory_raw[i]; i++) {
		memory_raw[i] = toupper(memory_raw[i]);
	}
	struct archive_data *file = asset_mes_load(name);
	if (!file)
		VM_ERROR("Failed to load MES file \"%s\"", name);
	vm_load_file(file, 0);
	archive_data_release(file);
}

void vm_expr_var16(void)
{
	vm_stack_push(mem_get_var16(vm_read_byte()));
}

void vm_expr_ptr16_get16(void)
{
	int32_t i = vm_stack_pop();
	uint8_t var = vm_read_byte();
	uint8_t *src = memory_ptr.system_var16;
	if (var)
		src = memory_raw + mem_get_var16(var - 1);
	if (unlikely(!mem_ptr_valid(src + i * 2, 2)))
		VM_ERROR("Out of bounds read");
	vm_stack_push(le_get16(src, i * 2));
}

void vm_expr_ptr16_get8(void)
{
	uint32_t i = vm_stack_pop();
	uint8_t var = vm_read_byte();
	uint8_t *src = memory_raw + mem_get_var16(var);
	if (unlikely(!mem_ptr_valid(src + i, 1)))
		VM_ERROR("Out of bounds read");
	vm_stack_push(src[i]);
}

#define VM_EXPR_OPERATOR(name, op) \
	void name(void) \
	{ \
		uint32_t b = vm_stack_pop(); \
		uint32_t a = vm_stack_pop(); \
		vm_stack_push(a op b); \
	}
VM_EXPR_OPERATOR(vm_expr_plus,   +)
VM_EXPR_OPERATOR(vm_expr_minus,  -)
VM_EXPR_OPERATOR(vm_expr_mul,    *)
VM_EXPR_OPERATOR(vm_expr_div,    /)
VM_EXPR_OPERATOR(vm_expr_mod,    %)
VM_EXPR_OPERATOR(vm_expr_and,   &&)
VM_EXPR_OPERATOR(vm_expr_or,    ||)
VM_EXPR_OPERATOR(vm_expr_bitand, &)
VM_EXPR_OPERATOR(vm_expr_bitior, |)
VM_EXPR_OPERATOR(vm_expr_bitxor, ^)
VM_EXPR_OPERATOR(vm_expr_lt,     <)
VM_EXPR_OPERATOR(vm_expr_gt,     >)
VM_EXPR_OPERATOR(vm_expr_lte,   <=)
VM_EXPR_OPERATOR(vm_expr_gte,   >=)
VM_EXPR_OPERATOR(vm_expr_eq,    ==)
VM_EXPR_OPERATOR(vm_expr_neq,   !=)
#undef VM_EXPR_OPERATOR

void vm_expr_rand(void)
{
	uint32_t range = vm_stack_pop();
	vm_stack_push(rand() % range);
}

// doukyuusei
void vm_expr_rand_with_imm_range(void)
{
	uint16_t range = vm_read_word();
	vm_stack_push(rand() % range);
}

void vm_expr_imm16(void)
{
	vm_stack_push(vm_read_word());
}

void vm_expr_imm32(void)
{
	vm_stack_push(vm_read_dword());
}

void vm_expr_cflag(void)
{
	vm_stack_push(mem_get_var4(vm_read_word()));
}

void vm_expr_eflag(void)
{
	vm_stack_push(mem_get_var4(vm_stack_pop()));
}

void vm_expr_ptr32_get32(void)
{
	int32_t i = vm_stack_pop();
	uint8_t var = vm_read_byte();
	uint8_t *src = memory_ptr.system_var32;
	if (var)
		src = memory_raw + mem_get_var32(var - 1);
	if (unlikely(!mem_ptr_valid(src + i * 4, 4)))
		VM_ERROR("Out of bounds read");
	vm_stack_push(le_get32(src, i * 4));

}

void vm_expr_ptr32_get16(void)
{
	int32_t i = vm_stack_pop();
	uint8_t var = vm_read_byte();
	uint8_t *src = memory_raw + mem_get_var32(var - 1);
	if (unlikely(!mem_ptr_valid(src + i * 2, 2)))
		VM_ERROR("Out of bounds read");
	vm_stack_push(le_get16(src, i * 2));
}

void vm_expr_ptr32_get8(void)
{
	int32_t i = vm_stack_pop();
	uint8_t var = vm_read_byte();
	uint8_t *src = memory_raw + mem_get_var32(var - 1);
	if (unlikely(!mem_ptr_valid(src + i, 1)))
		VM_ERROR("Out of bounds read");
	vm_stack_push(src[i]);
}

void vm_expr_var32(void)
{
	vm_stack_push(mem_get_var32(vm_read_byte()));
}

static uint32_t vm_expr_end(void)
{
	uint32_t r = vm_stack_pop();
	if (vm.stack_ptr > 0)
		VM_ERROR("Stack pointer is non-zero at end of expression");
	return r;
}

static uint32_t vm_eval(void)
{
	while (true) {
		uint8_t op = vm_read_byte();
		if (op == 0xff)
			return vm_expr_end();
		if (game->expr_op[op])
			game->expr_op[op]();
		else
			vm_stack_push(op);
	}
	return 0;
}

static void read_string_param(char *str)
{
	size_t str_i = 0;
	uint8_t c;
	for (str_i = 0; (c = vm_read_byte()); str_i++) {
		if (unlikely(str_i >= STRING_PARAM_SIZE))
			VM_ERROR("String parameter overflowed buffer");
		str[str_i] = c;
	}
	str[str_i] = '\0';
}

void read_params(struct param_list *params)
{
	int i;
	uint8_t b;
	for (i = 0; (b = vm_read_byte()); i++) {
		if (unlikely(i >= MAX_PARAMS))
			VM_ERROR("Too many parameters");
		params->params[i].type = b;
		if (b == MES_PARAM_EXPRESSION) {
			params->params[i].val = vm_eval();
		} else {
			read_string_param(params->params[i].str);
		}
	}
	params->nr_params = i;
}

char *vm_string_param(struct param_list *params, int i)
{
	if (params->nr_params < i)
		VM_ERROR("Too few parameters");
	if (params->params[i].type != MES_PARAM_STRING)
		VM_ERROR("Expected string parameter");
	return params->params[i].str;
}

void vm_draw_text(const char *text)
{
	const uint16_t surface = mem_get_sysvar16(mes_sysvar16_dst_surface);
	const uint16_t start_x = mem_get_sysvar16(mes_sysvar16_text_start_x);
	const uint16_t end_x = mem_get_sysvar16(mes_sysvar16_text_end_x);
	const uint16_t char_space = mem_get_sysvar16(mes_sysvar16_char_space);
	const uint16_t line_space = mem_get_sysvar16(mes_sysvar16_line_space);
	uint16_t x = mem_get_sysvar16(mes_sysvar16_text_cursor_x);
	uint16_t y = mem_get_sysvar16(mes_sysvar16_text_cursor_y);
	while (*text) {
		int ch;
		bool zenkaku = SJIS_2BYTE(*text);
		uint16_t next_x = x + (zenkaku ? char_space : char_space / 2);
		if (next_x > end_x) {
			y += line_space;
			x = start_x;
			next_x = x + (zenkaku ? char_space : char_space / 2);
		}
		text = sjis_char2unicode(text, &ch);
		gfx_text_draw_glyph(x, y, surface, ch);
		x = next_x;
	}
	mem_set_sysvar16(mes_sysvar16_text_cursor_x, x);
	mem_set_sysvar16(mes_sysvar16_text_cursor_y, y);
}

#define TXT_BUF_SIZE 4096

void handle_text(void(*read_text)(char*), void(*draw_text)(const char*), bool with_op)
{
	if (vm_flag_is_on(FLAG_LOG_ENABLE) && vm_flag_is_on(FLAG_LOG_TEXT)) {
		backlog_prepare();
		vm_flag_on(FLAG_LOG);
		if (with_op)
			backlog_push_byte(mes_code_tables.stmt_op_to_int[MES_STMT_STR]);
	}

	char str[TXT_BUF_SIZE];
	read_text(str);
	texthook_push(str);
	draw_text(str);

	if (vm_flag_is_on(FLAG_LOG_ENABLE) && vm_flag_is_on(FLAG_LOG_TEXT))
		vm_flag_off(FLAG_LOG);
}

static void read_zenkaku(char *str)
{
	uint8_t c;
	int str_i = 0;
	while ((c = vm_peek_byte())) {
		if (unlikely(!mes_char_is_zenkaku(c)))
			goto unterminated;
		str[str_i++] = vm_read_byte();
		str[str_i++] = vm_read_byte();
	}
	vm_read_byte();
unterminated:
	str[str_i] = 0;
}

static void read_hankaku(char *str)
{
	uint8_t c;
	int str_i = 0;
	while ((c = vm_peek_byte())) {
		if (unlikely(!mes_char_is_hankaku(c)))
			goto unterminated;
		str[str_i++] = c;
		vm_read_byte();
	}
	vm_read_byte();
unterminated:
	str[str_i] = 0;
}

static void _vm_stmt_txt(bool with_op)
{
	handle_text(read_zenkaku, game->draw_text_zen ? game->draw_text_zen : vm_draw_text,
			with_op);
}

void vm_stmt_txt(void)
{
	_vm_stmt_txt(true);
}

static void _vm_stmt_str(bool with_op)
{
	handle_text(read_hankaku, game->draw_text_han ? game->draw_text_han : vm_draw_text,
			with_op);
}

void vm_stmt_str(void)
{
	_vm_stmt_str(true);
}

void vm_stmt_set_cflag(void)
{
	uint16_t i = vm_read_word();
	do {
		if (unlikely(!mem_ptr_valid(memory_raw + MEMORY_MES_NAME_SIZE + i, 1)))
			VM_ERROR("Out of bounds write");
		mem_set_var4(i++, vm_eval());
	} while (vm_read_byte());
}

void vm_stmt_set_cflag_4bit_wrap(void)
{
	uint16_t i = vm_read_word();
	do {
		if (unlikely(!mem_ptr_valid(memory_raw + MEMORY_MES_NAME_SIZE + i, 1)))
			VM_ERROR("Out of bounds write");
		mem_set_var4(i++, vm_eval() & 0xf);
	} while (vm_read_byte());
}

void vm_stmt_set_cflag_4bit_saturate(void)
{
	uint16_t i = vm_read_word();
	do {
		if (unlikely(!mem_ptr_valid(memory_raw + MEMORY_MES_NAME_SIZE + i, 1)))
			VM_ERROR("Out of bounds write");
		mem_set_var4(i++, min(vm_eval(), 0xf));
	} while (vm_read_byte());
}

void vm_stmt_set_var16(void)
{
	uint8_t i = vm_read_byte();
	do {
		if (unlikely(!mem_ptr_valid(memory_ptr.var16 + i * 2, 2)))
			VM_ERROR("Out of bounds write");
		mem_set_var16(i++, vm_eval());
	} while (vm_read_byte());
}

void vm_stmt_set_eflag(void)
{
	int32_t i = vm_eval();
	do {
		if (unlikely(!mem_ptr_valid(memory_raw + MEMORY_MES_NAME_SIZE + i, 1)))
			VM_ERROR("Out of bounds write");
		mem_set_var4(i++, vm_eval());
	} while (vm_read_byte());
}

void vm_stmt_set_eflag_4bit_wrap(void)
{
	int32_t i = vm_eval();
	do {
		if (unlikely(!mem_ptr_valid(memory_raw + MEMORY_MES_NAME_SIZE + i, 1)))
			VM_ERROR("Out of bounds write");
		mem_set_var4(i++, vm_eval() & 0xf);
	} while (vm_read_byte());
}

void vm_stmt_set_eflag_4bit_saturate(void)
{
	int32_t i = vm_eval();
	do {
		if (unlikely(!mem_ptr_valid(memory_raw + MEMORY_MES_NAME_SIZE + i, 1)))
			VM_ERROR("Out of bounds write");
		mem_set_var4(i++, min(vm_eval(), 0xf));
	} while (vm_read_byte());
}

void vm_stmt_set_var32(void)
{
	int32_t i = vm_read_byte();

	do {
		if (unlikely(!mem_ptr_valid(memory_ptr.var32 + i * 4, 4)))
			VM_ERROR("Out of bounds write");
		mem_set_var32(i++, vm_eval());
	} while (vm_read_byte());
}

void vm_stmt_ptr16_set8(void)
{
	int32_t i = vm_eval();
	uint8_t var = vm_read_byte();
	uint8_t *dst = memory_raw + mem_get_var16(var) + i;

	do {
		if (unlikely(!mem_ptr_valid(dst, 1)))
			VM_ERROR("Out of bounds write");
		*dst++ = vm_eval();
	} while (vm_read_byte());
}

void vm_stmt_ptr16_set16(void)
{
	int32_t i = vm_eval();
	uint8_t var = vm_read_byte();
	uint8_t *dst = memory_ptr.system_var16;
	if (var)
		dst = memory_raw + mem_get_var16(var - 1);

	do {
		if (unlikely(!mem_ptr_valid(dst + i * 2, 2)))
			VM_ERROR("Out of bounds write");
		le_put16(dst, i * 2, vm_eval());
		i++;
	} while (vm_read_byte());
}

void vm_stmt_ptr32_set32(void)
{
	int32_t i = vm_eval();
	uint8_t var = vm_read_byte();
	uint8_t *dst = memory_ptr.system_var32;
	if (var)
		dst = memory_raw + mem_get_var32(var - 1);

	do {
		if (unlikely(!mem_ptr_valid(dst + i*4, 4)))
			VM_ERROR("Out of bounds write");
		le_put32(dst, i * 4, vm_eval());
		i++;
	} while (vm_read_byte());
}

void vm_stmt_ptr32_set16(void)
{
	int32_t i = vm_eval();
	uint8_t var = vm_read_byte();
	uint8_t *dst = memory_raw + mem_get_var32(var - 1);

	do {
		if (unlikely(!mem_ptr_valid(dst + i*2, 2)))
			VM_ERROR("Out of bounds write");
		le_put16(dst, i * 2, vm_eval());
		i++;
	} while (vm_read_byte());
}

void vm_stmt_ptr32_set8(void)
{
	int32_t i = vm_eval();
	uint8_t var = vm_read_byte();
	uint8_t *dst = memory_raw + mem_get_var32(var - 1) + i;

	do {
		if (unlikely(!mem_ptr_valid(dst, 1)))
			VM_ERROR("Out of bounds write");
		*dst++ = vm_eval();
	} while (vm_read_byte());
}

void vm_stmt_jz(void)
{
	uint32_t val = vm_eval();
	uint32_t ptr = vm_read_dword();
	if (val == 1)
		return;
	vm.ip.ptr = ptr;
}

void vm_stmt_jmp(void)
{
	vm.ip.ptr = le_get32(vm.ip.code, vm.ip.ptr);
}

void vm_stmt_sys(void)
{
	if (vm_flag_is_on(FLAG_LOG_ENABLE) && vm_flag_is_on(FLAG_LOG_SYS)) {
		vm_flag_on(FLAG_LOG);
		backlog_push_byte(mes_code_tables.stmt_op_to_int[MES_STMT_SYS]);
	}

	uint32_t no = vm_eval();
	struct param_list params = {0};
	read_params(&params);

	if (unlikely(no >= GAME_MAX_SYS))
		VM_ERROR("Invalid System call number: %u", no);
	if (unlikely(!game->sys[no]))
		VM_ERROR("System.function[%u] not implemented", no);

	game->sys[no](&params);

	if (vm_flag_is_on(FLAG_LOG_ENABLE) && vm_flag_is_on(FLAG_LOG_SYS))
		vm_flag_off(FLAG_LOG);
}

void vm_stmt_mesjmp(void)
{
	struct param_list params = {0};
	read_params(&params);

	vm_load_mes(vm_string_param(&params, 0));

	vm_flag_on(FLAG_RETURN);
}

static void _vm_stmt_mescall(bool save_procedures)
{
	struct param_list params = {0};
	read_params(&params);
	vm_string_param(&params, 0);

	// save current VM state
	struct vm_mes_call *frame = &vm.mes_call_stack[vm.mes_call_stack_ptr++];
	frame->ip = vm.ip;
	memcpy(frame->mes_name, mem_mes_name(), 12);
	frame->mes_name[12] = '\0';
	if (save_procedures)
		memcpy(frame->procedures, vm.procedures, sizeof(vm.procedures));

	// load and execute mes file
	vm.ip.ptr = 0;
	vm.ip.code = memory.file_data;
	vm_load_mes(params.params[0].str);
	vm_exec();

	// restore previous VM state
	frame = &vm.mes_call_stack[--vm.mes_call_stack_ptr];
	vm.ip.code = frame->ip.code;
	if (!vm_flag_is_on(FLAG_RETURN)) {
		vm.ip.ptr = frame->ip.ptr;
		if (save_procedures)
			memcpy(vm.procedures, frame->procedures, sizeof(vm.procedures));
		vm_load_mes(frame->mes_name);
		frame->ip.ptr = 0;
		frame->ip.code = NULL;
		frame->mes_name[0] = '\0';
	}
}

void vm_stmt_mescall(void)
{
	_vm_stmt_mescall(false);
}

// YU-NO
void vm_stmt_mescall_save_procedures(void)
{
	_vm_stmt_mescall(true);
}

void vm_stmt_defmenu(void)
{
	struct param_list params = {0};
	read_params(&params);
	uint32_t addr = vm_read_dword();
	menu_define(vm_expr_param(&params, 0), addr == vm.ip.ptr + 1);
	vm.ip.ptr = addr;
}

void vm_call_procedure(unsigned no)
{
	if (unlikely(no >= VM_MAX_PROCEDURES))
		VM_ERROR("Invalid procedure number: %u", no);

	struct vm_pointer saved_ip = vm.ip;
	vm.ip = vm.procedures[no];
	vm_exec();
	vm.ip = saved_ip;
}

void vm_stmt_call(void)
{
	bool flag_on = vm_flag_is_on(FLAG_PROC_CLEAR);
	vm_flag_off(FLAG_PROC_CLEAR);

	struct param_list params = {0};
	read_params(&params);
	vm_call_procedure(vm_expr_param(&params, 0));

	if (flag_on)
		vm_flag_on(FLAG_PROC_CLEAR);
}

void vm_stmt_util(void)
{
	struct param_list params = {0};
	read_params(&params);
	if (unlikely(params.nr_params < 1))
		VM_ERROR("Util without parameters");
	uint32_t no = vm_expr_param(&params, 0);
	if (unlikely(no >= GAME_MAX_UTIL))
		VM_ERROR("Invalid Util number: %u", no);
	if (unlikely(!game->util[no]))
		VM_ERROR("Util.function[%u] not implemented", no);
	game->util[no](&params);
}

void vm_stmt_line(void)
{
	// FIXME: is this correct?
	if (vm_read_byte())
		return;

	uint16_t start_x = mem_get_sysvar16(mes_sysvar16_text_start_x);
	uint16_t cur_y = mem_get_sysvar16(mes_sysvar16_text_cursor_y);
	uint16_t line_space = mem_get_sysvar16(mes_sysvar16_line_space);
	mem_set_sysvar16(mes_sysvar16_text_cursor_x, start_x);
	mem_set_sysvar16(mes_sysvar16_text_cursor_y, cur_y + line_space);
}

void vm_stmt_defproc(void)
{
	uint32_t i = vm_eval();
	if (unlikely(i >= VM_MAX_PROCEDURES))
		VM_ERROR("Invalid procedure number: %d", i);
	vm.procedures[i] = vm.ip;
	vm.procedures[i].ptr += 4;
	vm.ip.ptr = vm_read_dword();
}

void vm_stmt_menuexec(void)
{
	menu_exec();
}

bool vm_exec_statement(void)
{
#if 0
	printf("%08x: ", vm.ip.ptr);
	struct mes_statement *stmt = mes_parse_statement(vm.ip.code + vm.ip.ptr, 2048);
	mes_statement_print(stmt, port_stdout());
	mes_statement_free(stmt);
#endif

	uint8_t op = vm_read_byte();
retry:
	if (unlikely(!game->stmt_op[op])) {
		if (!op)
			return false;
		if (op == MES_STMT_BREAKPOINT) {
			op = dbg_handle_breakpoint((vm.ip.code + vm.ip.ptr - 1) - memory_raw);
			goto retry;
		}
		vm_rewind_byte();
		if (mes_char_is_hankaku(op))
			_vm_stmt_str(false);
		else
			_vm_stmt_txt(false);
	} else {
		game->stmt_op[op]();
	}
	return true;
}

void vm_peek(void)
{
	handle_events();
	anim_execute();
#ifdef USE_SDL_MIXER
	audio_update();
#endif
	if (game->update)
		game->update();
	gfx_update();
}

void vm_exec(void)
{
	vm.scope_counter++;
	while (true) {
		if (vm_flag_is_on(FLAG_RETURN)) {
			if (vm.scope_counter != 1)
				break;
			vm_flag_off(FLAG_RETURN);
			vm.ip.ptr = 0;
		}
		if (!vm_exec_statement())
			break;
		vm_peek();
	}
	vm.scope_counter--;
}
