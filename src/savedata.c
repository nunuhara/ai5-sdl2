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

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "nulib.h"
#include "nulib/file.h"
#include "nulib/little_endian.h"

#include "game.h"
#include "memory.h"
#include "savedata.h"
#include "vm.h"

static uint32_t savedata_size(void)
{
	return game->mem16_size;
}

static void close_save(FILE *f)
{
	if (fclose(f))
		WARNING("fclose: %s", strerror(errno));
}

// XXX: Save files should be shipped with the game, but if not we create them.
static void create_save(const char *save_name)
{
	FILE *f = file_open_utf8(save_name, "wb");
	if (!f) {
		WARNING("fopen: %s", strerror(errno));
		return;
	}
	uint8_t buf[MEMORY_MEM16_MAX_SIZE];
	memset(buf, 0, savedata_size());
	if (fwrite(buf, savedata_size(), 1, f) != 1)
		WARNING("fwrite: %s", strerror(errno));
	close_save(f);
}

static FILE *open_save(const char *save_name, const char *mode)
{
	char *path = path_get_icase(save_name);
	if (!path) {
		create_save(save_name);
		path = strdup(save_name);
		WARNING("Save file \"%s\" doesn't exist", save_name);
	}
	FILE *f = file_open_utf8(path, mode);
	free(path);
	return f;
}

void savedata_read(const char *save_name, uint8_t *buf, uint32_t off, size_t size)
{
	FILE *f = open_save(save_name, "rb");
	if (!f) {
		WARNING("Failed to open save file \"%s\": %s", save_name, strerror(errno));
		return;
	}
	if (off && fseek(f, off, SEEK_SET) < 0)
		WARNING("fseek: %s", strerror(errno));
	if (fread(buf + off, size, 1, f) != 1)
		WARNING("fread: %s", strerror(errno));
	close_save(f);
}

void savedata_write(const char *save_name, const uint8_t *buf, uint32_t off, size_t size)
{
	// XXX: we open r+b (O_RDWR) because it's the only write mode that doesn't
	//      truncate or append
	FILE *f = open_save(save_name, "r+b");
	if (!f) {
		WARNING("Failed to open save file \"%s\": %s", save_name, strerror(errno));
		return;
	}
	if (off && fseek(f, off, SEEK_SET) < 0)
		WARNING("fseek: %s", strerror(errno));
	if (fwrite(buf + off, size, 1, f) != 1)
		WARNING("fwrite: %s", strerror(errno));
	close_save(f);
}

void savedata_resume_load(const char *save_name)
{
	savedata_read(save_name, memory_raw, 0, savedata_size());
	game->mem_restore();
	vm_load_mes(mem_mes_name());
	vm_flag_on(FLAG_RETURN);
}

void savedata_resume_save(const char *save_name)
{
	savedata_write(save_name, memory_raw, 0, savedata_size());
}

void savedata_load(const char *save_name)
{
	// load except mes name
	savedata_read(save_name, memory_raw, MEMORY_MES_NAME_SIZE,
			savedata_size() - MEMORY_MES_NAME_SIZE);
	game->mem_restore();
}

void savedata_save(const char *save_name)
{
	// save except mes name
	savedata_write(save_name, memory_raw, MEMORY_MES_NAME_SIZE,
			savedata_size() - MEMORY_MES_NAME_SIZE);
}

void savedata_load_var4(const char *save_name)
{
	savedata_read(save_name, memory_raw, MEMORY_VAR4_OFFSET, game->var4_size);
}

void savedata_save_var4(const char *save_name)
{
	savedata_write(save_name, memory_raw, MEMORY_VAR4_OFFSET, game->var4_size);
}

void savedata_save_union_var4(const char *save_name)
{
	uint8_t buf[MEMORY_VAR4_MAX_SIZE];
	unsigned var4_size = game->var4_size;
	FILE *f = open_save(save_name, "r+b");
	if (!f) {
		WARNING("Failed to open save file \"%s\": %s", save_name, strerror(errno));
		return;
	}
	if (fseek(f, MEMORY_VAR4_OFFSET, SEEK_SET) < 0) {
		WARNING("fseek: %s", strerror(errno));
		goto end;
	}
	if (fread(buf, var4_size, 1, f) != 1) {
		WARNING("fread: %s", strerror(errno));
		goto end;
	}

	uint8_t *var4 = mem_var4();
	for (unsigned i = 0; i < var4_size; i++) {
		buf[i] |= var4[i];
	}

	if (fseek(f, MEMORY_VAR4_OFFSET, SEEK_SET) < 0) {
		WARNING("fseek: %s", strerror(errno));
		goto end;
	}
	if (fwrite(buf, var4_size, 1, f) != 1)
		WARNING("fwrite: %s", strerror(errno));
end:
	close_save(f);
}

void savedata_load_var4_slice(const char *save_name, unsigned from, unsigned to)
{
	if (from > to)
		return;
	savedata_read(save_name, memory_raw, MEMORY_VAR4_OFFSET + from,
			(to + 1) - from);
}

void savedata_save_var4_slice(const char *save_name, unsigned from, unsigned to)
{
	if (from > to)
		return;
	savedata_write(save_name, memory_raw, MEMORY_VAR4_OFFSET + from,
			(to + 1) - from);
}

void savedata_copy(const char *src_save, const char *dst_save)
{
	uint8_t buf[MEMORY_MEM16_MAX_SIZE];
	savedata_read(src_save, buf, 0, savedata_size());
	savedata_write(dst_save, buf, 0, savedata_size());
}

void savedata_f11(const char *save_name)
{
	uint8_t buf[MEMORY_MEM16_MAX_SIZE];
	uint8_t *load_var4 = buf + MEMORY_MES_NAME_SIZE;
	uint8_t *cur_var4 = mem_var4();
	savedata_read(save_name, buf, 0, MEMORY_VAR4_OFFSET + game->var4_size);
	memcpy(memory_raw, buf, MEMORY_MES_NAME_SIZE);
	cur_var4[18] = load_var4[18];
	cur_var4[21] = load_var4[21];
	cur_var4[29] = load_var4[29];
	for (int i = 50; i < 90; i++) {
		cur_var4[i] = load_var4[i];
	}
	for (int i = 96; i < 2000; i++) {
		cur_var4[i] = load_var4[i];
	}
	game->mem_restore();
	vm_load_mes(mem_mes_name());
	vm_flag_on(FLAG_RETURN);
}

static uint8_t stashed_mes_name[MEMORY_MES_NAME_SIZE];

void savedata_stash_name(void)
{
	memcpy(stashed_mes_name, memory_raw, MEMORY_MES_NAME_SIZE);
}

void savedata_f12(const char *save_name)
{
	uint8_t buf[MEMORY_MEM16_MAX_SIZE];
	uint8_t *out_var4 = buf + MEMORY_MES_NAME_SIZE;
	uint8_t *cur_var4 = mem_var4();
	savedata_read(save_name, buf, 0, MEMORY_VAR4_OFFSET + game->var4_size);
	memcpy(buf, stashed_mes_name, MEMORY_MES_NAME_SIZE);
	for (int i = 50; i < 90; i++) {
		out_var4[i] = cur_var4[i];
	}
	for (int i = 96; i < 2000; i++) {
		out_var4[i] = cur_var4[i];
	}
	savedata_write(save_name, buf, 0, MEMORY_VAR4_OFFSET + game->var4_size);
}

void savedata_set_mes_name(const char *save_name, const char *mes_name)
{
	savedata_write(save_name, (const uint8_t*)mes_name, 0, strlen(mes_name) + 1);
}
