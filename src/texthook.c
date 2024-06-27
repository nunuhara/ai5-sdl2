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

#include <string.h>
#include <SDL.h>

#include "nulib.h"
#include "nulib/string.h"
#include "nulib/utfsjis.h"
#include "ai5.h"
#include "ai5/mes.h"

#include "memory.h"
#include "texthook.h"

#define TEXTHOOK_BUF_SIZE 512
static char texthook_buf[TEXTHOOK_BUF_SIZE] = {0};
static unsigned texthook_buf_ptr = 0;
bool texthook_buffered = true;

uint16_t prev_x = 0;
uint16_t prev_y = 0;

void texthook_commit(void)
{
	if (!config.texthook_clipboard && !config.texthook_stdout)
		return;
	if (!texthook_buf_ptr)
		return;

	string utf8 = sjis_cstring_to_utf8(texthook_buf, texthook_buf_ptr);
	utf8 = string_concat_cstring(utf8, "\n");
	if (config.texthook_clipboard)
		SDL_SetClipboardText(utf8);
	if (config.texthook_stdout)
		NOTICE("%s", utf8);
	string_free(utf8);
	texthook_buf_ptr = 0;
	texthook_buf[0] = '\0';
}

void texthook_push(const char *text)
{
	if (!config.texthook_clipboard && !config.texthook_stdout)
		return;

	// commit if drawing text to a new location
	uint16_t cur_x = mem_get_sysvar16(mes_sysvar16_text_start_x);
	uint16_t cur_y = mem_get_sysvar16(mes_sysvar16_text_start_y);
	if (cur_x != prev_x || cur_y != prev_y) {
		prev_x = cur_x;
		prev_y = cur_y;
		texthook_commit();
	}

	size_t len = strlen(text);
	if (len + 1 >= TEXTHOOK_BUF_SIZE) {
		WARNING("Text exceeded texthook buffer size (size=%u)", (unsigned)len);
		return;
	}
	if (texthook_buf_ptr + len + 1 >= TEXTHOOK_BUF_SIZE)
		texthook_commit();

	memcpy(texthook_buf + texthook_buf_ptr, text, len + 1);
	texthook_buf_ptr += len;

	if (!texthook_buffered)
		texthook_commit();
}

void texthook_set_buffered(bool buffered)
{
	texthook_buffered = buffered;
}
