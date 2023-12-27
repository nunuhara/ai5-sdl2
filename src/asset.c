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

#include "nulib.h"
#include "nulib/file.h"
#include "ai5/arc.h"
#include "ai5/cg.h"

#include "ai5.h"
#include "asset.h"
#include "game.h"

static struct {
	struct archive *bg;
	struct archive *mes;
	struct archive *bgm;
	struct archive *voice;
	struct archive *effect;
	struct archive *data;
	struct archive *priv;
} arc = {0};

char *asset_mes_name = NULL;
char *asset_cg_name = NULL;
char *asset_bgm_name = NULL;
char *asset_effect_name = NULL;
char *asset_data_name = NULL;

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

struct archive_data *asset_fs_load(const char *_name)
{
	// convert to *nix path
	char *name = xstrdup(_name);
	for (char *p = name; *p; p++) {
		if (*p == '\\')
			*p = '/';
	}

	// get case-insensitive path
	char *path = path_get_icase(name);
	// XXX: hack for YU-NO Eng TL: load .ogg file if .wav not found
	if (!path && !strcasecmp(file_extension(name), "wav")) {
		char *ext = (char*)file_extension(name);
		ext[0] = 'o';
		ext[1] = 'g';
		ext[2] = 'g';
		path = path_get_icase(name);
	}
	free(name);
	if (!path)
		return NULL;

	// read data
	size_t size;
	uint8_t *data = file_read(path, &size);
	free(path);
	if (!data)
		return NULL;

	// create fake archive_data
	struct archive_data *file = xcalloc(1, sizeof(struct archive_data));
	file->size = size;
	file->name = "<none>";
	file->data = data;
	file->ref = 1;
	file->allocated = true;
	return file;
}

struct archive_data *asset_mes_load(const char *name)
{
	if (!arc.mes)
		return asset_fs_load(name);
	struct archive_data *file = archive_get(arc.mes, name);
	if (!file)
		return NULL;
	free(asset_mes_name);
	asset_mes_name = xstrdup(name);
	return file;
}

struct archive_data *asset_cg_load(const char *name)
{
	if (!arc.bg)
		return asset_fs_load(name);
	struct archive_data *file = archive_get(arc.bg, name);
	if (!file)
		return NULL;
	free(asset_cg_name);
	asset_cg_name = xstrdup(name);
	return file;
}

struct archive_data *asset_bgm_load(const char *name)
{
	if (!arc.bgm)
		return asset_fs_load(name);
	struct archive_data *file = archive_get(arc.bgm, name);
	if (!file)
		return NULL;
	free(asset_bgm_name);
	asset_bgm_name = xstrdup(name);
	return file;
}

struct archive_data *asset_effect_load(const char *name)
{
	if (!game->use_effect_arc)
		return asset_bgm_load(name);
	if (!arc.effect)
		return asset_fs_load(name);
	struct archive_data *file = archive_get(arc.effect, name);
	if (!file)
		return NULL;
	free(asset_effect_name);
	asset_effect_name = xstrdup(name);
	return file;
}

struct archive_data *asset_data_load(const char *name)
{
	if (!arc.data)
		return asset_fs_load(name);
	struct archive_data *file = archive_get(arc.data, name);
	if (!file)
		return NULL;
	free(asset_data_name);
	asset_data_name = xstrdup(name);
	return file;
}
