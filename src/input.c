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

#include "cursor.h"
#include "debug.h"
#include "input.h"
#include "gfx_private.h"
#include "vm.h"

static bool key_down[INPUT_NR_INPUTS] = {0};

uint32_t cursor_swap_event = 0;

void input_init(void)
{
	cursor_swap_event = SDL_RegisterEvents(1);
	if (cursor_swap_event == (uint32_t)-1)
		WARNING("Failed to register custom event type");
}

enum input_event_type input_event_from_keycode(SDL_Keycode k)
{
	switch (k) {
	case SDLK_KP_ENTER:
	case SDLK_RETURN:    return INPUT_ACTIVATE;
	case SDLK_ESCAPE:    return INPUT_CANCEL;
	case SDLK_UP:        return INPUT_UP;
	case SDLK_DOWN:      return INPUT_DOWN;
	case SDLK_LEFT:      return INPUT_LEFT;
	case SDLK_RIGHT:     return INPUT_RIGHT;
	case SDLK_LSHIFT:
	case SDLK_RSHIFT:    return INPUT_SHIFT;
	case SDLK_RCTRL:
	case SDLK_LCTRL:     return INPUT_CTRL;
	case SDLK_SPACE:     return INPUT_SPACE;
	case SDLK_BACKSPACE: return INPUT_BACKSPACE;
	case SDLK_PAGEUP:    return INPUT_PAGE_UP;
	case SDLK_PAGEDOWN:  return INPUT_PAGE_DOWN;
	default:             return INPUT_NONE;
	}
}

static void key_event(SDL_KeyboardEvent *ev, bool down)
{
	enum input_event_type type = input_event_from_keycode(ev->keysym.sym);
	if (type == INPUT_NONE)
		return;
	key_down[type] = down;
}

static enum input_event_type input_event_from_button(int button)
{
	if (button == SDL_BUTTON_LEFT)
		return INPUT_ACTIVATE;
	if (button == SDL_BUTTON_RIGHT)
		return INPUT_CANCEL;
	return INPUT_NONE;
}

static void mouse_event(SDL_MouseButtonEvent *ev, bool down)
{
	enum input_event_type type = input_event_from_button(ev->button);
	if (type == INPUT_NONE)
		return;
	key_down[type] = down;
}

void handle_events(void)
{
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		if (game->handle_event)
			game->handle_event(&e);
		switch (e.type) {
		case SDL_WINDOWEVENT:
			if (e.window.windowID != gfx.window_id)
				break;
			switch (e.window.event) {
			case SDL_WINDOWEVENT_SHOWN:
			case SDL_WINDOWEVENT_EXPOSED:
			case SDL_WINDOWEVENT_RESIZED:
			case SDL_WINDOWEVENT_SIZE_CHANGED:
			case SDL_WINDOWEVENT_MAXIMIZED:
			case SDL_WINDOWEVENT_RESTORED:
				gfx_screen_dirty();
				break;
			case SDL_WINDOWEVENT_CLOSE:
				if (gfx_confirm_quit())
					sys_exit(0);
				break;
			}
			break;
		case SDL_KEYDOWN:
			if (e.key.windowID != gfx.window_id)
				break;
			key_event(&e.key, true);
			switch (e.key.keysym.sym) {
			case SDLK_F10:    gfx_screenshot(); break;
			case SDLK_F11:    gfx_window_toggle_fullscreen(); break;
			case SDLK_F12:    if (debug_on_F12) dbg_repl(); break;
			case SDLK_MINUS:  gfx_window_decrease_integer_size(); break;
			case SDLK_EQUALS: gfx_window_increase_integer_size(); break;
			}
			break;
		case SDL_KEYUP:
			if (e.key.windowID != gfx.window_id)
				break;
			key_event(&e.key, false);
			break;
		case SDL_MOUSEBUTTONDOWN:
			if (e.button.windowID != gfx.window_id)
				break;
			mouse_event(&e.button, true);
			break;
		case SDL_MOUSEBUTTONUP:
			if (e.button.windowID != gfx.window_id)
				break;
			mouse_event(&e.button, false);
			break;
		default:
			if (e.type == cursor_swap_event)
				cursor_swap();
			break;
		}
	}
}

void vm_delay(int ms)
{
	SDL_Delay(ms);
}

uint32_t vm_get_ticks(void)
{
	return SDL_GetTicks();
}

bool input_down(enum input_event_type type)
{
	assert(type >= 0 && type < INPUT_NR_INPUTS);
	handle_events();
	return key_down[type];
}

void input_wait_until_up(enum input_event_type type)
{
	if (!vm_flag_is_on(FLAG_WAIT_KEYUP))
		return;
	while (input_down(type)) {
		vm_peek();
		vm_delay(16);
	}
}
