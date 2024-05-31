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
	else
		Mix_Volume(ch->id, ch->fade.end_vol);
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
	if (game->persistent_volume) {
		Mix_Volume(ch->id, get_linear_volume(ch->volume));
	} else {
		Mix_Volume(ch->id, MIX_MAX_VOLUME);
		ch->volume = 31;
	}
	Mix_PlayChannel(ch->id, ch->chunk, ch->repeat);
	ch->file_name = strdup(file->name);
}

static void channel_set_volume(struct channel *ch, uint8_t vol)
{
	ch->volume = vol;
	Mix_Volume(ch->id, get_linear_volume(vol));
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

static bool channel_is_playing(struct channel *ch)
{
	return Mix_Playing(ch->id);
}

static bool channel_is_fading(struct channel *ch)
{
	return ch->fade.fading;
}

static struct channel channels[] = {
	[AUDIO_CH_BGM] = { .id = 0, .volume = 31, .repeat = -1 },
	[AUDIO_CH_SE0] = { .id = 1, .volume = 31 },
	[AUDIO_CH_SE1] = { .id = 2, .volume = 31 },
	[AUDIO_CH_SE2] = { .id = 3, .volume = 31 },
	[AUDIO_CH_VOICE0] = { .id = 4, .volume = 31 },
	[AUDIO_CH_VOICE1] = { .id = 5, .volume = 31 },
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
	Mix_Volume(ch->id, vol);
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
