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

#include <time.h>

#include "nulib.h"
#include "nulib/little_endian.h"
#include "ai5.h"
#include "ai5/arc.h"
#include "ai5/cg.h"

#include "anim.h"
#include "asset.h"
#include "audio.h"
#include "cursor.h"
#include "debug.h"
#include "game.h"
#include "gfx.h"
#include "input.h"
#include "menu.h"
#include "savedata.h"
#include "sys.h"
#include "texthook.h"
#include "vm_private.h"

/*
 * Common System function definitions.
 *
 * Functions which take a command parameter should generally not be
 * implemented here, as there tends to be subtle differences in the
 * commands between games.
 */

// Helper for System.SaveData functions.
const char *_sys_save_name(unsigned save_no)
{
	static char save_name[7];
	if (save_no > 99)
		VM_ERROR("Invalid save number: %u", save_no);
	sprintf(save_name, "FLAG%02u", save_no);
	return save_name;

}

const char *sys_save_name(struct param_list *params)
{
	return _sys_save_name(vm_expr_param(params, 1));
}

void util_warn_unimplemented(struct param_list *params)
{
	WARNING("Util.function[%d] not implemented", params->params[0].val);
}

void util_noop(struct param_list *params)
{
}

void sys_set_font_size(struct param_list *params)
{
	gfx_text_set_size(mem_get_sysvar16(mes_sysvar16_font_height),
			mem_get_sysvar16(mes_sysvar16_font_weight));
}

static uint8_t *write_digit(uint8_t *buf, int n, bool halfwidth)
{
	if (halfwidth) {
		buf[0] = '0' + n;
		return buf + 1;
	}
	buf[0] = 0x82;
	buf[1] = 0x4f + n;
	return buf + 2;
}

#define MAX_DIGITS 10
void sys_display_number(struct param_list *params)
{
	uint16_t flags = mem_get_sysvar16(mes_sysvar16_display_number_flags);
	unsigned display_digits = min(flags & 0xff, MAX_DIGITS);
	bool halfwidth = flags & 0x100;

	uint32_t n = vm_expr_param(params, 0);

	// write digits to array
	uint8_t nr_digits = 0;
	uint8_t digits[MAX_DIGITS];
	for (int i = MAX_DIGITS - 1; n && i >= 0; i--, n /= 10, nr_digits++) {
		digits[i] = n % 10;
	}
	for (int i = MAX_DIGITS - (nr_digits + 1); i >= 0; i--) {
		digits[i] = 0;
	}

	if (!display_digits)
		display_digits = nr_digits;

	// write digits to string
	uint8_t buf[MAX_DIGITS * 2 + 1];
	if (!display_digits) {
		uint8_t *end = write_digit(buf, 0, halfwidth);
		*end = 0;
	} else {
		uint8_t *digit_start = digits + (MAX_DIGITS - display_digits);
		uint8_t *buf_p = buf;
		for (int i = 0; i < display_digits; i++) {
			buf_p = write_digit(buf_p, digit_start[i], halfwidth);
		}
		*buf_p = 0;
	}

	if (flags & 0x400) {
		mem_set_var32(18, strlen((char*)buf));
		return;
	}

	// draw number
	if (game->draw_text_zen)
		game->draw_text_zen((char*)buf);
	else
		vm_draw_text((char*)buf);
}

void sys_cursor_save_pos(struct param_list *params)
{
	unsigned x, y;
	cursor_get_pos(&x, &y);
	mem_set_sysvar16(mes_sysvar16_cursor_x, x);
	mem_set_sysvar16(mes_sysvar16_cursor_y, y);
}

void sys_file(struct param_list *params)
{
	// TODO: on older games there is no File.write, and this call doesn't take
	//       a cmd parameter
	switch (vm_expr_param(params, 0)) {
	case 0:  vm_load_data_file(vm_string_param(params, 1), vm_expr_param(params, 2)); break;
	case 1:  // TODO: File.write
	default: VM_ERROR("System.File.function[%d] not implemented", params->params[0].val);
	}
}

void sys_load_file(struct param_list *params)
{
	vm_load_data_file(vm_string_param(params, 0), vm_expr_param(params, 1));
}

void _sys_load_image(const char *name, unsigned i, unsigned x_mult)
{
	struct archive_data *data = _asset_cg_load(name);
	if (!data) {
		WARNING("Failed to load CG \"%s\"", name);
		return;
	}

	// copy CG data into file_data
	uint32_t off = mem_get_sysvar32(mes_sysvar32_cg_offset);
	if (off + data->size > MEMORY_FILE_DATA_SIZE)
		VM_ERROR("CG data would exceed buffer size");
	vm_load_file(data, off);

	// decode CG
	struct cg *cg = asset_cg_decode(data);
	archive_data_release(data);
	if (!cg) {
		WARNING("Failed to decode CG \"%s\"", name);
		return;
	}

	// draw CG
	if (!vm_flag_is_on(FLAG_PALETTE_ONLY)) {
		mem_set_sysvar16(mes_sysvar16_cg_x, cg->metrics.x / x_mult);
		mem_set_sysvar16(mes_sysvar16_cg_y, cg->metrics.y);
		mem_set_sysvar16(mes_sysvar16_cg_w, cg->metrics.w / x_mult);
		mem_set_sysvar16(mes_sysvar16_cg_h, cg->metrics.h);
		gfx_draw_cg(i, cg);
	}

	// load palette
	if (cg->palette && vm_flag_is_on(FLAG_LOAD_PALETTE)) {
		memcpy(memory.palette, cg->palette, 256 * 4);
	}
	cg_free(cg);
}

void sys_load_image(struct param_list *params)
{
	anim_halt_all();
	_sys_load_image(vm_string_param(params, 0), mem_get_sysvar16(mes_sysvar16_dst_surface), 1);
}

void sys_graphics_copy(struct param_list *params)
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
	gfx_copy(src_x, src_y, src_w, src_h, src_i, dst_x, dst_y, dst_i);
}

void sys_graphics_copy_masked(struct param_list *params)
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
	gfx_copy_masked(src_x, src_y, src_w, src_h, src_i, dst_x, dst_y, dst_i,
			mem_get_sysvar16(mes_sysvar16_mask_color));
}

void sys_graphics_copy_masked24(struct param_list *params)
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
	gfx_copy_masked(src_x, src_y, src_w, src_h, src_i, dst_x, dst_y, dst_i,
			mem_get_sysvar32(mes_sysvar32_mask_color));
}

void sys_graphics_fill_bg(struct param_list *params)
{
	// System.Graphics.fill_bg(x, y, br_x, br_y)
	int x = vm_expr_param(params, 1);
	int y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - x) + 1;
	int h = (vm_expr_param(params, 4) - y) + 1;
	gfx_text_fill(x, y, w, h, mem_get_sysvar16(mes_sysvar16_dst_surface));
}

void sys_graphics_copy_swap(struct param_list *params)
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
	gfx_copy_swap(src_x, src_y, src_w, src_h, src_i, dst_x, dst_y, dst_i);
}

void sys_graphics_swap_bg_fg(struct param_list *params)
{
	// System.Graphics.swap_bg_fg(x, y, br_x, br_y)
	int x = vm_expr_param(params, 1);
	int y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - x) + 1;
	int h = (vm_expr_param(params, 4) - y) + 1;
	gfx_text_swap_colors(x, y, w, h, mem_get_sysvar16(mes_sysvar16_dst_surface));
}

void sys_graphics_compose(struct param_list *params)
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
	gfx_compose(fg_x, fg_y, w, h, fg_i, bg_x, bg_y, bg_i, dst_x, dst_y, dst_i,
			mem_get_sysvar16(mes_sysvar16_mask_color));
}

void sys_graphics_blend(struct param_list *params)
{
	// System.Graphics.blend(src_x, src_y, src_br_x, src_br_y, src_i, dst_x, dst_y, dst_i)
	int src_x = vm_expr_param(params, 1);
	int src_y = vm_expr_param(params, 2);
	int src_w = (vm_expr_param(params, 3) - src_x) + 1;
	int src_h = (vm_expr_param(params, 4) - src_y) + 1;
	unsigned src_i = vm_expr_param(params, 5);
	int dst_x = vm_expr_param(params, 6);
	int dst_y = vm_expr_param(params, 7);
	unsigned dst_i = vm_expr_param(params, 8);
	unsigned alpha = vm_expr_param(params, 9);
	if (alpha < 1)
		return;
	if (alpha > 15)
		gfx_copy(src_x, src_y, src_w, src_h, src_i, dst_x, dst_y, dst_i);
	else
		gfx_blend(src_x, src_y, src_w, src_h, src_i, dst_x, dst_y, dst_i, alpha * 16 - 8);
}

void sys_graphics_blend_masked(struct param_list *params)
{
	// System.Graphics.blend(src_x, src_y, src_br_x, src_br_y, src_i, dst_x, dst_y, dst_i)
	int src_x = vm_expr_param(params, 1);
	int src_y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - src_x) + 1;
	int h = (vm_expr_param(params, 4) - src_y) + 1;
	unsigned src_i = vm_expr_param(params, 5);
	int dst_x = vm_expr_param(params, 6);
	int dst_y = vm_expr_param(params, 7);
	unsigned dst_i = vm_expr_param(params, 8);
	uint8_t *mask = memory_raw + vm_expr_param(params, 9) + 4;
	if (!mem_ptr_valid(mask, w * h))
		VM_ERROR("Invalid mask pointer");

	gfx_blend_masked(src_x, src_y, w, h, src_i, dst_x, dst_y, dst_i, mask);
}

void sys_graphics_invert_colors(struct param_list *params)
{
	// System.Grahpics.invert_colors(x, y, br_x, br_y)
	int x = vm_expr_param(params, 1);
	int y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - x) + 1;
	int h = (vm_expr_param(params, 4) - y) + 1;
	unsigned i = mem_get_sysvar16(mes_sysvar16_dst_surface);
	gfx_invert_colors(x, y, w, h, i);
}

void sys_graphics_copy_progressive(struct param_list *params)
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
	gfx_copy_progressive(src_x, src_y, src_w, src_h, src_i, dst_x, dst_y, dst_i);
}

void sys_graphics_pixel_crossfade(struct param_list *params)
{
	// System.Grahpics.pixel_crossfade(src_x, src_y, src_br_x, src_br_y, src_i, dst_x, dst_y, dst_i)
	int src_x = vm_expr_param(params, 1);
	int src_y = vm_expr_param(params, 2);
	int src_w = (vm_expr_param(params, 3) - src_x) + 1;
	int src_h = (vm_expr_param(params, 4) - src_y) + 1;
	unsigned src_i = vm_expr_param(params, 5);
	int dst_x = vm_expr_param(params, 6);
	int dst_y = vm_expr_param(params, 7);
	unsigned dst_i = vm_expr_param(params, 8);
	gfx_pixel_crossfade(src_x, src_y, src_w, src_h, src_i, dst_x, dst_y, dst_i);
}

void sys_graphics_pixel_crossfade_masked(struct param_list *params)
{
	// System.Grahpics.pixel_crossfade_masked(src_x, src_y, src_br_x, src_br_y, src_i, dst_x, dst_y, dst_i)
	int src_x = vm_expr_param(params, 1);
	int src_y = vm_expr_param(params, 2);
	int src_w = (vm_expr_param(params, 3) - src_x) + 1;
	int src_h = (vm_expr_param(params, 4) - src_y) + 1;
	unsigned src_i = vm_expr_param(params, 5);
	int dst_x = vm_expr_param(params, 6);
	int dst_y = vm_expr_param(params, 7);
	unsigned dst_i = vm_expr_param(params, 8);
	gfx_pixel_crossfade_masked(src_x, src_y, src_w, src_h, src_i, dst_x, dst_y, dst_i,
			mem_get_sysvar16(mes_sysvar16_mask_color));
}
void sys_wait(struct param_list *params)
{
	texthook_commit();
	if (params->nr_params == 0 || vm_expr_param(params, 0) == 0) {
		while (true) {
			if (input_down(INPUT_CTRL)) {
				vm_peek();
				vm_delay(config.msg_skip_delay);
				return;
			}
			if (input_down(INPUT_ACTIVATE)) {
				input_wait_until_up(INPUT_ACTIVATE);
				return;
			}
			vm_peek();
			vm_delay(16);
		}
	} else {
		vm_timer_t timer = vm_timer_create();
		vm_timer_t target_t = timer + (params->params[0].val / 4) * 15;
		while (timer < target_t && !input_down(INPUT_SHIFT)) {
			vm_peek();
			vm_timer_tick(&timer, min(target_t - timer, 15));
		}
	}
}

void sys_set_text_colors_indexed(struct param_list *params)
{
	uint32_t colors = vm_expr_param(params, 0);
	gfx_text_set_colors((colors >> 4) & 0xf, colors & 0xf);
}

void sys_set_text_colors_direct(struct param_list *params)
{
	gfx_text_set_colors(vm_expr_param(params, 0), vm_expr_param(params, 1));
}

static bool farcall_addr_valid(uint32_t addr)
{
	return addr < sizeof(struct memory);
}

void sys_farcall(struct param_list *params)
{
	uint32_t addr = vm_expr_param(params, 0);
	if (unlikely(!farcall_addr_valid(addr)))
		VM_ERROR("Tried to farcall to invalid address");

	struct vm_pointer saved_ip = vm.ip;
	vm.ip.ptr = 0;
	vm.ip.code = memory_raw + addr;
	vm_exec();
	vm.ip = saved_ip;
}

/*
 * This is essentially an array lookup based on cursor position.
 * It reads an array of the following structures:
 *
 *     struct a6_entry {
 *         unsigned id;
 *         struct { unsigned x, y; } top_left;
 *         struct { unsigned x, y; } bot_right;
 *     };
 *
 * If the cursor position is between `top_left` and `bot_right`, then `id` is returned.
 * If no match is found, then 0xFFFF is returned.
 */
void _sys_get_cursor_segment(unsigned x, unsigned y, uint32_t off)
{
	if (x >= gfx_view.w || y >= gfx_view.h) {
		WARNING("Invalid argument to System.get_cursor_segment: (%u,%u)", x, y);
		return;
	}

	uint8_t *a = memory.file_data + off;
	while (a < memory.file_data + MEMORY_FILE_DATA_SIZE - 10) {
		uint16_t id = le_get16(a, 0);
		if (id == 0xffff) {
			mem_set_var16(18, 0xffff);
			return;
		}
		uint16_t x_left = le_get16(a, 2);
		uint16_t y_top = le_get16(a, 4);
		uint16_t x_right = le_get16(a, 6);
		uint16_t y_bot = le_get16(a, 8);
		if (x >= x_left && x <= x_right && y >= y_top && y <= y_bot) {
			mem_set_var16(18, id);
			return;
		}

		a += 10;
	}
	WARNING("Read past end of buffer in System.check_cursor_pos");
	mem_set_var16(18, 0);

}

void sys_get_cursor_segment(struct param_list *params)
{
	_sys_get_cursor_segment(vm_expr_param(params, 0), vm_expr_param(params, 1),
			mem_get_sysvar32(mes_sysvar32_a6_offset));
}

void sys_menu_get_no(struct param_list *params)
{
	menu_get_no(vm_expr_param(params, 0));
}

void sys_check_input(struct param_list *params)
{
	unsigned input = vm_expr_param(params, 0);
	bool value = vm_expr_param(params, 1);
	if (input >= INPUT_NR_INPUTS) {
		WARNING("Invalid input number: %u", input);
		mem_set_var32(18, false);
		return;
	}

	bool is_down = input_down(input);
	mem_set_var32(18, value == is_down);
}

void sys_strlen(struct param_list *params)
{
	uint32_t ptr = vm_expr_param(params, 0);
	if (unlikely(ptr >= sizeof(struct memory)))
		VM_ERROR("Invalid pointer: %u", ptr);
	uint8_t *str = memory_raw + ptr;
	mem_set_var32(18, strnlen((char*)str, sizeof(struct memory) - ptr));
}

void sys_get_time(struct param_list *params)
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	mem_set_var16(0, tm->tm_year + 1900);
	mem_set_var16(1, tm->tm_mon + 1);
	mem_set_var16(2, tm->tm_wday);
	mem_set_var16(3, tm->tm_mday);
	mem_set_var16(4, tm->tm_hour);
	mem_set_var16(5, tm->tm_min);
	mem_set_var16(6, max(tm->tm_sec, 59));
}
