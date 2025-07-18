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
#include <SDL.h>

#include "nulib.h"
#include "nulib/file.h"
#include "nulib/utfsjis.h"
#include "ai5.h"
#include "ai5/arc.h"
#include "ai5/cg.h"

#include "anim.h"
#include "asset.h"
#include "audio.h"
#include "char.h"
#include "classics.h"
#include "cursor.h"
#include "game.h"
#include "gfx_private.h"
#include "input.h"
#include "savedata.h"
#include "sys.h"
#include "vm_private.h"

#define MES_NAME_SIZE 128
#define VAR4_SIZE 4096
#define MEM16_SIZE 8192

#define VAR4_OFF     MES_NAME_SIZE
#define SV16_PTR_OFF (VAR4_OFF + VAR4_SIZE)
#define VAR16_OFF    (SV16_PTR_OFF + 4)
#define SYSVAR16_OFF (VAR16_OFF + 26 * 2)
#define VAR32_OFF    (SYSVAR16_OFF + 26 * 2)
#define SYSVAR32_OFF (VAR32_OFF + 26 * 4)
#define HEAP_OFF     (SYSVAR32_OFF + 161 * 4)
_Static_assert(HEAP_OFF == 0x13d8);

static void yuno_mem_restore(void)
{
	// XXX: In AI5WIN.EXE, these are 32-bit pointers into the VM's own
	//      address space. Since we support 64-bit systems, we treat
	//      32-bit pointers as offsets into the `memory` struct (similar
	//      to how AI5WIN.EXE treats 16-bit pointers).
	mem_set_sysvar16_ptr(SYSVAR16_OFF);
	mem_set_sysvar32(mes_sysvar32_memory, offsetof(struct memory, mem16));
	mem_set_sysvar32(mes_sysvar32_palette, offsetof(struct memory, palette));
	mem_set_sysvar32(mes_sysvar32_file_data, offsetof(struct memory, file_data));
	mem_set_sysvar32(mes_sysvar32_menu_entry_addresses,
		offsetof(struct memory, menu_entry_addresses));
	mem_set_sysvar32(mes_sysvar32_menu_entry_numbers,
		offsetof(struct memory, menu_entry_numbers));

	// this value is restored when loading a save via System.SaveData.resume_load...
	mem_set_sysvar16(0, HEAP_OFF);
}

static void yuno_mem_init(void)
{
	// set up pointer table for memory access
	memory_ptr.mes_name = memory_raw;
	memory_ptr.var4 = memory_raw + VAR4_OFF;
	memory_ptr.system_var16_ptr = memory_raw + SV16_PTR_OFF;
	memory_ptr.var16 = memory_raw + VAR16_OFF;
	memory_ptr.system_var16 = memory_raw + SYSVAR16_OFF;
	memory_ptr.var32 = memory_raw + VAR32_OFF;
	memory_ptr.system_var32 = memory_raw + SYSVAR32_OFF;

	mem_set_sysvar16(mes_sysvar16_flags, 0x260d);
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
	yuno_mem_restore();
}

static void yuno_anim(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0:  anim_init_stream(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 1:  anim_start(vm_expr_param(params, 1)); break;
	case 2:  anim_stop(vm_expr_param(params, 1)); break;
	case 3:  anim_halt(vm_expr_param(params, 1)); break;
	// TODO
	case 4:  WARNING("System.Anim.function[4] not implemented"); break;
	case 5:  anim_stop_all(); break;
	case 6:  anim_halt_all(); break;
	case 20: anim_set_offset(vm_expr_param(params, 1), vm_expr_param(params, 2) * 8,
				vm_expr_param(params, 3)); break;
	default: VM_ERROR("System.Anim.function[%u] not implemented", params->params[0].val);
	}
}

static void yuno_savedata_load_jewel_save(const char *save_name)
{
	uint8_t buf[MEMORY_MEM16_MAX_SIZE];
	uint8_t *load_var4 = buf + VAR4_OFF;
	uint8_t *cur_var4 = mem_var4();
	savedata_read(save_name, buf, 0, VAR4_OFF + VAR4_SIZE);
	memcpy(memory_raw, buf, MES_NAME_SIZE);
	cur_var4[18] = load_var4[18];
	cur_var4[21] = load_var4[21];
	cur_var4[29] = load_var4[29];
	for (int i = 50; i < 90; i++) {
		cur_var4[i] = load_var4[i];
	}
	for (int i = 150; i < 2000; i++) {
		cur_var4[i] = load_var4[i];
	}
	game->mem_restore();
	vm_load_mes(mem_mes_name());
	vm_flag_on(FLAG_RETURN);
}

static uint8_t stashed_mes_name[MES_NAME_SIZE];

static void yuno_savedata_save_jewel_save(const char *save_name)
{
	uint8_t buf[MEMORY_MEM16_MAX_SIZE];
	uint8_t *out_var4 = buf + VAR4_OFF;
	uint8_t *cur_var4 = mem_var4();
	savedata_read(save_name, buf, 0, VAR4_OFF + VAR4_SIZE);
	memcpy(buf, stashed_mes_name, MES_NAME_SIZE);
	for (int i = 50; i < 90; i++) {
		out_var4[i] = cur_var4[i];
	}
	for (int i = 150; i < 2000; i++) {
		out_var4[i] = cur_var4[i];
	}
	savedata_write(save_name, buf, 0, VAR4_OFF + VAR4_SIZE);
}

static void yuno_savedata(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: savedata_resume_load(sys_save_name(params)); break;
	case 1: savedata_resume_save(sys_save_name(params)); break;
	case 2: savedata_load(sys_save_name(params), VAR4_OFF); break;
	case 3: savedata_save(sys_save_name(params), VAR4_OFF); break;
	case 4: savedata_read(sys_save_name(params), memory_raw, VAR4_OFF, VAR4_SIZE); break;
	case 5: savedata_save_var4(sys_save_name(params)); break;
	case 6: savedata_save_union_var4(sys_save_name(params)); break;
	case 7: savedata_load_var4_slice(sys_save_name(params), vm_expr_param(params, 2),
				vm_expr_param(params, 3)); break;
	case 8: savedata_save_var4_slice(sys_save_name(params), vm_expr_param(params, 2),
				vm_expr_param(params, 3)); break;
	case 9: savedata_copy(sys_save_name(params),
				_sys_save_name(vm_expr_param(params, 2))); break;
	case 11: yuno_savedata_load_jewel_save(sys_save_name(params)); break;
	case 12: yuno_savedata_save_jewel_save(sys_save_name(params)); break;
	case 13: savedata_set_mes_name(sys_save_name(params), vm_string_param(params, 2)); break;
	default: VM_ERROR("System.savedata.function[%u] not implemented", params->params[0].val);
	}
}

static void yuno_load_image(struct param_list *params)
{
	// XXX: animations are not halted when loading an image in YUNO
	_sys_load_image(vm_string_param(params, 0), mem_get_sysvar16(mes_sysvar16_dst_surface), 8);
}

static void yuno_graphics_copy(struct param_list *params)
{
	// System.Grahpics.copy(src_x, src_y, src_br_x, src_br_y, src_i, dst_x, dst_y, dst_i)
	int src_x = vm_expr_param(params, 1);
	int src_y = vm_expr_param(params, 2);
	int src_w = (vm_expr_param(params, 3) - src_x) + 1;
	int src_h = (vm_expr_param(params, 4) - src_y) + 1;
	unsigned src_i = vm_expr_param(params, 5);
	int dst_x = vm_expr_param(params, 6);
	int dst_y = vm_expr_param(params, 7);
	unsigned dst_i = vm_expr_param(params, 8);
	gfx_copy(src_x * 8, src_y, src_w * 8, src_h, src_i, dst_x * 8, dst_y, dst_i);
}

static void yuno_graphics_copy_masked(struct param_list *params)
{
	// System.Grahpics.copy_masked(src_x, src_y, src_br_x, src_br_y, src_i, dst_x, dst_y, dst_i)
	int src_x = vm_expr_param(params, 1);
	int src_y = vm_expr_param(params, 2);
	int src_w = (vm_expr_param(params, 3) - src_x) + 1;
	int src_h = (vm_expr_param(params, 4) - src_y) + 1;
	unsigned src_i = vm_expr_param(params, 5);
	int dst_x = vm_expr_param(params, 6);
	int dst_y = vm_expr_param(params, 7);
	unsigned dst_i = vm_expr_param(params, 8);
	gfx_copy_masked(src_x * 8, src_y, src_w * 8, src_h, src_i, dst_x * 8, dst_y, dst_i,
			mem_get_sysvar16(mes_sysvar16_mask_color));
}

static void yuno_graphics_fill_bg(struct param_list *params)
{
	// System.Graphics.fill_bg(x, y, br_x, br_y)
	int x = vm_expr_param(params, 1);
	int y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - x) + 1;
	int h = (vm_expr_param(params, 4) - y) + 1;
	gfx_text_fill(x * 8, y, w * 8, h, mem_get_sysvar16(mes_sysvar16_dst_surface));
}

static void yuno_graphics_copy_swap(struct param_list *params)
{
	// System.Grahpics.copy_swap(src_x, src_y, src_br_x, src_br_y, src_i, dst_x, dst_y, dst_i)
	int src_x = vm_expr_param(params, 1);
	int src_y = vm_expr_param(params, 2);
	int src_w = (vm_expr_param(params, 3) - src_x) + 1;
	int src_h = (vm_expr_param(params, 4) - src_y) + 1;
	unsigned src_i = vm_expr_param(params, 5);
	int dst_x = vm_expr_param(params, 6);
	int dst_y = vm_expr_param(params, 7);
	unsigned dst_i = vm_expr_param(params, 8);
	gfx_copy_swap(src_x * 8, src_y, src_w * 8, src_h, src_i, dst_x * 8, dst_y, dst_i);
}

static void yuno_graphics_swap_bg_fg(struct param_list *params)
{
	// System.Graphics.swap_bg_fg(x, y, br_x, br_y)
	int x = vm_expr_param(params, 1);
	int y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - x) + 1;
	int h = (vm_expr_param(params, 4) - y) + 1;
	gfx_text_swap_colors(x * 8, y, w * 8, h, mem_get_sysvar16(mes_sysvar16_dst_surface));
}

static void yuno_graphics_compose(struct param_list *params)
{
	// System.Grahpics.compose(src_x, src_y, src_br_x, src_br_y, src_i, dst_x, dst_y, dst_i)
	int fg_x = vm_expr_param(params, 1);
	int fg_y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - fg_x) + 1;
	int h = (vm_expr_param(params, 4) - fg_y) + 1;
	unsigned fg_i = vm_expr_param(params, 5);
	int bg_x = vm_expr_param(params, 6);
	int bg_y = vm_expr_param(params, 7);
	unsigned bg_i = vm_expr_param(params, 8);
	int dst_x = vm_expr_param(params, 9);
	int dst_y = vm_expr_param(params, 10);
	unsigned dst_i = vm_expr_param(params, 11);
	gfx_compose(fg_x * 8, fg_y, w * 8, h, fg_i, bg_x * 8, bg_y, bg_i, dst_x * 8, dst_y,
			dst_i, mem_get_sysvar16(mes_sysvar16_mask_color));
}

static void yuno_graphics_invert_colors(struct param_list *params)
{
	// System.Grahpics.invert_colors(x, y, br_x, br_y)
	int x = vm_expr_param(params, 1);
	int y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - x) + 1;
	int h = (vm_expr_param(params, 4) - y) + 1;
	unsigned i = mem_get_sysvar16(mes_sysvar16_dst_surface);
	gfx_invert_colors(x * 8, y, w * 8, h, i);
}

static void yuno_graphics_copy_progressive(struct param_list *params)
{
	// System.Grahpics.copy(src_x, src_y, src_br_x, src_br_y, src_i, dst_x, dst_y, dst_i)
	int src_x = vm_expr_param(params, 1);
	int src_y = vm_expr_param(params, 2);
	int src_w = (vm_expr_param(params, 3) - src_x) + 1;
	int src_h = (vm_expr_param(params, 4) - src_y) + 1;
	unsigned src_i = vm_expr_param(params, 5);
	int dst_x = vm_expr_param(params, 6);
	int dst_y = vm_expr_param(params, 7);
	unsigned dst_i = vm_expr_param(params, 8);
	gfx_copy_progressive(src_x * 8, src_y, src_w * 8, src_h, src_i,
			dst_x * 8, dst_y, dst_i);
}

static void yuno_graphics(struct param_list *params)
{
	vm_expr_param(params, 0);
	switch (params->params[0].val) {
	case 0:  yuno_graphics_copy(params); break;
	case 1:  yuno_graphics_copy_masked(params); break;
	case 2:  yuno_graphics_fill_bg(params); break;
	case 3:  yuno_graphics_copy_swap(params); break;
	case 4:  yuno_graphics_swap_bg_fg(params); break;
	case 5:  yuno_graphics_compose(params); break;
	case 6:  yuno_graphics_invert_colors(params); break;
	case 20: yuno_graphics_copy_progressive(params); break;
	default: VM_ERROR("System.Image.function[%d] not implemented",
				 params->params[0].val);
	}
}

static void sys_22(struct param_list *params)
{
	WARNING("System.function[22] not implemented");
}

static void yuno_set_screen_surface(struct param_list *params)
{
	gfx_set_screen_surface(vm_expr_param(params, 0));
}

static void util_blink_fade(struct param_list *params)
{
	gfx_blink_fade(64, 0, 512, 288, 0);
}

static void util_scale_h(struct param_list *params)
{
	union {
		uint16_t u;
		int16_t i;
	} cast = {
		.u = vm_expr_param(params, 1)
	};

	gfx_scale_h(gfx_current_surface(), cast.i);
}

static void util_invert_colors(struct param_list *params)
{
	int x = vm_expr_param(params, 1);
	int y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - x) + 1;
	int h = (vm_expr_param(params, 4) - y) + 1;
	gfx_invert_colors(x, y, w, h, 0);
}

static void util_fade(struct param_list *params)
{
	int x = vm_expr_param(params, 1);
	int y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - x) + 1;
	int h = (vm_expr_param(params, 4) - y) + 1;
	unsigned dst_i = vm_expr_param(params, 5);
	bool down = vm_expr_param(params, 6) == 1;
	int src_i = vm_expr_param(params, 7) == 0 ? -1 : 2;

	if (down)
		gfx_fade_down(x * 8, y, w * 8, h, dst_i, src_i);
	else
		gfx_fade_right(x * 8, y, w * 8, h, dst_i, src_i);
}

static void util_savedata_stash_name(struct param_list *params)
{
	memcpy(stashed_mes_name, memory_raw, MES_NAME_SIZE);
}

static void util_pixelate(struct param_list *params)
{
	int x = vm_expr_param(params, 1);
	int y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - x) + 1;
	int h = (vm_expr_param(params, 4) - y) + 1;
	unsigned dst_i = vm_expr_param(params, 5);
	unsigned mag = vm_expr_param(params, 6);

	gfx_pixelate(x * 8, y, w * 8, h, dst_i, mag);
}

static void util_get_time(struct param_list *params)
{
	static uint32_t start_t = 0;
	if (!vm_expr_param(params, 1)) {
		start_t = vm_get_ticks();
		return;
	}

	// return hours:minutes:seconds
	uint32_t elapsed = (vm_get_ticks() - start_t) / 1000;
	mem_set_var16(7, elapsed / 3600);
	mem_set_var16(12, (elapsed % 3600) / 60);
	mem_set_var16(18, elapsed % 60);
}

// wait for cursor to rest for a given interval
static void util_check_cursor(struct param_list *params)
{
	static uint32_t start_t = 0, wait_t = 0;
	static unsigned cursor_x = 0, cursor_y = 0;
	if (!vm_expr_param(params, 1)) {
		start_t = vm_get_ticks();
		wait_t = vm_expr_param(params, 2);
		cursor_get_pos(&cursor_x, &cursor_y);
		return;
	}

	// check timer
	uint32_t current_t = vm_get_ticks();
	mem_set_var16(18, 0);
	if (current_t < start_t + wait_t)
		return;

	// return TRUE if cursor didn't move
	unsigned x, y;
	cursor_get_pos(&x, &y);
	if (x == cursor_x && y == cursor_y) {
		mem_set_var16(18, 1);
		return;
	}

	// otherwise restart timer
	start_t = current_t;
	cursor_x = x;
	cursor_y = y;
}

static void util_delay(struct param_list *params)
{
	unsigned nr_ticks = vm_expr_param(params, 1);
	vm_timer_t timer = vm_timer_create();
	vm_timer_t target_t = timer + nr_ticks * 15;
	while (timer < target_t) {
		vm_peek();
		vm_timer_tick(&timer, min(target_t - timer, 15));
	}
}

static char *saved_cg_name = NULL;
static char *saved_data_name = NULL;

static bool saved_anim_running[10] = {0};

static void util_save_animation(struct param_list *params)
{
	free(saved_cg_name);
	free(saved_data_name);
	saved_cg_name = asset_cg_name ? xstrdup(asset_cg_name) : NULL;
	saved_data_name = asset_data_name ? xstrdup(asset_data_name) : NULL;
}

static void util_restore_animation(struct param_list *params)
{
	if (!saved_cg_name || !saved_data_name)
		VM_ERROR("No saved animation in Util.restore_animation");
	_sys_load_image(saved_cg_name, 1, 8);
	vm_load_data_file(saved_data_name, mem_get_sysvar32(mes_sysvar32_data_offset));
	for (int i = 0; i < 10; i++) {
		if (saved_anim_running[i]) {
			anim_init_stream(i, i);
			anim_start(i);
		}
	}
}

static void util_anim_save_running(struct param_list *params)
{
	bool running = false;
	for (int i = 0; i < 10; i++) {
		saved_anim_running[i] = anim_stream_running(i);
		running |= saved_anim_running[i];
	}
	mem_set_var16(18, running);
}

static void util_copy_progressive(struct param_list *params)
{
	unsigned dst_i = vm_expr_param(params, 1);
	gfx_copy_progressive(64, 0, 512, 288, 2, 64, 0, dst_i);
}

static void util_fade_progressive(struct param_list *params)
{
	unsigned dst_i = vm_expr_param(params, 1);
	gfx_fade_progressive(64, 0, 512, 288, dst_i);
}

static void util_anim_running(struct param_list *params)
{
	mem_set_var16(18, anim_running());
}

// Locations of dream text in JP executable
static uint32_t yume_text_loc[] = {
	0x60fdc,
	0x60fc8,
	0x60fa8,
	0x60f90,
	0x60f68,
	0x60f48,
	0x60f10,
	0x60efc,
	0x60ec0,
	0x60e94,
	0x60e5c,
	0x60e30,
	0x60e04,
	0x60de8,
	0x60dcc,
};

// Locations of dream text in EN executable
static uint32_t yume_text_loc_eng[] = {
	0x60a00,
	0x60a40,
	0x60a80,
	0x60ac0,
	0x60b00,
	0x60b40,
	0x60b80,
	0x60bc0,
	0x60c00,
	0x60c40,
	0x60c80,
	0x60cc0,
	0x60d00,
	0x60d40,
	0x60d80,
	0x60dc0,
	0x60e00,
};

static int read_yume_text(char **dst)
{
	size_t size;
	uint8_t *exe = file_read(config.exe_path, &size);

	uint32_t *addrs = yume_text_loc;
	int nr_lines = ARRAY_SIZE(yume_text_loc);
	if (yuno_eng) {
		addrs = yume_text_loc_eng;
		nr_lines = ARRAY_SIZE(yume_text_loc_eng);
	}

	for (int i = 0; i < nr_lines; i++) {
		if (addrs[i] + 64 >= size) {
			free(exe);
			return -1;
		}
	}
	for (int i = 0; i < nr_lines; i++) {
		dst[i] = xmalloc(64);
		strncpy(dst[i], (char*)exe + addrs[i], 63);
		dst[i][63] = '\0';
	}

	free(exe);
	return nr_lines;
}

static void yuno_draw_text(const char *text);

// TODO: play effect from YUME.BIN
static void util_yume(struct param_list *params)
{
	char *text[17];
	int nr_lines = read_yume_text(text);

	const uint16_t start_x = mem_get_sysvar16(mes_sysvar16_text_start_x);
	const uint16_t start_y = mem_get_sysvar16(mes_sysvar16_text_start_y);

	// XXX: text colors are 0,7, but both are black
	gfx.palette[7] = (SDL_Color) { 255, 255, 255, 255 };
	gfx_update_palette(0, 256);

	gfx_fill(0, 0, 640, 400, 0, 0);
	gfx_display_freeze();
	for (unsigned i = 0; i < nr_lines; i++) {
		// draw text
		mem_set_sysvar16(mes_sysvar16_text_cursor_x, start_x);
		mem_set_sysvar16(mes_sysvar16_text_cursor_y, start_y);
		yuno_draw_text(text[i]);
		gfx_display_fade_in(1000);

		// wait for input
		struct param_list wait_params = { .nr_params = 0 };
		sys_wait(&wait_params);

		// fade out
		gfx_display_fade_out(0, 1000);
		gfx_fill(0, 0, 640, 400, 0, 0);
	}
	gfx_display_unfreeze();

	for (int i = 0; i < nr_lines; i++) {
		free(text[i]);
	}
}

static void util_copy(struct param_list *params)
{
	int src_x = vm_expr_param(params, 1);
	int src_y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - src_x) + 1;
	int h = (vm_expr_param(params, 4) - src_y) + 1;
	unsigned src_i = vm_expr_param(params, 5);
	int dst_x = vm_expr_param(params, 6);
	int dst_y = vm_expr_param(params, 7);
	unsigned dst_i = vm_expr_param(params, 8);
	gfx_copy(src_x, src_y, w, h, src_i, dst_x, dst_y, dst_i);
}

static void util_bgm_play(struct param_list *params)
{
	audio_bgm_play(vm_string_param(params, 1), false);
}

static void util_bgm_is_playing(struct param_list *params)
{
	mem_set_var16(18, audio_is_playing(AUDIO_CH_BGM));
}

static void util_se_is_playing(struct param_list *params)
{
	mem_set_var16(18, audio_is_playing(AUDIO_CH_SE(0)));
}

static void util_get_ticks(struct param_list *params)
{
	mem_set_var32(16, vm_get_ticks());
}

static void util_wait_until(struct param_list *params)
{
	if (!vm.procedures[110].code || !vm.procedures[111].code)
		VM_ERROR("procedures 110-111 not defined in Util.wait_until");

	uint32_t stop_t = vm_expr_param(params, 1);
	vm_timer_t t = vm_timer_create();
	do {
		vm_peek();
		if (input_down(INPUT_ACTIVATE)) {
			vm_call_procedure(110);
			return;
		} else if (input_down(INPUT_CANCEL)) {
			vm_call_procedure(111);
			return;
		}

		vm_timer_tick(&t, 16);
	} while (t < stop_t);
}

static void util_wait_until2(struct param_list *params)
{
	uint32_t stop_t = vm_expr_param(params, 1);
	vm_timer_t t = vm_timer_create();
	while (t < stop_t) {
		vm_peek();
		vm_timer_tick(&t, 16);
	}
}

static void util_bgm_is_fading(struct param_list *params)
{
	mem_set_var32(13, audio_is_fading(AUDIO_CH_BGM));
}

#define X 0xff
#define _ 0x00
static const uint8_t yuno_reflector_mask[] = {
	_,_,_,_,_,_,X,X,X,X,X,X,X,X,X,_,_,_,_,_,_,
	_,_,_,_,_,X,X,X,X,X,X,X,X,X,X,X,_,_,_,_,_,
	_,_,_,_,X,X,X,X,X,X,X,X,X,X,X,X,X,_,_,_,_,
	_,_,_,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,_,_,_,
	_,_,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,_,_,
	_,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,_,
	X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,
	X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,
	X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,
	X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,
	X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,
	_,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,_,
	_,_,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,_,_,
	_,_,_,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,_,_,_,
	_,_,_,_,X,X,X,X,X,X,X,X,X,X,X,X,X,_,_,_,_,
	_,_,_,_,_,X,X,X,X,X,X,X,X,X,X,X,_,_,_,_,_,
	_,_,_,_,_,_,X,X,X,X,X,X,X,X,X,_,_,_,_,_,_,
};
#undef _
#undef X

#define W 21
#define H 17
_Static_assert(sizeof(yuno_reflector_mask) == W*H);

static uint8_t yuno_reflector_frame0[W*H];
static uint8_t yuno_reflector_frame1[W*H];
static uint8_t yuno_reflector_frame2[W*H];
static uint8_t yuno_reflector_frame3[W*H];

static uint8_t *yuno_reflector_frames[6] = {
	yuno_reflector_frame0,
	yuno_reflector_frame1,
	yuno_reflector_frame2,
	yuno_reflector_frame3,
	yuno_reflector_frame2,
	yuno_reflector_frame1,
};

//  0 -> 11
// 11 -> 12
// 12 -> 7
static void generate_frame(uint8_t prev[W*H], uint8_t frame[W*H])
{
	for (int i = 0; i < W*H; i++) {
		if (!yuno_reflector_mask[i])
			frame[i] = 1;
		else if (prev[i] == 0)
			frame[i] = 11;
		else if (prev[i] == 11)
			frame[i] = 12;
		else if (prev[i] == 12)
			frame[i] = 7;
		else if (prev[i] == 7)
			frame[i] = 7;
		else
			WARNING("Unexpected color: %u", prev[i]);
	}
}

// location of base frame in MAPORB.GP8
#define MAPORB_X 21
#define MAPORB_Y 69

static void generate_reflector_frames(void)
{
	// load CG containing base frame
	struct cg *cg = asset_cg_load("maporb.gp8");
	if (!cg) {
		WARNING("Failed to decode CG: MAPORB.GP8");
		return;
	}
	if (cg->metrics.w < MAPORB_X + W || cg->metrics.h < MAPORB_Y + H) {
		cg_free(cg);
		WARNING("Unexpected dimensions for CG: MAPORB.GP8");
		return;
	}

	// load base frame
	uint8_t *base = cg->pixels + MAPORB_Y * cg->metrics.w + MAPORB_X;
	for (int row = 0; row < H; row++) {
		uint8_t *src = base + row * cg->metrics.w;
		uint8_t *dst = yuno_reflector_frame0 + row * W;
		for (int col = 0; col < W; col++, src++, dst++) {
			if (!yuno_reflector_mask[row * W + col])
				*dst = 1;
			else
				*dst = *src;
		}
	}
	cg_free(cg);

	// generate subsequent frames from base frame
	generate_frame(yuno_reflector_frame0, yuno_reflector_frame1);
	generate_frame(yuno_reflector_frame1, yuno_reflector_frame2);
	generate_frame(yuno_reflector_frame2, yuno_reflector_frame3);
}

#define DRAW_X 581
#define DRAW_Y 373

static void draw_frame(uint8_t frame[W*H])
{
	SDL_Surface *s = gfx_get_surface(gfx.screen);
	uint8_t *base = s->pixels + DRAW_Y * s->pitch + DRAW_X;
	for (int row = 0; row < H; row++) {
		uint8_t *dst = base + row * s->pitch;
		uint8_t *src = &frame[row*W];
		for (int i = 0; i < W; i++, src++, dst++) {
			if (*src != 1)
				*dst = *src;
		}
	}
}

#define FRAME_TIME 250

static void yuno_reflector_animation(void)
{
	static uint32_t t = 0;
	static unsigned frame = 0;
	static bool initialized = false;
	if (!initialized) {
		generate_reflector_frames();
		initialized = true;
	}

	uint32_t now_t = vm_get_ticks();
	if (now_t - t < FRAME_TIME)
		return;

	draw_frame(yuno_reflector_frames[frame]);
	frame = (frame + 1) % ARRAY_SIZE(yuno_reflector_frames);
	t = now_t;
	gfx_screen_dirty();
}

// character sizes for MS PGothic
static unsigned char_size_p[128] = {
	[' '] = 6,
	['!'] = 5,
	['"'] = 9,
	['#'] = 9,
	['$'] = 9,
	['%'] = 9,
	['&'] = 11,
	['\''] = 4,
	['('] = 6,
	[')'] = 6,
	['*'] = 9,
	['+'] = 9,
	[','] = 4,
	['-'] = 9,
	['.'] = 4,
	['/'] = 9,
	['0'] = 9,
	['1'] = 9,
	['2'] = 9,
	['3'] = 9,
	['4'] = 9,
	['5'] = 9,
	['6'] = 9,
	['7'] = 9,
	['8'] = 9,
	['9'] = 9,
	[':'] = 4,
	[';'] = 4,
	['<'] = 9,
	['='] = 9,
	['>'] = 9,
	['?'] = 8,
	['@'] = 12,
	['A'] = 11,
	['B'] = 11,
	['C'] = 12,
	['D'] = 11,
	['E'] = 10,
	['F'] = 10,
	['G'] = 12,
	['H'] = 11,
	['I'] = 5,
	['J'] = 10,
	['K'] = 11,
	['L'] = 10,
	['M'] = 13,
	['N'] = 11,
	['O'] = 12,
	['P'] = 11,
	['Q'] = 12,
	['R'] = 11,
	['S'] = 11,
	['T'] = 10,
	['U'] = 11,
	['V'] = 11,
	['W'] = 13,
	['X'] = 11,
	['Y'] = 10,
	['Z'] = 10,
	['['] = 6,
	['\\'] = 9,
	[']'] = 6,
	['^'] = 8,
	['_'] = 6,
	['`'] = 8,
	['a'] = 9,
	['b'] = 9,
	['c'] = 9,
	['d'] = 9,
	['e'] = 9,
	['f'] = 6,
	['g'] = 8,
	['h'] = 9,
	['i'] = 4,
	['j'] = 5,
	['k'] = 8,
	['l'] = 4,
	['m'] = 13,
	['n'] = 9,
	['o'] = 9,
	['p'] = 9,
	['q'] = 9,
	['r'] = 7,
	['s'] = 8,
	['t'] = 7,
	['u'] = 9,
	['v'] = 9,
	['w'] = 11,
	['x'] = 8,
	['y'] = 9,
	['z'] = 8,
	['{'] = 5,
	['|'] = 5,
	['}'] = 5,
	['~'] = 8,
};

static unsigned en_char_size(unsigned ch)
{
	if (gfx.text.size != 16)
		return gfx_text_size_char(ch);

	if (ch < 128) {
		if (char_size_p[ch])
			return char_size_p[ch];
	}
	// full-width ':'
	if (ch == 0xff1a)
		return 9;
	// full-width space
	if (ch == 0x3000)
		return 12;
	return gfx_text_size_char(ch);
}

static void yuno_eng_draw_text(const char *text)
{
	static uint16_t x_last = 0;
	static uint16_t x_col_last = 0;
	static uint16_t y_last = 0;

	// XXX: System.text_cursor_x stores the text position as a multiple of
	//      8, but AI5ENG.EXE continues from the precise position when
	//      drawing characters individually. Hence this hack.
	uint16_t x = mem_get_sysvar16(mes_sysvar16_text_cursor_x);
	uint16_t y = mem_get_sysvar16(mes_sysvar16_text_cursor_y);
	if (x == x_col_last && y == y_last)
		x = x_last;
	else
		x *= 8;

	const unsigned surface = mem_get_sysvar16(mes_sysvar16_dst_surface);
	while (*text) {
		int ch;
		text = sjis_char2unicode(text, &ch);
		gfx_text_draw_glyph(x, y, surface, ch);
		x += en_char_size(ch);
	}

	x_last = x;
	x_col_last = ((x+7u) & ~7u) / 8;
	y_last = y;
	mem_set_sysvar16(mes_sysvar16_text_cursor_x, x_col_last);
	mem_set_sysvar16(mes_sysvar16_text_cursor_y, y_last);
}

static void yuno_draw_text(const char *text)
{
	if (yuno_eng) {
		yuno_eng_draw_text(text);
		return;
	}

	vm_draw_text(text, 8);
}

static void yuno_update(void)
{
	if (vm_flag_is_on(FLAG_REFLECTOR)) {
		if (gfx_current_surface() != 1 || mem_get_var4(21) != 1)
			yuno_reflector_animation();
	}
}

static void yuno_init(void)
{
	han_line_breaks = true;
	asset_effect_is_bgm = false;
}

struct game game_yuno = {
	.id = GAME_YUNO,
	.surface_sizes = {
		{ 640, 400 },
		{ 640, 400 },
		{ 640, 768 },
		{ 640, 768 },
		{ 1696, 720 },
		{ 0, 0 },
	},
	.bpp = 8,
	.var4_size = VAR4_SIZE,
	.mem16_size = MEM16_SIZE,
	.init = yuno_init,
	.update = yuno_update,
	.mem_init = yuno_mem_init,
	.mem_restore = yuno_mem_restore,
	.draw_text_zen = yuno_draw_text,
	.draw_text_han = yuno_draw_text,
	.vm = VM_AI5,
	.expr_op = { CLASSICS_EXPR_OP },
	.stmt_op = {
		CLASSICS_STMT_OP,
		[0x03] = vm_stmt_set_flag_const16_4bit_wrap,
		[0x05] = vm_stmt_set_flag_expr_4bit_wrap,
		[0x0f] = vm_stmt_mescall_save_procedures,
	},
	.sys = {
		[0] = sys_set_font_size,
		[1] = sys_display_number,
		[2] = classics_cursor,
		[3] = yuno_anim,
		[4] = yuno_savedata,
		[5] = classics_audio,
		[6] = NULL,
		[7] = sys_file,
		[8] = yuno_load_image,
		[9] = classics_palette,
		[10] = yuno_graphics,
		[11] = sys_wait,
		[12] = sys_set_text_colors_indexed,
		[13] = sys_farcall,
		[14] = classics_get_cursor_segment,
		[15] = sys_menu_get_no,
		[18] = sys_check_input,
		[21] = sys_strlen,
		[22] = sys_22,
		[23] = yuno_set_screen_surface,
	},
	.util = {
		[1] = classics_get_text_colors,
		[3] = util_noop,
		[5] = util_blink_fade,
		[6] = util_scale_h,
		[8] = util_invert_colors,
		[10] = util_fade,
		[11] = util_savedata_stash_name,
		[12] = util_pixelate,
		[14] = util_get_time,
		[15] = util_check_cursor,
		[16] = util_delay,
		[17] = util_save_animation,
		[18] = util_restore_animation,
		[19] = util_anim_save_running,
		[20] = util_copy_progressive,
		[21] = util_fade_progressive,
		[22] = util_anim_running,
		[26] = util_yume,
		[27] = util_warn_unimplemented,
		[100] = util_warn_unimplemented,
		[101] = util_warn_unimplemented,
		[200] = util_copy,
		[201] = util_bgm_play,
		[202] = util_bgm_is_playing,
		[203] = util_se_is_playing,
		[210] = util_get_ticks,
		[211] = util_wait_until,
		[212] = util_wait_until2,
		[213] = util_warn_unimplemented,
		[214] = util_bgm_is_fading,
	},
	.flags = {
		[FLAG_REFLECTOR]    = 0x0002,
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
