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

#ifndef AI5_ANIM_H
#define AI5_ANIM_H

#include <stdbool.h>
#include <stdint.h>

struct anim_draw_call;

extern unsigned anim_frame_t;
extern void(*anim_load_palette)(uint8_t*);

enum anim_state {
	// stream is in halted state
	ANIM_STATE_HALTED,
	// stream is in running state
	ANIM_STATE_RUNNING,
	// halt on next CHECK_STOP instruction
	ANIM_STATE_HALT_NEXT,
	// waiting until halted
	ANIM_STATE_WAITING,
	// pause on next CHECK_STOP instruction
	ANIM_STATE_PAUSE_NEXT,
	// stream is in paused state
	ANIM_STATE_PAUSED,
};

void anim_execute(void);
bool anim_running(void);
bool anim_stream_running(unsigned slot);
void anim_init_stream(unsigned slot, unsigned stream);
void anim_init_stream_from(unsigned slot, unsigned stream, uint32_t off);
enum anim_state anim_get_state(unsigned slot);
void anim_start(unsigned slot);
void anim_stop(unsigned slot);
void anim_pause(unsigned slot);
void anim_pause_sync(unsigned slot);
void anim_unpause(unsigned slot);
void anim_halt(unsigned slot);
void anim_wait(unsigned slot);
void anim_stop_all(void);
void anim_halt_all(void);
void anim_reset_all(void);
void anim_wait_all(void);
void anim_pause_range_sync(unsigned start, unsigned end);
void anim_pause_all_sync(void);
void anim_unpause_range(unsigned start, unsigned end);
void anim_unpause_all(void);
void anim_set_offset(unsigned slot, unsigned x, unsigned y);
void anim_exec_copy_call(unsigned stream);
void anim_decompose_draw_call(struct anim_draw_call *call, int *dst_x, int *dst_y, int *w,
		int *h);

#endif // AI5_ANIM_H
