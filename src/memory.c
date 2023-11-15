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

#include "ai5.h"
#include "ai5/mes.h"
#include "memory.h"
#include "gfx.h"

struct memory memory = {0};

void memory_init(void)
{
	struct mem16 *mem16 = &memory.mem16;
	mem16->system_var16_ptr = mem16->system_var16;

	mem16->system_var16[0] = 5080; // ???
	mem16->system_var16[MES_SYS_VAR_FLAGS] = 0x260d;
	mem16->system_var16[MES_SYS_VAR_TEXT_START_X] = 0;
	mem16->system_var16[MES_SYS_VAR_TEXT_START_Y] = 0;
	mem16->system_var16[MES_SYS_VAR_TEXT_END_X] = gfx_view.w;
	mem16->system_var16[MES_SYS_VAR_TEXT_END_Y] = gfx_view.h;
	mem16->system_var16[MES_SYS_VAR_FONT_WIDTH] = DEFAULT_FONT_SIZE;
	mem16->system_var16[MES_SYS_VAR_FONT_HEIGHT] = DEFAULT_FONT_SIZE;
	mem16->system_var16[MES_SYS_VAR_CHAR_SPACE] = DEFAULT_FONT_SIZE;
	mem16->system_var16[MES_SYS_VAR_LINE_SPACE] = DEFAULT_FONT_SIZE;
	mem16->system_var16[MES_SYS_VAR_MASK_COLOR] = 0;

#define MEM32_OFFSET(f) ((uint8_t*)&memory.f - (uint8_t*)&memory)
	mem16->system_var32[MES_SYS_VAR_MEMORY] = MEM32_OFFSET(mem16);
	mem16->system_var32[1] = 0x20000;
	mem16->system_var32[MES_SYS_VAR_PALETTE] = MEM32_OFFSET(palette);
	mem16->system_var32[MES_SYS_VAR_FILE_DATA] = MEM32_OFFSET(file_data);
	mem16->system_var32[MES_SYS_VAR_MENU_ENTRY_ADDRESSES] = MEM32_OFFSET(menu_entry_addresses);
	mem16->system_var32[MES_SYS_VAR_MENU_ENTRY_NUMBERS] = MEM32_OFFSET(menu_entry_numbers);
#undef MEM32_OFFSET
}
