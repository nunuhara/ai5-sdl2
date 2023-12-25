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

#include <SDL.h>

#include "nulib.h"
#include "ai5/mes.h"

#include "game.h"
#include "gfx_private.h"
#include "sys.h"
#include "util.h"
#include "vm_private.h"

#define VAR4_SIZE 2048
#define MEM16_SIZE 4096

static void isaku_mem_restore(void)
{
	mem_set_sysvar16_ptr(MEMORY_MES_NAME_SIZE + VAR4_SIZE + 56);
	mem_set_sysvar32(mes_sysvar32_memory, offsetof(struct memory, mem16));
	mem_set_sysvar32(mes_sysvar32_palette, offsetof(struct memory, palette));
	mem_set_sysvar32(mes_sysvar32_file_data, offsetof(struct memory, file_data));
	mem_set_sysvar32(mes_sysvar32_menu_entry_addresses,
		offsetof(struct memory, menu_entry_addresses));
	mem_set_sysvar32(mes_sysvar32_menu_entry_numbers,
		offsetof(struct memory, menu_entry_numbers));

	uint16_t flags = mem_get_sysvar16(mes_sysvar16_flags);
	mem_set_sysvar16(mes_sysvar16_flags, flags | 4);
	mem_set_sysvar16(0, 2632);
	mem_set_var16(22, 20);
}

static void isaku_mem_init(void)
{
	// set up pointer table for memory access
	// (needed because var4 size changes per game)
	uint32_t off = MEMORY_MES_NAME_SIZE + VAR4_SIZE;
	memory_ptr.system_var16_ptr = memory_raw + off;
	memory_ptr.var16 = memory_raw + off + 4;
	memory_ptr.system_var16 = memory_raw + off + 56;
	memory_ptr.var32 = memory_raw + off + 108;
	memory_ptr.system_var32 = memory_raw + off + 212;

	mem_set_sysvar16(mes_sysvar16_flags, 0xf);
	mem_set_sysvar16(mes_sysvar16_text_start_x, 0);
	mem_set_sysvar16(mes_sysvar16_text_start_y, 0);
	mem_set_sysvar16(mes_sysvar16_text_end_x, game_yuno.surface_sizes[0].w);
	mem_set_sysvar16(mes_sysvar16_text_end_y, game_yuno.surface_sizes[0].h);
	mem_set_sysvar16(mes_sysvar16_font_width, 16);
	mem_set_sysvar16(mes_sysvar16_font_height, 16);
	mem_set_sysvar16(mes_sysvar16_char_space, 16);
	mem_set_sysvar16(mes_sysvar16_line_space, 16);
	mem_set_sysvar16(mes_sysvar16_mask_color, 0);

	mem_set_sysvar32(mes_sysvar32_cg_offset, 0x20000);
	isaku_mem_restore();
}

static void sys_graphics_isaku(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: sys_graphics_copy(params); break;
	case 1: sys_graphics_copy_masked(params); break;
	case 2: sys_graphics_fill_bg(params); break;
	case 3: sys_graphics_copy_swap(params); break;
	case 4: sys_graphics_swap_bg_fg(params); break;
	case 5: sys_graphics_copy_progressive(params); break;
	case 6: sys_graphics_compose(params); break;
	}
}

static void sys_item_window(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0:
	case 6:
		WARNING("System.ItemWindow.function[%u] not implemented",
				params->params[0].val);
	default:
		VM_ERROR("System.ItemWindow.function[%u] not implemented",
				params->params[0].val);
		break;
	}
}

static void sys_25_26(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 1:
		WARNING("System.function[25].function[1] not implemented");
		break;
	default:
		VM_ERROR("System.function[25].function[%u] not implemented",
				params->params[0].val);
		break;
	}
}

// only unfreeze is used in Isaku
static void sys_display_freeze_unfreeze(struct param_list *params)
{
	if (params->nr_params > 1) {
		gfx_display_freeze();
	} else {
		gfx_display_unfreeze();
	}
}

static void sys_display_fade_out_fade_in(struct param_list *params)
{
	if (params->nr_params > 1) {
		gfx_display_fade_out(vm_expr_param(params, 1));
	} else {
		gfx_display_fade_in();
	}
}

// this is not actually used in Isaku, though it is available
static void sys_display_scan_out_scan_in(struct param_list *params)
{
	if (params->nr_params > 1) {
		WARNING("System.Display.scan_out unimplemented");
		gfx_display_fade_out(vm_expr_param(params, 1));
	} else {
		WARNING("System.Display.scan_in unimplemented");
		gfx_display_fade_in();
	}
}

static void sys_display(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: sys_display_freeze_unfreeze(params); break;
	case 1: sys_display_fade_out_fade_in(params); break;
	case 2: sys_display_scan_out_scan_in(params); break;
	default: VM_ERROR("System.Display.function[%u] unimplemented",
				 params->params[0].val);
	}
}

struct game game_isaku = {
	.surface_sizes = {
		{  640,  480 },
		{ 1000, 1750 },
		{  640,  480 }, // XXX: AI5WIN.exe crashes when using this surface
		{  640,  480 },
		{  640,  480 },
		{  640,  480 },
		{  352,   32 },
		{  320,   32 },
		{  640,  480 },
		{  0, 0 }
	},
	.bpp = 16,
	.x_mult = 1,
	.var4_size = VAR4_SIZE,
	.mem16_size = MEM16_SIZE,
	.mem_init = isaku_mem_init,
	.mem_restore = isaku_mem_restore,
	.sys = {
		[8]  = sys_load_image,
		[9]  = sys_display,
		[10] = sys_graphics_isaku,
		[11] = sys_wait,
		[12] = sys_set_text_colors_direct,
		[22] = sys_item_window,
		[25] = sys_25_26,
		[26] = sys_25_26,
	},
	.util = {
		[12] = util_warn_unimplemented,
	}
};
