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

#ifndef AI5_SDL2_VM_PRIVATE_H
#define AI5_SDL2_VM_PRIVATE_H

#include "vm.h"

#define STRING_PARAM_SIZE 64

struct param {
	enum mes_parameter_type type;
	union {
		char str[STRING_PARAM_SIZE];
		uint32_t val;
	};
};

#define MAX_PARAMS 30

struct param_list {
	struct param params[MAX_PARAMS];
	unsigned nr_params;
};

char *vm_string_param(struct param_list *params, int i);

#define vm_expr_param(params, i) _vm_expr_param(params, i, __func__)
static inline uint32_t _vm_expr_param(struct param_list *params, int i, const char *func) {
	if (i >= params->nr_params) {
		WARNING("Too few parameters at %s", func);
		return 0;
	}
	if (params->params[i].type != MES_PARAM_EXPRESSION)
		VM_ERROR("Expected expression parameter %d / %d", i, params->nr_params);
	return params->params[i].val;
}

void vm_load_data_file(const char *name, uint32_t offset);
void vm_util_set_game(enum ai5_game_id game);
void vm_draw_text(const char *text);

#endif // AI5_SDL2_VM_PRIVATE_H
