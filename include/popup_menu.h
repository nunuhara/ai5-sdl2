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

#ifndef AI5_POPUP_MENU_H
#define AI5_POPUP_MENU_H

struct menu;

struct menu *popup_menu_new(void);
void popup_menu_free(struct menu *menu);
int popup_menu_append_entry(struct menu *m, int icon_no, const char *label,
		const char *hotkey, void(*on_click)(void*), void *data);
int popup_menu_append_radio_group(struct menu *m, const char **labels, int nr_labels,
		int dflt, void(*on_click)(int,void*), void *data);
int popup_menu_append_separator(struct menu *m);
int popup_menu_append_submenu(struct menu *m, int icon_no, const char *label,
		struct menu *submenu);
void popup_menu_set_active(struct menu *m, int entry_id, bool active);
void popup_menu_run(struct menu *m, int x, int y);

#endif // AI5_POPUP_MENU_H
