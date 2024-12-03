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

#include <SDL.h>

#include "nulib/vector.h"
#include "cursor.h"
#include "input.h"
#include "gfx_private.h"
#include "popup_menu.h"

enum menu_entry_type {
	MENU_ENTRY_LABEL,
	MENU_ENTRY_SEPARATOR,
	// TODO: sub-menu
};

struct menu_entry {
	int id;
	enum menu_entry_type type;
	char *label;
	char *hotkey;
	bool active;
	int icon_no;
	void(*on_click)(void*);
	void *data;

	int y;
};

struct menu {
	int next_id;
	vector_t(struct menu_entry) entries;
};

struct menu *popup_menu_new(void)
{
	return xcalloc(1, sizeof(struct menu));
}

void popup_menu_free(struct menu *menu)
{
	struct menu_entry *e;
	vector_foreach_p(e, menu->entries) {
		switch (e->type) {
		case MENU_ENTRY_LABEL:
			free(e->label);
			if (e->hotkey)
				free(e->hotkey);
			break;
		case MENU_ENTRY_SEPARATOR:
			break;
		}
	}
	vector_destroy(menu->entries);
	free(menu);
}

int popup_menu_append_entry(struct menu *m, int icon_no, const char *label,
		const char *hotkey, void(*on_click)(void*), void *data)
{
	struct menu_entry *e = vector_pushp(struct menu_entry, m->entries);
	e->id = m->next_id++;
	e->type = MENU_ENTRY_LABEL;
	e->label = xstrdup(label);
	e->hotkey = hotkey ? xstrdup(hotkey) : NULL;
	e->active = true;
	e->icon_no = icon_no;
	e->on_click = on_click;
	e->data = data;
	return e->id;
}

int popup_menu_append_separator(struct menu *m)
{
	struct menu_entry *e = vector_pushp(struct menu_entry, m->entries);
	e->id = m->next_id++;
	e->type = MENU_ENTRY_SEPARATOR;
	return e->id;
}

static struct menu_entry *find_entry(struct menu *m, int id)
{
	struct menu_entry *e;
	vector_foreach_p(e, m->entries) {
		if (e->id == id)
			return e;
	}
	return NULL;
}

void popup_menu_set_active(struct menu *m, int entry_id, bool active)
{
	struct menu_entry *e = find_entry(m, entry_id);
	if (!e) {
		WARNING("No menu entry with id: %d", entry_id);
		return;
	}
	e->active = active;
}

#define BORDER_SIZE 3
#define ENTRY_H 18
#define ENTRY_ICON_SIZE 16
#define ENTRY_ICON_PAD 2
#define ENTRY_TEXT_PAD 6
#define ENTRY_HOTKEY_PAD 12
#define ENTRY_PAD_RIGHT 14
#define SEPARATOR_H 8
#define SEPARATOR_PAD 2

SDL_Color popup_menu_bg_color = { 212, 208, 200 };
SDL_Color popup_menu_fg_color = { 24, 22, 18 };
SDL_Color popup_menu_sel_bg_color = { 61, 113, 163 };
SDL_Color popup_menu_sel_fg_color = { 248, 248, 248 };
SDL_Color popup_menu_inactive_bg_color = { 248, 248, 248 };
SDL_Color popup_menu_inactive_fg_color = { 166, 165, 164 };
SDL_Color popup_menu_border_bright_color = { 250, 249, 248 };
SDL_Color popup_menu_border_dim_color = { 103, 101, 97 };
SDL_Color popup_menu_border_dark_color = { 60, 59, 57 };

static uint32_t bg_color;
static uint32_t fg_color;
static uint32_t sel_bg_color;
static uint32_t sel_fg_color;
static uint32_t inactive_bg_color;
static uint32_t inactive_fg_color;
static uint32_t border_bright_color;
static uint32_t border_dim_color;
static uint32_t border_dark_color;

struct menu_window {
	struct menu *menu;
	struct menu_entry *selected;
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *bg;
	SDL_Texture *texture;
	SDL_Surface *surface;
	uint32_t window_id;
	bool opened;
	int width;
	int height;
	int hotkey_x;
};

static void fill_rect(SDL_Surface *s, int x, int y, int w, int h, uint32_t c)
{
	SDL_Rect r = { x, y, w, h };
	SDL_CALL(SDL_FillRect, s, &r, c);
}

static void draw_vline(SDL_Surface *s, int x, int y, int h, uint32_t c)
{
	fill_rect(s, x, y, 1, y+h, c);
}

static void draw_hline(SDL_Surface *s, int x, int y, int w, uint32_t c)
{
	fill_rect(s, x, y, x+w, 1, c);
}

static void draw_frame(struct menu_window *m)
{
	// fill background
	SDL_CALL(SDL_FillRect, m->surface, NULL, bg_color);

	// draw border bevels
	draw_hline(m->surface, 1, 1, m->width - 3, border_bright_color);
	draw_vline(m->surface, 1, 2, m->height - 4, border_bright_color);
	draw_vline(m->surface, m->width - 2, 1, m->height - 2, border_dim_color);
	draw_hline(m->surface, 1, m->height - 2, m->width - 3, border_dim_color);
	draw_vline(m->surface, m->width - 1, 0, m->height, border_dark_color);
	draw_hline(m->surface, 0, m->height - 1, m->width - 1, border_dark_color);

	// draw separators
	struct menu_entry *e;
	vector_foreach_p(e, m->menu->entries) {
		if (e->type != MENU_ENTRY_SEPARATOR)
			continue;
		draw_hline(m->surface, BORDER_SIZE + SEPARATOR_PAD, e->y + 3,
				m->width - BORDER_SIZE*2 - SEPARATOR_PAD*2,
				border_dim_color);
		draw_hline(m->surface, BORDER_SIZE + SEPARATOR_PAD, e->y + 4,
				m->width - BORDER_SIZE*2 - SEPARATOR_PAD*2,
				border_bright_color);
	}
}

static void draw_entry(struct menu_window *m, struct menu_entry *e, bool selected)
{
	if (e->type != MENU_ENTRY_LABEL)
		return;

	// background
	if (selected && e->active) {
		fill_rect(m->surface, BORDER_SIZE, e->y, m->width - BORDER_SIZE*2,
				ENTRY_H, sel_bg_color);
	} else {
		fill_rect(m->surface, BORDER_SIZE, e->y, m->width - BORDER_SIZE*2,
				ENTRY_H, bg_color);
	}

	// icon
	if (e->icon_no >= 0) {
		SDL_Surface *icon = icon_get(e->icon_no);
		SDL_Rect r = {
			BORDER_SIZE + ENTRY_ICON_PAD,
			e->y + (ENTRY_H - icon->h) / 2,
			icon->w,
			icon->h
		};
		SDL_CALL(SDL_BlitSurface, icon, NULL, m->surface, &r);
	}

	// text
	SDL_Color *fg_color;
	if (!e->active) {
		fg_color = &popup_menu_inactive_fg_color;
	} else if (selected) {
		fg_color = &popup_menu_sel_fg_color;
	} else {
		fg_color = &popup_menu_fg_color;
	}
	int text_x = ENTRY_ICON_PAD + ENTRY_ICON_SIZE + ENTRY_TEXT_PAD;
	int text_y = e->y  + ENTRY_H/2;
	if (!e->active && !selected) {
		ui_draw_text(m->surface, text_x+1, text_y+1, e->label,
				popup_menu_inactive_bg_color);
		if (e->hotkey)
			ui_draw_text(m->surface, m->hotkey_x+1, text_y+1, e->hotkey,
					popup_menu_inactive_bg_color);
	}
	ui_draw_text(m->surface, text_x, text_y, e->label, *fg_color);
	if (e->hotkey)
		ui_draw_text(m->surface, m->hotkey_x, text_y, e->hotkey, *fg_color);

}

static struct menu_entry *popup_menu_selected(struct menu_window *m)
{
	int mouse_x, mouse_y;
	SDL_GetMouseState(&mouse_x, &mouse_y);
	if (mouse_x < BORDER_SIZE || mouse_x >= m->width - BORDER_SIZE
			|| mouse_y < BORDER_SIZE || mouse_y >= m->height - BORDER_SIZE)
		return NULL;

	int y = BORDER_SIZE;
	struct menu_entry *e;
	vector_foreach_p(e, m->menu->entries) {
		int h = 0;
		switch (e->type) {
		case MENU_ENTRY_LABEL:
			h = ENTRY_H;
			break;
		case MENU_ENTRY_SEPARATOR:
			h = SEPARATOR_H;
			break;
		}
		if (mouse_y >= y && mouse_y < y + h)
			return e;
		y += h;
	}
	return NULL;
}

static void popup_menu_update(struct menu_window *m)
{
	if (!m->opened)
		return;

	struct menu_entry *sel = popup_menu_selected(m);
	if (sel != m->selected) {
		if (m->selected)
			draw_entry(m, m->selected, false);
		if (sel)
			draw_entry(m, sel, true);
		m->selected = sel;
	}

	SDL_CALL(SDL_UpdateTexture, m->texture, NULL, m->surface->pixels, m->surface->pitch);
	SDL_CALL(SDL_RenderClear, m->renderer);
	SDL_CALL(SDL_RenderCopy, m->renderer, m->texture, NULL, NULL);
	SDL_RenderPresent(m->renderer);
}

static void popup_menu_close(struct menu_window *m)
{
	if (!m->opened)
		return;
	SDL_FreeSurface(m->surface);
	SDL_DestroyTexture(m->texture);
	SDL_DestroyRenderer(m->renderer);
	SDL_DestroyWindow(m->window);
	m->window = NULL;
	m->renderer = NULL;
	m->texture = NULL;
	m->surface = NULL;
	m->window_id = 0;
	m->opened = false;
}

static void popup_menu_handle_event(struct menu_window *m, SDL_Event *e)
{
	switch (e->type) {
	case SDL_WINDOWEVENT:
		if (e->window.windowID != m->window_id)
			break;
		switch (e->window.event) {
		case SDL_WINDOWEVENT_SHOWN:
		case SDL_WINDOWEVENT_EXPOSED:
		case SDL_WINDOWEVENT_RESIZED:
		case SDL_WINDOWEVENT_SIZE_CHANGED:
		case SDL_WINDOWEVENT_MAXIMIZED:
		case SDL_WINDOWEVENT_RESTORED:
			popup_menu_update(m);
			break;
		case SDL_WINDOWEVENT_ENTER:
			cursor_unload();
			popup_menu_update(m);
			break;
		case SDL_WINDOWEVENT_LEAVE:
			cursor_reload();
			popup_menu_update(m);
			break;
		case SDL_WINDOWEVENT_FOCUS_LOST:
		case SDL_WINDOWEVENT_CLOSE:
			popup_menu_close(m);
			break;
		}
		break;
	case SDL_MOUSEBUTTONDOWN:
		if (e->button.windowID != m->window_id)
			popup_menu_close(m);
		break;
	case SDL_MOUSEBUTTONUP:
		if (e->button.windowID == m->window_id) {
			if (m->selected && m->selected->type == MENU_ENTRY_LABEL
					&& m->selected->active) {
				popup_menu_close(m);
				if (m->selected->on_click)
					m->selected->on_click(m->selected->data);
			}
		}
		break;
	case SDL_MOUSEMOTION:
		if (e->window.windowID == m->window_id)
			popup_menu_update(m);
		break;
	}
}

// calculate size of frame and hotkey text position
static void calc_size(struct menu *m, int *w, int *h, int *hotkey_x)
{
	int max_label = 0;
	int max_hotkey = 0;
	struct menu_entry *e;
	int height = BORDER_SIZE * 2;
	vector_foreach_p(e, m->entries) {
		switch (e->type) {
		case MENU_ENTRY_SEPARATOR:
			height += SEPARATOR_H;
			break;
		case MENU_ENTRY_LABEL:
			max_label = max(max_label, ui_measure_text(e->label));
			if (e->hotkey)
				max_hotkey = max(max_hotkey, ui_measure_text(e->hotkey));
			height += ENTRY_H;
			break;
		}
	}

	*w = ENTRY_ICON_PAD + ENTRY_ICON_SIZE + ENTRY_TEXT_PAD + max_label
		+ (max_hotkey ? max_hotkey + ENTRY_HOTKEY_PAD : 0) + ENTRY_PAD_RIGHT;
	*h = height;
	*hotkey_x = ENTRY_ICON_PAD + ENTRY_ICON_SIZE + ENTRY_TEXT_PAD + max_label + ENTRY_HOTKEY_PAD;
}

// calculate vertical position of each entry
static void calc_layout(struct menu_window *m)
{
	int y = BORDER_SIZE;
	struct menu_entry *e;
	vector_foreach_p(e, m->menu->entries) {
		e->y = y;
		switch (e->type) {
		case MENU_ENTRY_LABEL:
			y += ENTRY_H;
			break;
		case MENU_ENTRY_SEPARATOR:
			y += SEPARATOR_H;
			break;
		}
	}
}

#define map_color(f, c) SDL_MapRGB(f, c.r, c.g, c.b)

static void map_colors(SDL_PixelFormat *format)
{
	static bool mapped = false;
	if (mapped)
		return;

	mapped = true;
	bg_color = map_color(format, popup_menu_bg_color);
	fg_color = map_color(format, popup_menu_fg_color);
	sel_bg_color = map_color(format, popup_menu_sel_bg_color);
	sel_fg_color = map_color(format, popup_menu_sel_fg_color);
	inactive_bg_color = map_color(format, popup_menu_inactive_bg_color);
	inactive_fg_color = map_color(format, popup_menu_inactive_fg_color);
	border_bright_color = map_color(format, popup_menu_border_bright_color);
	border_dim_color = map_color(format, popup_menu_border_dim_color);
	border_dark_color = map_color(format, popup_menu_border_dark_color);
}

void popup_menu_run(struct menu *m, int x, int y)
{
	struct menu_window w;
	w.menu = m;
	w.selected = NULL;
	calc_size(m, &w.width, &w.height, &w.hotkey_x);
	SDL_CTOR(SDL_CreateWindow, w.window, "", x, y, w.width, w.height,
			SDL_WINDOW_BORDERLESS | SDL_WINDOW_POPUP_MENU);
	w.window_id = SDL_GetWindowID(w.window);
	SDL_CTOR(SDL_CreateRenderer, w.renderer, w.window, -1, 0);
	SDL_CALL(SDL_SetRenderDrawColor, w.renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
	SDL_CALL(SDL_RenderSetLogicalSize, w.renderer, w.width, w.height);
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, w.surface, 0, w.width, w.height,
			GFX_DIRECT_BPP, GFX_DIRECT_FORMAT);
	SDL_CTOR(SDL_CreateTexture, w.texture, w.renderer, w.surface->format->format,
			SDL_TEXTUREACCESS_STATIC, w.width, w.height);

	// draw initial state (no selection)
	map_colors(w.surface->format);
	calc_layout(&w);
	draw_frame(&w);
	struct menu_entry *e;
	vector_foreach_p(e, m->entries) {
		draw_entry(&w, e, false);
	}

	int cursor_state = SDL_ShowCursor(SDL_QUERY);
	SDL_ShowCursor(SDL_ENABLE);

	w.opened = true;
	while (w.opened) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_WINDOWEVENT && e.window.windowID == gfx.window_id) {
				handle_window_event(&e.window);
			} else {
				popup_menu_handle_event(&w, &e);
			}
		}
		gfx_update();
	}

	SDL_ShowCursor(cursor_state);
}
