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

#ifndef AI5_SAVEDATA_H
#define AI5_SAVEDATA_H

void savedata_resume_load(const char *save_name);
void savedata_resume_save(const char *save_name);
void savedata_load(const char *save_name);
void savedata_save(const char *save_name);
void savedata_load_var4(const char *save_name);
void savedata_save_var4(const char *save_name);
void savedata_save_union_var4(const char *save_name);
void savedata_load_var4_slice(const char *save_name, unsigned from, unsigned to);
void savedata_save_var4_slice(const char *save_name, unsigned from, unsigned to);
void savedata_copy(const char *src_save, const char *dst_save);
void savedata_f11(const char *save_name);
void savedata_stash_name(void);
void savedata_f12(const char *save_name);
void savedata_set_mes_name(const char *save_name, const char *mes_name);

#endif // AI5_SAVEDATA_H
