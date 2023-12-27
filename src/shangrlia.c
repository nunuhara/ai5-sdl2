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

#include "nulib.h"
#include "nulib/little_endian.h"
#include "ai5/mes.h"

#include "anim.h"
#include "game.h"
#include "gfx.h"
#include "memory.h"
#include "sys.h"
#include "util.h"
#include "vm_private.h"

#define VAR4_SIZE 2048
#define MEM16_SIZE 4096

static void shangrlia_mem_restore(void)
{
	// XXX: In AI5WIN.EXE, these are 32-bit pointers into the VM's own
	//      address space. Since we support 64-bit systems, we treat
	//      32-bit pointers as offsets into the `memory` struct (similar
	//      to how AI5WIN.EXE treats 16-bit pointers).
	mem_set_sysvar16_ptr(MEMORY_MES_NAME_SIZE + VAR4_SIZE + 56);
	mem_set_sysvar32(mes_sysvar32_memory, offsetof(struct memory, mem16));
	mem_set_sysvar32(mes_sysvar32_file_data, offsetof(struct memory, file_data));
	mem_set_sysvar32(mes_sysvar32_menu_entry_addresses,
			offsetof(struct memory, menu_entry_addresses));
	mem_set_sysvar32(mes_sysvar32_menu_entry_numbers,
			offsetof(struct memory, menu_entry_numbers));

	// this value is restored when loading a save via System.SaveData.resume_load...
	mem_set_sysvar16(0, 2634);
}

static void shangrlia_mem_init(void)
{
	// set up pointer table for memory access
	// (needed because var4 size changes per game)
	uint32_t off = MEMORY_MES_NAME_SIZE + VAR4_SIZE;
	memory_ptr.system_var16_ptr = memory_raw + off;
	memory_ptr.var16 = memory_raw + off + 4;
	memory_ptr.system_var16 = memory_raw + off + 56;
	memory_ptr.var32 = memory_raw + off + 106;
	memory_ptr.system_var32 = memory_raw + off + 210;

	mem_set_sysvar16(mes_sysvar16_flags, 0x260f);
	mem_set_sysvar16(mes_sysvar16_text_start_x, 0);
	mem_set_sysvar16(mes_sysvar16_text_start_y, 0);
	mem_set_sysvar16(mes_sysvar16_text_end_x, game_shangrlia.surface_sizes[0].w);
	mem_set_sysvar16(mes_sysvar16_text_end_y, game_shangrlia.surface_sizes[0].h);
	mem_set_sysvar16(mes_sysvar16_font_width, 16);
	mem_set_sysvar16(mes_sysvar16_font_height, 16);
	mem_set_sysvar16(mes_sysvar16_char_space, 16);
	mem_set_sysvar16(mes_sysvar16_line_space, 16);
	mem_set_sysvar16(mes_sysvar16_mask_color, 0);

	mem_set_sysvar32(mes_sysvar32_cg_offset, 0x20000);
	shangrlia_mem_restore();
}

static void sys_load_image_halt_anim(struct param_list *params)
{
	anim_halt_all();
	vm_load_image(vm_string_param(params, 0), mem_get_sysvar16(mes_sysvar16_dst_surface));
}

static void sys_22_warn(struct param_list *params)
{
	WARNING("System.function[22] not implemented");
}

static void sys_set_speaker(struct param_list *params)
{
	unsigned no = vm_expr_param(params, 0);
	switch (no) {
	case 0: gfx_palette_set_color(15, 0x88, 0x88, 0x88); break;
	case 1: gfx_palette_set_color(15, 0x03, 0xaa, 0xff); break;
	case 2: gfx_palette_set_color(15, 0xff, 0x00, 0xaa); break;
	case 3: gfx_palette_set_color(15, 0xdd, 0x00, 0xff); break;
	case 4: gfx_palette_set_color(15, 0x03, 0xff, 0x00); break;
	case 5: gfx_palette_set_color(15, 0x00, 0xff, 0xff); break;
	case 6: gfx_palette_set_color(15, 0xff, 0xdd, 0x00); break;
	case 7: gfx_palette_set_color(15, 0xff, 0xff, 0xff); break;
	case 0xfff: gfx_palette_set_color(15, 0, 0, 0); break;
	default: WARNING("Unexpected color index: %u", params->params[0].val);
	}
	if (no < 8)
		anim_start(no);
}

struct game game_shangrlia = {
	.surface_sizes = {
		{ 640, 400 },
		{ 640, 400 },
		{ 640, 768 },
		{ 640, 768 },
		{ 1280, 800 },
		{ 0, 0 },
	},
	.bpp = 8,
	.x_mult = 1,
	.use_effect_arc = true,
	.persistent_volume = true,
	.var4_size = VAR4_SIZE,
	.mem16_size = MEM16_SIZE,
	.mem_init = shangrlia_mem_init,
	.mem_restore = shangrlia_mem_restore,
	.sys = {
		[0] = sys_set_font_size,
		[1] = sys_display_number,
		[2] = sys_cursor,
		[3] = sys_anim,
		[4] = sys_savedata,
		[5] = sys_audio,
		[6] = NULL,
		[7] = sys_file,
		[8] = sys_load_image_halt_anim,
		[9] = sys_palette,
		[10] = sys_graphics_classics,
		[11] = sys_wait,
		[12] = sys_set_text_colors_indexed,
		[13] = sys_farcall,
		[14] = sys_get_cursor_segment,
		[15] = sys_menu_get_no,
		[18] = sys_check_input,
		[21] = sys_strlen,
		[22] = sys_22_warn,
		[23] = sys_set_speaker,
	},
	.util = {
		[0] = NULL,
		[1] = util_get_text_colors,
		[100] = NULL,
	},
	.flags = {
		[FLAG_REFLECTOR]    = 0x0002,
		[FLAG_MENU_RETURN]  = 0x0008,
		[FLAG_RETURN]       = 0x0010,
		[FLAG_LOG]          = 0x0080,
		[FLAG_LOAD_PALETTE] = 0x2000,
	}
};
