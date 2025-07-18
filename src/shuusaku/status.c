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

#include "asset.h"
#include "audio.h"
#include "memory.h"
#include "gfx.h"
#include "gfx_private.h"
#include "vm.h"

#include "shuusaku.h"

#define STATUS_WINDOW_W 640
#define STATUS_WINDOW_H 64
#define STATUS_PARTS_H 320

static struct {
	bool open;
	uint32_t window_id;
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	SDL_Surface *parts;
	SDL_Surface *display;
} status;

/*
 * Wrapper to copy from the parts CG to the display surface.
 */
static void blit_parts(int src_x, int src_y, int w, int h, int dst_x, int dst_y)
{
	SDL_Rect src_r = { src_x, src_y, w, h };
	SDL_Rect dst_r = { dst_x, dst_y, w, h };
	SDL_CALL(SDL_BlitSurface, status.parts, &src_r, status.display, &dst_r);
}

static void blit_number(unsigned n, int i)
{
	n = min(n, 15);
	unsigned row = n / 5;
	unsigned col = n % 5;
	blit_parts(392 + col * 40, 64 + row * 16, 40, 16, 348 + i * 60, 40);
}

static void blit_meal(unsigned value, int i)
{
	value = min(value, 4);
	// image
	blit_parts(value * 56, 72, 56, 32, 148 + i * 60, 8);
	// label
	blit_parts(i * 56, 144, 56, 16, 148 + i * 60, 40);
}

static void blit_bath_temp(unsigned value, int i)
{
	value = min(value, 4);
	// image
	blit_parts(value * 56, 112, 56, 32, 12 + i * 60, 8);
	// text
	blit_parts(i * 112, 144, 56, 16, 12 + i * 60, 40);
}

static void status_window_draw(void)
{
	if (!status.open)
		return;

	// background
	blit_parts(0, 0, STATUS_WINDOW_W, 64, 0, 0);

	// item counts
	blit_number(mem_get_var16(101), 0);
	blit_number(mem_get_var16(100), 1);
	blit_number(mem_get_var16(102), 2);
	blit_number(mem_get_var16(104), 3);
	blit_number(mem_get_var16(103), 4);

	unsigned abs_t = shuusaku_absolute_time(mem_get_sysvar16(60), mem_get_sysvar16(61));
	if (abs_t >= shuusaku_absolute_time(DAY_SAT, 1900)) {
		// Saturday dinner
		blit_meal(mem_get_var4_packed(170), 0);
	}
	if (abs_t >= shuusaku_absolute_time(DAY_SAT, 2100)) {
		// Saturday bath
		blit_bath_temp(mem_get_var4_packed(245), 0);
	}
	if (abs_t >= shuusaku_absolute_time(DAY_SUN, 900)) {
		// Sunday breakfast
		blit_meal(mem_get_var4_packed(276), 1);
	}
	if (abs_t >= shuusaku_absolute_time(DAY_SUN, 1900)) {
		// Sunday dinner
		blit_meal(mem_get_var4_packed(311), 2);
	}
	if (abs_t >= shuusaku_absolute_time(DAY_SUN, 2100)) {
		// Sunday bath
		blit_bath_temp(mem_get_var4_packed(326), 1);
	}
}

static void status_window_update(void)
{
	if (!status.open)
		return;
	SDL_CALL(SDL_UpdateTexture, status.texture, NULL, status.display->pixels,
			status.display->pitch);
	SDL_CALL(SDL_RenderClear, status.renderer);
	SDL_CALL(SDL_RenderCopy, status.renderer, status.texture, NULL, NULL);
	SDL_RenderPresent(status.renderer);
}

void shuusaku_status_window_toggle(void)
{
	if (status.open) {
		// close
		SDL_HideWindow(status.window);
		audio_sysse_play("se03.wav", 0);
		status.open = false;
	} else {
		// open
		status.open = true;
		status_window_draw();
		SDL_ShowWindow(status.window);
		audio_sysse_play("se02.wav", 0);
	}
}

void shuusaku_status_update(void)
{
	status_window_draw();
	status_window_update();
}

bool shuusaku_status_window_event(SDL_Event *e)
{
	if (!status.open)
		return false;
	switch (e->type) {
	case SDL_WINDOWEVENT:
		if (e->window.windowID != status.window_id)
			break;
		switch (e->window.event) {
		case SDL_WINDOWEVENT_SHOWN:
		case SDL_WINDOWEVENT_EXPOSED:
		case SDL_WINDOWEVENT_RESIZED:
		case SDL_WINDOWEVENT_SIZE_CHANGED:
		case SDL_WINDOWEVENT_MAXIMIZED:
		case SDL_WINDOWEVENT_RESTORED:
			status_window_update();
			return true;
		case SDL_WINDOWEVENT_CLOSE:
			assert(status.open);
			shuusaku_status_window_toggle();
			return true;
		}
		break;
	}
	return false;
}

void shuusaku_status_init(void)
{
	int x, y;
	SDL_GetWindowPosition(gfx.window, &x, &y);
	SDL_CTOR(SDL_CreateWindow, status.window, "風呂・食事＆アイテム",
			x, y, STATUS_WINDOW_W, STATUS_WINDOW_H,
			SDL_WINDOW_HIDDEN);
	status.window_id = SDL_GetWindowID(status.window);
	SDL_CTOR(SDL_CreateRenderer, status.renderer, status.window, -1, 0);
	SDL_CALL(SDL_SetRenderDrawColor, status.renderer, 0, 0, 0,  SDL_ALPHA_OPAQUE);
	SDL_CALL(SDL_RenderSetLogicalSize, status.renderer, STATUS_WINDOW_W,
			STATUS_WINDOW_H);
	SDL_CTOR(SDL_CreateTexture, status.texture, status.renderer,
			gfx.display->format->format, SDL_TEXTUREACCESS_STATIC,
			STATUS_WINDOW_W, STATUS_WINDOW_H);
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, status.parts, 0,
			STATUS_WINDOW_W, STATUS_PARTS_H,
			GFX_INDEXED_BPP, GFX_INDEXED_FORMAT);
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormat, status.display, 0,
			STATUS_WINDOW_W, STATUS_WINDOW_H,
			GFX_DIRECT_BPP, GFX_DIRECT_FORMAT);

	SDL_Surface *icon = icon_get(2);
	if (icon)
		SDL_SetWindowIcon(status.window, icon);

	// load UI parts
	struct cg *cg = asset_cg_load("propitem.gpx");
	if (!cg) {
		WARNING("Failed to load cg \"propitem.gpx\"");
		return;
	}
	assert(cg->metrics.w <= STATUS_WINDOW_W);
	assert(cg->metrics.h <= STATUS_PARTS_H);
	uint8_t *base = status.parts->pixels;
	for (int row = 0; row < cg->metrics.h; row++) {
		uint8_t *dst = base + row * status.parts->pitch;
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

	SDL_CALL(SDL_SetPaletteColors, status.parts->format->palette, pal, 0, 256);
	cg_free(cg);
}
