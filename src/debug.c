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

#include "nulib.h"
#include "ai5/mes.h"

#include "asset.h"
#include "cmdline.h"
#include "memory.h"
#include "vm.h"

static cmdline_t dbg_cmdline = vector_initializer;
bool dbg_initialized = false;

#define DBG_REPL 0
enum dbg_return_code {
	DBG_CONTINUE = 1,
	DBG_QUIT = 2,
};

static int dbg_cmd_continue(unsigned nr_args, char **args)
{
	return DBG_CONTINUE;
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
	{ "continue", "c", NULL, "Continue running", 0, 0, dbg_cmd_continue },
	{ "help", "h", NULL, "Display debugger help", 0, 2, dbg_cmd_help },
	{ "quit", "q", NULL, "Quit AI5-SDL2", 0, 0, dbg_cmd_quit },
	{ "vm-state", "vm", NULL, "Display current VM state", 0, 0, dbg_cmd_vm_state },
};

static void dbg_init(void)
{
	dbg_cmdline = cmdline_create(dbg_commands, ARRAY_SIZE(dbg_commands));
}

void dbg_repl(void)
{
	if (!dbg_initialized)
		dbg_init();
	switch ((enum dbg_return_code)cmdline_repl(dbg_cmdline)) {
	case DBG_CONTINUE:
		break;
	case DBG_QUIT:
		sys_exit(0);
	}
}
