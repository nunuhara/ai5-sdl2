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

#ifndef AI5_AUDIO_H
#define AI5_AUDIO_H

#include <stdint.h>

enum audio_channel {
	AUDIO_CH_BGM = 0,
	AUDIO_CH_SE0 = 1,
	AUDIO_CH_SE1 = 2,
	AUDIO_CH_SE2 = 3,
	AUDIO_CH_VOICE0 = 4,
	AUDIO_CH_VOICE1 = 5,
};
#define AUDIO_CH_SE(n) (AUDIO_CH_SE0 + n)
#define AUDIO_CH_VOICE(n) (AUDIO_CH_VOICE0 + n)

#define AUDIO_VOLUME_MIN (-5000)
#define AUDIO_VOLUME_MAX 0

static inline bool audio_se_channel_valid(unsigned ch)
{
	return ch < 3;
}

static inline bool audio_voice_channel_valid(unsigned ch)
{
	return ch < 2;
}

static inline const char *audio_channel_name(enum audio_channel ch)
{
	switch (ch) {
	case AUDIO_CH_BGM: return "BGM";
	case AUDIO_CH_SE0: return "SE0";
	case AUDIO_CH_SE1: return "SE1";
	case AUDIO_CH_SE2: return "SE2";
	case AUDIO_CH_VOICE0: return "VOICE0";
	case AUDIO_CH_VOICE1: return "VOICE1";
	}
	return "INVALID_CHANNEL";
}

struct archive_data;

void audio_init(void);
void audio_update(void);
void audio_play(enum audio_channel ch, struct archive_data *file, bool check_playing);
void audio_stop(enum audio_channel ch);
void audio_set_volume(enum audio_channel ch, int vol);
void audio_fade(enum audio_channel ch, int vol, int t, bool stop, bool sync);
void audio_mixer_fade(enum audio_channel ch, int vol, int t, bool stop, bool sync);
void audio_restore_volume(enum audio_channel ch);
bool audio_is_playing(enum audio_channel ch);
bool audio_is_fading(enum audio_channel ch);

// convenience functions
void audio_bgm_play(const char *name, bool check_playing);

void audio_se_play(const char *name, unsigned ch);
void audio_se_stop(unsigned ch);
void audio_se_fade(int vol, unsigned t, bool stop, bool sync, unsigned ch);

void audio_voice_play(const char *name, unsigned ch);
void audio_voice_stop(unsigned ch);
void audio_voicesub_play(const char *name);
#define audio_voicesub_stop() audio_stop(AUDIO_CH_VOICE1)
#define audio_voicesub_is_playing() audio_is_playing(AUDIO_CH_VOICE1)

#endif // AI5_AUDIO_H
