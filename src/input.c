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
#include "input.h"
#include "gfx.h"
#include "vm.h"

static bool key_down[INPUT_NR_INPUTS] = {0};
unsigned input_events[INPUT_NR_INPUTS] = {0};

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
	case SDLK_RETURN: return INPUT_ACTIVATE;
	case SDLK_ESCAPE: return INPUT_CANCEL;
	case SDLK_UP:     return INPUT_UP;
	case SDLK_DOWN:   return INPUT_DOWN;
	case SDLK_LEFT:   return INPUT_LEFT;
	case SDLK_RIGHT:  return INPUT_RIGHT;
	default:          return INPUT_NONE;
	}
}

static void push_key_event(SDL_KeyboardEvent *ev, bool down)
{
	enum input_event_type type = input_event_from_keycode(ev->keysym.sym);
	if (type == INPUT_NONE)
		return;
	if (!down && key_down[type])
		input_events[type]++;
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

static void push_mouse_event(SDL_MouseButtonEvent *ev, bool down)
{
	enum input_event_type type = input_event_from_button(ev->button);
	if (type == INPUT_NONE)
		return;
	if (!down && key_down[type])
		input_events[type]++;
	key_down[type] = down;
}

void handle_events(void)
{
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_QUIT:
			sys_exit(0);
			break;
		case SDL_WINDOWEVENT:
			gfx_dirty();
			break;
		case SDL_KEYDOWN:
			push_key_event(&e.key, true);
			break;
		case SDL_KEYUP:
			push_key_event(&e.key, false);
			break;
		case SDL_MOUSEBUTTONDOWN:
			push_mouse_event(&e.button, true);
			break;
		case SDL_MOUSEBUTTONUP:
			push_mouse_event(&e.button, false);
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

static enum input_event_type pop_event(void)
{
	for (int i = 0; i < INPUT_NR_INPUTS; i++) {
		if (input_events[i]) {
			input_events[i]--;
			return i;
		}
	}
	return INPUT_NONE;
}

enum input_event_type input_keywait(void)
{
	enum input_event_type e;
	while ((e = pop_event()) == INPUT_NONE) {
		handle_events();
		gfx_update();
		vm_delay(16);
	}
	input_clear();
	return e;
}

enum input_event_type input_poll(void)
{
	handle_events();
	return pop_event();
}

void input_clear(void)
{
	for (int i = 0; i < INPUT_NR_INPUTS; i++) {
		input_events[i] = 0;
		key_down[i] = false;
	}
}

bool input_down(enum input_event_type type)
{
	assert(type >= 0 && type < INPUT_NR_INPUTS);
	handle_events();
	return key_down[type];
}
