/* Copyright (C) 2024 Nunuhara Cabbage <nunuhara@haniwa.technology>
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
#include "ai5/anim.h"

#include "ai5.h"
#include "anim.h"
#include "audio.h"
#include "backlog.h"
#include "cursor.h"
#include "game.h"
#include "gfx.h"
#include "input.h"
#include "memory.h"
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
#define VAR32_OFF    (SYSVAR16_OFF + 24 * 2)
#define SYSVAR32_OFF (VAR32_OFF + 26 * 4)
#define HEAP_OFF     (SYSVAR32_OFF + 61 * 4)
_Static_assert(HEAP_OFF == 0x1244);

#define SCREEN_W 640
#define SCREEN_H 480

static void beyond_mem_restore(void)
{
	// XXX: In AI5WIN.EXE, these are 32-bit pointers into the VM's own
	//      address space. Since we support 64-bit systems, we treat
	//      32-bit pointers as offsets into the `memory` struct (similar
	//      to how AI5WIN.EXE treats 16-bit pointers).
	mem_set_sysvar16_ptr(SYSVAR16_OFF);
	mem_set_sysvar32(mes_sysvar32_memory, offsetof(struct memory, mem16));
	mem_set_sysvar32(mes_sysvar32_file_data, offsetof(struct memory, file_data));
	mem_set_sysvar32(mes_sysvar32_menu_entry_addresses,
			offsetof(struct memory, menu_entry_addresses));
	mem_set_sysvar32(mes_sysvar32_menu_entry_numbers,
			offsetof(struct memory, menu_entry_numbers));

	uint16_t flags = mem_get_sysvar16(mes_sysvar16_flags);
	mem_set_sysvar16(mes_sysvar16_flags, flags | 4);
	mem_set_sysvar16(0, HEAP_OFF);
	mem_set_sysvar32(10, 0);
}

static void beyond_mem_init(void)
{
	// set up pointer table for memory access
	memory_ptr.mes_name = memory_raw;
	memory_ptr.var4 = memory_raw + VAR4_OFF;
	memory_ptr.system_var16_ptr = memory_raw + SV16_PTR_OFF;
	memory_ptr.var16 = memory_raw + VAR16_OFF;
	memory_ptr.system_var16 = memory_raw + SYSVAR16_OFF;
	memory_ptr.var32 = memory_raw + VAR32_OFF;
	memory_ptr.system_var32 = memory_raw + SYSVAR32_OFF;

	mem_set_sysvar16(mes_sysvar16_flags, 0xf);
	mem_set_sysvar16(mes_sysvar16_text_start_x, 0);
	mem_set_sysvar16(mes_sysvar16_text_start_y, 0);
	mem_set_sysvar16(mes_sysvar16_text_end_x, SCREEN_W);
	mem_set_sysvar16(mes_sysvar16_text_end_y, SCREEN_H);
	mem_set_sysvar16(mes_sysvar16_font_width, 16);
	mem_set_sysvar16(mes_sysvar16_font_height, 16);
	mem_set_sysvar16(mes_sysvar16_char_space, 16);
	mem_set_sysvar16(mes_sysvar16_line_space, 16);
	mem_set_sysvar16(mes_sysvar16_mask_color, 0x3e0);

	mem_set_sysvar32(mes_sysvar32_cg_offset, 0x20000);
	beyond_mem_restore();
}

static void beyond_cursor(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: cursor_show(); break;
	case 1: cursor_hide(); break;
	case 2: sys_cursor_save_pos(params); break;
	case 3: cursor_set_pos(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	//case 4: break; // TODO
	//case 5: break; // TODO
	//case 6: break; // TODO
	//case 7: break; // TODO
	//case 8: break; // TODO
	case 9: mem_set_var32(18, 0); break; // TODO: ???
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

static void beyond_anim(struct param_list *params)
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
	case 8: anim_unpause_all(); break;
	case 9: anim_init_stream_from(vm_anim_param(params, 1), vm_anim_param(params, 1),
				mem_get_var32(2)); break;
	case 10: anim_init_stream_from(vm_anim_param(params, 1), vm_anim_param(params, 1),
				 vm_expr_param(params, 3)); break;
	//case 11:
	case 12: mem_set_var16(18, !anim_stream_running(vm_anim_param(params, 1))); break;
	case 13: mem_set_var16(18, anim_running()); break;
	default: VM_ERROR("System.Anim.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void beyond_resume_load(const char *save_name)
{
	uint8_t buf[VAR4_OFF + VAR4_SIZE];
	uint8_t *var4 = buf + VAR4_OFF;
	uint8_t *mem_var4 = memory_raw + VAR4_OFF;
	savedata_read(save_name, memory_raw, 0, MEM16_SIZE);
	savedata_read("FLAG00", buf, VAR4_OFF, VAR4_SIZE);

	memcpy(mem_var4 +  200, var4 +  200, 800);
	memcpy(mem_var4 + 1200, var4 + 1200, 800);
	memcpy(mem_var4 + 2030, var4 + 2030, 12);
	memcpy(mem_var4 + 2100, var4 + 2100, 1900);
	mem_var4[2015] = 1;

	beyond_mem_restore();
	vm_load_mes(mem_mes_name());
	vm_flag_on(FLAG_RETURN);
}

static void beyond_load_extra_var32(const char *save_name)
{
	savedata_read(save_name, memory_raw, SYSVAR32_OFF + (11 * 4), 50 * 4);
}

static void beyond_save_extra_var32(const char *save_name)
{
	savedata_write(save_name, memory_raw, SYSVAR32_OFF + (11 * 4), 50 * 4);
}

static void beyond_load_heap(const char *save_name, int start, int count)
{
	if (count <= 0 || start < 0 || start + count > 3516) {
		WARNING("Invalid heap load: %d+%d", start, count);
		return;
	}
	savedata_read(save_name, memory_raw, HEAP_OFF + start, count);
}

static void beyond_save_heap(const char *save_name, int start, int count)
{
	if (count <= 0 || start < 0 || start + count > 3516) {
		WARNING("Invalid heap save: %d+%d", start, count);
		return;
	}
	savedata_write(save_name, memory_raw, HEAP_OFF + start, count);
}

static void beyond_savedata(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: beyond_resume_load(sys_save_name(params)); break;
	case 1: savedata_resume_save(sys_save_name(params)); break;
	case 2: savedata_load_var4_restore(sys_save_name(params)); break;
	case 3: savedata_save_union_var4(sys_save_name(params)); break;
	case 4: beyond_load_extra_var32(sys_save_name(params)); break;
	case 5: beyond_save_extra_var32(sys_save_name(params)); break;
	case 6: memset(memory_raw + VAR4_OFF, 0, VAR4_SIZE); break;
	case 7: beyond_load_heap(sys_save_name(params), vm_expr_param(params, 2),
				vm_expr_param(params, 3)); break;
	case 8: beyond_save_heap(sys_save_name(params), vm_expr_param(params, 2),
				vm_expr_param(params, 3)); break;
	default: VM_ERROR("System.SaveData.function[%u] not implemented",
				 params->params[0].val);
	}
}

#define VAR4_BGM_FADING 4020
#define VAR4_SE_FADING  4021

static void beyond_audio(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0:
		mem_set_var4(VAR4_BGM_FADING, 0);
		audio_bgm_play(vm_string_param(params, 1), true);
		break;
	case 1:
		mem_set_var4(VAR4_BGM_FADING, 0);
		audio_stop(AUDIO_CH_BGM);
		break;
	case 2:
		mem_set_var4(VAR4_BGM_FADING, 1);
		audio_fade(AUDIO_CH_BGM, AUDIO_VOLUME_MIN, 3000, true, false);
		break;
	case 3:
		audio_fade(AUDIO_CH_BGM, AUDIO_VOLUME_MIN, 3000, true, false);
		while (audio_is_playing(AUDIO_CH_BGM)) {
			if (mem_get_var4(2019) == 1 || mem_get_var4(2020) == 1)
				break;
			if (input_down(INPUT_ACTIVATE) || input_down(INPUT_CTRL))
				break;
		}
		audio_stop(AUDIO_CH_BGM);
		break;
	case 4:
		mem_set_var4(VAR4_SE_FADING, 0);
		audio_se_play(vm_string_param(params, 1), vm_expr_param(params, 2));
		break;
	case 5:
		mem_set_var4(VAR4_SE_FADING, 0);
		audio_se_stop(vm_expr_param(params, 1));
		break;
	case 6:
		mem_set_var4(VAR4_SE_FADING, 1);
		audio_se_fade(AUDIO_VOLUME_MIN, 3000, true, false, vm_expr_param(params, 1));
		break;
	case 7: {
		int ch = vm_expr_param(params, 1);
		if (!audio_se_channel_valid(ch)) {
			WARNING("Invalid SE channel: %u", ch);
			break;
		}
		while (audio_is_playing(AUDIO_CH_SE(ch))) {
			if (input_down(INPUT_ACTIVATE) || input_down(INPUT_CTRL))
				break;
		}
		break;
	case 8: {
		int vol = clamp(-5000, 0, vm_expr_param(params, 1));
		audio_set_volume(AUDIO_CH_BGM, vol);
		break;
	}
	case 9: {
		int vol = clamp(0, 4, config.volume.music);
		audio_set_volume(AUDIO_CH_BGM, (vol - 4) * 1000);
		break;
	}
	case 10:
		mem_set_var16(18, audio_is_playing(AUDIO_CH_BGM));
		break;
	}
	default: VM_ERROR("System.Audio.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void beyond_voice(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	default: VM_ERROR("System.Voice.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void beyond_display(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	default: VM_ERROR("System.Display.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void beyond_graphics_blend_masked(struct param_list *params)
{
	int a_x = vm_expr_param(params, 1);
	int a_y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - a_x) + 1;
	int h = (vm_expr_param(params, 4) - a_y) + 1;
	unsigned a_i = vm_expr_param(params, 5);
	int b_x = vm_expr_param(params, 6);
	int b_y = vm_expr_param(params, 7);
	unsigned b_i = vm_expr_param(params, 8);
	int dst_x = vm_expr_param(params, 9);
	int dst_y = vm_expr_param(params, 10);
	unsigned dst_i = vm_expr_param(params, 11);
	uint8_t *mask = memory_raw + vm_expr_param(params, 12);
	gfx_blend_with_mask_color_to(a_x, a_y, w, h, a_i, b_x, b_y, b_i, dst_x, dst_y, dst_i,
			le_get16(mask, 0), le_get16(mask, 2), mask + 4);
}

static void beyond_graphics_crossfade(struct param_list *params)
{
	int src_x = vm_expr_param(params, 1);
	int src_y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - src_x) + 1;
	int h = (vm_expr_param(params, 4) - src_y) + 1;
	unsigned src_i = vm_expr_param(params, 5);
	int new_x = vm_expr_param(params, 6);
	int new_y = vm_expr_param(params, 7);
	unsigned new_i = vm_expr_param(params, 8);
	int dst_x = vm_expr_param(params, 9);
	int dst_y = vm_expr_param(params, 10);
	unsigned dst_i = vm_expr_param(params, 11);

	vm_timer_t timer = vm_timer_create();
	for (unsigned a = 0; a < 255; a += 8) {
		gfx_copy(src_x, src_y, w, h, src_i, dst_x, dst_y, dst_i);
		gfx_blend(new_x, new_y, w, h, new_i, dst_x, dst_y, dst_i, a);
		vm_peek();
		vm_timer_tick(&timer, 33);
	}
	gfx_copy(new_x, new_y, w, h, new_i, dst_x, dst_y, dst_i);
}

static void beyond_graphics(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: sys_graphics_copy(params); break;
	case 1: sys_graphics_copy_masked(params); break;
	case 2: sys_graphics_fill_bg(params); break;
	case 3: sys_graphics_copy_swap(params); break;
	case 4: sys_graphics_swap_bg_fg(params); break;
	case 5: sys_graphics_copy_progressive(params); break;
	case 6: sys_graphics_compose(params); break;
	case 10: beyond_graphics_blend_masked(params); break;
	case 11: beyond_graphics_crossfade(params); break;
	default: VM_ERROR("System.Graphics.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void beyond_backlog(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: backlog_clear(); break;
	case 1: backlog_prepare(); break;
	case 2: backlog_commit(); break;
	case 3: mem_set_var32(18, backlog_count()); break;
	case 4: mem_set_var32(18, backlog_get_pointer(vm_expr_param(params, 1))); break;
	case 5: mem_set_var16(18, backlog_has_voice(vm_expr_param(params, 1))); break;
	default: VM_ERROR("System.Backlog.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void beyond_overlay(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	default: WARNING("System.Overlay.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void util_6(struct param_list *params)
{
	char *heap = (char*)memory_raw + HEAP_OFF;
	switch (vm_expr_param(params, 1)) {
	case 1:
		mem_set_var4(4000, 1);
		strcpy(heap + 100, vm_string_param(params, 2));
		break;
	case 2:
		mem_set_var4(4005, 1);
		strcpy(heap + 116, vm_string_param(params, 2));
		strcpy(heap + 132, vm_string_param(params, 3));
		break;
	case 4:
		mem_set_var4(4003, 1);
		strcpy(heap + 180, vm_string_param(params, 2));
		break;
	case 5:
		mem_set_var4(4004, 1);
		strcpy(heap + 196, vm_string_param(params, 2));
		break;
	}
}

static void beyond_set_volume(struct param_list *params)
{
	int which = vm_expr_param(params, 1);
	int vol = vm_expr_param(params, 2);
	if (vol < 0 || vol > 4) {
		WARNING("Invalid volume: %d", vol);
		vol = clamp(0, 4, vol);
	}
	switch (which) {
	case 0:
		which = AUDIO_CH_BGM;
		config.volume.music = vol;
		break;
	case 1:
		which = AUDIO_CH_VOICE(0);
		config.volume.voice = vol;
		break;
	default:
		which = AUDIO_CH_SE(0);
		config.volume.effect = vol;
		break;
	}

	audio_set_volume(which, (vol - 4) * 1000);
	// TODO: save to .ini
}

static void beyond_get_ini_values(struct param_list *params)
{
	mem_set_var16(0, config.volume.music);
	mem_set_var16(1, config.volume.effect);
	mem_set_var16(2, config.volume.voice);
	mem_set_var16(3, 4); // SPEED
	mem_set_var16(4, 1); // SKIP
}

struct game game_beyond = {
	.id = 0,
	.surface_sizes = {
		{ 640, 480 },
		{ 1280, 1280 },
		{ 640, 480 },
		{ 640, 960 },
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 480 },
		{ 768, 440 },
		{ 472, 104 },
		{ 32,  480 },
		{ 640, 240 },
		{ 320, 240 },
		{ 0, 0 }
	},
	.bpp = 16,
	.var4_size = VAR4_SIZE,
	.mem16_size = MEM16_SIZE,
	.mem_init = beyond_mem_init,
	.mem_restore = beyond_mem_restore,
	.expr_op = { DEFAULT_EXPR_OP },
	.stmt_op = { DEFAULT_STMT_OP },
	.sys = {
		[0]  = sys_set_font_size,
		[1]  = sys_display_number,
		[2]  = beyond_cursor,
		[3]  = beyond_anim,
		[4]  = beyond_savedata,
		[5]  = beyond_audio,
		[6]  = beyond_voice,
		[7]  = sys_load_file,
		[8]  = sys_load_image,
		[9]  = beyond_display,
		[10] = beyond_graphics,
		[11] = sys_wait,
		[12] = sys_set_text_colors_direct,
		[13] = sys_farcall,
		[14] = sys_get_cursor_segment,
		[15] = sys_menu_get_no,
		[16] = sys_get_time,
		[17] = util_noop,
		[18] = sys_check_input,
		[19] = beyond_backlog,
		[20] = util_noop,
		[21] = sys_strlen,
		[22] = beyond_overlay,
		[23] = NULL, // IME
	},
	.util = {
		[6]  = util_6,
		[11] = beyond_set_volume,
		[12] = beyond_get_ini_values,
	},
	.flags = {
		[FLAG_MENU_RETURN]  = 0x0008,
		[FLAG_RETURN]       = 0x0010,
		[FLAG_LOG_ENABLE]   = 0x0020,
		[FLAG_LOG_TEXT]     = 0x0040,
		[FLAG_LOG]          = 0x0080,
		[FLAG_VOICE_ENABLE] = 0x0100,
		[FLAG_AUDIO_ENABLE] = FLAG_ALWAYS_ON,
		[FLAG_STRLEN]       = 0x0400,
		[FLAG_WAIT_KEYUP]   = FLAG_ALWAYS_ON,
		[FLAG_SKIP_KEYUP]   = 0x0800,
		[FLAG_LOG_SYS]      = 0x1000,
	}
};
