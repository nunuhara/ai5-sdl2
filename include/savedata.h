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

/*
 * Read bytes from a save file into a buffer.
 * `off` is both a read offset into the file, and a write offset into the buffer.
 */
void savedata_read(const char *save_name, uint8_t *buf, uint32_t off, size_t size);

/*
 * Write bytes from a buffer into a save file.
 * `off` is both a read offset into the buffer, and a write offset into the file.
 */
void savedata_write(const char *save_name, const uint8_t *buf, uint32_t off, size_t size);

void savedata_resume_load(const char *save_name);
void savedata_resume_save(const char *save_name);
void savedata_load(const char *save_name);
void savedata_save(const char *save_name);
void savedata_load_var4(const char *save_name, unsigned var4_size);
void savedata_save_var4(const char *save_name, unsigned var4_size);
void savedata_save_union_var4(const char *save_name, unsigned var4_size);
void savedata_load_var4_slice(const char *save_name, unsigned from, unsigned to);
void savedata_save_var4_slice(const char *save_name, unsigned from, unsigned to);
void savedata_copy(const char *src_save, const char *dst_save);
void savedata_set_mes_name(const char *save_name, const char *mes_name);

#endif // AI5_SAVEDATA_H
