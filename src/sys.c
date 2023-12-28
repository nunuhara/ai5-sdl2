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
#include "ai5/arc.h"
#include "ai5/cg.h"

#include "anim.h"
#include "asset.h"
#include "audio.h"
#include "cursor.h"
#include "game.h"
#include "gfx.h"
#include "input.h"
#include "menu.h"
#include "savedata.h"
#include "vm_private.h"

void sys_set_font_size(struct param_list *params)
{
	gfx_text_set_size(mem_get_sysvar16(mes_sysvar16_font_height));
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
	vm_draw_text((char*)buf);
}

void sys_cursor_save_pos(struct param_list *params)
{
	unsigned x, y;
	cursor_get_pos(&x, &y);
	mem_set_sysvar16(3, x);
	mem_set_sysvar16(4, y);
}

void sys_cursor(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: cursor_reload(); break;
	case 1: cursor_unload(); break;
	case 2: sys_cursor_save_pos(params); break;
	case 3: cursor_set_pos(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 4: cursor_load(vm_expr_param(params, 1)); break;
	case 5: cursor_show(); break;
	case 6: cursor_hide(); break;
	default: VM_ERROR("System.Cursor.function[%u] not implemented", params->params[0].val);
	}
}

void sys_anim(struct param_list *params)
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
	case 20: anim_set_offset(vm_expr_param(params, 1), vm_expr_param(params, 2) * game->x_mult,
				vm_expr_param(params, 3)); break;
	default: VM_ERROR("System.Anim.function[%u] not implemented", params->params[0].val);
	}
}

void sys_savedata(struct param_list *params)
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
	case 3: savedata_save(save_name); break;
	case 4: savedata_load_var4(save_name); break;
	case 5: savedata_save_var4(save_name); break;
	case 6: savedata_save_union_var4(save_name); break;
	case 7: savedata_load_var4_slice(save_name, vm_expr_param(params, 2),
				vm_expr_param(params, 3)); break;
	case 8: savedata_save_var4_slice(save_name, vm_expr_param(params, 2),
				vm_expr_param(params, 3)); break;
	case 9: {
		char save_name2[7];
		uint32_t save_no2 = vm_expr_param(params, 2);
		if (save_no2 > 99)
			VM_ERROR("Invalid save number: %u", save_no2);
		sprintf(save_name2, "FLAG%02u", save_no2);
		savedata_copy(save_name, save_name2);
		break;
	}
	case 11: savedata_f11(save_name); break;
	case 12: savedata_f12(save_name); break;
	case 13: savedata_set_mes_name(save_name, vm_string_param(params, 2)); break;
	default: VM_ERROR("System.savedata.function[%u] not implemented", params->params[0].val);
	}
}

void sys_audio(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0:  audio_bgm_play(vm_string_param(params, 1), true); break;
	case 2:  audio_bgm_stop(); break;
	case 3:  audio_se_play(vm_string_param(params, 1)); break;
	case 4:  audio_bgm_fade(vm_expr_param(params, 2), -1, vm_expr_param(params, 3), true); break;
	case 5:  audio_bgm_set_volume(vm_expr_param(params, 1)); break;
	case 7:  audio_bgm_fade(vm_expr_param(params, 2), -1, vm_expr_param(params, 3), false); break;
	case 9:  audio_bgm_fade_out(vm_expr_param(params, 1), true); break;
	case 10: audio_bgm_fade_out(vm_expr_param(params, 2), false); break;
	case 12: audio_se_stop(); break;
	case 18: audio_bgm_restore_volume(); break;
	default: VM_ERROR("System.Audio.function[%d] not implemented", params->params[0].val);
	}
}

void vm_read_file(const char *name, uint32_t offset)
{
	struct archive_data *data = asset_data_load(name);
	if (!data) {
		WARNING("Failed to read data file \"%s\"", name);
		return;
	}
	if (offset + data->size > MEMORY_FILE_DATA_SIZE) {
		WARNING("Tried to read file beyond end of buffer");
		goto end;
	}
	memcpy(memory.file_data + offset, data->data, data->size);
end:
	archive_data_release(data);
}

void sys_file(struct param_list *params)
{
	// TODO: on older games there is no File.write, and this call doesn't take
	//       a cmd parameter
	switch (vm_expr_param(params, 0)) {
	case 0:  vm_read_file(vm_string_param(params, 1), vm_expr_param(params, 2)); break;
	case 1:  // TODO: File.write
	default: VM_ERROR("System.File.function[%d] not implemented", params->params[0].val);
	}
}

void sys_load_file(struct param_list *params)
{
	vm_read_file(vm_string_param(params, 0), vm_expr_param(params, 1));
}

void vm_load_image(const char *name, unsigned i)
{
	struct archive_data *data = asset_cg_load(name);
	if (!data) {
		WARNING("Failed to load CG \"%s\"", name);
		return;
	}

	// copy CG data into file_data
	uint32_t off = mem_get_sysvar32(mes_sysvar32_cg_offset);
	if (off + data->size > MEMORY_FILE_DATA_SIZE)
		VM_ERROR("CG data would exceed buffer size");
	memcpy(memory.file_data + off, data->data, data->size);

	// decode CG
	struct cg *cg = cg_load_arcdata(data);
	archive_data_release(data);
	if (!cg) {
		WARNING("Failed to decode CG \"%s\"", name);
		return;
	}

	mem_set_sysvar16(mes_sysvar16_cg_x, cg->metrics.x / game->x_mult);
	mem_set_sysvar16(mes_sysvar16_cg_y, cg->metrics.y);
	mem_set_sysvar16(mes_sysvar16_cg_w, cg->metrics.w / game->x_mult);
	mem_set_sysvar16(mes_sysvar16_cg_h, cg->metrics.h);

	// draw CG
	gfx_draw_cg(i, cg);
	if (cg->palette && vm_flag_is_on(FLAG_LOAD_PALETTE)) {
		memcpy(memory.palette, cg->palette, 256 * 4);
	}
	cg_free(cg);
}

void sys_load_image(struct param_list *params)
{
	vm_load_image(vm_string_param(params, 0), mem_get_sysvar16(mes_sysvar16_dst_surface));
}

static void check_rgb_param(struct param_list *params, unsigned i, uint8_t *r, uint8_t *g,
		uint8_t *b)
{
	uint32_t c = vm_expr_param(params, i);
	*r = ((c >> 4) & 0xf) * 17;
	*g = ((c >> 8) & 0xf) * 17;
	*b = (c & 0xf) * 17;
}

void sys_palette_set(struct param_list *params)
{
	if (params->nr_params > 1) {
		uint8_t r, g, b;
		uint8_t pal[256*4];
		check_rgb_param(params, 1, &r, &g, &b);
		for (int i = 0; i < 256; i += 4) {
			pal[i+0] = b;
			pal[i+1] = g;
			pal[i+2] = r;
			pal[i+3] = 0;
		}
		gfx_palette_set(pal);
	} else {
		gfx_palette_set(memory.palette);
	}
}

void sys_palette_crossfade1(struct param_list *params)
{
	if (params->nr_params > 1) {
		uint8_t r, g, b;
		check_rgb_param(params, 1, &r, &g, &b);
		gfx_palette_crossfade_to(r, g, b, 240);
	} else {
		gfx_palette_crossfade(memory.palette, 240);
	}
}

void sys_palette_crossfade2(struct param_list *params)
{
	// XXX: t is a value from 0-15 corresponding to the interval [0-3600]
	//      in increments of 240
	uint32_t t = vm_expr_param(params, 1);
	if (params->nr_params > 2) {
		uint8_t r, g, b;
		check_rgb_param(params, 2, &r, &g, &b);
		gfx_palette_crossfade_to(r, g, b, (t & 0xf) * 240);
	} else {
		gfx_palette_crossfade(memory.palette, (t & 0xf) * 240);
	}
}

void sys_palette(struct param_list *params)
{
	vm_expr_param(params, 0);
	switch (params->params[0].val) {
	case 0:  sys_palette_set(params); break;
	case 1:  sys_palette_crossfade1(params); break;
	case 2:  sys_palette_crossfade2(params); break;
	case 3:  gfx_display_hide(); break;
	case 4:  gfx_display_unhide(); break;
	default: VM_ERROR("System.Palette.function[%d] not implemented",
				 params->params[0].val);
	}
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
	gfx_copy(src_x * game->x_mult, src_y, src_w * game->x_mult, src_h, src_i,
			dst_x * game->x_mult, dst_y, dst_i);
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
	gfx_copy_masked(src_x * game->x_mult, src_y, src_w * game->x_mult, src_h, src_i,
			dst_x * game->x_mult, dst_y, dst_i,
			mem_get_sysvar16(mes_sysvar16_mask_color));
}

void sys_graphics_fill_bg(struct param_list *params)
{
	// System.Graphics.fill_bg(x, y, br_x, br_y)
	int x = vm_expr_param(params, 1);
	int y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - x) + 1;
	int h = (vm_expr_param(params, 4) - y) + 1;
	gfx_text_fill(x * game->x_mult, y, w * game->x_mult, h,
			mem_get_sysvar16(mes_sysvar16_dst_surface));
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
	gfx_copy_swap(src_x * game->x_mult, src_y, src_w * game->x_mult, src_h, src_i,
			dst_x * game->x_mult, dst_y, dst_i);
}

void sys_graphics_swap_bg_fg(struct param_list *params)
{
	// System.Graphics.swap_bg_fg(x, y, br_x, br_y)
	int x = vm_expr_param(params, 1);
	int y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - x) + 1;
	int h = (vm_expr_param(params, 4) - y) + 1;
	gfx_text_swap_colors(x * game->x_mult, y, w * game->x_mult, h,
			mem_get_sysvar16(mes_sysvar16_dst_surface));
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
	gfx_compose(fg_x * game->x_mult, fg_y, w * game->x_mult, h, fg_i, bg_x * game->x_mult,
			bg_y, bg_i, dst_x * game->x_mult, dst_y, dst_i,
			mem_get_sysvar16(mes_sysvar16_mask_color));
}

void sys_graphics_invert_colors(struct param_list *params)
{
	// System.Grahpics.invert_colors(x, y, br_x, br_y)
	int x = vm_expr_param(params, 1);
	int y = vm_expr_param(params, 2);
	int w = (vm_expr_param(params, 3) - x) + 1;
	int h = (vm_expr_param(params, 4) - y) + 1;
	unsigned i = mem_get_sysvar16(mes_sysvar16_dst_surface);
	gfx_invert_colors(x * game->x_mult, y, w * game->x_mult, h, i);
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
	gfx_copy_progressive(src_x * game->x_mult, src_y, src_w * game->x_mult, src_h, src_i,
			dst_x * game->x_mult, dst_y, dst_i);
}

void sys_graphics_classics(struct param_list *params)
{
	vm_expr_param(params, 0);
	switch (params->params[0].val) {
	case 0:  sys_graphics_copy(params); break;
	case 1:  sys_graphics_copy_masked(params); break;
	case 2:  sys_graphics_fill_bg(params); break;
	case 3:  sys_graphics_copy_swap(params); break;
	case 4:  sys_graphics_swap_bg_fg(params); break;
	case 5:  sys_graphics_compose(params); break;
	case 6:  sys_graphics_invert_colors(params); break;
	case 20: sys_graphics_copy_progressive(params); break;
	default: VM_ERROR("System.Image.function[%d] not implemented",
				 params->params[0].val);
	}
}

void sys_wait(struct param_list *params)
{
	if (params->nr_params == 0 || vm_expr_param(params, 0) == 0) {
		while (true) {
			if (input_down(INPUT_CTRL)) {
				vm_peek();
				vm_delay(16);
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
		uint32_t target_t = timer + params->params[0].val * 4;
		while (timer < target_t && !input_down(INPUT_SHIFT)) {
			vm_timer_tick(&timer, 16);
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
static void get_cursor_segment(unsigned x, unsigned y, uint32_t off)
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

void sys_get_cursor_segment_classics(struct param_list *params)
{
	get_cursor_segment(vm_expr_param(params, 0), vm_expr_param(params, 1),
			vm_expr_param(params, 2));
}

void sys_get_cursor_segment(struct param_list *params)
{
	get_cursor_segment(vm_expr_param(params, 0), vm_expr_param(params, 1),
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

void sys_set_screen_surface(struct param_list *params)
{
	gfx_set_screen_surface(vm_expr_param(params, 0));
}
