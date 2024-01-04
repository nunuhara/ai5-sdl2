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

#ifndef AI5_CMDLINE_H
#define AI5_CMDLINE_H

#include "nulib/vector.h"

typedef vector_t(struct cmdline_cmd) cmdline_t;

struct cmdline_cmd {
	const char *fullname;
	const char *shortname;
	const char *arg_description;
	const char *description;
	unsigned min_args;
	unsigned max_args;
	int (*run)(unsigned nr_args, char **args);
	cmdline_t children;
};

cmdline_t cmdline_create(struct cmdline_cmd *commands, unsigned nr_commands);
void cmdline_free(cmdline_t cmdline);

int cmdline_repl(cmdline_t cmdline);
void cmdline_help(cmdline_t cmdline, unsigned nr_args, char **args);

#endif // AI5_CMDLINE_H
