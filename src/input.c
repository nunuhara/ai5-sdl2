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

#include "ai5.h"
#include "cursor.h"
#include "debug.h"
#include "input.h"
#include "gfx_private.h"
#include "vm.h"

static enum input_event_type controller_mappings[SDL_CONTROLLER_BUTTON_MAX] = {
	[SDL_CONTROLLER_BUTTON_A] = INPUT_ACTIVATE,
	[SDL_CONTROLLER_BUTTON_B] = INPUT_CANCEL,
	[SDL_CONTROLLER_BUTTON_X] = INPUT_CTRL,
	[SDL_CONTROLLER_BUTTON_Y] = INPUT_SPACE,
	[SDL_CONTROLLER_BUTTON_BACK] = INPUT_NONE,
	[SDL_CONTROLLER_BUTTON_GUIDE] = INPUT_NONE,
	[SDL_CONTROLLER_BUTTON_START] = INPUT_NONE,
	[SDL_CONTROLLER_BUTTON_LEFTSTICK] = INPUT_NONE,
	[SDL_CONTROLLER_BUTTON_RIGHTSTICK] = INPUT_NONE,
	[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = INPUT_PAGE_UP,
	[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = INPUT_PAGE_DOWN,
	[SDL_CONTROLLER_BUTTON_DPAD_UP] = INPUT_UP,
	[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = INPUT_DOWN,
	[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = INPUT_LEFT,
	[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = INPUT_RIGHT,
	[SDL_CONTROLLER_BUTTON_MISC1] = INPUT_NONE,
	[SDL_CONTROLLER_BUTTON_PADDLE1] = INPUT_NONE,
	[SDL_CONTROLLER_BUTTON_PADDLE2] = INPUT_NONE,
	[SDL_CONTROLLER_BUTTON_PADDLE3] = INPUT_NONE,
	[SDL_CONTROLLER_BUTTON_PADDLE4] = INPUT_NONE,
	[SDL_CONTROLLER_BUTTON_TOUCHPAD] = INPUT_NONE,
};
static bool controller_mapped_explicitly[SDL_CONTROLLER_BUTTON_MAX] = {0};
static enum input_event_type controller_ltrigger_mapping = INPUT_NONE;
static enum input_event_type controller_rtrigger_mapping = INPUT_NONE;
static bool controller_ltrigger_mapped_explicitly = false;
static bool controller_rtrigger_mapped_explicitly = false;

static uint32_t key_down_timestamp[INPUT_NR_INPUTS] = {0};
static bool key_down[INPUT_NR_INPUTS] = {0};

uint32_t cursor_swap_event = 0;

static SDL_GameController *controller = NULL;

static SDL_GameController *find_controller(void)
{
	for (int i = 0; i < SDL_NumJoysticks(); i++) {
		if (SDL_IsGameController(i)) {
			NOTICE("Opening controller: \"%s\"",
					SDL_GameControllerNameForIndex(i));
			return SDL_GameControllerOpen(i);
		}
	}
	return NULL;
}

enum input_event_type parse_input_event_type(const char *str)
{
	if (!strcasecmp(str, "NONE"))
		return INPUT_NONE;
	if (!strcasecmp(str, "ACTIVATE") || !strcasecmp(str, "ENTER") || !strcasecmp(str, "LMOUSE"))
		return INPUT_ACTIVATE;
	if (!strcasecmp(str, "CANCEL") || !strcasecmp(str, "ESCAPE") || !strcasecmp(str, "RMOUSE"))
		return INPUT_CANCEL;
	if (!strcasecmp(str, "UP"))
		return INPUT_UP;
	if (!strcasecmp(str, "DOWN"))
		return INPUT_DOWN;
	if (!strcasecmp(str, "LEFT"))
		return INPUT_LEFT;
	if (!strcasecmp(str, "RIGHT"))
		return INPUT_RIGHT;
	if (!strcasecmp(str, "SHIFT"))
		return INPUT_SHIFT;
	if (!strcasecmp(str, "CTRL"))
		return INPUT_CTRL;
	if (!strcasecmp(str, "SPACE"))
		return INPUT_SPACE;
	if (!strcasecmp(str, "BACKSPACE"))
		return INPUT_BACKSPACE;
	if (!strcasecmp(str, "PAGEUP"))
		return INPUT_PAGE_UP;
	if (!strcasecmp(str, "PAGEDOWN"))
		return INPUT_PAGE_DOWN;
	if (!strcasecmp(str, "L"))
		return INPUT_L;
	if (!strcasecmp(str, "S"))
		return INPUT_S;
	if (!strcasecmp(str, "TAB"))
		return INPUT_TAB;
	return INPUT_NONE;
}

void map_controller_button(int btn, enum input_event_type e)
{
	if (btn == INPUT_LTRIGGER_BUTTON) {
		controller_ltrigger_mapping = e;
		controller_ltrigger_mapped_explicitly = true;
	} else if (btn == INPUT_RTRIGGER_BUTTON) {
		controller_rtrigger_mapping = e;
		controller_rtrigger_mapped_explicitly = true;
	} else {
		assert(btn >= 0 && btn < SDL_CONTROLLER_BUTTON_MAX);
		controller_mappings[btn] = e;
		controller_mapped_explicitly[btn] = true;
	}
}

static bool controller_button_mapped_explicitly(SDL_GameControllerButton btn)
{
	if (btn == INPUT_LTRIGGER_BUTTON)
		return controller_ltrigger_mapped_explicitly;
	if (btn == INPUT_RTRIGGER_BUTTON)
		return controller_rtrigger_mapped_explicitly;
	assert(btn >= 0 && btn < SDL_CONTROLLER_BUTTON_MAX);
	return controller_mapped_explicitly[btn];
}

void map_controller_button_implicitly(int btn, enum input_event_type e)
{
	if (controller_button_mapped_explicitly(btn))
		return;
	if (btn == INPUT_LTRIGGER_BUTTON) {
		controller_ltrigger_mapping = e;
	} else if (btn == INPUT_RTRIGGER_BUTTON) {
		controller_rtrigger_mapping = e;
	} else {
		assert(btn >= 0 && btn < SDL_CONTROLLER_BUTTON_MAX);
		controller_mappings[btn] = e;
	}
}

void input_init(void)
{
	cursor_swap_event = SDL_RegisterEvents(1);
	if (cursor_swap_event == (uint32_t)-1)
		WARNING("Failed to register custom event type");
}

static enum input_event_type input_event_from_keycode(SDL_Keycode k)
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
	case SDLK_l:         return INPUT_L;
	case SDLK_s:         return INPUT_S;
	case SDLK_TAB:       return INPUT_TAB;
	default:             return INPUT_NONE;
	}
}

static enum input_event_type input_event_from_controller_button(SDL_GameControllerButton b)
{
	if (b < 0 || b >= SDL_CONTROLLER_BUTTON_MAX)
		return INPUT_NONE;
	return controller_mappings[b];
}

void input_key_event(enum input_event_type type, bool down)
{
	if (type == INPUT_NONE)
		return;
	assert(type >= 0 && type < INPUT_NR_INPUTS);
	key_down[type] = down;
	if (down)
		key_down_timestamp[type] = SDL_GetTicks();
}

static void key_event(SDL_KeyboardEvent *ev, bool down)
{
	enum input_event_type type = input_event_from_keycode(ev->keysym.sym);
	input_key_event(type, down);
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
	if (down)
		key_down_timestamp[type] = SDL_GetTicks();
}

static void controller_button_event(SDL_ControllerButtonEvent *ev)
{
	bool down = ev->type == SDL_CONTROLLERBUTTONDOWN;
	enum input_event_type type = input_event_from_controller_button(ev->button);
	if (type == INPUT_NONE)
		return;
	key_down[type] = down;
	if (down)
		key_down_timestamp[type] = SDL_GetTicks();
}

static vm_timer_t controller_poll_timer = 0;

static float controller_get_stick_axis(SDL_GameControllerAxis axis)
{
	int i = SDL_GameControllerGetAxis(controller, axis);
	float f = clamp(-1.0f, 1.0f, i / 32700.0f);
	if (f > -config.controller.dead_zone && f < config.controller.dead_zone)
		f = 0.0f;
	return f;
}

static bool controller_get_trigger_down(SDL_GameControllerAxis axis)
{
	int i = SDL_GameControllerGetAxis(controller, axis);
	return i > 3267 / 2;
}

static void controller_update_analog(void)
{
	if (!config.controller.enabled || !controller)
		return;
	if (!vm_timer_tick_async(&controller_poll_timer, 33))
		return;
	if (config.controller.left_stick != CONFIG_STICK_CURSOR
			&& config.controller.right_stick != CONFIG_STICK_CURSOR)
		return;

	// get stick position
	float stick_x = 0.0f;
	float stick_y = 0.0f;
	if (config.controller.left_stick == CONFIG_STICK_CURSOR) {
		stick_x = controller_get_stick_axis(SDL_CONTROLLER_AXIS_LEFTX);
		stick_y = controller_get_stick_axis(SDL_CONTROLLER_AXIS_LEFTY);
	}
	if (config.controller.right_stick == CONFIG_STICK_CURSOR) {
		float this_x = controller_get_stick_axis(SDL_CONTROLLER_AXIS_RIGHTX);
		float this_y = controller_get_stick_axis(SDL_CONTROLLER_AXIS_RIGHTY);
		stick_x = clamp(-1.0f, 1.0f, stick_x + this_x);
		stick_y = clamp(-1.0f, 1.0f, stick_y + this_y);
	}

	// get current cursor position
	int mouse_ix, mouse_iy;
	float mouse_fx, mouse_fy;
	SDL_GetMouseState(&mouse_ix, &mouse_iy);
	SDL_RenderWindowToLogical(gfx.renderer, mouse_ix, mouse_iy, &mouse_fx, &mouse_fy);
	mouse_fx = clamp(0.0f, (float)(gfx_view.w-1), mouse_fx);
	mouse_fy = clamp(0.0f, (float)(gfx_view.h-1), mouse_fy);

	// get next cursor position
	float move_fx = clamp(0.0f, (float)(gfx_view.w-1),
			mouse_fx + stick_x * config.controller.cursor_speed);
	float move_fy = clamp(0.0f, (float)(gfx_view.h-1),
			mouse_fy + stick_y * config.controller.cursor_speed);
	int move_ix, move_iy;
	SDL_RenderLogicalToWindow(gfx.renderer, move_fx, move_fy, &move_ix, &move_iy);

	if (mouse_ix != move_ix || mouse_iy != move_iy)
		SDL_WarpMouseInWindow(gfx.window, move_ix, move_iy);
}

void handle_window_event(struct SDL_WindowEvent *e)
{
	if (e->windowID != gfx.window_id)
		return;
	switch (e->event) {
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
}

static bool active_controller(int which)
{
	return controller && which == SDL_JoystickInstanceID(
			SDL_GameControllerGetJoystick(controller));
}

void handle_events(void)
{
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		if (game->handle_event && game->handle_event(&e))
			continue;
		switch (e.type) {
		case SDL_WINDOWEVENT:
			handle_window_event(&e.window);
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
		case SDL_MOUSEWHEEL:
			if (e.wheel.windowID != gfx.window_id)
				break;
			if (e.wheel.y > 0)
				cursor_set_direction(CURSOR_DIR_UP);
			else if (e.wheel.y < 0)
				cursor_set_direction(CURSOR_DIR_DOWN);
			break;
		case SDL_CONTROLLERDEVICEADDED:
			if (!controller)
				controller = SDL_GameControllerOpen(e.cdevice.which);
			break;
		case SDL_CONTROLLERDEVICEREMOVED:
			if (active_controller(e.cdevice.which)) {
				SDL_GameControllerClose(controller);
				controller = find_controller();
			}
			break;
		case SDL_CONTROLLERBUTTONDOWN:
		case SDL_CONTROLLERBUTTONUP:
			if (active_controller(e.cdevice.which))
				controller_button_event(&e.cbutton);
			break;
		default:
			if (e.type == cursor_swap_event)
				cursor_swap();
			break;
		}
	}
	controller_update_analog();
}

void vm_delay(int ms)
{
	SDL_Delay(ms);
}

uint32_t vm_get_ticks(void)
{
	return SDL_GetTicks();
}

static bool event_is_dir(enum input_event_type type)
{
	switch (type) {
	case INPUT_UP:
	case INPUT_DOWN:
	case INPUT_LEFT:
	case INPUT_RIGHT:
		return true;
	default:
		return false;
	}
}

static bool have_analog_dpad(void)
{
	return config.controller.enabled &&
		(config.controller.left_stick == CONFIG_STICK_DPAD
		 || config.controller.right_stick == CONFIG_STICK_DPAD);
}

#ifndef M_PI
#define M_PI (3.14159265358979323846)
#endif

/*
 * An analog D-pad direction is considered to be down if:
 *   * the magnitude of its vector is > 0.5
 *   * the angle of its vector is within 60 degrees of the cardinal direction
 * This creates a 30 degree zone along the diagonals where two directions can
 * be down simultaneously.
 */
static bool analog_dpad_down(float x, float y, enum input_event_type type)
{
	if (sqrtf(x*x + y*y) < 0.5f)
		return false;
	float ang = atan2f(-y, x);
	if (ang < 0)
		ang += 2.f*M_PI;
	switch (type) {
	case INPUT_UP:
		return ang >= M_PI/6.f && ang <= (5.f*M_PI)/6.f;
	case INPUT_DOWN:
		return ang >= (7.f*M_PI)/6.f && ang <= (11.f*M_PI)/6.f;
	case INPUT_RIGHT:
		return ang >= (5.f*M_PI)/3.f || ang <= M_PI/3.f;
	case INPUT_LEFT:
		return ang >= (2.f*M_PI)/3.f && ang <= (4.f*M_PI)/3.f;
	default:
		return false;
	}
}

bool input_down(enum input_event_type type)
{
	assert(type >= 0 && type < INPUT_NR_INPUTS);
	handle_events();
	if (key_down[type] || SDL_GetTicks() - key_down_timestamp[type] < 30)
		return true;
	if (have_analog_dpad() && event_is_dir(type)) {
		if (config.controller.left_stick == CONFIG_STICK_DPAD) {
			float x = controller_get_stick_axis(SDL_CONTROLLER_AXIS_LEFTX);
			float y = controller_get_stick_axis(SDL_CONTROLLER_AXIS_LEFTY);
			if (analog_dpad_down(x, y, type))
				return true;
		}
		if (config.controller.right_stick == CONFIG_STICK_DPAD) {
			float x = controller_get_stick_axis(SDL_CONTROLLER_AXIS_RIGHTX);
			float y = controller_get_stick_axis(SDL_CONTROLLER_AXIS_RIGHTY);
			if (analog_dpad_down(x, y, type))
				return true;
		}
	}
	if (config.controller.enabled) {
		if (controller_ltrigger_mapping == type) {
			return controller_get_trigger_down(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
		}
		if (controller_rtrigger_mapping == type) {
			return controller_get_trigger_down(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
		}
	}
	return false;
}

void _input_wait_until_up(enum input_event_type type)
{
	while (input_down(type)) {
		vm_peek();
		vm_delay(16);
	}
}
void input_wait_until_up(enum input_event_type type)
{
	if (!vm_flag_is_on(FLAG_WAIT_KEYUP))
		return;
	_input_wait_until_up(type);
}
