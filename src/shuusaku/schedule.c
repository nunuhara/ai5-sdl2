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

#include "nulib.h"
#include "ai5/cg.h"

#include "anim.h"
#include "asset.h"
#include "audio.h"
#include "cursor.h"
#include "input.h"
#include "game.h"
#include "gfx.h"
#include "gfx_private.h"
#include "vm.h"

#include "shuusaku.h"

#define SCHEDULE_WINDOW_W 800
#define SCHEDULE_WINDOW_H 376

#define COL_W 80
#define ROW_H 32

static struct {
	bool open;
	uint32_t window_id;
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	SDL_Surface *parts;
	SDL_Surface *display;
	SDL_Surface *saved;
	unsigned start_t;
	int current_t;
	int plan_t;
	vm_timer_t flash_timer;
	bool flash_on;
} schedule = {0};

static struct sched_away_event *away_events[NR_LOC] = {0};

/*
 * Get the position of the cell background on the parts CG for a given flags value.
 */
static SDL_Point get_bg_pos(unsigned flags)
{
	if (flags == SCHED_FLAG_UNKNOWN)
		return (SDL_Point) { 160, ROW_H * 7 };
	if (flags & SCHED_FLAG_PINK)
		return (SDL_Point) { 160, ROW_H * 8 };
	if (flags & SCHED_FLAG_EVENT) {
		if (flags & SCHED_FLAG_EMPTY)
			return (SDL_Point) { 160, ROW_H * 5 };
		return (SDL_Point) { 160, ROW_H * 3 };
	}
	if (flags & SCHED_FLAG_OCCUPIED) {
		if (flags & SCHED_FLAG_EMPTY)
			return (SDL_Point) { 160, ROW_H * 2 };
		return (SDL_Point) { 160, 0 };
	}
	if (flags & SCHED_FLAG_EMPTY)
		return (SDL_Point) { 160, ROW_H * 1 };
	WARNING("Invalid flags: %u", flags);
	return (SDL_Point) { 400, 312 };
}

/*
 * Get the position of the header on the parts CG for a given day/time.
 */
static SDL_Point get_header_pos(unsigned abs_t)
{
	unsigned hour = abs_t / 4;
	unsigned hour_i = abs_t % 4;
	if (hour_i == 1) {
		// 15
		return (SDL_Point) { 240, 0 };
	} else if (hour_i == 2) {
		// 30
		return (SDL_Point) { 320, 0 };
	} else if (hour_i == 3) {
		// 45
		return (SDL_Point) { 400, 0 };
	} else if (hour < 7) {
		// saturday
		return (SDL_Point) { 240, (hour + 1) * 24 };
	} else if (hour < 19) {
		// sunday before noon
		hour -= 7;
		return (SDL_Point) { 320, (hour + 1) * 24 };
	} else if (hour < 31) {
		// sunday after noon
		hour -= 19;
		return (SDL_Point) { 400, (hour + 1) * 24 };
	} else if (hour < 36) {
		// monday
		hour -= 31;
		return (SDL_Point) { 240, 192 + hour * 24 };
	}
	WARNING("Invalid absolute time: %u", abs_t);
	return (SDL_Point) { 160, 288 };
}

/*
 * Get the position of a character head on the parts CG for a given character ID.
 */
static SDL_Point get_head_pos(unsigned ch)
{
	return (SDL_Point) { 160 + ch * 32, 344 };
}

/*
 * Convert day/time values to an absolute interval number starting at 17:00 Saturday.
 */
int shuusaku_absolute_time(unsigned day, unsigned t)
{
	unsigned hour = (t / 100);
	if (hour > 23)
		return -1;
	if (day == DAY_SAT) {
		if (hour < 17)
			return -1;
		hour -= 17;
	} else if (day == DAY_SUN) {
		hour += 7;
	} else if (day == DAY_MON) {
		if (hour > 4)
			return -1;
		hour += 31;
	} else {
		return -1;
	}

	return hour * 4 + (t % 100) / 15;
}

/*
 * Get the flag number for a cell on the schedule, given by day/time and location.
 * (This is the flag which contains the `enum sched_flag` value.)
 */
static unsigned schedule_flag_no(unsigned abs_t, unsigned location)
{
	if (abs_t < 0)
		VM_ERROR("Invalid absolute time: %u", abs_t);

	// XXX: Akaya flags are out of order
	if (location == LOC_AYAKA)
		return 3000 + abs_t;
	if (location < LOC_TOILET)
		location++;
	return 3000 + location * 150 + abs_t;
}

static void get_camera_info(unsigned location, unsigned *cam_type, unsigned *cam_placed,
		unsigned *cam_elapsed)
{
	if (location == LOC_AYAKA) {
		*cam_type = 0;
		return;
	}
	if (location > LOC_AYAKA)
		location--;

	unsigned t = mem_get_var4_packed(120 + location);
	*cam_type = t;
	if (t == CAM_NONE)
		return;
	if (t > 2) {
		*cam_type = CAM_NONE;
		return;
	}

	*cam_placed = shuusaku_absolute_time(mem_get_var16(120 + location * 3),
			mem_get_var16(121 + location * 3));
	*cam_elapsed = mem_get_var16(213 + location * 2 + ((t == CAM_DIGI) ? 1 : 0));
}

static void get_pink_info(unsigned location, bool *pink, unsigned *start_t)
{
	unsigned state_flag = 120 + location;
	unsigned start_var = 162 + location * 2;
	if (location == LOC_ERI || location > LOC_AYAKA) {
		*pink = false;
		return;
	}
	if (location == LOC_AYAKA) {
		state_flag = 129;
		start_var = 160;
	}

	if (mem_get_var4_packed(state_flag) != 9) {
		*pink = false;
		return;
	}

	*pink = true;
	*start_t = shuusaku_absolute_time(mem_get_var16(start_var), mem_get_var16(start_var + 1));
}

/*
 * Set a flag at a given location/day/time (util 3).
 */
void shuusaku_schedule_set_flag(unsigned location, unsigned day, unsigned t, uint8_t flag)
{
	unsigned abs_t = shuusaku_absolute_time(day, t);
	if (flag == SCHED_FLAG_EVENT)
		flag |= SCHED_FLAG_OCCUPIED;

	// set flag
	// XXX: location 0 is Ayaka here (otherwise in normal schedule order)
	unsigned flag_no = 3000 + location * 150 + abs_t;
	mem_set_var4_packed(flag_no, mem_get_var4_packed(flag_no) | flag);
}

/*
 * Wrapper to copy from the parts CG to the display surface.
 */
static void blit_parts(int src_x, int src_y, int w, int h, int dst_x, int dst_y)
{
	SDL_Rect src_r = { src_x, src_y, w, h };
	SDL_Rect dst_r = { dst_x, dst_y, w, h };
	SDL_CALL(SDL_BlitSurface, schedule.parts, &src_r, schedule.display, &dst_r);
}

void schedule_window_draw(void)
{
	if (!schedule.open)
		return;
	unsigned day = mem_get_sysvar16(60);
	unsigned rel_t = mem_get_sysvar16(61);
	int abs_t = shuusaku_absolute_time(day, rel_t);
	if (abs_t < 0) {
		WARNING("Invalid day/time: %u/%u", day, rel_t);
		abs_t = 0;
	}

	for (int i = 0; i < 8; i++) {
		SDL_Point header_pos = get_header_pos(schedule.start_t + i);
		blit_parts(header_pos.x, header_pos.y, 80, 24, 160 + i*COL_W, 0);
	}

	for (int loc = 0; loc < NR_LOC; loc++) {
		// draw cell background
		unsigned flag_no = schedule_flag_no(schedule.start_t, loc);
		for (int i = 0; i < 8; i++) {
			uint8_t flags = mem_get_var4_packed(flag_no + i);
			if (flags < 8) {
				SDL_Point pos = get_bg_pos(flags);
				blit_parts(pos.x, pos.y, 80, 32, 160 + i*COL_W, 24 + loc*ROW_H);
			}
		}

		// draw faces
		for (int i = 0; i < 8; i++) {
			if (!away_events[loc])
				continue;
			struct sched_away_event *ev = &away_events[loc][(schedule.start_t+i)*4];
			unsigned dst_x = 160 + i*COL_W + 48;
			for (int j = 0; j < 4; j++, ev++) {
				if (!ev->flag_no)
					break;
				if (mem_get_var4_packed(ev->flag_no) > 1) {
					SDL_Point pos = get_head_pos(ev->character);
					blit_parts(pos.x, pos.y, 32, 32, dst_x, 24 + loc*ROW_H);
					dst_x -= 16;
				}
			}
		}

		// draw cameras
		unsigned cam_type, cam_placed, cam_elapsed;
		get_camera_info(loc, &cam_type, &cam_placed, &cam_elapsed);
		for (int i = 0; i < 8; i++) {
			unsigned cell_t = schedule.start_t + i;
			if (cam_type != CAM_NONE && cam_placed <= cell_t
					&& cam_placed + cam_elapsed >= cell_t) {
				blit_parts(160 + (cam_type-1) * 80, 312, 80, 32,
						160 + i*COL_W, 24 + loc*ROW_H);
				unsigned cam_end_t = cam_placed + (cam_type == CAM_DIGI ? 4 : 8);
				if (cell_t >= cam_end_t) {
					blit_parts(320, 312, 80, 32, 160 + i*COL_W, 24 + loc*ROW_H);
				}
			}
		}

		bool pink;
		unsigned pink_start_t;
		get_pink_info(loc, &pink, &pink_start_t);
		for (int i = 0; i < 8; i++) {
			unsigned cell_t = schedule.start_t + i;
			if (pink && cell_t >= pink_start_t) {
				blit_parts(160, 256, 80, 32, 160 + i*COL_W, 24 + loc*ROW_H);
			}
		}
	}

	// save area behind yellow flashing box (current time)
	int i = schedule.current_t - (int)schedule.start_t;
	if (i >= 0 && i < 8) {
		SDL_Rect src_r = { 160 + i * 80, 0, 80, SCHEDULE_WINDOW_H };
		SDL_Rect dst_r = { 0, 0, 80, SCHEDULE_WINDOW_H };
		SDL_CALL(SDL_BlitSurface, schedule.display, &src_r, schedule.saved, &dst_r);
	}

	// save area behind red flashing box (plan time)
	i = schedule.plan_t - (int)schedule.start_t;
	if (i >= 0 && i < 8) {
		SDL_Rect src_r = { 160 + i * 80, 0, 80, SCHEDULE_WINDOW_H };
		SDL_Rect dst_r = { 80, 0, 80, SCHEDULE_WINDOW_H };
		SDL_CALL(SDL_BlitSurface, schedule.display, &src_r, schedule.saved, &dst_r);
	}
}

void schedule_window_init(void)
{
	int x, y;
	SDL_GetWindowPosition(gfx.window, &x, &y);
	SDL_CTOR(SDL_CreateWindow, schedule.window, "スケジュール表",
			x, y, SCHEDULE_WINDOW_W, SCHEDULE_WINDOW_H,
			SDL_WINDOW_HIDDEN);
	schedule.window_id = SDL_GetWindowID(schedule.window);
	SDL_CTOR(SDL_CreateRenderer, schedule.renderer, schedule.window, -1, 0);
	SDL_CALL(SDL_SetRenderDrawColor, schedule.renderer, 0, 0, 0,  SDL_ALPHA_OPAQUE);
	SDL_CALL(SDL_RenderSetLogicalSize, schedule.renderer, SCHEDULE_WINDOW_W,
			SCHEDULE_WINDOW_H);
	SDL_CTOR(SDL_CreateTexture, schedule.texture, schedule.renderer,
			gfx.display->format->format, SDL_TEXTUREACCESS_STATIC,
			SCHEDULE_WINDOW_W, SCHEDULE_WINDOW_H);
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, schedule.parts, 0,
			SCHEDULE_WINDOW_W, SCHEDULE_WINDOW_H,
			GFX_INDEXED_BPP, GFX_INDEXED_FORMAT);
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, schedule.display, 0,
			SCHEDULE_WINDOW_W, SCHEDULE_WINDOW_H,
			GFX_DIRECT_BPP, GFX_DIRECT_FORMAT);
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, schedule.saved, 0,
			160, SCHEDULE_WINDOW_H,
			GFX_DIRECT_BPP, GFX_DIRECT_FORMAT);

	SDL_Surface *icon = icon_get(1);
	if (icon)
		SDL_SetWindowIcon(schedule.window, icon);

	// load UI parts
	struct cg *cg = asset_cg_load("dialy.gpx");
	if (!cg) {
		WARNING("Failed to load cg \"dialy.gpx\"");
		return;
	}
	assert(cg->metrics.w <= SCHEDULE_WINDOW_W);
	assert(cg->metrics.h <= SCHEDULE_WINDOW_H);
	uint8_t *base = schedule.parts->pixels;
	for (int row = 0; row < cg->metrics.h; row++) {
		uint8_t *dst = base + row * schedule.parts->pitch;
		uint8_t *src = cg->pixels + row * cg->metrics.w;
		memcpy(dst, src, cg->metrics.w);
	}

	assert(cg->palette);
	uint8_t *c = cg->palette;
	SDL_Color pal[256];
	for (int i = 0; i < 256; i++, c += 4) {
		pal[i].r = c[2];
		pal[i].g = c[1];
		pal[i].b = c[0];
	}

	SDL_CALL(SDL_SetPaletteColors, schedule.parts->format->palette, pal, 0, 256);
	cg_free(cg);

	SDL_CALL(SDL_SetColorKey, schedule.parts, SDL_TRUE, 10);

	// draw static part of display (room names)
	blit_parts(0, 0, 160, SCHEDULE_WINDOW_H, 0, 0);

	shuusaku_init_away_events(away_events);
	schedule.current_t = -1;
	schedule.plan_t = -1;
}

static void update_time(void)
{
	schedule.current_t = shuusaku_absolute_time(mem_get_sysvar16(60),
				mem_get_sysvar16(61));
}

static void schedule_close(void)
{
	SDL_HideWindow(schedule.window);
	audio_sysse_play("se03.wav", 0);
	schedule.open = false;
}

bool shuusaku_subwindow_valid(void)
{
	unsigned day = mem_get_sysvar16(60);
	unsigned t = mem_get_sysvar16(61);
	if (day == 0xffff || t == 0xffff)
		return false;
	if (day == DAY_MON && t == 500)
		return false;
	return true;
}

void shuusaku_schedule_window_toggle(void)
{
	if (schedule.open) {
		schedule_close();
	} else {
		if (!shuusaku_subwindow_valid())
			return;
		schedule.open = true;
		update_time();
		if (schedule.current_t >= 0) {
			// round down to nearest multiple of 2
			schedule.start_t = min(136, schedule.current_t) & ~1;
		}
		schedule_window_draw();
		SDL_ShowWindow(schedule.window);
		audio_sysse_play("se02.wav", 0);
	}
}

void schedule_window_update(void)
{
	if (!schedule.open)
		return;
	if (!shuusaku_subwindow_valid()) {
		schedule_close();
		return;
	}
	SDL_CALL(SDL_UpdateTexture, schedule.texture, NULL, schedule.display->pixels,
			schedule.display->pitch);
	SDL_CALL(SDL_RenderClear, schedule.renderer);
	SDL_CALL(SDL_RenderCopy, schedule.renderer, schedule.texture, NULL, NULL);
	SDL_RenderPresent(schedule.renderer);
}

void shuusaku_schedule_update(void)
{
	update_time();
	schedule_window_draw();
	schedule_window_update();
}

static void draw_current_time_box(void)
{
	if (!schedule.open)
		return;
	int i = schedule.current_t - (int)schedule.start_t;
	if (i < 0 || i >= 8)
		return;
	SDL_Rect src_r = { 480, 0, 80, SCHEDULE_WINDOW_H };
	SDL_Rect dst_r = { 160 + i * 80, 0, 80, SCHEDULE_WINDOW_H };
	SDL_CALL(SDL_BlitSurface, schedule.parts, &src_r, schedule.display, &dst_r);
	schedule_window_update();
}

static void draw_plan_time_box(void)
{
	if (!schedule.open)
		return;
	int i = schedule.plan_t - (int)schedule.start_t;
	if (i < 0 || i >= 8)
		return;
	SDL_Rect src_r = { 560, 0, 80, SCHEDULE_WINDOW_H };
	SDL_Rect dst_r = { 160 + i * 80, 0, 80, SCHEDULE_WINDOW_H };
	SDL_CALL(SDL_BlitSurface, schedule.parts, &src_r, schedule.display, &dst_r);
	schedule_window_update();
}

static void clear_current_time_box(void)
{
	if (!schedule.open)
		return;
	int i = schedule.current_t - (int)schedule.start_t;
	if (i < 0 || i >= 8)
		return;
	SDL_Rect src_r = { 0, 0, 80, SCHEDULE_WINDOW_H };
	SDL_Rect dst_r = { 160 + i * 80, 0, 80, SCHEDULE_WINDOW_H };
	SDL_CALL(SDL_BlitSurface, schedule.saved, &src_r, schedule.display, &dst_r);
	schedule_window_update();
}

static void clear_plan_time_box(void)
{
	if (!schedule.open)
		return;
	int i = schedule.plan_t - (int)schedule.start_t;
	if (i < 0 || i >= 8)
		return;
	SDL_Rect src_r = { 80, 0, 80, SCHEDULE_WINDOW_H };
	SDL_Rect dst_r = { 160 + i * 80, 0, 80, SCHEDULE_WINDOW_H };
	SDL_CALL(SDL_BlitSurface, schedule.saved, &src_r, schedule.display, &dst_r);
	schedule_window_update();
}

static void _load_image(const char *name, unsigned i, struct cg_metrics *metrics)
{
	struct cg *cg = asset_cg_load(name);
	if (!cg) {
		WARNING("Failed to load CG \"%s\"", name);
		return;
	}

	gfx_draw_cg(i, cg);
	memcpy(memory.palette + 10 * 4, cg->palette + 10 * 4, 236 * 4);
	if (metrics)
		*metrics = cg->metrics;
	cg_free(cg);
}

static void cam_event_wait(void)
{
	while (true) {
		if (input_down(INPUT_ACTIVATE)) {
			do {
				vm_delay(16);
				handle_events();
				gfx_update();
			} while (input_down(INPUT_ACTIVATE));
			break;
		}
		if (input_down(INPUT_CTRL))
			break;
		vm_delay(16);
		handle_events();
		shuusaku_schedule_tick();
		gfx_update();
	}
}

static bool have_cam_event(struct sched_cam_event *ev)
{
	for (int i = 0; i < 3 && ev->entries[i].flag_no; i++) {
		if (mem_get_var4_packed(ev->entries[i].flag_no))
			return true;
	}
	return false;
}

bool shuusaku_running_cam_event = false;

static void run_cam_event(struct sched_cam_event *ev)
{
	if (shuusaku_running_cam_event)
		return;
	if (!have_cam_event(ev))
		return;

	shuusaku_running_cam_event = true;
	game->flags[FLAG_ANIM_ENABLE] = 0;
	int saved_screen_y = gfx.surface[0].src.y;

	// save surface 0 pixels
	SDL_Surface *screen = gfx_get_surface(0);
	uint8_t *saved = xcalloc(640, 480);
	for (int row = 0; row < 480; row++) {
		memcpy(saved+row*640, screen->pixels+row*screen->pitch, 640);
	}
	// save palettes
	uint8_t mem_palette[256*4];
	memcpy(mem_palette, memory.palette, 256*4);
	SDL_Color gfx_palette[256];
	memcpy(gfx_palette, gfx.palette, sizeof(gfx_palette));

	SDL_RaiseWindow(gfx.window);

	for (int i = 0; i < 3 && ev->entries[i].flag_no; i++) {
		struct sched_cam_event_entry *e = &ev->entries[i];
		uint8_t flag = mem_get_var4_packed(e->flag_no);
		if (!flag)
			continue;

		// TODO: animated cursor
		//cursor_load(2, 1, NULL);
		shuusaku_crossfade_to(0, 0, 0);
		gfx.surface[0].src.y = 0;

		char name[16];
		if (flag & 2) {
			// video
			_load_image("ev11.gpx", 1, NULL);
			gfx_copy(0, 0, 640, 480, 1, 0, 0, 0);
			shuusaku_crossfade(memory.palette, false);

			snprintf(name, 16, "%s.mdd", e->name);
			shuusaku_play_movie(name);
			shuusaku_after_movie_crossfade();
			// fill with proper black before updating palette
			gfx_fill(0, 0, 640, 72, 0, 12);
			gfx_fill(0, 312, 640, 168, 0, 12);
			gfx_fill(0, 72, 56, 240, 0, 12);
			gfx_fill(376, 72, 264, 240, 0, 12);

			snprintf(name, 16, "%s.gpx", e->name);
			_load_image(name, 1, NULL);
			shuusaku_update_palette(memory.palette);
			shuusaku_zoom(56, 72, 320, 240, 1);
		} else {
			// photo
			snprintf(name, 16, "%s.gpx", e->name);
			_load_image(name, 1, NULL);
			gfx_copy(0, 0, 640, 480, 1, 0, 0, 0);
			shuusaku_crossfade(memory.palette, false);
		}

		//cursor_unload();
		cam_event_wait();

		if (e->zoom_name) {
			struct cg_metrics metrics;
			snprintf(name, 16, "%s.gpx", e->zoom_name);
			_load_image(name, 1, &metrics);
			shuusaku_cam_event_zoom(metrics.x, metrics.y, metrics.w, metrics.h);
			gfx_copy(metrics.x, metrics.y, metrics.w, metrics.h, 1, metrics.x, metrics.y, 0);
			cam_event_wait();
		}
	}

	shuusaku_crossfade_to(0, 0, 0);
	gfx.surface[0].src.y = saved_screen_y;

	// restore palette
	memcpy(memory.palette, mem_palette, sizeof(mem_palette));

	for (int row = 0; row < 480; row++) {
		memcpy(screen->pixels+row*screen->pitch, saved+row*640, 640);
	}
	free(saved);

	// crossfade to original palette/pixels
	_gfx_palette_crossfade(gfx_palette, 0, 256, mem_get_sysvar16(13) * 16);

	gfx.surface[0].src.y = saved_screen_y;
	game->flags[FLAG_ANIM_ENABLE] = FLAG_ALWAYS_ON;
	shuusaku_running_cam_event = false;
}

static struct sched_cam_event *clicked_cam_event = NULL;

static void schedule_mouse_down(int x, int y)
{
	if (x < 160 || x >= 160 + 8 * 80 || y < 24 || y >= 24 + 32 * 11)
		return;
	x -= 160;
	y -= 24;
	unsigned t = schedule.start_t +  x / 80;
	unsigned loc = y / 32;
	clicked_cam_event = shuusaku_get_cam_event(loc, t);
}

static void draw_flash(bool on)
{
	if (on) {
		clear_current_time_box();
		if (schedule.plan_t >= 0 && schedule.plan_t != schedule.current_t)
			clear_plan_time_box();
	} else {
		draw_plan_time_box();
		draw_current_time_box();
	}

}

void shuusaku_schedule_tick(void)
{
	if (clicked_cam_event) {
		struct sched_cam_event *ev = clicked_cam_event;
		clicked_cam_event = NULL;
		run_cam_event(ev);
	}

	if (!vm_timer_tick_async(&schedule.flash_timer, 1000))
		return;

	draw_flash(schedule.flash_on);
	schedule.flash_on = !schedule.flash_on;
}

static void scroll_left(void)
{
	if (schedule.start_t >= 2) {
		schedule.start_t -= 2;
		schedule_window_draw();
		draw_flash(!schedule.flash_on);
		schedule_window_update();
	}
}

static void scroll_right(void)
{
	if (schedule.start_t <= 134) {
		schedule.start_t += 2;
		schedule_window_draw();
		draw_flash(!schedule.flash_on);
		schedule_window_update();
	}
}

bool shuusaku_schedule_window_event(SDL_Event *e)
{
	if (!schedule.open)
		return false;
	switch (e->type) {
	case SDL_WINDOWEVENT:
		if (e->window.windowID != schedule.window_id)
			break;
		switch (e->window.event) {
		case SDL_WINDOWEVENT_SHOWN:
		case SDL_WINDOWEVENT_EXPOSED:
		case SDL_WINDOWEVENT_RESIZED:
		case SDL_WINDOWEVENT_SIZE_CHANGED:
		case SDL_WINDOWEVENT_MAXIMIZED:
		case SDL_WINDOWEVENT_RESTORED:
			schedule_window_update();
			return true;
		case SDL_WINDOWEVENT_CLOSE:
			assert(schedule.open);
			shuusaku_schedule_window_toggle();
			return true;
		}
		break;
	case SDL_KEYDOWN:
		if (e->key.windowID != schedule.window_id)
			break;
		switch (e->key.keysym.sym) {
		case SDLK_LEFT:
			scroll_left();
			return true;
		case SDLK_RIGHT:
			scroll_right();
			return true;
		}
		break;
	case SDL_MOUSEBUTTONDOWN:
		if (e->button.windowID != schedule.window_id)
			break;
		if (e->button.button != SDL_BUTTON_LEFT)
			return true;
		schedule_mouse_down(e->button.x, e->button.y);
		return true;
	case SDL_MOUSEWHEEL:
		if (e->wheel.windowID != schedule.window_id)
			break;
		if (e->wheel.y < 0) {
			scroll_right();
		} else if (e->wheel.y > 0) {
			scroll_left();
		}
		return true;
	}
	return false;
}

void shuusaku_schedule_set_plan_time(unsigned day, unsigned t)
{
	schedule.plan_t = shuusaku_absolute_time(day, t);
	schedule_window_draw();
	schedule_window_update();

	// XXX: hack to immediately update flashing box
	schedule.flash_timer = vm_timer_create() - 1001;
	schedule.flash_on = false;
	shuusaku_schedule_tick();
}

void shuusaku_schedule_clear_plan(void)
{
	clear_plan_time_box();
	schedule.plan_t = -1;
}
