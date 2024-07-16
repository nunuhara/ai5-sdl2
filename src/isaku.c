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

#include <SDL.h>

#include "nulib.h"
#include "nulib/little_endian.h"
#include "ai5/anim.h"
#include "ai5/mes.h"

#include "anim.h"
#include "audio.h"
#include "cursor.h"
#include "dungeon.h"
#include "game.h"
#include "gfx_private.h"
#include "input.h"
#include "savedata.h"
#include "sys.h"
#include "vm_private.h"

#define VAR4_SIZE 2048
#define MEM16_SIZE 4096

static void isaku_mem_restore(void)
{
	mem_set_sysvar16_ptr(MEMORY_MES_NAME_SIZE + VAR4_SIZE + 56);
	mem_set_sysvar32(mes_sysvar32_memory, offsetof(struct memory, mem16));
	mem_set_sysvar32(mes_sysvar32_palette, offsetof(struct memory, palette));
	mem_set_sysvar32(mes_sysvar32_file_data, offsetof(struct memory, file_data));
	mem_set_sysvar32(mes_sysvar32_menu_entry_addresses,
		offsetof(struct memory, menu_entry_addresses));
	mem_set_sysvar32(mes_sysvar32_menu_entry_numbers,
		offsetof(struct memory, menu_entry_numbers));

	uint16_t flags = mem_get_sysvar16(mes_sysvar16_flags);
	mem_set_sysvar16(mes_sysvar16_flags, flags | 4);
	mem_set_sysvar16(0, 2632);
	mem_set_var16(22, 20);
}

static void isaku_mem_init(void)
{
	// set up pointer table for memory access
	// (needed because var4 size changes per game)
	uint32_t off = MEMORY_MES_NAME_SIZE + VAR4_SIZE;
	memory_ptr.system_var16_ptr = memory_raw + off;
	memory_ptr.var16 = memory_raw + off + 4;
	memory_ptr.system_var16 = memory_raw + off + 56;
	memory_ptr.var32 = memory_raw + off + 108;
	memory_ptr.system_var32 = memory_raw + off + 212;

	mem_set_sysvar16(mes_sysvar16_flags, 0xf);
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
	isaku_mem_restore();
}

static unsigned cursor_no(unsigned n)
{
	switch (n) {
	case 0: return 30;
	case 1: return 32;
	case 2: return 34;
	case 3: return 36;
	case 4: return 38;
	case 5: return 40;
	case 6: return 42;
	// XXX: skip 7
	case 8: return 44;
	// XXX: skip 9-12
	case 13: return 46;
	case 14: return 48;
	case 15: return 50;
	case 16: return 52;
	default:
		 WARNING("Invalid cursor number: %u", n);
	}
	return 30;
}

static void isaku_cursor(struct param_list *params)
{
	static uint32_t uk = 0;
	switch (vm_expr_param(params, 0)) {
	case 0: cursor_show(); break;
	case 1: cursor_hide(); break;
	case 2: sys_cursor_save_pos(params); break;
	case 3: cursor_set_pos(vm_expr_param(params, 1), vm_expr_param(params, 2)); break;
	case 4: cursor_load(cursor_no(vm_expr_param(params, 1)), 2, NULL); break;
	case 5: uk = 0; break;
	case 6: mem_set_var16(18, 0); break;
	case 7: mem_set_var32(18, uk); break;
	case 8: uk = vm_expr_param(params, 1); break;
	default: VM_ERROR("System.Cursor.function[%u] not implemented",
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

static void isaku_anim(struct param_list *params)
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
	case 8: anim_exec_copy_call(vm_anim_param(params, 1)); break;
	default: VM_ERROR("System.Anim.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void isaku_savedata(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: savedata_resume_load(sys_save_name(params)); break;
	case 1: savedata_resume_save(sys_save_name(params)); break;
	case 2: savedata_load(sys_save_name(params)); break;
	case 3: savedata_save_union_var4(sys_save_name(params), VAR4_SIZE); break;
	//case 4: savedata_load_isaku_vars(save_name); break;
	//case 5: savedata_save_isaku_vars(save_name); break;
	//case 6: savedata_clear_var4(save_name); break;
	default: VM_ERROR("System.SaveData.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void isaku_bgm_fade_out_sync(void)
{
	audio_fade(AUDIO_CH_BGM, AUDIO_VOLUME_MIN, 500, true, false);
	while (audio_is_playing(AUDIO_CH_BGM)) {
		if (input_down(INPUT_SHIFT))
			break;
		vm_peek();
		vm_delay(16);
	}
}

static void isaku_se_fade_out_sync(void)
{
	audio_fade(AUDIO_CH_SE(0), AUDIO_VOLUME_MIN, 500, true, false);
	while (audio_is_playing(AUDIO_CH_SE(0))) {
		if (input_down(INPUT_SHIFT))
			break;
		vm_peek();
		vm_delay(16);
	}
}

static void isaku_se_wait(void)
{
	while (audio_is_playing(AUDIO_CH_SE(0))) {
		vm_peek();
		vm_delay(16);
	}
}

static void isaku_audio(struct param_list *params)
{
	if (!vm_flag_is_on(FLAG_AUDIO_ENABLE))
		return;
	switch (vm_expr_param(params, 0)) {
	case 0: audio_bgm_play(vm_string_param(params, 1), true); break;
	case 1: audio_fade(AUDIO_CH_BGM, AUDIO_VOLUME_MIN, 2000, true, false); break;
	case 2: audio_stop(AUDIO_CH_BGM); break;
	case 3: audio_se_play(vm_string_param(params, 1), 0); break;
	case 4: audio_stop(AUDIO_CH_SE(0)); break;
	case 5: audio_fade(AUDIO_CH_SE(0), AUDIO_VOLUME_MIN, 2000, true, false); break;
	//case 6: audio_bgm_play_sync(vm_string_param(params, 1)); break; // NOT USED
	case 7: isaku_bgm_fade_out_sync(); break;
	case 8: isaku_se_fade_out_sync(); break;
	case 9: isaku_se_wait(); break;
	default: VM_ERROR("System.Audio.function[%u] not implemented",
				 params->params[0].val);
	}
}

static void isaku_voice(struct param_list *params)
{
	if (!vm_flag_is_on(FLAG_VOICE_ENABLE))
		return;
	switch (vm_expr_param(params, 0)) {
	case 0: audio_voice_play(vm_string_param(params, 1), 0); break;
	case 1: audio_stop(AUDIO_CH_VOICE(0)); break;
	default: WARNING("System.Voice.function[%u] not implemented",
				 params->params[0].val);
	}
}

// only unfreeze is used in Isaku
static void isaku_display_freeze_unfreeze(struct param_list *params)
{
	if (params->nr_params > 1) {
		gfx_display_freeze();
	} else {
		gfx_display_unfreeze();
	}
}

static bool skip_on_shift(void)
{
	return !input_down(INPUT_SHIFT);
}

static void isaku_display_fade_out_fade_in(struct param_list *params)
{
	if (params->nr_params > 1) {
		_gfx_display_fade_out(vm_expr_param(params, 1), 1000, skip_on_shift);
	} else {
		_gfx_display_fade_in(1000, skip_on_shift);
	}
}

// this is not actually used in Isaku, though it is available
static void isaku_display_scan_out_scan_in(struct param_list *params)
{
	if (params->nr_params > 1) {
		WARNING("System.Display.scan_out unimplemented");
		gfx_display_fade_out(vm_expr_param(params, 1), 1000);
	} else {
		WARNING("System.Display.scan_in unimplemented");
		gfx_display_fade_in(1000);
	}
}

static void isaku_display(struct param_list *params)
{
	anim_halt_all();
	switch (vm_expr_param(params, 0)) {
	case 0: isaku_display_freeze_unfreeze(params); break;
	case 1: isaku_display_fade_out_fade_in(params); break;
	case 2: isaku_display_scan_out_scan_in(params); break;
	default: VM_ERROR("System.Display.function[%u] unimplemented",
				 params->params[0].val);
	}
}

static void isaku_graphics(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: sys_graphics_copy(params); break;
	case 1: sys_graphics_copy_masked(params); break;
	case 2: sys_graphics_fill_bg(params); break;
	case 3: sys_graphics_copy_swap(params); break;
	case 4: sys_graphics_swap_bg_fg(params); break;
	case 5: sys_graphics_copy_progressive(params); break;
	case 6: sys_graphics_compose(params); break;
	}
}

static void isaku_dungeon(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: dungeon_load(memory_raw + vm_expr_param(params, 1),
				memory_raw + vm_expr_param(params, 5),
				memory_raw + vm_expr_param(params, 6),
				memory_raw + vm_expr_param(params, 7),
				memory_raw + vm_expr_param(params, 8),
				memory_raw + vm_expr_param(params, 9));
		break;
	case 1: dungeon_set_pos(vm_expr_param(params, 1), vm_expr_param(params, 2),
				vm_expr_param(params, 3));
		break;
	case 2: dungeon_draw(); break;
	case 3: mem_set_var16(18, dungeon_move(vm_expr_param(params, 1))); break;
	case 8: WARNING("System.Dungeon.function[8] not implemented"); break;
	case 9: cursor_load(vm_expr_param(params, 1) + 25, 1, NULL); break;
	default:
		VM_ERROR("System.Dungeon.function[%u] not implemented",
				params->params[0].val);
	}

	if (vm_expr_param(params, 0) != 9) {
		uint16_t x, y, dir;
		dungeon_get_pos(&x, &y, &dir);
		mem_set_var16(23, x);
		mem_set_var16(24, y);
		mem_set_var16(3, dir);
	}
}

#define ITEM_WINDOW_W 320
#define ITEM_WINDOW_H 32
struct {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	uint32_t window_id;
	bool enabled;
	bool opened;
	bool lmb_down;
	bool rmb_down;
} item_window = {0};

static void item_window_create(void)
{
	SDL_CTOR(SDL_CreateWindow, item_window.window, "Items",
			SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
			ITEM_WINDOW_W, ITEM_WINDOW_H,
			SDL_WINDOW_HIDDEN);
	item_window.window_id = SDL_GetWindowID(item_window.window);
	SDL_CTOR(SDL_CreateRenderer, item_window.renderer, item_window.window, -1, 0);
	SDL_CALL(SDL_SetRenderDrawColor, item_window.renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
	SDL_CALL(SDL_RenderSetLogicalSize, item_window.renderer, ITEM_WINDOW_W, ITEM_WINDOW_H);
	SDL_CTOR(SDL_CreateTexture, item_window.texture, item_window.renderer,
			gfx.display->format->format, SDL_TEXTUREACCESS_STATIC,
			ITEM_WINDOW_W, ITEM_WINDOW_H);
	item_window.enabled = true;
}

static void item_window_update(void)
{
	if (!item_window.opened)
		return;
	SDL_CALL(SDL_UpdateTexture, item_window.texture, NULL, gfx.surface[7].s->pixels,
			gfx.surface[7].s->pitch);
	SDL_CALL(SDL_RenderClear, item_window.renderer);
	SDL_CALL(SDL_RenderCopy, item_window.renderer, item_window.texture, NULL, NULL);
	SDL_RenderPresent(item_window.renderer);
}

static void item_window_toggle(void)
{
	if (!item_window.enabled)
		return;
	if (item_window.opened) {
		SDL_HideWindow(item_window.window);
		audio_se_play("wincls.wav", 0);
		item_window.opened = false;
	} else {
		SDL_ShowWindow(item_window.window);
		audio_se_play("winopn.wav", 0);
		item_window.opened = true;
		item_window_update();
		gfx_dump_surface(7, "item_window.png");
	}
}

static void item_window_is_open(void)
{
	if (!item_window.enabled)
		return;
	mem_set_var16(18, item_window.opened);
}

static void item_window_get_pos(void)
{
	if (!item_window.enabled)
		return;
	int x, y;
	SDL_GetWindowPosition(item_window.window, &x, &y);
	le_put16(memory_ptr.system_var32, 44, x);
	le_put16(memory_ptr.system_var32, 46, y);
	le_put16(memory_ptr.system_var32, 48, x + ITEM_WINDOW_W - 1);
	le_put16(memory_ptr.system_var32, 50, y + ITEM_WINDOW_H - 1);
}

static void item_window_get_cursor_pos(void)
{
	if (!item_window.enabled)
		return;
	int x, y;
	if (SDL_GetMouseFocus() != item_window.window) {
		x = ITEM_WINDOW_W;
		y = ITEM_WINDOW_H;
	} else {
		SDL_GetMouseState(&x, &y);
	}
	mem_set_sysvar16(mes_sysvar16_cursor_x, x);
	mem_set_sysvar16(mes_sysvar16_cursor_y, y);
}

static void item_window_enable(void)
{
	if (!item_window.window)
		return;
	item_window.enabled = true;
}

static void item_window_disable(void)
{
	item_window.enabled = false;
	if (item_window.opened) {
		SDL_HideWindow(item_window.window);
		item_window.enabled = false;
	}
}

static void item_window_get_mouse_state(void)
{
	if (!item_window.enabled)
		return;
	le_put16(memory_ptr.system_var32, 52, item_window.lmb_down);
	le_put16(memory_ptr.system_var32, 54, item_window.rmb_down);
}

static void item_window_9(void)
{
	mem_set_var16(18, 0);
}

static void item_window_10(void)
{
	WARNING("ItemWindow.function[10] not implemented");
}

static void isaku_item_window(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	case 0: item_window_create(); break;
	case 1: item_window_toggle(); break;
	case 2: item_window_is_open(); break;
	case 3: item_window_get_pos(); break;
	case 4: item_window_get_cursor_pos(); break;
	case 5: item_window_enable(); break;
	case 6: item_window_disable(); break;
	case 7: item_window_get_mouse_state(); break;
	case 8: item_window_update(); break;
	case 9: item_window_9(); break;
	case 10: item_window_10(); break;
	default:
		VM_ERROR("System.ItemWindow.function[%u] not implemented",
				params->params[0].val);
	}
}

static void isaku_strlen(struct param_list *params)
{
	vm_flag_on(FLAG_STRLEN);
	mem_set_var32(18, 0);
	sys_farcall(params);
	vm_flag_off(FLAG_STRLEN);
}

struct menu {
	bool enabled;
	bool requested;
	const char *name;
};
static struct menu save_menu = { .name = "SaveMenu" };
static struct menu load_menu = { .name = "LoadMenu" };

static void menu_open(struct menu *menu)
{
	if (!menu->enabled) {
		audio_se_play("error.wav", 0);
		return;
	}
	audio_se_play("winopn.wav", 0);
	menu->requested = true;
}

static void isaku_menu(struct param_list *params, struct menu *menu)
{
	switch (vm_expr_param(params, 0)) {
	case 0: menu_open(menu); break;
	case 1: menu->enabled = vm_expr_param(params, 1); break;
	case 2: menu->requested = false; break;
	case 3: mem_set_var16(18, menu->requested); break;
	default: VM_ERROR("System.%s.function[%u] not implemented", menu->name,
				 params->params[0].val);
	}
}

static void isaku_save_menu(struct param_list *params)
{
	isaku_menu(params, &save_menu);
}

static void isaku_load_menu(struct param_list *params)
{
	isaku_menu(params, &load_menu);
}

static void sys_27(struct param_list *params)
{
	switch (vm_expr_param(params, 0)) {
	default:
		WARNING("System.function[27].function[%u] not implemented",
				params->params[0].val);
	}
}

static void util_load_heap(struct param_list *params)
{
	savedata_read("FLAG08", memory_raw, 3132, 100);
}

static void util_save_heap(struct param_list *params)
{
	savedata_write("FLAG08", memory_raw, 3132, 100);
}

static void util_delay(struct param_list *params)
{
	vm_timer_t t = vm_timer_create();
	uint32_t stop_t = t + vm_expr_param(params, 1) * 16;
	mem_set_var32(18, 0);
	while (t < stop_t) {
		if (input_down(INPUT_SHIFT))
			return;
		if (input_down(INPUT_CANCEL)) {
			mem_set_var32(18, 1);
			return;
		}
		vm_peek();
		vm_timer_tick(&t, 16);
	}
}

static void isaku_handle_event(SDL_Event *e)
{
	switch (e->type) {
	case SDL_WINDOWEVENT:
		if (e->window.windowID != item_window.window_id)
			break;
		switch (e->window.event) {
		case SDL_WINDOWEVENT_SHOWN:
		case SDL_WINDOWEVENT_EXPOSED:
		case SDL_WINDOWEVENT_RESIZED:
		case SDL_WINDOWEVENT_SIZE_CHANGED:
		case SDL_WINDOWEVENT_MAXIMIZED:
		case SDL_WINDOWEVENT_RESTORED:
			item_window_update();
			break;
		case SDL_WINDOWEVENT_CLOSE:
			assert(item_window.opened);
			item_window_toggle();
			break;
		}
	case SDL_KEYDOWN:
		switch (e->key.keysym.sym) {
		case SDLK_SPACE:
			item_window_toggle();
			break;
		case SDLK_F5:
			menu_open(&save_menu);
			break;
		case SDLK_F9:
			menu_open(&load_menu);
			break;
		}
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		if (e->button.windowID != item_window.window_id)
			break;
		if (e->button.button == SDL_BUTTON_LEFT) {
			item_window.lmb_down = e->button.state == SDL_PRESSED;
		} else if (e->button.button == SDL_BUTTON_RIGHT) {
			item_window.rmb_down = e->button.state == SDL_PRESSED;
		}
		break;
	}
}

static void isaku_draw_text(const char *text)
{
	if (vm_flag_is_on(FLAG_STRLEN))
		mem_set_var32(18, mem_get_var32(18) + strlen(text));
	else
		vm_draw_text(text);
}

struct game game_isaku = {
	.id = GAME_ISAKU,
	.surface_sizes = {
		{  640,  480 },
		{ 1000, 1750 },
		{  640,  480 }, // XXX: AI5WIN.exe crashes when using this surface
		{  640,  480 },
		{  640,  480 },
		{  640,  480 },
		{  352,   32 },
		{  320,   32 },
		{  640,  480 },
		{  0, 0 }
	},
	.bpp = 16,
	.mem16_size = MEM16_SIZE,
	.handle_event = isaku_handle_event,
	.mem_init = isaku_mem_init,
	.mem_restore = isaku_mem_restore,
	.draw_text_zen = isaku_draw_text,
	.draw_text_han = isaku_draw_text,
	.expr_op = { DEFAULT_EXPR_OP },
	.stmt_op = { DEFAULT_STMT_OP },
	.sys = {
		[0]  = sys_set_font_size,
		[1]  = sys_display_number,
		[2]  = isaku_cursor,
		[3]  = isaku_anim,
		[4]  = isaku_savedata,
		[5]  = isaku_audio,
		[6]  = isaku_voice,
		[7]  = sys_load_file,
		[8]  = sys_load_image,
		[9]  = isaku_display,
		[10] = isaku_graphics,
		[11] = sys_wait,
		[12] = sys_set_text_colors_direct,
		[13] = sys_farcall,
		[14] = sys_get_cursor_segment,
		[15] = sys_menu_get_no,
		[18] = sys_check_input,
		[20] = isaku_dungeon,
		[22] = isaku_item_window,
		[24] = isaku_strlen,
		[25] = isaku_save_menu,
		[26] = isaku_load_menu,
		[27] = sys_27,
	},
	.util = {
		[2]  = util_warn_unimplemented,
		[3]  = util_load_heap,
		[4]  = util_save_heap,
		[7]  = util_delay,
		[11] = util_warn_unimplemented,
		[12] = util_warn_unimplemented,
	},
	.flags = {
		[FLAG_ANIM_ENABLE]  = 0x0004,
		[FLAG_MENU_RETURN]  = 0x0008,
		[FLAG_RETURN]       = 0x0010,
		[FLAG_PROC_CLEAR]   = 0x0040,
		[FLAG_VOICE_ENABLE] = 0x0100,
		[FLAG_AUDIO_ENABLE] = 0x0200,
		[FLAG_STRLEN]       = 0x0400,
		[FLAG_WAIT_KEYUP]   = 0x0800,
	}
};
