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

// XXX: Volume is given in hundredths of decibels, from -5000 to 0.
//      We convert this value to a linear volume scale.
static int get_linear_volume(int vol)
{
	vol = clamp(-5000, 0, vol);
	int linear_volume = 100;
	if (vol < 31) {
		float v = powf(10.f, (float)vol / 2000.f);
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
		mixer_stream_play(ch->ch);
		ch->file_name = strdup(file->name);
	}
}

static void channel_set_volume(struct channel *ch, int vol)
{
	mixer_set_volume(ch->id, get_linear_volume(vol));
}

static void channel_fade(struct channel *ch, int vol, int t, bool stop, bool sync)
{
	if (!ch->ch)
		return;
	unsigned end_vol = get_linear_volume(vol);
	if (mixer_stream_get_volume(ch->ch) == end_vol)
		return;

	mixer_stream_fade(ch->ch, t, end_vol, stop);
	if (sync) {
		while (mixer_stream_is_fading(ch->ch)) {
			vm_peek();
			vm_delay(16);
		}
	}
}

static void channel_mixer_fade(struct channel *ch, int vol, int t, bool stop, bool sync)
{
	int cur_vol;
	unsigned end_vol = get_linear_volume(vol);
	if (mixer_get_volume(ch->id, &cur_vol) && cur_vol == end_vol)
		return;

	mixer_fade(ch->id, t, end_vol, stop);
	if (sync) {
		while (mixer_is_fading(ch->id)) {
			vm_peek();
			vm_delay(16);
		}
	}
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
	[AUDIO_CH_BGM]    = { .id = MIXER_MUSIC },
	[AUDIO_CH_SE0]    = { .id = MIXER_EFFECT },
	[AUDIO_CH_SE1]    = { .id = MIXER_EFFECT },
	[AUDIO_CH_SE2]    = { .id = MIXER_EFFECT },
	[AUDIO_CH_VOICE0] = { .id = MIXER_VOICE },
	[AUDIO_CH_VOICE1] = { .id = MIXER_VOICE },
};

void audio_update(void)
{
}

// XXX: Include interface boilerplate
#include "audio_interface.c"
