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
#include "ai5/anim.h"

#include "anim.h"
#include "gfx.h"
#include "memory.h"
#include "vm.h"

#if 0
#define ANIM_LOG(...) NOTICE(__VA_ARGS__)
#else
#define ANIM_LOG(...)
#endif

#if 0
#define STREAM_LOG(...) NOTICE(__VA_ARGS__)
#else
#define STREAM_LOG(...)
#endif

#define ANIM_NR_SLOTS ANIM_MAX_STREAMS

struct anim_stream {
	uint8_t state;
	// pointer to S4/A file in memory
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
	// index of stream in animation file
	unsigned stream;
	// animation initialized
	bool initialized;
};

static struct anim_stream streams[ANIM_MAX_STREAMS] = {0};

void(*anim_load_palette)(uint8_t*) = NULL;

static void check_slot(unsigned i)
{
	if (i >= ANIM_NR_SLOTS)
		VM_ERROR("Invalid animation slot index: %u", i);
}

static uint32_t get_mask_color(void)
{
	if (game->bpp == 24)
		return mem_get_sysvar32(mes_sysvar32_mask_color);
	else if (game->id == GAME_SHUUSAKU)
		return 10;
	return mem_get_sysvar16(mes_sysvar16_mask_color);
}

static void _anim_init_stream(unsigned slot, unsigned stream, uint32_t off)
{
	struct anim_stream *anim = &streams[slot];
	memset(anim, 0, sizeof(struct anim_stream));
	anim->file_data = memory.file_data + off;
	if (anim_type == ANIM_S4 || anim_type == ANIM_A8) {
		anim->bytecode = anim->file_data + le_get16(anim->file_data, 1 + stream * 2);
	} else {
		anim->bytecode = anim->file_data + le_get32(anim->file_data, 2 + stream * 4);
	}
	anim->stream = stream;
	anim->initialized = true;
}

void anim_init_stream(unsigned slot, unsigned stream)
{
	ANIM_LOG("anim_init_stream(%u,%u)", slot, stream);
	check_slot(slot);
	if (stream >= ANIM_MAX_STREAMS)
		VM_ERROR("Invalid animation stream index: %u", stream);
	_anim_init_stream(slot, stream, mem_get_sysvar32(mes_sysvar32_data_offset));
}

void anim_init_stream_from(unsigned slot, unsigned stream, uint32_t off)
{
	ANIM_LOG("anim_init_stream_from(%u,%u) @ 0x%08x", slot, stream, off);
	check_slot(slot);
	if (stream >= ANIM_MAX_STREAMS)
		VM_ERROR("Invalid animation stream index: %u", stream);
	_anim_init_stream(slot, stream, off);
}

enum anim_state anim_get_state(unsigned slot)
{
	check_slot(slot);
	return streams[slot].state;
}

void anim_start(unsigned slot)
{
	ANIM_LOG("anim_start(%u)", slot);
	check_slot(slot);
	if (streams[slot].initialized) {
		streams[slot].state = ANIM_STATE_RUNNING;
		streams[slot].ip = 0;
	}
}

void anim_stop(unsigned slot)
{
	ANIM_LOG("anim_stop(%u)", slot);
	check_slot(slot);
	streams[slot].state = ANIM_STATE_HALT_NEXT;
}

void anim_pause(unsigned slot)
{
	ANIM_LOG("anim_pause(%u)", slot);
	check_slot(slot);
	if (streams[slot].initialized && streams[slot].state == ANIM_STATE_RUNNING)
		streams[slot].state = ANIM_STATE_PAUSE_NEXT;
}

void anim_pause_sync(unsigned slot)
{
	ANIM_LOG("anim_pause_sync(%u)", slot);
	check_slot(slot);
	if (streams[slot].initialized || streams[slot].state != ANIM_STATE_RUNNING)
		return;

	streams[slot].state = ANIM_STATE_PAUSE_NEXT;
	do {
		vm_peek();
	} while (streams[slot].state != ANIM_STATE_PAUSED);
}

void anim_unpause(unsigned slot)
{
	ANIM_LOG("anim_unpause(%u)", slot);
	check_slot(slot);
	if (streams[slot].initialized && streams[slot].state == ANIM_STATE_PAUSED)
		streams[slot].state = ANIM_STATE_RUNNING;
}

void anim_halt(unsigned slot)
{
	ANIM_LOG("anim_halt(%u)", slot);
	check_slot(slot);
	streams[slot].state = ANIM_STATE_HALTED;
	streams[slot].initialized = false;
}

void anim_wait(unsigned slot)
{
	ANIM_LOG("anim_wait(%u)", slot);
	check_slot(slot);
	streams[slot].state = ANIM_STATE_WAITING;
	do {
		vm_peek();
	} while (streams[slot].state != ANIM_STATE_HALTED);
}

void anim_stop_all(void)
{
	ANIM_LOG("anim_stop_all()");
	for (int i = 0; i < ANIM_NR_SLOTS; i++) {
		if (streams[i].state != ANIM_STATE_HALTED)
			streams[i].state = ANIM_STATE_HALT_NEXT;
	}
}

void anim_halt_all(void)
{
	ANIM_LOG("anim_halt_all()");
	for (int i = 0; i < ANIM_MAX_STREAMS; i++) {
		streams[i].state = ANIM_STATE_HALTED;
		streams[i].initialized = false;
	}
}

void anim_reset_all(void)
{
	ANIM_LOG("anim_reset_all()");
	for (int i = 0; i < ANIM_MAX_STREAMS; i++) {
		if (streams[i].state == ANIM_STATE_HALTED)
			continue;
		_anim_init_stream(i, streams[i].stream, mem_get_sysvar32(mes_sysvar32_data_offset));
	}
}

void anim_wait_all(void)
{
	ANIM_LOG("anim_wait_all()");
	while (anim_running()) {
		vm_peek();
	}
}

bool anim_range_running(unsigned start, unsigned end)
{
	for (unsigned i = start; i < end; i++) {
		if (streams[i].state != ANIM_STATE_HALTED && streams[i].state != ANIM_STATE_PAUSED)
			return true;
	}
	return false;
}

bool anim_any_running(void)
{
	return anim_range_running(0, ANIM_MAX_STREAMS);
}

void anim_pause_range_sync(unsigned start, unsigned end)
{
	for (unsigned i = start; i < end; i++) {
		if (streams[i].state == ANIM_STATE_RUNNING)
			streams[i].state = ANIM_STATE_PAUSE_NEXT;
	}
	// wait for all animations to enter halted or paused state
	do {
		vm_peek();
	} while (anim_range_running(start, end));
}

void anim_pause_all_sync(void)
{
	ANIM_LOG("anim_pause_all_sync()");
	anim_pause_range_sync(0, ANIM_MAX_STREAMS);
}

void anim_unpause_range(unsigned start, unsigned end)
{
	for (int i = start; i < end; i++) {
		if (streams[i].state == ANIM_STATE_PAUSED)
			streams[i].state = ANIM_STATE_RUNNING;
	}
}

void anim_unpause_all(void)
{
	ANIM_LOG("anim_unpause_all()");
	anim_unpause_range(0, ANIM_MAX_STREAMS);
}

void anim_set_offset(unsigned slot, unsigned x, unsigned y)
{
	ANIM_LOG("anim_set_offset(%u,%u,%u)", slot, x, y);
	check_slot(slot);
	streams[slot].off.x = x;
	streams[slot].off.y = y;
}

static uint16_t read_value(struct anim_stream *anim)
{
	if (anim_type == ANIM_S4 || anim_type == ANIM_A8)
		return anim->bytecode[anim->ip++];
	uint16_t code = le_get16(anim->bytecode, anim->ip);
	anim->ip += 2;
	return code;
}

static bool anim_stream_draw(struct anim_stream *anim, uint8_t i)
{
	if (i < 20) {
		WARNING("Invalid draw call index: %u", i);
		return false;
	}

	// compute offset
	unsigned off;
	if (anim_type == ANIM_S4) {
		off = 1 + anim->file_data[0] * 2 + (i - 20) * anim_draw_call_size;
	} else if (anim_type == ANIM_A8) {
		off = 1 + 10 * 2 + (i - 20) * anim_draw_call_size;
	} else {
		off = 2 + 100 * 4 + (i - 20) * anim_draw_call_size;
	}

	// parse
	struct anim_draw_call call;
	unsigned src_i = 1;
	if (ai5_target_game == GAME_DOUKYUUSEI) {
		src_i = 9;
	} else if (ai5_target_game == GAME_SHUUSAKU) {
		src_i = ((anim - streams) < 10) ? 6 : 7;
	}
	if (!anim_parse_draw_call(anim->file_data + off, &call, src_i)) {
		WARNING("Failed to parse draw call %u", i);
		return false;
	}

	// execute
	switch (call.op) {
	case ANIM_DRAW_OP_FILL:
		STREAM_LOG("FILL %u(%u,%u) @ (%u,%u);", call.fill.dst.i, call.fill.dst.x,
				call.fill.dst.y, call.fill.dim.w, call.fill.dim.h);
		gfx_fill(call.fill.dst.x + anim->off.x, call.fill.dst.y + anim->off.y,
				call.fill.dim.w, call.fill.dim.h, call.fill.dst.i, 8);
		break;
	case ANIM_DRAW_OP_COPY:
		STREAM_LOG("COPY %u(%u,%u) -> %u(%u,%u) @ (%u,%u);", call.copy.src.i,
				call.copy.src.x, call.copy.src.y, call.copy.dst.i,
				call.copy.dst.x, call.copy.dst.y, call.copy.dim.w,
				call.copy.dim.h);
		gfx_copy(call.copy.src.x, call.copy.src.y, call.copy.dim.w, call.copy.dim.h,
				call.copy.src.i, call.copy.dst.x + anim->off.x,
				call.copy.dst.y + anim->off.y, call.copy.dst.i);
		break;
	case ANIM_DRAW_OP_COPY_MASKED:
		STREAM_LOG("COPY_MASKED %u(%u,%u) -> %u(%u,%u) @ (%u,%u);", call.copy.src.i,
				call.copy.src.x, call.copy.src.y, call.copy.dst.i,
				call.copy.dst.x, call.copy.dst.y, call.copy.dim.w,
				call.copy.dim.h);
		gfx_copy_masked(call.copy.src.x, call.copy.src.y, call.copy.dim.w,
				call.copy.dim.h, call.copy.src.i,
				call.copy.dst.x + anim->off.x, call.copy.dst.y + anim->off.y,
				call.copy.dst.i, get_mask_color());
		break;
	case ANIM_DRAW_OP_SWAP:
		STREAM_LOG("SWAP %u(%u,%u) -> %u(%u,%u) @ (%u,%u);", call.copy.src.i,
				call.copy.src.x, call.copy.src.y, call.copy.dst.i,
				call.copy.dst.x, call.copy.dst.y, call.copy.dim.w,
				call.copy.dim.h);
		gfx_copy_swap(call.copy.src.x, call.copy.src.y, call.copy.dim.w,
				call.copy.dim.h, call.copy.src.i,
				call.copy.dst.x + anim->off.x, call.copy.dst.y + anim->off.y,
				call.copy.dst.i);
		break;
	case ANIM_DRAW_OP_COMPOSE:
		STREAM_LOG("COMPOSE %u(%u,%u) + %u(%u,%u) -> %u(%u,%u) @ (%u,%u);",
				call.compose.bg.i, call.compose.bg.x, call.compose.bg.y,
				call.compose.fg.i, call.compose.fg.x, call.compose.fg.y,
				call.compose.dst.i, call.compose.dst.x, call.compose.dst.y,
				call.compose.dim.w, call.compose.dim.h);
		gfx_compose(call.compose.fg.x, call.compose.fg.y, call.compose.dim.w,
				call.compose.dim.h, call.compose.fg.i, call.compose.bg.x,
				call.compose.bg.y, call.compose.bg.i,
				call.compose.dst.x + anim->off.x,
				call.compose.dst.y + anim->off.y,
				call.compose.dst.i, get_mask_color());
		break;
	case ANIM_DRAW_OP_SET_COLOR:
		break;
	case ANIM_DRAW_OP_SET_PALETTE:
		break;
	case ANIM_DRAW_OP_COMPOSE_WITH_OFFSET:
	case ANIM_DRAW_OP_0x60_COPY_MASKED:
	case ANIM_DRAW_OP_0x61_COMPOSE:
	case ANIM_DRAW_OP_0x62:
	case ANIM_DRAW_OP_0x63_COPY_MASKED_WITH_XOFFSET:
	case ANIM_DRAW_OP_0x64_COMPOSE_MASKED:
	case ANIM_DRAW_OP_0x65_COMPOSE:
	case ANIM_DRAW_OP_0x66:
		// TODO: BE-YOND
		break;
	}
	if (game->after_anim_draw)
		game->after_anim_draw(&call);
	return true;
}

bool anim_stream_execute(struct anim_stream *anim)
{
	if (anim->stall_count) {
		anim->stall_count--;
		return false;
	}
	uint16_t op = read_value(anim);
	switch (op) {
	case ANIM_OP_NOOP:
		STREAM_LOG("NOOP;");
		break;
	case ANIM_OP_CHECK_STOP:
		STREAM_LOG("CHECK_STOP;");
		if (anim->state == ANIM_STATE_HALT_NEXT)
			anim->state = ANIM_STATE_HALTED;
		else if (anim->state == ANIM_STATE_PAUSE_NEXT)
			anim->state = ANIM_STATE_PAUSED;
		break;
	case ANIM_OP_STALL:
		anim->stall_count = read_value(anim);
		STREAM_LOG("STALL %u;", anim->stall_count);
		break;
	case ANIM_OP_RESET:
		STREAM_LOG("RESET;");
		anim->ip = 0;
		break;
	case ANIM_OP_HALT:
		STREAM_LOG("HALT;");
		anim->state = ANIM_STATE_HALTED;
		break;
	case ANIM_OP_LOOP_START:
		anim->loop.count = read_value(anim);
		anim->loop.start = anim->ip;
		STREAM_LOG("LOOP_START %u;", anim->loop.count);
		break;
	case ANIM_OP_LOOP_END:
		STREAM_LOG("LOOP_END;");
		if (anim->loop.count && --anim->loop.count)
			anim->ip = anim->loop.start;
		break;
	case ANIM_OP_LOOP2_START:
		anim->loop2.count = read_value(anim);
		anim->loop2.start = anim->ip;
		STREAM_LOG("LOOP2_START %u;", anim->loop2.start);
		break;
	case ANIM_OP_LOOP2_END:
		STREAM_LOG("LOOP2_END;");
		if (anim->loop2.count && --anim->loop2.count)
			anim->ip = anim->loop2.start;
		break;
	case 0xff:
	case 0xffff:
		STREAM_LOG("[END];");
		anim->state = ANIM_STATE_HALTED;
		break;
	default:
		// XXX: ANIM_OP_LOAD_PALETTE is a valid draw call index, so we only
		//      execute it as an instruction if anim_load_palette is set.
		if (op == ANIM_OP_LOAD_PALETTE && anim_load_palette) {
			uint16_t addr = le_get16(anim->bytecode, anim->ip);
			STREAM_LOG("LOAD_PALETTE %u;", addr);
			anim_load_palette(anim->file_data + addr);
			anim->ip += 2;
		} else {
			return anim_stream_draw(anim, op);
		}
		break;
	}
	return false;
}

unsigned anim_frame_t = 16;

void anim_execute(void)
{
	if (!vm_flag_is_on(FLAG_ANIM_ENABLE))
		return;

	static uint32_t anim_prev_frame_t = 0;
	uint32_t t = vm_get_ticks();
	if (t - anim_prev_frame_t < anim_frame_t)
		return;

	anim_prev_frame_t = t;
	for (int i = 0; i < ANIM_MAX_STREAMS; i++) {
		struct anim_stream *anim = &streams[i];
		if (anim->state == ANIM_STATE_HALTED || anim->state == ANIM_STATE_PAUSED)
			continue;
		anim_stream_execute(anim);
	}
}

bool anim_stream_running(unsigned stream)
{
	assert(stream < ANIM_MAX_STREAMS);
	return streams[stream].state != ANIM_STATE_HALTED;
}

bool anim_running(void)
{
	for (int i = 0; i < ANIM_MAX_STREAMS; i++) {
		if (streams[i].state != ANIM_STATE_HALTED)
			return true;
	}
	return false;
}

void anim_exec_copy_call(unsigned stream)
{
	ANIM_LOG("anim_exec_copy_call(%u)", stream);
	if (unlikely(anim_type != ANIM_A))
		VM_ERROR("Wrong animation type for anim_exec_copy_call");
	if (unlikely(stream >= ANIM_MAX_STREAMS))
		VM_ERROR("Invalid animation stream index: %u", stream);

	// get draw call index
	uint8_t *data = memory.file_data + mem_get_sysvar32(mes_sysvar32_data_offset);
	uint8_t *bytecode = data + le_get32(data, 2 + stream * 4);
	uint16_t no = le_get16(bytecode, 0);
	if (no < 20 || (no - 20) >= le_get16(data, 0)) {
		WARNING("Invalid draw call index: %d", (int)no - 20);
		return;
	}

	uint8_t *call = data + 2 + 100 * 4 + (no - 20) * anim_draw_call_size;
	gfx_copy_masked(le_get16(call, 2), le_get16(call, 4), le_get16(call, 6),
			le_get16(call, 8), 1, le_get16(call, 10), le_get16(call, 12),
			mem_get_sysvar16(mes_sysvar16_dst_surface), get_mask_color());
}

void anim_decompose_draw_call(struct anim_draw_call *call, int *dst_x, int *dst_y, int *w,
		int *h)
{
	switch (call->op) {
	case ANIM_DRAW_OP_COPY:
	case ANIM_DRAW_OP_COPY_MASKED:
	case ANIM_DRAW_OP_SWAP:
		*dst_x = call->copy.dst.x;
		*dst_y = call->copy.dst.y;
		*w = call->copy.dim.w;
		*h = call->copy.dim.h;
		break;
	case ANIM_DRAW_OP_COMPOSE:
		*dst_x = call->compose.dst.x;
		*dst_y = call->compose.dst.y;
		*w = call->compose.dim.w;
		*h = call->compose.dim.h;
		break;
	default:
		VM_ERROR("Unexpected animation draw operation: %u", (unsigned)call->op);
	}
}
