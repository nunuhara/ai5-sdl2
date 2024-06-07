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

#include "nulib.h"
#include "nulib/little_endian.h"
#include "ai5/arc.h"
#include "ai5/ccd.h"
#include "ai5/mes.h"

#include "asset.h"
#include "cursor.h"
#include "gfx_private.h"
#include "input.h"
#include "map.h"
#include "memory.h"
#include "vm.h"

/*
 * XXX: This implementation is specific to Doukyuusei in many ways.
 *      Significant refactoring would be required to make it usable
 *      for other games.
 */

#if 0
#define MAP_LOG(...) NOTICE(__VA_ARGS__)
#else
#define MAP_LOG(...)
#endif

#define MAP_MAX_TILES 11655

#define MAP_FRAME_TIME 54

#define NO_TILE 0xffff
#define NO_LOCATION 0xffff

// static map tile data
struct map_tile {
	uint16_t bg;
	uint16_t fg;
	bool collides;
};

// on-screen tile data
struct tile {
	uint16_t bg;  // background tile index
	uint16_t fg;  // foreground tile index
	uint16_t sp;  // sprite tile index
	uint16_t sp2; // secondary sprite tile index
	bool fg_cha;  // foreground tile is in bmp_cha
};

struct sprite_pos {
	unsigned tx;
	unsigned ty;
	uint8_t frame;
};

struct map_pos {
	uint16_t x, y;
};

struct path_data {
	struct map_pos pred;
	unsigned g_score : 16;
	unsigned f_score : 15;
	unsigned not_in_frontier : 1;
};

static struct {
	struct {
		unsigned tx;
		unsigned ty;
		unsigned tw;
		unsigned th;
	} screen;
	// size of the map
	unsigned cols;
	unsigned rows;
	// camera offset from the player sprite
	unsigned cam_off_tx;
	unsigned cam_off_ty;
	// Map.get_location state
	enum map_location_mode location_mode;
	bool get_location_enabled;
	uint16_t prev_location;
	// sprite list
	vector_t(struct ccd_sprite) sprites;
	// player position history
	struct sprite_pos pos_history[32];
	unsigned pos_history_ptr;
	// static map tile data
	struct map_tile tile_data[MAP_MAX_TILES];
	// on-screen tiles (dynamic)
	struct tile tiles[480][640];
	// frame rate timer
	vm_timer_t timer;
	// pathing data
	struct {
		bool active;
		struct map_pos goal;
		struct path_data tiles[480][640];
		vector_t(struct map_pos) frontier;
		vector_t(struct map_pos) path;
		unsigned path_ptr;
		struct {
			struct ccd_sprite *sp;
			uint8_t state;
			uint8_t cmd;
			uint8_t repetitions;
		} saved;
	} path;
	// Tile graphics (bitmaps). There are separate bitmaps for
	// map and sprite tiles.
	uint8_t bmp_map[1280 * 960];
	uint8_t bmp_cha[640 * 96 * 2];
	SDL_Color pal_map[256];
	SDL_Color pal_cha[256];
} map = {0};

// Bitmaps {{{

static void copy_to_bmp(uint8_t *bmp, unsigned bmp_size, unsigned off, struct archive_data *file)
{
	unsigned size = file->size;
	if (off + size > bmp_size) {
		WARNING("Tried to write past the end of bitmap");
		size = bmp_size - off;
	}
	memcpy(bmp + off, file->data, size);
}
#define copy_to_bmp_map(off, file) copy_to_bmp(map.bmp_map, 0x12c000, off, file)
#define copy_to_bmp_cha(off, file) copy_to_bmp(map.bmp_cha, 0x1e000, off, file)

void map_load_bitmap(const char *name, unsigned col, unsigned row, unsigned which)
{
	MAP_LOG("map_load_bitmap(\"%s\",%u,%u,%u)", name, col, row, which);
	struct archive_data *file = asset_data_load(name);
	if (!file) {
		WARNING("Failed to load map bitmap: \"%s\"", name);
		return;
	}

	switch (which) {
	case 0:  copy_to_bmp_map(row * 1280 + col, file); break;
	case 1:  copy_to_bmp_cha(0xf000 + row * 640 + col, file); break;
	case 3:  copy_to_bmp_cha(0, file); break;
	default: copy_to_bmp_cha(0x7800 + col, file); break;
	}
}

void map_load_palette(const char *name, unsigned which)
{
	MAP_LOG("map_load_palette(\"%s\",%u)", name, which);
	struct archive_data *file = asset_data_load(name);
	if (!file) {
		WARNING("Failed to load map palette: \"%s\"", name);
		return;
	}
	if (file->size < 512)
		WARNING("Incomplete palette (%uB)", file->size);
	else if (file->size > 512)
		WARNING("Palette file is larger than expected (%uB)", file->size);

	for (unsigned i = 0; i * 2 < file->size && i < 256; i++) {
		if (which == 1)
			map.pal_cha[i] = gfx_decode_bgr555(le_get16(file->data, i * 2));
		else
			map.pal_map[i] = gfx_decode_bgr555(le_get16(file->data, i * 2));
	}
}

// Bitmaps }}}
// Tiles {{{

/* memory.map_data has the following structure:
 *
 * struct map_data {
 *     uint32_t mpx_ptr;
 *     uint32_t screen_tx;
 *     uint32_t screen_ty;
 *     uint32_t cols;
 *     uint32_t rows;
 *     uint32_t uk[2];
 *     uint32_t screen_tw;
 *     uint32_t screen_th;
 *     uint32_t cam_off_tx;
 *     uint32_t cam_off_ty;
 *     uint32_t uk[2];
 * };
 */
static void update_map_data(void)
{
	map.screen.tx  = le_get32(memory.map_data, 4);
	map.screen.ty  = le_get32(memory.map_data, 8);
	map.screen.tw  = le_get32(memory.map_data, 28);
	map.screen.th  = le_get32(memory.map_data, 32);
	map.cam_off_tx = le_get32(memory.map_data, 36);
	map.cam_off_ty = le_get32(memory.map_data, 40);
}

void map_load_tilemap(void)
{
	MAP_LOG("map_load_tilemap()");
	// XXX: AI5WIN.EXE exposes its own internal memory to the game through
	//      System.map_data, but the game doesn't read from it anyway, and
	//      only writes to it immediately before calling Map.load_tilemap.
	//      So we just load it into a private struct.
	update_map_data();

	uint32_t mpx_off = mem_get_sysvar32(mes_sysvar32_mpx_offset);
	map.cols = le_get16(memory.file_data, mpx_off);
	map.rows = le_get16(memory.file_data, mpx_off + 2);
	map.location_mode = MAP_LOCATION_DISABLED;
	map.get_location_enabled = false;
	map.prev_location = NO_LOCATION;

	if (map.rows * map.cols > MAP_MAX_TILES)
		VM_ERROR("too many tiles in mpx: %ux%u", map.cols, map.rows);

	// XXX: AI5WIN.EXE reads tiles directly from System.mpx_offset in Map.load_tiles,
	//      but once again the game doesn't seem to access this data ever so we just
	//      load it into a private array.
	uint8_t *mpx = memory.file_data + mpx_off;
	for (unsigned row = 0; row < map.rows; row++) {
		for (unsigned col = 0; col < map.cols; col++) {
			uint8_t *tdata = mpx + 4 + (map.cols * row + col) * 5;
			map.tile_data[row * map.cols + col] = (struct map_tile) {
				.bg = le_get16(tdata, 0),
				.fg = le_get16(tdata, 2),
				.collides = !!tdata[4]
			};
		}
	}
}

void map_load_tiles(void)
{
	// load on-screen tiles into tiles array
	for (unsigned row = 0; row < map.screen.th; row++) {
		for (unsigned col = 0; col < map.screen.tw; col++) {
			unsigned i = (row + map.screen.ty) * map.cols + map.screen.tx + col;
			map.tiles[row][col].bg = map.tile_data[i].bg;
			map.tiles[row][col].fg = map.tile_data[i].fg;
			map.tiles[row][col].sp = NO_TILE;
			map.tiles[row][col].sp2 = NO_TILE;
			map.tiles[row][col].fg_cha = false;
		}
	}
}

static int bmp_offset(unsigned tile_no, int w, int h)
{
	unsigned ty = tile_no / (w / 16);
	unsigned tx = tile_no % (w / 16);
	unsigned x = tx * 16;
	unsigned y = ty * 16;
	return ((h - y) - 1) * w + x;
}

static void blit_tile(SDL_Surface *dst, int x, int y, uint8_t *bmp, SDL_Color pal[256],
		unsigned tile_no, unsigned bmp_w, unsigned bmp_h)
{
	uint8_t *dst_row = dst->pixels + (y * dst->pitch + x * 3);
	uint8_t *bmp_row = bmp + bmp_offset(tile_no, bmp_w, bmp_h);

	for (int row = 0; row < 16; row++, dst_row += dst->pitch, bmp_row -= bmp_w) {
		uint8_t *dst_p = dst_row;
		uint8_t *bmp_p = bmp_row;
		for (int col = 0; col < 16; col++, dst_p += 3, bmp_p++) {
			SDL_Color *c = &pal[*bmp_p];
			dst_p[0] = c->r;
			dst_p[1] = c->g;
			dst_p[2] = c->b;
		}
	}
}

static void blit_tile_masked(SDL_Surface *dst, int x, int y, uint8_t *bmp, SDL_Color pal[256],
		unsigned tile_no, unsigned bmp_w, unsigned bmp_h)
{
	uint8_t *dst_row = dst->pixels + (y * dst->pitch + x * 3);
	uint8_t *bmp_row = bmp + bmp_offset(tile_no, bmp_w, bmp_h);

	for (int row = 0; row < 16; row++, dst_row += dst->pitch, bmp_row -= bmp_w) {
		uint8_t *dst_p = dst_row;
		uint8_t *bmp_p = bmp_row;
		for (int col = 0; col < 16; col++, dst_p += 3, bmp_p++) {
			if (*bmp_p == 0)
				continue;
			SDL_Color *c = &pal[*bmp_p];
			dst_p[0] = c->r;
			dst_p[1] = c->g;
			dst_p[2] = c->b;
		}
	}
}

static void draw_tile(struct tile *tile, int x, int y)
{
	SDL_Surface *dst = gfx_get_surface(0);
	if (SDL_MUSTLOCK(dst))
		SDL_CALL(SDL_LockSurface, dst);

	if (tile->bg != NO_TILE) {
		blit_tile(dst, x, y, map.bmp_map, map.pal_map, tile->bg, 1280, 960);
	}
	if (tile->sp != NO_TILE) {
		blit_tile_masked(dst, x, y, map.bmp_cha, map.pal_cha, tile->sp, 640, 192);
		if (tile->sp2 != NO_TILE) {
			blit_tile_masked(dst, x, y, map.bmp_cha, map.pal_cha, tile->sp2, 640, 192);
		}
	}
	if (tile->fg != NO_TILE) {
		if (tile->fg_cha) {
			blit_tile_masked(dst, x, y, map.bmp_cha, map.pal_cha, tile->fg, 640, 192);
		} else {
			blit_tile_masked(dst, x, y, map.bmp_map, map.pal_map, tile->fg, 1280, 960);
		}
	}

	if (SDL_MUSTLOCK(dst))
		SDL_UnlockSurface(dst);
}

void map_draw_tiles(void)
{
	for (unsigned row = 0; row < map.screen.th; row++) {
		for (unsigned col = 0; col < map.screen.tw; col++) {
			draw_tile(&map.tiles[row][col], col * 16, row * 16);
		}
	}

	// copy area hidden by status bar to surface 7
	gfx_copy(0, 448, 640, 32, 0, 0, 1248, 7);
	// draw status bar
	gfx_copy(0, 106, 640, 32, 7, 0, 448, 0);
	gfx_dirty(0);

	// XXX: If shift is held down, we double the frame rate.
	//      This is not what AI5WIN.EXE does (it doubles the amount of movement that
	//      occurs per-frame), but the end result is the same, and this method results
	//      in smoother movement.
	vm_timer_tick(&map.timer, input_down(INPUT_SHIFT) ? MAP_FRAME_TIME/2 : MAP_FRAME_TIME);
}

// Tiles }}}
// Sprites {{{

static uint8_t *get_ccd(void)
{
	return memory.file_data + mem_get_sysvar32(mes_sysvar32_ccd_offset);
}

static bool check_sprite_no(unsigned no)
{
	if (no >= vector_length(map.sprites)) {
		WARNING("Invalid sprite index: %u", no);
		return false;
	}
	return true;
}

static struct ccd_sprite *get_sprite(unsigned no)
{
	if (!check_sprite_no(no))
		return NULL;
	return &vector_A(map.sprites, no);
}

void map_load_sprite_scripts(void)
{
	MAP_LOG("map_load_sprite_scripts()");
	vector_destroy(map.sprites);
	vector_init(map.sprites);

	// XXX: AI5WIN.EXE modifies the ccd data in user-visible memory, but the
	//      game doesn't seem to access this memory so we just load it into
	//      private VM memory.
	uint8_t *ccd = get_ccd();
	uint16_t script_off = le_get16(ccd, 2);

	for (unsigned i = 0; true; i++) {
		struct ccd_sprite sp = ccd_load_sprite(i, ccd);
		if (sp.state == 0xff)
			break;
		sp.script_ptr = le_get16(ccd, script_off + sp.script_index * 2);
		sp.script_cmd = ccd[sp.script_ptr] >> 4;
		sp.script_repetitions = (ccd[sp.script_ptr] & 0xf) + 1;
		sp.script_ptr++;
		vector_set(struct ccd_sprite, map.sprites, i, sp);
	}
}

void map_set_sprite_script(unsigned sp_no, unsigned script_no)
{
	MAP_LOG("map_set_sprite_script(%u,%u)", sp_no, script_no);
	struct ccd_sprite *sp = get_sprite(sp_no);
	if (!sp)
		return;

	uint8_t *ccd = get_ccd();
	uint16_t script_off = le_get16(ccd, 2);
	sp->script_index = script_no;
	sp->script_ptr = le_get16(ccd, script_off + script_no * 2);
	sp->script_cmd = ccd[sp->script_ptr] >> 4;
	sp->script_repetitions = (ccd[sp->script_ptr] & 0xf) + 1;
	sp->script_ptr++;
}

void map_set_sprite_anim(unsigned sp_no, uint8_t anim_no)
{
	MAP_LOG("map_set_sprite_anim(%u,%u)", sp_no, (unsigned)anim_no);
	if (!check_sprite_no(sp_no))
		return;
	vector_A(map.sprites, sp_no).frame = anim_no << 4;
}

void map_spawn_sprite(unsigned spawn_no, unsigned sp_no, uint8_t anim_no)
{
	MAP_LOG("map_spawn_sprite(%u,%u,%u)", spawn_no, sp_no, (unsigned)anim_no);
	struct ccd_sprite *sp = get_sprite(sp_no);
	if (!sp)
		return;

	struct ccd_spawn spawn = ccd_load_spawn(spawn_no, get_ccd());

	uint32_t screen_tx = spawn.screen_x;
	uint32_t screen_ty = spawn.screen_y;

	// adjust screen location if spawn is near edge of map
	if (screen_tx + map.screen.tw >= map.cols)
		screen_tx = map.cols - map.screen.tw;
	if (screen_ty + map.screen.th >= map.rows)
		screen_ty = map.rows - map.screen.th;

	map.screen.tx = screen_tx;
	map.screen.ty = screen_ty;
	sp->x = spawn.sprite_x;
	sp->y = spawn.sprite_y;
	sp->frame = anim_no << 4;

	for (int i = 0; i < ARRAY_SIZE(map.pos_history); i++) {
		map.pos_history[i].tx = sp->x;
		map.pos_history[i].ty = sp->y;
		map.pos_history[i].frame = sp->frame;
	}
	map.pos_history_ptr = 0;
}

void map_set_sprite_state(unsigned no, uint8_t state)
{
	MAP_LOG("map_set_sprite_state(%u,0x%x)", no, (unsigned)state);
	if (!check_sprite_no(no))
		return;
	vector_A(map.sprites, no).state = state;
}

#define TILES_PER_FRAME 9
#define FRAMES_PER_ANIM 12
#define ANIMS_PER_SHEET 4

#define BYTES_PER_FRAME (TILES_PER_FRAME * 2)
#define BYTES_PER_ANIM  (BYTES_PER_FRAME * FRAMES_PER_ANIM)
#define BYTES_PER_SHEET (BYTES_PER_ANIM * ANIMS_PER_SHEET)

static void place_sprite(struct ccd_sprite *sp)
{
	uint8_t *ccd = get_ccd();
	unsigned sp_tile_off = sp->no * BYTES_PER_SHEET
		+ (sp->frame >> 4) * BYTES_PER_ANIM
		+ (sp->frame & 0xf) * BYTES_PER_FRAME;
	uint8_t *sp_tiles = ccd + le_get16(ccd, 6) + sp_tile_off;

	unsigned off_tx = sp->x - map.screen.tx;
	unsigned off_ty = sp->y - map.screen.ty;
	unsigned screen_tw = map.screen.tw;
	unsigned screen_th = map.screen.th;
	for (unsigned row = 0, sp_t = 0; row < sp->h && off_ty + row < screen_th; row++) {
		for (unsigned col = 0; col < sp->w && off_tx + col < screen_tw; col++, sp_t++) {
			uint16_t tile_no = le_get16(sp_tiles, sp_t * 2);
			unsigned char_tx = off_tx + col;
			unsigned char_ty = off_ty + row;
			if (sp->state & MAP_SP_NONCHARA) {
				map.tiles[char_ty][char_tx].fg = tile_no;
				map.tiles[char_ty][char_tx].fg_cha = true;
			} else {
				if (map.tiles[char_ty][char_tx].sp == NO_TILE) {
					map.tiles[char_ty][char_tx].sp = tile_no;
				} else if (map.tiles[char_ty][char_tx].sp2 == NO_TILE) {
					if (vector_A(map.sprites, 0).y < sp->y) {
						map.tiles[char_ty][char_tx].sp2 = tile_no;
					} else {
						map.tiles[char_ty][char_tx].sp2 =
							map.tiles[char_ty][char_tx].sp;
						map.tiles[char_ty][char_tx].sp = tile_no;
					}
				}
			}
		}
	}
}

void map_place_sprites(void)
{
	struct ccd_sprite *sp;
	vector_foreach_p(sp, map.sprites) {
		if (sp->state & MAP_SP_ENABLED)
			place_sprite(sp);
	}
}

static void sprite_pos_history_push(struct ccd_sprite *sp)
{
	map.pos_history[map.pos_history_ptr] = (struct sprite_pos) {
		.tx = sp->x,
		.ty = sp->y,
		.frame = sp->frame
	};
	map.pos_history_ptr = (map.pos_history_ptr + 1) % ARRAY_SIZE(map.pos_history);
}

static uint16_t sprite_rewind_pos(struct ccd_sprite *sp, unsigned d)
{
	unsigned i = ((map.pos_history_ptr - 1) - d * 2) % ARRAY_SIZE(map.pos_history);
	sp->x = map.pos_history[i].tx;
	sp->y = map.pos_history[i].ty;
	sp->frame = map.pos_history[i].frame;
	return 0;
}

void map_rewind_sprite_pos(unsigned sp_no, unsigned d)
{
	MAP_LOG("map_rewind_sprite_pos(%u,%u)", sp_no, d);
	struct ccd_sprite *sp = get_sprite(sp_no);
	if (!sp)
		return;
	sprite_rewind_pos(sp, d);
}

/*
 * Check if any tiles within the given rectangle have collision enabled.
 * The collision state is additionally written to `result` for each tile.
 * This allows the move functions to decide whether or not to hook around
 * corners.
 *
 * Note that this is NOT what the original game does: the original game
 * has your character slide up/right along walls, which makes navigating
 * with the keyboard very annoying.
 */
static bool map_tiles_collide(unsigned tx, unsigned ty, unsigned tw, unsigned th,
		bool *result)
{
	bool r = false;
	for (unsigned row = 0, i = 0; row < th; row++) {
		struct map_tile *t = &map.tile_data[(ty + row) * map.cols + tx];
		for (unsigned col = 0; col < tw; col++, t++, i++) {
			if ((result[i] = t->collides))
				r = true;
		}
	}
	return r;
}

static bool sprite_can_move_up(struct ccd_sprite *sp, bool *r)
{
	if (sp->y == 0) {
		r[0] = true;
		r[1] = true;
		r[2] = true;
		return false;
	}
	return !map_tiles_collide(sp->x, sp->y, 3, 1, r);
}

static bool sprite_can_move_down(struct ccd_sprite *sp, bool *r)
{
	if (sp->y + 3 >= map.rows) {
		r[0] = true;
		r[1] = true;
		r[2] = true;
		return false;
	}
	return !map_tiles_collide(sp->x, sp->y + 3, 3, 1, r);
}

static bool sprite_can_move_left(struct ccd_sprite *sp, bool *r)
{
	if (sp->x == 0) {
		r[0] = true;
		r[1] = true;
		return false;
	}
	return !map_tiles_collide(sp->x - 1, sp->y + 1, 1, 2, r);
}

static bool sprite_can_move_right(struct ccd_sprite *sp, bool *r)
{
	if (sp->x + 3 >= map.cols) {
		r[0] = true;
		r[1] = true;
		return false;
	}
	return !map_tiles_collide(sp->x + 3, sp->y + 1, 1, 2, r);
}

static void sprite_advance_frame(struct ccd_sprite *sp, enum map_direction dir)
{
	sp->frame = (sp->frame & 0xf) + 1;
	if (sp->frame >= 12)
		sp->frame = 1;
	sp->frame |= (dir << 4);
}

static uint16_t sprite_move_left(struct ccd_sprite *sp, bool advance);
static uint16_t sprite_move_right(struct ccd_sprite *sp, bool advance);

static void _sprite_move_up(struct ccd_sprite *sp)
{
	if (sp->y > 0) {
		sp->y--;
		if (sp->state & MAP_SP_CAMERA) {
			if (sp->y < map.cam_off_ty + map.screen.ty && map.screen.ty > 0)
				map.screen.ty--;
		}
	}
}

static uint16_t sprite_move_up(struct ccd_sprite *sp, bool advance)
{
	if (advance)
		sprite_advance_frame(sp, MAP_UP);

	// check if sprite would collide with terrain
	bool result[3];
	if (sp->state & MAP_SP_COLLIDES && !sprite_can_move_up(sp, result)) {
		if (!result[0])
			return sprite_move_left(sp, false);
		if (!result[2])
			return sprite_move_right(sp, false);
		return 0xffff;
	}

	// move sprite
	_sprite_move_up(sp);
	return 0;
}

static void _sprite_move_down(struct ccd_sprite *sp)
{
	if (sp->y + sp->h < map.rows - 1) {
		sp->y++;
		if (sp->state & MAP_SP_CAMERA) {
			int max_ty = map.rows - map.screen.th;
			if (sp->y > map.cam_off_ty + map.screen.ty && map.screen.ty < max_ty)
				map.screen.ty++;
		}
	}
}

static uint16_t sprite_move_down(struct ccd_sprite *sp, bool advance)
{
	if (advance)
		sprite_advance_frame(sp, MAP_DOWN);

	// check if sprite would collide with terrain
	bool result[3];
	if (sp->state & MAP_SP_COLLIDES && !sprite_can_move_down(sp, result)) {
		if (!result[0])
			return sprite_move_left(sp, false);
		if (!result[2])
			return sprite_move_right(sp, false);
		return 0xffff;
	}

	// move sprite
	_sprite_move_down(sp);
	return 0;
}

static void _sprite_move_left(struct ccd_sprite *sp)
{
	if (sp->x > 0) {
		sp->x--;
		if (sp->state & MAP_SP_CAMERA) {
			if (sp->x < map.cam_off_tx + map.screen.tx && map.screen.tx > 0)
				map.screen.tx--;
		}
	}
}

static uint16_t sprite_move_left(struct ccd_sprite *sp, bool advance)
{
	if (advance)
		sprite_advance_frame(sp, MAP_LEFT);

	// check if sprite would collide with terrain
	bool result[2];
	if (sp->state & MAP_SP_COLLIDES && !sprite_can_move_left(sp, result)) {
		if (!result[0])
			return sprite_move_up(sp, false);
		if (!result[1])
			return sprite_move_down(sp, false);
		return 0xffff;
	}

	// move sprite
	_sprite_move_left(sp);
	return 0;
}

static void _sprite_move_right(struct ccd_sprite *sp)
{
	if (sp->x + sp->w < map.cols - 1) {
		sp->x++;
		if (sp->state & MAP_SP_CAMERA) {
			int max_tx = map.cols - map.screen.tw;
			if (sp->x > map.cam_off_tx + map.screen.tx && map.screen.tx < max_tx)
				map.screen.tx++;
		}
	}
}

static uint16_t sprite_move_right(struct ccd_sprite *sp, bool advance)
{
	if (advance)
		sprite_advance_frame(sp, MAP_RIGHT);

	// check if sprite would collide with terrain
	bool result[2];
	if (sp->state & MAP_SP_COLLIDES && !sprite_can_move_right(sp, result)) {
		if (!result[0])
			return sprite_move_up(sp, false);
		if (!result[1])
			return sprite_move_down(sp, false);
		return 0xffff;
	}

	// move sprite
	_sprite_move_right(sp);
	return 0;
}

static uint16_t sprite_move_up_left(struct ccd_sprite *sp, bool advance)
{
	if (advance)
		sprite_advance_frame(sp, MAP_UP);

	// check if sprite would collide with terrain
	if (sp->state & MAP_SP_COLLIDES) {
		bool r[3];
		if (!sprite_can_move_up(sp, r)) {
			if (!sprite_can_move_left(sp, r))
				return 0xffff;
			_sprite_move_left(sp);
			return 0;
		}
		_sprite_move_up(sp);
		if (sprite_can_move_left(sp, r))
			_sprite_move_left(sp);
		return 0;
	}

	_sprite_move_up(sp);
	_sprite_move_left(sp);
	return 0;
}

static uint16_t sprite_move_up_right(struct ccd_sprite *sp, bool advance)
{
	if (advance)
		sprite_advance_frame(sp, MAP_UP);

	// check if sprite would collide with terrain
	if (sp->state & MAP_SP_COLLIDES) {
		bool r[3];
		if (!sprite_can_move_up(sp, r)) {
			if (!sprite_can_move_right(sp, r))
				return 0xffff;
			_sprite_move_right(sp);
			return 0;
		}
		_sprite_move_up(sp);
		if (sprite_can_move_right(sp, r))
			_sprite_move_right(sp);
		return 0;
	}

	_sprite_move_up(sp);
	_sprite_move_right(sp);
	return 0;
}

static uint16_t sprite_move_down_left(struct ccd_sprite *sp, bool advance)
{
	if (advance)
		sprite_advance_frame(sp, MAP_DOWN);

	// check if sprite would collide with terrain
	if (sp->state & MAP_SP_COLLIDES) {
		bool r[3];
		if (!sprite_can_move_down(sp, r)) {
			if (!sprite_can_move_left(sp, r))
				return 0xffff;
			_sprite_move_left(sp);
			return 0;
		}
		_sprite_move_down(sp);
		if (sprite_can_move_left(sp, r))
			_sprite_move_left(sp);
		return 0;
	}

	_sprite_move_down(sp);
	_sprite_move_left(sp);
	return 0;
}

static uint16_t sprite_move_down_right(struct ccd_sprite *sp, bool advance)
{
	if (advance)
		sprite_advance_frame(sp, MAP_DOWN);

	// check if sprite would collide with terrain
	if (sp->state & MAP_SP_COLLIDES) {
		bool r[3];
		if (!sprite_can_move_down(sp, r)) {
			if (!sprite_can_move_right(sp, r))
				return 0xffff;
			_sprite_move_right(sp);
			return 0;
		}
		_sprite_move_down(sp);
		if (sprite_can_move_right(sp, r))
			_sprite_move_right(sp);
		return 0;
	}

	_sprite_move_down(sp);
	_sprite_move_right(sp);
	return 0;
}

static void sprite_move_path(struct ccd_sprite *sp)
{
	if (map.path.path_ptr == 0) {
		map_stop_pathing();
		return;
	}

	if (input_down(INPUT_CANCEL) && mem_get_var4(4067)) {
		mem_set_var32(18, 1);
		map_stop_pathing();
		return;
	}

	map.path.path_ptr--;
	struct map_pos next = vector_A(map.path.path, map.path.path_ptr);
	if (next.y < sp->y) {
		if (next.x < sp->x)
			sprite_move_up_left(sp, true);
		else if (next.x > sp->x)
			sprite_move_up_right(sp, true);
		else
			sprite_move_up(sp, true);
	} else if (next.y > sp->y) {
		if (next.x < sp->x)
			sprite_move_down_left(sp, true);
		else if (next.x > sp->x)
			sprite_move_down_right(sp, true);
		else
			sprite_move_down(sp, true);
	} else if (next.x < sp->x) {
		sprite_move_left(sp, true);
	} else if (next.x > sp->x) {
		sprite_move_right(sp, true);
	}
	if (sp->x != next.x || sp->y != next.y) {
		WARNING("pathed to wrong tile?");
		sp->x = next.x;
		sp->y = next.y;
	}
	mem_set_var16(3, sp->frame >> 4);
	sprite_pos_history_push(sp);
}

enum {
	SP_INPUT_UP = 1,
	SP_INPUT_DOWN = 2,
	SP_INPUT_LEFT = 4,
	SP_INPUT_RIGHT = 8,
};

static unsigned map_get_keyboard_inputs(void)
{
	unsigned inputs = 0;
	if (input_down(INPUT_UP))
		inputs |= SP_INPUT_UP;
	if (input_down(INPUT_DOWN))
		inputs |= SP_INPUT_DOWN;
	if (input_down(INPUT_LEFT))
		inputs |= SP_INPUT_LEFT;
	if (input_down(INPUT_RIGHT))
		inputs |= SP_INPUT_RIGHT;
	if (input_down(INPUT_CANCEL) && !inputs)
		return 0xfffe;
	return inputs;
}

static unsigned map_get_mouse_inputs(struct ccd_sprite *sp)
{
	unsigned cur_x, cur_y;
	cursor_get_pos(&cur_x, &cur_y);

	unsigned sp_x = (sp->x - map.screen.tx) * 16;
	unsigned sp_y = ((sp->y + 1) - map.screen.ty) * 16;

	unsigned inputs = 0;
	if (cur_y < sp_y)
		inputs |= SP_INPUT_UP;
	if (cur_y > sp_y + 32)
		inputs |= SP_INPUT_DOWN;
	if (cur_x < sp_x)
		inputs |= SP_INPUT_LEFT;
	if (cur_x > sp_x + 48)
		inputs |= SP_INPUT_RIGHT;

	return inputs;
}

static uint16_t sprite_do_handle_input(struct ccd_sprite *sp, unsigned inputs)
{
	unsigned tx = sp->x;
	unsigned ty = sp->y;

	switch (inputs) {
	case SP_INPUT_UP: sprite_move_up(sp, true); break;
	case SP_INPUT_DOWN: sprite_move_down(sp, true); break;
	case SP_INPUT_LEFT: sprite_move_left(sp, true); break;
	case SP_INPUT_RIGHT: sprite_move_right(sp, true); break;
	case SP_INPUT_UP | SP_INPUT_LEFT: sprite_move_up_left(sp, true); break;
	case SP_INPUT_UP | SP_INPUT_RIGHT: sprite_move_up_right(sp, true); break;
	case SP_INPUT_DOWN | SP_INPUT_LEFT: sprite_move_down_left(sp, true); break;
	case SP_INPUT_DOWN | SP_INPUT_RIGHT: sprite_move_down_right(sp, true); break;
	case 0xfffe: return 0xfffe;
	default: return 0xffff;
	}

	if (sp->x != tx || sp->y != ty)
		sprite_pos_history_push(sp);
	return 0;

}

static uint16_t sprite_handle_input(struct ccd_sprite *sp)
{
	unsigned inputs;
	if (input_down(INPUT_ACTIVATE))
		inputs = map_get_mouse_inputs(sp);
	else
		inputs = map_get_keyboard_inputs();

	return sprite_do_handle_input(sp, inputs);
}

static uint16_t exec_sprite(struct ccd_sprite *sp)
{
	// load next command
	if (!sp->script_repetitions) {
		uint8_t *ccd = get_ccd();
		uint8_t *script = ccd + sp->script_ptr;
		if (*script == 0) {
			// loop
			uint16_t script_off = le_get16(ccd, 2);
			sp->script_ptr = le_get16(ccd, script_off + sp->script_index * 2);
			script = ccd + sp->script_ptr;
		}
		sp->script_ptr++;
		sp->script_cmd = *script >> 4;
		sp->script_repetitions = *script & 0xf;
	}

	//NOTICE("EXEC %x:%d", sp->script_cmd, sp->script_repetitions);

	// exec command
	uint16_t r = 0;
	switch (sp->script_cmd) {
	case 0:
		return 0; // ???
	case 2:
		sprite_move_up(sp, true);
		break;
	case 3:
		sprite_move_down(sp, true);
		break;
	case 4:
		sprite_move_left(sp, true);
		break;
	case 5:
		sprite_move_right(sp, true);
		break;
	case 6:
		r = sprite_rewind_pos(sp, sp->script_repetitions);
		break;
	case 13:
		sprite_move_path(sp);
		break;
	case 14:
		r = sprite_handle_input(sp);
		break;
	default:
		VM_ERROR("Unimplemented sprite command: %u\n"
				"sprite %d\n"
				"\tscript_index = %u\n"
				"\tscript_ptr = %u\n"
				"\tscript_cmd = %u\n"
				"\tscript_repetitions = %u\n",
				sp->script_cmd,
				(int)(sp - &vector_A(map.sprites, 0)),
				(unsigned)sp->script_index,
				(unsigned)sp->script_ptr,
				(unsigned)sp->script_cmd,
				(unsigned)sp->script_repetitions);
	}

	if ((r & 0xff) == 0) {
		if (sp->script_repetitions != 0xff)
			sp->script_repetitions--;
		if (map.location_mode == MAP_LOCATION_MODE_ONESHOT && sp->state & MAP_SP_PLAYER)
			map.get_location_enabled = true;
		return 0xffff;
	}
	if ((r & 0xff) == 0xff)
		return 0;
	return r;
}

static uint16_t exec_sprites(void)
{
	unsigned i = 0;
	uint16_t r = 0xffff;
	struct ccd_sprite *sp;
	vector_foreach_p(sp, map.sprites) {
		if (sp->state) {
			uint16_t v = exec_sprite(sp);
			if (sp->state & 0x40 && i == 0)
				r = v;
		}
		i++;
	}
	return r;
}

void map_exec_sprites(void)
{
	mem_set_var16(18, exec_sprites());
}

void map_exec_sprites_and_redraw(void)
{
	assert(vector_length(map.sprites) > 0);
	uint16_t r = exec_sprites();
	if (r) {
		map_load_tiles();
		map_place_sprites();
		map_draw_tiles();
		mem_set_var16(3, vector_A(map.sprites, 0).frame >> 4);
	}
	mem_set_var16(18, r);
}

void map_move_sprite(unsigned sp_no, enum map_direction dir)
{
	MAP_LOG("map_move_sprite(%u,%d)", sp_no, dir);
	struct ccd_sprite *sp = get_sprite(sp_no);
	if (!sp)
		return;

	switch (dir) {
	case MAP_UP: sprite_move_up(sp, true); break;
	case MAP_DOWN: sprite_move_down(sp, true); break;
	case MAP_LEFT: sprite_move_left(sp, true); break;
	case MAP_RIGHT: sprite_move_right(sp, true); break;
	default: WARNING("Invalid move direction: %d", dir);
	}
	sprite_pos_history_push(sp);
	mem_set_var16(18, 0); // ???
}

// Sprites }}}
// Location {{{

void map_set_location_mode(enum map_location_mode mode)
{
	map.location_mode = mode;
	map.get_location_enabled = mode != 0;
	map.prev_location = NO_LOCATION;
}

static struct ccd_sprite *get_player(void)
{
	struct ccd_sprite *sp;
	vector_foreach_p(sp, map.sprites) {
		if (sp->state & MAP_SP_PLAYER)
			return sp;
	}
	return NULL;
}

static uint16_t get_sprite_location(struct ccd_sprite *sp)
{
	uint8_t *eve = memory.file_data + mem_get_sysvar32(mes_sysvar32_eve_offset);
	for (; le_get16(eve, 0) != 0xffff; eve += 12) {
		uint16_t x_left = le_get16(eve, 2);
		uint16_t y_top = le_get16(eve, 4);
		uint16_t x_right = le_get16(eve, 6);
		uint16_t y_bot = le_get16(eve, 8);
		uint16_t dir_mask = eve[10];

		if (sp->x + (sp->w - 1) < x_left || sp->x > x_right)
			continue;
		if (sp->y + sp->h <= y_top || sp->y >= y_bot)
			continue;
		if (!(dir_mask & (1 << (sp->frame >> 4))))
			continue;
		return le_get16(eve, 0);
	}
	return NO_LOCATION;
}

static uint16_t get_location(void)
{
	if (!map.get_location_enabled)
		return NO_LOCATION;

	struct ccd_sprite *sp = get_player();
	if (!sp) {
		WARNING("no player sprite?");
		return NO_LOCATION;
	}

	uint16_t loc = get_sprite_location(sp);
	if (map.location_mode == MAP_LOCATION_MODE_ONESHOT) {
		map.get_location_enabled = false;
	} else if (map.location_mode == MAP_LOCATION_MODE_NO_REPEAT) {
		if (loc == map.prev_location)
			return NO_LOCATION;
		map.prev_location = loc;
	}
	if (loc != NO_LOCATION) {
		MAP_LOG("map_get_location() -> %u", (unsigned)loc);
	}
	return loc;
}

void map_get_location(void)
{
	mem_set_var16(18, get_location());
}

// Location }}}
// Pathing {{{

static bool map_pos_equal(struct map_pos a, struct map_pos b)
{
	return a.x == b.x && a.y == b.y;
}

static unsigned h_distance(struct map_pos from, struct map_pos to)
{
	// XXX: taxicab distance
	return abs(to.x - from.x) + abs(to.y - from.y);
}

static struct path_data *get_path_data(struct map_pos pos)
{
	return &map.path.tiles[pos.y][pos.x];
}

static bool frontier_less_than(uint16_t a, uint16_t b)
{
	struct path_data *a_data = get_path_data(vector_A(map.path.frontier, a));
	struct path_data *b_data = get_path_data(vector_A(map.path.frontier, b));
	return a_data->f_score < b_data->f_score;
}

static uint16_t frontier_min(uint16_t a, uint16_t b)
{
	if (a >= vector_length(map.path.frontier))
		a = 0xffff;
	if (b >= vector_length(map.path.frontier))
		b = 0xffff;
	if (a == 0xffff)
		return b;
	if (b == 0xffff)
		return a;
	if (frontier_less_than(a, b))
		return a;
	return b;
}

static void frontier_sink(uint16_t node)
{
	uint16_t l_child = node * 2 + 1;
	uint16_t r_child = node * 2 + 2;

	uint16_t min_i = frontier_min(node, frontier_min(l_child, r_child));
	if (min_i != node) {
		struct map_pos tmp = vector_A(map.path.frontier, min_i);
		vector_A(map.path.frontier, min_i) = vector_A(map.path.frontier, node);
		vector_A(map.path.frontier, node) = tmp;
		frontier_sink(min_i);
	}
}

static struct map_pos frontier_pop(void)
{
	assert(vector_length(map.path.frontier) > 0);
	struct map_pos r = vector_A(map.path.frontier, 0);
	if (vector_length(map.path.frontier) > 1) {
		vector_A(map.path.frontier, 0) = vector_pop(map.path.frontier);
		frontier_sink(0);
	}

	return r;
}

static void frontier_swim(uint16_t node)
{
	if (node == 0)
		return;

	uint16_t parent = (node - 1) / 2;
	if (frontier_less_than(node, parent)) {
		struct map_pos tmp = vector_A(map.path.frontier, parent);
		vector_A(map.path.frontier, parent) = vector_A(map.path.frontier, node);
		vector_A(map.path.frontier, node) = tmp;
		frontier_swim(parent);
	}
}

static void frontier_push(struct map_pos pos)
{
	vector_push(struct map_pos, map.path.frontier, pos);
	frontier_swim(vector_length(map.path.frontier) - 1);
}

static bool map_tile_collides(unsigned x, unsigned y)
{
	return map.tile_data[y * map.cols + x].collides;
}

static bool sprite_pos_valid(unsigned x, unsigned y)
{
	return !map_tile_collides(x, y + 1) && !map_tile_collides(x, y + 2)
		&& !map_tile_collides(x + 1, y + 1) && !map_tile_collides(x + 1, y + 2)
		&& !map_tile_collides(x + 2, y + 1) && !map_tile_collides(x + 2, y + 2);
}

static struct map_pos get_neighbor(struct map_pos pos, int dir)
{
	switch (dir) {
	case MAP_UP:
		if (pos.y == 0)
			goto no_neighbor;
		if (!sprite_pos_valid(pos.x, pos.y - 1))
			goto no_neighbor;
		return (struct map_pos) { pos.x, pos.y - 1 };
	case MAP_DOWN:
		if (pos.y >= map.rows - 1)
			goto no_neighbor;
		if (!sprite_pos_valid(pos.x, pos.y + 1))
			goto no_neighbor;
		return (struct map_pos) { pos.x, pos.y + 1 };
	case MAP_LEFT:
		if (pos.x == 0)
			goto no_neighbor;
		if (!sprite_pos_valid(pos.x - 1, pos.y))
			goto no_neighbor;
		return (struct map_pos) { pos.x - 1, pos.y };
	case MAP_RIGHT:
		if (pos.x >= map.cols - 1)
			goto no_neighbor;
		if (!sprite_pos_valid(pos.x + 1, pos.y))
			goto no_neighbor;
		return (struct map_pos) { pos.x + 1, pos.y };
	case MAP_UP_LEFT:
		if (pos.x == 0 || pos.y == 0)
			goto no_neighbor;
		if (!sprite_pos_valid(pos.x - 1, pos.y - 1))
			goto no_neighbor;
		return (struct map_pos) { pos.x - 1, pos.y - 1 };
	case MAP_UP_RIGHT:
		if (pos.x >= map.cols - 1 || pos.y == 0)
			goto no_neighbor;
		if (!sprite_pos_valid(pos.x + 1, pos.y - 1))
			goto no_neighbor;
		return (struct map_pos) { pos.x + 1, pos.y - 1 };
	case MAP_DOWN_LEFT:
		if (pos.x == 0 || pos.y >= map.rows - 1)
			goto no_neighbor;
		if (!sprite_pos_valid(pos.x - 1, pos.y + 1))
			goto no_neighbor;
		return (struct map_pos) { pos.x - 1, pos.y + 1 };
	case MAP_DOWN_RIGHT:
		if (pos.x >= map.cols - 1 || pos.y >= map.rows - 1)
			goto no_neighbor;
		if (!sprite_pos_valid(pos.x + 1, pos.y + 1))
			goto no_neighbor;
		return (struct map_pos) { pos.x + 1, pos.y + 1 };
	}
no_neighbor:
	return (struct map_pos) { 0xffff, 0xffff };
}

/*
 * A* pathfinding algorithm.
 */
void map_path_sprite(unsigned sp_no, unsigned tx, unsigned ty)
{
	MAP_LOG("map_path_sprite(%u,%u,%u)", sp_no, tx, ty);
	struct ccd_sprite *sp = get_sprite(sp_no);
	if (!sp)
		return;

	if (tx + 2 >= map.cols || ty < 1 || ty + 1 >= map.rows || map.tile_data[ty * map.cols + tx].collides) {
		WARNING("Invalid pathing target: (%u,%u)", tx, ty);
		return;
	}

	// XXX: y-coord is center of character?
	ty--;

	if (!sprite_pos_valid(tx, ty)) {
		WARNING("Invalid pathing target (collides): (%u,%u)", tx, ty);
		return;
	}

	struct map_pos start = { sp->x, sp->y };
	map.path.goal = (struct map_pos) { tx, ty };
	if (map_pos_equal(start, map.path.goal))
		return;

	// initialize path data
	memset(map.path.tiles, 0xff, sizeof(map.path.tiles));
	map.path.tiles[sp->y][sp->x].g_score = 0;
	map.path.tiles[sp->y][sp->x].f_score = h_distance(start, map.path.goal);

	// put start node into frontier
	vector_length(map.path.frontier) = 0;
	vector_set(struct map_pos, map.path.frontier, 0, start);
	map.path.tiles[start.y][start.x].not_in_frontier = 0;

	while (true) {
		if (vector_length(map.path.frontier) == 0) {
			WARNING("pathing failed");
			return;
		}
		struct map_pos cur = frontier_pop();
		map.path.tiles[cur.y][cur.x].not_in_frontier = 1;
		if (map_pos_equal(cur, map.path.goal))
			break;

		// loop over neighbors
		for (int i = 0; i < 8; i++) {
			struct map_pos neighbor_pos = get_neighbor(cur, i);
			if (neighbor_pos.x == 0xffff)
				continue;

			struct path_data *neighbor = get_path_data(neighbor_pos);
			uint16_t g = map.path.tiles[cur.y][cur.x].g_score + (i < 3 ? 1 : 2);
			if (g < neighbor->g_score) {
				neighbor->pred = cur;
				neighbor->g_score = g;
				neighbor->f_score = g + h_distance(neighbor_pos, map.path.goal);
				if (neighbor->not_in_frontier) {
					frontier_push(neighbor_pos);
					neighbor->not_in_frontier = 0;
				}
			}
		}
	}

	// reconstruct the path
	vector_length(map.path.path) = 0;
	struct map_pos cur = map.path.goal;
	do {
		vector_push(struct map_pos, map.path.path, cur);
		cur = map.path.tiles[cur.y][cur.x].pred;
	} while (!map_pos_equal(cur, start));
	map.path.path_ptr = vector_length(map.path.path);

	// put sprite into pathing state
	map.path.active = true;
	map.path.saved.sp = sp;
	map.path.saved.state = sp->state;
	map.path.saved.cmd = sp->script_cmd;
	map.path.saved.repetitions = sp->script_repetitions;
	sp->state = (sp->state & MAP_SP_CAMERA) | MAP_SP_PLAYER | MAP_SP_ENABLED;
	sp->script_cmd = 13;
	sp->script_repetitions = 0xff;
}

void map_stop_pathing(void)
{
	MAP_LOG("map_stop_pathing()");
	if (map.path.active) {
		map.path.active = false;
		map.path.saved.sp->state = map.path.saved.state;
		map.path.saved.sp->script_cmd = map.path.saved.cmd;
		map.path.saved.sp->script_repetitions = map.path.saved.repetitions;
		if (map.location_mode != MAP_LOCATION_DISABLED)
			map.get_location_enabled = true;
	}
}

void map_get_pathing(void)
{
	//MAP_LOG("map_get_pathing()");
	mem_set_var16(18, map.path.active ? 0xffff : 0);
}

// Pathing }}}
