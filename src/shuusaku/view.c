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
#include "nulib/vector.h"
#include "nulib/utfsjis.h"
#include "ai5/cg.h"
#include "ai5/mes.h"

#include "anim.h"
#include "asset.h"
#include "cursor.h"
#include "gfx.h"
#include "gfx_private.h"
#include "input.h"
#include "memory.h"
#include "vm.h"
#include "vm_private.h"

#include "shuusaku.h"

#define SCREEN 0
#define CS_ENABLED 1
#define CS_HOVER 2
#define CS_DISABLED 3

struct char_select {
	SDL_Rect pos;
	bool enabled;
	unsigned enabled_flag;
	unsigned complete_flag;
};

static struct char_select characters[] = {
	[CHAR_NAGISA] = {
		.pos = { 11,  43,  154, 209 },
		.enabled_flag = 120,
		.complete_flag = 10
	},
	[CHAR_KAORI] = {
		.pos = { 166, 43,  154, 209 },
		.enabled_flag = 121,
		.complete_flag = 11
	},
	[CHAR_SHIHO] = {
		.pos = { 321, 43,  154, 209 },
		.enabled_flag = 122,
		.complete_flag = 12
	},
	[CHAR_CHIAKI] = {
		.pos = { 476, 43,  154, 209 },
		.enabled_flag = 123,
		.complete_flag = 13
	},
	[CHAR_ASAMI] = {
		.pos = { 11,  253, 154, 209 },
		.enabled_flag = 123,
		.complete_flag = 14
	},
	[CHAR_MOEKO] = {
		.pos = { 166, 253, 154, 209 },
		.enabled_flag = 125,
		.complete_flag = 15
	},
	[CHAR_ERI] = {
		.pos = { 321, 253, 154, 209 },
		.enabled_flag = 2793,
		.complete_flag = 16
	},
	[CHAR_AYAKA] = {
		.pos = { 476, 253, 154, 209 },
		.enabled_flag = 129,
		.complete_flag = 17
	}
};

static void blit_hover(struct char_select *ch)
{
	gfx_copy(ch->pos.x, ch->pos.y, ch->pos.w, ch->pos.h, CS_HOVER,
			ch->pos.x, ch->pos.y, SCREEN);
}

static void blit_unhover(struct char_select *ch)
{
	gfx_copy(ch->pos.x, ch->pos.y, ch->pos.w, ch->pos.h, CS_ENABLED,
			ch->pos.x, ch->pos.y, SCREEN);
}

static int char_hovered;

static int char_select_handle_input(void)
{
	if (input_down(INPUT_CANCEL))
		return 0;

	SDL_Point mouse;
	cursor_get_pos((unsigned*)&mouse.x, (unsigned*)&mouse.y);

	bool hovering = false;
	for (int i = 0; i < ARRAY_SIZE(characters); i++) {
		if (!characters[i].enabled)
			continue;
		if (SDL_PointInRect(&mouse, &characters[i].pos)) {
			hovering = true;
			if (char_hovered != i) {
				blit_hover(&characters[i]);
				if (char_hovered >= 0)
					blit_unhover(&characters[char_hovered]);
				char_hovered = i;
			}
			if (input_down(INPUT_ACTIVATE)) {
				input_wait_until_up(INPUT_ACTIVATE);
				return i + 1;
			}
		}
	}

	if (!hovering && char_hovered >= 0) {
		blit_unhover(&characters[char_hovered]);
		char_hovered = -1;
	}

	return -1;
}

unsigned shuusaku_scene_viewer_char_select(void)
{
	// view.gpx on surface 1
	// viewpart.gpx on surface 2
	// viewmono.gpx on surface 3
	// kanryou.gpx on surface 6
	char_hovered = -1;

	for (int i = 0; i < NR_CHAR; i++) {
		SDL_Rect *r = &characters[i].pos;
		characters[i].enabled = mem_get_var4_packed(characters[i].enabled_flag) == 9;
		if (!characters[i].enabled) {
			gfx_copy(r->x, r->y, r->w, r->h, CS_DISABLED, r->x, r->y, SCREEN);
		}
		if (mem_get_var4_packed(10 + i)) {
			int x = (i == CHAR_ERI) ? 151 : 0;
			gfx_copy_masked(x, 0, 151, 206, 6, r->x+1, r->y+1, SCREEN, MASK_COLOR);
			gfx_copy_masked(x, 0, 151, 206, 6, r->x+1, r->y+1, CS_ENABLED, MASK_COLOR);
			gfx_copy_masked(x, 0, 151, 206, 6, r->x+1, r->y+1, CS_HOVER, MASK_COLOR);
		}
	}

	shuusaku_crossfade(memory.palette, false);

	int clicked;
	while (true) {
		vm_peek();
		clicked = char_select_handle_input();
		if (clicked >= 0)
			break;
		vm_delay(16);
	}

	return clicked;
}

struct scene {
	unsigned flag_no;
	unsigned scene_id;
	const char *cg_name;
};

static struct scene scenes_nagisa[] = {
	{ 2700, 1, "ev33.gpx" }, { 2706, 7, "ev33r.gpx" },
	{ 2701, 2, "ev34.gpx" }, { 2707, 8, "ev34r.gpx" },
	{ 2702, 3, "ev35.gpx" }, { 2708, 9, "ev35r.gpx" },
	{ 2703, 4, "ev36.gpx" }, { 2709, 10, "ev36r.gpx" },
	{ 2704, 5, "ev37.gpx" }, { 2710, 11, "ev37r.gpx" },
	{ 2705, 6, "ev38.gpx" }, { 2711, 12, "ev38r.gpx" },
	{ 1051, 13, "ev163.gpx" },
	{0}
};

static struct scene scenes_kaori[] = {
	{ 2712, 1, "ev67.gpx" }, { 2718, 7, "ev67r.gpx" },
	{ 2713, 2, "ev68.gpx" }, { 2719, 8, "ev68r.gpx" },
	{ 2714, 3, "ev69.gpx" }, { 2720, 9, "ev69r.gpx" },
	{ 2715, 4, "ev70.gpx" }, { 2721, 10, "ev70r.gpx" },
	{ 2716, 5, "ev71.gpx" }, { 2722, 11, "ev71r.gpx" },
	{ 2717, 6, "ev72.gpx" }, { 2723, 12, "ev72r.gpx" },
	{ 2952, 13, "ev62asp.gpx" },
	{ 566, 14, "ev64.gpx" },
	{ 1052, 15, "ev168.gpx" },
	{0}
};

static struct scene scenes_shiho[] = {
	{ 2724, 1, "ev50.gpx" }, { 2730, 7, "ev50r.gpx" },
	{ 2725, 2, "ev51.gpx" }, { 2731, 8, "ev51r.gpx" },
	{ 2726, 3, "ev52.gpx" }, { 2732, 9, "ev52r.gpx" },
	{ 2727, 4, "ev53.gpx" }, { 2733, 10, "ev53r.gpx" },
	{ 2728, 5, "ev54.gpx" }, { 2734, 11, "ev54r.gpx" },
	{ 2729, 6, "ev55.gpx" }, { 2735, 12, "ev55r.gpx" },
	{ 1053, 13, "ev165.gpx" },
	{0}
};

static struct scene scenes_chiaki[] = {
	{ 2736, 1, "ev125.gpx" }, { 2742, 7, "ev125r.gpx" },
	{ 2737, 2, "ev126.gpx" }, { 2743, 8, "ev126r.gpx" },
	{ 2738, 3, "ev127.gpx" }, { 2744, 9, "ev127r.gpx" },
	{ 2739, 4, "ev128.gpx" }, { 2745, 10, "ev128r.gpx" },
	{ 2740, 5, "ev129.gpx" }, { 2746, 11, "ev129r.gpx" },
	{ 2741, 6, "ev130.gpx" }, { 2747, 12, "ev130r.gpx" },
	{ 2797, 13, "ev113asp.gpx" },
	{ 2953, 14, "u14sp.gpx" },
	{ 1054, 15, "ev166.gpx" },
	{0}
};

static struct scene scenes_asami[] = {
	{ 2748, 1, "ev105.gpx" }, { 2754, 7, "ev105r.gpx" },
	{ 2749, 2, "ev106.gpx" }, { 2755, 8, "ev106r.gpx" },
	{ 2750, 3, "ev108.gpx" }, { 2756, 9, "ev108r.gpx" },
	{ 2751, 4, "ev109.gpx" }, { 2757, 10, "ev109r.gpx" },
	{ 2752, 5, "ev110.gpx" }, { 2758, 11, "ev110r.gpx" },
	{ 2753, 6, "ev111.gpx" }, { 2759, 12, "ev111r.gpx" },
	{ 708, 13, "ev93a.gpx" },
	{ 771, 14, "ev101.gpx" },
	{ 2954, 15, "ev95asp.gpx" },
	{ 1055, 16, "ev164.gpx" },
	{0}
};

static struct scene scenes_moeko[] = {
	{ 2760, 1, "ev86.gpx" }, { 2766, 7, "ev86r.gpx" },
	{ 2761, 2, "ev87.gpx" }, { 2767, 8, "ev87r.gpx" },
	{ 2762, 3, "ev88.gpx" }, { 2768, 9, "ev88r.gpx" },
	{ 2763, 4, "ev89.gpx" }, { 2769, 10, "ev89r.gpx" },
	{ 2764, 5, "ev90.gpx" }, { 2770, 11, "ev90r.gpx" },
	{ 2765, 6, "ev91.gpx" }, { 2771, 12, "ev91r.gpx" },
	{ 2798, 13, "ev81.gpx" },
	{ 600, 14, "ev73.gpx" },
	{ 661, 15, "ev73r.gpx" },
	{ 663, 16, "ev84.gpx" },
	{ 1056, 17, "ev167.gpx" },
	{0}
};

static struct scene scenes_eri[] = {
	{ 2784, 1, "ev151sp.gpx" },
	{ 2785, 2, "ev152e.gpx" },
	{ 2786, 3, "ev152r.gpx" },
	{ 2787, 4, "ev154.gpx" },
	{ 2788, 5, "ev155.gpx" },
	{ 2789, 6, "ev156.gpx" },
	{ 2790, 7, "ev158.gpx" },
	{ 2791, 8, "ev160.gpx" },
	{ 2792, 9, "ev161.gpx" },
	{ 2793, 10, "ev171.gpx" },
	{0}
};

static struct scene scenes_ayaka[] = {
	{ 2772, 1, "ev135.gpx" }, { 2778, 7, "ev135r.gpx" },
	{ 2773, 2, "ev136.gpx" }, { 2779, 8, "ev136r.gpx" },
	{ 2774, 3, "ev137.gpx" }, { 2780, 9, "ev137r.gpx" },
	{ 2775, 4, "ev138.gpx" }, { 2781, 10, "ev138r.gpx" },
	{ 2776, 5, "ev139.gpx" }, { 2782, 11, "ev139r.gpx" },
	{ 2777, 6, "ev140.gpx" }, { 2783, 12, "ev140r.gpx" },
	{ 2958, 13, "ev131.gpx" },
	{ 2957, 14, "ev131asp.gpx" },
	{ 2955, 15, "ev131bsp.gpx" },
	{ 2950, 16, "ev141.gpx" },
	{ 2951, 17, "ev141asp.gpx" },
	{ 1057, 18, "ev169.gpx" },
	{0}
};

struct scene *scene_lists[] = {
	[CHAR_NAGISA] = scenes_nagisa,
	[CHAR_KAORI] = scenes_kaori,
	[CHAR_SHIHO] = scenes_shiho,
	[CHAR_CHIAKI] = scenes_chiaki,
	[CHAR_ASAMI] = scenes_asami,
	[CHAR_MOEKO] = scenes_moeko,
	[CHAR_ERI] = scenes_eri,
	[CHAR_AYAKA] = scenes_ayaka
};

#define THUMB 1
#define THUMB_W 120
#define THUMB_H 90
#define THUMB_SRC_X(i) (((i) % 5) * 120)
#define THUMB_SRC_Y(i) (90 + ((i) / 5) * 90)
#define THUMB_DST_X(i) (12 + ((i) % 5) * 124)
#define THUMB_DST_Y(i) (44 + ((i) / 5) * 94)
#define THUMB_RECT(i) { THUMB_DST_X(i), THUMB_DST_Y(i), THUMB_W, THUMB_H }

static SDL_Rect scene_thumbs[20] = {
	THUMB_RECT(0),
	THUMB_RECT(1),
	THUMB_RECT(2),
	THUMB_RECT(3),
	THUMB_RECT(4),
	THUMB_RECT(5),
	THUMB_RECT(6),
	THUMB_RECT(7),
	THUMB_RECT(8),
	THUMB_RECT(9),
	THUMB_RECT(10),
	THUMB_RECT(11),
	THUMB_RECT(12),
	THUMB_RECT(13),
	THUMB_RECT(14),
	THUMB_RECT(15),
	THUMB_RECT(16),
	THUMB_RECT(17),
	THUMB_RECT(18),
	THUMB_RECT(19),
};

static void blit_crosshair(unsigned i)
{
	gfx_copy_masked(0, 0, THUMB_W, THUMB_H, THUMB, THUMB_DST_X(i), THUMB_DST_Y(i),
			SCREEN, MASK_COLOR);
}

static void blit_question_mark(unsigned i)
{
	gfx_copy(120, 0, THUMB_W, THUMB_H, THUMB, THUMB_DST_X(i), THUMB_DST_Y(i), SCREEN);
}

static void blit_thumbnail(unsigned i)
{
	gfx_copy(THUMB_SRC_X(i), THUMB_SRC_Y(i), THUMB_W, THUMB_H, THUMB,
			THUMB_DST_X(i), THUMB_DST_Y(i), SCREEN);
}

static int scene_hovered;

static int scene_select_handle_input(struct scene *scenes, bool scene_enabled[20])
{
	if (input_down(INPUT_CANCEL))
		return 0;

	SDL_Point mouse;
	cursor_get_pos((unsigned*)&mouse.x, (unsigned*)&mouse.y);

	bool hovering = false;
	for (int i = 0; i < 20 && scenes[i].flag_no; i++) {
		if (!scene_enabled[i])
			continue;
		if (SDL_PointInRect(&mouse, &scene_thumbs[i])) {
			hovering = true;
			if (scene_hovered != i) {
				blit_crosshair(i);
				if (scene_hovered >= 0)
					blit_thumbnail(scene_hovered);
				scene_hovered = i;
			}
			if (input_down(INPUT_ACTIVATE)) {
				input_wait_until_up(INPUT_ACTIVATE);
				return i + 1;
			}
		}
	}

	if (!hovering && scene_hovered >= 0) {
		blit_thumbnail(scene_hovered);
		scene_hovered = -1;
	}

	return -1;
}

static void _load_image(const char *name, unsigned i)
{
	struct cg *cg = asset_cg_load(name);
	if (!cg) {
		WARNING("Failed to load CG \"%s\"", name);
		return;
	}

	gfx_draw_cg(i, cg);
	memcpy(memory.palette + 10 * 4, cg->palette + 10 * 4, 236 * 4);
	cg_free(cg);
}

unsigned shuusaku_scene_viewer_scene_select(enum sched_character ch, bool need_bg)
{
	scene_hovered = -1;

	// load background CG if needed
	if (need_bg) {
		uint8_t pal[256*4] = {0};
		gfx_palette_set(pal + 10*4, 10, 236);
		_load_image("choice.gpx", 0);
	}

	// draw unlocked scenes
	struct scene *list = scene_lists[ch];
	bool scene_enabled[20] = {0};
	for (int i = 0; list[i].flag_no; i++) {
		if (mem_get_var4_packed(scene_lists[ch][i].flag_no)) {
			blit_thumbnail(i);
			scene_enabled[i] = true;
		} else {
			blit_question_mark(i);
		}
	}

	if (need_bg)
		shuusaku_crossfade(memory.palette, true);

	// save screen to surface 8
	gfx_copy(0, 0, 640, 480, 0, 0, 0, 8);

	int clicked;
	while (true) {
		vm_peek();
		clicked = scene_select_handle_input(list, scene_enabled);
		if (clicked >= 0)
			break;
		vm_delay(16);
	}

	if (clicked <= 0) {
		uint8_t pal[256*4];
		memset(pal, 0, 256*4);
		shuusaku_crossfade(pal, true);
		return 0;
	}

	_load_image(list[clicked-1].cg_name, 2);
	gfx_palette_set(memory.palette + 42*4, 42, 204);

	SDL_Rect *r = &scene_thumbs[clicked-1];
	shuusaku_zoom(r->x, r->y, r->w, r->h, 2);
	return list[clicked-1].scene_id;
}
