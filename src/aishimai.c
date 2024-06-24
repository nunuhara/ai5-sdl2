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

#include <time.h>

#include "nulib.h"
#include "nulib/string.h"
#include "nulib/utfsjis.h"
#include "ai5.h"
#include "ai5/anim.h"
#include "ai5/mes.h"

#include "anim.h"
#include "audio.h"
#include "backlog.h"
#include "cursor.h"
#include "game.h"
#include "gfx_private.h"
#include "input.h"
#include "savedata.h"
#include "sys.h"
#include "vm_private.h"

#define VAR4_SIZE 2048
#define MEM16_SIZE 4096

#define VAR16_OFF    (MEMORY_MES_NAME_SIZE + VAR4_SIZE + 4)
#define SYSVAR16_OFF (VAR16_OFF + 26 * 2)
#define VAR32_OFF    (SYSVAR16_OFF + 24 * 2)
#define SYSVAR32_OFF (VAR32_OFF + 26 * 4)
#define HEAP_OFF     (SYSVAR32_OFF + 62 * 4)

static void ai_shimai_mem_restore(void)
{
	mem_set_sysvar16_ptr(MEMORY_MES_NAME_SIZE + VAR4_SIZE + 56);
	mem_set_sysvar32(mes_sysvar32_memory, offsetof(struct memory, mem16));
	mem_set_sysvar32(mes_sysvar32_file_data, offsetof(struct memory, file_data));
	mem_set_sysvar32(mes_sysvar32_menu_entry_addresses,
			offsetof(struct memory, menu_entry_addresses));
	mem_set_sysvar32(mes_sysvar32_menu_entry_numbers,
			offsetof(struct memory, menu_entry_numbers));
	mem_set_sysvar32(mes_sysvar32_map_data, 0);

	uint16_t flags = mem_get_sysvar16(mes_sysvar16_flags);
	mem_set_sysvar16(mes_sysvar16_flags, (flags & 0xffbf) | 0x21);
	mem_set_sysvar16(0, HEAP_OFF);
}

static void ai_shimai_mem_init(void)
{
	// set up pointer table for memory access
	// (needed because var4 size changes per game)
	uint32_t off = MEMORY_MES_NAME_SIZE + VAR4_SIZE;
	memory_ptr.system_var16_ptr = memory_raw + off;
	memory_ptr.var16 = memory_raw + VAR16_OFF;
	memory_ptr.system_var16 = memory_raw + SYSVAR16_OFF;
	memory_ptr.var32 = memory_raw + VAR32_OFF;
	memory_ptr.system_var32 = memory_raw + SYSVAR32_OFF;

	mem_set_sysvar16(mes_sysvar16_flags, 0x60f);
	mem_set_sysvar16(mes_sysvar16_text_start_x, 0);
	mem_set_sysvar16(mes_sysvar16_text_start_y, 0);
	mem_set_sysvar16(mes_sysvar16_text_end_x, 640);
	mem_set_sysvar16(mes_sysvar16_text_end_y, 480);
	mem_set_sysvar16(mes_sysvar16_font_width, 16);
	mem_set_sysvar16(mes_sysvar16_font_height, 16);
	mem_set_sysvar16(mes_sysvar16_char_space, 16);
	mem_set_sysvar16(mes_sysvar16_line_space, 16);
	mem_set_sysvar16(mes_sysvar16_mask_color, 0);

	mem_set_sysvar32(mes_sysvar32_cg_offset, 0x20000);
	mem_set_sysvar32(11, 0);
	ai_shimai_mem_restore();
}

// Text Variables:
// ---------------
//
// var4[2001] controls whether "separate"-rendered text is merged in
// System.function[22].function[1]
//   * 1 -> text is merged
//   * !1 -> text is not merged
//
// var4[2002] selects the font.
//   * 0 -> FONT.FNT
//   * 1 -> SELECT1.FNT
//   * 2 -> SELECT2.FNT
//   * 3 -> SELECT3.FNT
//
// (Note that SELECT fonts always use the "merged" rendering mode.)
//
// var4[2017] controls whether the "merged" or "separate" rendering mode is used.
//   * 0 -> use "separate" rendering mode to surface 7
//   * !0 -> use "merged" rendering mode to System.dst_surface
//
// var4[2018] controls whether text is greyscale or redscale
//   * 0 -> greyscale
//   * !0 -> redscale

// XXX: many functions below assume pixel format
_Static_assert(GFX_DIRECT_FORMAT == SDL_PIXELFORMAT_RGB24);

/*
 * Get character index from table.
 */
static int get_char_index(uint16_t ch, uint8_t *table)
{
	uint16_t size = le_get16(table, 0);
	for (unsigned i = 0; i < size; i++) {
		if (le_get16(table, (i + 1) * 2) == ch)
			return i;
	}
	return -1;
}

/*
 * Blend monochrome color data with an RGB24 pixel at a given alpha level.
 */
static void alpha_blend_rgb_mono(uint8_t *bg, uint8_t fg, uint8_t alpha)
{
	uint32_t a = (uint32_t)alpha + 1;
	uint32_t inv_a = 256 - (uint32_t)alpha;
	bg[0] = (uint8_t)((a * fg + inv_a * bg[0]) >> 8);
	bg[1] = (uint8_t)((a * fg + inv_a * bg[1]) >> 8);
	bg[2] = (uint8_t)((a * fg + inv_a * bg[2]) >> 8);
}

/*
 * Blend a BGR24 pixel with an RGB24 pixel at a given alpha level.
 */
static void alpha_blend_rgb_bgr(uint8_t *bg, uint8_t *fg, uint8_t alpha)
{
	uint32_t a = (uint32_t)alpha + 1;
	uint32_t inv_a = 256 - (uint32_t)alpha;
	bg[0] = (uint8_t)((a * fg[2] + inv_a * bg[0]) >> 8);
	bg[1] = (uint8_t)((a * fg[1] + inv_a * bg[1]) >> 8);
	bg[2] = (uint8_t)((a * fg[0] + inv_a * bg[2]) >> 8);
}

/*
 * This is the simple rendering mode, in which the mask and greyscale color data are
 * merged and written directly to a surface.
 */
static void render_char_merged(uint8_t *dst_in, uint8_t *fnt_in, uint8_t *msk_in,
		uint8_t *pal, int char_w, int char_h, int stride)
{
	for (int row = 0; row < char_h; row++) {
		uint8_t *fnt = fnt_in + char_w * row;
		uint8_t *msk = msk_in + char_w * row;
		uint8_t *dst = dst_in + row * stride;
		for (int col = 0; col < char_w; col++, fnt++, msk++, dst += 3) {
			if (*msk == 0)
				continue;
			if (pal) {
				uint8_t alpha = (min(*msk, 15) * 16) - 8;
				uint8_t *c = pal + *fnt * 3;
				alpha_blend_rgb_bgr(dst, c, alpha);
			} else if (*msk > 15) {
				dst[0] = *fnt;
				dst[1] = *fnt;
				dst[2] = *fnt;
			} else {
				uint8_t alpha = (min(*msk, 15) * 16) - 8;
				alpha_blend_rgb_mono(dst, *fnt, alpha);
			}
		}
	}
}

/*
 * "Redscale" rendering mode. This mode is like the "merged" mode, except that only the
 * red channel is blended. The green and blue channels are set to zero whenever the
 * mask is non-zero.
 */
static void render_char_redscale(uint8_t *dst_in, uint8_t *fnt_in, uint8_t *msk_in,
		uint8_t *pal, int char_w, int char_h, int stride)
{
	for (int row = 0; row < char_h; row++) {
		uint8_t *fnt = fnt_in + char_w * row;
		uint8_t *msk = msk_in + char_w * row;
		uint8_t *dst = dst_in + row * stride;
		for (int col = 0; col < char_w; col++, fnt++, msk++, dst += 3) {
			if (*msk == 0)
				continue;
			if (*msk > 15) {
				dst[0] = *fnt;
			} else {
				uint8_t alpha = (min(*msk, 15) * 16) - 8;
				alpha_blend_rgb_mono(dst, *fnt, alpha);
			}
			dst[1] = 0;
			dst[2] = 0;
		}
	}
}

/*
 * In this rendering mode, the greyscale color data is written at the text cursor, and
 * the mask data is written 256 lines below the cursor. Merging the two is a separate
 * operation.
 */
static void render_char_separate(uint8_t *dst_in, uint8_t *fnt_in, uint8_t *msk_in,
		uint8_t *pal, int char_w, int char_h, int stride)
{
	for (int row = 0; row < char_h; row++) {
		uint8_t *fnt = fnt_in + char_w * row;
		uint8_t *msk = msk_in + char_w * row;
		uint8_t *fnt_dst = dst_in + row * stride;
		uint8_t *msk_dst = dst_in + (row + 256) * stride;
		for (int col = 0; col < char_w; col++, fnt++, msk++, fnt_dst += 3, msk_dst += 3) {
			if (*fnt) {
				fnt_dst[0] = *fnt;
				fnt_dst[1] = *fnt;
				fnt_dst[2] = *fnt;
			}
			if (*msk) {
				msk_dst[0] = *msk;
				msk_dst[1] = *msk;
				msk_dst[2] = *msk;
			}
		}
	}
}

struct render_text_params {
	int char_w, char_h;
	unsigned surface;
	void (*render_char)(uint8_t*,uint8_t*,uint8_t*,uint8_t*,int,int,int);
	uint8_t *font_tbl;
	uint8_t *font_msk;
	uint8_t *font_fnt;
	uint8_t *font_pal;
};

/*
 * Render a string according to the given parameters.
 */
static void render_text(const char *txt, struct render_text_params *p)
{
	const uint16_t start_x = mem_get_sysvar16(mes_sysvar16_text_start_x);
	const uint16_t end_x = mem_get_sysvar16(mes_sysvar16_text_end_x);
	const uint16_t char_space = mem_get_sysvar16(mes_sysvar16_char_space);
	const uint16_t line_space = mem_get_sysvar16(mes_sysvar16_line_space);
	uint16_t x = mem_get_sysvar16(mes_sysvar16_text_cursor_x);
	uint16_t y = mem_get_sysvar16(mes_sysvar16_text_cursor_y);

	SDL_Surface *surf = gfx_get_surface(p->surface);
	if (SDL_MUSTLOCK(surf))
		SDL_CALL(SDL_LockSurface, surf);

	for (; *txt; txt += 2) {
		// get index of char in font data
		uint16_t char_code = le_get16((uint8_t*)txt, 0);
		int char_i = get_char_index(char_code, p->font_tbl);
		if (char_i < 0) {
			WARNING("Invalid character: %04x", char_code);
			continue;
		}

		uint8_t *char_msk = p->font_msk + (char_i * p->char_w * p->char_h);
		uint8_t *char_fnt = p->font_fnt + (char_i * p->char_w * p->char_h);
		uint8_t *dst = surf->pixels + y * surf->pitch + x * 3;
		p->render_char(dst, char_fnt, char_msk, p->font_pal, p->char_w, p->char_h, surf->pitch);

		x += char_space;
		if (x + char_space > end_x) {
			y += line_space;
			x = start_x;
		}
	}

	mem_set_sysvar16(mes_sysvar16_text_cursor_x, x);
	mem_set_sysvar16(mes_sysvar16_text_cursor_y, y);

	if (SDL_MUSTLOCK(surf))
		SDL_UnlockSurface(surf);

	gfx_dirty(p->surface);
}

/*
 * Render a string using one of the SELECT fonts.
 */
static void render_text_select(const char *txt)
{
	int sel = mem_get_var4(2002);
	if (sel < 1 || sel > 3) {
		WARNING("Invalid SELECT font index: %d", sel);
		return;
	}
	struct render_text_params p = {
		.char_w = sel == 2 ? 49 : 47,
		.char_h = sel == 2 ? 49 : 47,
		.surface = mem_get_sysvar16(mes_sysvar16_dst_surface),
		.render_char = render_char_merged,
		.font_tbl = memory.file_data + mem_get_var32(3),
		.font_msk = memory.file_data + mem_get_var32(5 + (sel - 1) * 3),
		.font_fnt = memory.file_data + mem_get_var32(6 + (sel - 1) * 3),
		.font_pal = memory.file_data + mem_get_var32(4 + (sel - 1) * 3)
	};
	render_text(txt, &p);
}

/*
 * Custom TXT function.
 */
static void ai_shimai_TXT(const char *txt)
{
	if (mem_get_var4(2002) != 0) {
		if (mem_get_var4(2002) < 4)
			render_text_select(txt);
		else {
			vm_draw_text(txt);
		}
		return;
	}

	bool render_merged = mem_get_var4(2017) != 0;
	bool render_redscale = mem_get_var4(2018) != 0;
	struct render_text_params p = {
		.char_w = 28,
		.char_h = 28,
		.surface = render_merged ? mem_get_sysvar16(mes_sysvar16_dst_surface) : 7,
		.render_char = render_redscale ? render_char_redscale :
				render_merged ? render_char_merged :
				render_char_separate,
		.font_tbl = memory.file_data + mem_get_var32(0),
		.font_msk = memory.file_data + mem_get_var32(1),
		.font_fnt = memory.file_data + mem_get_var32(2),
		.font_pal = NULL,
	};
	render_text(txt, &p);
}

static void ai_shimai_cursor(struct param_list *params)
{
	static unsigned cursor1_frame_time[4] = { 200, 200, 200, 500 };
	switch (vm_expr_param(params, 0)) {
	case 0: cursor_show(); break;
	case 1: cursor_hide(); break;
	case 2: sys_cursor_save_pos(params); break;
	case 3: cursor_set_pos(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 4:
		switch (vm_expr_param(params, 1)) {
		case 0: cursor_unload(); break;
		case 1: cursor_load(0, 4, cursor1_frame_time); break;
		case 2: cursor_load(4, 2, NULL); break;
		default: WARNING("Invalid cursor number: %u", vm_expr_param(params, 1));
		}
		break;
	case 5: cursor_set_direction(CURSOR_DIR_NONE);
	case 6:
		mem_set_var32(18, cursor_get_direction());
		cursor_set_direction(CURSOR_DIR_NONE);
		break;
	default: WARNING("System.Cursor.function[%u] not implemented",
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

static void ai_shimai_anim(struct param_list *params)
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
	//case 8: TODO
	default: WARNING("System.Anim.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void ai_shimai_resume_load(const char *save_name)
{
	uint8_t buf[MEMORY_VAR4_OFFSET + VAR4_SIZE];
	uint8_t *var4 = buf + MEMORY_VAR4_OFFSET;
	uint8_t *mem_var4 = memory_raw + MEMORY_VAR4_OFFSET;
	savedata_read(save_name, memory_raw, 0, MEM16_SIZE);
	savedata_read("FLAG00", buf, MEMORY_VAR4_OFFSET, VAR4_SIZE);

	memcpy(mem_var4 + 700, var4 + 700, 181);
	memcpy(mem_var4 + 1065, var4 + 1065, 735);
	mem_var4[2005] = var4[2005];
	mem_var4[2009] = var4[2009];

	ai_shimai_mem_restore();
	vm_load_mes(mem_mes_name());
	vm_flag_on(FLAG_RETURN);
}

static void ai_shimai_load_var4(const char *save_name)
{
	savedata_load_var4(save_name);
	ai_shimai_mem_restore();
}

static void ai_shimai_load_extra_var32(const char *save_name)
{
	// sysvar32[12] -> sysvar32[61]
	savedata_read(save_name, memory_raw, SYSVAR32_OFF + (12 * 4), 50 * 4);
}

static void ai_shimai_save_extra_var32(const char *save_name)
{
	// sysvar32[12] -> sysvar32[61]
	savedata_write(save_name, memory_raw, SYSVAR32_OFF + (12 * 4), 50 * 4);
}

static void ai_shimai_load_heap(const char *save_name, int start, int count)
{
	if (count <= 0 || start < 0 || start + count > 1464) {
		WARNING("Invalid heap load: %d+%d", start, count);
		return;
	}
	savedata_read(save_name, memory_raw, HEAP_OFF + start, count);
}

static void ai_shimai_save_heap(const char *save_name, int start, int count)
{
	if (count <= 0 || start < 0 || start + count > 1464) {
		WARNING("Invalid heap save: %d+%d", start, count);
		return;
	}
	savedata_write(save_name, memory_raw, HEAP_OFF + start, count);
}

static void ai_shimai_savedata(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: ai_shimai_resume_load(sys_save_name(params)); break;
	case 1: savedata_resume_save(sys_save_name(params)); break;
	case 2: ai_shimai_load_var4(sys_save_name(params)); break;
	case 3: savedata_save_union_var4(sys_save_name(params)); break;
	case 4: ai_shimai_load_extra_var32(sys_save_name(params)); break;
	case 5: ai_shimai_save_extra_var32(sys_save_name(params)); break;
	case 6: memset(memory_raw + MEMORY_VAR4_OFFSET, 0, VAR4_SIZE); break;
	case 7: ai_shimai_load_heap(sys_save_name(params), vm_expr_param(params, 2),
				vm_expr_param(params, 3)); break;
	case 8: ai_shimai_save_heap(sys_save_name(params), vm_expr_param(params, 2),
				vm_expr_param(params, 3)); break;
	default: VM_ERROR("System.SaveData.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void ai_shimai_audio(struct param_list *params)
{
	static bool have_next = false;
	static char next[128] = {0};
	switch (vm_expr_param(params, 0)) {
	case 0: audio_bgm_play(vm_string_param(params, 1), true); break;
	case 1: audio_stop(AUDIO_CH_BGM); break;
	case 2: audio_fade(AUDIO_CH_BGM, AUDIO_VOLUME_MIN, 3000, true, false); break;
	case 3: audio_fade(AUDIO_CH_BGM, AUDIO_VOLUME_MIN, 3000, true, true); break;
	case 4:
		strcpy(next, vm_string_param(params, 1));
		have_next = true;
		break;
	case 5:
		if (have_next) {
			audio_bgm_play(next, true);
			have_next = false;
		}
		break;
	case 6: audio_se_play(vm_string_param(params, 1), vm_expr_param(params, 2)); break;
	case 7: audio_se_stop(vm_expr_param(params, 1)); break;
	case 8: audio_se_fade(AUDIO_VOLUME_MIN, 3000, true, false, vm_expr_param(params, 1)); break;
	case 9: audio_se_fade(AUDIO_VOLUME_MIN, 3000, true, true, vm_expr_param(params, 1)); break;
	default: VM_ERROR("System.Audio.function[%u] not implemented",
				 params->params[0].val);
	}
}

static char prepared_voice[STRING_PARAM_SIZE] = {0};
static bool have_prepared_voice = false;

static void ai_shimai_voice(struct param_list *params)
{

	if (!vm_flag_is_on(FLAG_VOICE_ENABLE))
		return;
	if (params->nr_params > 2 && vm_expr_param(params, 2) != 0) {
		switch (vm_expr_param(params, 0)) {
		case 0: audio_voicesub_play(vm_string_param(params, 1)); break;
		case 1: audio_voicesub_stop(); break;
		case 5: mem_set_var32(18, audio_voicesub_is_playing()); break;
		default: WARNING("System.Voice(sub).function[%u] not implemented",
					 params->params[0].val);
		}
		return;
	}
	switch (vm_expr_param(params, 0)) {
	case 0: audio_voice_play(vm_string_param(params, 1), 0); break;
	case 1: audio_stop(AUDIO_CH_VOICE0); break;
	//case 2: audio_voice_play_sync(vm_string_param(params, 1)); break;
	case 3:
		if (vm_flag_is_on(FLAG_LOG))
			backlog_set_has_voice();
		strcpy(prepared_voice, vm_string_param(params, 1));
		have_prepared_voice = true;
		break;
	case 4:
		if (vm_flag_is_on(FLAG_LOG))
			backlog_set_has_voice();
		if (have_prepared_voice) {
			audio_voice_play(prepared_voice, 0);
			have_prepared_voice = 0;
			mem_set_var32(18, 1);
		} else {
			mem_set_var32(18, 0);
		}
		break;
	case 5:
		mem_set_var32(18, audio_is_playing(AUDIO_CH_VOICE0));
		break;
	default: WARNING("System.Voice.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void ai_shimai_display(struct param_list *params)
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
			gfx_display_fade_out(vm_expr_param(params, 1), 250);
		} else {
			gfx_display_fade_in(250);
		}
		break;
	default: VM_ERROR("System.Display.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void ai_shimai_graphics(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: sys_graphics_copy(params); break;
	case 1: sys_graphics_copy_masked24(params); break;
	case 2: sys_graphics_fill_bg(params); break;
	case 4: sys_graphics_swap_bg_fg(params); break;
	case 6: {
		vm_timer_t timer = vm_timer_create();
		sys_graphics_blend(params);
		// XXX: The game calls this function in a loop to implement a
		//      crossfade effect. We throttle it here so that the
		//      effect is visible on modern systems.
		if (!input_down(INPUT_CTRL))
			vm_timer_tick(&timer, config.progressive_frame_time * 4);
		break;
	}
	case 7: sys_graphics_blend_masked(params); break;
	default: VM_ERROR("System.Graphics.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void ai_shimai_get_time(struct param_list *params)
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

static void ai_shimai_backlog(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: backlog_clear(); break;
	case 1: backlog_prepare(); break;
	case 2: backlog_commit(); break;
	case 3: mem_set_var32(18, backlog_count()); break;
	case 4: mem_set_var32(18, backlog_get_pointer(vm_expr_param(params, 1))); break;
	case 5: mem_set_var16(18, backlog_has_voice(vm_expr_param(params, 1))); break;
	default: WARNING("System.Backlog.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void update_text(struct param_list *params)
{
	if (mem_get_var4(2001) != 1)
		return;

	SDL_Surface *src = gfx_get_surface(7);
	SDL_Surface *dst = gfx_get_overlay();
	if (SDL_MUSTLOCK(src))
		SDL_CALL(SDL_LockSurface, src);
	if (SDL_MUSTLOCK(dst))
		SDL_CALL(SDL_LockSurface, dst);

	// clear overlay
	SDL_Rect r = { 0, 336, 640, 128 };
	SDL_CALL(SDL_FillRect, dst, &r, SDL_MapRGBA(dst->format, 0, 0, 0, 0));

	// merge color/mask from surface 7 and write to overlay surface
	// color data is at (0,   0) -> (640, 128) on surface 7
	// mask data is at  (0, 256) -> (640, 384) on surface 7
	// destination is   (0, 336) -> (640, 464) on overlay
	for (int row = 0; row < 128; row++) {
		uint8_t *fnt = src->pixels + row * src->pitch;
		uint8_t *msk = src->pixels + (row + 256) * src->pitch;
		uint8_t *p = dst->pixels + (row + 336) * dst->pitch;
		for (int col = 0; col < 640; col++, fnt += 3, msk += 3, p += 4) {
			// XXX: only blue channel matters for mask
			if (msk[2] == 0)
				continue;
			// ???: do all channels get copied here?
			p[0] = fnt[0];
			p[1] = fnt[1];
			p[2] = fnt[2];
			if (msk[2] > 15) {
				p[3] = 255;
			} else {
				p[3] = *msk * 16 - 8;
			}
		}
	}

	if (SDL_MUSTLOCK(src))
		SDL_UnlockSurface(src);
	if (SDL_MUSTLOCK(dst))
		SDL_UnlockSurface(dst);

	// TODO: mark (x,y,w,h) as dirty
	//int x = vm_expr_param(params, 2);
	//int y = vm_expr_param(params, 3);
	//int w = vm_expr_param(params, 4);
	//int h = vm_expr_param(params, 5);
	gfx_screen_dirty();
}

static void clear_text(struct param_list *params)
{
	SDL_Surface *dst = gfx_get_overlay();
	SDL_Rect r = { 0, 336, 640, 128 };
	SDL_CALL(SDL_FillRect, dst, &r, SDL_MapRGBA(dst->format, 0, 0, 0, 0));

	// TODO: mark (x,y,w,h) as dirty
	//int x = vm_expr_param(params, 2);
	//int y = vm_expr_param(params, 3);
	//int w = vm_expr_param(params, 4);
	//int h = vm_expr_param(params, 5);
	gfx_screen_dirty();

}

static void sys_22(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 1:  update_text(params); break;
	case 2:  clear_text(params); break;
	default: WARNING("System.function[22].function[%u] not implemented",
				 params->params[0].val);
	}
}

#define IME_BUF_LEN 1024
static uint8_t ime_buf[IME_BUF_LEN] = {0};
static unsigned ime_cursor_pos = 0;
static bool ime_cursor_inside = false;
static bool ime_enabled = false;
static bool ime_composition_started = false;
static bool ime_composition_finished = false;

#if 0
#define IME_LOG(...) NOTICE(__VA_ARGS__)
#else
#define IME_LOG(...)
#endif

static void ime_reset(void)
{
	ime_enabled = true;
	ime_composition_started = false;
	ime_composition_finished = false;
}

static void ime_enable(void)
{
	IME_LOG("ime_enable()");
	ime_reset();
	memset(ime_buf, 0, sizeof(ime_buf));
	SDL_StartTextInput();
}

static void ime_disable(void)
{
	IME_LOG("ime_disable()");
	SDL_StopTextInput();
	ime_reset();
}

/*
 * Calculate the byte-offset for a (character-indexed) cursor position
 * in a sjis string.
 */
static unsigned calc_cursor_pos(const char *sjis, unsigned cursor)
{
	unsigned len = 0;
	for (unsigned i = 0; i < cursor && *sjis; i++) {
		if (SJIS_2BYTE(*sjis)) {
			len += 2;
			sjis += 2;
		} else {
			len++;
			sjis++;
		}
	}
	return len;
}

static uint16_t hanzen_table[256] = {
	['a'] = 0x8281, ['A'] = 0x8260,
	['b'] = 0x8282, ['B'] = 0x8261,
	['c'] = 0x8283, ['C'] = 0x8262,
	['d'] = 0x8284, ['D'] = 0x8263,
	['e'] = 0x8285, ['E'] = 0x8264,
	['f'] = 0x8286, ['F'] = 0x8265,
	['g'] = 0x8287, ['G'] = 0x8266,
	['h'] = 0x8288, ['H'] = 0x8267,
	['i'] = 0x8289, ['I'] = 0x8268,
	['j'] = 0x828a, ['J'] = 0x8269,
	['k'] = 0x828b, ['K'] = 0x826a,
	['l'] = 0x828c, ['L'] = 0x826b,
	['m'] = 0x828d, ['M'] = 0x826c,
	['n'] = 0x828e, ['N'] = 0x826d,
	['o'] = 0x828f, ['O'] = 0x826e,
	['p'] = 0x8290, ['P'] = 0x826f,
	['q'] = 0x8291, ['Q'] = 0x8270,
	['r'] = 0x8292, ['R'] = 0x8271,
	['s'] = 0x8293, ['S'] = 0x8272,
	['t'] = 0x8294, ['T'] = 0x8273,
	['u'] = 0x8295, ['U'] = 0x8274,
	['v'] = 0x8296, ['V'] = 0x8275,
	['w'] = 0x8297, ['W'] = 0x8276,
	['x'] = 0x8298, ['X'] = 0x8277,
	['y'] = 0x8299, ['Y'] = 0x8278,
	['z'] = 0x829a, ['Z'] = 0x8279,
	[0xa1] = 0x8142,
	[0xa2] = 0x8175,
	[0xa3] = 0x8176,
	[0xa4] = 0x8141,
	[0xa5] = 0x8145,
	[0xa6] = 0x8392,
	[0xa7] = 0x8340,
	[0xa8] = 0x8342,
	[0xa9] = 0x8344,
	[0xaa] = 0x8346,
	[0xab] = 0x8348,
	[0xac] = 0x8383,
	[0xad] = 0x8385,
	[0xae] = 0x8387,
	[0xaf] = 0x8362,
	[0xb0] = 0x815b,
	[0xb1] = 0x8341,
	[0xb2] = 0x8343,
	[0xb3] = 0x8345,
	[0xb4] = 0x8347,
	[0xb5] = 0x8349,
	[0xb6] = 0x834a,
	[0xb7] = 0x834c,
	[0xb8] = 0x834e,
	[0xb9] = 0x8350,
	[0xba] = 0x8352,
	[0xbb] = 0x8354,
	[0xbc] = 0x8356,
	[0xbd] = 0x8358,
	[0xbe] = 0x835a,
	[0xbf] = 0x835c,
	[0xc0] = 0x835e,
	[0xc1] = 0x8360,
	[0xc2] = 0x8363,
	[0xc3] = 0x8365,
	[0xc4] = 0x8367,
	[0xc5] = 0x8369,
	[0xc6] = 0x836a,
	[0xc7] = 0x836b,
	[0xc8] = 0x836c,
	[0xc9] = 0x836d,
	[0xca] = 0x836e,
	[0xcb] = 0x8371,
	[0xcc] = 0x8374,
	[0xcd] = 0x8377,
	[0xce] = 0x837a,
	[0xcf] = 0x837d,
	[0xd0] = 0x837e,
	[0xd1] = 0x8380,
	[0xd2] = 0x8381,
	[0xd3] = 0x8382,
	[0xd4] = 0x8384,
	[0xd5] = 0x8386,
	[0xd6] = 0x8388,
	[0xd7] = 0x8389,
	[0xd8] = 0x838a,
	[0xd9] = 0x838b,
	[0xda] = 0x838c,
	[0xdb] = 0x838d,
	[0xdc] = 0x838f,
	[0xdd] = 0x8393,
	[0xde] = 0x814a,
	[0xdf] = 0x814b,
};

/*
 * Convert hankaku characters to zenkaku.
 */
string ime_convert_zenkaku(string in)
{
	unsigned han_count = 0;
	for (char *p = in; *p; p++) {
		if (!SJIS_2BYTE(*p))
			han_count++;
		else
			p++;
	}
	if (han_count == 0)
		return in;

	string out = string_new_len(NULL, string_length(in) + han_count);
	for (char *p_in = in, *p_out = out; *p_in; p_in++, p_out += 2) {
		if (!SJIS_2BYTE(*p_in)) {
			if (hanzen_table[(uint8_t)*p_in]) {
				p_out[0] = hanzen_table[(uint8_t)*p_in] >> 8;
				p_out[1] = hanzen_table[(uint8_t)*p_in] & 0xff;
			} else {
				p_out[0] = 0x81;
				p_out[1] = 0x48;
			}
		} else {
			p_out[0] = p_in[0];
			p_out[1] = p_in[1];
			p_in++;
		}
	}
	string_free(in);
	return out;
}

static void _ime_set_text(string sjis)
{
	size_t len = min(1023, string_length(sjis));
	memcpy((char*)ime_buf, sjis, len);
	ime_buf[len] = '\0';
}

/*
 * Set the intermediate composition text.
 */
static void ime_set_text(const char *utf8, int cursor)
{
	string sjis = utf8_cstring_to_sjis(utf8, 0);
	ime_cursor_pos = calc_cursor_pos(sjis, cursor);
	ime_cursor_inside = sjis[ime_cursor_pos] != '\0';
	IME_LOG("ime_set_text(\"%s\", %u, %s)", sjis, ime_cursor_pos, ime_cursor_inside ? "true" : "false");
	_ime_set_text(sjis);
}

/*
 * Finalize the composition text.
 */
static void ime_commit_text(const char *utf8)
{
	string sjis = ime_convert_zenkaku(utf8_cstring_to_sjis(utf8, 0));
	ime_composition_started = false;
	ime_composition_finished = true;
	ime_cursor_pos = 0;
	ime_cursor_inside = false;
	IME_LOG("ime_commit_text(\"%s\")", sjis);
	_ime_set_text(sjis);
}

static void sys_ime_get_text(struct param_list *params)
{
	uint8_t *out = memory_raw + vm_expr_param(params, 1);
	unsigned out_len = vm_expr_param(params, 2);
	if (!mem_ptr_valid(out, out_len + 2))
		VM_ERROR("Invalid output buffer for System.IME.get_string");
	if (!memchr(out, 0, out_len + 2)) {
		WARNING("Output buffer is not null-terminated");
		out[out_len] = 0;
		out[out_len + 1] = 0;
	}

	int32_t ord = strcmp((char*)out, (char*)ime_buf);
	size_t copy_len = min(out_len, strlen((char*)ime_buf));
	memcpy(out, ime_buf, copy_len);
	out[copy_len] = 0;
	out[copy_len + 1] = 0;
	ime_buf[copy_len] = 0;
	ime_buf[copy_len + 1] = 0;

	mem_set_var16(18, copy_len);
	mem_set_var32(18, ord);

	if (ord)
		IME_LOG("ime_get_text(...) -> %s, %u, %d", (char*)out, (unsigned)copy_len, ord);
}

static void sys_ime_strcmp(struct param_list *params)
{
	char *str = mem_get_cstring(vm_expr_param(params, 1));
	mem_set_var16(18, strcmp(str, (char*)ime_buf));
	//IME_LOG("ime_strcmp(...) -> %u", mem_get_var16(18));
}

static void ai_shimai_ime(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: ime_enable(); break;
	case 1: ime_disable(); break;
	case 2: mem_set_var16(18, ime_composition_started); break;
	case 3: sys_ime_get_text(params); break;
	case 4: mem_set_var16(18, ime_cursor_inside); break; // ???
	case 5: mem_set_var16(18, ime_cursor_pos); break;
	case 6: sys_ime_strcmp(params); break;
	case 7: mem_set_var16(18, ime_composition_finished ? 2 : 0); break;
	}
}

static void util_shift_screen(struct param_list *params)
{
	int32_t x = vm_expr_param(params, 1);
	int32_t y = vm_expr_param(params, 2);

	vm_timer_t timer = vm_timer_create();
	SDL_Rect src_r = { 0, 0, 640, 480 };
	SDL_Rect dst_r = { x, y, 640, 480 };
	SDL_CALL(SDL_RenderCopy, gfx.renderer, gfx.texture, &src_r, &dst_r);
	SDL_RenderPresent(gfx.renderer);
	vm_timer_tick(&timer, 16);
}

static void util_copy_to_surface_7(struct param_list *params)
{
	// XXX: this is dead code...? Only used in MES.ARC:DEFMAIN.MES,
	//      NOT in DATA.ARC:DEFMAIN.MES (which is the real DEFMAIN).
	int x = vm_expr_param(params, 1);
	int y = vm_expr_param(params, 2);
	int w = vm_expr_param(params, 3);
	int h = vm_expr_param(params, 4);
	gfx_copy(x, y, w, h, 0, 0, 0, 7);
}

static void util_strcpy(struct param_list *params)
{
	uint8_t *dst = memory_raw + vm_expr_param(params, 1);
	char *src = mem_get_cstring(vm_expr_param(params, 2));
	if (!mem_ptr_valid(dst, strlen(src) + 1))
		VM_ERROR("Invalid destination for strcpy");
	strcpy((char*)dst, src);
}

static void util_strcpy2(struct param_list *params)
{
	uint8_t *src = memory_raw + vm_expr_param(params, 1);
	uint8_t *dst = memory_raw + vm_expr_param(params, 2);
	unsigned count = vm_expr_param(params, 3);
	unsigned off = vm_expr_param(params, 4);

	if (!mem_ptr_valid(src + off + 1, count))
		VM_ERROR("Invalid source for strcpy2");
	if (!mem_ptr_valid(dst, count + 2))
		VM_ERROR("Invalid destination for strcpy2");

	// don't read from 2nd byte of zenkaku character
	if (off > 1 && (off & 1) && mes_char_is_zenkaku(src[off - 1]))
		off++;
	src = src + off;

	unsigned i;
	for (i = 0; i < count && src[i]; i++) {
		dst[i] = src[i];
		if (mes_char_is_zenkaku(src[i])) {
			i++;
			dst[i] = src[i];
		}
	}
	i = min(i, count);
	dst[i] = 0;
	dst[i+1] = 0;

	mem_set_var16(18, i);
	mem_set_var32(18, off);
}

static void util_location_index(struct param_list *params)
{
	static const char *out_loc[5] = {
		"\x8a\x77\x8d\x5a",         // 学校
		"\x96\x6b\x91\xf2\x89\xc6", // 北沢家
		"\x8e\x96\x96\xb1\x8f\x8a", // 事務所
		"\x96\xec\x90\xec\x89\xc6", // 野川家
		NULL
	};
	static const char *school_loc[6] = {
		"\x8a\x77\x8d\x5a\x82\xcc\x8a\x4f",         //"学校の外",
		"\x91\xcc\x88\xe7\x8f\x80\x94\xf5\x8e\xba", //"体育準備室",
		"\x94\xfc\x8f\x70\x8f\x80\x94\xf5\x8e\xba", //"美術準備室",
		"\x8d\x5a\x8e\xc9\x82\xcc\x89\xae\x8f\xe3", //"校舎の屋上",
		"\x8b\xb3\x8e\xba",                         //"教室",
		NULL
	};
	const char **options = vm_expr_param(params, 1) ? school_loc : out_loc;
	const char *loc = mem_get_cstring(vm_expr_param(params, 2) + 1);
	if (!loc)
		VM_ERROR("Invalid cstring parameter");

	for (int i = 0; options[i]; i++) {
		if (!strcmp(options[i], loc)) {
			mem_set_var16(18, i);
			return;
		}
	}
	mem_set_var16(18, 255);
}

static void util_location_zoom(struct param_list *params)
{
	static const unsigned out_coords[5][2] = {
		{  60, 8   },
		{ 129, 320 },
		{ 416, 312 },
		{ 368, 40  },
		{   0, 0   },
	};
	static const unsigned school_coords[5][2] = {
		{ 452, 336 },
		{  20, 304 },
		{ 436, 32  },
		{   8, 8   },
		{ 216, 204 },
	};

	// get coordinates of zoom area
	unsigned x, y;
	unsigned school = vm_expr_param(params, 1);
	unsigned loc = vm_expr_param(params, 2);
	if ((!school && loc > 3) || loc > 4) {
		WARNING("Invalid location index: %u:%u", school, loc);
		return;
	}
	if (school) {
		x = school_coords[loc][0];
		y = school_coords[loc][1];
	} else {
		x = out_coords[loc][0];
		y = out_coords[loc][1];
	}

	gfx_zoom(x, y, 180, 136, 2, 0, 350);
}

static void util_get_mess(struct param_list *params)
{
	// TODO: return "CONFIG.MESS" value from ini
	mem_set_var32(18, 0);
}

static void util_write_backlog_header(struct param_list *params)
{
	backlog_push_byte(16);
	backlog_push_byte(2);
	backlog_push_byte(8);
	backlog_push_byte(255);
	backlog_push_byte(0);
}

static void util_line(struct param_list *params)
{
	uint16_t cursor_y = mem_get_sysvar16(mes_sysvar16_text_cursor_y);
	uint16_t line_space = mem_get_sysvar16(mes_sysvar16_line_space);
	uint16_t start_x = mem_get_sysvar16(mes_sysvar16_text_start_x);
	mem_set_sysvar16(mes_sysvar16_text_cursor_y, cursor_y + line_space);
	mem_set_sysvar16(mes_sysvar16_text_cursor_x, start_x);
}

static void util_save_voice(struct param_list *params)
{
	WARNING("Util.save_voice not implemented");
}

static void util_quit(struct param_list *params)
{
	if (gfx_confirm_quit())
		sys_exit(0);
}

static void util_get_imode(struct param_list *params)
{
	// TODO: return "CONFIG.IMODE" value from ini
	mem_set_var32(18, 0);
}

static void util_set_prepared_voice(struct param_list *params)
{
	have_prepared_voice = !!vm_expr_param(params, 1);
}

static void util_cgmode_zoom(struct param_list *params)
{
	unsigned x = vm_expr_param(params, 1);
	unsigned y = vm_expr_param(params, 2);
	gfx_zoom(x, y, 160, 120, 5, 0, 350);
}

static void util_scroll(struct param_list *params)
{
	int end = vm_expr_param(params, 1);
	if (end > 1280) {
		WARNING("Invalid end argument to Util.scroll: %d", end);
		return;
	}
	end = -end;

	vm_timer_t timer = vm_timer_create();
	SDL_Surface *src = gfx_get_surface(1);
	SDL_Surface *dst = gfx_get_surface(0);
	SDL_Rect src_r = { 0, 0, 400, 1280 };
	for (int y = 479; y >= end; y--) {
		SDL_Rect dst_r = { 120, y, 400, 1280 - y };
		SDL_CALL(SDL_BlitSurface, src, &src_r, dst, &dst_r);

		gfx_dirty(0);
		vm_peek();
		if (input_down(INPUT_CANCEL)) {
			mem_set_var32(18, 1);
			return;
		}
		vm_timer_tick(&timer, 16);
	}
	mem_set_var32(18, 0);
}

static void util_15(struct param_list *params)
{
	WARNING("Util.function[15] not implemented");
}

static void util_get_cut(struct param_list *params)
{
	// TODO: return "CONFIG.CUT" value from ini
	mem_set_var32(18, 1);
}

static void ai_shimai_init(void)
{
	gfx_text_set_colors(0, 0xffffff);
}

static void ai_shimai_handle_event(SDL_Event *e)
{
	if (!ime_enabled)
		return;
	switch (e->type) {
	case SDL_TEXTINPUT:
		IME_LOG("ime_text_input_event(...)");
		ime_commit_text(e->text.text);
		break;
	case SDL_TEXTEDITING:
		// XXX: ignore spurious editing events
		if (!ime_composition_started && !e->edit.text[0])
			break;
		IME_LOG("ime_text_editing_event(...)");
		if (!e->edit.text[0]) {
			// XXX: end composition when compstr is empty
			ime_commit_text(e->edit.text);
		} else {
			ime_composition_started = true;
			ime_set_text(e->edit.text, e->edit.start);
		}
		break;
	}
}

struct game game_ai_shimai = {
	.id = GAME_AI_SHIMAI,
	.surface_sizes = {
		{ 640, 480 },
		{ 640, 1280 },
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 480 },
		{ 640, 512 },
		{ 864, 468 },
		{ 720, 680 },
		{ 640, 480 },
		{ 0, 0 }
	},
	.bpp = 24,
	.x_mult = 1,
	.use_effect_arc = false,
	.call_saves_procedures = false,
	.proc_clears_flag = true,
	.var4_size = VAR4_SIZE,
	.mem16_size = MEM16_SIZE,
	.handle_event = ai_shimai_handle_event,
	.mem_init = ai_shimai_mem_init,
	.mem_restore = ai_shimai_mem_restore,
	.init = ai_shimai_init,
	.custom_TXT = ai_shimai_TXT,
	.sys = {
		[0]  = sys_set_font_size,
		[1]  = sys_display_number,
		[2]  = ai_shimai_cursor,
		[3]  = ai_shimai_anim,
		[4]  = ai_shimai_savedata,
		[5]  = ai_shimai_audio,
		[6]  = ai_shimai_voice,
		[7]  = sys_file,
		[8]  = sys_load_image,
		[9]  = ai_shimai_display,
		[10] = ai_shimai_graphics,
		[11] = sys_wait,
		[12] = sys_set_text_colors_direct,
		[13] = sys_farcall,
		[14] = sys_get_cursor_segment,
		[15] = sys_menu_get_no,
		[16] = ai_shimai_get_time,
		[17] = NULL,
		[18] = sys_check_input,
		[19] = ai_shimai_backlog,
		[20] = NULL,
		[21] = sys_strlen,
		[22] = sys_22,
		[23] = ai_shimai_ime,
	},
	.util = {
		[0] = util_shift_screen,
		[1] = util_copy_to_surface_7,
		[2] = util_strcpy,
		[3] = util_strcpy2,
		[4] = util_location_index,
		[5] = util_location_zoom,
		[6] = util_get_mess,
		[7] = util_write_backlog_header,
		[8] = util_line,
		[9] = util_save_voice,
		[10] = util_quit,
		[11] = util_get_imode,
		[12] = util_set_prepared_voice,
		[13] = util_cgmode_zoom,
		[14] = util_scroll,
		[15] = util_15,
		[16] = util_get_cut,
	},
	.flags = {
		[FLAG_ANIM_ENABLE]  = 0x0004,
		[FLAG_MENU_RETURN]  = 0x0008,
		[FLAG_RETURN]       = 0x0010,
		[FLAG_LOG_ENABLE]   = 0x0020,
		[FLAG_LOG_TEXT]     = 0x0040,
		[FLAG_LOG]          = 0x0080,
		[FLAG_VOICE_ENABLE] = 0x0100,
		[FLAG_LOG_SYS]      = 0x1000,
		[FLAG_WAIT_KEYUP]   = FLAG_ALWAYS_ON,
	}
};
