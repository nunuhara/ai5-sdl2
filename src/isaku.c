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
#include "ai5/anim.h"
#include "ai5/mes.h"

#include "anim.h"
#include "audio.h"
#include "cursor.h"
#include "game.h"
#include "gfx_private.h"
#include "input.h"
#include "savedata.h"
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

static void isaku_sys_cursor(struct param_list *params)
{
	static uint32_t uk = 0;
	switch (vm_expr_param(params, 0)) {
	case 0: cursor_show(); break;
	case 1: cursor_hide(); break;
	case 2: sys_cursor_save_pos(params); break;
	case 3: cursor_set_pos(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 4: cursor_load(vm_expr_param(params, 1) + 15); break;
	case 5: uk = 0; break;
	case 6: mem_set_var16(18, 0); break;
	case 7: mem_set_var32(18, uk); break;
	case 8: uk = vm_expr_param(params, 1); break;
	default: VM_ERROR("System.Cursor.function[%u] not implemented",
				 params->params[0].val);
	}
}

static unsigned vm_anim_param(struct param_list *params, unsigned i)
{
	unsigned a = vm_expr_param(params, i);
	unsigned b = vm_expr_param(params, i+1);
	unsigned stream = a * 10 + b;
	if (a >= ANIM_MAX_STREAMS)
		VM_ERROR("Invalid animation stream index: %u:%u", a, b);
	return stream;
}

static void isaku_sys_anim(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: anim_init_stream(vm_anim_param(params, 1), vm_anim_param(params, 1)); break;
	case 1: anim_start(vm_anim_param(params, 1)); break;
	case 2: anim_stop(vm_anim_param(params, 1)); break;
	case 3: anim_halt(vm_anim_param(params, 1)); break;
	case 4: anim_wait(vm_anim_param(params, 1)); break;
	case 5: anim_stop_all(); break;
	case 6: anim_halt_all(); break;
	case 7: anim_reset_all(); break;
	default: VM_ERROR("System.Anim.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void isaku_sys_savedata(struct param_list *params)
{
	char save_name[7];
	uint32_t save_no = vm_expr_param(params, 1);
	if (save_no > 99)
		VM_ERROR("Invalid save number: %u", save_no);
	sprintf(save_name, "FLAG%02u", save_no);

	switch (vm_expr_param(params, 0)) {
	case 0: savedata_resume_load(save_name); break;
	case 1: savedata_resume_save(save_name); break;
	case 2: savedata_load(save_name); break;
	case 3: savedata_save_union_var4(save_name); break;
	//case 4: savedata_load_isaku_vars(save_name); break;
	//case 5: savedata_save_isaku_vars(save_name); break;
	//case 6: savedata_clear_var4(save_name); break;
	default: VM_ERROR("System.SaveData.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void isaku_bgm_fade_out_sync(void)
{
	audio_bgm_fade(0, 500, true, false);
	while (audio_bgm_is_playing()) {
		if (input_down(INPUT_SHIFT))
			break;
		vm_peek();
		vm_delay(16);
	}
}

static void isaku_se_fade_out_sync(void)
{
	audio_se_fade(0, 500, true, false);
	while (audio_se_is_playing()) {
		if (input_down(INPUT_SHIFT))
			break;
		vm_peek();
		vm_delay(16);
	}
}

static void isaku_se_wait(void)
{
	while (audio_se_is_playing()) {
		vm_peek();
		vm_delay(16);
	}
}

static void isaku_sys_audio(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: audio_bgm_play(vm_string_param(params, 1), true); break;
	case 1: audio_bgm_fade(0, 2000, true, false); break;
	case 2: audio_bgm_stop(); break;
	case 3: audio_se_play(vm_string_param(params, 1)); break;
	case 4: audio_se_stop(); break;
	case 5: audio_se_fade(0, 2000, true, false); break;
	//case 6: audio_bgm_play_sync(vm_string_param(params, 1)); break; // NOT USED
	case 7: isaku_bgm_fade_out_sync(); break;
	case 8: isaku_se_fade_out_sync(); break;
	case 9: isaku_se_wait(); break;
	default: VM_ERROR("System.Audio.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void isaku_sys_voice(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: audio_voice_play(vm_string_param(params, 1)); break;
	case 1: audio_voice_stop(); break;
	default: WARNING("System.Voice.function[%u] not implemented",
				 params->params[0].val);
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

static void isaku_sys_graphics(struct param_list *params)
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
	default:
//		WARNING("System.ItemWindow.function[%u] not implemented",
//				params->params[0].val);
	}
}

static void isaku_sys_farcall_strlen(struct param_list *params)
{
	vm_flag_on(FLAG_STRLEN);
	sys_farcall(params);
	vm_flag_off(FLAG_STRLEN);
}

static void sys_25(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 1:
		WARNING("System.function[25].function[1] not implemented");
		break;
	default:
		VM_ERROR("System.function[25].function[%u] not implemented",
				params->params[0].val);
	}
}

static void sys_26(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 1:
		WARNING("System.function[26].function[1] not implemented");
		break;
	default:
		VM_ERROR("System.function[26].function[%u] not implemented",
				params->params[0].val);
	}
}

static void sys_27(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	default:
		WARNING("System.function[27].function[%u] not implemented",
				params->params[0].val);
	}
}

static void isaku_util_delay(struct param_list *params)
{
	vm_timer_t t = vm_timer_create();
	uint32_t stop_t = t + vm_expr_param(params, 1) * 16;
	mem_set_var32(18, 0);
	while (t < stop_t) {
		if (input_down(INPUT_SHIFT))
			return;
		if (input_down(INPUT_CANCEL)) {
			mem_set_var32(18, 1);
			return;
		}
		vm_peek();
		vm_timer_tick(&t, 16);
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
	.use_effect_arc = false,
	.persistent_volume = false,
	.var4_size = VAR4_SIZE,
	.mem16_size = MEM16_SIZE,
	.mem_init = isaku_mem_init,
	.mem_restore = isaku_mem_restore,
	.sys = {
		[0]  = sys_set_font_size,
		[1]  = sys_display_number,
		[2]  = isaku_sys_cursor,
		[3]  = isaku_sys_anim,
		[4]  = isaku_sys_savedata,
		[5]  = isaku_sys_audio,
		[6]  = isaku_sys_voice,
		[7]  = sys_load_file,
		[8]  = sys_load_image,
		[9]  = sys_display,
		[10] = isaku_sys_graphics,
		[11] = sys_wait,
		[12] = sys_set_text_colors_direct,
		[13] = sys_farcall,
		[14] = sys_get_cursor_segment,
		[15] = sys_menu_get_no,
		[18] = sys_check_input,
		[22] = sys_item_window,
		[24] = isaku_sys_farcall_strlen,
		[25] = sys_25,
		[26] = sys_26,
		[27] = sys_27,
	},
	.util = {
		[7]  = isaku_util_delay,
		[11] = util_warn_unimplemented,
		[12] = util_warn_unimplemented,
	},
	.flags = {
		[FLAG_MENU_RETURN] = 0x0008,
		[FLAG_RETURN]      = 0x0010,
		[FLAG_STRLEN]      = 0x0400,
	}
};
