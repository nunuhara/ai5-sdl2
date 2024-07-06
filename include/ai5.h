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

#ifndef AI5_H
#define AI5_H

#include "nulib.h"

#ifdef NDEBUG
#define BUILD_RELEASE
#else
#define BUILD_DEBUG
#endif

typedef char* string;

struct config {
	// [CONFIG]
	string title;
	string start_mes;
	bool voice;
	bool voicesub;
	bool sound;
	bool music;
	bool effect;
	bool screen;
	bool notify;

	// [FILE]
	struct {
		struct { bool arc; string name; } bg;
		struct { bool arc; string name; } mes;
		struct { bool arc; string name; } bgm;
		struct { bool arc; string name; } voice;
		struct { bool arc; string name; } voicesub;
		struct { bool arc; string name; } effect;
		struct { bool arc; string name; } data;
		struct { bool arc; string name; } priv;
		string cddrv;
	} file;

	// [GRAPHICS]
	struct { bool bg_type; } graphics;

	// [MES]
	struct { bool mes_type; } mes;

	// [DATA]
	struct { bool data_type; } data;

	// [MONITOR]
	struct { int screen; } monitor;

	// [VOLUME] / [VOLUMEINFO]
	struct {
		int music;
		int se;
		int effect;
		int voice;
	} volume;

	// [SOUNDINFO]
	struct {
		bool music;
		bool effect;
		bool voice;
	} soundinfo;

	char *exe_path;
	char *font_path;
	int font_face;
	unsigned progressive_frame_time;
	unsigned msg_skip_delay;
	bool texthook_clipboard;
	bool texthook_stdout;
	bool map_no_wallslide;
};

extern struct config config;

extern bool yuno_eng;

#endif // AI5_H
