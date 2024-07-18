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

#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "nulib.h"
#include "nulib/file.h"
#include "ai5/anim.h"
#include "ai5/arc.h"
#include "ai5/cg.h"

#include "anim.h"
#include "asset.h"
#include "audio.h"
#include "backlog.h"
#include "cursor.h"
#include "game.h"
#include "gfx_private.h"
#include "input.h"
#include "map.h"
#include "movie.h"
#include "savedata.h"
#include "sys.h"
#include "vm_private.h"

#define VAR4_SIZE 4096
#define MEM16_SIZE 8192

#define VAR16_OFF    (MEMORY_MES_NAME_SIZE + VAR4_SIZE + 4)
#define SYSVAR16_OFF (VAR16_OFF + 26 * 2)
#define VAR32_OFF    (SYSVAR16_OFF + 26 * 2)
#define SYSVAR32_OFF (VAR32_OFF + 26 * 4)
#define HEAP_OFF     (SYSVAR32_OFF + 211 * 4)

static void doukyuusei_mem_restore(void)
{
	mem_set_sysvar16_ptr(SYSVAR16_OFF);
	mem_set_sysvar32(mes_sysvar32_memory, offsetof(struct memory, mem16));
	mem_set_sysvar32(mes_sysvar32_file_data, offsetof(struct memory, file_data));
	mem_set_sysvar32(mes_sysvar32_menu_entry_addresses,
		offsetof(struct memory, menu_entry_addresses));
	mem_set_sysvar32(mes_sysvar32_menu_entry_numbers,
		offsetof(struct memory, menu_entry_numbers));
	mem_set_sysvar32(mes_sysvar32_map_data,
		offsetof(struct memory, map_data));

	uint16_t flags = mem_get_sysvar16(mes_sysvar16_flags);
	mem_set_sysvar16(mes_sysvar16_flags, flags | 0x20);
	mem_set_sysvar16(0, HEAP_OFF);
}

static void doukyuusei_mem_init(void)
{
	// set up pointer table for memory access
	// (needed because var4 size changes per game)
	memory_ptr.system_var16_ptr = memory_raw + MEMORY_MES_NAME_SIZE + VAR4_SIZE;
	memory_ptr.var16 = memory_raw + VAR16_OFF;
	memory_ptr.system_var16 = memory_raw + SYSVAR16_OFF;
	memory_ptr.var32 = memory_raw + VAR32_OFF;
	memory_ptr.system_var32 = memory_raw + SYSVAR32_OFF;

	mem_set_sysvar16(mes_sysvar16_flags, 0xf);
	mem_set_sysvar16(mes_sysvar16_text_start_x, 0);
	mem_set_sysvar16(mes_sysvar16_text_start_y, 0);
	mem_set_sysvar16(mes_sysvar16_text_end_x, 640);
	mem_set_sysvar16(mes_sysvar16_text_end_y, 480);
	mem_set_sysvar16(mes_sysvar16_bg_color, 0);
	mem_set_sysvar16(mes_sysvar16_fg_color, 0x7fff);
	mem_set_sysvar16(mes_sysvar16_font_width, 16);
	mem_set_sysvar16(mes_sysvar16_font_height, 16);
	mem_set_sysvar16(mes_sysvar16_font_weight, 1);
	mem_set_sysvar16(mes_sysvar16_char_space, 16);
	mem_set_sysvar16(mes_sysvar16_line_space, 16);
	mem_set_sysvar16(mes_sysvar16_mask_color, 0);

	mem_set_sysvar32(mes_sysvar32_cg_offset, 0x20000);
	doukyuusei_mem_restore();

	// HACK: Map.load_tilemap and Map.load_sprite_scripts are sometimes called
	//       without first loading an mpx/ccd file (this happens when loading
	//       certain scenes from OMOIDE.MES, e.g. Yoshiko's ending scene).
	//       We initialize with fake empty files.
	uint8_t *mpx = memory.file_data + 0x3e000;
	le_put16(mpx, 0, 0);
	le_put16(mpx, 2, 0);

	uint8_t *ccd = memory.file_data + 0x34000;
	le_put16(ccd, 0, 2);
	ccd[2] = 0xff;
}

static void doukyuusei_cursor(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: cursor_show(); break;
	case 1: cursor_hide(); break;
	case 2: sys_cursor_save_pos(params); break;
	case 3: cursor_set_pos(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 4: cursor_load((vm_expr_param(params, 1) + 0) * 2, 2, NULL); break;
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

static void doukyuusei_anim_halt_group(unsigned no)
{
	for (unsigned i = 0; i < 10; i++) {
		anim_halt(no * 10 + i);
	}
}

static void doukyuusei_anim(struct param_list *params)
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
	case 8: anim_exec_copy_call(vm_anim_param(params, 1)); break;
	case 9: doukyuusei_anim_halt_group(vm_expr_param(params, 1)); break;
	case 10: anim_wait(vm_anim_param(params, 1)); break; // TODO: activate skips wait
	case 13: anim_wait(vm_anim_param(params, 1)); break;
	case 11: anim_pause_all_sync(); break;
	case 12: anim_unpause_all(); break;
	default: VM_ERROR("System.Anim.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void doukyuusei_savedata_load_var4(const char *save_name)
{
	savedata_load_var4(save_name, VAR4_SIZE);
	doukyuusei_mem_restore();
}

static void doukyuusei_savedata_load_extra_var32(const char *save_name)
{
	// sysvar32[11] -> sysvar32[210]
	savedata_read(save_name, memory_raw, SYSVAR32_OFF + (11 * 4), 200 * 4);
}

static void doukyuusei_savedata_save_extra_var32(const char *save_name)
{
	// sysvar32[11] -> sysvar32[210]
	savedata_write(save_name, memory_raw, SYSVAR32_OFF + (11 * 4), 200 * 4);
}

static void doukyuusei_load_variables(const char *save_name, const char *vars)
{
	uint8_t save[SYSVAR16_OFF];
	savedata_read(save_name, save, VAR16_OFF, SYSVAR16_OFF - VAR16_OFF);

	for (const char *str = vars; *str; str++) {
		if (*str < 'A' || *str > 'Z') {
			WARNING("Invalid variable name: %c", *str);
			return;
		}
		unsigned varno = *str - 'A';
		mem_set_var16(varno, le_get16(save, VAR16_OFF + varno * 2));
	}
}

static void doukyuusei_savedata_load_special_flags(void)
{
	uint8_t save[MEMORY_MES_NAME_SIZE + VAR4_SIZE];
	savedata_read(_sys_save_name(0), save, MEMORY_MES_NAME_SIZE, VAR4_SIZE);

	for (int i = MEMORY_MES_NAME_SIZE + 2001; i < MEMORY_MES_NAME_SIZE + 3500; i++) {
		if ((save[i] && !memory_raw[i]) || save[i] > 5)
			memory_raw[i] = save[i];
	}
	mem_set_var4(1896, save[MEMORY_MES_NAME_SIZE + 1896]);
	mem_set_var4(1897, save[MEMORY_MES_NAME_SIZE + 1897]);
}

static void doukyuusei_savedata_save_special_flags(void)
{
	uint8_t save[MEMORY_MES_NAME_SIZE + VAR4_SIZE];
	const char *save_name = _sys_save_name(0);
	savedata_read(save_name, save, MEMORY_MES_NAME_SIZE, VAR4_SIZE);

	for (int i = MEMORY_MES_NAME_SIZE + 2001; i < MEMORY_MES_NAME_SIZE + 3500; i++) {
		if (memory_raw[i])
			save[i] = memory_raw[i];
	}

	savedata_write(save_name, save, MEMORY_MES_NAME_SIZE, VAR4_SIZE);
}

static void doukyuusei_savedata(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: savedata_resume_load(sys_save_name(params)); break;
	case 1: savedata_resume_save(sys_save_name(params)); break;
	case 2: doukyuusei_savedata_load_var4(sys_save_name(params)); break;
	case 3: savedata_save_union_var4(sys_save_name(params), VAR4_SIZE); break;
	case 4: doukyuusei_savedata_load_extra_var32(sys_save_name(params)); break;
	case 5: doukyuusei_savedata_save_extra_var32(sys_save_name(params)); break;
	case 6: memset(memory_raw + MEMORY_VAR4_OFFSET, 0, VAR4_SIZE); break;
	case 7: doukyuusei_load_variables(sys_save_name(params), vm_string_param(params, 2)); break;
	case 8: doukyuusei_savedata_load_special_flags(); break;
	case 9: doukyuusei_savedata_save_special_flags(); break;
	case 10: savedata_save_var4(sys_save_name(params), VAR4_SIZE); break;
	default: VM_ERROR("System.SaveData.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void doukyuusei_audio_se_stop(struct param_list *params)
{
	unsigned no = vm_expr_param(params, 1);
	if (no < 3) {
		audio_stop(AUDIO_CH_SE(no));
	} else {
		audio_stop(AUDIO_CH_SE(0));
		audio_stop(AUDIO_CH_SE(1));
		audio_stop(AUDIO_CH_SE(2));
	}
}

static void doukyuusei_audio_se_play_sync(const char *name, unsigned ch)
{
	if (!audio_se_channel_valid(ch)) {
		WARNING("Invalid SE channel: %u", ch);
		return;
	}

	audio_se_play(name, ch);
	while (audio_is_playing(AUDIO_CH_SE(ch))) {
		if (input_down(INPUT_SHIFT)) {
			audio_se_stop(ch);
		}
	}
}

static void doukyuusei_audio(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: audio_bgm_play(vm_string_param(params, 1), true); break;
	case 1: audio_fade(AUDIO_CH_BGM, AUDIO_VOLUME_MIN, 2000, true, false); break;
	case 2: audio_stop(AUDIO_CH_BGM); break;
	case 3: audio_se_play(vm_string_param(params, 1), vm_expr_param(params, 2)); break;
	case 4: doukyuusei_audio_se_stop(params); break;
	case 5: audio_se_fade(AUDIO_VOLUME_MIN, 3000, true, false, vm_expr_param(params, 1)); break;
	case 7: doukyuusei_audio_se_play_sync(vm_string_param(params, 1), vm_expr_param(params, 2)); break;
	default: VM_ERROR("System.Audio.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void doukyuusei_voice_play(struct param_list *params)
{
	const char *name = vm_string_param(params, 1);
	char c = toupper(*name);

	if (c < 'L') {
		asset_set_voice_archive("BSIDE.ARC");
	} else if (c < 'S') {
		asset_set_voice_archive("LSIDE.ARC");
	} else {
		asset_set_voice_archive("SSIDE.ARC");
	}
	audio_voice_play(name, vm_expr_param(params, 2));
}

static void doukyuusei_voice(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: doukyuusei_voice_play(params); break;
	case 1: audio_voice_stop(vm_expr_param(params, 1)); break;
	//case 2: audio_voice_play_sync(vm_string_param(params, 1)); break;
	case 3: mem_set_var32(18, audio_is_playing(AUDIO_CH_VOICE(0))); break;
	default: VM_ERROR("System.Voice.function[%u] not implemented",
				 params->params[0].val);
	}
}

static bool skip_on_shift(void)
{
	return !input_down(INPUT_SHIFT);
}

static void doukyuusei_display(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0:
		if (params->nr_params > 1) {
			gfx_display_hide(vm_expr_param(params, 1));
		} else {
			gfx_display_unhide();
		}
		break;
	case 1:
		if (params->nr_params > 1) {
			_gfx_display_fade_out(vm_expr_param(params, 1), 1000, skip_on_shift);
		} else {
			_gfx_display_fade_in(1000, skip_on_shift);
		}
		break;
	default: VM_ERROR("System.Display.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void doukyuusei_graphics_darken(struct param_list *params)
{
	int x = vm_expr_param(params, 1);
	int y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - x) + 1;
	int h = (vm_expr_param(params, 4) - y) + 1;
	unsigned dst_i = mem_get_sysvar16(mes_sysvar16_dst_surface);
	gfx_blend_fill(x, y, w, h, dst_i, 0, 127);
}

static void doukyuusei_graphics_blend_to(struct param_list *params)
{
	// System.Graphics.blend_to(a_x, a_y, a_br_x, a_br_y, a_i, b_x, b_y, b_i, dst_x, dst_y, dst_i)
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
	unsigned rate = vm_expr_param(params, 12);

	// XXX: This function is always called with (b_x,b_y)=(dst_x,dst_y)=(0,0), and
	//      b_i=dst_i. We just blend surface 'a' onto 'dst'; no need to implement the
	//      full semantics.
	if (b_x || b_y || dst_x || dst_y || w != 640 || h != 480 || b_i != dst_i) {
		WARNING("Unexpected parameters to System.Graphics.blend_to");
		return;
	}

	// rate is a number between 0 - 0x40000, and neither end of that range is fully
	// transparent.
	rate = clamp(1, 254, rate / 2048);
	gfx_blend(a_x, a_y, 640, 480, a_i, 0, 0, dst_i, rate);
}

static void doukyuusei_graphics(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: sys_graphics_copy(params); break;
	case 1: sys_graphics_copy_masked(params); break;
	case 2: sys_graphics_fill_bg(params); break;
	case 3: sys_graphics_copy_swap(params); break;
	case 4: sys_graphics_swap_bg_fg(params); break;
	case 5: sys_graphics_pixel_crossfade(params); break;
	case 6: sys_graphics_compose(params); break;
	case 9: sys_graphics_pixel_crossfade_masked(params); break;
	case 11: doukyuusei_graphics_darken(params); break;
	case 14: doukyuusei_graphics_blend_to(params); break;
	default: VM_ERROR("System.Graphics.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void doukyuusei_wait(struct param_list *params)
{
	// XXX: crossfade to/from title has wait of 1, probably because fade_to
	//      operation is slow. We increase it.
	if (params->nr_params == 1 && vm_expr_param(params, 0) == 1) {
		params->params[0].val = 20;
	}
	sys_wait(params);
}

static void doukyuusei_map(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: map_load_tilemap(); break;
	case 1: map_spawn_sprite(vm_expr_param(params, 1), vm_expr_param(params, 2),
				vm_expr_param(params, 3)); break;
	case 2: map_load_tiles(); break;
	case 4: map_load_sprite_scripts(); break;
	case 5: map_set_sprite_script(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 6: map_place_sprites(); break;
	case 7: map_set_sprite_state(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 8: map_exec_sprites_and_redraw(); break;
	case 9: map_exec_sprites(); break;
	case 10:
	case 11: map_draw_tiles(); break;
	case 12: map_set_location_mode(vm_expr_param(params, 1)); break;
	case 13: map_get_location(); break;
	case 14: map_move_sprite(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 15: map_path_sprite(vm_expr_param(params, 1), vm_expr_param(params, 2),
				 vm_expr_param(params, 3)); break;
	case 16: map_stop_pathing(); break;
	case 17: map_get_pathing(); break;
	case 20: map_rewind_sprite_pos(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 22: map_set_sprite_anim(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 24: map_load_palette(vm_string_param(params, 1), vm_expr_param(params, 2)); break;
	case 25: map_load_bitmap(vm_string_param(params, 1), vm_expr_param(params, 2),
				 vm_expr_param(params, 3), vm_expr_param(params, 4)); break;
	default: VM_ERROR("System.Map.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void doukyuusei_backlog(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: backlog_clear(); break;
	case 1: backlog_prepare(); break;
	case 2: backlog_commit(); break;
	case 3: mem_set_var32(18, backlog_count()); break;
	case 4: mem_set_var32(18, backlog_get_pointer(vm_expr_param(params, 1))); break;
	default: VM_ERROR("System.Backlog.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void decompose_draw_call(struct anim_draw_call *call,
		int *dst_x, int *dst_y, int *w, int *h)
{
	switch (call->op) {
	case ANIM_DRAW_OP_COPY:
	case ANIM_DRAW_OP_COPY_MASKED:
	case ANIM_DRAW_OP_SWAP:
		*dst_x = call->copy.dst.x;
		*dst_y = call->copy.dst.y;
		*w = call->copy.dim.w;
		*h = call->copy.dim.h;
		break;
	case ANIM_DRAW_OP_COMPOSE:
		*dst_x = call->compose.dst.x;
		*dst_y = call->compose.dst.y;
		*w = call->compose.dim.w;
		*h = call->compose.dim.h;
		break;
	default:
		VM_ERROR("Unexpected animation draw operation: %u", (unsigned)call->op);
	}
}

/*
 * Redraw the message box when it's clobbered by an animation.
 */
static void doukyuusei_after_anim_draw(struct anim_draw_call *call)
{
	if (!mem_get_var4(4046))
		return;

	int src_top_y = 106;
	int dst_top_y = 360;
	int max_h = 106;
	if (mem_get_var4(4084)) {
		src_top_y += 32;
		dst_top_y += 32;
		max_h -= 32;
	}

	int dst_x, dst_y, w, h;
	decompose_draw_call(call, &dst_x, &dst_y, &w, &h);
	if (dst_y + h <= dst_top_y)
		return;

	if (dst_y < dst_top_y) {
		h -= dst_top_y - dst_y;
		dst_y = dst_top_y;
	}
	h = min(h, max_h);

	// draw call clobbered message box: redraw it
	int src_x = dst_x;
	int src_y = src_top_y + (dst_y - dst_top_y);
	// darken area under message box
	gfx_blend_fill(dst_x, dst_y, w, h, 0, 0, 127);
	// compose message box on top
	gfx_copy_masked(src_x, src_y, w, h, 7, dst_x, dst_y, 0, mem_get_sysvar16(mes_sysvar16_mask_color));
}

static void doukyuusei_strlen(struct param_list *params)
{
	vm_flag_on(FLAG_STRLEN);
	mem_set_var32(11, 0);
	sys_farcall(params);
	vm_flag_off(FLAG_STRLEN);
}

static void doukyuusei_sys_25(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0:
		game->after_anim_draw = doukyuusei_after_anim_draw;
		break;
	case 1:
	case 3:
		game->after_anim_draw = NULL;
		break;
	default:
		WARNING("System.function[25].function[%u] not implemented",
				params->params[0].val);
	}
}

static void doukyuusei_sys_26(struct param_list *params)
{
	VM_ERROR("System.function[26] not implemented");
}

/*
 * Copy a set of var4s to a particular location in the var4 array.
 */
static void util_copy_var4(struct param_list *params)
{
	uint16_t dst = mem_get_var16(2);
	unsigned count = vm_expr_param(params, 1);
	if (dst + count >= VAR4_SIZE) {
		VM_ERROR("Tried to write past end of var4 array: %u+%u", dst, count);
	}
	for (unsigned i = 0; i < count; i++) {
		uint16_t src = 0;
		if (i + 2 >= params->nr_params) {
			WARNING("Tried to read past the end of parameter list (%u/%u)",
					count + 2, params->nr_params);
		} else {
			src = vm_expr_param(params, i + 2);
		}
		if (src >= VAR4_SIZE) {
			VM_ERROR("Tried to read past end of var4 array: %u", src);
		}
		mem_set_var4(dst + i, mem_get_var4(src));
	}
}

static void util_get_ctrl(struct param_list *params)
{
	mem_set_var16(18, input_down(INPUT_CTRL));
}

static void util_resume_load_with_special_flags(struct param_list *params)
{
	savedata_resume_load(sys_save_name(params));
	doukyuusei_savedata_load_special_flags();
}

// Animate date bar sliding up from bottom of screen to top of message box.
static void util_datebar_slide_up(struct param_list *params)
{
	const unsigned s7 = 7;
	const unsigned s0 = 0;

	// dimensions of bar
	const unsigned w = 640;
	const unsigned h = 32;
	// location of bar (surface 7)
	const unsigned bar_y = 106;
	// location of scratch area (surface 7)
	const unsigned scratch_y = 1216;
	// location of hidden area (surface 7)
	const unsigned hide_y = 1248;
	// the new bar location, updated every iteration (surface 0)
	unsigned dst_y = 440;

	vm_timer_t timer = vm_timer_create();
	for (int i = 0; i < 11; i++) {
		// restore bottom 8 lines under bar
		gfx_copy(0, (hide_y + h) - 8, w, 8, s7, 0, dst_y + h, s0);
		// update hidden area
		gfx_copy(0, hide_y, w, h - 8, s7, 0, scratch_y, s7);
		gfx_copy(0, dst_y, w, 8, s0, 0, hide_y, s7);
		gfx_copy(0, scratch_y, w, h - 8, s7, 0, hide_y+8, s7);
		// draw bar at new location (8 lines up)
		gfx_copy(0, bar_y, w, h, s7, 0, dst_y, s0);
		// update
		gfx_dirty(s0, 0, dst_y, 640, 480 - dst_y);
		vm_peek();
		vm_timer_tick(&timer, 16);
		dst_y -= 8;
	}
}

// Animate date bar sliding down from top of message box to bottom of screen.
static void util_datebar_slide_down(struct param_list *params)
{
	const unsigned s7 = 7;
	const unsigned s0 = 0;

	// dimensions of bar
	const unsigned w = 640;
	const unsigned h = 32;
	// location of bar (surface 7)
	const unsigned bar_y = 1216;
	// location of hidden area (surface 7)
	const unsigned hide_y = 1248;
	// the new bar location, updated every iteration (surface 0)
	unsigned dst_y = 368;

	// make working copy of bar (why?)
	gfx_copy(0, 106, w, h, s7, 0, bar_y, s7);

	vm_timer_t timer = vm_timer_create();
	for (int i = 0; i < 11; i++) {
		// restore top 8 lines under bar
		gfx_copy(0, hide_y, w, 8, s7, 0, dst_y - 8, s0);
		// update hidden area
		gfx_copy(0, hide_y + 8, w, h - 8, s7, 0, hide_y, s7);
		gfx_copy(0, (dst_y + h) - 8, w, 8, s0, 0, (hide_y + h) - 8, s7);
		// draw bar at new location (8 lines down)
		gfx_copy(0, bar_y, w, h, s7, 0, dst_y, s0);
		// update
		gfx_dirty(s0, 0, dst_y - 8, 640, 480 - (dst_y - 8));
		vm_peek();
		vm_timer_tick(&timer, 16);
		dst_y += 8;
	}
}

// Animate cursor description box sliding up.
static void util_cursor_description_slide_up(struct param_list *params)
{
	const unsigned s7 = 7;
	const unsigned s0 = 0;
	const uint16_t mask_color = mem_get_sysvar16(mes_sysvar16_mask_color);

	// dimensions of box
	const unsigned w = 144;
	const unsigned h = 34;
	// location of area hidden behind box (surface 7)
	const unsigned hide_y = 1178;
	// location of box (surface 7)
	const unsigned box_y = 1212;
	// location of scratch area (surface 7)
	const unsigned scratch_y = 1246;
	// the new box location, updated every iteration (surface 0)
	const unsigned dst_x = 248;
	unsigned dst_y = 430;

	vm_timer_t timer = vm_timer_create();
	for (int i = 0; i < 5; i++) {
		// restore row below dst
		gfx_copy(0, (hide_y + h) - 8, w, 8, s7, dst_x, dst_y + h, s0);
		// create new hidden area in scratch
		gfx_copy(dst_x, dst_y, w, 8, s0, 0, scratch_y, s7);
		gfx_copy(0, hide_y, w, h - 8, s7, 0, scratch_y + 8, s7);
		// copy scratch to hidden area
		gfx_copy(0, scratch_y, w, h, s7, 0, hide_y, s7);
		// compose box with scratch
		gfx_copy_masked(0, box_y, w, h, s7, 0, scratch_y, s7, mask_color);
		// copy from scratch to destination
		gfx_copy(0, scratch_y, w, h, s7, dst_x, dst_y, s0);
		// update
		gfx_dirty(s0, dst_x, dst_y, w, h + 8);
		vm_peek();
		vm_timer_tick(&timer, 16);
		dst_y -= 8;
	}
}

// Animate cursor description box sliding down.
static void util_cursor_description_slide_down(struct param_list *params)
{
	const unsigned s7 = 7;
	const unsigned s0 = 0;
	const uint16_t mask_color = mem_get_sysvar16(mes_sysvar16_mask_color);

	// dimensions of box
	const unsigned w = 144;
	const unsigned h = 34;
	// location of area hidden behind box (surface 7)
	const unsigned hide_y = 1178;
	// location of box (surface 7)
	const unsigned box_y = 1212;
	// location of scratch area (surface 7)
	const unsigned scratch_y = 1246;
	// the new box location, updated every iteration (surface 0)
	const unsigned dst_x = 248;
	unsigned dst_y = 406;

	vm_timer_t timer = vm_timer_create();
	for (int i = 0; i < 5; i++) {
		// restore row above dst
		gfx_copy(0, hide_y, w, 8, s7, dst_x, dst_y - 8, s0);
		// create new hidden area in scratch
		gfx_copy(0, hide_y + 8, w, h - 8, s7, 0, scratch_y, s7);
		gfx_copy(dst_x, (dst_y + h) - 8, w, 8, s0, 0, (scratch_y + h) - 8, s7);
		// copy scratch to hidden area
		gfx_copy(0, scratch_y, w, h, s7, 0, hide_y, s7);
		// compose box with scratch
		gfx_copy_masked(0, box_y, w, h, s7, 0, scratch_y, s7, mask_color);
		// copy from scratch to destination
		gfx_copy(0, scratch_y, w, h, s7, dst_x, dst_y, s0);
		// update
		gfx_dirty(s0, dst_x, dst_y - 8, w, h + 8);
		vm_peek();
		vm_timer_tick(&timer, 16);
		dst_y += 8;
	}
}

static void util_scroll(struct param_list *params)
{
	// XXX: always called with the same parameters
	if (vm_expr_param(params, 1) != 0 || vm_expr_param(params, 2) != 480
			|| vm_expr_param(params, 3) != 640
			|| vm_expr_param(params, 4) != 960
			|| vm_expr_param(params, 5) != 5) {
		WARNING("Unexpected parameters to Util.scroll");
	}

	vm_timer_t timer = vm_timer_create();
	SDL_Surface *src = gfx_get_surface(9);
	SDL_Surface *dst = gfx_get_surface(0);
	SDL_Rect dst_r = { 0, 0, 640, 480 };
	for (int y = 480; y > 0; y -= 4) {
		SDL_Rect src_r = { 0, y, 640, 480 };
		SDL_CALL(SDL_BlitSurface, src, &src_r, dst, &dst_r);

		gfx_whole_surface_dirty(0);
		vm_peek();
		vm_timer_tick(&timer, 16);
	}
	SDL_Rect src_r = { 0, 0, 640, 480 };
	SDL_CALL(SDL_BlitSurface, src, &src_r, dst, &dst_r);
	gfx_whole_surface_dirty(0);
	vm_peek();
	vm_timer_tick(&timer, 16);
}

/*
 * Takes a variable number of string parameters and compares them against a
 * string on the heap. If any of the strings match, returns true. (Used to
 * prevent the player from entering certain names.)
 */
static void util_multi_strcmp(struct param_list *params)
{
	char *str = mem_get_cstring(HEAP_OFF);
	for (unsigned i = 1; i < params->nr_params; i++) {
		if (!strcmp(str, vm_string_param(params, i))) {
			mem_set_var32(18, 1);
			return;
		}
	}
	mem_set_var32(18, 0);
}

/*
 * Save the player's name to FLAG00.
 */
static void util_save_name(struct param_list *params)
{
	savedata_write(_sys_save_name(0), memory_raw, HEAP_OFF, 20);
}

/*
 * Load the player's name from FLAG00.
 */
static void util_load_name(struct param_list *params)
{
	savedata_read(_sys_save_name(0), memory_raw, HEAP_OFF, 20);
}

static void util_IME_enable(struct param_list *params)
{
	// TODO
	WARNING("Util.IME_enable not implemented");
}

static void util_IME_disable(struct param_list *params)
{
	// TODO
	WARNING("Util.IME_disable not implemented");
}

static void util_IME_get_composition_started(struct param_list *params)
{
	// TODO
	mem_set_var32(18, 0);
}

static void util_IME_get_open(struct param_list *params)
{
	// TODO
	mem_set_var32(18, 0);
}

static void util_IME_set_open(struct param_list *params)
{
	// TODO
	//WARNING("Util.IME_set_open not implemented");
}

static void util_get_backspace(struct param_list *params)
{
	mem_set_var32(18, input_down(INPUT_BACKSPACE));
}

static void util_get_backspace2(struct param_list *params)
{
	uint32_t prev = mem_get_var32(4);
	bool bs_down = input_down(INPUT_BACKSPACE);
	if (!prev && bs_down) {
		mem_set_var32(20, 1);
		mem_set_var32(4, 0xffffffff);
	} else if (prev && !bs_down) {
		mem_set_var32(20, 0);
		mem_set_var32(4, 0);
	} else {
		mem_set_var32(20, 0);
	}
}

/*
 * Save a particular set of var4s to FLAG00.
 */
static void util_save_var4(struct param_list *params)
{
	const char *save_name = _sys_save_name(0);
	uint8_t save[MEMORY_MES_NAME_SIZE + VAR4_SIZE];
	savedata_read(save_name, save, MEMORY_MES_NAME_SIZE, VAR4_SIZE);

	for (int i = MEMORY_MES_NAME_SIZE; i < MEMORY_MES_NAME_SIZE + 1900; i++) {
		save[i] |= memory_raw[i];
	}
	uint8_t flag;
	if ((flag = mem_get_var4(1834)))
		save[MEMORY_MES_NAME_SIZE + 1834] = flag;
	if ((flag = mem_get_var4(1721)))
		save[MEMORY_MES_NAME_SIZE + 1721] = flag;
	if (mem_get_var4(1859))
		save[MEMORY_MES_NAME_SIZE + 1859] = flag;
	if ((flag = mem_get_var4(1789)))
		save[MEMORY_MES_NAME_SIZE + 1789] = flag;
	if ((flag = mem_get_var4(1860)))
		save[MEMORY_MES_NAME_SIZE + 1860] = flag;
	if ((flag = mem_get_var4(1863)))
		save[MEMORY_MES_NAME_SIZE + 1863] = flag;

	savedata_write(save_name, save, MEMORY_MES_NAME_SIZE, VAR4_SIZE);
}

/*
 * Animation during Misa's ending where the train pulls into the station.
 */
static void util_misa_train_in(struct param_list *params)
{
	_sys_load_image("Y04BTR.G16", 11, 1);

	uint16_t mask_color = mem_get_sysvar16(mes_sysvar16_mask_color);
	vm_timer_t timer = vm_timer_create();

	for (int dst_x = 640 - 8; dst_x >= 0; dst_x -= 8) {
		gfx_copy(0, 0, 640, 480, 2, 0, 0, 0);
		gfx_copy_masked(0, 0, 640 - dst_x, 480, 11, dst_x, 0, 0, mask_color);
		vm_peek();
		vm_timer_tick(&timer, 30);
	}
	for (int src_x = 8; src_x <= 2080; src_x += 8) {
		gfx_copy(0, 0, 640, 480, 2, 0, 0, 0);
		gfx_copy_masked(src_x, 0, 640, 480, 11, 0, 0, 0, mask_color);
		vm_peek();
		vm_timer_tick(&timer, 30);
	}
}

/*
 * Animation during Misa's ending where the train pulls out of the station.
 */
static void util_misa_train_out(struct param_list *params)
{
	uint16_t mask_color = mem_get_sysvar16(mes_sysvar16_mask_color);
	vm_timer_t timer = vm_timer_create();

	for (int src_x = 2088; src_x < 2672; src_x += 8) {
		gfx_copy(0, 0, 640, 480, 2, 0, 0, 0);
		gfx_copy_masked(src_x, 0, 640, 480, 11, 0, 0, 0, mask_color);
		vm_peek();
		vm_timer_tick(&timer, 30);
	}
	gfx_copy(0, 0, 640, 480, 2, 0, 0, 0);
}

#ifdef HAVE_FFMPEG

static struct {
	struct archive *arc;
	struct movie_context *ctx;
	bool is_ending;
	struct archive_data *video;
	struct archive_data *audio;
	SDL_Texture *credits;
	SDL_Texture *chara;
} movie;

static void movie_end(void)
{
	if (movie.video)
		archive_data_release(movie.video);
	if (movie.audio)
		archive_data_release(movie.audio);
	if (movie.ctx)
		movie_free(movie.ctx);
	if (movie.credits)
		SDL_DestroyTexture(movie.credits);
	if (movie.chara)
		SDL_DestroyTexture(movie.chara);
	movie.video = NULL;
	movie.audio = NULL;
	movie.ctx = NULL;
	movie.is_ending = false;
	movie.credits = NULL;
	movie.chara = NULL;
}

const char *chara_file_name(unsigned i)
{
	switch (i) {
	case 0:  return "mai.g16";
	case 1:  return "misa.g16";
	case 2:  return "miho.g16";
	case 3:  return "satomi.g16";
	case 4:  return "kurumi.g16";
	case 5:  return "chiharu.g16";
	case 6:  return "yoshiko.g16";
	case 7:  return "mako.g16";
	case 8:  return "ako.g16";
	case 9:  return "hiromi.g16";
	case 10: return "reiko.g16";
	case 11: return "kaori.g16";
	case 12: return "yayoi.g16";
	case 13: return "natuko.g16";
	default: return NULL;
	}
}

static SDL_Texture *load_movie_texture(const char *name)
{
	// load file from movie archive
	struct archive_data *file = archive_get(movie.arc, name);
	if (!file) {
		WARNING("Failed to open %s", name);
		return NULL;
	}

	// decode CG
	struct cg *cg = cg_load_arcdata(file);
	archive_data_release(file);
	if (!cg) {
		WARNING("Failed to decode CG \"%s\"", name);
		return NULL;
	}

	// convert color key to alpha
	uint8_t *end = cg->pixels + cg->metrics.w * cg->metrics.h * 4;
	for (uint8_t *p = cg->pixels; p < end; p += 4) {
		if (p[0] == 0 && p[1] == 248 && p[2] == 0) {
			//memset(p, 0, 4);
			p[3] = 0;
		}
	}

	// create RGBA texture
	SDL_Texture *t;
	SDL_CTOR(SDL_CreateTexture, t, gfx.renderer, SDL_PIXELFORMAT_RGBA32,
			SDL_TEXTUREACCESS_STATIC, cg->metrics.w, cg->metrics.h);
	SDL_CALL(SDL_SetTextureBlendMode, t, SDL_BLENDMODE_BLEND);
	SDL_CALL(SDL_UpdateTexture, t, NULL, cg->pixels, cg->metrics.w * 4);

	cg_free(cg);
	return t;
}

static void util_movie_load(struct param_list *params)
{
	if (movie.arc == NULL) {
		// open "STREAM.DAT"
		char *arc_path = path_get_icase("STREAM.DAT");
		if (!arc_path || !(movie.arc = archive_open(arc_path, 0))) {
			WARNING("Failed to open archive: STREAM.DAT");
			goto error;
		}
	}

	if (!(movie.video = archive_get(movie.arc, vm_string_param(params, 4)))) {
		WARNING("Failed to open video file: %s", vm_string_param(params, 4));
		goto error;
	}
	if (params->nr_params > 8) {
		asset_set_voice_archive("SSIDE.ARC");
		if (!(movie.audio = asset_voice_load(vm_string_param(params, 8)))) {
			WARNING("Failed to open audio file: %s", vm_string_param(params, 8));
		}
	}

	if (!(movie.ctx = movie_load_arc(movie.video, movie.audio, 640, 480))) {
		WARNING("Failed to load movie");
		goto error;
	}

	if (!strcasecmp(vm_string_param(params, 4), "end.avi")) {
		// load overlay textures
		const char *chara_name = chara_file_name(vm_expr_param(params, 1));
		if (!chara_name || !(movie.chara = load_movie_texture(chara_name)))
			goto error;
		if (!(movie.credits = load_movie_texture("staff.g16")))
			goto error;
		movie.is_ending = true;
	}

	return;
error:
	movie_end();
}

static bool movie_cancelled(void)
{
	vm_peek();
	if (mem_get_var4(4047) && input_down(INPUT_CANCEL)) {
		mem_set_var32(18, 1);
#ifdef USE_SDL_MIXER
		audio_stop(AUDIO_CH_SE0);
#endif
		return true;
	}
	return false;
}

struct movie_seek {
	int t;
	unsigned frame;
};

struct movie_credits_frame {
	int t;
	unsigned src_y;
	unsigned dst_y;
	unsigned h;
};

#define MS(minutes, seconds, ms) (((minutes * 60) + seconds) * 1000 + ms)

static void play_ending(void)
{
	static const struct movie_seek seek[] = {
		{ MS(0, 24,   0), 185 },
		{ MS(0, 29, 400), 185 },
		{ MS(0, 34, 900),  95 },
		{ MS(0, 49, 300), 185 },
		{ MS(0, 54, 700), 185 },
		{ MS(1,  0, 100), 185 },
		{ MS(1,  5, 500),  95 },
		{ MS(1, 14, 700),  95 },
		{ MS(1, 29, 100), 185 },
		{ MS(1, 34, 500), 185 },
		{ MS(1, 40,   0),  95 },
		{ MS(1, 54, 400), 185 },
		{ MS(1, 59, 800), 185 },
		{ MS(2,  5, 200), 185 },
		{ MS(2, 10, 600),  95 },
		{ MS(2, 25, 100),  95 },
		// transition to evening
		{ MS(2, 46, 500), 400 },
		{ MS(2, 51, 300), 310 },
		{ MS(3,  5,   0), 400 },
		{ MS(3,  9, 800), 310 },
		{ MS(3, 23, 500), 400 },
		{ MS(3, 28, 300), 400 },
		{ MS(3, 33, 100), 310 },
		{ MS(3, 46, 800), 400 },
		{ MS(3, 51, 600), 310 },
		{ MS(4,  5, 300), 400 },
		{ MS(4, 10, 100), 310 },
		{ MS(4, 23, 800), 310 }
	};
	int seek_i = 0;

	static const struct movie_credits_frame credits[] = {
		//  frame time    src_y  dst_y   h
		{ MS(0,  0,   0),    0,     0,   0 },
		{ MS(0, 10,   0),    0,   128,  53 },
		{ MS(0, 20,   0),    0,     0,   0 },
		{ MS(0, 21, 500),   57,   113,  89 },
		{ MS(0, 31, 500),    0,     0,   0 },
		{ MS(0, 33,   0),  158,   134,  47 },
		{ MS(0, 43,   0),    0,     0,   0 },
		{ MS(0, 44, 500),  214,   134,  47 },
		{ MS(0, 54, 500),    0,     0,   0 },
		{ MS(0, 56,   0),  264,    88, 139 },
		{ MS(1,  6,   0),    0,     0,   0 },
		{ MS(1,  7, 500),  413,   101, 114 },
		{ MS(1, 17, 500),    0,     0,   0 },
		{ MS(1, 19,   0),  529,   121,  72 },
		{ MS(1, 29,   0),    0,     0,   0 },
		{ MS(1, 30, 500),  611,   122,  69 },
		{ MS(1, 40, 500),    0,     0,   0 },
		{ MS(1, 42,   0),  684,   100, 114 },
		{ MS(1, 52,   0),    0,     0,   0 },
		{ MS(1, 53, 500),  804,   100, 114 },
		{ MS(2,  3, 500),    0,     0,   0 },
		{ MS(2,  5,   0),  924,   100, 114 },
		{ MS(2, 15,   0),    0,     0,   0 },
		{ MS(2, 16, 500), 1044,   100, 114 },
		{ MS(2, 26, 500),    0,     0,   0 },
		{ MS(2, 28,   0), 1162,   122,  70 },
		{ MS(2, 38,   0),    0,     0,   0 },
		{ MS(2, 45,   0), 1236,   100, 114 },
		{ MS(2, 55,   0),    0,     0,   0 },
		{ MS(2, 56, 500), 1356,   100, 114 },
		{ MS(3,  6, 500),    0,     0,   0 },
		{ MS(3,  8,   0), 1479,   111,  93 },
		{ MS(3, 18,   0),    0,     0,   0 },
		{ MS(3, 19, 500), 1580,    99, 115 },
		{ MS(3, 29, 500),    0,     0,   0 },
		{ MS(3, 31,   0), 1700,   100, 114 },
		{ MS(3, 41,   0),    0,     0,   0 },
		{ MS(3, 42, 500), 1820,   100, 114 },
		{ MS(3, 52, 500),    0,     0,   0 },
		{ MS(3, 54,   0), 1940,   100, 114 },
		{ MS(4,  4,   0),    0,     0,   0 },
		{ MS(4,  5, 500), 2060,   100, 114 },
		{ MS(4, 15, 500),    0,     0,   0 },
	};
	SDL_Rect credits_src = { 0,   0, 224, 0 };
	SDL_Rect credits_dst = { 208, 0, 224, 0 };
	int credits_i = 0;

	static const int chara[] = {
		MS(0,  0,   0),
		MS(2, 42,   0),
		MS(2, 42, 100),
		MS(2, 42, 200),
		MS(2, 42, 300),
		MS(2, 42, 400),
		MS(2, 42, 500),
		MS(2, 42, 600),
		MS(2, 42, 700),
		MS(2, 42, 800),
		MS(2, 42, 900),
		MS(2, 43,   0),
		MS(2, 43, 100),
		MS(2, 43, 200),
		MS(2, 43, 300),
		MS(2, 43, 400),
		MS(2, 43, 500),
		MS(2, 43, 600),
		MS(2, 43, 700),
		MS(2, 43, 800),
		MS(2, 43, 900),
		MS(4, 17, 100),
		MS(4, 17, 400),
		MS(4, 17, 600),
		MS(4, 17, 800),
		MS(4, 18,   0),
		MS(4, 18, 200),
		MS(4, 18, 400),
		MS(4, 18, 600),
		MS(4, 18, 800),
		MS(4, 19, 300),
		MS(4, 19, 500),
		MS(4, 37, 600),
		MS(4, 37, 700),
		MS(4, 37, 800),
		MS(4, 37, 900),
		MS(4, 38,   0),
		MS(4, 38, 100),
		MS(4, 38, 200),
	};
	SDL_Rect chara_src = { 0, 0, 205, 200 };
	SDL_Rect chara_dst = { 160, 265, 205, 200 };
	int chara_i = 0;

	while (!movie_is_end(movie.ctx)) {
		int pos = movie_get_position(movie.ctx);
		if (seek_i < ARRAY_SIZE(seek) && pos - seek[seek_i].t > -2) {
			movie_seek_video(movie.ctx, seek[seek_i].frame);
			seek_i++;
		}
		if (credits_i < ARRAY_SIZE(credits) && pos - credits[credits_i].t >= 0) {

		}
		int r = movie_draw(movie.ctx);
		if (r < 0)
			break;
		if (r > 0) {
			// draw credits/characters on top of video
			if (credits_i + 1 < ARRAY_SIZE(credits)) {
				if (pos - credits[credits_i + 1].t >= 0) {
					credits_i++;
					credits_src.y = credits[credits_i].src_y;
					credits_dst.y = credits[credits_i].dst_y;
					credits_src.h = credits[credits_i].h;
					credits_dst.h = credits[credits_i].h;
				}
			}
			if (chara_i + 1 < ARRAY_SIZE(chara)) {
				if (pos - chara[chara_i + 1] >= 0) {
					chara_i++;
					chara_src.y += 200;
				}
			}
			if (credits_src.h) {
				SDL_CALL(SDL_RenderCopy, gfx.renderer, movie.credits,
						&credits_src, &credits_dst);
			}
			if (pos < MS(4, 38, 300)) {
				SDL_CALL(SDL_RenderCopy, gfx.renderer, movie.chara,
						&chara_src, &chara_dst);
			}
			SDL_RenderPresent(gfx.renderer);
		}
		if (movie_cancelled())
			break;
	}
}

static void util_movie_play(struct param_list *params)
{
	if (!movie.ctx) {
		WARNING("No movie loaded");
		return;
	}

#ifdef USE_SDL_MIXER
	// XXX: for SDL_Mixer, we don't sync video to audio
	if (movie.audio)
		audio_play(AUDIO_CH_SE0, movie.audio, false);
#endif

	movie_set_volume(movie.ctx, 18);
	movie_play(movie.ctx);

	gfx_display_freeze();
	if (movie.is_ending) {
		play_ending();
	} else {
		while (!movie_is_end(movie.ctx)) {
			int r = movie_draw(movie.ctx);
			if (r < 0)
				break;
			if (r > 0)
				SDL_RenderPresent(gfx.renderer);
			if (movie_cancelled())
				break;
		}
	}

	// copy last frame to surface 0 (converting RGBA -> RGB)
	unsigned stride;
	uint8_t *pixels = movie_get_pixels(movie.ctx, &stride);
	if (pixels) {
		SDL_Surface *s0 = gfx_get_surface(0);
		for (int row = 0; row < 480; row++) {
			uint8_t *src = pixels + row * stride;
			uint8_t *dst = s0->pixels + row * s0->pitch;
			uint8_t *end = dst + 640 * 3;
			for (; dst < end; dst += 3, src += 4) {
				memcpy(dst, src, 3);
			}
		}
	} else {
		WARNING("Failed to copy final video frame");
	}

	gfx_display_unfreeze();
	gfx_whole_surface_dirty(0);

#ifdef USE_SDL_MIXER
	if (movie.audio) {
		while (audio_is_playing(AUDIO_CH_SE0))
			vm_delay(16);
	}
#endif

	movie_end();
}

#else
static void util_movie_load(struct param_list *params)
{
	WARNING("movie not supported (built without ffmpeg)");
}

static void util_movie_play(struct param_list *params)
{
	WARNING("movie not supported (built without ffmpeg)");
}
#endif

static void util_500(struct param_list *params)
{
	// TODO
	//WARNING("Util.function[500] not implemented");
}

static void util_501(struct param_list *params)
{
	// TODO
	//WARNING("Util.function[501] not implemented");
	mem_set_var32(25, 0);
}

static void util_end_wait(struct param_list *params)
{
	uint32_t ticks = vm_expr_param(params, 1);
	vm_timer_t start = vm_timer_create();
	vm_timer_t timer = start;

	bool allow_skip = mem_get_var4(4047);
	while (timer - start < ticks) {
		if (allow_skip && input_down(INPUT_CANCEL))
			break;
		vm_timer_tick(&timer, 16);
	}
}

static void doukyuusei_init(void)
{
	audio_set_volume(AUDIO_CH_BGM, -1500);
	audio_set_volume(AUDIO_CH_SE0, -1500);
	audio_set_volume(AUDIO_CH_VOICE0, -500);
}

static void doukyuusei_draw_text(const char *text)
{
	if (vm_flag_is_on(FLAG_STRLEN)) {
		mem_set_var32(11, mem_get_var32(11) + strlen(text));
	} else {
		vm_draw_text(text);
	}
}

struct game game_doukyuusei = {
	.id = GAME_DOUKYUUSEI,
	.surface_sizes = {
		{  640,  480 },
		{  640,  480 },
		{  640,  480 },
		{  640,  480 },
		{  640,  480 },
		{  992,  832 },
		{  640,  480 },
		{  640, 1280 },
		{  640,  480 },
		{ 1280, 1280 },
		{  264,  532 },
		{ 2672,  480 }, // XXX: for Misa train utils -- doesn't exist in AI5WIN.EXE
		{    0,    0 }
	},
	.bpp = 16,
	.mem16_size = MEM16_SIZE,
	.mem_init = doukyuusei_mem_init,
	.mem_restore = doukyuusei_mem_restore,
	.init = doukyuusei_init,
	.draw_text_zen = doukyuusei_draw_text,
	.draw_text_han = doukyuusei_draw_text,
	.expr_op = {
		DEFAULT_EXPR_OP,
		[0xe5] = vm_expr_rand_with_imm_range,
	},
	.stmt_op = {
		DEFAULT_STMT_OP,
		[0x03] = vm_stmt_set_cflag_4bit_saturate,
		[0x05] = vm_stmt_set_eflag_4bit_saturate,
	},
	.sys = {
		[0]  = sys_set_font_size,
		[1]  = sys_display_number,
		[2]  = doukyuusei_cursor,
		[3]  = doukyuusei_anim,
		[4]  = doukyuusei_savedata,
		[5]  = doukyuusei_audio,
		[6]  = doukyuusei_voice,
		[7]  = sys_load_file,
		[8]  = sys_load_image,
		[9]  = doukyuusei_display,
		[10] = doukyuusei_graphics,
		[11] = doukyuusei_wait,
		[12] = sys_set_text_colors_direct,
		[13] = sys_farcall,
		[14] = sys_get_cursor_segment,
		[15] = sys_menu_get_no,
		[16] = sys_get_time,
		[17] = doukyuusei_map,
		[18] = sys_check_input,
		[19] = doukyuusei_backlog,
		[24] = doukyuusei_strlen,
		[25] = doukyuusei_sys_25,
		[26] = doukyuusei_sys_26,
		[255] = util_noop,
	},
	.util = {
		[5] = util_copy_var4,
		[6] = util_resume_load_with_special_flags,
		[7] = util_get_ctrl,
		[11] = util_datebar_slide_up,
		[12] = util_datebar_slide_down,
		[13] = util_cursor_description_slide_up,
		[14] = util_cursor_description_slide_down,
		[16] = util_scroll,
		[17] = util_multi_strcmp,
		[18] = util_save_name,
		[19] = util_IME_enable,
		[20] = util_IME_disable,
		[21] = util_IME_get_open,
		[22] = util_IME_set_open,
		[23] = NULL, // TODO IME? (NAME.MES)
		[24] = NULL, // TODO IME? (NAME.MES)
		[25] = util_IME_get_composition_started,
		[26] = NULL, // TODO IME? (NAME.MES)
		[69] = util_get_backspace,
		[70] = util_get_backspace2,
		[71] = NULL, // TODO IME? (NAME.MES)
		[100] = util_save_var4,
		[200] = util_misa_train_in,
		[201] = util_misa_train_out,
		[300] = util_movie_load,
		[301] = util_movie_play,
		[350] = util_warn_unimplemented, // TODO (MUSIC.MES)
		[351] = util_warn_unimplemented, // TODO (MUSIC.MES)
		[400] = util_load_name,
		[500] = util_500,
		[501] = util_501,
		[600] = util_end_wait,
	},
	.flags = {
		[FLAG_ANIM_ENABLE]  = 0x0004,
		[FLAG_MENU_RETURN]  = 0x0008,
		[FLAG_RETURN]       = 0x0010,
		[FLAG_LOG_ENABLE]   = 0x0020,
		[FLAG_LOG_TEXT]     = 0x0040,
		[FLAG_LOG]          = 0x0080,
		[FLAG_VOICE_ENABLE] = 0x0100,
		[FLAG_AUDIO_ENABLE] = 0x0200,
		[FLAG_STRLEN]       = 0x0400,
		[FLAG_LOG_SYS]      = 0x1000,
		[FLAG_WAIT_KEYUP]   = FLAG_ALWAYS_ON,
	},
};
