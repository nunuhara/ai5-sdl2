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

struct fade {
	bool mixer_fade;
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
	int repeat;
	struct fade fade;
};

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

static void channel_fade_end(struct channel *ch)
{
	assert(ch->fade.fading);
	ch->fade.fading = false;
	if (ch->fade.stop)
		channel_stop(ch);
	else if (ch->fade.mixer_fade)
		Mix_Volume(ch->id, ch->fade.end_vol);
	else if (ch->chunk)
		Mix_VolumeChunk(ch->chunk, ch->fade.end_vol);
}

// XXX: Volume is given in hundredths of decibels, from -5000 to 0.
//      We convert this value to a linear volume scale.
static int get_linear_volume(int vol)
{
	vol = clamp(-5000, 0, vol);
	int linear_volume = 128;
	if (vol < 0) {
		float v = powf(10.f, (float)vol / 2000.f);
		linear_volume = floorf(v * 128 + 0.5f);
	};
	return linear_volume;
}

static void channel_play(struct channel *ch, struct archive_data *file, bool check_playing)
{
	if (check_playing && ch->file_name && !strcmp(ch->file_name, file->name))
		return;
	channel_stop(ch);
	ch->chunk = Mix_LoadWAV_RW(SDL_RWFromConstMem(file->data, file->size), 1);
	if (!ch->chunk) {
		WARNING("Failed to decode audio file on channel %u: \"%s\"", ch->id, file->name);
		return;
	}
	Mix_PlayChannel(ch->id, ch->chunk, ch->repeat);
	ch->file_name = strdup(file->name);
}

static void channel_set_volume(struct channel *ch, int vol)
{
	if (ch->fade.fading)
		channel_fade_end(ch);
	Mix_Volume(ch->id, get_linear_volume(vol));
}

static void channel_fade_wait(struct channel *ch)
{
	while (ch->fade.fading) {
		vm_peek();
		vm_delay(16);
	}
}

static void channel_fade(struct channel *ch, int vol, int t, bool stop, bool sync)
{
	if (ch->fade.fading)
		channel_fade_end(ch);

	if (!ch->chunk)
		return;

	unsigned end_vol = get_linear_volume(vol);
	if (Mix_VolumeChunk(ch->chunk, -1) == end_vol)
		return;

	ch->fade.fading = true;
	ch->fade.mixer_fade = false;
	ch->fade.start_t = vm_get_ticks();
	ch->fade.ms = t;
	ch->fade.start_vol = Mix_VolumeChunk(ch->chunk, -1);
	ch->fade.end_vol = end_vol;
	ch->fade.stop = stop;

	if (sync)
		channel_fade_wait(ch);
}

static void channel_mixer_fade(struct channel *ch, int vol, int t, bool stop, bool sync)
{
	if (ch->fade.fading)
		channel_fade_end(ch);

	unsigned end_vol = get_linear_volume(vol);
	if (!Mix_Playing(ch->id)) {
		Mix_Volume(ch->id, end_vol);
		return;
	}

	ch->fade.fading = true;
	ch->fade.mixer_fade = true;
	ch->fade.start_t = vm_get_ticks();
	ch->fade.ms = t;
	ch->fade.start_vol = Mix_Volume(ch->id, -1);
	ch->fade.end_vol = end_vol;
	ch->fade.stop = stop;

	if (sync)
		channel_fade_wait(ch);
}

static bool channel_is_playing(struct channel *ch)
{
	return Mix_Playing(ch->id);
}

static bool channel_is_fading(struct channel *ch)
{
	return ch->fade.fading;
}

static struct channel channels[] = {
	[AUDIO_CH_BGM] = { .id = 0, .repeat = -1 },
	[AUDIO_CH_SE0] = { .id = 1 },
	[AUDIO_CH_SE1] = { .id = 2 },
	[AUDIO_CH_SE2] = { .id = 3 },
	[AUDIO_CH_VOICE0] = { .id = 4 },
	[AUDIO_CH_VOICE1] = { .id = 5 },
};

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
	if (ch->fade.mixer_fade)
		Mix_Volume(ch->id, vol);
	else if (ch->chunk)
		Mix_VolumeChunk(ch->chunk, vol);
}

void audio_update(void)
{
	static uint32_t prev_fade_t = 0;
	uint32_t t = vm_get_ticks();
	if (t - prev_fade_t < 30)
		return;

	for (int i = 0; i < ARRAY_SIZE(channels); i++) {
		channel_update(&channels[i], t);
	}
}

// XXX: Include interface boilerplate
#include "audio_interface.c"
