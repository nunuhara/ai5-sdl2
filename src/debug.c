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

#include <stdio.h>
#include <string.h>

#include "nulib.h"
#include "nulib/file.h"
#include "nulib/queue.h"
#include "ai5/mes.h"

#include "asset.h"
#include "cmdline.h"
#include "debug.h"
#include "memory.h"
#include "vm.h"

#if 0
#define DBG_LOG(...) NOTICE(__VA_ARGS__)
#else
#define DBG_LOG(...)
#endif

static cmdline_t dbg_cmdline = vector_initializer;
static bool dbg_initialized = false;

#define DBG_REPL 0
enum dbg_return_code {
	DBG_CONTINUE = 1,
	DBG_QUIT = 2,
};

enum file_type {
	FILE_A,
	FILE_A6,
	FILE_MES,
	FILE_S4,
	FILE_OTHER,
};

struct memfile {
	TAILQ_ENTRY(memfile) entry;
	enum file_type type;
	char *filename;
	uint32_t start;
	uint32_t end;
};

TAILQ_HEAD(, memfile) memory_map = TAILQ_HEAD_INITIALIZER(memory_map);

#define ADDR_NOT_LOADED 0xffffffff

struct breakpoint {
	char *filename;
	uint32_t mes_addr;
	uint32_t load_addr;
	uint8_t restore_op;
	void (*cb)(struct breakpoint*);
	void *data;
};

static vector_t(struct breakpoint) breakpoints;
static struct breakpoint *current_breakpoint = NULL;

static void set_breakpoint(struct breakpoint *bp, uint32_t load_addr)
{
	if (bp->load_addr != ADDR_NOT_LOADED)
		VM_ERROR("breakpoint at %s:0x%08x loaded twice", bp->filename, bp->mes_addr);
	bp->load_addr = load_addr;
	bp->restore_op = memory_raw[load_addr + bp->mes_addr];
	memory_raw[load_addr + bp->mes_addr] = MES_STMT_BREAKPOINT;
}

static void dbg_set_breakpoint(const char *filename, uint32_t addr,
		void (*cb)(struct breakpoint*), void *data)
{
	struct breakpoint bp = {
		.filename = strdup(filename),
		.mes_addr = addr,
		.load_addr = ADDR_NOT_LOADED,
		.cb = cb,
		.data = data
	};

	struct memfile *mf;
	TAILQ_FOREACH(mf, &memory_map, entry) {
		if (!strcasecmp(mf->filename, filename))
			set_breakpoint(&bp, mf->start);
	}

	vector_push(struct breakpoint, breakpoints, bp);
	printf("Set breakpoint at %s:0x%08x\n", filename, addr);
}

static void clear_breakpoint(struct breakpoint *bp)
{
	if (bp->load_addr != ADDR_NOT_LOADED)
		memory_raw[bp->load_addr + bp->mes_addr] = bp->restore_op;
}

static void dbg_clear_breakpoint(const char *filename, uint32_t addr)
{
	struct breakpoint *bp;
	vector_foreach_p(bp, breakpoints) {
		if (bp->mes_addr == addr && !strcasecmp(bp->filename, filename)) {
			clear_breakpoint(bp);
			// FIXME: reuse/delete zombie entry
			bp->filename[0] = '\0';
			printf("Cleared breakpoint at %s:0x%08x\n", filename, addr);
			return;
		}
	}
	printf("No breakpoint set at %s:0x%08x\n", filename, addr);
}

/*
 * Parse a string of the form "file:address", e.g. "START.MES:0xF00".
 */
static char *parse_breakpoint(char *str, uint32_t *addr_out)
{
	char *p = strchr(str, ':');
	if (!p) {
		printf("Invalid breakpoint: %s\n", str);
		return NULL;
	}

	char *endptr;
	long i = strtol(p+1, &endptr, 0);
	if (!p[1] || *endptr || i < 0) {
		printf("Invalid address: %s\n", p+1);
		return NULL;
	}

	*p = '\0';
	*addr_out = i;
	return str;
}

static int dbg_cmd_breakpoint(unsigned nr_args, char **args)
{
	uint32_t addr;
	char *file = parse_breakpoint(args[0], &addr);
	if (file)
		dbg_set_breakpoint(file, addr, NULL, NULL);
	return DBG_REPL;
}

static int dbg_cmd_clear(unsigned nr_args, char **args)
{
	uint32_t addr;
	char *file = parse_breakpoint(args[0], &addr);
	if (file)
		dbg_clear_breakpoint(file, addr);
	return DBG_REPL;
}

static int dbg_cmd_continue(unsigned nr_args, char **args)
{
	return DBG_CONTINUE;
}

static int dbg_cmd_map(unsigned nr_args, char **args)
{
#define ENTRY(start, size, name) \
	printf("%08x -> %08x: %s\n", (unsigned)(start), (unsigned)((start)+(size)), name)
#define MEM_ENTRY(field, name) \
	ENTRY(offsetof(struct memory, field), sizeof(memory.field), name)
	struct memfile *mf;
	ENTRY(0, game->mem16_size, "[MEM16]");
	TAILQ_FOREACH(mf, &memory_map, entry) {
		ENTRY(mf->start, mf->end - mf->start, mf->filename);
	}
	if (game->bpp == 8)
		MEM_ENTRY(palette, "[PALETTE]");
	MEM_ENTRY(menu_entry_addresses, "[MENU ENTRY ADDRESSES]");
	MEM_ENTRY(menu_entry_numbers, "[MENU ENTRY NUMBERS]");
	return DBG_REPL;
#undef MEM_ENTRY
#undef ENTRY
}

static int dbg_cmd_quit(unsigned nr_args, char **args)
{
	return DBG_QUIT;
}

static int dbg_cmd_vm_state(unsigned nr_args, char **args)
{
	printf("\n%s @ %08x\n", asset_mes_name, vm.ip.ptr);
	for (int i = vm.mes_call_stack_ptr - 1; i >= 0; i--) {
		printf("%s @ %08x\n", vm.mes_call_stack[i].mes_name, vm.mes_call_stack[i].ip.ptr);
	}
	putchar('\n');

	for (int i = 0; i < 26; i++) {
		printf("var16[%02d] = %04x\tvar32[%02d] = %08x\n",
				i, mem_get_var16(i), i, mem_get_var32(i));
	}
	putchar('\n');

	for (int i = 0; i < 26; i++) {
		enum mes_system_var16 v = mes_code_tables.int_to_sysvar16[i];
		if (v == MES_CODE_INVALID)
			continue;
		printf("System.%s = %04x\n", mes_system_var16_names[v], mem_get_sysvar16(v));
	}
	for (int i = 0; i < 26; i++) {
		enum mes_system_var32 v = mes_code_tables.int_to_sysvar32[i];
		if (v == MES_CODE_INVALID)
			continue;
		printf("System.%s = %08x\n", mes_system_var32_names[v], mem_get_sysvar32(v));
	}
	putchar('\n');

	return DBG_REPL;
}

static int dbg_cmd_help(unsigned nr_args, char **args)
{
	cmdline_help(dbg_cmdline, nr_args, args);
	return DBG_REPL;
}

static struct cmdline_cmd dbg_commands[] = {
	{ "breakpoint", "b", "<file:address>", "Set breakpoint", 1, 1, dbg_cmd_breakpoint },
	{ "clear", NULL, "<file:address>", "Clear breakpoint", 1, 1, dbg_cmd_clear },
	{ "continue", "c", NULL, "Continue running", 0, 0, dbg_cmd_continue },
	{ "help", "h", NULL, "Display debugger help", 0, 2, dbg_cmd_help },
	{ "map", NULL, NULL, "Display memory map", 0, 0, dbg_cmd_map },
	{ "quit", "q", NULL, "Quit AI5-SDL2", 0, 0, dbg_cmd_quit },
	{ "vm-state", "vm", NULL, "Display current VM state", 0, 0, dbg_cmd_vm_state },
};

static void dbg_fini(void)
{
	cmdline_free(dbg_cmdline);
}

static void dbg_init(void)
{
	dbg_cmdline = cmdline_create(dbg_commands, ARRAY_SIZE(dbg_commands));
	atexit(dbg_fini);
	dbg_initialized = true;
}

void dbg_repl(void)
{
	if (!dbg_initialized)
		dbg_init();
	switch ((enum dbg_return_code)cmdline_repl(dbg_cmdline, "dbg> ")) {
	case DBG_CONTINUE:
		break;
	case DBG_QUIT:
		sys_exit(0);
	}
}

static enum file_type filename_to_type(const char *filename)
{
	if (!strcasecmp(file_extension(filename), "A"))
		return FILE_A;
	if (!strcasecmp(file_extension(filename), "A6"))
		return FILE_A6;
	if (!strcasecmp(file_extension(filename), "MES"))
		return FILE_MES;
	if (!strcasecmp(file_extension(filename), "S4"))
		return FILE_S4;
	return FILE_OTHER;
}

static void dbg_unmap(struct memfile *f)
{
	DBG_LOG("dbg_unmap %08x -> %08x: %s", f->start, f->end, f->filename);
	struct breakpoint *bp;
	vector_foreach_p(bp, breakpoints) {
		if (bp->load_addr == ADDR_NOT_LOADED)
			continue;
		uint32_t addr = bp->load_addr + bp->mes_addr;
		if (addr >= f->start && addr < f->end) {
			bp->load_addr = ADDR_NOT_LOADED;
		}
	}
}

static void dbg_map(struct memfile *f)
{
	DBG_LOG("dbg_map   %08x -> %08x: %s", f->start, f->end, f->filename);
	struct breakpoint *bp;
	vector_foreach_p(bp, breakpoints) {
		if (!strcasecmp(f->filename, bp->filename))
			set_breakpoint(bp, f->start);
	}
}

/*
 * Called before dbg_load_file to invalidate a region of the memory map.
 * This should be called *before* new file data is written to memory. E.g.
 *
 *   dbg_invalidate(...);
 *   memcpy(...);
 *   dbg_load_file(...);
 */
void dbg_invalidate(uint32_t start, size_t size)
{
	uint32_t end = start + size;
	for (struct memfile *iter = memory_map.tqh_first; iter;) {
		if (iter->end <= start) {
			iter = iter->entry.tqe_next;
			continue;
		}
		if (end <= iter->start)
			return;
		// XXX: any amount of overlap invalidates region
		struct memfile *next = iter->entry.tqe_next;
		TAILQ_REMOVE(&memory_map, iter, entry);
		dbg_unmap(iter);
		free(iter->filename);
		free(iter);
		iter = next;
	}
}

/*
 * Add a new file to the memory map. dbg_invalidate should be called first to
 * remove overlapping entries.
 * This should be called *after* new file data is written to memory. E.g.
 *
 *   dbg_invalidate(...);
 *   memcpy(...);
 *   dbg_load_file(...);
 */
void dbg_load_file(const char *filename, uint32_t start, size_t size)
{
	uint32_t end = start + size;
	struct memfile *mf = xcalloc(1, sizeof(struct memfile));
	mf->type = filename_to_type(filename);
	mf->filename = xstrdup(filename);
	mf->start = start;
	mf->end = end;

	struct memfile *iter;
	TAILQ_FOREACH(iter, &memory_map, entry) {
		if (iter->end <= start)
			continue;
		if (end <= iter->start) {
			TAILQ_INSERT_BEFORE(iter, mf, entry);
			dbg_map(mf);
			return;
		}
		// XXX: dbg_invalidate wasn't called
		assert(false);
	}

	TAILQ_INSERT_TAIL(&memory_map, mf, entry);
	dbg_map(mf);
}

static uint8_t handle_breakpoint(struct breakpoint *bp)
{
	current_breakpoint = bp;
	if (bp->cb) {
		bp->cb(bp);
	} else {
		printf("Hit breakpoint at %s:%08x. Entering REPL...\n",
				bp->filename, bp->load_addr + bp->mes_addr);
		dbg_repl();
	}
	current_breakpoint = NULL;
	return bp->restore_op;
}

uint8_t dbg_handle_breakpoint(uint32_t addr)
{
	struct breakpoint *bp;
	vector_foreach_p(bp, breakpoints) {
		if (bp->load_addr + bp->mes_addr == addr) {
			return handle_breakpoint(bp);
		}
	}
	VM_ERROR("Hit breakpoint at %08x, but couldn't locate entry in breakpoint table", addr);
}
