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

#include <stddef.h>

#include "ai5.h"
#include "ai5/mes.h"
#include "memory.h"
#include "gfx.h"

struct memory memory = {0};
struct memory_ptr memory_ptr = {0};

void memory_restore(void)
{
	// XXX: In AI5WIN.EXE, these are 32-bit pointers into the VM's own
	//      address space. Since we support 64-bit systems, we treat
	//      32-bit pointers as offsets into the `memory` struct (similar
	//      to how AI5WIN.EXE treats 16-bit pointers).
	memory_system_var16_ptr_set(MEMORY_MES_NAME_SIZE + memory_var4_size() + 56);
	memory_system_var32()[MES_SYS_VAR_MEMORY] = offsetof(struct memory, mem16);
	memory_system_var32()[MES_SYS_VAR_PALETTE] = offsetof(struct memory, palette);
	memory_system_var32()[MES_SYS_VAR_FILE_DATA] = offsetof(struct memory, file_data);
	memory_system_var32()[MES_SYS_VAR_MENU_ENTRY_ADDRESSES] =
		offsetof(struct memory, menu_entry_addresses);
	memory_system_var32()[MES_SYS_VAR_MENU_ENTRY_NUMBERS] =
		offsetof(struct memory, menu_entry_numbers);

	// this value is restored when loading a save via System.SaveData.resume_load...
	memory_system_var16()[0] = 5080;
}

void memory_init(void)
{
	// set up pointer table for memory access
	// (needed because var4 size changes per game)
	uint32_t off = MEMORY_MES_NAME_SIZE + memory_var4_size();
	memory_ptr.system_var16_ptr = (uint32_t*)(memory_raw + off);
	memory_ptr.var16 = (uint16_t*)(memory_raw + off + 4);
	memory_ptr.system_var16 = (uint16_t*)(memory_raw + off + 56);
	memory_ptr.var32 = (uint32_t*)(memory_raw + off + 108);
	memory_ptr.system_var32 = (uint32_t*)(memory_raw + off + 212);

	memory_system_var16()[MES_SYS_VAR_FLAGS] = 0x260d;
	memory_system_var16()[MES_SYS_VAR_TEXT_START_X] = 0;
	memory_system_var16()[MES_SYS_VAR_TEXT_START_Y] = 0;
	memory_system_var16()[MES_SYS_VAR_TEXT_END_X] = gfx_view.w;
	memory_system_var16()[MES_SYS_VAR_TEXT_END_Y] = gfx_view.h;
	memory_system_var16()[MES_SYS_VAR_FONT_WIDTH] = DEFAULT_FONT_SIZE;
	memory_system_var16()[MES_SYS_VAR_FONT_HEIGHT] = DEFAULT_FONT_SIZE;
	memory_system_var16()[MES_SYS_VAR_CHAR_SPACE] = DEFAULT_FONT_SIZE;
	memory_system_var16()[MES_SYS_VAR_LINE_SPACE] = DEFAULT_FONT_SIZE;
	memory_system_var16()[MES_SYS_VAR_MASK_COLOR] = 0;

	memory_system_var32()[MES_SYS_VAR_CG_OFFSET] = 0x20000;
	memory_restore();
}
