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
#include <ctype.h>

#include "nulib.h"
#include "nulib/vector.h"

#include "cmdline.h"

/*
 * Get a command by name.
 */
static struct cmdline_cmd *get_node(cmdline_t cmdline, const char *name)
{
	for (unsigned i = 0; i < vector_length(cmdline); i++) {
		struct cmdline_cmd *node = &vector_A(cmdline, i);
		if (!strcmp(name, node->fullname))
			return node;
		if (node->shortname && !strcmp(name, node->shortname))
			return node;
	}
	return NULL;
}

static unsigned cmd_name_max(cmdline_t cmdline)
{
	unsigned m = 0;
	for (unsigned i = 0; i < vector_length(cmdline); i++) {
		m = max(m, strlen(vector_A(cmdline, i).fullname));
	}
	return m;
}

#ifdef HAVE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
/*
 * Get a string from the user. The result can be modified but shouldn't be free'd.
 */
static char *cmdline_gets(void)
{
	static char *line_read = NULL;
	free(line_read);
	line_read = readline("dbg(cmd)> ");
	if (line_read && *line_read)
		add_history(line_read);
	return line_read;
}
#else
/*
 * Get a string from the user. The result can be modified but shouldn't be free'd.
 */
static char *cmdline_gets(void)
{
	static char line[1024];

	printf("dbg(cmd)> ");
	fflush(stdout);
	return fgets(line, 1024, stdin);
}
#endif

/*
 * Parse a line into an array of words. Modified the input string.
 */
static char **cmdline_parse(char *line, unsigned *nr_words)
{
	static char *words[32];

	*nr_words = 0;
	while (*line) {
		// skip whitespace
		while (*line && isspace(*line)) line++;
		if (!*line) break;
		// save word ptr
		words[(*nr_words)++] = line;
		// skip word
		while (*line && !isspace(*line)) line++;
		if (!*line) break;
		*line = '\0';
		line++;
	}
	return words;
}

static int execute_command(struct cmdline_cmd *cmd, unsigned nr_args, char **args)
{
	if (nr_args < cmd->min_args || nr_args > cmd->max_args) {
		printf("Wrong number of arguments to '%s' command\n", cmd->fullname);
		return 0;
	}
	return cmd->run(nr_args, args);
}

static int execute_words(cmdline_t cmdline, unsigned nr_words, char **words)
{
	struct cmdline_cmd *node = get_node(cmdline, words[0]);
	if (!node) {
		printf("Invalid command: %s (type 'help' for a list of commands)\n", words[0]);
		return 0;
	}

	if (!node->run) {
		if (nr_words == 1) {
			printf("TODO: module help");
			return 0;
		}
		return execute_words(node->children, nr_words-1, words+1);
	}
	return execute_command(node, nr_words-1, words+1);
}

static int execute_line(cmdline_t cmdline, char *line)
{
	unsigned nr_words;
	char **words = cmdline_parse(line, &nr_words);
	if (!nr_words)
		return 0;

	return execute_words(cmdline, nr_words, words);
}

cmdline_t cmdline_create(struct cmdline_cmd *commands, unsigned nr_commands)
{
	cmdline_t cmdline = vector_initializer;
	for (unsigned i = 0; i < nr_commands; i++) {
		vector_push(struct cmdline_cmd, cmdline, commands[i]);
	}

	return cmdline;
}

void cmdline_free(cmdline_t cmdline)
{
	vector_destroy(cmdline);
}

int cmdline_repl(cmdline_t cmdline)
{
	while (1) {
		char *line = cmdline_gets();
		if (!line)
			continue;
		int r = execute_line(cmdline, line);
		if (r)
			return r;
	}
}

static void cmdline_cmd_help_short(struct cmdline_cmd *cmd, unsigned name_len)
{
	printf("%*s", name_len, cmd->fullname);
	if (cmd->shortname)
		printf(" (%s)%*s", cmd->shortname, 3 - (int)strlen(cmd->shortname), "");
	else
		printf("      ");
	printf(" -- %s\n", cmd->description);
}

static void cmdline_list_help(cmdline_t cmdline)
{
	unsigned name_len = cmd_name_max(cmdline);

	puts("");
	puts("Available Commands");
	puts("==================");
	for (unsigned i = 0; i < vector_length(cmdline); i++) {
		if (vector_A(cmdline, i).run) {
			cmdline_cmd_help_short(&vector_A(cmdline, i), name_len);
		} else {
			printf("%s - command module; run to see additional commands\n",
					vector_A(cmdline, i).fullname);
		}
	}
	puts("");
}

static void cmdline_cmd_help(struct cmdline_cmd *cmd)
{
	if (!cmd->run) {
		cmdline_list_help(cmd->children);
		return;
	}
	printf("%s", cmd->fullname);
	if (cmd->shortname)
		printf(", %s", cmd->shortname);
	if (cmd->arg_description)
		printf(" %s", cmd->arg_description);
	printf("\n\t%s\n", cmd->description);
}

void cmdline_help(cmdline_t cmdline, unsigned nr_args, char **args)
{
	if (!nr_args) {
		cmdline_list_help(cmdline);
		return;
	}
	struct cmdline_cmd *node = get_node(cmdline, args[0]);
	if (!node) {
		printf("ERROR: '%s' is not a command or module", args[0]);
		return;
	}
	if (nr_args == 1) {
		cmdline_cmd_help(node);
		return;
	}
	if (node->run) {
		printf("ERROR: '%s' is not a command module", args[0]);
		return;
	}
	cmdline_help(node->children, nr_args - 1, args + 1);
}
