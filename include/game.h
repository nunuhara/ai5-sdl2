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

#include <stdbool.h>
#include <stdint.h>

#include "ai5/game.h"

#define GAME_MAX_UTIL 601
#define GAME_MAX_SYS 256

struct param_list;
struct anim_draw_call;
typedef union SDL_Event SDL_Event;

// virtual flags -- mapped to real flag values in game.flags
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
	// enables backlog
	FLAG_LOG_ENABLE,
	// controls whether text is written to backlog
	FLAG_LOG_TEXT,
	// write to backlog
	FLAG_LOG,
	// controls whether system calls are written to backlog
	FLAG_LOG_SYS,
	// enables loading of palette in System.load_image
	FLAG_LOAD_PALETTE,
	// enables System.Voice subsystem
	FLAG_VOICE_ENABLE,
	// enables System.Audio subsystem
	FLAG_AUDIO_ENABLE,
	// if set, count the length of text rather than displaying it
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

enum flags_type {
	FLAGS_8BIT,
	FLAGS_4BIT_WRAPPED,
	FLAGS_4BIT_CAPPED,
};

struct game {
	enum ai5_game_id id;
	struct { uint16_t w, h; } surface_sizes[13];
	// bits per pixel
	//   8  = 8-bit indexed
	//   16 = BGR555
	//   24 = BGR888
	unsigned bpp;
	// size of the 16-bit address space
	uint32_t mem16_size;
	// called immediately before running the initial mes file
	void (*init)(void);
	// called in vm_peek
	void (*update)(void);
	// called for all input events, before built-in input handling
	bool (*handle_event)(SDL_Event *e);
	// called in early init
	void (*mem_init)(void);
	// called whenever a full save file is loaded (savedata_resume_load)
	void (*mem_restore)(void);
	// called whenever text is encountered in the mes file
	void (*draw_text_zen)(const char *text);
	void (*draw_text_han)(const char *text);
	// called after animation draw ops
	void (*after_anim_draw)(struct anim_draw_call *call);
	// VM opcode tables
	void (*stmt_op[256])(void);
	void (*expr_op[256])(void);
	// system/util call tables
	void (*util[GAME_MAX_UTIL])(struct param_list*);
	void (*sys[GAME_MAX_SYS])(struct param_list*);
	// mapping of virtual flags to actual flag bits
	uint32_t flags[GAME_NR_FLAGS];
};

extern struct game game_ai_shimai;
extern struct game game_doukyuusei;
extern struct game game_isaku;
extern struct game game_shangrlia;
extern struct game game_yuno;

extern struct game *game;

#endif
