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

#include "nulib.h"
#include "ai5/anim.h"

#include "anim.h"
#include "asset.h"
#include "audio.h"
#include "backlog.h"
#include "cursor.h"
#include "game.h"
#include "gfx_private.h"
#include "input.h"
#include "map.h"
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
	savedata_load_var4(save_name);
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
	case 3: savedata_save_union_var4(sys_save_name(params)); break;
	case 4: doukyuusei_savedata_load_extra_var32(sys_save_name(params)); break;
	case 5: doukyuusei_savedata_save_extra_var32(sys_save_name(params)); break;
	case 6: memset(memory_raw + MEMORY_VAR4_OFFSET, 0, VAR4_SIZE); break;
	case 7: doukyuusei_load_variables(sys_save_name(params), vm_string_param(params, 2)); break;
	case 8: doukyuusei_savedata_load_special_flags(); break;
	case 9: doukyuusei_savedata_save_special_flags(); break;
	case 10: savedata_save_var4(sys_save_name(params)); break;
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
			// FIXME: use fill color
			gfx_display_hide();
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
		gfx_dirty(s0);
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
		gfx_dirty(s0);
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
		gfx_dirty(s0);
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
		gfx_dirty(s0);
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

		gfx_dirty(0);
		vm_peek();
		vm_timer_tick(&timer, 16);
	}
	SDL_Rect src_r = { 0, 0, 640, 480 };
	SDL_CALL(SDL_BlitSurface, src, &src_r, dst, &dst_r);
	gfx_dirty(0);
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
	if (!prev && input_down(INPUT_BACKSPACE)) {
		mem_set_var32(20, 1);
		mem_set_var32(4, 0xffffffff);
	} else {
		mem_set_var32(20, 0);
		mem_set_var32(4, 0);
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
	_sys_load_image("Y04BTR.G16", 11);

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
	.x_mult = 1,
	.use_effect_arc = false,
	.call_saves_procedures = false,
	.proc_clears_flag = false,
	.no_antialias_text = true,
	.flags_type = FLAGS_4BIT_CAPPED,
	.farcall_strlen_retvar = 11,
	.var4_size = VAR4_SIZE,
	.mem16_size = MEM16_SIZE,
	.mem_init = doukyuusei_mem_init,
	.mem_restore = doukyuusei_mem_restore,
	.init = doukyuusei_init,
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
		[24] = sys_farcall_strlen,
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
		[300] = util_warn_unimplemented, // TODO movie ({HEROINE}_E.MES, TRUE_END.MES, Y15.MES)
		[301] = util_warn_unimplemented, // TODO movie ({HEROINE}_E.MES, TRUE_END.MES, Y15.MES)
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
