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

#ifndef AI5_SDL2_MAP_H
#define AI5_SDL2_MAP_H

enum map_location_mode {
	// Map.get_location is disabled
	MAP_LOCATION_DISABLED = 0,
	// Map.get_location is enabled
	MAP_LOCATION_ENABLED = 1,
	// Map.get_location is disabled after a valid location is returned
	MAP_LOCATION_MODE_ONESHOT = 2,
	// Map.get_location returns -1 if the previous location would be returned
	MAP_LOCATION_MODE_NO_REPEAT = 3,
};

enum map_sprite_flag {
	MAP_SP_ENABLED     = 0x01,
	MAP_SP_TRIGGER     = 0x02,
	MAP_SP_NONCHARA    = 0x04,
	MAP_SP_TRIGGERABLE = 0x08,
	MAP_SP_COLLIDES    = 0x10,
	MAP_SP_CAMERA      = 0x20,
	MAP_SP_PLAYER      = 0x40,
};

enum map_direction {
	MAP_UP = 0,
	MAP_DOWN = 1,
	MAP_LEFT = 2,
	MAP_RIGHT = 3,
};

enum map_diagonal {
	MAP_UP_LEFT = 4,
	MAP_UP_RIGHT = 5,
	MAP_DOWN_LEFT = 6,
	MAP_DOWN_RIGHT = 7,
};

void map_load_bitmap(const char *name, unsigned col, unsigned row, unsigned which);
void map_load_palette(const char *name, unsigned which);
void map_load_tilemap(void);
void map_load_tiles(void);
void map_draw_tiles(void);
void map_load_sprite_scripts(void);
void map_set_sprite_script(unsigned sp_no, unsigned script_no);
void map_set_sprite_state(unsigned no, uint8_t state);
void map_set_sprite_anim(unsigned sp_no, uint8_t anim_no);
void map_spawn_sprite(unsigned spawn_no, unsigned sp_no, uint8_t anim_no);
void map_rewind_sprite_pos(unsigned sp_no, unsigned d);
void map_place_sprites(void);
void map_exec_sprites(void);
void map_exec_sprites_and_redraw(void);
void map_move_sprite(unsigned sp_no, enum map_direction dir);
void map_path_sprite(unsigned sp_no, unsigned tx, unsigned ty);
void map_stop_pathing(void);
void map_get_pathing(void);
void map_set_location_mode(enum map_location_mode mode);
void map_get_location(void);

#endif // AI5_SDL2_MAP_H
