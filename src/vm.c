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

#include "nulib.h"
#include "nulib/little_endian.h"
#include "nulib/string.h"
#include "nulib/utfsjis.h"
#include "ai5/cg.h"
#include "ai5/game.h"
#include "ai5/mes.h"

#include "asset.h"
#include "audio.h"
#include "gfx.h"
#include "input.h"
#include "memory.h"
#include "vm.h"

#define VM_ERROR(fmt, ...) { vm_print_state(); ERROR(fmt, ##__VA_ARGS__); }

#define usr_var4  memory.mem16.var4
#define usr_var16 memory.mem16.var16
#define usr_var32 memory.mem16.var32
#define sys_var16 memory.mem16.system_var16
#define sys_var32 memory.mem16.system_var32

struct vm vm = {0};

void vm_print_state(void)
{
	sys_warning("ip = %08x\n", vm.ip.ptr);
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
	strcpy(memory.mem16.mes_name, name);
	if (!asset_mes_load(name, memory.file_data))
		VM_ERROR("Failed to load MES file \"%s\"", name);
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
			vm_stack_push(usr_var16[vm_read_byte()]);
			break;
		case MES_EXPR_ARRAY16_GET16: {
			uint32_t i = vm_stack_pop();
			uint8_t var = vm_read_byte();
			uint16_t *src = memory.mem16.system_var16_ptr;
			if (var)
				src = (uint16_t*)(memory_raw + usr_var16[var - 1]);
			vm_stack_push(src[i]);
			break;
		}
		case MES_EXPR_ARRAY16_GET8: {
			uint32_t i = vm_stack_pop();
			uint8_t var = vm_read_byte();
			uint8_t *src = memory_raw + usr_var16[var];
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
			vm_stack_push(usr_var4[vm_read_word()]);
			break;
		case MES_EXPR_REG8:
			vm_stack_push(usr_var4[vm_stack_pop()]);
			break;
		case MES_EXPR_ARRAY32_GET32: {
			uint32_t i = vm_stack_pop();
			uint8_t var = vm_read_byte();
			uint32_t *src = sys_var32;
			if (var)
				src = (uint32_t*)(memory_raw + usr_var32[var - 1]);
			vm_stack_push(src[i]);
			break;
		}
		case MES_EXPR_ARRAY32_GET16: {
			uint32_t i = vm_stack_pop();
			uint8_t var = vm_read_byte();
			uint16_t *src = (uint16_t*)(memory_raw + usr_var32[var - 1]);
			vm_stack_push(src[i]);
			break;
		}
		case MES_EXPR_ARRAY32_GET8: {
			uint32_t i = vm_stack_pop();
			uint8_t var = vm_read_byte();
			uint8_t *src = memory_raw + usr_var32[var - 1];
			vm_stack_push(src[i]);
			break;
		}
		case MES_EXPR_VAR32:
			vm_stack_push(usr_var32[vm_read_byte()]);
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

#define STRING_PARAM_SIZE 64

struct param {
	enum mes_parameter_type type;
	union {
		char str[STRING_PARAM_SIZE];
		uint32_t val;
	};
};

#define MAX_PARAMS 8

struct param_list {
	struct param params[MAX_PARAMS];
	unsigned nr_params;
};

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

static char *check_string_param(struct param_list *params, int i)
{
	if (params->nr_params < i)
		VM_ERROR("Too few parameters");
	if (params->params[i].type != MES_PARAM_STRING)
		VM_ERROR("Expected string parameter");
	return params->params[i].str;
}

static uint32_t check_expr_param(struct param_list *params, int i)
{
	if (params->nr_params < i)
		VM_ERROR("Too few parameters");
	if (params->params[i].type != MES_PARAM_EXPRESSION)
		VM_ERROR("Expected expression parameter");
	return params->params[i].val;
}

#define TXT_BUF_SIZE 4096

static void draw_text(const char *text)
{
	const uint16_t char_space = sys_var16[MES_SYS_VAR_CHAR_SPACE];
	uint16_t *x = &sys_var16[MES_SYS_VAR_TEXT_CURSOR_X];
	uint16_t *y = &sys_var16[MES_SYS_VAR_TEXT_CURSOR_Y];
	while (*text) {
		int ch;
		bool zenkaku = SJIS_2BYTE(*text);
		uint16_t next_x = *x + (zenkaku ? char_space / 8 : char_space / 16);
		if (next_x > sys_var16[MES_SYS_VAR_TEXT_END_X]) {
			*y += sys_var16[MES_SYS_VAR_LINE_SPACE];
			*x = sys_var16[MES_SYS_VAR_TEXT_START_X];
			next_x = *x + (zenkaku ? char_space / 8 : char_space / 16);
		}
		text = sjis_char2unicode(text, &ch);
		gfx_text_draw_glyph(*x * 8, *y, ch);
		*x = next_x;
		// TODO: YU-NO Eng TL doesnt' wrap the same way -- text can overflow the
		//       text area, and if it overflows the screen, it wraps around to the
		//       left side of the screen (with no y-increment)
		// TODO: YU-NO Eng TL uses patched executable with kerning
		//       ---but char space is still used in some way...
	}
}

static void stmt_txt(void)
{
	size_t str_i = 0;
	char str[TXT_BUF_SIZE];

	uint8_t c;
	while ((c = vm_peek_byte())) {
		if (unlikely(!mes_char_is_zenkaku(c))) {
			WARNING("Invalid byte in TXT statement: %02x", (unsigned)c);
			goto unterminated;
		}
		str[str_i++] = vm_read_byte();
		str[str_i++] = vm_read_byte();
	}
	vm_read_byte();
unterminated:
	str[str_i] = 0;
	draw_text(str);
}

static void stmt_str(void)
{
	size_t str_i = 0;
	char str[TXT_BUF_SIZE];

	uint8_t c;
	while ((c = vm_peek_byte())) {
		if (unlikely(!mes_char_is_hankaku(c))) {
			WARNING("Invalid byte in STR statement: %02x", (unsigned)c);
			goto unterminated;
		}
		str[str_i++] = c;
		vm_read_byte();
	}
	vm_read_byte();
unterminated:
	str[str_i] = 0;
	draw_text(str);
}

static void stmt_setrbc(void)
{
	uint16_t i = vm_read_word();
	do {
		usr_var4[i++] = vm_eval() & 0xf;
	} while (vm_read_byte());
}

static void stmt_setv(void)
{
	uint8_t i = vm_read_byte();
	do {
		usr_var16[i++] = vm_eval();
	} while (vm_read_byte());
}

static void stmt_setrbe(void)
{
	uint32_t i = vm_eval();
	do {
		usr_var4[i++] = vm_eval() & 0xf;
	} while (vm_read_byte());
}

static void stmt_setrd(void)
{
	uint32_t i = vm_read_byte();
	do {
		usr_var32[i++] = vm_eval();
	} while (vm_read_byte());
}

static void stmt_setac(void)
{
	uint32_t i = vm_eval();
	uint8_t var = vm_read_byte();
	uint8_t *dst = memory_raw + usr_var4[var] + i;

	do {
		*dst++ = vm_eval();
	} while (vm_read_byte());
}

static void stmt_seta_at(void)
{
	uint32_t i = vm_eval();
	uint8_t var = vm_read_byte();
	uint16_t *dst = memory.mem16.system_var16_ptr;
	if (var) {
		dst = (uint16_t*)(memory_raw + usr_var16[var - 1]);
	}
	dst += i;

	do {
		*dst++ = vm_eval();
	} while (vm_read_byte());
}

static void stmt_setad(void)
{
	uint32_t i = vm_eval();
	uint8_t var = vm_read_byte();
	uint32_t *dst = sys_var32;
	if (var)
		dst = (uint32_t*)(memory_raw + usr_var32[var - 1]);
	dst += i;

	do {
		*dst++ = vm_eval();
	} while (vm_read_byte());
}

static void stmt_setaw(void)
{
	uint32_t i = vm_eval();
	uint8_t var = vm_read_byte();
	uint16_t *dst = (uint16_t*)(memory_raw + usr_var32[var - 1]) + i;

	do {
		*dst++ = vm_eval();
	} while (vm_read_byte());
}

static void stmt_setab(void)
{
	uint32_t i = vm_eval();
	uint8_t var = vm_read_byte();
	uint8_t *dst = memory_raw + usr_var32[var - 1] + i;

	do {
		*dst++ = vm_eval();
	} while (vm_read_byte());
}

static void stmt_jz(void)
{
	uint32_t ptr = vm_read_dword();
	if (vm_eval() == 1)
		return;
	vm.ip.ptr = ptr;
}

static void stmt_jmp(void)
{
	vm.ip.ptr = le_get32(vm.ip.code, vm.ip.ptr);
}

static void stmt_sys_set_font_size(struct param_list *params)
{
	gfx_text_set_size(sys_var16[MES_SYS_VAR_FONT_HEIGHT]);
}

static void stmt_sys_audio(struct param_list *params)
{
	switch (check_expr_param(params, 0)) {
	case 0:  audio_bgm_play(check_string_param(params, 1)); break;
	case 2:  audio_bgm_stop(); break;
	case 3:  audio_se_play(check_string_param(params, 1), check_expr_param(params, 2)); break;
	case 4:  audio_bgm_fade(check_expr_param(params, 1), check_expr_param(params, 2),
				check_expr_param(params, 3), true); break;
	case 5:  audio_bgm_set_volume(check_expr_param(params, 1)); break;
	case 7:  audio_bgm_fade(check_expr_param(params, 1), check_expr_param(params, 2),
				check_expr_param(params, 3), false); break;
	case 9:  audio_bgm_fade(check_expr_param(params, 1), check_expr_param(params, 1),
				true, true); break;
	case 10: audio_bgm_fade(check_expr_param(params, 1), check_expr_param(params, 2),
				true, false); break;
	case 12: audio_se_stop(check_expr_param(params, 1)); break;
	case 18: audio_bgm_stop(); break;
	default: VM_ERROR("System.Audio.function[%d] not implemented", params->params[0].val);
	}
}

static void stmt_sys_load_image(struct param_list *params)
{
	check_string_param(params, 0);
	struct cg *cg = asset_cg_load(params->params[0].str);
	if (!cg) {
		WARNING("Failed to load CG \"%s\"", params->params[0].str);
		return;
	}
	gfx_draw_cg(cg);
	if (cg->palette) {
		memcpy(memory.palette, cg->palette, 256 * 4);
	}
	cg_free(cg);
}

static void stmt_sys_palette(struct param_list *params)
{
	check_expr_param(params, 0);
	switch (params->params[0].val) {
	case 0:  gfx_set_palette(memory.palette); break;
	default: VM_ERROR("System.Palette.function[%d] not implemented",
				 params->params[0].val);
	}
}

static void stmt_sys_graphics_fill_bg(struct param_list *params)
{
	uint32_t tl_x = check_expr_param(params, 1);
	uint32_t tl_y = check_expr_param(params, 2);
	uint32_t br_x = check_expr_param(params, 3);
	uint32_t br_y = check_expr_param(params, 4);
	gfx_text_fill(tl_x * 8, tl_y, br_x * 8, br_y);
}

static void stmt_sys_graphics(struct param_list *params)
{
	check_expr_param(params, 0);
	switch (params->params[0].val) {
	case 2:  stmt_sys_graphics_fill_bg(params); break;
	default: VM_ERROR("System.Image.function[%d] not implemented",
				 params->params[0].val);
	}
}

static void stmt_sys_wait(struct param_list *params)
{
	input_keywait();
}

static void stmt_sys_set_text_colors(struct param_list *params)
{
	check_expr_param(params, 0);
	uint32_t colors = params->params[0].val;
	gfx_text_set_colors((colors >> 4) & 0xf, colors & 0xf);
}

static void stmt_sys(void)
{
	int32_t no = vm_eval();

	struct param_list params = {0};
	read_params(&params);

	switch (no) {
	case 0:  stmt_sys_set_font_size(&params); break;
	case 5:  stmt_sys_audio(&params); break;
	case 8:  stmt_sys_load_image(&params); break;
	case 9:  stmt_sys_palette(&params); break;
	case 10: stmt_sys_graphics(&params); break;
	case 11: stmt_sys_wait(&params); break;
	case 12: stmt_sys_set_text_colors(&params); break;
	default: VM_ERROR("System.function[%d] not implemented", no);
	}
}

static void stmt_goto(void)
{
	struct param_list params = {0};
	read_params(&params);

	check_string_param(&params, 0);
	vm_load_mes(params.params[0].str);

	vm_flag_on(VM_FLAG_RETURN);
}

static void stmt_call(void)
{
	struct param_list params = {0};
	read_params(&params);
	check_string_param(&params, 0);

	// save current VM state
	struct vm_mes_call *frame = &vm.mes_call_stack[vm.mes_call_stack_ptr++];
	frame->ip = vm.ip;
	memcpy(frame->mes_name, memory.mem16.mes_name, 12);
	frame->mes_name[12] = '\0';
	memcpy(frame->procedures, vm.procedures, sizeof(vm.procedures));

	// load and execute mes file
	vm.ip.ptr = 0;
	vm.ip.code = memory.file_data;
	vm_load_mes(params.params[0].str);
	vm_exec();

	// restore previous VM state
	frame = &vm.mes_call_stack[--vm.mes_call_stack_ptr];
	vm.ip.code = frame->ip.code;
	if (vm_flag_is_on(VM_FLAG_RETURN)) {
		vm.ip.ptr = frame->ip.ptr;
		memcpy(vm.procedures, frame->procedures, sizeof(vm.procedures));
		vm_load_mes(frame->mes_name);
		frame->ip.ptr = 0;
		frame->ip.code = NULL;
		frame->mes_name[0] = '\0';
	}
}

static void stmt_menui(void)
{
	VM_ERROR("MENUI statement not implemented");
}

static void stmt_menus(void)
{
	VM_ERROR("MENUS statement not implemented");
}

static void stmt_proc(void)
{
	struct param_list params = {0};
	read_params(&params);
	check_expr_param(&params, 0);
	if (unlikely(params.params[0].val >= VM_MAX_PROCEDURES))
		VM_ERROR("Invalid procedure number: %d", params.params[0].val);

	struct vm_pointer saved_ip = vm.ip;
	vm.ip = vm.procedures[params.params[0].val];
	vm_exec();
	vm.ip = saved_ip;
}

static void stmt_util(void)
{
	VM_ERROR("UTIL statement not implemented");
}

static void stmt_line(void)
{
	// FIXME: is this correct?
	if (vm_read_byte())
		return;

	sys_var16[MES_SYS_VAR_TEXT_CURSOR_X] = sys_var16[MES_SYS_VAR_TEXT_START_X];
	sys_var16[MES_SYS_VAR_TEXT_CURSOR_Y] += sys_var16[MES_SYS_VAR_LINE_SPACE];
}

static void stmt_procd(void)
{
	uint32_t i = vm_eval();
	if (unlikely(i >= VM_MAX_PROCEDURES))
		VM_ERROR("Invalid procedure number: %d", i);
	vm.procedures[i] = vm.ip;
	vm.ip.ptr = vm_read_dword();
}

bool vm_exec_statement(void)
{
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
	case MES_STMT_MENUS:   stmt_menus(); break;
	case MES_STMT_SETRD:   stmt_setrd(); break;
	case MES_STMT_INVALID:
		vm_rewind_byte();
		WARNING("Unprefixed text: 0x%02x (possibly unhandled statement)", (unsigned)op);
		if (mes_char_is_hankaku(op))
			stmt_str();
		else
			stmt_txt();
		break;
	}
	return true;
}

void vm_exec(void)
{
	vm.scope_counter++;
	while (true) {
		gfx_update();
		if (vm_flag_is_on(VM_FLAG_RETURN)) {
			if (vm.scope_counter != 1)
				break;
			vm_flag_off(VM_FLAG_RETURN);
			vm.ip.ptr = 0;
		}
		if (!vm_exec_statement())
			break;
		handle_events();
	}
	vm.scope_counter--;
}
