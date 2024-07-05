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

#ifndef AI5_SDL2_CLASSICS_H
#define AI5_SDL2_CLASSICS_H

struct param_list;

void classics_audio(struct param_list *params);
void classics_cursor(struct param_list *params);
void classics_anim(struct param_list *params);
void classics_palette(struct param_list *params);
void classics_graphics(struct param_list *params);
void classics_get_cursor_segment(struct param_list *params);

void classics_get_text_colors(struct param_list *params);

#define CLASSICS_EXPR_OP \
	DEFAULT_EXPR_OP, \
	[0xf6] = vm_expr_ptr32_get16, \
	[0xf7] = vm_expr_ptr32_get8, \
	[0xf8] = vm_expr_var32

#define CLASSICS_STMT_OP \
	[0x01] = vm_stmt_txt, \
	[0x02] = vm_stmt_str, \
	[0x03] = vm_stmt_set_cflag, \
	[0x04] = vm_stmt_set_var16, \
	[0x05] = vm_stmt_set_eflag, \
	[0x06] = vm_stmt_ptr16_set8, \
	[0x07] = vm_stmt_ptr16_set16, \
	[0x08] = vm_stmt_ptr32_set32, \
	[0x09] = vm_stmt_ptr32_set16, \
	[0x0a] = vm_stmt_ptr32_set8, \
	[0x0b] = vm_stmt_jz, \
	[0x0c] = vm_stmt_jmp, \
	[0x0d] = vm_stmt_sys, \
	[0x0e] = vm_stmt_mesjmp, \
	[0x0f] = vm_stmt_mescall, \
	[0x10] = vm_stmt_defmenu, \
	[0x11] = vm_stmt_call, \
	[0x12] = vm_stmt_util, \
	[0x13] = vm_stmt_line, \
	[0x14] = vm_stmt_defproc, \
	[0x15] = vm_stmt_menuexec, \
	[0x16] = vm_stmt_set_var32

#endif // AI5_SDL2_CLASSICS_H
