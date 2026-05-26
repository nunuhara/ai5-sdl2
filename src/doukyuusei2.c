/* Copyright (C) 2026 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include "anim.h"
#include "asset.h"
#include "audio.h"
#include "backlog.h"
#include "cursor.h"
#include "game.h"
#include "gfx_private.h"
#include "input.h"
#include "map.h"
#include "menu.h"
#include "savedata.h"
#include "sys.h"
#include "vm_private.h"

#include "doukyuusei2_pal.c"

#if 0
#define PALETTE_LOG(...) NOTICE(__VA_ARGS__)
#else
#define PALETTE_LOG(...)
#endif

#define MES_NAME_SIZE 128
#define VAR4_SIZE 2048
#define MEM16_SIZE 4096

#define VAR4_OFF     MES_NAME_SIZE
#define SV16_PTR_OFF (VAR4_OFF + VAR4_SIZE)
#define VAR16_OFF    (SV16_PTR_OFF + 4)
#define SYSVAR16_OFF (VAR16_OFF + 26 * 2)
#define VAR32_OFF    (SYSVAR16_OFF + 28 * 2)
#define SYSVAR32_OFF (VAR32_OFF + 26 * 4)
#define HEAP_OFF     (SYSVAR32_OFF + 110 * 4)
_Static_assert(HEAP_OFF == 0xb10);

static void nanpa2_mem_restore(void)
{
	mem_set_sysvar16_ptr(SYSVAR16_OFF);
	mem_set_sysvar32(mes_sysvar32_file_data, offsetof(struct memory, file_data));
	mem_set_sysvar32(mes_sysvar32_menu_entry_addresses,
		offsetof(struct memory, menu_entry_addresses));
	mem_set_sysvar32(mes_sysvar32_menu_entry_numbers,
		offsetof(struct memory, menu_entry_numbers));
	mem_set_sysvar32(mes_sysvar32_map_data,
		offsetof(struct memory, map_data));
	mem_set_sysvar16(0, HEAP_OFF);
}

static void nanpa2_mem_init(void)
{
	// set up pointer table for memory access
	memory_ptr.mes_name = memory_raw;
	memory_ptr.var4 = memory_raw + VAR4_OFF;
	memory_ptr.system_var16_ptr = memory_raw + SV16_PTR_OFF;
	memory_ptr.var16 = memory_raw + VAR16_OFF;
	memory_ptr.system_var16 = memory_raw + SYSVAR16_OFF;
	memory_ptr.var32 = memory_raw + VAR32_OFF;
	memory_ptr.system_var32 = memory_raw + SYSVAR32_OFF;

	mem_set_sysvar16(mes_sysvar16_flags, 0x27);
	mem_set_sysvar16(mes_sysvar16_text_start_x, 0);
	mem_set_sysvar16(mes_sysvar16_text_start_y, 0);
	mem_set_sysvar16(mes_sysvar16_text_end_x, 640);
	mem_set_sysvar16(mes_sysvar16_text_end_y, 480);
	mem_set_sysvar16(mes_sysvar16_bg_color, 7);
	mem_set_sysvar16(mes_sysvar16_font_width, 16);
	mem_set_sysvar16(mes_sysvar16_font_height, 16);
	mem_set_sysvar16(mes_sysvar16_font_weight, 1);
	mem_set_sysvar16(mes_sysvar16_char_space, 16);
	mem_set_sysvar16(mes_sysvar16_line_space, 16);
	mem_set_sysvar16(19, 0xffff);
	mem_set_sysvar16(20, 0xffff);
	mem_set_sysvar16(mes_sysvar16_mask_color, 8);
	mem_set_sysvar32(mes_sysvar32_cg_offset, 0x20000);
	nanpa2_mem_restore();
}

static void nanpa2_menu_exec(void)
{
	uint16_t saved_flags = mem_get_sysvar16(mes_sysvar16_flags);
	vm_flag_off(FLAG_LOG_ENABLE);
	menu_exec();
	mem_set_sysvar16(mes_sysvar16_flags, saved_flags);
}

static void nanpa2_cursor(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: cursor_show(); break;
	case 1: cursor_hide(); break;
	case 3: sys_cursor_save_pos(params); break;
	case 4: cursor_set_pos(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 5: cursor_load(vm_expr_param(params, 1) * 2, 2, NULL); break;
	default: VM_ERROR("System.Cursor.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void nanpa2_anim(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: anim_init_stream(vm_expr_param(params, 1), vm_expr_param(params, 1)); break;
	case 1: anim_start(vm_expr_param(params, 1)); break;
	case 2: anim_stop(vm_expr_param(params, 1)); break;
	case 3: anim_halt(vm_expr_param(params, 1)); break;
	case 4: anim_wait(vm_expr_param(params, 1)); break;
	case 5: anim_stop_all(); break;
	case 6: anim_halt_all(); break;
	case 7: anim_reset_all(); break;
	default: VM_ERROR("System.Anim.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void nanpa2_savedata(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: savedata_resume_load(sys_save_name(params)); break;
	case 1: savedata_resume_save(sys_save_name(params)); break;
	case 2: savedata_load(sys_save_name(params), VAR4_OFF); break;
	default: VM_ERROR("System.SaveData.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void nanpa2_audio(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: audio_bgm_play(vm_string_param(params, 1), true); break;
	case 1: audio_fade(AUDIO_CH_BGM, AUDIO_VOLUME_MIN, 1000, true, false); break;
	case 2: audio_fade(AUDIO_CH_BGM, AUDIO_VOLUME_MIN, 250, true, false); break;
	case 3: audio_se_play(vm_string_param(params, 1), 0); break;
	case 4: audio_se_fade(AUDIO_VOLUME_MIN, 250, true, false, 0); break;
	default: VM_ERROR("System.Audio.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void nanpa2_voice(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: audio_voice_play(vm_string_param(params, 1), 0); break;
	case 1: audio_voice_stop(0); break;
	case 2:
		audio_voice_play(vm_string_param(params, 1), 0);
		while (audio_is_playing(AUDIO_CH_VOICE0)) {
			vm_peek();
		}
		break;
	default: VM_ERROR("System.Voice.function[%u] not implemented",
				 params->params[0].val);
	}
}

static bool nanpa2_fixed_crossfade_tick(SDL_Color new_pal[236])
{
	bool changed = false;
	for (int i = 0; i < 236; i++) {
		SDL_Color *cur_c = &gfx.palette[i];
		SDL_Color *new_c = &new_pal[i];
		if (cur_c->r < new_c->r) {
			cur_c->r++;
			changed = true;
		}
		else if (cur_c->r > new_c->r) {
			cur_c->r--;
			changed = true;
		}
		if (cur_c->g < new_c->g) {
			cur_c->g++;
			changed = true;
		}
		else if (cur_c->g > new_c->g) {
			cur_c->g--;
			changed = true;
		}
		if (cur_c->b < new_c->b) {
			cur_c->b++;
			changed = true;
		}
		else if (cur_c->b > new_c->b) {
			cur_c->b--;
			changed = true;
		}
	}
	if (changed)
		gfx_update_palette(0, 236);
	return changed;
}

static void mem_to_sdl_palette(SDL_Color new_pal[236])
{
	for (int i = 0; i < 236; i++) {
		new_pal[i].b = memory.palette[i*4];
		new_pal[i].g = memory.palette[i*4+1];
		new_pal[i].r = memory.palette[i*4+2];
	}
}

/*
 * This is a crossfade in which each color moves at a fixed velocity towards the target
 * color (so some colors will reach their target before others).
 */
static void nanpa2_fixed_crossfade(void)
{
	SDL_Color new_pal[236];
	mem_to_sdl_palette(new_pal);

	vm_timer_t timer = vm_timer_create();
	for (int i = 0; i < 256; i++) {
		if (!nanpa2_fixed_crossfade_tick(new_pal))
			break;
		vm_peek();
		vm_timer_tick(&timer, 20);
	}
}

static bool async_crossfade_active = false;
static SDL_Color async_crossfade_palette[236];
static vm_timer_t async_crossfade_t;

static void nanpa2_palette(struct param_list *params)
{
	if (vm_flag_is_on(FLAG_SAVE_PALETTE)) {
		PALETTE_LOG("(Palette saved)");
		memcpy(memory.saved_palette, memory.palette, 256 * 4);
	}
	switch (vm_expr_param(params, 0)) {
	case 1:
		if (params->nr_params > 1) {
			memset(memory.palette, (uint8_t)vm_expr_param(params, 1), 236 * 4);
			PALETTE_LOG("Palette.crossfade(%u)", params->params[1].val);
		} else {
			PALETTE_LOG("Palette.crossfade()");
		}
		gfx_palette_crossfade(memory.palette, 0, 236, 400);
		gfx_palette_copy(memory.palette, 0, 256);
		break;
	case 2:
		PALETTE_LOG("Palette.crossfade_range(%d, %d)", vm_expr_param(params, 1),
				vm_expr_param(params, 2));
		gfx_palette_crossfade(memory.palette, vm_expr_param(params, 1),
				vm_expr_param(params, 2), 400);
		gfx_palette_copy(memory.palette, 0, 256);
		break;
	case 5: {
		int start = vm_expr_param(params, 1);
		int n = vm_expr_param(params, 2);
		PALETTE_LOG("Palette.set(%d, %d)", start, n);
		_gfx_palette_set(memory.palette + start * 4, start, n);
		gfx_update_palette(start, n);
		break;
	}
	case 6:
		// Util 102/114 change whether this is a normal or 'fixed'-style crossfade.
		// In practice I think it's always a 'fixed'-style crossfade
		// Range of crossfade is always 16-236
		mem_to_sdl_palette(async_crossfade_palette);
		async_crossfade_t = vm_timer_create();
		async_crossfade_active = true;
		break;
	case 7:
		if (params->nr_params > 2) {
			memset(memory.palette, (uint8_t)vm_expr_param(params, 2), 236 * 4);
			PALETTE_LOG("Palette_crossfade2(%u,%u)", params->params[1].val,
					params->params[2].val);
		}
		else {
			PALETTE_LOG("Palette_crossfade2(%u)", params->params[1].val);
		}
		if (vm_expr_param(params, 1) == 0xff) {
			nanpa2_fixed_crossfade();
		} else {
			gfx_palette_crossfade(memory.palette, 0, 236, vm_expr_param(params, 1) * 50);
		}
		gfx_palette_copy(memory.palette, 0, 256);
		break;
	default: VM_ERROR("System.Palette.function[%u] not implemented",
				 params->params[0].val);
	}
	if (vm_flag_is_on(FLAG_LOAD_PALETTE)) {
		PALETTE_LOG("(Palette restored)");
		memcpy(memory.palette, memory.saved_palette, 256 * 4);
	}
}

static void nanpa2_pixel_crossfade(struct param_list *params)
{
	int src_x = vm_expr_param(params, 1);
	int src_y = vm_expr_param(params, 2);
	int src_w = (vm_expr_param(params, 3) - src_x) + 1;
	int src_h = (vm_expr_param(params, 4) - src_y) + 1;
	unsigned src_i = vm_expr_param(params, 5);
	int dst_x = vm_expr_param(params, 6);
	int dst_y = vm_expr_param(params, 7);
	unsigned dst_i = vm_expr_param(params, 8);
	gfx_pixel_crossfade_masked_indexed_8x8(src_x, src_y, src_w, src_h, src_i,
			dst_x, dst_y, dst_i, 255, 10);
}

static void nanpa2_graphics(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: sys_graphics_copy(params); break;
	case 1: sys_graphics_copy_masked(params); break;
	case 2: sys_graphics_fill_bg(params); break;
	case 4: sys_graphics_swap_bg_fg(params); break;
	case 5: nanpa2_pixel_crossfade(params); break;
	case 6: sys_graphics_compose(params); break;
	default: VM_ERROR("System.Graphics.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void nanpa2_map_draw_waku(void)
{
	int src_x, src_y;
	int month = mem_get_var16(7) / 100;
	int day = mem_get_var16(7) % 100;
	int day_of_week = mem_get_var16(4);
	int hour = mem_get_var16(19) / 100;
	int minute = mem_get_var16(19) % 100;
	int money = mem_get_var16(12);
	int location = mem_get_var4(2047);

	gfx_copy(0, 8, 640, 80, 0, 0, 0, 4); // save map bg to surface 4
	gfx_copy(0, 80, 88, 80, 5, 8, 8, 0); // draw datetime frame
	gfx_copy(92, 80, 88, 80, 5, 544, 8, 0); // draw location/money frame

	// month
	if (month == 12) {
		src_x = 546;
	} else {
		src_x = 570;
	}
	gfx_copy(src_x, 144, 24, 36, 5, 12, 12, 0);

	// day (digit)
	int day_i = day;
	if (day_i > 7)
		day_i -= 8;
	day_i--;
	gfx_copy(432 + (day_i % 5) * 38, (day_i / 5) * 36, 38, 36, 5, 36, 12, 0);

	// day of week
	if (day == 1) {
		// new year's day
		src_x = 594;
		src_y = 144;
	} else if (day == 23) {
		// emperor's birthday
		src_x = 608;
		src_y = 144;
	} else if (day_of_week == 0) {
		// sunday
		src_x = 608;
		src_y = 180;
	} else {
		// normal day
		src_x = 622;
		src_y = (day_of_week - 1) * 36;
	}
	gfx_copy(src_x, src_y, 14, 36, 5, 78, 12, 0);

	// AM/PM
	if (hour / 12 == 1) {
		src_x = 326;
	} else {
		src_x = 312;
	}
	gfx_copy(src_x, 132, 14, 32, 5, 12, 51, 0);

	// hour
	if (hour > 12)
		hour -= 12;
	if (hour >= 10) {
		gfx_copy(342, 80, 4, 28, 5, 32, 54, 0);
		hour -= 10;
	}
	gfx_copy(198 + hour * 14, 80, 14, 28, 5, 38, 54, 0);

	// minute
	gfx_copy(198 + (minute / 10) * 14, 80, 14, 28, 5, 60, 54, 0);
	gfx_copy(198 + (minute % 10) * 14, 80, 14, 28, 5, 76, 54, 0);

	// money
	if (money >= 1000)
		gfx_copy(328, 108, 4, 24, 5, 586, 58, 0);

	int place = 4;
	do {
		int dst_x = 562 + place * 12 + (place > 1 ? 4 : 0);
		gfx_copy(208 + (money % 10) * 12, 108, 12, 24, 5, dst_x, 56, 0);
		money /= 10;
		place--;
	} while (money && place >= 0);

	// location
	if (location == 0) {
		src_x = 448;
		src_y = 216;
	} else if (location == 6) {
		src_x = 448;
		src_y = 252;
	} else {
		src_x = 528;
		src_y = 216 + (location - 1) * 36;
	}
	gfx_copy(src_x, src_y, 80, 36, 5, 548, 12, 0);
}

static void nanpa2_map_draw_tiles(void)
{
	map_draw_tiles();
	nanpa2_map_draw_waku();
}

static void nanpa2_map_draw_tiles2(void)
{
	gfx_copy(0, 0, 640, 80, 4, 0, 8, 0);
	nanpa2_map_draw_tiles();
}

static void nanpa2_map_exec_sprites_and_redraw(void)
{
	map_exec_sprites();
	map_load_tiles();
	map_place_sprites();
	nanpa2_map_draw_tiles2();
	if (mem_get_var16(18) == 0xfffe) {
		// XXX: bug (misfeature?) in game: single click can open and close map
		//      if held too long
		input_wait_until_up(INPUT_CANCEL);
	}
}

static void nanpa2_map_set_sprite_script(int sp_no, int script_no)
{
	if (!map_is_pathing())
		map_set_sprite_script(sp_no, script_no);
}

static void nanpa2_map(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: map_load_tilemap(); break;
	case 1: map_spawn_sprite(vm_expr_param(params, 1), vm_expr_param(params, 2),
				vm_expr_param(params, 3)); break;
	case 2: map_load_tiles(); break;
	case 4: map_load_sprite_scripts(); break;
	case 5: nanpa2_map_set_sprite_script(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 6: map_place_sprites(); break;
	case 7: map_set_sprite_state(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 8: nanpa2_map_exec_sprites_and_redraw(); break;
	case 9: map_exec_sprites(); break;
	case 10: nanpa2_map_draw_tiles(); break;
	case 11: nanpa2_map_draw_tiles2(); break;
	case 12: map_set_location_mode(vm_expr_param(params, 1)); break;
	case 13: map_get_location(); break;
	case 14: map_move_sprite(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 15: map_path_sprite(vm_expr_param(params, 1), vm_expr_param(params, 2),
				 vm_expr_param(params, 3)); break;
	case 16: map_stop_pathing(); break;
	case 17: map_get_pathing(); break;
	case 20: map_rewind_sprite_pos(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 21: map_skip_pathing(0); break;
	case 22: map_set_sprite_anim(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	default: VM_ERROR("System.Map.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void nanpa2_backlog(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: backlog_clear(); break;
	case 1: backlog_prepare_old(); break;
	case 2: backlog_commit_old(); break;
	case 3: {
		unsigned count = backlog_count();
		mem_set_var16(18, count ? count - 1 : 0xffff);
		break;
	}
	case 4: mem_set_var32(18, backlog_get_pointer(vm_expr_param(params, 1))); break;
	default: VM_ERROR("System.Backlog.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void nanpa2_checkdisc(struct param_list *params)
{
	// XXX: We don't support reading from CD-ROM, so we treat all discs as being present
	//      at all times (the user must manually install all .ARC files into the game
	//      folder).
	mem_set_var16(18, 1);
}

static void nanpa2_sys_20(struct param_list *params)
{
	while (!input_down(INPUT_ACTIVATE) && !input_down(INPUT_CANCEL))
		vm_peek();
	while (input_down(INPUT_ACTIVATE) || input_down(INPUT_CANCEL))
		vm_peek();
}

static uint8_t *saved_memory = NULL;

static void nanpa2_save_memory(struct param_list *params)
{
	if (!saved_memory)
		saved_memory = xmalloc(MEM16_SIZE);
	memcpy(saved_memory, memory_raw, MEM16_SIZE);
}

static void nanpa2_restore_memory(struct param_list *params)
{
	memcpy(memory_raw + 0x83a, saved_memory + 0x83a, 29);
	memcpy(memory_raw + 0xef8, saved_memory + 0xef8, 36);
	memory_raw[VAR4_OFF + 2030] = saved_memory[VAR4_OFF + 2030];
}

static void nanpa2_yuki_start(struct param_list *params)
{
	// TODO
}

static void nanpa2_yuki_end(struct param_list *params)
{
	// TODO
}

static void nanpa2_yuki_resume(struct param_list *params)
{
	// TODO
}

static void nanpa2_save_palette(struct param_list *params)
{
	int start = vm_expr_param(params, 1);
	int n = vm_expr_param(params, 2);
	PALETTE_LOG("nanpa2_save_palette(%d, %d)", start, n);
	if (n < 0)
		return;
	if (start + n > 256) {
		WARNING("Invalid range in restore_palette");
		n = 256 - start;
	}
	memcpy(memory.saved_palette + start * 4, memory.palette + start * 4, n * 4);
}

static void nanpa2_restore_palette(struct param_list *params)
{
	PALETTE_LOG("nanpa2_restore_palette()");
	memcpy(memory.palette, memory.saved_palette, 256 * 4);
}

static void nanpa2_load_low_palette(struct param_list *params)
{
	PALETTE_LOG("nanpa2_load_low_palette[%d]", mem_get_var4(2024));
	int n = mem_get_var4(2024) == 1 ? 10 : 16;
	memcpy(memory.palette, nanpa2_low_palette, n * 4);
}

static void nanpa2_ctrl_is_down(struct param_list *params)
{
	mem_set_var16(18, input_down(INPUT_CTRL));
}

static void nanpa2_activate_is_down(struct param_list *params)
{
	mem_set_var16(18, input_down(INPUT_ACTIVATE));
}

static void nanpa2_wait_for_activate_cancel_up(struct param_list *params)
{
	while (input_down(INPUT_ACTIVATE))
		vm_peek();
	while (input_down(INPUT_CANCEL))
		vm_peek();
}

#define MAP_FRAME_TIME 54

static vm_timer_t nanpa2_map_timer = 0;

static void nanpa2_map_wait(struct param_list *params)
{
	vm_timer_tick(&nanpa2_map_timer, input_down(INPUT_SHIFT) ? MAP_FRAME_TIME/2 : MAP_FRAME_TIME);
}

static void nanpa2_load_mid_palette(struct param_list *params)
{
	memcpy(memory.palette + 16*4, nanpa2_mid_palette, 48 * 4);
}

static void nanpa2_util_105(struct param_list *params)
{
	PALETTE_LOG("nanpa2_util_105");
	int n = mem_get_var4(2024) == 1 ? 10 : 16;
	gfx_palette_copy(memory.saved_palette, 0, 236);
	memcpy(memory.palette + n*4, memory.saved_palette + n*4, (256 - n) * 4);
}

static void nanpa2_util_106(struct param_list *params)
{
	PALETTE_LOG("nanpa2_util_106");
	int n = mem_get_var4(2024) == 1 ? 10 : 16;
	memcpy(memory.palette + n*4, memory.saved_palette + n*4, (256 - n) * 4);
}

static void nanpa2_sp_load(struct param_list *params)
{
	savedata_read("flag06", memory_raw, 0xf5c, 14);
}

static void nanpa2_cancel_is_down(struct param_list *params)
{
	mem_set_var16(18, input_down(INPUT_CANCEL));
}

static void nanpa2_crossfade_static_palette(struct param_list *params)
{
	PALETTE_LOG("nanpa2_crossfade_static_palette()");
	memcpy(memory.palette, nanpa2_static_palette, 256 * 4);
	nanpa2_fixed_crossfade();
	gfx_palette_copy(memory.palette, 0, 256);
}

static void nanpa2_util_110(struct param_list *params)
{
	unsigned i = vm_expr_param(params, 1) * 4 + vm_expr_param(params, 2);
	if (i >= 16)
		VM_ERROR("Invalid palette number: %u", i);

	memcpy(memory.palette + 16 * 4, nanpa2_map_palettes[i], 172 * 4);
}

static void nanpa2_util_111(struct param_list *params)
{
	// TODO: add number to backlog
}

static void nanpa2_load_end_sepia_palette(struct param_list *params)
{
	unsigned n = vm_expr_param(params, 1) * 4 + vm_expr_param(params, 2);
	if (n >= 64) {
		WARNING("Invalid palette number");
		return;
	}
	memcpy(memory.palette + 16 * 4, nanpa2_end_sepia_palette[n], 220 * 4);
}

static void nanpa2_load_end_color_palette(struct param_list *params)
{
	unsigned n = vm_expr_param(params, 1) * 4 + vm_expr_param(params, 2);
	if (n >= 64) {
		WARNING("Invalid palette number");
		return;
	}
	memcpy(memory.palette + 16 * 4, nanpa2_end_color_palette[n], 220 * 4);
}

static void nanpa2_init_end_crossfade(struct param_list *params)
{
	memcpy(memory.palette, nanpa2_end_low_palette, 16 * 4);
}

static void nanpa2_clear_high_palette(struct param_list *params)
{
	PALETTE_LOG("nanpa2_clear_high_palette()");
	memset(memory.palette + 16 * 4, 0, 220 * 4);
}

static void nanpa2_wait(struct param_list *params)
{
	int n = vm_expr_param(params, 1);
	vm_timer_t t = vm_timer_create();
	for (int i = 0; i < n; i++) {
		vm_peek();
		vm_timer_tick(&t, 15);
	}
}

static bool palette_equal(uint8_t *a, uint8_t *b, int start, int end)
{
	for (int i = start; i < end; i++) {
		if (memcmp(a + i * 4, b + i * 4, 3))
			return false;
	}
	return true;
}

static void nanpa2_hana_crossfade(struct param_list *params)
{
	memcpy(memory.palette, nanpa2_hana_palette, 256 * 4);
	nanpa2_fixed_crossfade();
	gfx_palette_copy(memory.palette, 0, 256);
}

static void nanpa2_hana_start(struct param_list *params)
{
	// TODO
}

static void nanpa2_hana_end(struct param_list *params)
{
	// TODO
}

static void nanpa2_util_120(struct param_list *params)
{
	PALETTE_LOG("nanpa_util_120()");
	uint8_t pal[236 * 4];
	gfx_palette_copy(pal, 0, 236);
	memcpy(memory.palette, memory.saved_palette, 236 * 4);

	// if background palette (128-236) is equal, return 0
	if (palette_equal(memory.palette, pal, 128, 236)) {
		PALETTE_LOG("(equal)");
		mem_set_var16(18, 0);
		return;
	}

	// otherwise crossfade to black and return 1
	PALETTE_LOG("(not equal)");
	memset(memory.palette + 16 * 4, 0, 220 * 4);
	gfx_palette_crossfade(memory.palette, 0, 236, 400);
	gfx_palette_copy(memory.palette, 0, 256);
	mem_set_var16(18, 1);
}

static void nanpa2_util_122(struct param_list *params)
{
	PALETTE_LOG("nanpa_util_122()");
	gfx_palette_copy(memory.palette, 0, 236);
	memcpy(memory.palette + 16 * 4, memory.saved_palette + 16 * 4, 112 * 4);
}

static void nanpa2_util_123(struct param_list *params)
{
	uint8_t pal[236 * 4];
	gfx_palette_copy(pal, 0, 236);

	int n = mem_get_var4(2024) == 1 ? 10 : 16;
	memcpy(memory.saved_palette, nanpa2_low_palette, n * 4);
	memcpy(memory.palette, memory.saved_palette, 236 * 4);

	// if background and character palettes (10 - 236) are equal, return 0
	if (palette_equal(memory.palette, pal, 10, 236)) {
		PALETTE_LOG("(equal)");
		mem_set_var16(18, 0);
		return;
	}

	// otherwise crossfade to black and return 1
	PALETTE_LOG("(not equal)");
	memset(memory.palette + 10 * 4, 0, 226 * 4);
	gfx_palette_crossfade(memory.palette, 0, 236, 400);
	gfx_palette_copy(memory.palette, 0, 256);
	gfx_fill(0, 0, 640, 400, 0, mes_sysvar16_bg_color >> 4);
	mem_set_var16(18, 1);
}

static void nanpa2_anim_load_palette(uint8_t *src)
{
	uint8_t pal[256 * 4];
	uint8_t *dst = pal;
	for (int i = 0; i < 236; i++, src += 3, dst += 4) {
		// XXX: BRG -> BGR
		dst[0] = src[0];
		dst[1] = src[2];
		dst[2] = src[1];
		dst[3] = 1;
	}
	gfx_palette_set(pal, 0, 236);
}

static void nanpa2_init(void)
{
	bool have_boy = asset_set_voice_archive("boy.arc");
	bool have_girl = asset_set_voice2_archive("girl.arc");
	bool have_pr = asset_set_voice3_archive("pr.arc");
	bool have_sp = asset_set_voice4_archive("sp.arc");
	if (!have_boy || !have_girl || !have_pr || !have_sp) {
		char msg[1024];
		snprintf(msg, 1024, "Missing voice archives:\n%s%s%s%s"
				"Voice archives from both game discs must be copied into "
				"the game directory,\notherwise some voices may be missing.",
				have_boy ? "" : "\t\"boy.arc\"\n",
				have_girl ? "" : "\t\"girl.arc\"\n",
				have_pr ? "" : "\t\"pr.arc\"\n",
				have_sp ? "" : "\t\"sp.arc\"\n");
		gfx_warning_message(msg);
	}

	text_shadow = TEXT_SHADOW_A;
	gfx_update_palette(0, 256);
	anim_load_palette = nanpa2_anim_load_palette;
}

static void nanpa2_update(void)
{
	if (async_crossfade_active && vm_timer_tick_async(&async_crossfade_t, 15)) {
		if (!nanpa2_fixed_crossfade_tick(async_crossfade_palette)) {
			async_crossfade_active = false;
		}
	}
	// TODO: hana
	// TODO: snow
}

struct game game_doukyuusei2 = {
	.id = GAME_DOUKYUUSEI2,
	.surface_sizes = {
		[0] = { 640,  400 },
		[1] = { 640, 1800 },
		[2] = { 640,  400 },
		[3] = { 640,  400 },
		[4] = { 640,  400 },
		[5] = { 640, 2800 },
	},
	.bpp = 8,
	.var4_size = VAR4_SIZE,
	.mem16_size = MEM16_SIZE,
	.mem_init = nanpa2_mem_init,
	.mem_restore = nanpa2_mem_restore,
	.init = nanpa2_init,
	.update = nanpa2_update,
	.after_anim_draw = NULL,
	.unprefixed_zen = vm_stmt_txt_no_log,
	// FIXME?: unprefixed hankaku is ignored
	.unprefixed_han = vm_stmt_str_no_log,
	.vm = VM_AI5,
	.expr_op = {
		DEFAULT_EXPR_OP,
		[0xe5] = vm_expr_rand_with_imm_range,
	},
	.stmt_op = {
DEFAULT_STMT_OP,
		[0x01] = vm_stmt_txt_old_log,
		[0x02] = vm_stmt_str_old_log,
		[0x03] = vm_stmt_set_flag_const16_4bit_saturate,
		// XXX: set_flag_expr does NOT saturate!
		[0x0b] = vm_stmt_sys_old_log,
		[0x0f] = vm_stmt_call_old_log,
		[0x11] = vm_stmt_line_old_log,
		[0x13] = nanpa2_menu_exec,
	},
	.sys = {
		[0] = &sys_set_font_size,
		[1] = &sys_display_number,
		[2] = &nanpa2_cursor,
		[3] = &nanpa2_anim,
		[4] = &nanpa2_savedata,
		[5] = &nanpa2_audio,
		[6] = &nanpa2_voice,
		[7] = &sys_load_file,
		[8] = &sys_load_image,
		[9] = &nanpa2_palette,
		[10] = &nanpa2_graphics,
		[11] = &sys_wait,
		[12] = &sys_set_text_colors_indexed_with_sysvar,
		[13] = &sys_farcall,
		[14] = &sys_get_cursor_segment,
		[15] = &sys_menu_get_no,
		[16] = &sys_get_time,
		[17] = &nanpa2_map,
		[18] = &nanpa2_backlog,
		[19] = &nanpa2_checkdisc,
		[20] = &nanpa2_sys_20,
	},
	.util = {
		[90] = nanpa2_save_memory,
		[91] = nanpa2_restore_memory,
		[92] = nanpa2_yuki_start,
		[93] = nanpa2_yuki_end,
		[94] = nanpa2_save_palette,
		[95] = nanpa2_restore_palette,
		[96] = nanpa2_load_low_palette,
		[97] = nanpa2_ctrl_is_down,
		// 98 unused
		[99] = nanpa2_activate_is_down,
		[100] = nanpa2_wait_for_activate_cancel_up,
		[101] = util_warn_unimplemented,
		[102] = util_warn_unimplemented,
		[103] = nanpa2_map_wait,
		[104] = nanpa2_load_mid_palette,
		[105] = nanpa2_util_105,
		[106] = nanpa2_util_106,
		[107] = nanpa2_sp_load,
		[108] = nanpa2_cancel_is_down,
		[109] = nanpa2_crossfade_static_palette,
		[110] = nanpa2_util_110,
		[111] = nanpa2_util_111,
		[112] = nanpa2_load_end_sepia_palette,
		[113] = nanpa2_load_end_color_palette,
		[114] = nanpa2_init_end_crossfade,
		[115] = nanpa2_clear_high_palette,
		[116] = nanpa2_wait,
		[117] = nanpa2_hana_crossfade,
		[118] = nanpa2_hana_start,
		[119] = nanpa2_hana_end,
		[120] = nanpa2_util_120,
		// 121 unused
		[122] = nanpa2_util_122,
		[123] = nanpa2_util_123,
		[124] = nanpa2_yuki_end,
		[125] = nanpa2_yuki_resume,
	},
	.flags = {
		[FLAG_LOAD_PALETTE] = 0x0001,
		[FLAG_SAVE_PALETTE] = 0x0002,
		[FLAG_ANIM_ENABLE]  = 0x0004,
		[FLAG_MENU_RETURN]  = 0x0008,
		[FLAG_RETURN]       = 0x0010,
		[FLAG_LOG_TEXT]     = 0x0040,
		[FLAG_LOG]          = 0x0080,
		[FLAG_VOICE_ENABLE] = 0x0200,
		[FLAG_LOG_ENABLE]   = 0x0400,
		[FLAG_AUDIO_ENABLE] = FLAG_ALWAYS_ON,
		[FLAG_WAIT_KEYUP]   = FLAG_ALWAYS_ON,
	},
};
