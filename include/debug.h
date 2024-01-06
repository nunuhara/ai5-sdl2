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

#ifndef AI5_DEBUG_H
#define AI5_DEBUG_H

#include <stdint.h>
#include <stddef.h>

#define MES_STMT_BREAKPOINT 0xFF

void dbg_repl(void);
void dbg_invalidate(uint32_t addr, size_t size);
void dbg_load_file(const char *name, uint32_t addr, size_t size);
uint8_t dbg_handle_breakpoint(uint32_t addr);

#endif // AI5_DEBUG_H
