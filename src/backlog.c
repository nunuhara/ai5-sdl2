/* Copyright (C) 2024 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

#include <string.h>

#include "nulib.h"
#include "ai5/mes.h"

#include "game.h"
#include "memory.h"
#include "vm.h"

#if 0
#define BACKLOG_LOG(...) NOTICE(__VA_ARGS__)
#else
#define BACKLOG_LOG(...)
#endif

struct backlog_entry {
	unsigned ptr;
	bool present;
	bool has_voice;
};

// XXX: We use a circular buffer where `head == tail` is a valid non-empty state.
//      A boolean is used to distinguish the actual empty state.
static struct backlog_entry backlog[MEMORY_BACKLOG_NR_ENTRIES];
static unsigned backlog_head = 0;
static unsigned backlog_tail = 0;
bool backlog_empty = true;

static inline uint8_t *backlog_data(unsigned no)
{
	return memory.backlog + no * MEMORY_BACKLOG_DATA_SIZE;
}

static unsigned translate_index(unsigned no)
{
	return (backlog_tail + no) % MEMORY_BACKLOG_NR_ENTRIES;
}

void backlog_clear(void)
{
	BACKLOG_LOG("backlog_clear()");
	memset(backlog, 0, sizeof(backlog));
	memset(memory.backlog, 0, sizeof(memory.backlog));
	backlog_head = 0;
	backlog_tail = 0;
	backlog_empty = true;
}

void backlog_prepare(void)
{
	if (!vm_flag_is_on(FLAG_LOG_ENABLE) || vm_flag_is_on(FLAG_LOG_TEXT))
		return;

	BACKLOG_LOG("backlog_prepare()");

	if (backlog_head == backlog_tail && !backlog_empty) {
		backlog_tail = (backlog_tail + 1) % MEMORY_BACKLOG_NR_ENTRIES;
	}

	backlog[backlog_head].ptr = 0;
	backlog[backlog_head].present = false;
	backlog[backlog_head].has_voice = false;
	vm_flag_on(FLAG_LOG_TEXT);
}

void backlog_commit(void)
{
	if (!vm_flag_is_on(FLAG_LOG_ENABLE) || !vm_flag_is_on(FLAG_LOG_TEXT))
		return;

	BACKLOG_LOG("backlog_commit()");
	backlog[backlog_head].present = 1;
	backlog_head = (backlog_head + 1) % MEMORY_BACKLOG_NR_ENTRIES;
	backlog_empty = false;
	vm_flag_off(FLAG_LOG_TEXT);
}

unsigned backlog_count(void)
{
	if (backlog_empty || !backlog[backlog_tail].present)
		return 0;
	unsigned count = 0;
	unsigned i = backlog_tail;
	do {
		count++;
		i = (i + 1) % MEMORY_BACKLOG_NR_ENTRIES;
	} while (i != backlog_tail && backlog[i].present);
	BACKLOG_LOG("backlog_count() -> %u", count);
	return count;
}

uint32_t backlog_get_pointer(unsigned no)
{
	BACKLOG_LOG("backlog_get_pointer(%u)", no);
	if (no >= MEMORY_BACKLOG_NR_ENTRIES) {
		WARNING("Invalid backlog index: %u", no);
		return 0;
	}
	no = translate_index(no);
	if (!backlog[no].present)
		return 0;
	return offsetof(struct memory, backlog) + no * MEMORY_BACKLOG_DATA_SIZE;
}

bool backlog_has_voice(unsigned no)
{
	BACKLOG_LOG("backlog_has_voice(%u)", no);
	if (no >= MEMORY_BACKLOG_NR_ENTRIES) {
		WARNING("Invalid backlog index: %u", no);
		return 0;
	}
	no = translate_index(no);
	if (!backlog[no].present)
		return 0;
	return backlog[no].has_voice;
}

void backlog_set_has_voice(void)
{
	BACKLOG_LOG("backlog_set_has_voice()");
	backlog[backlog_head].has_voice = true;
}

void backlog_push_byte(uint8_t b)
{
	struct backlog_entry *e = &backlog[backlog_head];
	if (e->ptr >= MEMORY_BACKLOG_DATA_SIZE)
		VM_ERROR("Backlog buffer overflow");

	uint8_t *data = backlog_data(backlog_head);
	data[e->ptr++] = b;
	data[e->ptr] = 0;
}
