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

void audio_init(void);
#ifdef USE_SDL_MIXER
void audio_update(void);
#endif

void audio_bgm_play(const char *name, bool check_playing);
void audio_bgm_stop(void);
void audio_bgm_set_volume(uint8_t vol);
void audio_bgm_fade(uint8_t vol, int t, bool stop, bool sync);
void audio_bgm_fade_out(uint8_t vol, bool sync);
void audio_bgm_restore_volume(void);
bool audio_bgm_is_playing(void);
bool audio_bgm_is_fading(void);

void audio_se_play(const char *name);
void audio_se_stop(void);
bool audio_se_is_playing(void);
void audio_se_fade(uint8_t vol, int t, bool stop, bool sync);

void audio_voice_play(const char *name);
void audio_voice_stop(void);
bool audio_voice_is_playing(void);

void audio_aux_play(const char *name, int no);
void audio_aux_stop(int no);
void audio_aux_fade_out(uint8_t vol, bool sync, int no);

#endif // AI5_AUDIO_H
