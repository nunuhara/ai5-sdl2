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
#include <math.h>
#include <SDL_mixer.h>

#include "nulib.h"
#include "ai5/arc.h"

#include "asset.h"

static Mix_Chunk *bgm = NULL;
static Mix_Chunk *se = NULL;

void audio_fini(void)
{
	Mix_CloseAudio();
	Mix_Quit();
}

void audio_init(void)
{
	Mix_Init(0);
	if (Mix_OpenAudio(44100, AUDIO_S16LSB, 2, 2048) < 0) {
		ERROR("Mix_OpenAudio");
	}
	atexit(audio_fini);
}

void audio_bgm_stop(void)
{
	Mix_HaltChannel(0);
	if (bgm) {
		Mix_FreeChunk(bgm);
		bgm = NULL;
	}
}

void audio_bgm_play(const char *name)
{
	audio_bgm_stop();
	struct archive_data *data = asset_bgm_load(name);
	if (!data) {
		WARNING("Failed to load BGM \"%s\"", name);
		return;
	}
	bgm = Mix_LoadWAV_RW(SDL_RWFromConstMem(data->data, data->size), 1);
	archive_data_release(data);
	if (!bgm) {
		WARNING("Failed to decode BGM \"%s\": %s", name, SDL_GetError());
		return;
	}
	Mix_PlayChannel(0, bgm, -1);
}

// XXX: Volume is a value in the range [0,31], which corresponds to the range
//      [-5000,0] in increments of 156 (volume in dB as expected by
//      DirectSound). We convert this value to a linear volume scale.
void audio_bgm_set_volume(uint8_t vol)
{
	int directsound_volume = -5000 + vol * 156;
	int linear_volume = 128;
	if (vol < 31) {
		float v = powf(10.f, (float)directsound_volume / 2000.f);
		linear_volume = floorf(v * MIX_MAX_VOLUME + 0.5f);
	};
	Mix_Volume(0, linear_volume);
}

void audio_bgm_fade(uint32_t uk, uint8_t vol, bool stop, bool sync)
{
	// FIXME: implement fade (as usual, SDL_mixer makes this difficult...)
	if (stop)
		audio_bgm_stop();
	else
		audio_bgm_set_volume(vol);
}

void audio_se_stop(uint32_t uk)
{
	Mix_HaltChannel(1);
	if (se) {
		Mix_FreeChunk(se);
		se = NULL;
	}
}

void audio_se_play(const char *name, uint32_t uk)
{
	audio_se_stop(uk);
	struct archive_data *data = asset_effect_load(name);
	if (!data) {
		WARNING("Failed to load sound effect \"%s\"", name);
		return;
	}
	se = Mix_LoadWAV_RW(SDL_RWFromConstMem(data->data, data->size), 1);
	archive_data_release(data);
	if (!se) {
		WARNING("Failed to decode sound effect \"%s\": %s", name, SDL_GetError());
		return;
	}
	// FIXME: some effects loop (but SDL_mixer only gives loop info for music...)
	Mix_PlayChannel(1, se, 0);
}
