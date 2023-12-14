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
#include "mixer.h"
#include "vm.h"

#if 0
#define AUDIO_LOG(...) NOTICE(__VA_ARGS__)
#else
#define AUDIO_LOG(...)
#endif

static struct channel *bgm = NULL;
static struct channel *se = NULL;
static char *bgm_name = NULL;
static uint8_t bgm_volume = 31;

void audio_init(void)
{
	mixer_init();
}

static void _audio_bgm_stop(void)
{
	if (bgm) {
		channel_stop(bgm);
		channel_close(bgm);
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

void audio_bgm_play(const char *name, bool check_playing)
{
	AUDIO_LOG("audio_bgm_play(\"%s\", %s)", name, check_playing ? "true" : "false");
	if (check_playing && bgm_name && !strcmp(name, bgm_name))
		return;
	_audio_bgm_stop();

	if ((bgm = channel_open(name, MIXER_MUSIC))) {
		if (bgm_volume != 31)
			channel_fade(bgm, 0, get_linear_volume(bgm_volume), false);
		channel_play(bgm);
		bgm_name = strdup(name);
	}
}

void audio_bgm_set_volume(uint8_t vol)
{
	AUDIO_LOG("audio_bgm_set_volume(%u)", vol);
	bgm_volume = vol;
	if (!bgm)
		return;
	channel_fade(bgm, 0, get_linear_volume(vol), false);
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
	AUDIO_LOG("audio_bgm_fade(%u,%u,%s,%s)", uk, vol, stop ? "true" : "false",
			sync ? "true" : "false");
	if (!bgm)
		return;
	// XXX: a bit strange, but this is how it works...
	if (vol > bgm_volume)
		stop = false;
	else if (vol == bgm_volume)
		return;

	channel_fade(bgm, fade_time(vol), get_linear_volume(vol), stop);
	while (channel_is_fading(bgm)) {
		vm_peek();
		vm_delay(16);
	}
	bgm_volume = vol;
}

void audio_bgm_restore_volume(void)
{
	AUDIO_LOG("bgm_restore_volume()");
	if (!bgm)
		return;
	if (bgm_volume == 31) {
		_audio_bgm_stop();
		return;
	}

	channel_fade(bgm, fade_time(31), 100, false);
	bgm_volume = 31;
}

bool audio_bgm_is_playing(void)
{
	AUDIO_LOG("audio_bgm_is_playing()");
	return bgm && channel_is_playing(bgm);
}

bool audio_bgm_is_fading(void)
{
	AUDIO_LOG("audio_bgm_is_fading()");
	return bgm && channel_is_fading(bgm);
}

static void _audio_se_stop(void)
{
	if (se) {
		channel_stop(se);
		channel_close(se);
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

	if ((se = channel_open(name, MIXER_EFFECT))) {
		channel_play(se);
	}
}

bool audio_se_is_playing(void)
{
	AUDIO_LOG("audio_se_is_playing()");
	return se && channel_is_playing(se);
}
