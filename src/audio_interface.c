/* Copyright (C) 2024 Nunuhara Cabbage <nunuhara@haniwa.technology>
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

// NOTE: This file is included by the actual audio implementation file
//       (audio.c, audio_sdl_mixer.c).

#if 0
#define AUDIO_LOG(...) NOTICE(__VA_ARGS__)
#else
#define AUDIO_LOG(...)
#endif

// XXX: The audio implementation provides this interface.
static void channel_play(struct channel *ch, struct archive_data *file, bool check_playing);
static void channel_set_volume(struct channel *ch, int vol);
static void channel_fade(struct channel *ch, int vol, int t, bool stop, bool sync);
static void channel_mixer_fade(struct channel *ch, int vol, int t, bool stop, bool sync);
static bool channel_is_playing(struct channel *ch);
static bool channel_is_fading(struct channel *ch);

void audio_play(enum audio_channel ch, struct archive_data *file, bool check_playing)
{
	AUDIO_LOG("audio_play(%s, \"%s\", %s)", audio_channel_name(ch), file->name,
			check_playing ? "true" : "false");
	channel_play(&channels[ch], file, check_playing);
}

void audio_stop(enum audio_channel ch)
{
	AUDIO_LOG("audio_stop(%s)", audio_channel_name(ch));
	channel_stop(&channels[ch]);
}

void audio_set_volume(enum audio_channel ch, int vol)
{
	AUDIO_LOG("audio_set_volume(%s, %d)", audio_channel_name(ch), vol);
	channel_set_volume(&channels[ch], vol);
}

void audio_fade(enum audio_channel ch, int vol, int t, bool stop, bool sync)
{
	AUDIO_LOG("audio_fade(%s, %d, %d, %s, %s)", audio_channel_name(ch), vol, t,
			stop ? "true" : "false", sync ? "true" : "false");
	channel_fade(&channels[ch], vol, t, stop, sync);
}

void audio_mixer_fade(enum audio_channel ch, int vol, int t, bool stop, bool sync)
{
	AUDIO_LOG("audio_mixer_fade(%s, %d, %d, %s, %s)", audio_channel_name(ch), vol, t,
			stop ? "true" : "false", sync ? "true" : "false");
	channel_mixer_fade(&channels[ch], vol, t, stop, sync);
}

bool audio_is_playing(enum audio_channel ch)
{
	AUDIO_LOG("audio_is_playing(%s)", audio_channel_name(ch));
	return channel_is_playing(&channels[ch]);
}

bool audio_is_fading(enum audio_channel ch)
{
	AUDIO_LOG("audio_is_fading(%s)", audio_channel_name(ch));
	return channel_is_fading(&channels[ch]);
}

void audio_bgm_play(const char *name, bool check_playing)
{
	struct archive_data *file = asset_bgm_load(name);
	if (!file) {
		WARNING("Failed to load BGM file: %s", name);
		return;
	}
	audio_play(AUDIO_CH_BGM, file, check_playing);
	archive_data_release(file);
}

void audio_se_play(const char *name, unsigned ch)
{
	if (!audio_se_channel_valid(ch)) {
		WARNING("Invalid SE channel: %u", ch);
		return;
	}
	struct archive_data *file = asset_effect_load(name);
	if (!file) {
		WARNING("Failed to load SE file: %s", name);
		return;
	}
	audio_play(AUDIO_CH_SE(ch), file, false);
	archive_data_release(file);
}

void audio_sysse_play(const char *name, unsigned ch)
{
	if (!audio_se_channel_valid(ch)) {
		WARNING("Invalid SE channel: %u", ch);
		return;
	}
	struct archive_data *file = asset_load(ASSET_SYSSE, name);
	if (!file) {
		WARNING("Failed to load SYSSE file: %s", name);
		return;
	}
	audio_play(AUDIO_CH_SE(ch), file, false);
	archive_data_release(file);
}

void audio_voice_play(const char *name, unsigned ch)
{
	if (!audio_voice_channel_valid(ch)) {
		WARNING("Invalid voice channel: %u", ch);
		return;
	}
	struct archive_data *file = asset_voice_load(name);
	if (!file) {
		WARNING("Failed to load voice file: %s", name);
		return;
	}
	audio_play(AUDIO_CH_VOICE(ch), file, false);
	archive_data_release(file);
}

void audio_voice_stop(unsigned ch)
{
	if (!audio_voice_channel_valid(ch)) {
		WARNING("Invalid voice channel: %u", ch);
		return;
	}
	audio_stop(AUDIO_CH_VOICE(ch));
}

void audio_voicesub_play(const char *name)
{
	struct archive_data *file = asset_voicesub_load(name);
	if (!file) {
		WARNING("Failed to load voicesub file: %s", name);
		return;
	}
	audio_play(AUDIO_CH_VOICE1, file, false);
	archive_data_release(file);
}

void audio_se_stop(unsigned ch)
{
	if (!audio_se_channel_valid(ch)) {
		WARNING("Invalid SE channel: %u", ch);
		return;
	}
	audio_stop(AUDIO_CH_SE(ch));
}

void audio_se_fade(int vol, unsigned t, bool stop, bool sync, unsigned ch)
{
	if (!audio_se_channel_valid(ch)) {
		WARNING("Invalid SE channel: %u", ch);
		return;
	}
	audio_fade(AUDIO_CH_SE(ch), vol, t, stop, sync);
}
