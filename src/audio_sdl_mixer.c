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
#include "audio.h"
#include "mixer.h"
#include "vm.h"

#if 0
#define AUDIO_LOG(...) NOTICE(__VA_ARGS__)
#else
#define AUDIO_LOG(...)
#endif

static Mix_Chunk *bgm = NULL;
static Mix_Chunk *se = NULL;
static char *bgm_name = NULL;
static uint8_t bgm_volume = 31;

struct {
	bool fading;
	uint32_t start_t;
	uint32_t ms;
	int start_vol;
	int end_vol;
	bool stop;
} fade = {0};

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

static void _audio_bgm_stop(void)
{
	fade.fading = false;
	Mix_HaltChannel(0);
	if (bgm) {
		Mix_FreeChunk(bgm);
		bgm = NULL;
	}
	if (bgm_name) {
		free(bgm_name);
		bgm_name = NULL;
	}
}

void audio_bgm_stop(void)
{
	AUDIO_LOG("audio_bgm_stop()");
	_audio_bgm_stop();
}

void audio_update(void)
{
	static uint32_t prev_fade_t = 0;

	if (!fade.fading)
		return;

	uint32_t t = vm_get_ticks();
	if (t - prev_fade_t < 30)
		return;

	if (t >= fade.start_t + fade.ms) {
		fade.fading = false;
		if (fade.stop)
			_audio_bgm_stop();
		else
			Mix_Volume(0, fade.end_vol);
		return;
	}

	float rate = (float)(t - fade.start_t) / (float)fade.ms;
	int vol = fade.start_vol + (fade.end_vol - fade.start_vol) * rate;
	Mix_Volume(0, vol);
}

// XXX: Volume is a value in the range [0,31], which corresponds to the range
//      [-5000,0] in increments of 156 (volume in dB as expected by
//      DirectSound). We convert this value to a linear volume scale.
static int get_linear_volume(uint8_t vol)
{
	int directsound_volume = -5000 + vol * 156;
	int linear_volume = 128;
	if (vol < 31) {
		float v = powf(10.f, (float)directsound_volume / 2000.f);
		linear_volume = floorf(v * 128 + 0.5f);
	};
	return linear_volume;
}

void audio_bgm_play(const char *name, bool check_playing)
{
	AUDIO_LOG("audio_bgm_play(\"%s\", %s)", name, check_playing ? "true" : "false");
	if (check_playing && bgm_name && !strcmp(name, bgm_name))
		return;
	_audio_bgm_stop();
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
	Mix_Volume(0, get_linear_volume(bgm_volume));
	Mix_PlayChannel(0, bgm, -1);
	bgm_name = strdup(name);
}

void audio_bgm_set_volume(uint8_t vol)
{
	AUDIO_LOG("audio_bgm_set_volume(%u)", vol);
	bgm_volume = vol;
	Mix_Volume(0, get_linear_volume(vol));
}

static unsigned fade_time(uint8_t vol)
{
	unsigned diff = abs((int)vol - (int)bgm_volume);
	return diff * 100 + 50;
}

void audio_bgm_fade_out(uint32_t uk, uint8_t vol, bool sync)
{
	if (vol == bgm_volume) {
		_audio_bgm_stop();
		return;
	}
	audio_bgm_fade(uk, vol, true, sync);
}

void audio_bgm_fade(uint32_t uk, uint8_t vol, bool stop, bool sync)
{
	AUDIO_LOG("audio_bgm_fade(%u, %u, %s, %s)", uk, vol, stop ? "true" : "false",
			sync ? "true" : "false");
	if (fade.fading) {
		Mix_Volume(0, fade.end_vol);
		fade.fading = false;
	}

	if (vol > bgm_volume)
		stop = false;
	else if (vol == bgm_volume)
		return;

	fade.fading = true;
	fade.start_t = vm_get_ticks();
	fade.ms = fade_time(vol);
	fade.start_vol = get_linear_volume(bgm_volume);
	fade.end_vol = get_linear_volume(vol);
	fade.stop = stop;

	bgm_volume = vol;

	if (sync) {
		while (fade.fading) {
			vm_peek();
			vm_delay(16);
		}
	}
}

void audio_bgm_restore_volume(void)
{
	AUDIO_LOG("audio_bgm_restore_volume()");
	if (!bgm)
		return;
	if (bgm_volume == 31) {
		_audio_bgm_stop();
		return;
	}

	audio_bgm_fade(0, 31, false, false);
}

bool audio_bgm_is_playing(void)
{
	AUDIO_LOG("audio_bgm_is_playing()");
	return Mix_Playing(0);
}

bool audio_bgm_is_fading(void)
{
	AUDIO_LOG("audio_bgm_is_fading()");
	return Mix_FadingChannel(0) != MIX_NO_FADING;
}

static void _audio_se_stop(void)
{
	Mix_HaltChannel(1);
	if (se) {
		Mix_FreeChunk(se);
		se = NULL;
	}
}

void audio_se_stop(void)
{
	AUDIO_LOG("audio_se_stop()");
	_audio_se_stop();
}

void audio_se_play(const char *name)
{
	AUDIO_LOG("audio_se_play(\"%s\")", name);
	_audio_se_stop();
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

bool audio_se_is_playing(void)
{
	return Mix_Playing(1);
}

