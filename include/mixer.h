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

#ifndef AI5_MIXER_H
#define AI5_MIXER_H

#include <stdbool.h>

enum mix_channel {
	MIXER_MUSIC = 0,
	MIXER_EFFECT = 1,
	MIXER_VOICE = 2,
	MIXER_VOICESUB = 3,
	MIXER_MASTER = 4,
};

void mixer_init(void);
int mixer_get_numof(void);
const char *mixer_get_name(int n);
int mixer_set_name(int n, const char *name);
int mixer_get_volume(int n, int *volume);
int mixer_set_volume(int n, int volume);
int mixer_get_mute(int n, int *mute);
int mixer_set_mute(int n, int mute);

struct archive_data;
struct mixer_stream;

struct mixer_stream *mixer_stream_open(struct archive_data *dfile, enum mix_channel mixer);
void mixer_stream_close(struct mixer_stream *ch);
int mixer_stream_play(struct mixer_stream *ch);
int mixer_stream_stop(struct mixer_stream *ch);
int mixer_stream_is_playing(struct mixer_stream *ch);
int mixer_stream_set_loop_count(struct mixer_stream *ch, int count);
int mixer_stream_get_loop_count(struct mixer_stream *ch);
int mixer_stream_set_loop_start_pos(struct mixer_stream *ch, int pos);
int mixer_stream_set_loop_end_pos(struct mixer_stream *ch, int pos);
int mixer_stream_fade(struct mixer_stream *ch, int time, int volume, bool stop);
int mixer_stream_stop_fade(struct mixer_stream *ch);
int mixer_stream_is_fading(struct mixer_stream *ch);
int mixer_stream_pause(struct mixer_stream *ch);
int mixer_stream_restart(struct mixer_stream *ch);
int mixer_stream_is_paused(struct mixer_stream *ch);
int mixer_stream_get_pos(struct mixer_stream *ch);
int mixer_stream_get_length(struct mixer_stream *ch);
int mixer_stream_get_sample_pos(struct mixer_stream *ch);
int mixer_stream_get_sample_length(struct mixer_stream *ch);
int mixer_stream_seek(struct mixer_stream *ch, int pos);
int mixer_stream_reverse_LR(struct mixer_stream *ch);
int mixer_stream_get_volume(struct mixer_stream *ch);
int mixer_stream_get_time_length(struct mixer_stream *ch);

#endif /* AI5_MIXER_H */
