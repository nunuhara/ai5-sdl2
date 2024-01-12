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

#include <string.h>

#include "nulib.h"
#include "ai5/game.h"
#include "ai5/anim.h"

#include "anim.h"
#include "asset.h"
#include "audio.h"
#include "cursor.h"
#include "game.h"
#include "gfx.h"
#include "input.h"
#include "savedata.h"
#include "vm_private.h"

#define MAX_UTILS 256

typedef void(*util_fn)(struct param_list*);

void util_warn_unimplemented(struct param_list *params)
{
	WARNING("Util.function[%d] not implemented", params->params[0].val);
}

void util_noop(struct param_list *params)
{
}

void util_get_text_colors(struct param_list *params)
{
	uint32_t bg, fg;
	gfx_text_get_colors(&bg, &fg);
	mem_set_var32(18, ((bg & 0xf) << 4) | (fg & 0xf));
}

void util_blink_fade(struct param_list *params)
{
	gfx_blink_fade(64, 0, 512, 288, 0);
}

void util_scale_h(struct param_list *params)
{
	union {
		uint16_t u;
		int16_t i;
	} cast = {
		.u = vm_expr_param(params, 1)
	};

	gfx_scale_h(gfx_current_surface(), cast.i);
}

void util_invert_colors(struct param_list *params)
{
	int x = vm_expr_param(params, 1);
	int y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - x) + 1;
	int h = (vm_expr_param(params, 4) - y) + 1;
	gfx_invert_colors(x, y, w, h, 0);
}

void util_fade(struct param_list *params)
{
	int x = vm_expr_param(params, 1);
	int y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - x) + 1;
	int h = (vm_expr_param(params, 4) - y) + 1;
	unsigned dst_i = vm_expr_param(params, 5);
	bool down = vm_expr_param(params, 6) == 1;
	int src_i = vm_expr_param(params, 7) == 0 ? -1 : 2;

	if (down)
		gfx_fade_down(x * game->x_mult, y, w * game->x_mult, h, dst_i, src_i);
	else
		gfx_fade_right(x * game->x_mult, y, w * game->x_mult, h, dst_i, src_i);
}

void util_savedata_stash_name(struct param_list *params)
{
	savedata_stash_name();
}

void util_pixelate(struct param_list *params)
{
	int x = vm_expr_param(params, 1);
	int y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - x) + 1;
	int h = (vm_expr_param(params, 4) - y) + 1;
	unsigned dst_i = vm_expr_param(params, 5);
	unsigned mag = vm_expr_param(params, 6);

	gfx_pixelate(x * game->x_mult, y, w * game->x_mult, h, dst_i, mag);
}

void util_get_time(struct param_list *params)
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
void util_check_cursor(struct param_list *params)
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

void util_delay(struct param_list *params)
{
	unsigned nr_ticks = vm_expr_param(params, 1);
	vm_timer_t timer = vm_timer_create();
	for (unsigned i = 0; i < nr_ticks; i++) {
		vm_peek();
		vm_timer_tick(&timer, 15);
	}
}

static char *saved_cg_name = NULL;
static char *saved_data_name = NULL;

static bool saved_anim_running[10] = {0};

void util_save_animation(struct param_list *params)
{
	free(saved_cg_name);
	free(saved_data_name);
	saved_cg_name = asset_cg_name ? xstrdup(asset_cg_name) : NULL;
	saved_data_name = asset_data_name ? xstrdup(asset_data_name) : NULL;
}

void util_restore_animation(struct param_list *params)
{
	if (!saved_cg_name || !saved_data_name)
		VM_ERROR("No saved animation in Util.restore_animation");
	vm_load_image(saved_cg_name, 1);
	vm_read_file(saved_data_name, mem_get_sysvar32(mes_sysvar32_data_offset));
	for (int i = 0; i < 10; i++) {
		if (saved_anim_running[i]) {
			anim_init_stream(i, i);
			anim_start(i);
		}
	}
}

void util_anim_save_running(struct param_list *params)
{
	bool running = false;
	for (int i = 0; i < 10; i++) {
		saved_anim_running[i] = anim_stream_running(i);
		running |= saved_anim_running[i];
	}
	mem_set_var16(18, running);
}

void util_copy_progressive(struct param_list *params)
{
	unsigned dst_i = vm_expr_param(params, 1);
	gfx_copy_progressive(64, 0, 512, 288, 2, 64, 0, dst_i);
}

void util_fade_progressive(struct param_list *params)
{
	unsigned dst_i = vm_expr_param(params, 1);
	gfx_fade_progressive(64, 0, 512, 288, dst_i);
}

void util_anim_running(struct param_list *params)
{
	mem_set_var16(18, anim_running());
}

void util_copy(struct param_list *params)
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

void util_bgm_play(struct param_list *params)
{
	audio_bgm_play(vm_string_param(params, 1), false);
}

void util_bgm_is_playing(struct param_list *params)
{
	mem_set_var16(18, audio_bgm_is_playing());
}

void util_se_is_playing(struct param_list *params)
{
	mem_set_var16(18, audio_se_is_playing());
}

void util_get_ticks(struct param_list *params)
{
	mem_set_var32(16, vm_get_ticks());
}

void util_wait_until(struct param_list *params)
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

void util_wait_until2(struct param_list *params)
{
	uint32_t stop_t = vm_expr_param(params, 1);
	vm_timer_t t = vm_timer_create();
	while (t < stop_t) {
		vm_peek();
		vm_timer_tick(&t, 16);
	}
}

void util_bgm_is_fading(struct param_list *params)
{
	mem_set_var32(13, audio_bgm_is_fading());
}
