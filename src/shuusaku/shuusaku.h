/* Copyright (C) 2025 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#ifndef SHUUSAKU_H
#define SHUUSAKU_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL.h>

#define MASK_COLOR 10

#define DAY_SAT 6
#define DAY_SUN 0
#define DAY_MON 1

struct menu_entry {
	uint32_t body_addr;
	unsigned index;
};

enum sched_flag {
	SCHED_FLAG_UNKNOWN = 0,
	SCHED_FLAG_EMPTY = 1,
	SCHED_FLAG_OCCUPIED = 2,
	SCHED_FLAG_EVENT = 4,
	SCHED_FLAG_PINK = 8
};

enum sched_location {
	LOC_NAGISA = 0,
	LOC_KAORI = 1,
	LOC_SHIHO = 2,
	LOC_CHIAKI = 3,
	LOC_ASAMI = 4,
	LOC_MOEKO = 5,
	LOC_ERI = 6,
	LOC_AYAKA = 7,
	LOC_TOILET = 8,
	LOC_CHANGING = 9,
	LOC_KANRININ = 10,
#define NR_LOC 11
};

enum sched_character {
	CHAR_NAGISA = 0,
	CHAR_KAORI = 1,
	CHAR_SHIHO = 2,
	CHAR_CHIAKI = 3,
	CHAR_ASAMI = 4,
	CHAR_MOEKO = 5,
	CHAR_ERI = 6,
	CHAR_AYAKA = 7,
#define NR_CHAR 8
};

enum sched_camera {
	CAM_NONE = 0,
	CAM_VIDEO = 1,
	CAM_DIGI = 2,
};

#define NR_INTERVALS 144

struct sched_away_event {
	uint8_t t;
	uint8_t character;
	uint16_t flag_no;
};

struct sched_cam_event_entry {
	unsigned flag_no;
	const char *name;
	const char *zoom_name;
};

struct sched_cam_event {
	uint8_t t;
	struct sched_cam_event_entry entries[3];
};

extern bool shuusaku_running_cam_event;

// schedule_data.c
void shuusaku_init_away_events(struct sched_away_event *away_events[NR_LOC]);
struct sched_cam_event *shuusaku_get_cam_event(enum sched_location loc, unsigned t);

// schedule.c
void schedule_window_init(void);
void shuusaku_schedule_window_toggle(void);
bool shuusaku_schedule_window_event(SDL_Event *e);
void shuusaku_schedule_tick(void);
void shuusaku_schedule_update(void);
void shuusaku_schedule_set_flag(unsigned location, unsigned day, unsigned t, uint8_t flag);
void shuusaku_schedule_set_plan_time(unsigned day, unsigned t);
void shuusaku_schedule_clear_plan(void);
int shuusaku_absolute_time(unsigned day, unsigned t);

// status.c
void shuusaku_status_init(void);
void shuusaku_status_window_toggle(void);
bool shuusaku_status_window_event(SDL_Event *e);
void shuusaku_status_update(void);

// shuusaku.c
void shuusaku_draw_text(const char *text);
void shuusaku_update_palette(uint8_t *pal);
void shuusaku_crossfade(uint8_t *pal, bool allow_16_32);
void shuusaku_crossfade_to(uint8_t r, uint8_t g, uint8_t b);
void shuusaku_play_movie(const char *name);
void shuusaku_after_movie_crossfade(void);
void shuusaku_zoom(int x, int y, int w, int h, unsigned src_i);
void shuusaku_cam_event_zoom(unsigned cg_x, unsigned cg_y, unsigned cg_w, unsigned cg_h);

// name.c
void shuusaku_name_input_screen(uint8_t *myouji, uint8_t *namae);

// menu.c
unsigned shuusaku_menuexec(struct menu_entry *entries, unsigned nr_entries, unsigned mode);

// view.c
unsigned shuusaku_scene_viewer_char_select(void);
unsigned shuusaku_scene_viewer_scene_select(enum sched_character ch, bool need_bg);

#endif // SHUUSAKU_H
