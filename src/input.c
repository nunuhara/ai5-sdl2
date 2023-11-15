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

#include "input.h"
#include "gfx.h"

struct input_event {
	TAILQ_ENTRY(input_event) event_list_entry;
	// TODO: store type of event, etc.
};

TAILQ_HEAD(event_list, input_event);
struct event_list event_list = TAILQ_HEAD_INITIALIZER(event_list);

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
		case SDL_KEYUP: {
			struct input_event *ie = xcalloc(1, sizeof(struct input_event));
			TAILQ_INSERT_TAIL(&event_list, ie, event_list_entry);
			break;
		}
		case SDL_MOUSEBUTTONUP: {
			struct input_event *ie = xcalloc(1, sizeof(struct input_event));
			TAILQ_INSERT_TAIL(&event_list, ie, event_list_entry);
			break;
		}
		default:
			break;
		}
	}
}

void input_keywait(void)
{
	while (TAILQ_EMPTY(&event_list)) {
		handle_events();
		gfx_update();
	}
	struct input_event *ev = TAILQ_FIRST(&event_list);
	TAILQ_REMOVE(&event_list, ev, event_list_entry);
	free(ev);
}
