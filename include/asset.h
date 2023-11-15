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

#ifndef AI5_ASSET_H
#define AI5_ASSET_H

#include <stdbool.h>

struct cg;

enum asset_type {
	ASSET_BG,
	ASSET_MES,
	ASSET_BGM,
	ASSET_VOICE,
	ASSET_EFFECT,
	ASSET_DATA,
	ASSET_PRIV,
};

void asset_init(void);
void asset_fini(void);

bool asset_mes_load(const char *name, uint8_t *dst);
struct cg *asset_cg_load(const char *name);

#endif // AI5_ASSET_H
