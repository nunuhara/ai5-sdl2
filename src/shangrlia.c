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
#include "nulib/utfsjis.h"
#include "ai5/mes.h"

#include "anim.h"
#include "asset.h"
#include "char.h"
#include "classics.h"
#include "cursor.h"
#include "game.h"
#include "gfx.h"
#include "input.h"
#include "memory.h"
#include "savedata.h"
#include "sys.h"
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

static void shangrlia_savedata(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: savedata_resume_load(sys_save_name(params)); break;
	case 1: savedata_resume_save(sys_save_name(params)); break;
	case 2: savedata_load(sys_save_name(params)); break;
	case 3: savedata_save(sys_save_name(params)); break;
	case 4: savedata_load_var4(sys_save_name(params), VAR4_SIZE); break;
	case 5: savedata_save_var4(sys_save_name(params), VAR4_SIZE); break;
	case 6: savedata_save_union_var4(sys_save_name(params), VAR4_SIZE); break;
	case 7: savedata_load_var4_slice(sys_save_name(params), vm_expr_param(params, 2),
				vm_expr_param(params, 3)); break;
	case 8: savedata_save_var4_slice(sys_save_name(params), vm_expr_param(params, 2),
				vm_expr_param(params, 3)); break;
	default: VM_ERROR("System.savedata.function[%u] not implemented", params->params[0].val);
	}
}

static void shangrlia_set_speaker(struct param_list *params)
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

// size of the map in tiles
#define MAP_TW 19
#define MAP_TH 11

// size of a tile in pixels
#define TILE_SIZE 32

// top-left screen coordinate of map
#define MAP_X 16
#define MAP_Y 40

#define NO_UNIT 0xff

// tile coordinates (32x32) into CHIP2.GP8 for each tile index
struct { uint8_t tx, ty; } tile_cg_coords[] = {
	[0x00] = {  0, 0 },
	[0x01] = {  4, 0 },
	[0x02] = {  8, 0 },
	[0x03] = { 12, 0 },
	[0x04] = { 16, 0 },
	[0x05] = {  0, 1 },
	[0x06] = {  4, 1 },
	[0x07] = {  8, 1 },
	[0x08] = { 12, 1 },
	[0x09] = { 16, 1 },
	[0x0a] = {  0, 2 },
	[0x10] = {  1, 0 },
	[0x11] = {  5, 0 },
	[0x12] = {  9, 0 },
	[0x13] = { 13, 0 },
	[0x14] = { 17, 0 },
	[0x15] = {  1, 1 },
	[0x16] = {  5, 1 },
	[0x17] = {  9, 1 },
	[0x18] = { 13, 1 },
	[0x19] = { 17, 1 },
	[0x1a] = {  1, 2 },
	[0x20] = {  2, 0 },
	[0x21] = {  6, 0 },
	[0x22] = { 10, 0 },
	[0x23] = { 14, 0 },
	[0x24] = { 18, 0 },
	[0x25] = {  2, 1 },
	[0x26] = {  6, 1 },
	[0x27] = { 10, 1 },
	[0x28] = { 14, 1 },
	[0x29] = { 18, 1 },
	[0x30] = {  3, 0 },
	[0x31] = {  7, 0 },
	[0x32] = { 11, 0 },
	[0x33] = { 15, 0 },
	[0x34] = { 19, 0 },
	[0x35] = {  3, 1 },
	[0x36] = {  7, 1 },
	[0x37] = { 11, 1 },
	[0x38] = { 15, 1 },
	[0x39] = { 19, 1 },
};

static struct {
	uint8_t *map_ptr;
	uint8_t *unit_ptr;
	uint8_t *unitpara_ptr;
	uint8_t *chikei_ptr;
} mapdata;

struct tile {
	uint8_t tile_no;
	uint8_t unit_no;
};

struct unit {
	uint8_t present;
	uint8_t index;
	uint8_t tx;
	uint8_t ty;
	uint8_t uk5;
};

static struct unit units[64];

static struct tile tilemap[MAP_TH][MAP_TW];

static void load_map(uint8_t *map)
{
	memset(tilemap, 0, sizeof(tilemap));
	if (le_get16(map, 1) != MAP_TW)
		VM_ERROR("Unexpected map width: %u", le_get16(map, 1));
	if (le_get16(map, 3) != MAP_TH)
		VM_ERROR("Unexpected map height: %u", le_get16(map, 3));

	uint8_t *p = map + 5;
	for (int row = 0; row < MAP_TH; row++) {
		for (int col = 0; col < MAP_TW; col++, p += 4) {
			tilemap[row][col].tile_no = *p;
			tilemap[row][col].unit_no = NO_UNIT;
		}
	}
}

static void load_unit(uint8_t *unit)
{
	for (int i = 0; i < 64; i++, unit += 8) {
		units[i].present = unit[0] & 0xf;
		units[i].index = unit[1];
		units[i].tx = unit[3];
		units[i].ty = unit[4];
		units[i].uk5 = unit[5];
	}
}

static void load_unitpara(uint8_t *unitpara)
{

}

static void load_chikei(uint8_t *chikei)
{

}

static void place_units(void)
{
	for (int i = 0; i < 64; i++) {
		if (!units[i].present) {
			continue;
		}
		struct unit *unit = &units[i];
		struct tile *tile = &tilemap[unit->ty][unit->tx];
		tile->unit_no = i;
	}
}

static void update_map(void)
{
	load_map(mapdata.map_ptr);
	load_unit(mapdata.unit_ptr);
	load_unitpara(mapdata.unitpara_ptr);
	load_chikei(mapdata.chikei_ptr);
	place_units();
}

static void util_map_init(struct param_list *params)
{
	uint8_t *map = memory_raw + vm_expr_param(params, 2);
	uint8_t *unit = memory_raw + vm_expr_param(params, 3);
	uint8_t *unitpara = memory_raw + vm_expr_param(params, 4);
	uint8_t *chikei = memory_raw + vm_expr_param(params, 5);
	//unsigned uk1 = vm_expr_param(params, 6);
	uint8_t *uk2 = memory_raw + vm_expr_param(params, 7);
	uint8_t *uk3 = memory_raw + vm_expr_param(params, 8);
	if (!mem_ptr_valid(map, 5 + MAP_TW * MAP_TH * 4))
		VM_ERROR("Invalid map pointer: 0x%x", params->params[2].val);
	if (!mem_ptr_valid(unit, 1))
		VM_ERROR("Invalid unit pointer: 0x%x", params->params[3].val);
	if (!mem_ptr_valid(unitpara, 1))
		VM_ERROR("Invalid unitpara pointer: 0x%x", params->params[4].val);
	if (!mem_ptr_valid(chikei, 1))
		VM_ERROR("Invalid chikei pointer: 0x%x", params->params[5].val);
	if (!mem_ptr_valid(uk2, 1))
		VM_ERROR("Invalid uk2 pointer: 0x%x", params->params[7].val);
	if (!mem_ptr_valid(uk3, 1))
		VM_ERROR("Invalid uk3 pointer: 0x%x", params->params[8].val);
	mapdata.map_ptr = map;
	mapdata.unit_ptr = unit;
	mapdata.unitpara_ptr = unitpara;
	mapdata.chikei_ptr = chikei;
	update_map();
}

static void draw_tile(int col, int row)
{
	uint8_t tile_no = tilemap[row][col].tile_no;
	unsigned tile_ord = (tile_no & 0xf) * 4 + (tile_no >> 4);
	int src_x = (tile_ord % 20) * TILE_SIZE;
	int src_y = (tile_ord / 20) * TILE_SIZE;
	int dst_x = MAP_X + col * TILE_SIZE;
	int dst_y = MAP_Y + row * TILE_SIZE;
	gfx_copy(src_x, src_y, TILE_SIZE, TILE_SIZE, 1, dst_x, dst_y, 0);

	int unit_no = tilemap[row][col].unit_no;
	if (unit_no == NO_UNIT)
		return;

	if (unit_no < 32) {
		tile_ord = units[unit_no].index + 64;
	} else {
		tile_ord = units[unit_no].index + 42;
		if (tile_ord >= 74)
			tile_ord += 8; // ???
	}
	src_x = (tile_ord % 20) * TILE_SIZE;
	src_y = (tile_ord / 20) * TILE_SIZE;
	gfx_copy_masked(src_x, src_y, TILE_SIZE, TILE_SIZE, 1, dst_x, dst_y, 0, 0xf);
}

static void draw_tile_cursor(int tx, int ty)
{
	int dst_x = MAP_X + tx * TILE_SIZE;
	int dst_y = MAP_Y + ty * TILE_SIZE;
	gfx_copy_masked(0, 160, TILE_SIZE, TILE_SIZE, 1, dst_x, dst_y, 0, 0xf);
}

static void draw_map(void)
{
	for (int row = 0; row < MAP_TH; row++) {
		for (int col = 0; col < MAP_TW; col++) {
			draw_tile(col, row);
		}
	}
}

static int mouse_tx = 0;
static int mouse_ty = 0;
static int mouse_btn = 0;

static void get_mouse_state(void)
{
	mouse_btn = 0;

	unsigned x, y;
	cursor_get_pos(&x, &y);
	if (x >= MAP_X && x < 640 - MAP_X && y >= MAP_Y && y < 400 - 8) {
		mouse_tx = (x - MAP_X) / TILE_SIZE;
		mouse_ty = (y - MAP_Y) / TILE_SIZE;
	}

	if (input_down(INPUT_ACTIVATE)) {
		mouse_btn = 1;
	}
	if (input_down(INPUT_CANCEL)) {
		mouse_btn = 2;
	}
}

static void handle_map_input(void)
{
	// TODO: draw selected info

	do {
		int prev_mouse_tx = mouse_tx;
		int prev_mouse_ty = mouse_ty;
		get_mouse_state();
		if (prev_mouse_tx != mouse_tx || prev_mouse_ty != mouse_ty) {
			// move tile cursor
			draw_tile(prev_mouse_tx, prev_mouse_ty);
			draw_tile_cursor(mouse_tx, mouse_ty);
		}
		vm_peek();
		gfx_update();
	} while (mouse_btn == 0);

	if (mouse_btn == 1) {
		mem_set_var16(18, (mouse_tx << 8) | mouse_ty);
		mem_set_var16(19, mouse_tx);
	} else {
		mem_set_var16(18, 0xffff);
	}
}

static void util_map(struct param_list *params)
{
	switch (vm_expr_param(params, 1)) {
	case 0:
		util_map_init(params);
		break;
	case 4:
		update_map();
		draw_map();
		// TODO: draw_selected_info
		break;
	case 6:
		draw_map();
		break;
	case 5:
		handle_map_input();
		break;
	case 7:
		load_map(mapdata.map_ptr);
		load_unit(mapdata.unit_ptr);
		place_units();
		draw_map();
		break;
	case 1:
		VM_ERROR("Util.Map.function[%d] not implemented", params->params[1].val);
		break;
	}
}

static void shangrlia_draw_text(const char *text)
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
		uint16_t this_char_space = zenkaku ? char_space : char_space / 2;
		if (x + this_char_space > end_x + 1) {
			x = start_x;
			y += line_space;
		}

		text = sjis_char2unicode(text, &ch);
		gfx_text_draw_glyph(x, y, surface, ch);
		x += this_char_space;
	}
	mem_set_sysvar16(mes_sysvar16_text_cursor_x, x);
	mem_set_sysvar16(mes_sysvar16_text_cursor_y, y);
}

static void shangrlia_init(void)
{
	asset_effect_is_bgm = false;
}

struct game game_shangrlia = {
	.id = GAME_SHANGRLIA,
	.surface_sizes = {
		{ 640, 400 },
		{ 640, 400 },
		{ 640, 768 },
		{ 640, 768 },
		{ 1280, 800 },
		{ 0, 0 },
	},
	.bpp = 8,
	.mem16_size = MEM16_SIZE,
	.mem_init = shangrlia_mem_init,
	.mem_restore = shangrlia_mem_restore,
	.init = shangrlia_init,
	.draw_text_zen = shangrlia_draw_text,
	.draw_text_han = shangrlia_draw_text,
	.expr_op = { CLASSICS_EXPR_OP },
	.stmt_op = { CLASSICS_STMT_OP },
	.sys = {
		[0] = sys_set_font_size,
		[1] = sys_display_number,
		[2] = classics_cursor,
		[3] = classics_anim,
		[4] = shangrlia_savedata,
		[5] = classics_audio,
		[6] = NULL, // unused
		[7] = sys_file,
		[8] = sys_load_image,
		[9] = classics_palette,
		[10] = classics_graphics,
		[11] = sys_wait,
		[12] = sys_set_text_colors_indexed,
		[13] = sys_farcall,
		[14] = classics_get_cursor_segment,
		[15] = sys_menu_get_no,
		[17] = util_noop,
		[18] = sys_check_input,
		[19] = NULL, // unused
		[20] = util_noop,
		[21] = sys_strlen,
		[22] = util_noop,
		[23] = shangrlia_set_speaker,
	},
	.util = {
		[0] = util_map,
		[1] = classics_get_text_colors,
		[100] = NULL,
	},
	.flags = {
		[FLAG_ANIM_ENABLE]  = 0x0004,
		[FLAG_MENU_RETURN]  = 0x0008,
		[FLAG_RETURN]       = 0x0010,
		[FLAG_LOG]          = 0x0080,
		[FLAG_VOICE_ENABLE] = 0x0100,
		[FLAG_AUDIO_ENABLE] = FLAG_ALWAYS_ON,
		[FLAG_LOAD_PALETTE] = 0x2000,
		[FLAG_WAIT_KEYUP]   = FLAG_ALWAYS_ON,
		[FLAG_SKIP_KEYUP]   = 0x4000,
		[FLAG_PALETTE_ONLY] = 0x8000,
	}
};
