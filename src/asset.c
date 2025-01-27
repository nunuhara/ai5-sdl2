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
#include "nulib/queue.h"
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
	struct archive *voice2;
	struct archive *voicesub;
	struct archive *effect;
	struct archive *data;
	struct archive *priv;
} arc = {0};

char *asset_mes_name = NULL;
char *asset_cg_name = NULL;
char *asset_bgm_name = NULL;
char *asset_effect_name = NULL;
char *asset_voice_name = NULL;
char *asset_voice2_name = NULL;
char *asset_voicesub_name = NULL;
char *asset_data_name = NULL;

bool asset_effect_is_bgm = true;

static void cg_cache_init(void);

static struct archive *open_arc(const char *name, unsigned flags)
{
	char *path = path_get_icase(name);
	if (!path)
		return NULL;
	struct archive *arc = archive_open(path, flags);
	free(path);
	return arc;
}

void asset_init(void)
{
	unsigned typ_flags = ARCHIVE_RAW;
	unsigned mes_flags = ARCHIVE_CACHE;
	unsigned data_flags = ARCHIVE_CACHE;
	if (!config.mes.mes_type)
		mes_flags |= ARCHIVE_RAW;
	if (!config.data.data_type)
		data_flags |= ARCHIVE_RAW;

#define ARC_OPEN(t, flags, warn) \
	if (config.file.t.arc) { \
		assert(config.file.t.name); \
		if (!(arc.t = open_arc(config.file.t.name, flags))) \
			warn("Failed to open archive \"%s\"", config.file.t.name); \
	}
	ARC_OPEN(bg,       typ_flags,  WARNING);
	ARC_OPEN(mes,      mes_flags,  ERROR);
	ARC_OPEN(bgm,      typ_flags,  WARNING);
	ARC_OPEN(voice,    typ_flags,  WARNING);
	ARC_OPEN(voice2,   typ_flags,  WARNING);
	ARC_OPEN(voicesub, typ_flags,  WARNING);
	ARC_OPEN(effect,   typ_flags,  WARNING);
	ARC_OPEN(data,     data_flags, WARNING);
	ARC_OPEN(priv,     typ_flags,  WARNING);
#undef ARC_OPEN
	cg_cache_init();
}

void asset_fini(void)
{
#define ARC_CLOSE(t) if (arc.t) { archive_close(arc.t); arc.t = NULL; }
	ARC_CLOSE(bg);
	ARC_CLOSE(mes);
	ARC_CLOSE(bgm);
	ARC_CLOSE(voice);
	ARC_CLOSE(voicesub);
	ARC_CLOSE(effect);
	ARC_CLOSE(data);
	ARC_CLOSE(priv);
#undef ARC_CLOSE
}

bool asset_set_voice_archive(const char *name)
{
	if (config.file.voice.arc && !strcasecmp(config.file.voice.name, name))
		return true;

	struct archive *ar = open_arc(name, ARCHIVE_RAW);
	if (!ar) {
		NOTICE("failed to open %s", name);
		return false;
	}
	if (arc.voice)
		archive_close(arc.voice);
	arc.voice = ar;
	config.file.voice.arc = true;
	string_free(config.file.voice.name);
	config.file.voice.name = string_new(name);
	return true;
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

struct cached_cg {
	TAILQ_ENTRY(cached_cg) entry;
	unsigned key;
	const char *name;
	struct cg *cg;
};

#define CG_CACHE_SIZE 16
static struct cached_cg cg_cache_entry[CG_CACHE_SIZE] = {0};
static TAILQ_HEAD(cg_cache_head, cached_cg) cg_cache;
static TAILQ_HEAD(cg_freelist_head, cached_cg) cg_free_list;

static void cg_cache_init(void)
{
	TAILQ_INIT(&cg_cache);
	TAILQ_INIT(&cg_free_list);
	for (int i = 0; i < CG_CACHE_SIZE; i++) {
		TAILQ_INSERT_TAIL(&cg_free_list, &cg_cache_entry[i], entry);
	}
}

struct archive_data *_asset_cg_load(const char *name)
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

static uint64_t cg_name_hash(const char *s)
{
	uint16_t h = (uint64_t)*s;
	if (h) for (++s ; *s; ++s) h = (h << 5) - h + (khint_t)*s;
	return h;
}

static struct cg *cg_cache_get(const char *name, uint64_t key)
{
	for (int i = 0; i < CG_CACHE_SIZE; i++) {
		if (!cg_cache_entry[i].name)
			continue;
		if (cg_cache_entry[i].key == key && !strcasecmp(cg_cache_entry[i].name, name)) {
			// move to front of cache
			TAILQ_REMOVE(&cg_cache, &cg_cache_entry[i], entry);
			TAILQ_INSERT_HEAD(&cg_cache, &cg_cache_entry[i], entry);
			return cg_cache_entry[i].cg;
		}
	}
	return NULL;
}

struct cg *asset_cg_decode(struct archive_data *file)
{
	// check for cached CG
	uint64_t key = cg_name_hash(file->name);
	struct cg *cg = cg_cache_get(file->name, key);
	if (cg) {
		cg->ref++;
		return cg;
	}

	// decode CG
	cg = cg_load_arcdata(file);
	if (!cg)
		return NULL;

	// evict least recently used CG from cache
	struct cached_cg *cached;
	if (TAILQ_EMPTY(&cg_free_list)) {
		cached = TAILQ_LAST(&cg_cache, cg_cache_head);
		TAILQ_REMOVE(&cg_cache, cached, entry);
		cached->key = 0;
		cached->name = NULL;
		cg_free(cached->cg);
	} else {
		cached = TAILQ_FIRST(&cg_free_list);
		TAILQ_REMOVE(&cg_free_list, cached, entry);
	}

	// add CG to cache
	cg->ref++;
	cached->key = key;
	cached->name = file->name;
	cached->cg = cg;
	TAILQ_INSERT_HEAD(&cg_cache, cached, entry);

	return cg;
}

struct cg *asset_cg_load(const char *name)
{
	struct archive_data *data = _asset_cg_load(name);
	if (!data)
		return NULL;
	struct cg *cg = asset_cg_decode(data);
	archive_data_release(data);
	return cg;
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
	if (asset_effect_is_bgm)
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

struct archive_data *asset_voice_load(const char *name)
{
	if (!arc.voice)
		return asset_fs_load(name);
	struct archive_data *file = archive_get(arc.voice, name);
	if (!file)
		file = archive_get(arc.voice2, name);
	if (!file && (!arc.voice2 || !(file = archive_get(arc.voice2, name))))
		return NULL;
	free(asset_voice_name);
	asset_voice_name = xstrdup(name);
	return file;
}

struct archive_data *asset_voicesub_load(const char *name)
{
	if (!arc.voicesub)
		return asset_fs_load(name);
	struct archive_data *file = archive_get(arc.voicesub, name);
	if (!file)
		return NULL;
	free(asset_voice_name);
	asset_voice_name = xstrdup(name);
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
