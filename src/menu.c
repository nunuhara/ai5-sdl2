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

#include <string.h>

#include "nulib.h"

#include "input.h"
#include "memory.h"
#include "menu.h"
#include "vm.h"

static bool menu_initialized = false;
static int menu_index = 0;

// table mapping menu numbers (defmenu argument) to address indices
static unsigned menu_no_to_index_table[MEMORY_MENU_ENTRY_MAX] = {0};

static unsigned count_entries(void)
{
	for (unsigned i = 0; i < MEMORY_MENU_ENTRY_MAX; i++) {
		if (!memory.menu_entry_addresses[i])
			return i;
	}
	return MEMORY_MENU_ENTRY_MAX;
}

void menu_define(unsigned menu_no, bool empty)
{
	if (!menu_initialized) {
		memset(memory.menu_entry_addresses, 0, sizeof(memory.menu_entry_addresses));
		memset(menu_no_to_index_table, 0xff, sizeof(menu_no_to_index_table));
		menu_index = 0;
		menu_initialized = true;
	}

	// XXX: if menu entry is empty, nothing is written to menu_entry_numbers
	//      or menu_entry_addresses
	if (empty)
		return;

	if (menu_index >= MEMORY_MENU_ENTRY_MAX)
		VM_ERROR("Too many menu entries");

	// compute virtual address of current IP (will be farcall'd from bytecode)
	uint8_t *ip = vm.ip.code + vm.ip.ptr;
	assert(ip >= memory_raw && ip < memory_raw + sizeof(struct memory));

	// XXX: menu_nos are written to menu_entry_numbers sequentially, regardless
	//      of the current contents
	memory.menu_entry_numbers[menu_index++] = menu_no;

	// XXX: addresses are pushed to the first free slot in menu_entry_addresses
	unsigned i = count_entries();
	if (i >= MEMORY_MENU_ENTRY_MAX)
		VM_ERROR("Too many menu entries");
	memory.menu_entry_addresses[i] = ip - memory_raw;

	// keep track of which menu_no corresponds to which address index
	// (for menu_get_no)
	menu_no_to_index_table[menu_no] = i;
}

void menu_exec(void)
{
	for (int i = 32; i < 40; i++) {
		if (!vm.procedures[i].code)
			VM_ERROR("Procedure %d is undefined in menuexec", i);
	}

	mem_set_sysvar16(mes_sysvar16_nr_menu_entries, count_entries());

	// initialize menu
	vm_call_procedure(38);
	while (true) {
		if (vm_flag_is_on(FLAG_MENU_RETURN))
			break;
		// update menu
		vm_call_procedure(39);
		if (input_down(INPUT_ACTIVATE)) {
			vm_call_procedure(32);
			input_wait_until_up(INPUT_ACTIVATE);
		} else if (input_down(INPUT_CANCEL)) {
			vm_call_procedure(33);
			input_wait_until_up(INPUT_CANCEL);
		} else if (input_down(INPUT_UP)) {
			vm_call_procedure(34);
			input_wait_until_up(INPUT_UP);
		} else if (input_down(INPUT_DOWN)) {
			vm_call_procedure(35);
			input_wait_until_up(INPUT_DOWN);
		} else if (input_down(INPUT_LEFT)) {
			vm_call_procedure(36);
			input_wait_until_up(INPUT_LEFT);
		} else if (input_down(INPUT_RIGHT)) {
			vm_call_procedure(37);
			input_wait_until_up(INPUT_RIGHT);
		} else {
			vm_delay(16);
		}
	}
	menu_initialized = false;
}

/* Common code pattern ("selected" = var16[18]):
 *     System.get_menu_no(selected); // puts menu_no of selected index into System.var16[22]
 *     selected = System.var16[22] + 1; // set selected index to menu_no + 1
 */
void menu_get_no(unsigned index)
{
	for (int no = 0; no < MEMORY_MENU_ENTRY_MAX; no++) {
		if (menu_no_to_index_table[no] == index) {
			mem_set_sysvar16(mes_sysvar16_menu_no, no);
			return;
		}
	}
	mem_set_sysvar16(mes_sysvar16_menu_no, 200);
}
