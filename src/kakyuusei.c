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

#if 0
#define PALETTE_LOG(...) NOTICE(__VA_ARGS__)
#else
#define PALETTE_LOG(...)
#endif

#define MES_NAME_SIZE 128
#define VAR4_SIZE 4096
#define MEM16_SIZE 8192

#define VAR4_OFF     MES_NAME_SIZE
#define SV16_PTR_OFF (VAR4_OFF + VAR4_SIZE)
#define VAR16_OFF    (SV16_PTR_OFF + 4)
#define SYSVAR16_OFF (VAR16_OFF + 26 * 2)
#define VAR32_OFF    (SYSVAR16_OFF + 28 * 2)
#define SYSVAR32_OFF (VAR32_OFF + 26 * 4)
#define HEAP_OFF     (SYSVAR32_OFF + 210 * 4)
_Static_assert(HEAP_OFF == 0x14a0);

static void kakyuusei_mem_restore(void)
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

static void kakyuusei_mem_init(void)
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
	kakyuusei_mem_restore();
}

static void kakyuusei_menu_exec(void)
{
	uint16_t saved_flags = mem_get_sysvar16(mes_sysvar16_flags);
	vm_flag_off(FLAG_LOG_ENABLE);
	menu_exec();
	mem_set_sysvar16(mes_sysvar16_flags, saved_flags);
}

static void kakyuusei_cursor(struct param_list *params)
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

static void kakyuusei_anim(struct param_list *params)
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
	case 8: anim_wait_all(); break;
	default: VM_ERROR("System.Anim.function[%u] not implemented",
				 params->params[0].val);
	}
}

/*
 * Redraw message box if clobbered by animation.
 */
static void kakyuusei_after_anim_copy(struct anim_draw_call *call)
{
	int dst_x, dst_y, w, h;
	anim_decompose_draw_call(call, &dst_x, &dst_y, &w, &h);
	if (dst_y + h <= 280 || dst_x + w <= 16 || dst_x >= 624)
		return;

	if (dst_y < 280) {
		h -= 280 - dst_y;
		dst_y = 280;
	}
	if (dst_x < 16) {
		w -= 16 - dst_x;
		dst_x = 16;
	}
	if (dst_x + w > 624) {
		w = 624 - dst_x;
	}

	// update clean bg on surface 7
	gfx_copy(dst_x, dst_y, w, h, 0, dst_x - 16, dst_y - 280, 7);

	if (mem_get_var4(2829) || mem_get_var4(2808) != 1)
		return;

	// draw message box
	gfx_copy_masked(dst_x - 16, 120 + (dst_y - 280), w, h, 7, dst_x, dst_y, 0, 0);
}

/*
 * Redraw item window if clobbered by animation.
 */
static void kakyuusei_after_anim_copy_masked(struct anim_draw_call *call)
{
	if (mem_get_var4(3020) != 1)
		return;

	int dst_x, dst_y, w, h;
	anim_decompose_draw_call(call, &dst_x, &dst_y, &w, &h);
	if (dst_x + w <= 152 || dst_x > 152 + 336 || dst_y + h <= 128 || dst_y > 128 + 144)
		return;

	if (dst_x < 152) {
		w -= 152 - dst_x;
		dst_x = 152;
	}
	if (dst_x + w > 152 + 336) {
		w = (152 + 336) - dst_x;
	}
	if (dst_y < 128) {
		h -= 128 - dst_y;
		dst_y = 128;
	}
	if (dst_y + h > 128 + 144) {
		h = (128 + 144) - dst_y;
	}

	int s8_x = 240 + (dst_x - 152);

	// update clean bg on surface 8
	gfx_copy(dst_x, dst_y, w, h, 0, s8_x, dst_y - 128, 8);

	if (mem_get_var4(2829))
		return;

	// draw item box
	gfx_copy_masked(s8_x, 288 + (dst_y - 128), w, h, 8, dst_x, dst_y, 0, 0);
}

static void kakyuusei_after_anim_draw(struct anim_draw_call *call)
{
	if (call->op == ANIM_DRAW_OP_COPY)
		kakyuusei_after_anim_copy(call);
	else if (call->op == ANIM_DRAW_OP_COPY_MASKED)
		kakyuusei_after_anim_copy_masked(call);
}

static void kakyuusei_resume_load(const char *save_name)
{
	savedata_resume_load(save_name);
	// load player name
	savedata_read("FLAG08", memory_raw, HEAP_OFF + 2880, 16);
}

static void kakyuusei_save(const char *save_name)
{
	uint8_t save[MEM16_SIZE];
	uint8_t *var4 = save + VAR4_OFF;
	uint8_t *mem_var4 = memory_raw + VAR4_OFF;
	savedata_read(save_name, save, 0, MEM16_SIZE);
	for (int i = 0; i < VAR4_SIZE; i++) {
		if (mem_var4[i])
			var4[i] = mem_var4[i];
	}
	unsigned off = SYSVAR32_OFF + 240;
	memcpy(save+off, memory_raw+off, 200);
	savedata_write(save_name, save, 0, MEM16_SIZE);
}

static void kakyuusei_savedata(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: kakyuusei_resume_load(sys_save_name(params)); break;
	case 1: savedata_resume_save(sys_save_name(params)); break;
	case 2: savedata_load(sys_save_name(params), VAR4_OFF); break;
	case 3: kakyuusei_save(sys_save_name(params)); break;
	case 4: savedata_load_variables(sys_save_name(params), vm_string_param(params, 2)); break;
	default: VM_ERROR("System.SaveData.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void kakyuusei_audio(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: audio_bgm_play(vm_string_param(params, 1), true); break;
	case 1: audio_fade(AUDIO_CH_BGM, AUDIO_VOLUME_MIN, 250, true, false); break;
	case 2: audio_fade(AUDIO_CH_BGM, AUDIO_VOLUME_MIN, 3000, true, false); break;
	case 3: audio_se_play(vm_string_param(params, 1), vm_expr_param(params, 2)); break;
	case 4: audio_se_fade(AUDIO_VOLUME_MIN, 3000, true, false, vm_expr_param(params, 1)); break;
	case 5: audio_se_stop(vm_expr_param(params, 1)); break;
	default: VM_ERROR("System.Audio.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void kakyuusei_voice(struct param_list *params)
{
	if (!vm_flag_is_on(FLAG_VOICE_ENABLE))
		return;
	switch (vm_expr_param(params, 0)) {
	case 0: audio_voice_play(vm_string_param(params, 1), 0); break;
	case 1: audio_voice_stop(0); break;
	case 2: mem_set_var16(18, audio_is_playing(AUDIO_CH_VOICE0)); break;
	default: VM_ERROR("System.Voice.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void kakyuusei_load_image(struct param_list *params)
{
	int dst_surface = mem_get_sysvar16(mes_sysvar16_dst_surface);
	if (dst_surface < 2)
		anim_halt_all();
	_sys_load_image(vm_string_param(params, 0), dst_surface, 1);
}

static uint8_t extra_palette_256[0x400] = {0};
static uint8_t extra_palette_16[0x40] = {0};

static void kakyuusei_palette(struct param_list *params)
{
	if (vm_flag_is_on(FLAG_SAVE_PALETTE)) {
		PALETTE_LOG("(Palette saved)");
		memcpy(extra_palette_256, memory.palette, 256 * 4);
	}
	switch (vm_expr_param(params, 0)) {
	case 1:
		if (params->nr_params > 1) {
			memset(memory.palette, (uint8_t)vm_expr_param(params, 1), 236 * 4);
			PALETTE_LOG("Palette.crossfade(%u)", params->params[1].val);
		} else {
			PALETTE_LOG("Palette.crossfade()");
		}
		gfx_palette_crossfade(memory.palette, 0, 236, 1000);
		gfx_palette_copy(memory.palette, 0, 256);
		break;
	case 5: {
		int start = vm_expr_param(params, 1);
		int n = vm_expr_param(params, 2);
		PALETTE_LOG("Palette.set(%d, %d)", start, n);
		_gfx_palette_set(memory.palette, 0, 236);
		gfx_update_palette(start, n);
		break;
	}
	case 7:
		if (params->nr_params > 2) {
			memset(memory.palette, (uint8_t)vm_expr_param(params, 2), 236 * 4);
			PALETTE_LOG("Palette_crossfade2(%u,%u)", params->params[1].val,
					params->params[2].val);
		}
		else {
			PALETTE_LOG("Palette_crossfade2(%u)", params->params[1].val);
		}
		gfx_palette_crossfade(memory.palette, 0, 236, vm_expr_param(params, 1) * 50);
		gfx_palette_copy(memory.palette, 0, 256);
		break;
	default: VM_ERROR("System.Palette.function[%u] not implemented",
				 params->params[0].val);
	}
	if (vm_flag_is_on(FLAG_LOAD_PALETTE)) {
		PALETTE_LOG("(Palette restored)");
		memcpy(memory.palette, extra_palette_256, 256 * 4);
	}
}

static void kakyuusei_pixel_crossfade(struct param_list *params, bool slow)
{
	int src_x = vm_expr_param(params, 1);
	int src_y = vm_expr_param(params, 2);
	int src_w = (vm_expr_param(params, 3) - src_x) + 1;
	int src_h = (vm_expr_param(params, 4) - src_y) + 1;
	unsigned src_i = vm_expr_param(params, 5);
	int dst_x = vm_expr_param(params, 6);
	int dst_y = vm_expr_param(params, 7);
	unsigned dst_i = vm_expr_param(params, 8);
	if (slow)
		gfx_pixel_crossfade_masked_indexed_8x8(src_x, src_y, src_w, src_h, src_i,
				dst_x, dst_y, dst_i, 0);
	else
		gfx_pixel_crossfade_masked_indexed(src_x, src_y, src_w, src_h, src_i, dst_x,
				dst_y, dst_i, 0);
}

static void kakyuusei_graphics(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: sys_graphics_copy(params); break;
	case 1: sys_graphics_copy_masked(params); break;
	case 2: sys_graphics_fill_bg(params); break;
	case 5: kakyuusei_pixel_crossfade(params, false); break;
	case 7: kakyuusei_pixel_crossfade(params, true); break;
	default: VM_ERROR("System.Graphics.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void kakyuusei_wait(struct param_list *params)
{
	if (params->nr_params > 0 && vm_expr_param(params, 0) == 0) {
		params->params[0].val = 1;
	}
	sys_wait(params);
}

static void kakyuusei_set_text_colors(struct param_list *params)
{
	uint8_t param = vm_expr_param(params, 0);
	uint8_t bg = (param & 0xf0) >> 4;
	uint8_t fg = param & 0x0f;
	mem_set_sysvar16(mes_sysvar16_bg_color, ((uint16_t)bg << 8) | fg);
	gfx_text_set_colors(bg, fg);
}

static void draw_datetime(void)
{
	int buffer = 8;
	int screen = 0;
	int w = 120;
	int h = 128;
	gfx_copy(16, 16, w, h, screen, 0, 0, buffer);
	gfx_copy_masked(0, 256, w, h, buffer, 16, 16, screen, 0);
	gfx_copy(504, 16, w, h, screen, 120, 0, buffer);
	gfx_copy_masked(120, 256, w, h, buffer, 504, 16, screen, 0);
}

static void kakyuusei_map_exec_sprites_and_redraw(void)
{
	map_exec_sprites();
	map_load_tiles();
	map_place_sprites();
	map_draw_tiles();
	draw_datetime();
}

static void kakyuusei_map_draw_tiles(void)
{
	map_draw_tiles();
	draw_datetime();
}

static void kakyuusei_map(struct param_list *params)
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
	case 8: kakyuusei_map_exec_sprites_and_redraw(); break;
	case 9: map_exec_sprites(); break;
	case 10:
	case 11: kakyuusei_map_draw_tiles(); break;
	case 12: map_set_location_mode(vm_expr_param(params, 1)); break;
	case 13: map_get_location(); break;
	case 14: map_move_sprite(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 15: map_path_sprite(vm_expr_param(params, 1), vm_expr_param(params, 2),
				 vm_expr_param(params, 3)); break;
	case 16: map_stop_pathing(); break;
	case 17: map_get_pathing(); break;
	case 20: map_rewind_sprite_pos(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 21: map_skip_pathing(vm_expr_param(params, 1)); break;
	default: VM_ERROR("System.Map.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void kakyuusei_backlog(struct param_list *params)
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

/*
 * Copy from main to extra palette bank.
 */
static void kakyuusei_save_palette(struct param_list *params)
{
	int start = vm_expr_param(params, 1);
	int n = vm_expr_param(params, 2);
	PALETTE_LOG("Palette.save(%d, %d)", start, n);
	if (start + n < 0 || start + n > 256)
		VM_ERROR("Invalid palette range: %d+%d", start, n);
	memcpy(extra_palette_256 + start*4, memory.palette + start*4, n * 4);
}

/*
 * Copy from extra to main palette bank.
 */
static void kakyuusei_restore_palette(struct param_list *params)
{
	PALETTE_LOG("Palette.restore()");
	memcpy(memory.palette, extra_palette_256, 256 * 4);
}

/*
 * Copy from extra to main palette bank (system colors).
 */
static void kakyuusei_reset_low_palette(struct param_list *params)
{
	PALETTE_LOG("Palette.restore_low()");
	memcpy(memory.palette, extra_palette_16, 16 * 4);
}

static void kakyuusei_ctrl_is_down(struct param_list *params)
{
	mem_set_var16(18, input_down(INPUT_CTRL));
}

static void kakyuusei_activate_is_down(struct param_list *params)
{
	mem_set_var16(18, input_down(INPUT_ACTIVATE));
}

static void kakyuusei_wait_until_activate_is_up(struct param_list *params)
{
	while (input_down(INPUT_ACTIVATE)) {
		vm_peek();
	}
	while (input_down(INPUT_CANCEL)) {
		vm_peek();
	}
}

static vm_timer_t ticks;

static void kakyuusei_timer_init(struct param_list *params)
{
	ticks = vm_timer_create();
}

static void kakyuusei_timer_set(struct param_list *params)
{
	vm_timer_t now = vm_timer_create();
	ticks = ((ticks + 800) - now) / 200;
}

static void kakyuusei_timer_wait(struct param_list *params)
{
	if (ticks > 20000) {
		WARNING("Util.timer_wait called with t > 20s");
		ticks = 20000;
	}
	vm_delay(ticks);
}

static int move_speed = 2;

/*
 * Delay to control movement speed on map.
 */
static void kakyuusei_move_tick(struct param_list *params)
{
	vm_timer_t timer = vm_timer_create();
	for (int i = 0; i < move_speed; i++) {
		if (input_down(INPUT_SHIFT))
			return;
		vm_timer_tick(&timer, 30);
	}
}

/*
 * Sepia-ish effect. Used when viewing backlog.
 */
static void kakyuusei_crossfade_sepia(struct param_list *params)
{
	gfx_palette_copy(extra_palette_256, 0, 236);
	for (int i = 16; i < 256; i++) {
		float f_r = extra_palette_256[i * 4 + 2];
		float f_g = extra_palette_256[i * 4 + 1];
		float f_b = extra_palette_256[i * 4 + 0];
		float avg = (f_r + f_g + f_b) / 3.f;
		uint8_t col[3] = {
			avg,
			min(255, (unsigned)(avg * 1.02f)),
			min(255, (unsigned)(avg * 1.2f))
		};
		memcpy(memory.palette + i * 4, col, 3);
	}
	gfx_palette_crossfade(memory.palette, 0, 236, 1000);
}

/*
 * Palette crossfade (excluding system colors).
 */
static void kakyuusei_crossfade_high_palette(struct param_list *params)
{
	memcpy(memory.palette + 16 * 4, extra_palette_256 + 16 * 4, 240 * 4);
	gfx_palette_crossfade(memory.palette, 0, 236, 1000);
}

/*
 * Load the player name from disk.
 */
static void kakyuusei_load_player_name(struct param_list *params)
{
	savedata_read("FLAG08", memory_raw, HEAP_OFF + 2880, 32);
}

/*
 * Push a number to the backlog.
 */
static void kakyuusei_backlog_add_number(struct param_list *params)
{
	const char *str = sys_number_to_string(vm_expr_param(params, 1));
	backlog_push_byte(1);
	while (*str) {
		backlog_push_byte(*str);
		str++;
	}
	backlog_push_byte(0);
}

#define SCROLL_DELTA 2
#define MOVE_DELTA 4

static void scroll_tick(int x, int y, vm_timer_t *timer)
{
	gfx_copy(x, y, 640, 400, 9, 0, 0, 0);
	gfx_update();
	vm_peek();
	vm_timer_tick(timer, 30);
}

/*
 * Interactive scrolling effect. Used for viewing posters and a few character events.
 */
static void kakyuusei_scroll(struct param_list *params)
{
	//int x = vm_expr_param(params, 1); // XXX: always 0
	//int y = vm_expr_param(params, 2); // XXX: always 0
	int w = vm_expr_param(params, 3);
	int h = vm_expr_param(params, 4);
	unsigned flags = vm_expr_param(params, 5);

	if (!flags) {
		gfx_copy(0, 0, 640, 400, 9, 0, 0, 0);
	} else if (flags & 8) {
		// scroll with arrow keys
		SDL_Point cur = { 0, 0 };
		SDL_Point limit = { w - 640, h - 400 };
		vm_timer_t timer = vm_timer_create();
		while (true) {
			bool dirty = false;
			if (input_down(INPUT_ACTIVATE)) {
				input_wait_until_up(INPUT_ACTIVATE);
				break;
			}
			if (input_down(INPUT_LEFT) && cur.x > 0) {
				cur.x = max(0, cur.x - MOVE_DELTA);
				dirty = true;
			}
			if (input_down(INPUT_RIGHT) && cur.x < limit.x) {
				cur.x = min(limit.x, cur.x + MOVE_DELTA);
				dirty = true;
			}
			if (input_down(INPUT_UP) && cur.y > 0) {
				cur.y = max(0, cur.y - MOVE_DELTA);
				dirty = true;
			}
			if (input_down(INPUT_DOWN)) {
				cur.y = min(limit.y, cur.y + MOVE_DELTA);
				dirty = true;
			}
			if (dirty) {
				scroll_tick(cur.x, cur.y, &timer);
			} else {
				vm_peek();
				vm_timer_tick(&timer, 30);
			}
		}

		// return to origin
		int dx = 0;
		float dy = 0.f;
		float start_y = cur.y;
		if (cur.x && cur.y) {
			float fy = (float)cur.y / (float)cur.x;
			dx = -SCROLL_DELTA;
			dy = fy * -SCROLL_DELTA;
		} else if (cur.x) {
			dx = -SCROLL_DELTA;
		} else if (cur.y) {
			dy = -SCROLL_DELTA;
		}

		for (int frame = 1; cur.x || cur.y; frame++) {
			cur.x = max(0, cur.x + dx);
			cur.y = max(0, start_y + dy * frame);
			scroll_tick(cur.x, cur.y, &timer);
		}
	} else {
		// scroll to opposite corner and back
		if (w <= 640)
			flags &= 0xd;
		if (h <= 400)
			flags &= 0xb;
		SDL_Point end = { flags & 2 ? w - 640 : 0, flags & 4 ? h - 400 : 0 };
		int dx = 0;
		float dy = 0.f;
		if ((flags & 2) && (flags & 4)) {
			float fy = (float)(h - 400) / (float)(w - 640);
			dx = SCROLL_DELTA;
			dy = fy * SCROLL_DELTA;
		} else if (flags & 2) {
			dx = SCROLL_DELTA;
		} else if (flags & 4) {
			dy = SCROLL_DELTA;
		}

		vm_timer_t timer = vm_timer_create();
		SDL_Point cur = { 0, 0 };
		int start_y = 0;
		for (int frame = 1; cur.x != end.x || cur.y != end.y; frame++) {
			cur.x = min(end.x, cur.x + dx);
			cur.y = min(end.y, start_y + dy * frame);
			scroll_tick(cur.x, cur.y, &timer);
		}
		vm_timer_tick(&timer, 250);
		start_y = cur.y;
		for (int frame = 1; cur.x || cur.y; frame++) {
			cur.x = max(0, cur.x - dx);
			cur.y = max(0, start_y - dy * frame);
			scroll_tick(cur.x, cur.y, &timer);
		}
	}
}

/*
 * Screen quake effect.
 */
static void kakyuusei_quake(struct param_list *params)
{
	unsigned param1 = vm_expr_param(params, 1);
	unsigned param2 = vm_expr_param(params, 2);
	unsigned nr_quakes = param1 & 0xf;
	unsigned quake_t = ((param1 >> 4) & 0xf) + 1;
	unsigned flags = param2 & 0xf;
	unsigned quake_size = ((param2 >> 4) & 0xf) + 1;

	if (quake_size > 9)
		quake_size *= 2;
	quake_t = min(120, quake_t * 30);

	vm_timer_t timer = vm_timer_create();
	struct gfx_surface *s = &gfx.surface[0];
	s->scaled = true;
	for (unsigned i = 0; i < nr_quakes; i++) {
		if (i & 1) {
			if (flags & 1)
				s->dst.x = -quake_size;
			if (flags & 2)
				s->dst.y = -quake_size;
		} else {
			if (flags & 1)
				s->dst.x = quake_size;
			if (flags & 2)
				s->dst.y = quake_size;
		}
		gfx_screen_dirty();
		gfx_update();
		vm_peek();
		vm_timer_tick(&timer, quake_t);
	}
	s->scaled = false;
	s->dst.x = 0;
	s->dst.y = 0;
	gfx_screen_dirty();
	gfx_update();
	vm_timer_tick(&timer, quake_t);
}

static bool mahoko_spin_active = false;
static int mahoko_spin_y = 0;
static int mahoko_spin_speed = 1;
vm_timer_t mahoko_spin_timer = 0;
static bool mahoko_spin_msgbox_visible = true;
enum { WAITING_NONE, WAITING_ACTIVATE, WAITING_CANCEL } mahoko_spin_wait = WAITING_NONE;

/*
 * Spinning background effect used in the Mahoko amusement park date.
 * Runs asynchronously.
 */
static void mahoko_spin_tick(void)
{
	if (!vm_timer_tick_async(&mahoko_spin_timer, 50))
		return;

	// spinning bg left/right
	if (mahoko_spin_y <= 1400) {
		gfx_copy(0, mahoko_spin_y, 144, 400, 9, 0, 0, 0);
		gfx_copy(487, mahoko_spin_y, 153, 400, 9, 487, 0, 0);
	} else {
		int rem_h = 1800 - mahoko_spin_y;
		int loop_h = 400 - rem_h;
		gfx_copy(0,   mahoko_spin_y, 144, rem_h,  9, 0,   0,     0);
		gfx_copy(0,   0,             144, loop_h, 9, 0,   rem_h, 0);
		gfx_copy(487, mahoko_spin_y, 153, rem_h,  9, 487, 0,     0);
		gfx_copy(487, 0,             153, loop_h, 9, 487, rem_h, 0);
	}

	// static ride left/right
	gfx_copy_masked(92, 0, 52, 400, 4, 92, 0, 0, 0);
	gfx_copy_masked(487, 0, 52, 400, 4, 487, 0, 0, 0);

	gfx_copy(127, 0, 345, 104, 7, 143, 280, 0);
	if (mahoko_spin_msgbox_visible) {
		// restore messagebox
		gfx_copy_masked(0, 120, 608, 104, 7, 16, 280, 0, 0);
	}

	// ramp up speed, starting animation at max speed
	if (mahoko_spin_speed < 65 && ++mahoko_spin_speed == 65) {
		anim_halt_all();
		anim_init_stream(1, 1);
		anim_start(1);
	}

	// increment y
	mahoko_spin_y += mahoko_spin_speed;
	if (mahoko_spin_y >= 1800) {
		mahoko_spin_y = 0;
	}

	// message box visibility is handled here
	switch (mahoko_spin_wait) {
	case WAITING_NONE:
		if (input_down(INPUT_ACTIVATE)) {
			mahoko_spin_wait = WAITING_ACTIVATE;
		} else if (input_down(INPUT_CANCEL)) {
			mahoko_spin_wait = WAITING_CANCEL;
		}
		break;
	case WAITING_ACTIVATE:
		if (!input_down(INPUT_ACTIVATE)) {
			mahoko_spin_wait = WAITING_NONE;
			mahoko_spin_msgbox_visible = true;
		}
		break;
	case WAITING_CANCEL:
		if (!input_down(INPUT_CANCEL)) {
			mahoko_spin_wait = WAITING_NONE;
			mahoko_spin_msgbox_visible = !mahoko_spin_msgbox_visible;
		}
		break;
	}
}

/*
 * Begin the spinning background effect (Mahoko amusement park date).
 */
static void kakyuusei_mahoko_spin_start(struct param_list *params)
{
	// XXX: This effect lowers the frame rate significantly, which
	//      affects the animation speed. We emulate this.
	anim_frame_t = 50;
	mahoko_spin_active = true;
	mahoko_spin_y = 234;
	mahoko_spin_msgbox_visible = true;
	mahoko_spin_wait = WAITING_NONE;
	// draw opaque portion in center
	gfx_copy(143, 0, 345, 400, 4, 143, 0, 0);
	mahoko_spin_tick();
}

/*
 * End the spinning background effect (Mahoko amusement park date).
 */
static void kakyuusei_mahoko_spin_end(struct param_list *params)
{
	mahoko_spin_active = false;
	anim_frame_t = 16;
}

/*
 * Save the active palette to the extra banks.
 */
static void kakyuusei_save_current_palette(struct param_list *params)
{
	PALETTE_LOG("Palette.util_131()");
	gfx_palette_copy(extra_palette_256, 0, 236);
	memcpy(extra_palette_16, extra_palette_256, 16 * 4);
}

/*
 * Save the player name to disk.
 */
static void kakyuusei_save_player_name(struct param_list *params)
{
	savedata_write("FLAG08", memory_raw, HEAP_OFF + 2880, 32);
}

/*
 * Blend a color into every color in the palette.
 */
static void kakyuusei_palette_blend_color(struct param_list *params)
{
	int start = vm_expr_param(params, 1);
	int end = vm_expr_param(params, 2) + 1;
	int r = vm_expr_param(params, 3);
	int g = vm_expr_param(params, 4);
	int b = vm_expr_param(params, 5);
	int a = vm_expr_param(params, 6);
	PALETTE_LOG("Palette.blend(%d,%d,%d,%d,%d,%d)", start, end, r, g, b, a);

	for (int i = start; i < end; i++) {
		int src_r = extra_palette_256[i*4+2];
		int src_g = extra_palette_256[i*4+1];
		int src_b = extra_palette_256[i*4+0];
		extra_palette_256[i*4+2] = (src_r * a + (100 - a) * r) / 100;
		extra_palette_256[i*4+1] = (src_g * a + (100 - a) * g) / 100;
		extra_palette_256[i*4+0] = (src_b * a + (100 - a) * b) / 100;
	}
}

/*
 * Unskippable wait.
 */
static void kakyuusei_force_wait(struct param_list *params)
{
	vm_timer_t timer = vm_timer_create();
	vm_timer_t target_t = timer + vm_expr_param(params, 1);
	while (timer < target_t) {
		vm_peek();
		vm_timer_tick(&timer, min(target_t - timer, 15));
	}
}

/*
 * Blit an indexed texture to an RGBA surface (with background mask).
 */
static void blit_indexed_to_direct(SDL_Surface *src, SDL_Rect *src_r, SDL_Surface *dst,
		SDL_Rect *dst_r, int mask)
{
	assert(src->format->format == SDL_PIXELFORMAT_INDEX8);
	assert(dst->format->format == SDL_PIXELFORMAT_RGBA32);

	SDL_Rect _src_r = { 0, 0, src->w, src->h };
	SDL_Rect _dst_r = { 0, 0, dst->w, dst->h };
	if (!src_r)
		src_r = &_src_r;
	if (!dst_r)
		dst_r = &_dst_r;

	for (int row = 0; row < dst_r->h; row++) {
		if (dst_r->y + row >= dst->h)
			break;
		if (src_r->y + row >= src->h)
			break;
		uint8_t *dst_p = dst->pixels + (dst_r->y + row) * dst->pitch + dst_r->x * 4;
		uint8_t *src_p = src->pixels + (src_r->y + row) * src->pitch + src_r->x;
		for (int col = 0; col < dst_r->w; col++, src_p++, dst_p += 4) {
			if (*src_p == mask) {
				dst_p[0] = 0;
				dst_p[1] = 0;
				dst_p[2] = 0;
				dst_p[3] = 0;
			} else {
				SDL_Color *c = &gfx.palette[*src_p];
				dst_p[0] = c->r;
				dst_p[1] = c->g;
				dst_p[2] = c->b;
				dst_p[3] = 255;
			}
		}
	}
}

/*
 * Convert credits texture to RGBA.
 */
static SDL_Texture *get_credits_texture(void)
{
	SDL_Surface *src = gfx_get_surface(1);
	SDL_Surface *dst;
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, dst, 0, 320, 864 * 4,
			32, SDL_PIXELFORMAT_RGBA32);

	for (int col = 0; col < 4; col++) {
		SDL_Rect src_r = { 320 * col, 0, 320, 864 };
		SDL_Rect dst_r = { 0, 864 * col, 320, 864 };
		blit_indexed_to_direct(src, &src_r, dst, &dst_r, 0);
	}

	SDL_Texture *t;
	SDL_CTOR(SDL_CreateTexture, t, gfx.renderer, SDL_PIXELFORMAT_RGBA32,
			SDL_TEXTUREACCESS_STATIC, 320, 864 * 4);
	SDL_CALL(SDL_SetTextureBlendMode, t, SDL_BLENDMODE_BLEND);
	SDL_CALL(SDL_UpdateTexture, t, NULL, dst->pixels, dst->pitch);
	return t;
}

/*
 * Convert background texture to RGBA.
 */
static SDL_Texture *get_bg_texture(void)
{
	SDL_Surface *src = gfx_get_surface(2);
	SDL_Surface *dst;
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, dst, 0, 640, 400,
			32, SDL_PIXELFORMAT_RGBA32);
	blit_indexed_to_direct(src, NULL, dst, NULL, -1);

	SDL_Texture *t;
	SDL_CTOR(SDL_CreateTexture, t, gfx.renderer, SDL_PIXELFORMAT_RGBA32,
			SDL_TEXTUREACCESS_STATIC, 640, 400);
	SDL_CALL(SDL_UpdateTexture, t, NULL, dst->pixels, dst->pitch);
	return t;
}

/*
 * Ending credits roll animation. The credits text fades in/out at the bottom/top of screen.
 */
static void kakyuusei_ending(struct param_list *params)
{
	// XXX: we do this with RGBA textures to simplify blending
	SDL_Texture *bg = get_bg_texture();
	SDL_Texture *credits = get_credits_texture();

	int src_top_y = 0;
	int dst_top_y = 399;
	vm_timer_t timer = vm_timer_create();
	while (src_top_y < 2872) {
		SDL_CALL(SDL_RenderClear, gfx.renderer);
		SDL_CALL(SDL_RenderCopy, gfx.renderer, bg, NULL, NULL);

		// top fade
		for (int dst_y = dst_top_y; dst_y < 64; dst_y++) {
			SDL_Rect src_r = { 0, src_top_y + (dst_y - dst_top_y), 320, 1 };
			SDL_Rect dst_r = { 0, dst_y, 320, 1 };
			SDL_CALL(SDL_SetTextureAlphaMod, credits, dst_y * 4);
			SDL_CALL(SDL_RenderCopy, gfx.renderer, credits, &src_r, &dst_r);
		}
		// solid portion
		if (dst_top_y < 337) {
			int src_y = src_top_y + max(0, 64 - dst_top_y);
			int dst_y = max(64, dst_top_y);
			int h = 337 - dst_y;
			SDL_Rect src_r = { 0, src_y, 320, h };
			SDL_Rect dst_r = { 0, dst_y, 320, h };
			SDL_CALL(SDL_SetTextureAlphaMod, credits, 255);
			SDL_CALL(SDL_RenderCopy, gfx.renderer, credits, &src_r, &dst_r);
		}
		// bottom fade
		for (int dst_y = max(dst_top_y, 337); dst_y < 400; dst_y++) {
			int src_y = src_top_y + (dst_y - dst_top_y);
			SDL_Rect src_r = { 0, src_y, 320, 1 };
			SDL_Rect dst_r = { 0, dst_y, 320, 1 };
			SDL_CALL(SDL_SetTextureAlphaMod, credits, (400 - dst_y) * 4);
			SDL_CALL(SDL_RenderCopy, gfx.renderer, credits, &src_r, &dst_r);
		}

		SDL_RenderPresent(gfx.renderer);
		vm_peek();
		vm_timer_tick(&timer, 60);
		if (dst_top_y > 0) {
			dst_top_y--;
		} else {
			src_top_y++;
		}
	}

	SDL_DestroyTexture(bg);
	SDL_DestroyTexture(credits);

	// copy final frame to screen surface
	gfx_copy_masked(320 * 3, 279, 320, 400, 1, 0, 0, 0, 0);
}

static void kakyuusei_bgm_is_playing(struct param_list *params)
{
	mem_set_var16(18, audio_is_playing(AUDIO_CH_BGM));
}

static uint32_t kakyuusei_clock = 0;

/*
 * Start the timer (for music mode).
 */
static void kakyuusei_clock_start(struct param_list *params)
{
	kakyuusei_clock = vm_get_ticks();
}

/*
 * Get the minutes/seconds elapsed since calling kakyuusei_clock_start.
 */
static void kakyuusei_clock_get(struct param_list *params)
{
	uint32_t diff = vm_get_ticks() - kakyuusei_clock;
	unsigned minutes = diff / 60000;
	unsigned seconds = (diff % 60000) / 1000;
	if (minutes > 99) {
		minutes = 99;
		seconds = 59;
	}
	mem_set_var4(18, minutes / 10);
	mem_set_var4(19, minutes % 10);
	mem_set_var4(20, seconds / 10);
	mem_set_var4(21, seconds % 10);
}

static void kakyuusei_init(void)
{
	text_shadow = true;
}

static void kakyuusei_update(void)
{
	if (mahoko_spin_active)
		mahoko_spin_tick();
}

struct game game_kakyuusei = {
	.id = GAME_KAKYUUSEI,
	.surface_sizes = {
		[0]  = {  640,  400 },
		[1]  = { 2688,  864 },
		[2]  = {  640,  400 },
		[3]  = {  640,  400 },
		[4]  = {  640,  400 },
		[5]  = {  640,  400 },
		[6]  = {  640,  400 },
		[7]  = {  640,  896 },
		[8]  = {  640,  512 },
		[9]  = {  988, 1800 },
		[10] = {    0,    0 },
	},
	.bpp = 8,
	.var4_size = VAR4_SIZE,
	.mem16_size = MEM16_SIZE,
	.mem_init = kakyuusei_mem_init,
	.mem_restore = kakyuusei_mem_restore,
	.init = kakyuusei_init,
	.update = kakyuusei_update,
	.after_anim_draw = kakyuusei_after_anim_draw,
	.unprefixed_zen = vm_stmt_txt_no_log,
	.unprefixed_han = vm_stmt_str_no_log,
	.expr_op = {
		DEFAULT_EXPR_OP,
		[0xe5] = vm_expr_rand_with_imm_range,
	},
	.stmt_op = {
		DEFAULT_STMT_OP,
		[0x01] = vm_stmt_txt_old_log,
		[0x02] = vm_stmt_str_no_log,
		[0x0b] = vm_stmt_sys_old_log,
		[0x0f] = vm_stmt_call_old_log,
		[0x13] = kakyuusei_menu_exec,
	},
	.sys = {
		[0] = &sys_set_font_size,
		[1] = &sys_display_number,
		[2] = &kakyuusei_cursor,
		[3] = &kakyuusei_anim,
		[4] = &kakyuusei_savedata,
		[5] = &kakyuusei_audio,
		[6] = &kakyuusei_voice,
		[7] = &sys_load_file,
		[8] = &kakyuusei_load_image,
		[9] = &kakyuusei_palette,
		[10] = &kakyuusei_graphics,
		[11] = &kakyuusei_wait,
		[12] = &kakyuusei_set_text_colors,
		[13] = &sys_farcall,
		[14] = &sys_get_cursor_segment,
		[15] = &sys_menu_get_no,
		[16] = &sys_get_time,
		[17] = &kakyuusei_map,
		[18] = &kakyuusei_backlog,
	},
	.util = {
		[94] = kakyuusei_save_palette,
		[95] = kakyuusei_restore_palette,
		[96] = kakyuusei_reset_low_palette,
		[97] = kakyuusei_ctrl_is_down,
		[99] = kakyuusei_activate_is_down,
		[100] = kakyuusei_wait_until_activate_is_up,
		[101] = kakyuusei_timer_init,
		[102] = kakyuusei_timer_set,
		[103] = kakyuusei_move_tick,
		[105] = kakyuusei_crossfade_sepia,
		[106] = kakyuusei_crossfade_high_palette,
		[107] = kakyuusei_load_player_name,
		[111] = kakyuusei_backlog_add_number,
		[127] = kakyuusei_scroll,
		[128] = kakyuusei_quake,
		[129] = kakyuusei_mahoko_spin_start,
		[130] = kakyuusei_mahoko_spin_end,
		[131] = kakyuusei_save_current_palette,
		[133] = kakyuusei_save_player_name,
		[134] = util_warn_unimplemented, // minatsu events, backlog related
		[135] = util_warn_unimplemented, // minatsu events, backlog related
		[136] = kakyuusei_palette_blend_color,
		[137] = kakyuusei_force_wait,
		[138] = kakyuusei_ending,
		[139] = kakyuusei_timer_wait,
		[140] = kakyuusei_bgm_is_playing,
		[141] = kakyuusei_clock_start,
		[142] = kakyuusei_clock_get,
		[143] = util_warn_unimplemented,
		[144] = kakyuusei_activate_is_down,
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
