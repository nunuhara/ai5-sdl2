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
#include "vm.h"

enum menu_entry_type {
	MENU_ENTRY_LABEL,
	MENU_ENTRY_RADIO,
	MENU_ENTRY_SUBMENU,
	MENU_ENTRY_SEPARATOR,
};

struct menu_entry;

struct menu {
	int next_id;
	vector_t(struct menu_entry) entries;
};

struct menu_label {
	char *hotkey;
	void(*on_click)(void*);
	void *data;
};

struct menu_radio {
	int head_id;
	int count;
	bool on;
	void(*on_click)(int,void*);
	int index;
	void *data;
};

struct menu_entry {
	int id;
	enum menu_entry_type type;
	union {
		struct menu_label label;
		struct menu_radio radio;
		struct menu *submenu;
	};
	bool active;
	int icon_no;
	char *text;

	int y;
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
			free(e->text);
			if (e->label.hotkey)
				free(e->label.hotkey);
			break;
		case MENU_ENTRY_RADIO:
			free(e->text);
			break;
		case MENU_ENTRY_SUBMENU:
			free(e->text);
			popup_menu_free(e->submenu);
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
	e->text = xstrdup(label);
	e->label.hotkey = hotkey ? xstrdup(hotkey) : NULL;
	e->label.on_click = on_click;
	e->label.data = data;
	e->active = true;
	e->icon_no = icon_no;
	return e->id;
}

int popup_menu_append_submenu(struct menu *m, int icon_no, const char *label,
		struct menu *submenu)
{
	struct menu_entry *e = vector_pushp(struct menu_entry, m->entries);
	e->id = m->next_id++;
	e->type = MENU_ENTRY_SUBMENU;
	e->submenu = submenu;
	e->text = xstrdup(label);
	e->active = true;
	e->icon_no = icon_no;
	return e->id;
}

int popup_menu_append_radio_group(struct menu *m, const char **labels, int nr_labels,
		int dflt, void(*on_click)(int,void*), void *data)
{
	int id = m->next_id;
	for (int i = 0; i < nr_labels; i++) {
		struct menu_entry *e = vector_pushp(struct menu_entry, m->entries);
		e->id = m->next_id++;
		e->type = MENU_ENTRY_RADIO;
		e->text = xstrdup(labels[i]);
		e->radio.head_id = id;
		e->radio.count = nr_labels;
		e->radio.on = i == dflt;
		e->radio.on_click = on_click;
		e->radio.index = i;
		e->radio.data = data;
		e->active = true;
		e->icon_no = -1;
	}

	return id;
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
	SDL_Texture *texture;
	SDL_Surface *surface;
	uint32_t window_id;
	bool opened;
	int x, y;
	int width;
	int height;
	int text_x;
	int hotkey_x;

	struct menu_window *parent;
	struct menu_window *child;
};

struct popup_delayed_open {
	uint32_t t;
	struct menu *menu;
	struct menu_entry *entry;
	struct menu_window *parent;
};

struct popup_delayed_close {
	uint32_t t;
	struct menu_window *window;
};

#define POPUP_MAX_DELAYED 16
struct popup_delayed_open delayed_opens[POPUP_MAX_DELAYED];
struct popup_delayed_close delayed_closes[POPUP_MAX_DELAYED];

static void delayed_open(struct menu_window *parent, struct menu_entry *entry,
		struct menu *child, int ms)
{
	for (int i = 0; i < POPUP_MAX_DELAYED; i++) {
		struct popup_delayed_open *d = &delayed_opens[i];
		if (!d->t) {
			d->t = SDL_GetTicks() + ms;
			d->menu = child;
			d->entry = entry;
			d->parent = parent;
			return;
		}
		if (d->menu == child && d->parent == parent) {
			d->t = SDL_GetTicks() + ms;
			return;
		}
	}
	WARNING("too many delayed popup opens");
}

static void delayed_close(struct menu_window *w, int ms)
{
	for (int i = 0; i < POPUP_MAX_DELAYED; i++) {
		struct popup_delayed_close *d = &delayed_closes[i];
		if (!d->t) {
			d->t = SDL_GetTicks() + ms;
			d->window = w;
			return;
		}
		if (d->window == w) {
			d->t = SDL_GetTicks() + ms;
			return;
		}
	}
	WARNING("too many delayed popup closes");
}

static void cancel_delayed_close(struct menu_window *w)
{
	for (int i = 0; i < POPUP_MAX_DELAYED; i++) {
		struct popup_delayed_close *d = &delayed_closes[i];
		if (d->t && d->window == w) {
			d->t = 0;
			return;
		}
	}
}

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

static void draw_arrow(SDL_Surface *s, int x, int y, bool selected)
{
	static SDL_Surface *arrow = NULL;
	static SDL_Surface *arrow_sel = NULL;
	static uint8_t arrow_pixels[] = {
		1, 0, 0, 0,
		1, 1, 0, 0,
		1, 1, 1, 0,
		1, 1, 1, 1,
		1, 1, 1, 0,
		1, 1, 0, 0,
		1, 0, 0, 0
	};

	if (!arrow) {
		SDL_CTOR(SDL_CreateRGBSurfaceWithFormatFrom, arrow,
				arrow_pixels, 4, 7, 8, 4, SDL_PIXELFORMAT_INDEX8);
		SDL_CTOR(SDL_CreateRGBSurfaceWithFormatFrom, arrow_sel,
				arrow_pixels, 4, 7, 8, 4, SDL_PIXELFORMAT_INDEX8);
		SDL_Color pal[2] = { popup_menu_bg_color, popup_menu_fg_color };
		SDL_Color sel_pal[2] = { popup_menu_sel_bg_color, popup_menu_sel_fg_color };
		SDL_CALL(SDL_SetPaletteColors, arrow->format->palette, pal, 0, 2);
		SDL_CALL(SDL_SetPaletteColors, arrow_sel->format->palette, sel_pal, 0, 2);
	}

	SDL_Surface *src = selected ? arrow_sel : arrow;
	SDL_Rect r = { x, y - 4, 4, 7 };
	SDL_CALL(SDL_BlitSurface, src, NULL, s, &r);
}

static void draw_checkmark(SDL_Surface *s, int x, int y, bool selected)
{
	static SDL_Surface *check = NULL;
	static SDL_Surface *check_sel = NULL;
	static uint8_t check_pixels[] = {
		0, 0, 0, 0, 0, 0, 1,
		0, 0, 0, 0, 0, 1, 1,
		1, 0, 0, 0, 1, 1, 1,
		1, 1, 0, 1, 1, 1, 0,
		1, 1, 1, 1, 1, 0, 0,
		0, 1, 1, 1, 0, 0, 0,
		0, 0, 1, 0, 0, 0, 0
	};

	if (!check) {
		SDL_CTOR(SDL_CreateRGBSurfaceWithFormatFrom, check,
				check_pixels, 7, 7, 8, 7, SDL_PIXELFORMAT_INDEX8);
		SDL_CTOR(SDL_CreateRGBSurfaceWithFormatFrom, check_sel,
				check_pixels, 7, 7, 8, 7, SDL_PIXELFORMAT_INDEX8);
		SDL_Color pal[2] = { popup_menu_bg_color, popup_menu_fg_color };
		SDL_Color sel_pal[2] = { popup_menu_sel_bg_color, popup_menu_sel_fg_color };
		SDL_CALL(SDL_SetPaletteColors, check->format->palette, pal, 0, 2);
		SDL_CALL(SDL_SetPaletteColors, check_sel->format->palette, sel_pal, 0, 2);
	}

	SDL_Surface *src = selected ? check_sel : check;
	SDL_Rect r = { x, y - 4, 7, 7 };
	SDL_CALL(SDL_BlitSurface, src, NULL, s, &r);
}

static void draw_entry(struct menu_window *m, struct menu_entry *e, bool selected)
{
	switch (e->type) {
	case MENU_ENTRY_LABEL:
	case MENU_ENTRY_RADIO:
	case MENU_ENTRY_SUBMENU:
		break;
	case MENU_ENTRY_SEPARATOR:
		return;
	}

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
		if (icon) {
			SDL_Rect r = {
				BORDER_SIZE + ENTRY_ICON_PAD,
				e->y + (ENTRY_H - icon->h) / 2,
				icon->w,
				icon->h
			};
			SDL_CALL(SDL_BlitSurface, icon, NULL, m->surface, &r);
		}
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
	int text_y = e->y  + ENTRY_H/2;
	if (!e->active && !selected) {
		ui_draw_text(m->surface, m->text_x+1, text_y+1, e->text,
				popup_menu_inactive_bg_color);
		if (e->type == MENU_ENTRY_LABEL && e->label.hotkey)
			ui_draw_text(m->surface, m->hotkey_x+1, text_y+1, e->label.hotkey,
					popup_menu_inactive_bg_color);
	}
	ui_draw_text(m->surface, m->text_x, text_y, e->text, *fg_color);
	if (e->type == MENU_ENTRY_LABEL && e->label.hotkey)
		ui_draw_text(m->surface, m->hotkey_x, text_y, e->label.hotkey, *fg_color);

	if (e->type == MENU_ENTRY_RADIO && e->radio.on) {
		draw_checkmark(m->surface, BORDER_SIZE + 3, text_y, selected);
	}

	if (e->type == MENU_ENTRY_SUBMENU) {
		draw_arrow(m->surface, m->width - BORDER_SIZE - 8, text_y, selected);
	}

}

static struct menu_entry *popup_window_selected(struct menu_window *m)
{
	SDL_Window *focus_window = SDL_GetMouseFocus();
	if (!focus_window || m->window != focus_window)
		return NULL;

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
		case MENU_ENTRY_RADIO:
		case MENU_ENTRY_SUBMENU:
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

static void popup_window_free(struct menu_window *m);

static void popup_window_close(struct menu_window *m)
{
	if (!m->opened)
		return;
	if (m->child)
		popup_window_free(m->child);
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
	if (m->parent && m->parent->child == m)
		m->parent->child = NULL;
	cancel_delayed_close(m);
}

static void popup_window_close_all(struct menu_window *m)
{
	while (m->parent) {
		m = m->parent;
	}
	popup_window_close(m);
}

static void popup_window_update(struct menu_window *m)
{
	if (!m->opened)
		return;

	// update selection
	struct menu_entry *sel = popup_window_selected(m);
	if (!sel && m->selected && m->selected->type == MENU_ENTRY_SUBMENU) {
		// XXX: keep selected when mousing to submenu
	} else if (sel != m->selected) {
		if (m->selected) {
			draw_entry(m, m->selected, false);
			if (m->child) {
				delayed_close(m->child, 200);
			}
		}
		if (sel) {
			draw_entry(m, sel, true);
			if (sel->type == MENU_ENTRY_SUBMENU) {
				delayed_open(m, sel, sel->submenu, 200);
			}
		}
		m->selected = sel;
	}

	// update screen
	SDL_CALL(SDL_UpdateTexture, m->texture, NULL, m->surface->pixels, m->surface->pitch);
	SDL_CALL(SDL_RenderClear, m->renderer);
	SDL_CALL(SDL_RenderCopy, m->renderer, m->texture, NULL, NULL);
	SDL_RenderPresent(m->renderer);
}

static bool window_is_child(struct menu_window *m, int window_id)
{
	if (!m->child)
		return false;
	if (m->child->window_id == window_id)
		return true;
	return window_is_child(m->child, window_id);
}

static bool window_is_parent(struct menu_window *m, int window_id)
{
	if (!m->parent)
		return false;
	if (m->parent->window_id == window_id)
		return true;
	return window_is_parent(m->parent, window_id);
}

static bool window_is_related(struct menu_window *m, int window_id)
{
	return m->window_id == window_id || window_is_child(m, window_id)
		|| window_is_parent(m, window_id);
}

static void popup_window_handle_event(struct menu_window *m, SDL_Event *e)
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
			popup_window_update(m);
			break;
		case SDL_WINDOWEVENT_ENTER:
			cancel_delayed_close(m);
			popup_window_update(m);
			break;
		case SDL_WINDOWEVENT_LEAVE:
			popup_window_update(m);
			break;
		case SDL_WINDOWEVENT_CLOSE:
			popup_window_close_all(m);
			break;
		}
		break;
	case SDL_MOUSEBUTTONDOWN:
		if (!window_is_related(m, e->button.windowID))
			popup_window_close_all(m);
		break;
	case SDL_MOUSEBUTTONUP:
		if (e->button.windowID != m->window_id)
			break;
		if (!m->selected || !m->selected->active)
			break;
		if (m->selected->type == MENU_ENTRY_LABEL) {
			struct menu_label *label = &m->selected->label;
			popup_window_close_all(m);
			if (label->on_click)
				label->on_click(label->data);
		} else if (m->selected->type == MENU_ENTRY_RADIO) {
			struct menu_radio *radio = &m->selected->radio;
			popup_window_close_all(m);
			if (radio->on_click)
				radio->on_click(radio->index, radio->data);
		}
		break;
	case SDL_MOUSEMOTION:
		if (e->window.windowID == m->window_id)
			popup_window_update(m);
		break;
	}

	if (m->child)
		popup_window_handle_event(m->child, e);
}

// calculate size of frame and text positions
static void calc_size(struct menu *m, int *w, int *h, int *text_x, int *hotkey_x)
{
	int max_label = 0;
	int max_hotkey = 0;
	struct menu_entry *e;
	int height = BORDER_SIZE * 2;
	bool have_icon = false;
	vector_foreach_p(e, m->entries) {
		switch (e->type) {
		case MENU_ENTRY_SEPARATOR:
			height += SEPARATOR_H;
			break;
		case MENU_ENTRY_LABEL:
			max_label = max(max_label, ui_measure_text(e->text));
			if (e->icon_no >= 0)
				have_icon = true;
			if (e->label.hotkey)
				max_hotkey = max(max_hotkey, ui_measure_text(e->label.hotkey));
			height += ENTRY_H;
			break;
		case MENU_ENTRY_RADIO:
			max_label = max(max_label, ui_measure_text(e->text));
			have_icon = true;
			height += ENTRY_H;
			break;
		case MENU_ENTRY_SUBMENU:
			max_label = max(max_label, ui_measure_text(e->text));
			max_hotkey = max(max_hotkey, 4); // triangle
			height += ENTRY_H;
			break;
		}
	}

	int text_pad = ENTRY_TEXT_PAD;
	if (have_icon)
		text_pad += ENTRY_ICON_PAD + ENTRY_ICON_SIZE;
	*w = text_pad + max_label + (max_hotkey ? max_hotkey + ENTRY_HOTKEY_PAD : 0) + ENTRY_PAD_RIGHT;
	*h = height;
	*text_x = text_pad;
	*hotkey_x = text_pad + max_label + ENTRY_HOTKEY_PAD;
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
		case MENU_ENTRY_RADIO:
		case MENU_ENTRY_SUBMENU:
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

static void popup_window_init(struct menu_window *w, struct menu *m, int x, int y)
{
	w->menu = m;
	w->x = x;
	w->y = y;
	w->selected = NULL;
	calc_size(m, &w->width, &w->height, &w->text_x, &w->hotkey_x);
	SDL_CTOR(SDL_CreateWindow, w->window, "", x, y, w->width, w->height,
			SDL_WINDOW_BORDERLESS
			| SDL_WINDOW_POPUP_MENU
			| SDL_WINDOW_SKIP_TASKBAR);
	w->window_id = SDL_GetWindowID(w->window);
	SDL_CTOR(SDL_CreateRenderer, w->renderer, w->window, -1, 0);
	SDL_CALL(SDL_SetRenderDrawColor, w->renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
	SDL_CALL(SDL_RenderSetLogicalSize, w->renderer, w->width, w->height);
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, w->surface, 0, w->width, w->height,
			GFX_DIRECT_BPP, GFX_DIRECT_FORMAT);
	SDL_CTOR(SDL_CreateTexture, w->texture, w->renderer, w->surface->format->format,
			SDL_TEXTUREACCESS_STATIC, w->width, w->height);
	w->parent = NULL;
	w->child = NULL;

	// draw initial state (no selection)
	map_colors(w->surface->format);
	calc_layout(w);
	draw_frame(w);
	struct menu_entry *e;
	vector_foreach_p(e, m->entries) {
		draw_entry(w, e, false);
	}
}

static struct menu_window *popup_window_new(struct menu *menu, int x, int y)
{
	struct menu_window *w = xmalloc(sizeof(struct menu_window));
	popup_window_init(w, menu, x, y);
	return w;
}

static void popup_window_free(struct menu_window *m)
{
	popup_window_close(m);
	free(m);
}

static void run_delayed_open(struct popup_delayed_open *d)
{
	if (d->parent->selected != d->entry)
		return;
	int child_x = d->parent->x + d->parent->width - BORDER_SIZE;
	int child_y = d->parent->y + d->entry->y - BORDER_SIZE;
	struct menu_window *child = popup_window_new(d->menu, child_x, child_y);
	child->opened = true;
	child->parent = d->parent;
	if (d->parent->child) {
		popup_window_free(d->parent->child);
	}
	d->parent->child = child;
}

static void run_delayed_close(struct popup_delayed_close *d)
{
	popup_window_free(d->window);
}

void popup_menu_run(struct menu *m, int x, int y)
{
	struct menu_window w;
	popup_window_init(&w, m, x, y);

	// cursor may be disabled
	int cursor_state = SDL_ShowCursor(SDL_QUERY);
	SDL_ShowCursor(SDL_ENABLE);
	cursor_unload();

	w.opened = true;
	vm_timer_t timer = vm_timer_create();
	while (w.opened) {
		uint32_t ticks = SDL_GetTicks();
		for (int i = 0; i < POPUP_MAX_DELAYED; i++) {
			struct popup_delayed_open *open = &delayed_opens[i];
			if (open->t && open->t <= ticks) {
				run_delayed_open(open);
				open->t = 0;
			}
			struct popup_delayed_close *close = &delayed_closes[i];
			if (close->t && close->t <= ticks) {
				run_delayed_close(close);
				close->t = 0;
			}
		}
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_WINDOWEVENT && e.window.windowID == gfx.window_id) {
				handle_window_event(&e.window);
				if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
					popup_window_close_all(&w);
				}
			} else {
				popup_window_handle_event(&w, &e);
			}
		}
		gfx_update();
		vm_timer_tick(&timer, 16);
	}

	cursor_reload();
	SDL_ShowCursor(cursor_state);
}
