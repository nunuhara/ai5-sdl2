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
#include "nulib/queue.h"

#include "cursor.h"
#include "input.h"
#include "gfx.h"

struct input_event {
	TAILQ_ENTRY(input_event) event_list_entry;
	enum input_event_type type;
};

TAILQ_HEAD(event_list, input_event);
struct event_list event_list = TAILQ_HEAD_INITIALIZER(event_list);

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

static void push_event(enum input_event_type type)
{
	struct input_event *input = xcalloc(1, sizeof(struct input_event));
	input->type = type;
	TAILQ_INSERT_TAIL(&event_list, input, event_list_entry);
}

static void push_key_event(SDL_KeyboardEvent *ev)
{
	enum input_event_type type = input_event_from_keycode(ev->keysym.sym);
	if (type == INPUT_NONE)
		return;
	push_event(type);
}

static void push_mouse_event(SDL_MouseButtonEvent *ev)
{
	if (ev->button == SDL_BUTTON_LEFT) {
		push_event(INPUT_ACTIVATE);
	} else if (ev->button == SDL_BUTTON_RIGHT) {
		push_event(INPUT_CANCEL);
	}
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
		case SDL_KEYUP:
			push_key_event(&e.key);
			break;
		case SDL_MOUSEBUTTONUP:
			push_mouse_event(&e.button);
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

static enum input_event_type pop_event(void)
{
	assert(!TAILQ_EMPTY(&event_list));
	struct input_event *ev = TAILQ_FIRST(&event_list);
	enum input_event_type r = ev->type;
	TAILQ_REMOVE(&event_list, ev, event_list_entry);
	free(ev);
	return r;
}

enum input_event_type input_keywait(void)
{
	while (TAILQ_EMPTY(&event_list)) {
		handle_events();
		gfx_update();
		vm_delay(16);
	}
	return pop_event();
}

enum input_event_type input_poll(void)
{
	handle_events();
	if (TAILQ_EMPTY(&event_list))
		return INPUT_NONE;
	return pop_event();
}
