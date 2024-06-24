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

#include "anim.h"
#include "audio.h"
#include "cursor.h"
#include "gfx.h"
#include "savedata.h"
#include "sys.h"
#include "vm_private.h"

static uint8_t audio_vol[] = {
	[AUDIO_CH_BGM] = 31,
	[AUDIO_CH_SE0] = 31,
};

static unsigned fade_time(uint8_t from_vol, uint8_t to_vol)
{
	unsigned diff = abs((int)to_vol - (int)from_vol);
	return diff * 100 + 50;
}

// XXX: Volume is a value in the range [0,31], which corresponds to the range
//      [-5000,0] in increments of 156 (volume in dB).
static int get_volume_db(uint8_t vol)
{
	if (vol == 31)
		return 0;
	return -5000 + vol * 156;
}

static void classics_audio_fade(enum audio_channel ch, uint8_t vol, bool stop, bool sync)
{
	// XXX: a bit strange, but this is how it works...
	if (vol > audio_vol[ch])
		stop = false;
	else if (vol == audio_vol[ch])
		return;

	audio_mixer_fade(ch, get_volume_db(vol), fade_time(audio_vol[ch], vol), stop, sync);
	audio_vol[ch] = vol;
}

static void classics_audio_fade_out(enum audio_channel ch, uint8_t vol, bool sync)
{
	if (vol == audio_vol[ch]) {
		audio_stop(ch);
		return;
	}
	audio_mixer_fade(ch, get_volume_db(vol), fade_time(audio_vol[ch], vol), true, sync);
	audio_vol[ch] = vol;
}

static void classics_audio_set_volume(enum audio_channel ch, uint8_t vol)
{
	audio_vol[ch] = vol;
	audio_set_volume(ch, get_volume_db(vol));
}

static void classics_audio_restore_volume(enum audio_channel ch)
{
	if (!audio_is_playing(ch))
		return;
	if (audio_vol[ch] == 31) {
		audio_stop(ch);
		return;
	}
	audio_mixer_fade(ch, AUDIO_VOLUME_MAX, fade_time(audio_vol[ch], 31), false, false);
	audio_vol[ch] = 31;
}

void classics_audio(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0:  audio_bgm_play(vm_string_param(params, 1), true); break;
	case 2:  audio_stop(AUDIO_CH_BGM); break;
	case 3:  audio_se_play(vm_string_param(params, 1), 0); break;
	case 4:  classics_audio_fade(AUDIO_CH_BGM, vm_expr_param(params, 2),
				 vm_expr_param(params, 3), true); break;
	case 5:  classics_audio_set_volume(AUDIO_CH_BGM, vm_expr_param(params, 1)); break;
	case 7:  classics_audio_fade(AUDIO_CH_BGM, vm_expr_param(params, 2),
				 vm_expr_param(params, 3), false); break;
	case 9:  classics_audio_fade_out(AUDIO_CH_BGM, vm_expr_param(params, 1), true); break;
	case 10: classics_audio_fade_out(AUDIO_CH_BGM, vm_expr_param(params, 2), false); break;
	case 12: audio_stop(AUDIO_CH_SE(0)); break;
	case 18: classics_audio_restore_volume(AUDIO_CH_BGM); break;
	default: VM_ERROR("System.Audio.function[%d] not implemented", params->params[0].val);
	}
}

void classics_cursor(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: cursor_reload(); break;
	case 1: cursor_unload(); break;
	case 2: sys_cursor_save_pos(params); break;
	case 3: cursor_set_pos(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 4: cursor_load(vm_expr_param(params, 1) * 2, 2, NULL); break;
	case 5: cursor_show(); break;
	case 6: cursor_hide(); break;
	default: VM_ERROR("System.Cursor.function[%u] not implemented", params->params[0].val);
	}
}

void classics_anim(struct param_list *params)
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

void classics_savedata(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: savedata_resume_load(sys_save_name(params)); break;
	case 1: savedata_resume_save(sys_save_name(params)); break;
	case 2: savedata_load(sys_save_name(params)); break;
	case 3: savedata_save(sys_save_name(params)); break;
	case 4: savedata_load_var4(sys_save_name(params)); break;
	case 5: savedata_save_var4(sys_save_name(params)); break;
	case 6: savedata_save_union_var4(sys_save_name(params)); break;
	case 7: savedata_load_var4_slice(sys_save_name(params), vm_expr_param(params, 2),
				vm_expr_param(params, 3)); break;
	case 8: savedata_save_var4_slice(sys_save_name(params), vm_expr_param(params, 2),
				vm_expr_param(params, 3)); break;
	case 9: savedata_copy(sys_save_name(params),
				_sys_save_name(vm_expr_param(params, 2))); break;
	case 11: savedata_f11(sys_save_name(params)); break;
	case 12: savedata_f12(sys_save_name(params)); break;
	case 13: savedata_set_mes_name(sys_save_name(params), vm_string_param(params, 2)); break;
	default: VM_ERROR("System.savedata.function[%u] not implemented", params->params[0].val);
	}
}

static void check_rgb_param(struct param_list *params, unsigned i, uint8_t *r, uint8_t *g,
		uint8_t *b)
{
	uint32_t c = vm_expr_param(params, i);
	*r = ((c >> 4) & 0xf) * 17;
	*g = ((c >> 8) & 0xf) * 17;
	*b = (c & 0xf) * 17;
}

static void palette_set(struct param_list *params)
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

static void palette_crossfade1(struct param_list *params)
{
	if (params->nr_params > 1) {
		uint8_t r, g, b;
		check_rgb_param(params, 1, &r, &g, &b);
		gfx_palette_crossfade_to(r, g, b, 240);
	} else {
		gfx_palette_crossfade(memory.palette, 240);
	}
}

static void palette_crossfade2(struct param_list *params)
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

void classics_palette(struct param_list *params)
{
	vm_expr_param(params, 0);
	switch (params->params[0].val) {
	case 0:  palette_set(params); break;
	case 1:  palette_crossfade1(params); break;
	case 2:  palette_crossfade2(params); break;
	case 3:  gfx_display_hide(0); break;
	case 4:  gfx_display_unhide(); break;
	default: VM_ERROR("System.Palette.function[%d] not implemented",
				 params->params[0].val);
	}
}

void classics_graphics(struct param_list *params)
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

void classics_get_cursor_segment(struct param_list *params)
{
	_sys_get_cursor_segment(vm_expr_param(params, 0), vm_expr_param(params, 1),
			vm_expr_param(params, 2));
}

void classics_get_text_colors(struct param_list *params)
{
	uint32_t bg, fg;
	gfx_text_get_colors(&bg, &fg);
	mem_set_var32(18, ((bg & 0xf) << 4) | (fg & 0xf));
}
