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

#if 0
#define AUDIO_LOG(...) NOTICE(__VA_ARGS__)
#else
#define AUDIO_LOG(...)
#endif

struct audio_ch {
	unsigned id;
	struct channel *ch;
	char *file_name;
	uint8_t volume;
};
static struct audio_ch bgm_channel      = { .id = MIXER_MUSIC,    .volume = 31 };
static struct audio_ch se_channel       = { .id = MIXER_EFFECT,   .volume = 31 };
static struct audio_ch voice_channel    = { .id = MIXER_VOICE,    .volume = 31 };
static struct audio_ch voicesub_channel = { .id = MIXER_VOICESUB, .volume = 31 };

static struct audio_ch aux_channel[] = {
	{ .id = MIXER_MUSIC, .volume = 31 },
	{ .id = MIXER_MUSIC, .volume = 31 },
	{ .id = MIXER_MUSIC, .volume = 31 },
};

static inline bool aux_channel_valid(int no)
{
	return no >= 0 && no <= 2;
}

void audio_init(void)
{
	mixer_init();
}

static void audio_ch_stop(struct audio_ch *ch)
{
	if (ch->ch) {
		channel_stop(ch->ch);
		channel_close(ch->ch);
		ch->ch = NULL;
	}
	if (ch->file_name) {
		free(ch->file_name);
		ch->file_name = NULL;
	}
}

void audio_bgm_stop(void)
{
	AUDIO_LOG("audio_bgm_stop()");
	audio_ch_stop(&bgm_channel);
}

void audio_se_stop(void)
{
	AUDIO_LOG("audio_se_stop()");
	audio_ch_stop(&se_channel);
}

void audio_voice_stop(void)
{
	AUDIO_LOG("audio_voice_stop()");
	audio_ch_stop(&voice_channel);
}

void audio_voicesub_stop(void)
{
	AUDIO_LOG("audio_voicesub_stop()");
	audio_ch_stop(&voicesub_channel);
}

void audio_aux_stop(int no)
{
	AUDIO_LOG("audio_aux_stop(%d)", no);
	if (!aux_channel_valid(no)) {
		WARNING("Invalid aux channel: %d", no);
		return;
	}
	audio_ch_stop(&aux_channel[no]);
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

static void audio_ch_play(struct audio_ch *ch, const char *name, bool check_playing)
{
	if (check_playing && ch->file_name && !strcmp(name, ch->file_name)) {
		if (ch->ch && channel_is_playing(ch->ch))
			return;
	}

	audio_ch_stop(ch);

	if ((ch->ch = channel_open(name, ch->id))) {
		if (game->persistent_volume) {
			if (ch->volume != 31)
				channel_fade(ch->ch, 0, get_linear_volume(ch->volume), false);
		} else {
			ch->volume = 31;
		}
		channel_play(ch->ch);
		ch->file_name = strdup(name);
	}
}

void audio_bgm_play(const char *name, bool check_playing)
{
	AUDIO_LOG("audio_bgm_play(\"%s\", %s)", name, check_playing ? "true" : "false");
	audio_ch_play(&bgm_channel, name, check_playing);
}

void audio_se_play(const char *name)
{
	AUDIO_LOG("audio_se_play(\"%s\")", name);
	audio_ch_play(&se_channel, name, false);
}

void audio_voice_play(const char *name)
{
	AUDIO_LOG("audio_voice_play(\"%s\")", name);
	audio_ch_play(&voice_channel, name, false);
}

void audio_voicesub_play(const char *name)
{
	AUDIO_LOG("audio_voicesub_play(\"%s\")", name);
	audio_ch_play(&voicesub_channel, name, false);
}

void audio_aux_play(const char *name, int no)
{
	AUDIO_LOG("audio_aux_play(\"%s\", %d)", name, no);
	if (!aux_channel_valid(no)) {
		WARNING("Invalid aux channel: %d", no);
		return;
	}
	audio_ch_play(&aux_channel[no], name, false);
}

static void audio_ch_set_volume(struct audio_ch *ch, uint8_t vol)
{
	ch->volume = vol;
	if (ch->ch)
		channel_fade(ch->ch, 0, get_linear_volume(vol), false);
}

void audio_bgm_set_volume(uint8_t vol)
{
	AUDIO_LOG("audio_bgm_set_volume(%u)", vol);
	audio_ch_set_volume(&bgm_channel, vol);
}

static unsigned fade_time(struct audio_ch *ch, uint8_t vol)
{
	unsigned diff = abs((int)vol - (int)ch->volume);
	return diff * 100 + 50;
}

static void audio_ch_fade(struct audio_ch *ch, uint8_t vol, int t, bool stop, bool sync)
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

	channel_fade(ch->ch, t, get_linear_volume(vol), stop);
	while (sync && channel_is_fading(ch->ch)) {
		vm_peek();
		vm_delay(16);
	}
	ch->volume = vol;
}

static void audio_ch_fade_out(struct audio_ch *ch, uint8_t vol, bool sync)
{
	if (vol == ch->volume) {
		audio_ch_stop(ch);
		return;
	}
	audio_ch_fade(ch, vol, -1, true, sync);
}

void audio_bgm_fade_out(uint8_t vol, bool sync)
{
	audio_ch_fade_out(&bgm_channel, vol, sync);
}

void audio_aux_fade_out(uint8_t vol, bool sync, int no)
{
	AUDIO_LOG("audio_aux_fade_out(%u,%s,%d)", vol, sync ? "true" : "false", no);
	if (!aux_channel_valid(no)) {
		WARNING("Invalid aux channel: %d", no);
		return;
	}
	audio_ch_fade_out(&aux_channel[no], vol, sync);
}

void audio_bgm_fade(uint8_t vol, int t, bool stop, bool sync)
{
	AUDIO_LOG("audio_bgm_fade(%u,%d,%s,%s)", vol, t, stop ? "true" : "false",
			sync ? "true" : "false");
	audio_ch_fade(&bgm_channel, vol, t, stop, sync);
}

void audio_se_fade(uint8_t vol, int t, bool stop, bool sync)
{
	AUDIO_LOG("audio_se_fade(%u,%d,%s,%s)", vol, t, stop ? "true" : "false",
			sync ? "true" : "false");
	audio_ch_fade(&se_channel, vol, t, stop, sync);
}

static void audio_ch_restore_volume(struct audio_ch *ch)
{
	if (!ch->ch)
		return;
	if (ch->volume == 31) {
		audio_ch_stop(ch);
		return;
	}

	channel_fade(ch->ch, fade_time(ch, 31), 100, false);
	ch->volume = 31;
}

void audio_bgm_restore_volume(void)
{
	AUDIO_LOG("bgm_restore_volume()");
	audio_ch_restore_volume(&bgm_channel);
}

static bool audio_ch_is_playing(struct audio_ch *ch)
{
	return ch->ch && channel_is_playing(ch->ch);
}

bool audio_bgm_is_playing(void)
{
	AUDIO_LOG("audio_bgm_is_playing()");
	return audio_ch_is_playing(&bgm_channel);
}

bool audio_se_is_playing(void)
{
	AUDIO_LOG("audio_se_is_playing()");
	return audio_ch_is_playing(&se_channel);
}

bool audio_voice_is_playing(void)
{
	AUDIO_LOG("audio_voice_is_playing()");
	return audio_ch_is_playing(&voice_channel);
}

bool audio_voicesub_is_playing(void)
{
	AUDIO_LOG("audio_voicesub_is_playing()");
	return audio_ch_is_playing(&voicesub_channel);
}

static bool audio_ch_is_fading(struct audio_ch *ch)
{
	return ch->ch && channel_is_fading(ch->ch);
}

bool audio_bgm_is_fading(void)
{
	AUDIO_LOG("audio_bgm_is_fading()");
	return audio_ch_is_fading(&bgm_channel);
}
