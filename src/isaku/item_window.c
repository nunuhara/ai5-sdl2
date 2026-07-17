/* Copyright (C) 2026 Nunuhara Cabbage <nunuhara@haniwa.technology>
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
#include "ai5.h"
#include "cursor.h"
#include "gfx_private.h"
#include "input.h"

#include "isaku.h"

#define ITEM_WINDOW_W 320
#define ITEM_WINDOW_H 32
#define OVERLAY_X 160
#define OVERLAY_Y 224
struct {
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	uint32_t window_id;
	bool enabled;
	bool opened;
	bool created;
	bool lmb_down;
	bool rmb_down;
	bool use_overlay;
	int selected;
} item_window = {0};

static void item_window_create(void)
{
	int x, y;
	SDL_GetWindowPosition(gfx.window, &x, &y);
	SDL_CTOR(SDL_CreateWindow, item_window.window, "Items",
			x + config.itemwin.x, y + config.itemwin.y,
			ITEM_WINDOW_W, ITEM_WINDOW_H,
			SDL_WINDOW_HIDDEN);
	item_window.window_id = SDL_GetWindowID(item_window.window);
	SDL_CTOR(SDL_CreateRenderer, item_window.renderer, item_window.window, -1, 0);
	SDL_CALL(SDL_SetRenderDrawColor, item_window.renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
	SDL_CALL(SDL_RenderSetLogicalSize, item_window.renderer, ITEM_WINDOW_W, ITEM_WINDOW_H);
	SDL_CTOR(SDL_CreateTexture, item_window.texture, item_window.renderer,
			gfx.display->format->format, SDL_TEXTUREACCESS_STATIC,
			ITEM_WINDOW_W, ITEM_WINDOW_H);
}

static void item_overlay_create(void)
{
	SDL_Surface *s = gfx_get_overlay(1);
	SDL_Rect r = { 0, 0, 640, 480 };
	SDL_CALL(SDL_FillRect, s, &r, SDL_MapRGBA(s->format, 0, 0, 0, 200));
}

void isaku_item_window_create(void)
{
	if (item_window.use_overlay)
		item_overlay_create();
	else
		item_window_create();
	item_window.created = true;
	item_window.enabled = true;
	item_window.selected = -1;
}

static void item_window_update(void)
{
	SDL_CALL(SDL_UpdateTexture, item_window.texture, NULL, gfx.surface[7].s->pixels,
			gfx.surface[7].s->pitch);
	SDL_CALL(SDL_RenderClear, item_window.renderer);
	SDL_CALL(SDL_RenderCopy, item_window.renderer, item_window.texture, NULL, NULL);
	SDL_RenderPresent(item_window.renderer);
}

static void item_overlay_update(void)
{
	SDL_Rect src = { 0, 0, ITEM_WINDOW_W, ITEM_WINDOW_H };
	SDL_Rect dst = { OVERLAY_X, OVERLAY_Y, ITEM_WINDOW_W, ITEM_WINDOW_H };
	SDL_CALL(SDL_BlitSurface, gfx.surface[7].s, &src, gfx_get_overlay(1), &dst);
	gfx_screen_dirty();
}

void isaku_item_window_update(void)
{
	if (!item_window.opened)
		return;
	if (item_window.use_overlay)
		item_overlay_update();
	else
		item_window_update();
}

void isaku_item_window_toggle(void)
{
	if (!item_window.enabled)
		return;
	if (item_window.opened) {
		if (item_window.use_overlay)
			gfx_overlay_disable(1);
		else
			SDL_HideWindow(item_window.window);
		isaku_builtin_se_play("wincls.wav");
		item_window.opened = false;
	} else {
		if (item_window.use_overlay)
			gfx_overlay_enable(1);
		else
			SDL_ShowWindow(item_window.window);
		isaku_builtin_se_play("winopn.wav");
		item_window.opened = true;
		isaku_item_window_update();
	}
}

static void overlay_select_item(int i)
{
	cursor_set_pos(OVERLAY_X + 16 + i * 32, OVERLAY_Y + 16);
	item_window.selected = i;
}

void isaku_item_window_tick(void)
{
	if (!item_window.opened)
		return;

	if (input_down(INPUT_SPACE)) {
		_input_wait_until_up(INPUT_SPACE);
		isaku_item_window_toggle();
		return;
	}

	if (!item_window.use_overlay)
		return;

	if (input_down(INPUT_LEFT)) {
		_input_wait_until_up(INPUT_LEFT);
		if (item_window.selected < 0)
			overlay_select_item(0);
		else if (item_window.selected == 0)
			overlay_select_item(9);
		else
			overlay_select_item(item_window.selected - 1);
	} else if (input_down(INPUT_RIGHT)) {
		_input_wait_until_up(INPUT_RIGHT);
		if (item_window.selected < 0)
			overlay_select_item(0);
		else if (item_window.selected >= 9)
			overlay_select_item(0);
		else
			overlay_select_item(item_window.selected + 1);
	}
}

static void handle_button_event(SDL_MouseButtonEvent *e)
{
	if (e->button == SDL_BUTTON_LEFT) {
		item_window.lmb_down = e->state == SDL_PRESSED;
	} else if (e->button == SDL_BUTTON_RIGHT) {
		item_window.rmb_down = e->state == SDL_PRESSED;
	}
}

static bool item_window_event(SDL_Event *e)
{
	switch (e->type) {
	case SDL_WINDOWEVENT:
		if (e->window.windowID != item_window.window_id)
			return false;
		switch (e->window.event) {
		case SDL_WINDOWEVENT_SHOWN:
		case SDL_WINDOWEVENT_EXPOSED:
		case SDL_WINDOWEVENT_RESIZED:
		case SDL_WINDOWEVENT_SIZE_CHANGED:
		case SDL_WINDOWEVENT_MAXIMIZED:
		case SDL_WINDOWEVENT_RESTORED:
			isaku_item_window_update();
			break;
		case SDL_WINDOWEVENT_CLOSE:
			assert(item_window.opened);
			isaku_item_window_toggle();
			break;
		}
		return true;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		if (e->button.windowID != item_window.window_id)
			return false;
		handle_button_event(&e->button);
		return true;
	case SDL_KEYUP:
	case SDL_KEYDOWN:
		if (e->window.windowID != item_window.window_id)
			return false;
		// handle space only so that window can be closed when focused
		if (e->key.keysym.sym == SDLK_SPACE)
			input_key_event(INPUT_SPACE, e->type == SDL_KEYDOWN);
		break;
	}
	return false;
}

static bool item_overlay_event(SDL_Event *e)
{
	assert(item_window.opened);
	switch (e->type) {
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		if (e->button.windowID != gfx.window_id)
			return false;
		handle_button_event(&e->button);
		return true;
	}
	return false;
}

bool isaku_item_window_event(SDL_Event *e)
{
	if (item_window.use_overlay)
		return item_overlay_event(e);
	return item_window_event(e);
}

bool isaku_item_window_is_enabled(void)
{
	return item_window.enabled;
}

bool isaku_item_window_is_open(void)
{
	return item_window.enabled && item_window.opened;
}

void isaku_item_window_get_pos(int *x, int *y, int *w, int *h)
{
	if (!item_window.enabled) {
		*x = 0;
		*y = 0;
	} else if (item_window.use_overlay) {
		*x = OVERLAY_X;
		*y = OVERLAY_Y;
	} else if (item_window.window) {
		SDL_GetWindowPosition(item_window.window, x, y);
	} else {
		*x = 0;
		*y = 0;
	}
	*w = *x + ITEM_WINDOW_W - 1;
	*h = *y + ITEM_WINDOW_H - 1;
}

void isaku_item_window_get_cursor_pos(int *x, int *y)
{
	if (!item_window.enabled) {
		*x = ITEM_WINDOW_W;
		*y = ITEM_WINDOW_H;
	} else if (item_window.use_overlay) {
		SDL_GetMouseState(x, y);
		if (*x < OVERLAY_X)
			*x = ITEM_WINDOW_W;
		if (*y < OVERLAY_Y)
			*y = ITEM_WINDOW_H;
		*x -= OVERLAY_X;
		*y -= OVERLAY_Y;
	} else if (!item_window.window || SDL_GetMouseFocus() != item_window.window) {
		*x = ITEM_WINDOW_W;
		*y = ITEM_WINDOW_H;
	} else {
		SDL_GetMouseState(x, y);
	}

	if (*x >= 0 && *x < ITEM_WINDOW_W && *y >= 0 && *y < ITEM_WINDOW_H) {
		item_window.selected = *x / 32;
	}
}

void isaku_item_window_enable(void)
{
	if (!item_window.created)
		return;
	item_window.enabled = true;
}

void isaku_item_window_disable(void)
{
	item_window.enabled = false;
	if (item_window.opened) {
		if (item_window.use_overlay)
			gfx_overlay_disable(1);
		else
			SDL_HideWindow(item_window.window);
		item_window.opened = false;
	}
}

void isaku_item_window_get_mouse_state(bool *lmb_down, bool *rmb_down)
{
	if (!item_window.enabled) {
		*lmb_down = false;
		*rmb_down = false;
		return;
	}
	*lmb_down = item_window.lmb_down;
	*rmb_down = item_window.rmb_down;
	if (item_window.use_overlay) {
		if (input_down(INPUT_ACTIVATE))
			*lmb_down = true;
	}
}

void isaku_item_window_use_overlay(void)
{
	assert(!item_window.created);
	item_window.use_overlay = true;
}
