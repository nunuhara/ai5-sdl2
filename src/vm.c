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
#include "game.h"
#include "gfx.h"
#include "input.h"
#include "memory.h"
#include "menu.h"
#include "vm_private.h"

struct vm vm = {0};
struct memory memory = {0};
struct memory_ptr memory_ptr = {0};
struct game *game = NULL;

void vm_print_state(void)
{
	sys_warning("ip = %08x\n", vm.ip.ptr);
	sys_warning("file = %s\n", asset_mes_name);

	for (int i = 0; i < 26; i++) {
		sys_warning("var16[%02d] = %04x\tvar32[%02d] = %08x\n", i, mem_get_var16(i),
				i, mem_get_var32(i));
	}
}

void vm_init(void)
{
	vm.ip.code = memory.file_data;
}

uint8_t vm_read_byte(void)
{
	return vm.ip.code[vm.ip.ptr++];
}

uint8_t vm_peek_byte(void)
{
	return vm.ip.code[vm.ip.ptr];
}

void vm_rewind_byte(void)
{
	vm.ip.ptr--;
}

uint16_t vm_read_word(void)
{
	uint16_t v = le_get16(vm.ip.code, vm.ip.ptr);
	vm.ip.ptr += 2;
	return v;
}

uint32_t vm_read_dword(void)
{
	uint32_t v = le_get32(vm.ip.code, vm.ip.ptr);
	vm.ip.ptr += 4;
	return v;
}

void vm_stack_push(uint32_t val)
{
	vm.stack[vm.stack_ptr++] = val;
	if (unlikely(vm.stack_ptr >= VM_STACK_SIZE))
		VM_ERROR("Stack overflow");
}

uint32_t vm_stack_pop(void)
{
	if (!vm.stack_ptr)
		VM_ERROR("Tried to pop from empty stack");
	return vm.stack[--vm.stack_ptr];
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
	memcpy(memory.file_data, file->data, file->size);
	archive_data_release(file);
}

static uint32_t vm_eval(void)
{
#define OPERATOR(op) { \
	uint32_t b = vm_stack_pop(); \
	uint32_t a = vm_stack_pop(); \
	vm_stack_push(a op b); \
}
	while (true) {
		uint8_t op = vm_read_byte();
		switch (mes_opcode_to_expr(op)) {
		case MES_EXPR_IMM:
			vm_stack_push(op);
			break;
		case MES_EXPR_VAR:
			vm_stack_push(mem_get_var16(vm_read_byte()));
			break;
		case MES_EXPR_ARRAY16_GET16: {
			uint32_t i = vm_stack_pop();
			uint8_t var = vm_read_byte();
			uint16_t v;
			if (var)
				v = le_get16(memory_raw + mem_get_var16(var - 1), i * 2);
			else
				v = mem_get_sysvar16(i);
			vm_stack_push(v);
			break;
		}
		case MES_EXPR_ARRAY16_GET8: {
			uint32_t i = vm_stack_pop();
			uint8_t var = vm_read_byte();
			uint8_t *src = memory_raw + mem_get_var16(var);
			vm_stack_push(src[i]);
			break;
		}
		case MES_EXPR_PLUS:
			OPERATOR(+);
			break;
		case MES_EXPR_MINUS:
			OPERATOR(-);
			break;
		case MES_EXPR_MUL:
			OPERATOR(*);
			break;
		case MES_EXPR_DIV:
			OPERATOR(/);
			break;
		case MES_EXPR_MOD:
			OPERATOR(%);
			break;
		case MES_EXPR_RAND:
			// FIXME? confirm this is correct
			if (ai5_target_game == GAME_DOUKYUUSEI) {
				uint16_t range = vm_read_word();
				vm_stack_push(rand() % range);
			} else {
				uint32_t range = vm_stack_pop();
				vm_stack_push(rand() % range);
			}
			break;
		case MES_EXPR_AND:
			OPERATOR(&&);
			break;
		case MES_EXPR_OR:
			OPERATOR(||);
			break;
		case MES_EXPR_BITAND:
			OPERATOR(&);
			break;
		case MES_EXPR_BITIOR:
			OPERATOR(|);
			break;
		case MES_EXPR_BITXOR:
			OPERATOR(^);
			break;
		case MES_EXPR_LT:
			OPERATOR(<);
			break;
		case MES_EXPR_GT:
			OPERATOR(>);
			break;
		case MES_EXPR_LTE:
			OPERATOR(<=);
			break;
		case MES_EXPR_GTE:
			OPERATOR(>=);
			break;
		case MES_EXPR_EQ:
			OPERATOR(==);
			break;
		case MES_EXPR_NEQ:
			OPERATOR(!=);
			break;
		case MES_EXPR_IMM16:
			vm_stack_push(vm_read_word());
			break;
		case MES_EXPR_IMM32:
			vm_stack_push(vm_read_dword());
			break;
		case MES_EXPR_REG16:
			vm_stack_push(mem_get_var4(vm_read_word()));
			break;
		case MES_EXPR_REG8:
			vm_stack_push(mem_get_var4(vm_stack_pop()));
			break;
		case MES_EXPR_ARRAY32_GET32: {
			uint32_t i = vm_stack_pop();
			uint8_t var = vm_read_byte();
			uint32_t v;
			if (var)
				v = le_get32(memory_raw + mem_get_var32(var - 1), i * 4);
			else
				v = mem_get_sysvar32(i);
			vm_stack_push(v);
			break;
		}
		case MES_EXPR_ARRAY32_GET16: {
			uint32_t i = vm_stack_pop();
			uint8_t var = vm_read_byte();
			uint16_t v = le_get16(memory_raw + mem_get_var32(var - 1), i * 2);
			vm_stack_push(v);
			break;
		}
		case MES_EXPR_ARRAY32_GET8: {
			uint32_t i = vm_stack_pop();
			uint8_t var = vm_read_byte();
			uint8_t *src = memory_raw + mem_get_var32(var - 1);
			vm_stack_push(src[i]);
			break;
		}
		case MES_EXPR_VAR32:
			vm_stack_push(mem_get_var32(vm_read_byte()));
			break;
		case MES_EXPR_END: {
			uint32_t r = vm_stack_pop();
			if (vm.stack_ptr > 0)
				VM_ERROR("Stack pointer is non-zero at end of expression");
			return r;
		}
		}
	}
#undef OPERATOR
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

uint32_t vm_expr_param(struct param_list *params, int i)
{
	if (i >= params->nr_params) {
		WARNING("Too few parameters");
		return 0;
	}
	if (params->params[i].type != MES_PARAM_EXPRESSION)
		VM_ERROR("Expected expression parameter %d / %d", i, params->nr_params);
	return params->params[i].val;
}

static void draw_text_eng(const char *text)
{
	const unsigned surface = mem_get_sysvar16(mes_sysvar16_dst_surface);
	uint16_t x = mem_get_sysvar16(mes_sysvar16_text_cursor_x) * game->x_mult;
	uint16_t y = mem_get_sysvar16(mes_sysvar16_text_cursor_y);
	while (*text) {
		int ch;
		bool zenkaku = SJIS_2BYTE(*text);
		text = sjis_char2unicode(text, &ch);
		unsigned char_size = gfx_text_size_char(ch);
		uint16_t next_x = x + char_size;
		// XXX: hack for spacing of zenkaku characters
		if (zenkaku) {
			x -= 2;
			next_x -= 4;
		}
		if (next_x > mem_get_sysvar16(mes_sysvar16_text_end_x) * game->x_mult) {
			y += mem_get_sysvar16(mes_sysvar16_line_space);
			x = mem_get_sysvar16(mes_sysvar16_text_start_x) * game->x_mult;
			next_x = x + char_size;
		}
		gfx_text_draw_glyph(x, y, surface, ch);
		x = next_x;
	}
	mem_set_sysvar16(mes_sysvar16_text_cursor_x, ((x+7u) & ~7u) / 8);
	mem_set_sysvar16(mes_sysvar16_text_cursor_y, y);
}

void vm_draw_text(const char *text)
{
#if 0
	string u = sjis_cstring_to_utf8(text, strlen(text));
	NOTICE("%s", u);
	string_free(u);
#endif
	if (vm_flag_is_on(FLAG_STRLEN)) {
		mem_set_var32(18, mem_get_var32(18) + strlen(text));
		return;
	}
	if (yuno_eng) {
		draw_text_eng(text);
		return;
	}
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
		uint16_t next_x = x + (zenkaku ? char_space / game->x_mult
				: char_space / (game->x_mult * 2));
		if (next_x > end_x) {
			y += line_space;
			x = start_x;
			next_x = x + (zenkaku ? char_space / game->x_mult
					: char_space / (game->x_mult * 2));
		}
		text = sjis_char2unicode(text, &ch);
		gfx_text_draw_glyph(x * game->x_mult, y, surface, ch);
		x = next_x;
	}
	mem_set_sysvar16(mes_sysvar16_text_cursor_x, x);
	mem_set_sysvar16(mes_sysvar16_text_cursor_y, y);
}

#define TXT_BUF_SIZE 4096

static void stmt_txt(void)
{
	size_t str_i = 0;
	char str[TXT_BUF_SIZE];

	uint8_t c;
	while ((c = vm_peek_byte())) {
		if (unlikely(!mes_char_is_zenkaku(c))) {
			if (!yuno_eng)
				WARNING("Invalid byte in TXT statement: %02x", (unsigned)c);
			goto unterminated;
		}
		str[str_i++] = vm_read_byte();
		str[str_i++] = vm_read_byte();
	}
	vm_read_byte();
unterminated:
	str[str_i] = 0;
	vm_draw_text(str);
}

static void stmt_str(void)
{
	size_t str_i = 0;
	char str[TXT_BUF_SIZE];

	uint8_t c;
	while ((c = vm_peek_byte())) {
		if (unlikely(!mes_char_is_hankaku(c))) {
			if (!yuno_eng)
				WARNING("Invalid byte in STR statement: %02x", (unsigned)c);
			goto unterminated;
		}
		str[str_i++] = c;
		vm_read_byte();
	}
	vm_read_byte();
unterminated:
	str[str_i] = 0;
	vm_draw_text(str);
}

static void stmt_setrbc(void)
{
	uint16_t i = vm_read_word();
	do {
		mem_set_var4(i++, vm_eval());
	} while (vm_read_byte());
}

static void stmt_setv(void)
{
	uint8_t i = vm_read_byte();
	do {
		mem_set_var16(i++, vm_eval());
	} while (vm_read_byte());
}

static void stmt_setrbe(void)
{
	uint32_t i = vm_eval();
	do {
		mem_set_var4(i++, vm_eval());
	} while (vm_read_byte());
}

static void stmt_setrd(void)
{
	uint32_t i = vm_read_byte();
	do {
		mem_set_var32(i++, vm_eval());
	} while (vm_read_byte());
}

static void stmt_setac(void)
{
	uint32_t i = vm_eval();
	uint8_t var = vm_read_byte();
	uint8_t *dst = memory_raw + mem_get_var16(var) + i;

	do {
		*dst++ = vm_eval();
	} while (vm_read_byte());
}

static void stmt_seta_at(void)
{
	uint32_t i = vm_eval();
	uint8_t var = vm_read_byte();
	uint8_t *dst = memory_ptr.system_var16;
	if (var)
		dst = memory_raw + mem_get_var16(var - 1);

	do {
		le_put16(dst, i * 2, vm_eval());
		i++;
	} while (vm_read_byte());
}

static void stmt_setad(void)
{
	uint32_t i = vm_eval();
	uint8_t var = vm_read_byte();
	uint8_t *dst = memory_ptr.system_var32;
	if (var)
		dst = memory_raw + mem_get_var32(var - 1);

	do {
		le_put32(dst, i * 4, vm_eval());
		i++;
	} while (vm_read_byte());
}

static void stmt_setaw(void)
{
	uint32_t i = vm_eval();
	uint8_t var = vm_read_byte();
	uint8_t *dst = memory_raw + mem_get_var32(var - 1);

	do {
		le_put16(dst, i * 2, vm_eval());
		i++;
	} while (vm_read_byte());
}

static void stmt_setab(void)
{
	uint32_t i = vm_eval();
	uint8_t var = vm_read_byte();
	uint8_t *dst = memory_raw + mem_get_var32(var - 1) + i;

	do {
		*dst++ = vm_eval();
	} while (vm_read_byte());
}

static void stmt_jz(void)
{
	uint32_t val = vm_eval();
	uint32_t ptr = vm_read_dword();
	if (val == 1)
		return;
	vm.ip.ptr = ptr;
}

static void stmt_jmp(void)
{
	vm.ip.ptr = le_get32(vm.ip.code, vm.ip.ptr);
}

static void stmt_sys(void)
{
	uint32_t no = vm_eval();
	struct param_list params = {0};
	read_params(&params);

	if (unlikely(no >= GAME_MAX_SYS))
		VM_ERROR("Invalid System call number: %u", no);
	if (unlikely(!game->sys[no]))
		VM_ERROR("System.function[%u] not implemented", no);

	game->sys[no](&params);
}

static void stmt_goto(void)
{
	struct param_list params = {0};
	read_params(&params);

	vm_string_param(&params, 0);
	vm_load_mes(params.params[0].str);

	vm_flag_on(FLAG_RETURN);
}

static void stmt_call(void)
{
	struct param_list params = {0};
	read_params(&params);
	vm_string_param(&params, 0);

	// save current VM state
	struct vm_mes_call *frame = &vm.mes_call_stack[vm.mes_call_stack_ptr++];
	frame->ip = vm.ip;
	memcpy(frame->mes_name, mem_mes_name(), 12);
	frame->mes_name[12] = '\0';
	if (game->call_saves_procedures)
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
		if (game->call_saves_procedures)
			memcpy(vm.procedures, frame->procedures, sizeof(vm.procedures));
		vm_load_mes(frame->mes_name);
		frame->ip.ptr = 0;
		frame->ip.code = NULL;
		frame->mes_name[0] = '\0';
	}
}

static void stmt_menui(void)
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

static void stmt_proc(void)
{
	struct param_list params = {0};
	read_params(&params);
	vm_call_procedure(vm_expr_param(&params, 0));
}

static void stmt_util(void)
{
	struct param_list params = {0};
	read_params(&params);
	if (unlikely(params.nr_params < 1))
		VM_ERROR("Util without parameters");
	uint32_t no = vm_expr_param(&params, 0);
	if (unlikely(no >= 256))
		VM_ERROR("Invalid Util number: %u", no);
	if (unlikely(!game->util[no]))
		VM_ERROR("Util.function[%u] not implemented", no);
	game->util[no](&params);
}

static void stmt_line(void)
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

static void stmt_procd(void)
{
	uint32_t i = vm_eval();
	if (unlikely(i >= VM_MAX_PROCEDURES))
		VM_ERROR("Invalid procedure number: %d", i);
	vm.procedures[i] = vm.ip;
	vm.procedures[i].ptr += 4;
	vm.ip.ptr = vm_read_dword();
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
	switch ((uint8_t)mes_opcode_to_stmt(op)) {
	case MES_STMT_END:     return false;
	case MES_STMT_TXT:     stmt_txt(); break;
	case MES_STMT_STR:     stmt_str(); break;
	case MES_STMT_SETRBC:  stmt_setrbc(); break;
	case MES_STMT_SETV:    stmt_setv(); break;
	case MES_STMT_SETRBE:  stmt_setrbe(); break;
	case MES_STMT_SETAC:   stmt_setac(); break;
	case MES_STMT_SETA_AT: stmt_seta_at(); break;
	case MES_STMT_SETAD:   stmt_setad(); break;
	case MES_STMT_SETAW:   stmt_setaw(); break;
	case MES_STMT_SETAB:   stmt_setab(); break;
	case MES_STMT_JZ:      stmt_jz(); break;
	case MES_STMT_JMP:     stmt_jmp(); break;
	case MES_STMT_SYS:     stmt_sys(); break;
	case MES_STMT_GOTO:    stmt_goto(); break;
	case MES_STMT_CALL:    stmt_call(); break;
	case MES_STMT_MENUI:   stmt_menui(); break;
	case MES_STMT_PROC:    stmt_proc(); break;
	case MES_STMT_UTIL:    stmt_util(); break;
	case MES_STMT_LINE:    stmt_line(); break;
	case MES_STMT_PROCD:   stmt_procd(); break;
	case MES_STMT_MENUS:   menu_exec(); break;
	case MES_STMT_SETRD:   stmt_setrd(); break;
	case MES_CODE_INVALID:
		vm_rewind_byte();
		if (mes_char_is_hankaku(op))
			stmt_str();
		else
			stmt_txt();
		break;
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
