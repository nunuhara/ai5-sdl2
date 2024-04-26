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

#ifndef AI5_SDL2_GAME_H
#define AI5_SDL2_GAME_H

#include <stdint.h>

#define GAME_MAX_UTIL 256
#define GAME_MAX_SYS 32

struct param_list;
typedef union SDL_Event SDL_Event;

enum game_flag {
	// enables reflector animation (YU-NO specific)
	FLAG_REFLECTOR,
	// enables animation playback
	FLAG_ANIM_ENABLE,
	// return flag for menuexec
	FLAG_MENU_RETURN,
	// return flag
	FLAG_RETURN,
	// cleared/restored on procedure call
	FLAG_PROC_CLEAR,
	// message history?
	FLAG_LOG,
	// enables loading of palette in System.load_image
	FLAG_LOAD_PALETTE,
	// enables System.Voice subsystem
	FLAG_VOICE_ENABLE,
	// enables System.Audio subsystem
	FLAG_AUDIO_ENABLE,
	// if set, counts the length of text rather than displaying it
	FLAG_STRLEN,
	// if set, wait for keyup events
	FLAG_WAIT_KEYUP,
	// if set, skip keyup events in menus
	FLAG_SKIP_KEYUP,
	// if set, only palette is loaded in System.load_image
	FLAG_PALETTE_ONLY,
};
#define GAME_NR_FLAGS (FLAG_PALETTE_ONLY+1)
#define FLAG_ALWAYS_ON 0xffff

struct game {
	struct { uint16_t w, h; } surface_sizes[12];
	unsigned bpp;
	unsigned x_mult;
	bool use_effect_arc;
	bool persistent_volume;
	bool call_saves_procedures;
	bool proc_clears_flag;
	uint32_t var4_size;
	uint32_t mem16_size;
	void (*init)(void);
	void (*update)(void);
	void (*handle_event)(SDL_Event *e);
	void (*mem_init)(void);
	void (*mem_restore)(void);
	void (*custom_TXT)(const char *text);
	void (*util[GAME_MAX_UTIL])(struct param_list*);
	void (*sys[GAME_MAX_SYS])(struct param_list*);
	uint32_t flags[GAME_NR_FLAGS];
};

extern struct game game_ai_shimai;
extern struct game game_isaku;
extern struct game game_shangrlia;
extern struct game game_yuno;

extern struct game *game;

#endif
