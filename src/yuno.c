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

#include <stdlib.h>
#include <SDL.h>

#include "nulib.h"
#include "ai5/arc.h"
#include "ai5/cg.h"

#include "asset.h"
#include "game.h"
#include "gfx_private.h"
#include "sys.h"
#include "util.h"
#include "vm.h"

#define VAR4_SIZE 4096
#define MEM16_SIZE 8192

static void yuno_mem_restore(void)
{
	// XXX: In AI5WIN.EXE, these are 32-bit pointers into the VM's own
	//      address space. Since we support 64-bit systems, we treat
	//      32-bit pointers as offsets into the `memory` struct (similar
	//      to how AI5WIN.EXE treats 16-bit pointers).
	mem_set_sysvar16_ptr(MEMORY_MES_NAME_SIZE + VAR4_SIZE + 56);
	mem_set_sysvar32(mes_sysvar32_memory, offsetof(struct memory, mem16));
	mem_set_sysvar32(mes_sysvar32_palette, offsetof(struct memory, palette));
	mem_set_sysvar32(mes_sysvar32_file_data, offsetof(struct memory, file_data));
	mem_set_sysvar32(mes_sysvar32_menu_entry_addresses,
		offsetof(struct memory, menu_entry_addresses));
	mem_set_sysvar32(mes_sysvar32_menu_entry_numbers,
		offsetof(struct memory, menu_entry_numbers));

	// this value is restored when loading a save via System.SaveData.resume_load...
	mem_set_sysvar16(0, 5080);
}

static void yuno_mem_init(void)
{
	// set up pointer table for memory access
	// (needed because var4 size changes per game)
	uint32_t off = MEMORY_MES_NAME_SIZE + VAR4_SIZE;
	memory_ptr.system_var16_ptr = memory_raw + off;
	memory_ptr.var16 = memory_raw + off + 4;
	memory_ptr.system_var16 = memory_raw + off + 56;
	memory_ptr.var32 = memory_raw + off + 108;
	memory_ptr.system_var32 = memory_raw + off + 212;

	mem_set_sysvar16(mes_sysvar16_flags, 0x260d);
	mem_set_sysvar16(mes_sysvar16_text_start_x, 0);
	mem_set_sysvar16(mes_sysvar16_text_start_y, 0);
	mem_set_sysvar16(mes_sysvar16_text_end_x, game_yuno.surface_sizes[0].w);
	mem_set_sysvar16(mes_sysvar16_text_end_y, game_yuno.surface_sizes[0].h);
	mem_set_sysvar16(mes_sysvar16_font_width, DEFAULT_FONT_SIZE);
	mem_set_sysvar16(mes_sysvar16_font_height, DEFAULT_FONT_SIZE);
	mem_set_sysvar16(mes_sysvar16_char_space, DEFAULT_FONT_SIZE);
	mem_set_sysvar16(mes_sysvar16_line_space, DEFAULT_FONT_SIZE);
	mem_set_sysvar16(mes_sysvar16_mask_color, 0);

	mem_set_sysvar32(mes_sysvar32_cg_offset, 0x20000);
	yuno_mem_restore();
}

static void sys_22_warn(struct param_list *params)
{
	WARNING("System.function[22] not implemented");
}

struct game game_yuno = {
	.surface_sizes = {
		{ 640, 400 },
		{ 640, 400 },
		{ 640, 768 },
		{ 640, 768 },
		{ 1696, 720 }
	},
	.x_mult = 8,
	.var4_size = VAR4_SIZE,
	.mem16_size = MEM16_SIZE,
	.mem_init = yuno_mem_init,
	.mem_restore = yuno_mem_restore,
	.sys = {
		[0] = sys_set_font_size,
		[1] = sys_display_number,
		[2] = sys_cursor,
		[3] = sys_anim,
		[4] = sys_savedata,
		[5] = sys_audio,
		[6] = NULL,
		[7] = sys_file,
		[8] = sys_load_image,
		[9] = sys_palette,
		[10] = sys_graphics,
		[11] = sys_wait,
		[12] = sys_set_text_colors,
		[13] = sys_farcall,
		[14] = sys_get_cursor_segment,
		[15] = sys_menu_get_no,
		[18] = sys_check_input,
		[21] = sys_strlen,
		[22] = sys_22_warn,
		[23] = sys_set_screen_surface,
	},
	.util = {
		[1] = util_get_text_colors,
		[3] = util_noop,
		[5] = util_blink_fade,
		[6] = util_scale_h,
		[8] = util_invert_colors,
		[10] = util_fade,
		[11] = util_savedata_stash_name,
		[12] = util_pixelate,
		[14] = util_get_time,
		[15] = util_check_cursor,
		[16] = util_delay,
		[17] = util_save_animation,
		[18] = util_restore_animation,
		[19] = util_anim_save_running,
		[20] = util_copy_progressive,
		[21] = util_fade_progressive,
		[22] = util_anim_running,
		[100] = util_warn_unimplemented,
		[101] = util_warn_unimplemented,
		[200] = util_copy,
		[201] = util_bgm_play,
		[202] = util_bgm_is_playing,
		[203] = util_se_is_playing,
		[210] = util_get_ticks,
		[211] = util_wait_until,
		[212] = util_wait_until2,
		[213] = util_warn_unimplemented,
		[214] = util_bgm_is_fading,
	}
};

#define X 0xff
#define _ 0x00
static const uint8_t yuno_reflector_mask[] = {
	_,_,_,_,_,_,X,X,X,X,X,X,X,X,X,_,_,_,_,_,_,
	_,_,_,_,_,X,X,X,X,X,X,X,X,X,X,X,_,_,_,_,_,
	_,_,_,_,X,X,X,X,X,X,X,X,X,X,X,X,X,_,_,_,_,
	_,_,_,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,_,_,_,
	_,_,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,_,_,
	_,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,_,
	X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,
	X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,
	X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,
	X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,
	X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,
	_,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,_,
	_,_,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,_,_,
	_,_,_,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,_,_,_,
	_,_,_,_,X,X,X,X,X,X,X,X,X,X,X,X,X,_,_,_,_,
	_,_,_,_,_,X,X,X,X,X,X,X,X,X,X,X,_,_,_,_,_,
	_,_,_,_,_,_,X,X,X,X,X,X,X,X,X,_,_,_,_,_,_,
};
#undef _
#undef X

#define W 21
#define H 17
_Static_assert(sizeof(yuno_reflector_mask) == W*H);

static uint8_t yuno_reflector_frame0[W*H];
static uint8_t yuno_reflector_frame1[W*H];
static uint8_t yuno_reflector_frame2[W*H];
static uint8_t yuno_reflector_frame3[W*H];

static uint8_t *yuno_reflector_frames[6] = {
	yuno_reflector_frame0,
	yuno_reflector_frame1,
	yuno_reflector_frame2,
	yuno_reflector_frame3,
	yuno_reflector_frame2,
	yuno_reflector_frame1,
};

//  0 -> 11
// 11 -> 12
// 12 -> 7
static void generate_frame(uint8_t prev[W*H], uint8_t frame[W*H])
{
	for (int i = 0; i < W*H; i++) {
		if (!yuno_reflector_mask[i])
			frame[i] = 1;
		else if (prev[i] == 0)
			frame[i] = 11;
		else if (prev[i] == 11)
			frame[i] = 12;
		else if (prev[i] == 12)
			frame[i] = 7;
		else if (prev[i] == 7)
			frame[i] = 7;
		else
			WARNING("Unexpected color: %u", prev[i]);
	}
}

// location of base frame in MAPORB.GP8
#define MAPORB_X 21
#define MAPORB_Y 69

static void generate_reflector_frames(void)
{
	// load CG containing base frame
	struct archive_data *data = asset_cg_load("maporb.gp8");
	if (!data) {
		WARNING("Failed to load CG: MAPORB.GP8");
		return;
	}
	struct cg *cg = cg_load_arcdata(data);
	archive_data_release(data);
	if (!cg) {
		WARNING("Failed to decode CG: MAPORB.GP8");
		return;
	}
	if (cg->metrics.w < MAPORB_X + W || cg->metrics.h < MAPORB_Y + H) {
		cg_free(cg);
		WARNING("Unexpected dimensions for CG: MAPORB.GP8");
		return;
	}

	// load base frame
	uint8_t *base = cg->pixels + MAPORB_Y * cg->metrics.w + MAPORB_X;
	for (int row = 0; row < H; row++) {
		uint8_t *src = base + row * cg->metrics.w;
		uint8_t *dst = yuno_reflector_frame0 + row * W;
		for (int col = 0; col < W; col++, src++, dst++) {
			if (!yuno_reflector_mask[row * W + col])
				*dst = 1;
			else
				*dst = *src;
		}
	}
	cg_free(cg);

	// generate subsequent frames from base frame
	generate_frame(yuno_reflector_frame0, yuno_reflector_frame1);
	generate_frame(yuno_reflector_frame1, yuno_reflector_frame2);
	generate_frame(yuno_reflector_frame2, yuno_reflector_frame3);
}

#define DRAW_X 581
#define DRAW_Y 373

static void draw_frame(uint8_t frame[W*H])
{
	SDL_Surface *s = gfx_get_surface(gfx.screen);
	uint8_t *base = s->pixels + DRAW_Y * s->pitch + DRAW_X;
	for (int row = 0; row < H; row++) {
		uint8_t *dst = base + row * s->pitch;
		uint8_t *src = &frame[row*W];
		for (int i = 0; i < W; i++, src++, dst++) {
			if (*src != 1)
				*dst = *src;
		}
	}
}

#define FRAME_TIME 250

void gfx_yuno_reflector_animation(void)
{
	static uint32_t t = 0;
	static unsigned frame = 0;
	static bool initialized = false;
	if (!initialized) {
		generate_reflector_frames();
		initialized = true;
	}

	uint32_t now_t = vm_get_ticks();
	if (now_t - t < FRAME_TIME)
		return;

	draw_frame(yuno_reflector_frames[frame]);
	frame = (frame + 1) % ARRAY_SIZE(yuno_reflector_frames);
	t = now_t;
	gfx_dirty(gfx.screen);
}
