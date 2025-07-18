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
#include "ai5/mes.h"

#include "audio.h"
#include "cursor.h"
#include "gfx.h"
#include "gfx_private.h"
#include "input.h"
#include "memory.h"
#include "texthook.h"
#include "vm.h"
#include "vm_private.h"

#include "shuusaku.h"

#define MENU_BUTTON_H 34
#define MENU_ENTRY_H 52
#define MENU_BG_Y 204
#define MENU_MAX_H 276

#define MENU_ENTRY_OFF(i) (MENU_BUTTON_H + MENU_ENTRY_H * (i))
#define MENU_PG_DOWN_OFF MENU_ENTRY_OFF(4)

#define MENU_ENTRY_BASE 520
#define MENU_BUTTON_BASE 936
#define MENU_ENTRY_SRC(i) (MENU_ENTRY_BASE + MENU_ENTRY_H * (i))
#define MENU_BUTTON_SRC(i) (MENU_BUTTON_BASE + MENU_BUTTON_H * (i))

#define MENU_FRAME_GRAY_Y (MENU_BUTTON_H * 1)
#define MENU_FRAME_GREEN_Y (MENU_BUTTON_H * 2)
#define MENU_FRAME_BLUE_Y (MENU_BUTTON_H * 3)
#define MENU_FRAME_RED_Y (MENU_BUTTON_H * 4)
#define MENU_FRAME_PINK_Y (MENU_BUTTON_H * 5)

struct menu_data {
	struct menu_entry *entries;
	unsigned nr_entries;
	unsigned x;
	unsigned y;
	unsigned chunk_w;
	unsigned page;
	SDL_Rect buttons[6];
};

static void init_menu_entry(unsigned frame_y, unsigned dst_y, unsigned w, uint8_t color)
{
	const unsigned h = MENU_BUTTON_H - 8;
	const unsigned sel_y = dst_y + MENU_ENTRY_H * 4;
	// copy frame (unselected)
	gfx_copy_masked(0, frame_y, w, h, 5, 0, dst_y, 5, MASK_COLOR);
	gfx_copy_masked(0, frame_y + 8, w, h, 5, 0, dst_y + h, 5, MASK_COLOR);
	// copy frame + fill (selected)
	gfx_copy(0, dst_y, w, MENU_ENTRY_H, 5, 0, sel_y, 5);
	gfx_fill(8, sel_y + 8, w - 16, MENU_ENTRY_H - 16, 5, color);
}

static void draw_scroll_button(struct menu_data *menu, unsigned src_y, unsigned dst_y,
		const char *ch)
{
	const unsigned w = (menu->chunk_w + 1) * 16;
	const unsigned sel_y = dst_y + MENU_BUTTON_H * 2;
	// copy bg
	gfx_copy(0, src_y, w, MENU_BUTTON_H, 5, 0, dst_y, 5);
	// copy frame
	gfx_copy_masked(0, MENU_FRAME_GRAY_Y, w, MENU_BUTTON_H, 5, 0, dst_y, 5, MASK_COLOR);
	// copy frame+bg
	gfx_copy(0, dst_y, w, MENU_BUTTON_H, 5, 0, sel_y, 5);
	// fill inside of button
	gfx_fill(8, sel_y + 8, w - 16, MENU_BUTTON_H - 16, 5, 24);
	// XXX: chunk_w is mult by 16, cursor_x is mult by 8 -> chunk_w = midpoint
	mem_set_sysvar16(mes_sysvar16_text_cursor_x, menu->chunk_w);
	mem_set_sysvar16(mes_sysvar16_text_cursor_y, dst_y + 8);
	shuusaku_draw_text(ch);
	mem_set_sysvar16(mes_sysvar16_text_cursor_x, menu->chunk_w);
	mem_set_sysvar16(mes_sysvar16_text_cursor_y, dst_y + MENU_BUTTON_H * 2 + 8);
	shuusaku_draw_text(ch);
}

static void draw_menu_text(uint32_t body_addr, unsigned chunk_w, unsigned dst_y)
{
	mem_set_sysvar16(mes_sysvar16_text_start_x, 1);
	mem_set_sysvar16(mes_sysvar16_text_start_y, dst_y + 8);
	mem_set_sysvar16(mes_sysvar16_text_end_x, chunk_w * 2 + 1);
	mem_set_sysvar16(mes_sysvar16_text_end_y, dst_y + 8 + 32);
	mem_set_sysvar16(mes_sysvar16_text_cursor_x, 1);
	mem_set_sysvar16(mes_sysvar16_text_cursor_y, dst_y + 8);
	vm.ip.ptr = body_addr;
	game->vm.exec();
}

static void draw_menu(struct menu_data *menu)
{
	mem_set_sysvar16(mes_sysvar16_dst_surface, 5);
	const unsigned w = (menu->chunk_w + 1) * 16;

	if (menu->page == 0) {
		// no up arrow: copy bg
		gfx_copy(0, MENU_BG_Y, w, MENU_BUTTON_H, 5, 0, 936, 5);
		gfx_copy(0, MENU_BG_Y, w, MENU_BUTTON_H, 5, menu->x, menu->y, 0);
	} else {
		// up arrow
		draw_scroll_button(menu, MENU_BG_Y, 936, "\x81\xa3");//"▲");
		gfx_copy(0, 936, w, MENU_BUTTON_H, 5, menu->x, menu->y, 0);
	}

	const unsigned page_entries = menu->nr_entries - menu->page * 4;
	for (unsigned i = 0; i < 4; i++) {
		const unsigned y_off = i * 52;
		const unsigned bg_y = MENU_BG_Y + MENU_BUTTON_H + y_off;
		const unsigned dst_y = 520 + y_off;

		if (i >= page_entries) {
			// no entry: copy bg to screen
			gfx_copy(0, bg_y, w, MENU_ENTRY_H, 5, menu->x, menu->y + MENU_BUTTON_H + y_off, 0);
			continue;
		}

		// copy bg
		gfx_copy(0, bg_y, w, 52, 5, 0, dst_y, 5);
		// copy frame
		unsigned menu_no = i + menu->page * 4;
		uint8_t frame_type = mem_get_var4_packed(2010 + (menu->entries[menu_no].index - 1));
		if (frame_type == 0) {
			init_menu_entry(MENU_FRAME_GRAY_Y, dst_y, w, 24);
		} else if (frame_type == 1) {
			init_menu_entry(MENU_FRAME_GREEN_Y, dst_y, w, 15);
		} else if (frame_type < 4) {
			init_menu_entry(MENU_FRAME_BLUE_Y, dst_y, w, 16);
		} else if (frame_type < 8) {
			init_menu_entry(MENU_FRAME_RED_Y, dst_y, w, 17);
		} else if (frame_type == 8) {
			init_menu_entry(MENU_FRAME_PINK_Y, dst_y, w, 18);
		} else {
			WARNING("Unexpected menu frame type: %d", frame_type);
		}
		// call menu entry body
		uint32_t body_addr = menu->entries[menu->page * 4 + i].body_addr;
		draw_menu_text(body_addr, menu->chunk_w, dst_y);
		draw_menu_text(body_addr, menu->chunk_w, dst_y + MENU_ENTRY_H * 4);
		// copy entry to screen
		gfx_copy(0, dst_y, w, MENU_ENTRY_H, 5, menu->x, menu->y + MENU_BUTTON_H + y_off, 0);
	}
	if (menu->page * 4 + 4 >= menu->nr_entries) {
		// no down arrow: copy bg
		const unsigned y_off = MENU_MAX_H - MENU_BUTTON_H;
		gfx_copy(0, MENU_BG_Y + y_off, w, MENU_BUTTON_H, 5, 0, 970, 5);
		gfx_copy(0, MENU_BG_Y + y_off, w, MENU_BUTTON_H, 5, menu->x, menu->y + y_off, 0);
	} else {
		// down arrow
		const unsigned y_off = MENU_MAX_H - MENU_BUTTON_H;
		draw_scroll_button(menu, MENU_BG_Y + MENU_BUTTON_H + MENU_ENTRY_H * 4, 970,
				"\x81\xa5");//"▼");
		gfx_copy(0, 970, w, MENU_BUTTON_H, 5, menu->x, menu->y + y_off, 0);
	}

	mem_set_sysvar16(mes_sysvar16_dst_surface, 0);
}

static void init_menu_frame(unsigned frame_x, unsigned dst_y, int chunk_w)
{
	gfx_copy(frame_x, 0, 8, MENU_BUTTON_H, 5, 0, dst_y, 5);
	for (int chunk = 0; chunk < chunk_w; chunk++) {
		gfx_copy(frame_x + 8, 0, 16, MENU_BUTTON_H, 5, 8 + chunk * 16, dst_y, 5);
	}
	gfx_copy(frame_x + 24, 0, 8, MENU_BUTTON_H, 5, 8 + chunk_w * 16, dst_y, 5);
}

enum menu_button {
	MENU_NONE = -1,
	MENU_PAGE_UP = 0,
	MENU_ENTRY_0 = 1,
	MENU_ENTRY_1 = 2,
	MENU_ENTRY_2 = 3,
	MENU_ENTRY_3 = 4,
	MENU_PAGE_DOWN = 5,
};

static void menu_draw_selection(struct menu_data *menu, enum menu_button prev,
		enum menu_button cur)
{
	const unsigned w = (menu->chunk_w + 1) * 16;
	switch (prev) {
	case MENU_NONE:
		break;
	case MENU_PAGE_UP:
		gfx_copy(0, MENU_BUTTON_SRC(0), w, MENU_BUTTON_H, 5,
				menu->x, menu->y, 0);
		break;
	case MENU_PAGE_DOWN:
		gfx_copy(0, MENU_BUTTON_SRC(1), w, MENU_BUTTON_H, 5,
				menu->x, menu->y + MENU_PG_DOWN_OFF, 0);
		break;
	default:
		gfx_copy(0, MENU_ENTRY_SRC(prev - 1), w, MENU_ENTRY_H, 5,
				menu->x, menu->y + MENU_ENTRY_OFF(prev - 1), 0);
		break;
	}

	switch (cur) {
	case MENU_NONE:
		break;
	case MENU_PAGE_UP:
		gfx_copy(0, MENU_BUTTON_SRC(2), w, MENU_BUTTON_H, 5,
				menu->x, menu->y, 0);
		break;
	case MENU_PAGE_DOWN:
		gfx_copy(0, MENU_BUTTON_SRC(3), w, MENU_BUTTON_H, 5,
				menu->x, menu->y + MENU_PG_DOWN_OFF, 0);
		break;
	default:
		gfx_copy(0, MENU_ENTRY_SRC(cur + 3), w, MENU_ENTRY_H, 5,
				menu->x, menu->y + MENU_ENTRY_OFF(cur - 1), 0);
		break;
	}
}

static enum menu_button menu_get_selected(struct menu_data *menu)
{
	if (SDL_GetMouseFocus() != gfx.window)
		return MENU_NONE;

	SDL_Point mouse;
	cursor_get_pos((unsigned*)&mouse.x, (unsigned*)&mouse.y);
	for (int i = 0; i < 6; i++) {
		if (SDL_PointInRect(&mouse, &menu->buttons[i]))
			return i;
	}
	return MENU_NONE;
}

static void menu_set_button_hotspots(struct menu_data *menu)
{
	if (menu->page > 0) {
		menu->buttons[0] = (SDL_Rect) {
			.x = menu->x + 8,
			.y = menu->y + 8,
			.w = menu->chunk_w * 16,
			.h = MENU_BUTTON_H - 16,
		};
	} else {
		menu->buttons[0] = (SDL_Rect) { 0, 0, 0, 0 };
	}
	if (menu->page * 4 + 4 < menu->nr_entries) {
		menu->buttons[5] = (SDL_Rect) {
			.x = menu->x + 8,
			.y = menu->y + MENU_PG_DOWN_OFF + 8,
			.w = menu->chunk_w * 16,
			.h = MENU_BUTTON_H - 16,
		};
	} else {
		menu->buttons[5] = (SDL_Rect) { 0, 0, 0, 0 };
	}

	const unsigned page_entries = menu->nr_entries - menu->page * 4;
	for (unsigned i = 0; i < 4; i++) {
		if (i < page_entries) {
			menu->buttons[i + 1] = (SDL_Rect) {
				.x = menu->x + 8,
				.y = menu->y + MENU_ENTRY_OFF(i) + 8,
				.w = menu->chunk_w * 16,
				.h = MENU_ENTRY_H - 16,
			};
		} else {
			menu->buttons[i + 1] = (SDL_Rect) { 0, 0, 0, 0 };
		}
	}
}

static unsigned menuexec_roulette(struct menu_entry *entries, unsigned nr_entries);

unsigned shuusaku_menuexec(struct menu_entry *entries, unsigned nr_entries, unsigned mode)
{
	if (mode == 1) {
		return menuexec_roulette(entries, nr_entries);
	}

	const uint16_t text_cursor_x = mem_get_sysvar16(mes_sysvar16_text_cursor_x);
	const uint16_t text_cursor_y = mem_get_sysvar16(mes_sysvar16_text_cursor_y);
	const uint16_t text_start_x = mem_get_sysvar16(mes_sysvar16_text_start_x);
	const uint16_t text_start_y = mem_get_sysvar16(mes_sysvar16_text_start_y);
	const uint16_t text_end_x = mem_get_sysvar16(mes_sysvar16_text_end_x);
	const uint16_t text_end_y = mem_get_sysvar16(mes_sysvar16_text_end_y);

	struct menu_data menu = {
		.entries = entries,
		.nr_entries = nr_entries,
		.x = mem_get_var16(110) * 8,
		.y = mem_get_var16(111),
		.chunk_w = mem_get_var16(112),
		.page = min(mem_get_var16(115), (nr_entries - 1) / 4),
	};
	unsigned visible_entries = min(nr_entries, 4);

	// convert center-y to top-y
	menu.y -= MENU_BUTTON_H + (visible_entries * MENU_ENTRY_H) / 2;
	mem_set_var16(113, menu.y);

	// copy area underneath menu
	gfx_copy(menu.x, menu.y, (menu.chunk_w + 1) * 16, MENU_MAX_H, 0, 0, MENU_BG_Y, 5);

	// draw frames
	init_menu_frame(32 * 0, MENU_FRAME_GRAY_Y,  menu.chunk_w);
	init_menu_frame(32 * 1, MENU_FRAME_GREEN_Y, menu.chunk_w);
	init_menu_frame(32 * 2, MENU_FRAME_BLUE_Y,  menu.chunk_w);
	init_menu_frame(32 * 3, MENU_FRAME_RED_Y,   menu.chunk_w);
	init_menu_frame(32 * 4, MENU_FRAME_PINK_Y,  menu.chunk_w);

	draw_menu(&menu);
	menu_set_button_hotspots(&menu);
	texthook_commit();

	int ret = 0;
	enum menu_button prev_selection = MENU_NONE;
	while (true) {
		// get selected entry
		enum menu_button selection = menu_get_selected(&menu);
		if (selection != prev_selection) {
			menu_draw_selection(&menu, prev_selection, selection);
			prev_selection = selection;
		}

		// handle click
		if (input_down(INPUT_ACTIVATE)) {
			input_wait_until_up(INPUT_ACTIVATE);
			switch (selection) {
			case MENU_NONE:
				break;
			case MENU_PAGE_UP:
				menu.page -= 1;
				draw_menu(&menu);
				menu_set_button_hotspots(&menu);
				break;
			case MENU_PAGE_DOWN:
				menu.page += 1;
				draw_menu(&menu);
				menu_set_button_hotspots(&menu);
				break;
			default:
				ret = menu.entries[menu.page*4 + selection - 1].index;
				goto finish;
			}
		} else if (mode == 2 && input_down(INPUT_CANCEL)) {
			input_wait_until_up(INPUT_CANCEL);
			ret = 0;
			goto finish;
		}
		vm_peek();
	}

finish:
	gfx_copy(0, MENU_BG_Y, (menu.chunk_w + 1) * 16, MENU_MAX_H, 5, menu.x, menu.y, 0);
	mem_set_var16(115, menu.page);

	mem_set_sysvar16(mes_sysvar16_text_cursor_x, text_cursor_x);
	mem_set_sysvar16(mes_sysvar16_text_cursor_y, text_cursor_y);
	mem_set_sysvar16(mes_sysvar16_text_start_x, text_start_x);
	mem_set_sysvar16(mes_sysvar16_text_start_y, text_start_y);
	mem_set_sysvar16(mes_sysvar16_text_end_x, text_end_x);
	mem_set_sysvar16(mes_sysvar16_text_end_y, text_end_y);
	return ret;
}

static void draw_roulette_border(void)
{
	// corners
	gfx_copy(320, 0, 16, 16, 5, 56, 119, 0);
	gfx_copy_masked(336, 0, 16, 16, 5, 568, 119, 0, MASK_COLOR);
	gfx_copy_masked(320, 16, 16, 16, 5, 56, 343, 0, MASK_COLOR);
	gfx_copy(336, 16, 16, 16, 5, 568, 343, 0);

	// top/bottom
	for (int i = 0; i < 31; i++) {
		gfx_copy(352, 0, 16, 16, 5, 72 + i * 16, 119, 0);
		gfx_copy(352, 0, 16, 16, 5, 72 + i * 16, 343, 0);
	}

	// left/right
	for (int i = 0; i < 13; i++) {
		if (i == 6) {
			// arrow
			gfx_copy(384, 0, 16, 16, 5, 56, 135 + i * 16, 0);
			gfx_copy(384, 16, 16, 16, 5, 568, 135 + i * 16, 0);
		} else {
			// typical
			gfx_copy(368, 0, 16, 16, 5, 56, 135 + i * 16, 0);
			gfx_copy(368, 0, 16, 16, 5, 568, 135 + i * 16, 0);
		}
	}
}

static void init_roulette_menu_entry(unsigned frame_y, unsigned dst_y, unsigned w)
{
	const unsigned h = MENU_BUTTON_H - 8;
	gfx_copy(0, frame_y, w, h, 5, 0, dst_y, 5);
	gfx_copy(0, frame_y + 8, w, h, 5, 0, dst_y + h, 5);
}

static void draw_roulette(int center_y, int menu_h)
{
	// draw background
	gfx_copy(16, MENU_BG_Y + 16, 496, 208, 5, 72, 135, 0);
	int src_top = center_y - 104;
	if (src_top < 0) {
		// wrap back
		int h = -src_top;
		gfx_copy_masked(0, 520 + (menu_h - h), 496, h, 5, 72, 135, 0, MASK_COLOR);
		gfx_copy_masked(0, 520, 496, 208 - h, 5, 72, 135 + h, 0, MASK_COLOR);
	} else if (src_top + 208 > menu_h) {
		// wrap forward
		int h = menu_h - src_top;
		gfx_copy_masked(0, 520 + src_top, 496, h, 5, 72, 135, 0, MASK_COLOR);
		gfx_copy_masked(0, 520, 496, 208 - h, 5, 72, 135 + h, 0, MASK_COLOR);
	} else {
		gfx_copy_masked(0, 520 + src_top, 496, 208, 5, 72, 135, 0, MASK_COLOR);
	}
}

/*
 * XXX: syuusaku.exe's version of this is more generic. But since it's only
 *      used in one specific circumstance, we simplify things by hardcoding.
 * XXX: The game code includes a condition to run the roulette when placing
 *      a camera, but the flag that controls this doesn't appear to be set
 *      at any point.
 */
static unsigned menuexec_roulette(struct menu_entry *entries, unsigned nr_entries)
{
	audio_se_play("me55a.wav", 0);

	const uint16_t text_cursor_x = mem_get_sysvar16(mes_sysvar16_text_cursor_x);
	const uint16_t text_cursor_y = mem_get_sysvar16(mes_sysvar16_text_cursor_y);
	const uint16_t text_start_x = mem_get_sysvar16(mes_sysvar16_text_start_x);
	const uint16_t text_start_y = mem_get_sysvar16(mes_sysvar16_text_start_y);
	const uint16_t text_end_x = mem_get_sysvar16(mes_sysvar16_text_end_x);
	const uint16_t text_end_y = mem_get_sysvar16(mes_sysvar16_text_end_y);

	// copy area underneath menu
	gfx_copy(56, 119, 528, 240, 0, 0, MENU_BG_Y, 5);

	// draw frames
	init_menu_frame(32 * 0, MENU_FRAME_GRAY_Y,  30);
	init_menu_frame(32 * 1, MENU_FRAME_GREEN_Y, 30);
	init_menu_frame(32 * 2, MENU_FRAME_BLUE_Y,  30);
	init_menu_frame(32 * 3, MENU_FRAME_RED_Y,   30);
	init_menu_frame(32 * 4, MENU_FRAME_PINK_Y,  30);

	mem_set_sysvar16(mes_sysvar16_dst_surface, 5);

	// draw entries
	for (unsigned i = 0; i < nr_entries; i++) {
		const unsigned y_off = i * 52;
		const unsigned dst_y = 520 + y_off;

		uint8_t frame_type = mem_get_var4_packed(2010 + (entries[i].index - 1));
		if (frame_type == 0) {
			init_roulette_menu_entry(MENU_FRAME_GRAY_Y, dst_y, 496);
		} else if (frame_type == 1) {
			init_roulette_menu_entry(MENU_FRAME_GREEN_Y, dst_y, 496);
		} else if (frame_type < 4) {
			init_roulette_menu_entry(MENU_FRAME_BLUE_Y, dst_y, 496);
		} else if (frame_type < 8) {
			init_roulette_menu_entry(MENU_FRAME_RED_Y, dst_y, 496);
		} else if (frame_type == 8) {
			init_roulette_menu_entry(MENU_FRAME_PINK_Y, dst_y, 496);
		} else {
			WARNING("Unexpected menu frame type: %d", frame_type);
		}
		// call menu entry body
		draw_menu_text(entries[i].body_addr, 30, dst_y);
	}

	draw_roulette_border();

	int ticks = rand() % 16 + 7;
	unsigned center_y = MENU_ENTRY_H/2;
	unsigned menu_h = nr_entries * MENU_ENTRY_H;
	for (int i = 0; i < ticks; i++) {
		for (int frame = 0; frame < 4; frame++) {
			draw_roulette(center_y, menu_h);
			vm_peek();
			vm_delay(16);
			center_y = (center_y + 13) % menu_h; 
		}
	}
	draw_roulette(center_y, menu_h);

	audio_se_play("me55b.wav", 0);

	while (!input_down(INPUT_ACTIVATE)) {
		vm_peek();
		vm_delay(16);
	}
	while (input_down(INPUT_ACTIVATE)) {
		vm_peek();
		vm_delay(16);
	}

	gfx_copy(0, MENU_BG_Y, 528, 240, 5, 56, 119, 0);

	mem_set_sysvar16(mes_sysvar16_dst_surface, 0);
	mem_set_sysvar16(mes_sysvar16_text_cursor_x, text_cursor_x);
	mem_set_sysvar16(mes_sysvar16_text_cursor_y, text_cursor_y);
	mem_set_sysvar16(mes_sysvar16_text_start_x, text_start_x);
	mem_set_sysvar16(mes_sysvar16_text_start_y, text_start_y);
	mem_set_sysvar16(mes_sysvar16_text_end_x, text_end_x);
	mem_set_sysvar16(mes_sysvar16_text_end_y, text_end_y);
	mem_set_var16(200, mem_get_var16(200) + 3);
	return entries[ticks % nr_entries].index;
}
