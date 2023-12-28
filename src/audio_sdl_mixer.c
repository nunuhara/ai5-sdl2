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
#include "game.h"
#include "vm.h"

#if 0
#define AUDIO_LOG(...) NOTICE(__VA_ARGS__)
#else
#define AUDIO_LOG(...)
#endif

struct fade {
	bool fading;
	uint32_t start_t;
	uint32_t ms;
	int start_vol;
	int end_vol;
	bool stop;
};

struct channel {
	unsigned id;
	Mix_Chunk *chunk;
	char *file_name;
	uint8_t volume;
	int repeat;
	struct fade fade;
};
struct channel bgm_channel   = { .id = 0, .volume = 31, .repeat = -1 };
struct channel se_channel    = { .id = 1, .volume = 31 };
struct channel voice_channel = { .id = 2, .volume = 31 };

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

static void channel_stop(struct channel *ch)
{
	ch->fade.fading = false;
	Mix_HaltChannel(ch->id);
	if (ch->chunk) {
		Mix_FreeChunk(ch->chunk);
		ch->chunk = NULL;
	}
	if (ch->file_name) {
		free(ch->file_name);
		ch->file_name = NULL;
	}
}

void audio_bgm_stop(void)
{
	AUDIO_LOG("audio_bgm_stop()");
	channel_stop(&bgm_channel);
}

void audio_se_stop(void)
{
	AUDIO_LOG("audio_se_stop()");
	channel_stop(&se_channel);
}

void audio_voice_stop(void)
{
	AUDIO_LOG("audio_voice_stop()");
	channel_stop(&voice_channel);
}

static void channel_fade_end(struct channel *ch)
{
	assert(ch->fade.fading);
	ch->fade.fading = false;
	if (ch->fade.stop)
		channel_stop(ch);
	else
		Mix_Volume(ch->id, ch->fade.end_vol);
}

static void channel_update(struct channel *ch, uint32_t t)
{
	if (!ch->fade.fading)
		return;

	if (t >= ch->fade.start_t + ch->fade.ms) {
		channel_fade_end(ch);
		return;
	}

	float rate = (float)(t - ch->fade.start_t) / (float)ch->fade.ms;
	int vol = ch->fade.start_vol + (ch->fade.end_vol - ch->fade.start_vol) * rate;
	Mix_Volume(ch->id, vol);
}

void audio_update(void)
{
	static uint32_t prev_fade_t = 0;
	uint32_t t = vm_get_ticks();
	if (t - prev_fade_t < 30)
		return;

	channel_update(&bgm_channel, t);
	channel_update(&se_channel, t);
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

static struct archive_data *load_data(unsigned id, const char *name)
{
	switch (id) {
	case 0: return asset_bgm_load(name);
	case 1: return asset_effect_load(name);
	case 2: return asset_voice_load(name);
	default: VM_ERROR("Invalid channel id: %u", id);
	}
}

static void channel_play(struct channel *ch, const char *name, bool check_playing)
{
	if (check_playing && ch->file_name && !strcmp(ch->file_name, name))
		return;
	channel_stop(ch);
	struct archive_data *data = load_data(ch->id, name);
	if (!data) {
		WARNING("Failed to load audio file on channel %u: \"%s\"", ch->id, name);
		return;
	}
	ch->chunk = Mix_LoadWAV_RW(SDL_RWFromConstMem(data->data, data->size), 1);
	archive_data_release(data);
	if (!ch->chunk) {
		WARNING("Failed to decode audio file on channel %u: \"%s\"", ch->id, name);
		return;
	}
	if (game->persistent_volume) {
		Mix_Volume(ch->id, get_linear_volume(ch->volume));
	} else {
		Mix_Volume(ch->id, MIX_MAX_VOLUME);
		ch->volume = 31;
	}
	Mix_PlayChannel(ch->id, ch->chunk, ch->repeat);
	ch->file_name = strdup(name);
}

void audio_bgm_play(const char *name, bool check_playing)
{
	AUDIO_LOG("audio_bgm_play(\"%s\", %s)", name, check_playing ? "true" : "false");
	channel_play(&bgm_channel, name, check_playing);
}

void audio_se_play(const char *name)
{
	AUDIO_LOG("audio_se_play(\"%s\")", name);
	channel_play(&se_channel, name, false);
}

void audio_voice_play(const char *name)
{
	AUDIO_LOG("audio_voice_play(\"%s\")", name);
	channel_play(&voice_channel, name, false);
}

static void channel_set_volume(struct channel *ch, uint8_t vol)
{
	ch->volume = vol;
	Mix_Volume(ch->id, get_linear_volume(vol));
}

void audio_bgm_set_volume(uint8_t vol)
{
	AUDIO_LOG("audio_bgm_set_volume(%u)", vol);
	channel_set_volume(&bgm_channel, vol);
}

static unsigned fade_time(struct channel *ch, uint8_t vol)
{
	unsigned diff = abs((int)vol - (int)ch->volume);
	return diff * 100 + 50;
}

static void channel_fade(struct channel *ch, uint8_t vol, int t, bool stop, bool sync)
{
	if (ch->fade.fading)
		channel_fade_end(ch);

	if (!Mix_Playing(ch->id))
		return;

	if (vol > ch->volume)
		stop = false;
	else if (vol == ch->volume)
		return;

	if (t < 0)
		t = fade_time(ch, vol);

	ch->fade.fading = true;
	ch->fade.start_t = vm_get_ticks();
	ch->fade.ms = t;
	ch->fade.start_vol = get_linear_volume(ch->volume);
	ch->fade.end_vol = get_linear_volume(vol);
	ch->fade.stop = stop;

	ch->volume = vol;

	if (sync) {
		while (ch->fade.fading) {
			vm_peek();
			vm_delay(16);
		}
	}
}

static void channel_fade_out(struct channel *ch, uint8_t vol, bool sync)
{
	if (vol == ch->volume) {
		channel_stop(ch);
		return;
	}
	channel_fade(ch, vol, -1, true, sync);
}

void audio_bgm_fade_out(uint8_t vol, bool sync)
{
	channel_fade_out(&bgm_channel, vol, sync);
}

void audio_bgm_fade(uint8_t vol, int t, bool stop, bool sync)
{
	AUDIO_LOG("audio_bgm_fade(%u,%d,%s,%s)", vol, t, stop ? "true" : "false",
			sync ? "true" : "false");
	channel_fade(&bgm_channel, vol, t, stop, sync);
}

void audio_se_fade(uint8_t vol, int t, bool stop, bool sync)
{
	AUDIO_LOG("audio_se_fade(%u,%d,%s,%s)", vol, t, stop ? "true" : "false",
			sync ? "true" : "false");
	channel_fade(&se_channel, vol, t, stop, sync);
}

static void channel_restore_volume(struct channel *ch)
{
	if (!ch->chunk)
		return;
	if (ch->volume == 31) {
		channel_stop(ch);
		return;
	}

	channel_fade(ch, 31, -1, false, false);
}

void audio_bgm_restore_volume(void)
{
	AUDIO_LOG("audio_bgm_restore_volume()");
	channel_restore_volume(&bgm_channel);
}

static bool channel_is_playing(struct channel *ch)
{
	return Mix_Playing(ch->id);
}

bool audio_bgm_is_playing(void)
{
	AUDIO_LOG("audio_bgm_is_playing()");
	return channel_is_playing(&bgm_channel);
}

bool audio_se_is_playing(void)
{
	AUDIO_LOG("audio_se_is_playing() -> %s", channel_is_playing(&se_channel) ? "true" : "false");
	return channel_is_playing(&se_channel);
}

static bool channel_is_fading(struct channel *ch)
{
	return ch->fade.fading;
}

bool audio_bgm_is_fading(void)
{
	AUDIO_LOG("audio_bgm_is_fading()");
	return channel_is_fading(&bgm_channel);
}
