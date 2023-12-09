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

#include <stdlib.h>
#include <SDL.h>

#include "nulib.h"
#include "nulib/little_endian.h"
#include "ai5/s4.h"

#include "anim.h"
#include "gfx.h"
#include "memory.h"
#include "vm.h"

struct anim_stream {
	uint8_t cmd;
	// pointer to S4 file in memory
	uint8_t *file_data;
	// pointer to stream's bytecode
	uint8_t *bytecode;
	// instruction pointer
	unsigned ip;
	// number of cycles to stall
	unsigned stall_count;
	// loop info (1)
	struct { unsigned start, count; } loop;
	// loop info (2)
	struct { unsigned start, count; } loop2;
	// offset into animation CG
	struct { unsigned x, y; } off;
};

static struct anim_stream streams[S4_MAX_STREAMS] = {0};

enum anim_command {
	// stream is in halted state
	ANIM_CMD_HALTED,
	// stream is in running state
	ANIM_CMD_RUN,
	// halt on next CHECK_STOP instruction
	ANIM_CMD_STOP,
	// halt after next instruction
	ANIM_CMD_HALT_NEXT,
};

static void check_stream_index(unsigned i)
{
	if (i >= S4_MAX_STREAMS)
		VM_ERROR("Invalid animation stream index: %u", i);
}

void anim_init_stream(unsigned stream)
{
	check_stream_index(stream);
	struct anim_stream *anim = &streams[stream];

	memset(anim, 0, sizeof(struct anim_stream));
	anim->file_data = memory.file_data + memory_system_var32()[MES_SYS_VAR_DATA_OFFSET];
	anim->bytecode = anim->file_data + le_get16(anim->file_data, 1 + stream * 2);
}

void anim_start(unsigned stream)
{
	check_stream_index(stream);
	streams[stream].cmd = ANIM_CMD_RUN;
	streams[stream].ip = 0;
}

void anim_stop(unsigned stream)
{
	check_stream_index(stream);
	streams[stream].cmd = ANIM_CMD_STOP;
}

void anim_halt(unsigned stream)
{
	check_stream_index(stream);
	streams[stream].cmd = ANIM_CMD_HALTED;
}

void anim_stop_all(void)
{
	for (int i = 0; i < S4_MAX_STREAMS; i++) {
		if (streams[i].cmd != ANIM_CMD_HALTED)
			streams[i].cmd = ANIM_CMD_STOP;
	}
}

void anim_halt_all(void)
{
	for (int i = 0; i < S4_MAX_STREAMS; i++) {
		streams[i].cmd = ANIM_CMD_HALTED;
	}
}

void anim_set_offset(unsigned stream, unsigned x, unsigned y)
{
	check_stream_index(stream);
	streams[stream].off.x = x;
	streams[stream].off.y = y;
}

static uint8_t read_byte(struct anim_stream *anim)
{
	return anim->bytecode[anim->ip++];
}

static bool anim_stream_draw(struct anim_stream *anim, uint8_t i)
{
	if (i < 20) {
		WARNING("Invalid draw call index: %u", i);
		return false;
	}

	unsigned off = 1 + anim->file_data[0] * 2 + (i - 20) * S4_DRAW_CALL_SIZE;
	struct s4_draw_call call;
	if (!s4_parse_draw_call(anim->file_data + off, &call)) {
		WARNING("Failed to parse draw call %u", i);
		return false;
	}

	switch (call.op) {
	case S4_DRAW_OP_FILL:
		gfx_fill(call.fill.dst.x, call.fill.dst.y, call.fill.dim.w, call.fill.dim.h,
				call.fill.dst.i, 8);
		break;
	case S4_DRAW_OP_COPY:
		gfx_copy(call.copy.src.x, call.copy.src.y, call.copy.dim.w, call.copy.dim.h,
				call.copy.src.i, call.copy.dst.x, call.copy.dst.y,
				call.copy.dst.i);
		break;
	case S4_DRAW_OP_COPY_MASKED:
		gfx_copy_masked(call.copy.src.x, call.copy.src.y, call.copy.dim.w,
				call.copy.dim.h, call.copy.src.i, call.copy.dst.x,
				call.copy.dst.y, call.copy.dst.i,
				memory_system_var16()[MES_SYS_VAR_MASK_COLOR]);
		break;
	case S4_DRAW_OP_SWAP:
		gfx_copy_swap(call.copy.src.x, call.copy.src.y, call.copy.dim.w,
				call.copy.dim.h, call.copy.src.i, call.copy.dst.x,
				call.copy.dst.y, call.copy.dst.i);
		break;
	case S4_DRAW_OP_COMPOSE:
		gfx_compose(call.compose.fg.x, call.compose.fg.y, call.compose.dim.w,
				call.compose.dim.h, call.compose.fg.i, call.compose.bg.x,
				call.compose.bg.y, call.compose.bg.i, call.compose.dst.x,
				call.compose.dst.y, call.compose.dst.i,
				memory_system_var16()[MES_SYS_VAR_MASK_COLOR]);
		break;
	case S4_DRAW_OP_SET_COLOR:
		break;
	case S4_DRAW_OP_SET_PALETTE:
		break;
	}
	return true;
}

static bool anim_stream_execute(struct anim_stream *anim)
{
	if (anim->stall_count) {
		anim->stall_count--;
		return false;
	}
	uint8_t op = read_byte(anim);
	switch (op) {
	case S4_OP_NOOP:
		break;
	case S4_OP_CHECK_STOP:
		if (anim->cmd == ANIM_CMD_STOP)
			anim->cmd = ANIM_CMD_HALTED;
		break;
	case S4_OP_STALL:
		anim->stall_count = read_byte(anim);
		break;
	case S4_OP_RESET:
		anim->ip = 0;
		break;
	case S4_OP_HALT:
		anim->cmd = ANIM_CMD_HALTED;
		break;
	case S4_OP_LOOP_START:
		anim->loop.count = read_byte(anim);
		anim->loop.start = anim->ip;
		break;
	case S4_OP_LOOP_END:
		if (anim->loop.count && --anim->loop.count)
			anim->ip = anim->loop.start;
		break;
	case S4_OP_LOOP2_START:
		anim->loop2.count = read_byte(anim);
		anim->loop2.start = anim->ip;
		break;
	case S4_OP_LOOP2_END:
		if (anim->loop2.count && --anim->loop2.count)
			anim->ip = anim->loop2.start;
		break;
	default:
		return anim_stream_draw(anim, op);
	}
	return false;
}


void anim_execute(void)
{
	static uint32_t anim_prev_frame_t = 0;
	uint32_t t = vm_get_ticks();
	if (t - anim_prev_frame_t < 16)
		return;

	anim_prev_frame_t = t;
	for (int i = 0; i < S4_MAX_STREAMS; i++) {
		struct anim_stream *anim = &streams[i];
		if (anim->cmd == ANIM_CMD_HALTED)
			continue;
		anim_stream_execute(anim);
	}
}

bool anim_stream_running(unsigned stream)
{
	assert(stream < S4_MAX_STREAMS);
	return streams[stream].cmd != ANIM_CMD_HALTED;
}

bool anim_running(void)
{
	for (int i = 0; i < S4_MAX_STREAMS; i++) {
		if (streams[i].cmd != ANIM_CMD_HALTED)
			return true;
	}
	return false;
}
