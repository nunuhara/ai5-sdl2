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

#include "nulib.h"
#include "ai5/arc.h"

#include "asset.h"
#include "audio.h"
#include "game.h"
#include "mixer.h"
#include "vm.h"

struct channel {
	unsigned id;
	struct mixer_stream *ch;
	char *file_name;
	uint8_t volume;
};

void audio_init(void)
{
	mixer_init();
}

static void channel_stop(struct channel *ch)
{
	if (ch->ch) {
		mixer_stream_stop(ch->ch);
		mixer_stream_close(ch->ch);
		ch->ch = NULL;
	}
	if (ch->file_name) {
		free(ch->file_name);
		ch->file_name = NULL;
	}
}

// XXX: Volume is a value in the range [0,31], which corresponds to the range
//      [-5000,0] in increments of 156 (volume in dB as expected by
//      DirectSound). We convert this value to a linear volume scale.
static int get_linear_volume(uint8_t vol)
{
	int directsound_volume = -5000 + vol * 156;
	int linear_volume = 100;
	if (vol < 31) {
		float v = powf(10.f, (float)directsound_volume / 2000.f);
		linear_volume = floorf(v * 100 + 0.5f);
	};
	return linear_volume;
}

static void channel_play(struct channel *ch, struct archive_data *file, bool check_playing)
{
	if (check_playing && ch->file_name && !strcmp(file->name, ch->file_name)) {
		if (ch->ch && mixer_stream_is_playing(ch->ch))
			return;
	}

	channel_stop(ch);

	if ((ch->ch = mixer_stream_open(file, ch->id))) {
		if (game->persistent_volume) {
			if (ch->volume != 31)
				mixer_stream_fade(ch->ch, 0, get_linear_volume(ch->volume), false);
		} else {
			ch->volume = 31;
		}
		mixer_stream_play(ch->ch);
		ch->file_name = strdup(file->name);
	}
}

static void channel_set_volume(struct channel *ch, uint8_t vol)
{
	ch->volume = vol;
	if (ch->ch)
		mixer_stream_fade(ch->ch, 0, get_linear_volume(vol), false);
}

static unsigned fade_time(struct channel *ch, uint8_t vol)
{
	unsigned diff = abs((int)vol - (int)ch->volume);
	return diff * 100 + 50;
}

static void channel_fade(struct channel *ch, uint8_t vol, int t, bool stop, bool sync)
{
	if (!ch->ch)
		return;
	// XXX: a bit strange, but this is how it works...
	if (vol > ch->volume)
		stop = false;
	else if (vol == ch->volume)
		return;

	if (t < 0)
		t = fade_time(ch, vol);

	mixer_stream_fade(ch->ch, t, get_linear_volume(vol), stop);
	while (sync && mixer_stream_is_fading(ch->ch)) {
		vm_peek();
		vm_delay(16);
	}
	ch->volume = vol;
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
	if (!ch->ch)
		return;
	if (ch->volume == 31) {
		channel_stop(ch);
		return;
	}

	mixer_stream_fade(ch->ch, fade_time(ch, 31), 100, false);
	ch->volume = 31;
}

static bool channel_is_playing(struct channel *ch)
{
	return ch->ch && mixer_stream_is_playing(ch->ch);
}

static bool channel_is_fading(struct channel *ch)
{
	return ch->ch && mixer_stream_is_fading(ch->ch);
}

static struct channel channels[] = {
	[AUDIO_CH_BGM]    = { .id = MIXER_MUSIC,  .volume = 31 },
	[AUDIO_CH_SE0]    = { .id = MIXER_EFFECT, .volume = 31 },
	[AUDIO_CH_SE1]    = { .id = MIXER_EFFECT, .volume = 31 },
	[AUDIO_CH_SE2]    = { .id = MIXER_EFFECT, .volume = 31 },
	[AUDIO_CH_VOICE0] = { .id = MIXER_VOICE,  .volume = 31 },
	[AUDIO_CH_VOICE1] = { .id = MIXER_VOICE,  .volume = 31 },
};

void audio_update(void)
{
}

// XXX: Include interface boilerplate
#include "audio_interface.c"
