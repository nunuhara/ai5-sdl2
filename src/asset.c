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

#include "ai5.h"
#include "ai5/arc.h"
#include "ai5/cg.h"

static struct {
	struct archive *bg;
	struct archive *mes;
	struct archive *bgm;
	struct archive *voice;
	struct archive *effect;
	struct archive *data;
	struct archive *priv;
} arc = {0};

void asset_init(void)
{
#define ARC_OPEN(t, warn) \
	if (config.file.t.arc) { \
		assert(config.file.t.name); \
		if (!(arc.t = archive_open(config.file.t.name, 0))) \
			warn("Failed to open archive \"%s\"", config.file.t.name); \
	}
	ARC_OPEN(bg, WARNING);
	ARC_OPEN(mes, ERROR);
	ARC_OPEN(bgm, WARNING);
	ARC_OPEN(voice, WARNING);
	ARC_OPEN(effect, WARNING);
	ARC_OPEN(data, WARNING);
	ARC_OPEN(priv, WARNING);
#undef ARC_OPEN
}

void asset_fini(void)
{
#define ARC_CLOSE(t) if (arc.t) { archive_close(arc.t); arc.t = NULL; }
	ARC_CLOSE(bg);
	ARC_CLOSE(mes);
	ARC_CLOSE(bgm);
	ARC_CLOSE(voice);
	ARC_CLOSE(effect);
	ARC_CLOSE(data);
	ARC_CLOSE(priv);
#undef ARC_CLOSE
}

bool asset_mes_load(const char *name, uint8_t *dst)
{
	if (!arc.mes)
		return false;
	struct archive_data *file = archive_get(arc.mes, name);
	if (!file)
		return false;
	memcpy(dst, file->data, file->size);
	archive_data_release(file);
	return true;
}

struct cg *asset_cg_load(const char *name)
{
	if (!arc.bg)
		return NULL;
	struct archive_data *file = archive_get(arc.bg, name);
	if (!file)
		return NULL;

	struct cg *cg = cg_load_arcdata(file);
	archive_data_release(file);
	return cg;
}

struct archive_data *asset_bgm_load(const char *name)
{
	if (!arc.bgm)
		return NULL;
	return archive_get(arc.bgm, name);
}

struct archive_data *asset_effect_load(const char *name)
{
	if (!arc.effect)
		return NULL;
	return archive_get(arc.effect, name);
}
