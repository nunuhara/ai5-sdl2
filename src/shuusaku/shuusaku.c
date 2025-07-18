/* Copyright (C) 2025 Nunuhara Cabbage <nunuhara@haniwa.technology>
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
#include "nulib/utfsjis.h"
#include "ai5/arc.h"
#include "ai5/cg.h"
#include "ai5/mes.h"

#include "ai5.h"
#include "anim.h"
#include "asset.h"
#include "audio.h"
#include "game.h"
#include "gfx.h"
#include "gfx_private.h"
#include "input.h"
#include "memory.h"
#include "popup_menu.h"
#include "savedata.h"
#include "texthook.h"
#include "sys.h"
#include "vm.h"
#include "vm_private.h"

#include "shuusaku.h"

#define MES_NAME_SIZE 32
#define VAR4_SIZE 2500
// XXX: system variables are not saved, so not considered part of mem16
#define MEM16_SIZE 3736

#define VAR32_OFF    MES_NAME_SIZE
#define VAR16_OFF    (VAR32_OFF + 26 * 4)
#define VAR4_OFF     (VAR16_OFF + 500 * 2)
#define HEAP_OFF     (VAR4_OFF + VAR4_SIZE)
#define SYSVAR16_OFF (HEAP_OFF + 25 * 4)

// offset into memory.file_data
#define ANIM_OFFSET 0xa0000
#define ANIM_SIZE   0x10000

#define MASK_COLOR 10

static int screen_y(void)
{
	return gfx.surface[0].src.y;
}

static void set_screen_y(int y)
{
	gfx.surface[0].src.y = y;
	gfx_screen_dirty();
}

static void shuusaku_mem_restore(void)
{

}

static void shuusaku_mem_init(void)
{
	// set up pointer table for memory access
	memory_ptr.mes_name = memory_raw;
	memory_ptr.var4 = memory_raw + VAR4_OFF;
	memory_ptr.system_var16_ptr = memory_raw; // XXX: pointer doesn't exist
	memory_ptr.var16 = memory_raw + VAR16_OFF;
	memory_ptr.system_var16 = memory_raw + SYSVAR16_OFF;
	memory_ptr.var32 = memory_raw + VAR32_OFF;
	memory_ptr.system_var32 = memory_raw; // XXX: memory region doesn't exist

	mem_set_sysvar16(mes_sysvar16_text_end_x, 79);
	mem_set_sysvar16(mes_sysvar16_text_end_y, 479);
	mem_set_sysvar16(mes_sysvar16_bg_color, 0xff);
	mem_set_sysvar16(13, 32);
	mem_set_sysvar16(mes_sysvar16_line_space, 18);
	mem_set_sysvar16(60, 0xffff);
	mem_set_sysvar16(61, 0xffff);
}

static void unprefixed_error(void)
{
	VM_ERROR("Unprefixed text");
}

void shuusaku_draw_text(const char *_text)
{
	texthook_push(_text);
	const uint8_t *text = (const uint8_t*)_text;
	const uint16_t surface = mem_get_sysvar16(mes_sysvar16_dst_surface);
	const uint16_t start_x = mem_get_sysvar16(mes_sysvar16_text_start_x);
	const uint16_t end_x = start_x + mem_get_sysvar16(mes_sysvar16_text_end_x);
	const uint16_t line_space = mem_get_sysvar16(mes_sysvar16_line_space);
	uint16_t x = mem_get_sysvar16(mes_sysvar16_text_cursor_x);
	uint16_t y = mem_get_sysvar16(mes_sysvar16_text_cursor_y);

	while (*text) {
		// '％' = newline
		if (text[0] == 0x81 && text[1] == 0x93) {
			text += 2;
			x = start_x;
			y += line_space;
			continue;
		}
		int ch;
		bool zenkaku = SJIS_2BYTE(*text);
		uint16_t char_space = zenkaku ? 2 : 1;
		if (x + char_space > end_x) {
			x = start_x;
			y += line_space;
		}

		text = (const uint8_t*)sjis_char2unicode((const char*)text, &ch);
		gfx_text_draw_glyph(x * 8, y, surface, ch);
		x += char_space;
	}

	mem_set_sysvar16(mes_sysvar16_text_cursor_x, x);
	mem_set_sysvar16(mes_sysvar16_text_cursor_y, y);
}

static void stmt_txt(void)
{
	char str[VM_TXT_BUF_SIZE];
	vm_read_text_aiw(str, 0xff);
	shuusaku_draw_text(str);
}

#define PARAMS(name) \
	struct param_list name = {0}; \
	game->vm.read_params(&name);

static const char *aiw_save_name(struct param_list *params)
{
	return _sys_save_name_fmt("FLAG%u", vm_expr_param(params, 1));
}

static void stmt_load(void)
{
	PARAMS(params);
	switch (vm_expr_param(&params, 0)) {
	case 0:
		savedata_read(aiw_save_name(&params), memory_raw, 0, MEM16_SIZE);
		vm_mesjmp_aiw(mem_mes_name());
		break;
	case 2:
		savedata_read(aiw_save_name(&params), memory_raw,
				VAR4_OFF, SYSVAR16_OFF - VAR4_OFF);
		break;
	case 3:
		savedata_read(aiw_save_name(&params), memory_raw,
				VAR32_OFF, VAR16_OFF - VAR32_OFF);
		break;
	case 4:
		savedata_read(aiw_save_name(&params), memory_raw,
				VAR16_OFF + vm_expr_param(&params, 2) * 2, 2);
		break;
	default:
		VM_ERROR("Load.function[%u] not implemented", params.params[0].val);
	}
}

static void shuusaku_save_flags(const char *save_name)
{
	uint8_t save[MEM16_SIZE];
	savedata_read(save_name, save, VAR4_OFF, VAR4_SIZE + 100);

	uint8_t *var4 = save + VAR4_OFF;
	for (int i = 0; i < VAR4_SIZE; i++) {
		uint8_t mem_flags = mem_get_var4(i);
		if (i < 1350 || i >= 1400) {
			uint8_t mem_flag_hi = mem_flags & 0xf0;
			uint8_t mem_flag_lo = mem_flags & 0x0f;
			if ((var4[i] & 0xf0) < mem_flag_hi) {
				var4[i] = (var4[i] & 0x0f) | mem_flag_hi;
			}
			if ((var4[i] & 0x0f) < mem_flag_lo) {
				var4[i] = (var4[i] & 0xf0) | mem_flag_lo;
			}
		} else {
			var4[i] |= mem_flags;
		}
	}

	memcpy(save + HEAP_OFF, memory_raw + HEAP_OFF, 100);
	savedata_write(save_name, save, VAR4_OFF, VAR4_SIZE + 100);
}

static void stmt_save(void)
{
	PARAMS(params);
	uint16_t var = mem_get_var16(116);
	mem_set_var16(116, 0);
	switch (vm_expr_param(&params, 0)) {
	case 0:
		savedata_write(aiw_save_name(&params), memory_raw, 0, MEM16_SIZE);
		break;
	case 3:
		shuusaku_save_flags(aiw_save_name(&params));
		break;
	case 4:
		savedata_write(aiw_save_name(&params), memory_raw,
				VAR16_OFF + vm_expr_param(&params, 1) * 2, 2);
		break;
	default:
		VM_ERROR("Save.function[%u] not implemented", params.params[0].val);
	}
	mem_set_var16(116, var);
}

static void stmt_menuexec(void)
{
	// XXX: hack to ensure status window is updated, e.g. after adding aphrodesiac
	shuusaku_status_update();

	PARAMS(params);
	unsigned no = vm_expr_param(&params, 0);
	if (no >= AIW_MAX_MENUS)
		VM_ERROR("Invalid menu index: %u", no);
	unsigned mode = vm_expr_param(&params, 1);

	uint32_t saved_ip = vm.ip.ptr;

	unsigned nr_entries = 0;
	struct menu_entry entries[100];

	for (unsigned i = 0; i < aiw_menu_nr_entries[no]; i++) {
		if (aiw_menu_entries[no][i].cond_addr) {
			vm.ip.ptr = aiw_menu_entries[no][i].cond_addr;
			if (!game->vm.eval())
				continue;
		}
		entries[nr_entries].body_addr = aiw_menu_entries[no][i].body_addr;
		entries[nr_entries].index = i + 1;
		nr_entries++;
	}

	game->flags[FLAG_ANIM_ENABLE] = 0;
	unsigned selected = shuusaku_menuexec(entries, nr_entries, mode);
	game->flags[FLAG_ANIM_ENABLE] = FLAG_ALWAYS_ON;
	mem_set_var32(21, selected);

	vm.ip.ptr = saved_ip;
}

static void stmt_display_number(void)
{
	PARAMS(params);
	shuusaku_draw_text(_sys_number_to_string(vm_expr_param(&params, 0), 0, false));
}

static void stmt_set_text_color(void)
{
	PARAMS(params);
	uint16_t c = vm_expr_param(&params, 0);
	mem_set_sysvar16(mes_sysvar16_bg_color, c);
	gfx_text_set_colors((c >> 8) + 10, (c & 0xff) + 10);
}

static void stmt_wait(void)
{
	PARAMS(params);
	texthook_commit();
	if (params.nr_params == 0 || vm_expr_param(&params, 0) == 0) {
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
		vm_timer_t target_t = timer + (params.params[0].val) * 10;
		while (timer < target_t && !input_down(INPUT_SHIFT)) {
			vm_peek();
			vm_timer_tick(&timer, min(target_t - timer, 8));
		}
	}
}

static void stmt_text_clear(void)
{
	unsigned x = mem_get_sysvar16(mes_sysvar16_text_start_x);
	unsigned y = mem_get_sysvar16(mes_sysvar16_text_start_y);
	unsigned w = (mem_get_sysvar16(mes_sysvar16_text_end_x) - x) + 1;
	unsigned h = (mem_get_sysvar16(mes_sysvar16_text_end_y) - y) + 1;
	unsigned dst = mem_get_sysvar16(mes_sysvar16_dst_surface);
	unsigned c = (mem_get_sysvar16(mes_sysvar16_bg_color) >> 8) + 10;
	gfx_fill(x * 8, y, w * 8, h, dst, c);

	mem_set_sysvar16(mes_sysvar16_text_cursor_x, x);
	mem_set_sysvar16(mes_sysvar16_text_cursor_y, y);
}

static void clear_message_window(void)
{
	gfx_copy(0, 80, 640, 74, 4, 0, screen_y() + 371, 0);
	mem_set_sysvar16(mes_sysvar16_text_cursor_x,
			mem_get_sysvar16(mes_sysvar16_text_start_x));
	mem_set_sysvar16(mes_sysvar16_text_cursor_y,
			mem_get_sysvar16(mes_sysvar16_text_start_y));
}

static void toggle_message_window(void)
{
	gfx_copy_swap(0, 160, 640, 74, 4, 0, screen_y() + 371, 0);
}

static void stmt_commit_message(void)
{
	texthook_commit();
	bool hidden = false;
	while (true) {
		if (!hidden) {
			// clear on click/ctrl
			if (input_down(INPUT_ACTIVATE)) {
				input_wait_until_up(INPUT_ACTIVATE);
				clear_message_window();
				break;
			}
			if (input_down(INPUT_CTRL)) {
				vm_delay(16);
				clear_message_window();
				break;
			}
		}
		// show/hide message window
		if (input_down(INPUT_CANCEL)) {
			input_wait_until_up(INPUT_CANCEL);
			if (!hidden) {
				toggle_message_window();
				anim_unpause(1);
				hidden = true;
			} else {
				toggle_message_window();
				anim_pause(1);
				hidden = false;
			}
		}
		vm_peek();
		vm_delay(16);
	}
	audio_voice_stop(0);
}

void shuusaku_update_palette(uint8_t *pal)
{
	if (!mem_get_sysvar16(69))
		gfx_palette_set(pal + 10*4, 10, 16);
	gfx_palette_set(pal + 42*4, 42, 204);
}

static uint8_t extra_palette[0x400] = {0};

static void load_image(const char *name, unsigned i, unsigned x_off, unsigned y_off)
{
	struct cg *cg = asset_cg_load(name);
	if (!cg) {
		WARNING("Failed to load CG \"%s\"", name);
		return;
	}

	cg->metrics.x += x_off;
	cg->metrics.y += y_off;
	gfx_draw_cg(i, cg);
	cg->metrics.x -= x_off;
	cg->metrics.y -= y_off;

	if (cg->palette && vm_flag_is_on(FLAG_LOAD_PALETTE)) {
		memcpy(memory.palette, cg->palette, 256 * 4);
	} else {
		memcpy(extra_palette, cg->palette, sizeof(extra_palette));
	}

	mem_set_sysvar16(mes_sysvar16_cg_x, cg->metrics.x / 8);
	mem_set_sysvar16(mes_sysvar16_cg_y, cg->metrics.y);
	mem_set_sysvar16(mes_sysvar16_cg_w, cg->metrics.w / 8);
	mem_set_sysvar16(mes_sysvar16_cg_h, cg->metrics.h);

	cg_free(cg);
}

static void stmt_load_image(void)
{
	PARAMS(params);
	unsigned x = 0;
	unsigned y = 0;
	if (params.nr_params > 1) {
		x = vm_expr_param(&params, 1) * 8;
		y = vm_expr_param(&params, 2);
	}
	load_image(vm_string_param(&params, 0), mem_get_sysvar16(mes_sysvar16_dst_surface), x, y);
}

static void stmt_surface_copy(void)
{
	PARAMS(params);
	int src_x = vm_expr_param(&params, 0) * 8;
	int src_y = vm_expr_param(&params, 1);
	int src_w = (vm_expr_param(&params, 2) * 8 - src_x) + 8;
	int src_h = (vm_expr_param(&params, 3) - src_y) + 1;
	unsigned src_i = vm_expr_param(&params, 4);
	int dst_x = vm_expr_param(&params, 5) * 8;
	int dst_y = vm_expr_param(&params, 6);
	unsigned dst_i = vm_expr_param(&params, 7);
	gfx_copy(src_x, src_y, src_w, src_h, src_i, dst_x, dst_y, dst_i);
}

static void stmt_surface_copy_masked(void)
{
	PARAMS(params);
	int src_x = vm_expr_param(&params, 0) * 8;
	int src_y = vm_expr_param(&params, 1);
	int src_w = (vm_expr_param(&params, 2) * 8 - src_x) + 8;
	int src_h = (vm_expr_param(&params, 3) - src_y) + 1;
	unsigned src_i = vm_expr_param(&params, 4);
	int dst_x = vm_expr_param(&params, 5) * 8;
	int dst_y = vm_expr_param(&params, 6);
	unsigned dst_i = vm_expr_param(&params, 7);
	gfx_copy_masked(src_x, src_y, src_w, src_h, src_i, dst_x, dst_y, dst_i, MASK_COLOR);
}

static void stmt_surface_fill(void)
{
	PARAMS(params);
	int x = vm_expr_param(&params, 0) * 8;
	int y = vm_expr_param(&params, 1);
	int w = (vm_expr_param(&params, 2) * 8 - x) + 8;
	int h = (vm_expr_param(&params, 3) - y) + 1;
	gfx_fill(x, y, w, h, mem_get_sysvar16(mes_sysvar16_dst_surface),
			(mem_get_sysvar16(mes_sysvar16_bg_color) >> 8) + 10);
}

static void stmt_set_color(void)
{
	PARAMS(params);
	unsigned i = vm_expr_param(&params, 0);
	if (i > 15 && i < 32)
		return;
	memory.palette[(10+i)*4+0] = vm_expr_param(&params, 3);
	memory.palette[(10+i)*4+1] = vm_expr_param(&params, 2);
	memory.palette[(10+i)*4+2] = vm_expr_param(&params, 1);
	memory.palette[(10+i)*4+3] = 1;
}

static void fill_palette(uint8_t *pal, uint8_t r, uint8_t g, uint8_t b)
{
	for (int i = 0; i < 256*4; i += 4) {
		pal[i+0] = b;
		pal[i+1] = g;
		pal[i+2] = r;
		pal[i+3] = 1;
	}
}

static void stmt_show_hide(void)
{
	PARAMS(params);
	if (params.nr_params > 0) {
		uint8_t pal[256*4];
		fill_palette(pal, vm_expr_param(&params, 0), vm_expr_param(&params, 1),
				vm_expr_param(&params, 2));
		shuusaku_update_palette(pal);
	} else {
		shuusaku_update_palette(memory.palette);
	}
}

static bool crossfade_tick(float rate, void *data)
{
	if (mem_get_sysvar16(19))
		return true;
	return !input_down(INPUT_CTRL);
}

void shuusaku_crossfade(uint8_t *pal, bool allow_16_32)
{
	uint8_t colors[256];
	unsigned nr_colors = 0;
	unsigned freeze_low = mem_get_sysvar16(69);

	if (!freeze_low) {
		for (int i = 10; i < 26; i++) {
			colors[nr_colors++] = i;
		}
	}
	if (allow_16_32) {
		for (int i = 26; i < 42; i++) {
			colors[nr_colors++] = i;
		}
	}
	for (int i = 42; i < 246; i++) {
		colors[nr_colors++] = i;
	}

	unsigned ms = mem_get_sysvar16(13) * 16;
	gfx_crossfade_colors(pal, colors, nr_colors, ms, crossfade_tick, NULL);
}

void shuusaku_crossfade_to(uint8_t r, uint8_t g, uint8_t b)
{
	uint8_t pal[256*4];
	uint8_t c[4] = { b, g, r };
	for (int i = 0; i < 256; i++) {
		memcpy(pal + i*4, c, 4);
	}
	shuusaku_crossfade(pal, false);
}

static void stmt_crossfade(void)
{
	PARAMS(params);
	if (params.nr_params > 0) {
		shuusaku_crossfade_to(vm_expr_param(&params, 0), vm_expr_param(&params, 1),
				vm_expr_param(&params, 2));
	} else {
		shuusaku_crossfade(memory.palette, false);
	}

}

/*
 * This crossfade differs in that colors proceed towards the result at a fixed
 * velocity, reaching the target color at their own pace (i.e. they are not
 * interpolated such that all colors arrive at the target simultaneously).
 */
static void shuusaku_crossfade2(uint8_t *pal)
{
	SDL_Color new_pal[236];
	for (int i = 0; i < 236; i++) {
		new_pal[i].b = pal[(10+i)*4 + 0];
		new_pal[i].g = pal[(10+i)*4 + 1];
		new_pal[i].r = pal[(10+i)*4 + 2];
	}

	unsigned ms = mem_get_sysvar16(13);
	unsigned freeze_low = mem_get_sysvar16(69);
	for (int i = 0; i < 256; i++) {
		for (int i = 0; i < 236; i++) {
			if (freeze_low && i < 16)
				continue;
			if (i > 15 && i < 22)
				continue;
			SDL_Color *cur_c = &gfx.palette[10 + i];
			SDL_Color *new_c = &new_pal[i];
			if (cur_c->r < new_c->r)
				cur_c->r++;
			else if (cur_c->r > new_c->r)
				cur_c->r--;
			if (cur_c->g < new_c->g)
				cur_c->g++;
			else if (cur_c->g > new_c->g)
				cur_c->g--;
			if (cur_c->b < new_c->b)
				cur_c->b++;
			else if (cur_c->b > new_c->b)
				cur_c->b--;
		}
		gfx_update_palette(0, 246);
		vm_peek();
		vm_delay(ms);
		if (!crossfade_tick(0.f, NULL)) {
			for (int i = 0; i < 236; i++) {
				if (freeze_low && i < 16)
					continue;
				if (i > 15 && i < 22)
					continue;
				gfx.palette[10 + i] = new_pal[i];
			}
			gfx_update_palette(0, 246);
			break;
		}
	}
}

static void stmt_crossfade2(void)
{
	PARAMS(params);

	uint8_t pal[256 * 4];
	if (params.nr_params > 0) {
		uint8_t r = vm_expr_param(&params, 0);
		uint8_t g = vm_expr_param(&params, 1);
		uint8_t b = vm_expr_param(&params, 2);
		for (int i = 0; i < 256; i++) {
			pal[i*4+0] = b;
			pal[i*4+1] = g;
			pal[i*4+2] = r;
		}
		shuusaku_crossfade2(pal);
	} else {
		shuusaku_crossfade2(memory.palette);
	}
}

static void load_anim(const char *str, int no)
{
	struct archive_data *data = asset_data_load(str);
	if (!data) {
		VM_ERROR("Failed to load file: \"%s\"", str);
	}
	// XXX: only 2 animation files can be loaded at once.
	vm_load_file(data, ANIM_OFFSET + (no ? ANIM_SIZE : 0));
	for (int i = 0; i < 10; i++) {
		anim_halt(no ? i + 10 : i);
	}
}

static void anim_start_sync(int no)
{
	anim_start(no);
	while (anim_stream_running(no)) {
		vm_peek();
	}
}

static void shuusaku_anim_load_palette(uint8_t *src)
{
	uint8_t pal[256 * 4];
	uint8_t *dst = pal + 10 * 4;
	for (int i = 0; i < 236; i++, src += 3, dst += 4) {
		// XXX: BRG -> BGR
		dst[0] = src[0];
		dst[1] = src[2];
		dst[2] = src[1];
		dst[3] = 1;
	}
	shuusaku_update_palette(pal);
}

static void stmt_anim(void)
{
	PARAMS(params);
	if (params.nr_params == 0)
		VM_ERROR("Too few parameters");
	if (params.params[0].type == MES_PARAM_STRING) {
		int no = params.nr_params > 1 ? vm_expr_param(&params, 1) : 0;
		load_anim(vm_string_param(&params, 0), no);
		return;
	}

	switch (vm_expr_param(&params, 0)) {
	case 0: anim_init_stream_from(vm_expr_param(&params, 1),
				vm_expr_param(&params, 2),
				ANIM_OFFSET); break;
	case 1: anim_start(vm_expr_param(&params, 1)); break;
	case 2: anim_pause_sync(vm_expr_param(&params, 1)); break;
	case 3: anim_start_sync(vm_expr_param(&params, 1)); break;
	case 4: anim_halt(vm_expr_param(&params, 1)); break;
	case 6: anim_unpause_range(0, 10); break;
	case 7: anim_pause_range_sync(0, 10); break;
	case 8: anim_unpause(vm_expr_param(&params, 1)); break;
	case 16: anim_init_stream_from(vm_expr_param(&params, 1) + 10,
				 vm_expr_param(&params, 2),
				 ANIM_OFFSET+ANIM_SIZE); break;
	case 17: anim_start(vm_expr_param(&params, 1) + 10); break;
	case 18: anim_pause_sync(vm_expr_param(&params, 1) + 10); break;
	case 19: anim_start_sync(vm_expr_param(&params, 1) + 10); break;
	case 20: anim_halt(vm_expr_param(&params, 1) + 10); break;
	default:
		VM_ERROR("Anim.function[%u] not implemented", params.params[0].val);
	}
}

// XXX: load_audio/load_effect are presumably supposed to load the audio data in
//      advance of playing it; we just store the name and load on demand.
static char bgm_file[33] = {0};
static char se_file[33] = {0};

static void stmt_load_audio(void)
{
	PARAMS(params);
	strncpy(bgm_file, vm_string_param(&params, 0), 32);
}

static void stmt_load_effect(void)
{
	PARAMS(params);
	strncpy(se_file, vm_string_param(&params, 0), 32);
}

static void stmt_load_voice(void)
{
	PARAMS(params);
	audio_voice_play(vm_string_param(&params, 0), 0);
}

static int bgm_vol = 0;

static void stmt_audio(void)
{
	int vol, t;
	PARAMS(params);
	switch (vm_expr_param(&params, 0)) {
	case 0:
		audio_bgm_play(bgm_file, 0);
		break;
	case 1:
		audio_stop(AUDIO_CH_BGM);
		break;
	case 3:
		// FIXME: fade volume curve is wrong (should be logarithmic?)
		t = 1500 * (vm_expr_param(&params, 1) + 1);
		audio_fade(AUDIO_CH_BGM, AUDIO_VOLUME_MIN, t, true, false);
		break;
	case 5:
		vol = vm_expr_param(&params, 1) * 50 + AUDIO_VOLUME_MIN;
		audio_set_volume(AUDIO_CH_BGM, bgm_vol + vol);
		break;
	case 16:
		audio_se_play(se_file, 0);
		break;
	case 17:
		audio_se_stop(0);
		break;
	case 19:
		t = 1500 * (vm_expr_param(&params, 1) + 1);
		audio_se_fade(AUDIO_VOLUME_MIN, t, true, false, 0);
		break;
	case 20:
		t = 1500 * (vm_expr_param(&params, 1) + 1);
		audio_se_fade(AUDIO_VOLUME_MIN, t, true, true, 0);
		break;
	case 22:
		while (audio_is_playing(AUDIO_CH_SE0)) {
			vm_peek();
			vm_delay(16);
		}
		break;
	default:
		VM_ERROR("Audio.function[%u] not implemented", params.params[0].val);
	}
}

struct movie {
	unsigned nr_frames;
	unsigned w, h;
	uint8_t *frame[100];
};

#define MOVIE_X 56
#define MOVIE_Y 72

static uint8_t *decode_offset(uint8_t *dst, int stride, uint8_t b)
{
	static int same_line_offsets[8] = {
		-1, -2, -4, -6, -8, -12, -16, -20
	};
	static int prev_line_offsets[16] = {
		-20, -16, -12, -8, -6, -4, -2, -1, 0, 1, 2, 4, 6, 8, 12, 16
	};

	int x_off;
	int y_off;
	if (b & 0x70) {
		x_off = prev_line_offsets[b & 0xf];
		y_off = -((b >> 4) & 7);
	} else {
		x_off = same_line_offsets[b & 0x7];
		y_off = 0;
	}

	return dst + stride * y_off + x_off;
}

static void movie_draw_frame(struct movie *mov, unsigned frame)
{
	SDL_Surface *dst_s = gfx_get_surface(0);
	uint8_t *src = mov->frame[frame];
	uint8_t *dst_base = dst_s->pixels + MOVIE_Y * dst_s->pitch + MOVIE_X;

	for (int row = 0; row < mov->h; row++) {
		uint8_t *dst = dst_base + row * dst_s->pitch;
		for (int col = 0; col < mov->w;) {
			uint8_t b = *src++;
			if (b & 0x80) {
				uint8_t *copy_src = decode_offset(dst, dst_s->pitch, b);
				unsigned len = *src++ + 2;
				for (unsigned i = 0; i < len; i++, col++) {
					assert(col < mov->w);
					*dst++ = *copy_src++;
				}
			} else {
				// literal bytes
				for (unsigned i = 0; i < b; i++, col++) {
					assert(col < mov->w);
					*dst++ = *src++;
				}
			}
		}
	}
	gfx_dirty(0, MOVIE_X, MOVIE_Y, mov->w, mov->h);
}

void shuusaku_play_movie(const char *name)
{
	struct archive_data *file = asset_load(ASSET_MOVIE, name);
	if (!file) {
		WARNING("Failed to load movie: \"%s\"", name);
		return;
	}

	// minumum movie size: 1 frame at 1x1
	if (file->size < 12 + 708 + 1)
		goto invalid_movie;

	// read header
	struct movie mov;
	mov.nr_frames = le_get32(file->data, 0);
	mov.w = le_get16(file->data, 4);
	mov.h = le_get16(file->data, 6);

	// validate header params
	if (mov.nr_frames > 100)
		goto invalid_movie;
	if (MOVIE_X + mov.w > 640 || MOVIE_Y + mov.h > 480)
		goto invalid_movie;

	// read frame offsets
	uint8_t *data = file->data + 8 + mov.nr_frames * 4 + 708;
	for (unsigned i = 0; i < mov.nr_frames; i++) {
		mov.frame[i] = data + le_get32(file->data, 8 + i * 4);
	}

	// read/load palette
	uint8_t *pal = file->data + 8 + mov.nr_frames * 4;
	for (int i = 31; i < 172; i++) {
		uint8_t *c = pal + i * 3;
		memory.palette[(10 + i) * 4 + 0] = c[2];
		memory.palette[(10 + i) * 4 + 1] = c[1];
		memory.palette[(10 + i) * 4 + 2] = c[0];
	}
	shuusaku_update_palette(memory.palette);

	vm_timer_t timer = vm_timer_create();
	for (unsigned frame = 0; frame < mov.nr_frames; frame++) {
		movie_draw_frame(&mov, frame);
		vm_peek();
		vm_timer_tick(&timer, 80);
	}

	archive_data_release(file);
	return;

invalid_movie:
	WARNING("Failed to parse movie: \"%s\"", name);
	archive_data_release(file);

}

static void stmt_play_movie(void)
{
	PARAMS(params);
	shuusaku_play_movie(vm_string_param(&params, 0));
}

static void util_pixel_crossfade(struct param_list *params)
{
	unsigned src = vm_expr_param(params, 1);
	unsigned x = vm_expr_param(params, 2);
	unsigned y = vm_expr_param(params, 3);
	unsigned w = (vm_expr_param(params, 4) - x) + 1;
	unsigned h = (vm_expr_param(params, 5) - y) + 1;
	gfx_pixel_crossfade(x * 8, y, w * 8, h, src, x * 8, y, 0, 20, crossfade_tick, NULL);
}

struct crossfade_data {
	SDL_Color old_pal[236];
	SDL_Color new_pal[236];
};

static uint8_t u8_interp(uint8_t a, uint8_t b, float rate)
{
	int d = b - a;
	return a + d * rate;
}

static bool crossfade_update_palette(float t, void *_data)
{
	struct crossfade_data *data = _data;

	// check cancel
	if (mem_get_sysvar16(19) && input_down(INPUT_CTRL)) {
		for (int i = 0; i < 236; i++) {
			if (i > 15 && i < 32)
				continue;
			gfx.palette[10+i] = data->new_pal[i];
		}
		gfx_update_palette(10, 16);
		gfx_update_palette(42, 204);
		return false;
	}

	for (int i = 0; i < 236; i++) {
		if (i > 15 && i < 32)
			continue;
		gfx.palette[10+i].r = u8_interp(data->old_pal[i].r, data->new_pal[i].r, t);
		gfx.palette[10+i].g = u8_interp(data->old_pal[i].g, data->new_pal[i].g, t);
		gfx.palette[10+i].b = u8_interp(data->old_pal[i].b, data->new_pal[i].b, t);
	}
	gfx_update_palette(10, 16);
	gfx_update_palette(42, 204);
	return true;
}

static void util_pixel_and_palette_crossfade(struct param_list *params)
{
	unsigned src = vm_expr_param(params, 1);
	unsigned x = vm_expr_param(params, 2);
	unsigned y = vm_expr_param(params, 3);
	unsigned w = (vm_expr_param(params, 4) - x) + 1;
	unsigned h = (vm_expr_param(params, 5) - y) + 1;

	struct crossfade_data data;
	memcpy(data.old_pal, gfx.palette + 10, sizeof(SDL_Color) * 236);
	uint8_t *c = memory.palette + 10 * 4;
	for (int i = 0; i < 236; i++, c += 4) {
		data.new_pal[i] = (SDL_Color) { .r = c[2], .g = c[1], .b = c[0], .a = 255 };
	}

	gfx_pixel_crossfade(x * 8, y, w * 8, h, src, x * 8, y, 0, 20, crossfade_update_palette, &data);
}

/*
 * Draw the clock with a given day/time.
 */
static void draw_datetime(unsigned day, unsigned t, unsigned dst_x, unsigned dst_y, unsigned dst)
{
	// draw day of week
	if (day == DAY_SUN) {
		gfx_copy(240, 256, 88, 48, 4, dst_x, dst_y, dst);
	} else if (day == DAY_MON) {
		gfx_copy(328, 240, 88, 48, 4, dst_x, dst_y, dst);
	} else if (day == DAY_SAT) {
		gfx_copy(240, 240, 88, 48, 4, dst_x, dst_y, dst);
	}

	// draw time
	unsigned hour = t / 100;
	unsigned minute = t % 100;

	// AM / PM
	if (hour < 12) {
		gfx_copy(192, 240, 24, 16, 4, dst_x, dst_y+16, dst);
		gfx_copy(216, 256, 24, 16, 4, dst_x, dst_y+32, dst);
	} else {
		gfx_copy(216, 240, 24, 16, 4, dst_x, dst_y+16, dst);
		gfx_copy(192, 256, 24, 16, 4, dst_x, dst_y+32, dst);
	}

	if (hour > 12)
		hour -= 12;

	// hour - tens digit
	gfx_copy(hour < 10 ? 172 : 164, 240, 4, 32, 4, dst_x+24, dst_y+16, dst);
	// hour - ones digit
	gfx_copy((hour % 10) * 16, 240, 16, 32, 4, dst_x+28, dst_y+16, dst);
	// colon
	gfx_copy(176, 240, 8, 32, 4, dst_x+44, dst_y+16, dst);
	// minute - tens digit
	gfx_copy((minute / 10) * 16, 240, 16, 32, 4, dst_x+52, dst_y+16, dst);
	// minute - ones digit
	gfx_copy((minute % 10) * 16, 240, 16, 32, 4, dst_x+68, dst_y+16, dst);
	// right border
	gfx_copy(188, 240, 4, 32, 4, dst_x+84, dst_y+16, dst);
}

static struct {
	unsigned time;
	unsigned day;
	unsigned i;
	bool flash_enabled;
	bool flash_shown;
	vm_timer_t flash_timer;
} plan = {0};

static void check_time(unsigned t)
{
	unsigned hour = t / 100;
	unsigned minute = t % 100;
	if (hour > 23)
		VM_ERROR("Invalid time (bad hour): %u", t);
	if (minute > 45 || (minute % 15))
		VM_ERROR("Invalid time (bad minute): %u", t);
}

static void plan_tick(void)
{
	if (!plan.flash_enabled || plan.i == 0)
		return;

	if (!vm_timer_tick_async(&plan.flash_timer, 1000))
		return;

	if (plan.flash_shown) {
		gfx_copy(0, 0, plan.i * 88, 48, 3, 0, 0, 0);
		plan.flash_shown = false;
	} else {
		gfx_copy(0, 320, plan.i * 88, 48, 4, 0, 0, 0);
		plan.flash_shown = true;
	}
}

static void plan_fini(void)
{
	// restore background
	gfx_copy(0, 272, 352, 48, 4, 0, 0, 0);
	// restore original datetime
	gfx_copy(0, 416, 88, 48, 3, 0, 0, 0);
	plan.flash_enabled = false;
	shuusaku_schedule_clear_plan();
}

static void plan_init(void)
{
	plan.day = mem_get_var32(4);
	plan.time = mem_get_var32(19);
	plan.i = 0;
	plan.flash_enabled = true;

	check_time(plan.time);

	// copy original datetime to surface 3
	gfx_copy(0, 0, 88, 48, 0, 0, 416, 3);
	// copy bg at top of screen to surface 4 @ (0,272)
	gfx_copy(0, 0, 352, 48, 3, 0, 272, 4);

	shuusaku_schedule_set_plan_time(plan.day, plan.time);
}

static void plan_draw_datetime(void)
{
	if (!plan.flash_enabled)
		return;

	// draw next plan datetime entry
	draw_datetime(plan.day, plan.time, plan.i * 88, 320, 4);
	plan.i++;

	// ensure entire plan is drawn
	if (plan.i > 1)
		gfx_copy(0, 320, plan.i * 88, 48, 4, 0, 0, 0);

	// update schedule window
	shuusaku_schedule_set_plan_time(plan.day, plan.time);

	// adjust time
	plan.time += 15;
	if ((plan.time % 100) == 60)
		plan.time = plan.time + 40;

	// XXX: hack to immediately show flashing datetime
	plan.flash_timer = vm_timer_create() - 1001;
	plan.flash_shown = false;
	plan_tick();
}

static void plan_back(void)
{
	if (plan.i == 0)
		return;

	plan.i--;
	if (plan.time == 0) {
		if (plan.day == DAY_SUN) {
			plan.day = DAY_SAT;
		} else if (plan.day == DAY_MON) {
			plan.day = DAY_SUN;
		} else {
			WARNING("Invalid day/time: %u, %u", plan.day, plan.time);
			return;
		}
		plan.time = 2345;
	} else if (plan.time % 100 == 0) {
		plan.time -= 55;
	} else {
		plan.time -= 15;
	}

	// clear datetime
	gfx_copy(plan.i * 88, 272, 88, 48, 4, plan.i * 88, 0, 0);
	// update schedule window
	shuusaku_schedule_set_plan_time(plan.day, plan.time);
}

static void util_plan(struct param_list *params)
{
	switch (vm_expr_param(params, 1)) {
	case 0:
		plan_fini();
		break;
	case 1:
		plan_init();
		break;
	case 2:
		plan_draw_datetime();
		break;
	case 3:
		plan_back();
		break;
	default:
		VM_ERROR("Util.Plan.function[%u] not implemented", params->params[1].val);
	}
}

static void util_show_hide_message_window(struct param_list *params)
{
	static bool shown = false;
	if (vm_expr_param(params, 1)) {
		if (!shown) {
			// draw mesage window
			int y = screen_y() + 371;
			anim_pause(1);
			// copy bg under message window to surface 4 @ (0,80)
			gfx_copy(0, y, 640, 74, 0, 0, 80, 4);
			// create 2nd copy of bg on surface 4 @ (0,160)
			gfx_copy(0, 80, 640, 74, 4, 0, 160, 4);
			// copy message frame to surface 4 @ (0,80)
			gfx_copy_masked(0, 0, 640, 74, 4, 0, 80, 4, MASK_COLOR);
			// copy message window back to surface 0
			gfx_copy(0, 80, 640, 74, 4, 0, y, 0);
			shown = true;
		}
	} else {
		if (shown) {
			int y = screen_y() + 371;
			// copy bg from surface 4 @ (0,160) to screen
			gfx_copy(0, 160, 640, 74, 4, 0, y, 0);
			anim_unpause(1);
			shown = false;
		}
	}
}

static void util_update_schedule(struct param_list *params)
{
	unsigned location = vm_expr_param(params, 1);
	if (location != 0xff && location > 10)
		VM_ERROR("Invalid location: %u", location);

	if (location != 0xff) {
		unsigned day = mem_get_var32(4);
		unsigned t = mem_get_var32(19);
		uint8_t flag = vm_expr_param(params, 2);
		shuusaku_schedule_set_flag(location, day, t, flag);
	}

	shuusaku_schedule_update();
}

static void util_load_extra_palette(struct param_list *params)
{
	memcpy(memory.palette + 42*4, extra_palette + 42*4, 96*4);
}

static void util_photo_slide(struct param_list *params)
{
	if (vm_expr_param(params, 1)) {
		// slide out
		for (int x = 0; x > -408; x -= 68) {
			gfx_copy(0, 56, 408, 312, 1, 0, 56, 0);
			gfx_copy_masked(0, 0, 408, 312, 2, x, 56, 0, MASK_COLOR);
			vm_peek();
			vm_delay(10);
		}
	} else {
		// silde in
		for (int x = -340; x <= 0; x += 68) {
			gfx_copy(0, 56, 408, 312, 1, 0, 56, 0);
			gfx_copy_masked(0, 0, 408, 312, 2, x, 56, 0, MASK_COLOR);
			vm_peek();
			vm_delay(10);
		}
	}
}

static void util_status_dirty(struct param_list *params)
{
	shuusaku_status_update();
}

static void util_draw_datetime(struct param_list *params)
{
	unsigned t = vm_expr_param(params, 1);
	unsigned day = vm_expr_param(params, 2);
	unsigned dst = vm_expr_param(params, 3);

	// copy bg behind clock
	gfx_copy(0, 0, 88, 48, 0, 0, 368, 4);

	// draw
	draw_datetime(day, t, 0, 0, dst);
}

#define ZOOM_STEPS 8

static SDL_Surface *copy_surface(unsigned i)
{
	SDL_Surface *dst;
	SDL_Surface *src = gfx_get_surface(i);
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, dst, 0, 640, 480,
			GFX_DIRECT_BPP, GFX_DIRECT_FORMAT);
	SDL_CALL(SDL_FillRect, dst, NULL, SDL_MapRGB(gfx.display->format, 0, 0, 0));
	SDL_CALL(SDL_SetPaletteColors, src->format->palette, gfx.palette, 0, 256);
	SDL_CALL(SDL_BlitSurface, src, NULL, dst, NULL);
	return dst;
}

// XXX: util_scene_viewer_zoom_out uses the previous value of these to reverse the zoom
static int zoom_x_step;
static int zoom_y_step;
static int zoom_w_step;
static int zoom_h_step;

void shuusaku_zoom(int x, int y, int w, int h, unsigned src_i)
{
	// XXX: can't use SDL_BlitScaled on indexed surfaces, so we create a direct-color
	//      copy of src surface and draw directly to the display surface
	SDL_Surface *src = copy_surface(src_i);
	SDL_Surface *dst = gfx.display;

	zoom_x_step = x / ZOOM_STEPS;
	zoom_y_step = y / ZOOM_STEPS;
	zoom_w_step = (640 - w) / ZOOM_STEPS;
	zoom_h_step = (480 - h) / ZOOM_STEPS;

	SDL_Rect dst_r = { x, y, w, h };
	SDL_Rect src_r = { 0, 0, 640, 480 };
	vm_timer_t timer = vm_timer_create();
	for (int i = 0; i < ZOOM_STEPS - 1; i++) {
		dst_r.x -= zoom_x_step;
		dst_r.y -= zoom_y_step;
		dst_r.w += zoom_w_step;
		dst_r.h += zoom_h_step;

		SDL_CALL(SDL_BlitScaled, src, &src_r, dst, &dst_r);
		uint8_t *p = dst->pixels + dst_r.y * dst->pitch + dst_r.x * dst->format->BytesPerPixel;
		SDL_CALL(SDL_UpdateTexture, gfx.texture, &dst_r, p, dst->pitch);
		SDL_CALL(SDL_RenderClear, gfx.renderer);
		SDL_CALL(SDL_RenderCopy, gfx.renderer, gfx.texture, NULL, NULL);
		SDL_RenderPresent(gfx.renderer);
		vm_timer_tick(&timer, 20);
	}

	// update surface 0
	gfx_copy(0, 0, 640, 480, src_i, 0, 0, 0);
	SDL_FreeSurface(src);
}

static void util_zoom_movie(struct param_list *params)
{
	shuusaku_zoom(56, 72, 320, 240, 1);
}

static void util_scene_viewer_zoom_out(struct param_list *params)
{
	// XXX: can't use SDL_BlitScaled on indexed surfaces, so we create a direct-color
	//      copy of src surface and draw directly to the display surface
	SDL_Surface *src = copy_surface(0);
	SDL_Surface *dst = gfx.display;
	SDL_Surface *bg = gfx_get_surface(8);
	SDL_CALL(SDL_SetPaletteColors, bg->format->palette, gfx.palette, 0, 256);

	SDL_Rect prev_r = { 0, 0, 640, 480 };
	SDL_Rect dst_r = { 0, 0, 640, 480 };
	SDL_Rect src_r = { 0, 0, 640, 480 };
	vm_timer_t timer = vm_timer_create();
	for (int i = 0; i < ZOOM_STEPS - 1; i++) {
		// retore background
		SDL_CALL(SDL_BlitSurface, bg, &dst_r, dst, &dst_r);
		prev_r = dst_r;
		// update zoom area
		dst_r.x += zoom_x_step;
		dst_r.y += zoom_y_step;
		dst_r.w -= zoom_w_step;
		dst_r.h -= zoom_h_step;
		// draw zoomed surface
		SDL_CALL(SDL_BlitScaled, src, &src_r, dst, &dst_r);
		uint8_t *p = dst->pixels + prev_r.y * dst->pitch + prev_r.x * dst->format->BytesPerPixel;
		SDL_CALL(SDL_UpdateTexture, gfx.texture, &prev_r, p, dst->pitch);
		SDL_CALL(SDL_RenderClear, gfx.renderer);
		SDL_CALL(SDL_RenderCopy, gfx.renderer, gfx.texture, NULL, NULL);
		SDL_RenderPresent(gfx.renderer);
		vm_timer_tick(&timer, 20);
	}

	// restore full background to screen surface
	gfx_copy(0, 0, 640, 480, 8, 0, 0, 0);
	SDL_FreeSurface(src);
}

void shuusaku_cam_event_zoom(unsigned cg_x, unsigned cg_y, unsigned cg_w, unsigned cg_h)
{
	unsigned step_w = cg_w / 16;
	unsigned step_h = cg_h / 16;
	SDL_Rect r = { cg_x + cg_w / 2, cg_y + cg_h / 2, 1, 1 };

	for (int i = 0; i < 16; i++) {
		SDL_CALL(SDL_RenderClear, gfx.renderer);
		SDL_CALL(SDL_RenderCopy, gfx.renderer, gfx.texture, NULL, NULL);
		SDL_CALL(SDL_SetRenderDrawColor, gfx.renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
		SDL_CALL(SDL_RenderDrawRect, gfx.renderer, &r);
		SDL_CALL(SDL_SetRenderDrawColor, gfx.renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
		SDL_RenderPresent(gfx.renderer);
		r.x -= step_w/2;
		r.y -= step_h/2;
		r.w += step_w;
		r.h += step_h;
		vm_delay(20);
	}
}

static void util_cam_event_zoom(struct param_list *params)
{
	shuusaku_cam_event_zoom(vm_expr_param(params, 1) * 8, vm_expr_param(params, 2),
			vm_expr_param(params, 3) * 8, vm_expr_param(params, 4));
}

/*
 * Short fade to black on colors 172-235.
 */
void shuusaku_after_movie_crossfade(void)
{
	uint8_t pal[256 * 4];
	memset(pal + 182*4, 0, 64*4);
	uint8_t colors[64];
	for (int i = 0; i < 64; i++) {
		colors[i] = 182 + i;
	}
	gfx_crossfade_colors(pal, colors, 64, 64, NULL, NULL);
}

static void util_clear_high_colors(struct param_list *params)
{
	shuusaku_after_movie_crossfade();
}

static void util_ending_pixel_crossfade_slow(struct param_list *params)
{
	gfx_pixel_crossfade(0, 0, 640, 480, 1, 0, 0, 0, 600, crossfade_tick, NULL);
}

static void read_name(uint8_t *src, uint8_t *dst)
{
	for (int i = 0; i < 16 && src[i] != 0xff; i++) {
		*dst++ = src[i];
	}
	*dst = '\0';
}

static void util_scene_viewer_char_select(struct param_list *params)
{
	mem_set_var32(22, shuusaku_scene_viewer_char_select());
}

static void util_scene_viewer_scene_select(struct param_list *params)
{
	bool need_bg = vm_expr_param(params, 1);
	mem_set_var32(22, shuusaku_scene_viewer_scene_select(mem_get_var32(22) - 1, need_bg));
}

static void util_name_input(struct param_list *params)
{
	shuusaku_name_input_screen(memory_raw + HEAP_OFF, memory_raw + HEAP_OFF + 16);
}

static void util_draw_myouji(struct param_list *params)
{
	uint8_t name[17];
	read_name(memory_raw + HEAP_OFF, name);
	shuusaku_draw_text((char*)name);
}

static void util_draw_namae(struct param_list *params)
{
	uint8_t name[17];
	read_name(memory_raw + HEAP_OFF + 16, name);
	shuusaku_draw_text((char*)name);
}

static void util_credits(struct param_list *params)
{
	// 企画・シナリオ・ゲームデザイン
	uint8_t header[] = {
		0x8A, 0xE9, 0x89, 0xE6, 0x81, 0x45, 0x83, 0x56, 0x83, 0x69, 0x83, 0x8A,
		0x83, 0x49, 0x81, 0x45, 0x83, 0x51, 0x81, 0x5B, 0x83, 0x80, 0x83, 0x66,
		0x83, 0x55, 0x83, 0x43, 0x83, 0x93, 0
	};
	// 高部 絵里
	uint8_t eri_name[] = { 0x8D, 0x82, 0x95, 0x94, 0x20, 0x8A, 0x47, 0x97, 0xA2, 0 };
	uint8_t text[64];
	int x = 9, y;
	uint8_t fg_color;
	unsigned font_size;
	unsigned which = vm_expr_param(params, 1);
	if (which == 0) {
		memcpy(text, header, sizeof(header));
		y = 188;
		fg_color = 14;
		font_size = 24;
	} else if (which == 2) {
		uint8_t *myouji = memory_raw + HEAP_OFF;
		uint8_t *namae = memory_raw + HEAP_OFF + 16;
		int len = 0;
		for (int i = 0; i < 16 && myouji[i] != 0xff; i++) {
			text[len++] = myouji[i];
		}
		text[len++] = ' ';
		for (int i = 0; i < 16 && namae[i] != 0xff; i++) {
			text[len++] = namae[i];
		}
		text[len] = '\0';
		y = 252;
		fg_color = 22;
		font_size = 32;
	} else {
		memcpy(text, eri_name, sizeof(eri_name));
		y = 292;
		fg_color = 18;
		font_size = 32;
	}

	// draw text
	gfx_text_set_size(font_size, 1);
	gfx_text_set_colors(gfx.text.bg, fg_color);
	text_shadow = TEXT_SHADOW_NONE;
	const char *text_p = (char*)text;
	while (*text_p) {
		int ch;
		unsigned char_space = font_size / (SJIS_2BYTE(*text_p) ? 1 : 2);
		text_p = sjis_char2unicode(text_p, &ch);
		gfx_text_draw_glyph(x, y, 0, ch);
		x += char_space;
	}
	gfx_text_set_size(16, 1);
	text_shadow = TEXT_SHADOW_B;
}

static void util_scroll_down(struct param_list *params)
{
	int target_y = vm_expr_param(params, 1);
	for (int y = screen_y() + 4; y <= target_y; y += 4) {
		set_screen_y(y);
		vm_peek();
		vm_delay(20);
	}
}

struct pixel_drop {
	int16_t y;
	int16_t velocity;
};

static void util_pixel_drop(struct param_list *params)
{
	struct pixel_drop *drop = xmalloc(640 * 480 * sizeof(struct pixel_drop));

	uint16_t i = 1;
	struct pixel_drop *p = drop;
	for (int row = 0; row < 480; row++) {
		for (int col = 0; col < 640; col++, p++) {
			uint16_t v = i % 43;
			if (v < 17)
				v = 0;
			p->y = row;
			p->velocity = v;
			i = i * 5723 + 1;
		}
	}

	SDL_Surface *dst = gfx_get_surface(0);
	SDL_Surface *fg = gfx_get_surface(1);
	unsigned freeze_low = mem_get_sysvar16(69);

	uint16_t mod = 11;
	for (int frame = 0; frame < 16; frame++) {
		mod += 2;
		// draw background
		gfx_copy(0, 0, 640, 480, 3, 0, 0, 0);

		// draw fg pixels
		struct pixel_drop *p = drop;
		for (int row = 0; row < 480; row++) {
			for (int col = 0; col < 640; col++, p++) {
				uint8_t *dst_p = dst->pixels + p->y * dst->pitch + col;
				uint8_t *fg_p = fg->pixels + row * dst->pitch + col;
				if (p->y >= 480 || *fg_p == MASK_COLOR)
					continue;
				*dst_p = *fg_p;
				uint16_t v = i % mod;
				if (v < 7)
					v = 0;
				p->y += p->velocity;
				p->velocity = v;
				i = i * 5723 + 1;
			}
		}

		// crossfade
		for (int i = 0; i < 236; i++) {
			if (freeze_low && i < 16)
				continue;
			if (i > 15 && i < 32)
				continue;
			SDL_Color *c = &gfx.palette[10 + i];
			c->r = min((int)c->r + 16, 255);
			c->g = min((int)c->g + 16, 255);
			c->b = min((int)c->b + 16, 255);
		}
		gfx_update_palette(0, 246);
		vm_peek();
		vm_delay(30);
	}

	free(drop);
}

static void util_anim_wait(struct param_list *params)
{
	// if either mouse button down, return 1
	// if state is halted, return 0
	// if ctrl is down, return 1
	int which = vm_expr_param(params, 1);
	int slot = vm_expr_param(params, 2) + (which ? 10 : 0);
	while (true) {
		if (input_down(INPUT_ACTIVATE) || input_down(INPUT_CANCEL)) {
			mem_set_var32(18, 1);
			return;
		}
		if (anim_get_state(slot) == ANIM_STATE_HALTED) {
			mem_set_var32(18, 0);
			return;
		}
		if (input_down(INPUT_CTRL)) {
			mem_set_var32(18, 1);
			return;
		}
		vm_peek();
	}
}

static void util_set_y_offset(struct param_list *params)
{
	set_screen_y(vm_expr_param(params, 1));
}

static void util_quake(struct param_list *params)
{
	int nr_quakes = vm_expr_param(params, 1);
	int cur_y = screen_y();
	int quake_y = cur_y - nr_quakes;
	vm_timer_t timer = vm_timer_create();
	for (int i = 0; i < nr_quakes; i++, quake_y++) {
		set_screen_y(quake_y);
		gfx_update();
		vm_peek();
		vm_timer_tick(&timer, 20);
		set_screen_y(cur_y);
		gfx_update();
		vm_peek();
		vm_timer_tick(&timer, 20);
	}
}

static void util_ending_crossfade(struct param_list *params)
{
	// why does this exist?
	shuusaku_crossfade2(memory.palette);
}

static void util_ending_pixel_crossfade(struct param_list *params)
{
	gfx_pixel_crossfade(0, 0, 640, 480, 1, 0, 480, 0, 20, crossfade_tick, NULL);
}

static void util_set_config_enabled(struct param_list *params)
{
	// TODO: this function enables/disables the config button on the menu bar
}

void _load_image(const char *name, unsigned i)
{
	struct cg *cg = asset_cg_load(name);
	if (!cg) {
		WARNING("Failed to load CG \"%s\"", name);
		return;
	}

	gfx_draw_cg(i, cg);
	gfx_palette_set(cg->palette, 0, 236);
	cg_free(cg);
}

static void schedule_window_clicked(void *_)
{
	shuusaku_schedule_window_toggle();
}

static void status_window_clicked(void *_)
{
	shuusaku_status_window_toggle();
}

static void restart_clicked(void *_)
{
	if (gfx_confirm_quit())
		restart();
}

static void quit_clicked(void *_)
{
	if (gfx_confirm_quit())
		sys_exit(0);
}

static void open_context_menu(void)
{
	struct menu *m = popup_menu_new();
	// XXX: UI font doesn't have Japanese glyphs...
	popup_menu_append_entry(m, 1, "Schedule" /*"スケジュール表"*/, "Space",
			schedule_window_clicked, NULL);
	popup_menu_append_entry(m, 2, "Items & Status" /*"風呂・食事＆アイテム"*/, "F1",
			status_window_clicked, NULL);
	popup_menu_append_separator(m);
	popup_menu_append_entry(m, -1, "Restart", NULL, restart_clicked, NULL);
	popup_menu_append_entry(m, -1, "Quit", "Alt+F4", quit_clicked, NULL);
	popup_menu_append_separator(m);
	popup_menu_append_entry(m, -1, "Cancel", NULL, NULL, NULL);

	int win_x, win_y, mouse_x, mouse_y;
	SDL_GetWindowPosition(gfx.window, &win_x, &win_y);
	SDL_GetMouseState(&mouse_x, &mouse_y);
	popup_menu_run(m, win_x + mouse_x, win_y + mouse_y);

	popup_menu_free(m);
}

static bool shuusaku_handle_event(SDL_Event *e)
{
	if (shuusaku_schedule_window_event(e))
		return true;
	if (shuusaku_status_window_event(e))
		return true;

	switch (e->type) {
	case SDL_KEYDOWN:
		switch (e->key.keysym.sym) {
		case SDLK_SPACE:
			shuusaku_schedule_window_toggle();
			return true;
		case SDLK_F1:
			shuusaku_status_window_toggle();
			return true;
		}
		break;
	case SDL_MOUSEBUTTONDOWN:
		if (e->button.windowID != gfx.window_id)
			break;
		if (e->button.button == 2 && e->button.state == SDL_PRESSED) {
			open_context_menu();
			return true;
		}
		break;
	}

	return false;
}

static int init_volume(int *val, int ch)
{
	if (*val < 0 || *val > 191)
		*val = 191;

	int vol = -6000 + roundf((*val / 191.f) * 6000.f);
	audio_set_volume(ch, vol);
	return vol;
}

static void shuusaku_init(void)
{
	schedule_window_init();
	shuusaku_status_init();

	bgm_vol = init_volume(&config.volume.music, AUDIO_CH_BGM);
	init_volume(&config.volume.se, AUDIO_CH_SE0);
	init_volume(&config.volume.voice, AUDIO_CH_VOICE0);

	mem_set_sysvar16(50, config.shuusaku.kettei);

	asset_effect_is_bgm = false;

	text_shadow = TEXT_SHADOW_B;
	gfx_text_set_size(16, 1);

	anim_frame_t = 20;
	anim_load_palette = shuusaku_anim_load_palette;
	_load_image("selwaku.gpx", 5);
	_load_image("bll.gpx", 0);
}

static void shuusaku_update(void)
{
	if (shuusaku_running_cam_event)
		return;
	plan_tick();
	shuusaku_schedule_tick();
}

struct game game_shuusaku = {
	.id = GAME_SHUUSAKU,
	// XXX: surface[0] is larger than view
	.view = { 640, 480 },
	.surface_sizes = {
		[0] = { 640, 1200 },
		[1] = { 640, 560 },
		[2] = { 640, 480 },
		[3] = { 640, 480 },
		[4] = { 640, 480 },
		[5] = { 640, 1200 },
		[6] = { 640, 2040 },
		[7] = { 640, 1480 },
		[8] = { 640, 480 },
	},
	.bpp = 8,
	.var4_size = VAR4_SIZE,
	.mem16_size = MEM16_SIZE,
	.handle_event = shuusaku_handle_event,
	.mem_init = shuusaku_mem_init,
	.mem_restore = shuusaku_mem_restore,
	.init = shuusaku_init,
	.update = shuusaku_update,
	.unprefixed_zen = unprefixed_error,
	.unprefixed_han = unprefixed_error,
	.vm = VM_AIW,
	.expr_op = {
		[0xe0] = vm_expr_plus,
		[0xe1] = vm_expr_minus_unsigned,
		[0xe2] = vm_expr_mul,
		[0xe3] = vm_expr_div,
		[0xe4] = vm_expr_mod,
		[0xe5] = vm_expr_rand_with_imm_range,
		[0xe6] = vm_expr_and,
		[0xe7] = vm_expr_or,
		[0xe8] = vm_expr_bitand,
		[0xe9] = vm_expr_bitior,
		[0xea] = vm_expr_bitxor,
		[0xeb] = vm_expr_lt,
		[0xec] = vm_expr_gt,
		[0xed] = vm_expr_lte,
		[0xee] = vm_expr_gte,
		[0xef] = vm_expr_eq,
		[0xf0] = vm_expr_neq,
		[0xf1] = vm_expr_imm16,
		[0xf2] = vm_expr_imm32,
		[0xf3] = vm_expr_cflag_packed,
		[0xf4] = vm_expr_eflag_packed,
		[0xf6] = vm_expr_var16_const16,
		[0xf7] = vm_expr_var16_expr,
		[0xf8] = vm_expr_sysvar16_const16,
		[0xf9] = vm_expr_sysvar16_expr,
	},
	.stmt_op = {
		[0x00] = stmt_txt,
		[0x01] = vm_stmt_jmp,
		[0x02] = vm_stmt_util,
		[0x03] = vm_stmt_mesjmp_aiw,
		[0x04] = vm_stmt_mescall_aiw,
		[0x05] = vm_stmt_set_flag_const16_aiw,
		[0x06] = vm_stmt_set_flag_expr_aiw,
		[0x07] = vm_stmt_set_var32_const8_aiw,
		[0x0a] = vm_stmt_set_var16_const16_aiw,
		[0x0b] = vm_stmt_set_var16_expr_aiw,
		[0x0c] = vm_stmt_set_sysvar16_const16_aiw,
		[0x0d] = vm_stmt_set_sysvar16_expr_aiw,
		[0x0e] = stmt_load,
		[0x0f] = stmt_save,
		[0x10] = vm_stmt_jz,
		[0x11] = vm_stmt_defproc,
		[0x12] = vm_stmt_call,
		[0x13] = vm_stmt_defmenu_aiw,
		[0x14] = stmt_menuexec,
		[0x15] = stmt_display_number,
		[0x16] = stmt_set_text_color,
		[0x20] = stmt_wait,
		[0x21] = stmt_text_clear,
		[0x22] = stmt_commit_message,
		[0x23] = stmt_load_image,
		[0x24] = stmt_surface_copy,
		[0x25] = stmt_surface_copy_masked,
		[0x27] = stmt_surface_fill,
		[0x29] = stmt_set_color,
		[0x2a] = stmt_show_hide,
		[0x2b] = stmt_crossfade,
		[0x2c] = stmt_crossfade2,
		[0x2e] = stmt_anim,
		[0x2f] = stmt_load_audio,
		[0x30] = stmt_load_effect,
		[0x31] = stmt_load_voice,
		[0x32] = stmt_audio,
		[0x33] = stmt_play_movie,
	},
	// XXX: no syscall op
	.sys = {0},
	.util = {
		[0] = util_pixel_crossfade,
		[1] = util_plan,
		[2] = util_show_hide_message_window,
		[3] = util_update_schedule,
		// 4 unused
		[5] = util_load_extra_palette,
		[6] = util_photo_slide,
		[7] = util_status_dirty,
		[8] = util_pixel_and_palette_crossfade,
		[9] = util_draw_datetime,
		[10] = util_zoom_movie,
		[11] = util_cam_event_zoom,
		[12] = util_clear_high_colors,
		[13] = util_ending_pixel_crossfade_slow,
		[14] = util_scene_viewer_char_select,
		[15] = util_scene_viewer_scene_select,
		// 16 unused
		[17] = util_scene_viewer_zoom_out,
		[18] = util_name_input,
		[19] = util_draw_myouji,
		[20] = util_draw_namae,
		[21] = util_credits,
		[22] = util_scroll_down,
		[23] = util_pixel_drop,
		[24] = util_anim_wait,
		[25] = util_set_y_offset,
		[26] = util_quake,
		[27] = util_ending_crossfade,
		[29] = util_ending_pixel_crossfade,
		[30] = util_set_config_enabled,
	},
	.flags = {
		[FLAG_ANIM_ENABLE] = FLAG_ALWAYS_ON,
		[FLAG_VOICE_ENABLE] = FLAG_ALWAYS_ON,
		[FLAG_AUDIO_ENABLE] = FLAG_ALWAYS_ON,
		[FLAG_WAIT_KEYUP] = FLAG_ALWAYS_ON,
		[FLAG_LOAD_PALETTE] = 0x0001,
	},
};
