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
#include "gfx_private.h"
#include "vm.h"

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
