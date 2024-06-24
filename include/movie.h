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

#ifndef AI5_SDL2_MOVIE_H
#define AI5_SDL2_MOVIE_H

#include <SDL.h>

struct movie_context;
struct archive_data;

struct movie_context *movie_load(const char *movie_path, const char *audio_path, int w, int h);
struct movie_context *movie_load_arc(struct archive_data *movie, struct archive_data *audio, int w, int h);
void movie_free(struct movie_context *mc);
bool movie_play(struct movie_context *mc);
int movie_draw(struct movie_context *mc);
bool movie_is_end(struct movie_context *mc);
bool movie_seek_video(struct movie_context *mc, unsigned ts);
int movie_get_position(struct movie_context *mc);
bool movie_set_volume(struct movie_context *mc, int volume);
uint8_t *movie_get_pixels(struct movie_context *mc, unsigned *stride);

#endif // AI5_SDL2_MOVIE_H
