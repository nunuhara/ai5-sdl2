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

#include "dungeon.h"
#include "gfx_private.h"
#include "vm.h"

#if 0
#define DUNGEON_LOG(...) NOTICE(__VA_ARGS__)
#else
#define DUNGEON_LOG(...)
#endif

// Wall CG should be mirrored
#define VIEW_MIRRORED 0x80

// Stop drawing after this wall
#define DRAW_ORDER_END 0xff

// Maximum map size (in tiles)
#define MAX_TW 20
#define MAX_TH 20

/*
 * XXX: KABE[n].DAT is a bunch of variously-sized 4-bit indexed bitmaps
 *      concatenated together. The sizes and offsets are baked into the
 *      engine.
 */
static struct kabe_entry {
	unsigned w;      // width of CG
	unsigned h;      // height of CG
	unsigned offset; // offset into KABE[n].DAT
} kabe_entry[] = {
	// KABE1.DAT, KABE2.DAT
	[0]  = { 88,  480, 0x0 },
	[1]  = { 84,  440, 0x5280 },
	[2]  = { 60,  312, 0x9ab0 },
	[3]  = { 88,  432, 0xbf40 },
	[4]  = { 144, 432, 0x10980 },
	[5]  = { 84,  312, 0x18300 },
	[6]  = { 48,  312, 0x1b630 },
	[7]  = { 20,  312, 0x1d370 },
	[8]  = { 28,  480, 0x1dfa0 },
	[9]  = { 112, 480, 0x1f9e0 },
	[10] = { 92,  368, 0x262e0 },
	[11] = { 28,  480, 0x2a500 },
	[12] = { 264, 480, 0x2bf40 },
	[13] = { 112, 368, 0x3b6c0 },
	[14] = { 28,  368, 0x40740 },
	[15] = { 40,  368, 0x41b60 },
	[16] = { 92,  208, 0x43820 },
	[17] = { 116, 480, 0x45d80 },
	[18] = { 84,  412, 0x4ca40 },
	[19] = { 32,  268, 0x50dd8 },
	[20] = { 80,  412, 0x51e98 },
	[21] = { 40,  412, 0x55ef8 },
	[22] = { 84,  412, 0x57f28 },
	[23] = { 84,  260, 0x5c2c0 },
	[24] = { 36,  260, 0x5ed68 },
	[25] = { 412, 480, 0x5ffb0 },
	[26] = { 228, 408, 0x781f0 },
	[27] = { 92,  296, 0x837a0 },
	[28] = { 144, 420, 0x86cd0 },
	[29] = { 228, 480, 0x8e2f0 },
	[30] = { 268, 288, 0x9b8b0 },
	[31] = { 156, 256, 0xa4f70 },
	[32] = { 44,  232, 0xa9d70 },
	[33] = { 112, 224, 0xab160 },
	[34] = { 36,  224, 0xae260 },
	[35] = { 48,  240, 0xaf220 },
	[36] = { 180, 288, 0xb08a0 },
	[37] = { 320, 104, 0xb6de0 },
	[38] = { 320, 168, 0xbaee0 },
	[39] = { 320, 104, 0xc17e0 },
	[40] = { 320, 168, 0xc58e0 },
	[41] = { 320, 104, 0xcc1e0 },
	[42] = { 320, 168, 0xd02e0 },
	[43] = { 640, 104, 0xd6be0 },
	[44] = { 640, 168, 0xdede0 },
	// KABE3.DAT
	[45] = { 88,  480, 0x0 },
	[46] = { 84,  440, 0x5280 },
	[47] = { 60,  312, 0x9ab0 },
	[48] = { 88,  432, 0xbf40 },
	[49] = { 144, 432, 0x10980 },
	[50] = { 84,  312, 0x18300 },
	[51] = { 48,  312, 0x1b630 },
	[52] = { 20,  312, 0x1d370 },
	[53] = { 28,  480, 0x1dfa0 },
	[54] = { 112, 480, 0x1f9e0 },
	[55] = { 92,  368, 0x262e0 },
	[56] = { 28,  480, 0x2a500 },
	[57] = { 264, 480, 0x2bf40 },
	[58] = { 112, 368, 0x3b6c0 },
	[59] = { 28,  368, 0x40740 },
	[60] = { 40,  368, 0x41b60 },
	[61] = { 92,  208, 0x43820 },
	[62] = { 116, 480, 0x45d80 },
	[63] = { 84,  412, 0x4ca40 },
	[64] = { 32,  268, 0x50dd8 },
	[65] = { 80,  412, 0x51e98 },
	[66] = { 40,  412, 0x55ef8 },
	[67] = { 84,  412, 0x57f28 },
	[68] = { 84,  260, 0x5c2c0 },
	[69] = { 36,  260, 0x5ed68 },
	[70] = { 412, 480, 0x5ffb0 },
	[71] = { 228, 408, 0x781f0 },
	[72] = { 92,  296, 0x837a0 },
	[73] = { 144, 420, 0x86cd0 },
	[74] = { 228, 480, 0x8e2f0 },
	[75] = { 268, 288, 0x9b8b0 },
	[76] = { 156, 256, 0xa4f70 },
	[77] = { 44,  232, 0xa9d70 },
	[78] = { 112, 224, 0xab160 },
	[79] = { 36,  224, 0xae260 },
	[80] = { 48,  240, 0xaf220 },
	[81] = { 180, 288, 0xb08a0 },
};

/*
 * View Modes
 * ----------
 *
 * The view mode controls the order in which walls are drawn, which CGs are
 * drawn for each wall, and at which position they are drawn. All of this data
 * is hardcoded into the engine for each view mode.
 *
 * This is used to implement short (3 frame) animations when moving through the
 * dungeon.
 *
 * E.g. moving forward:
 *     [start] = VIEW_MODE_STANDARD @ tile a
 *     frame 1 = VIEW_MODE_FORWARD  @ tile a
 *     frame 2 = VIEW_MODE_BACKWARD @ tile b
 *     frame 3 = VIEW_MODE_STANDARD @ tile b
 *
 * rotating to the left:
 *     [start] = VIEW_MODE_STANDARD  @ rot a
 *     frame 1 = VIEW_MODE_ROT_LEFT  @ rot a
 *     frame 2 = VIEW_MODE_ROT_RIGHT @ rot b
 *     frame 3 = VIEW_MODE STANDARD  @ rot b
 */
enum dungeon_view_mode {
	VIEW_MODE_STANDARD,  // standard view mode
	VIEW_MODE_FORWARD,   // 1/3-tile forward view
	VIEW_MODE_BACKWARD,  // 1/3-tile backward view
	VIEW_MODE_ROT_RIGHT, // 15deg clockwise rotated view
	VIEW_MODE_ROT_LEFT,  // 15deg counter-clockwise rotated view
};

/*
 * Indices into `dungeon.view`.
 */
enum dungeon_view_wall {
	VIEW_1_LEFT,
	VIEW_1_FRONT,
	VIEW_2_LEFT,
	VIEW_2_FRONT,
	VIEW_2_RIGHT,
	VIEW_3_FRONT,
	VIEW_3_RIGHT,
	VIEW_5_FRONT,
	VIEW_6_LEFT,
	VIEW_6_FRONT,
	VIEW_7_LEFT,
	VIEW_7_FRONT,
	VIEW_7_RIGHT,
	VIEW_8_FRONT,
	VIEW_8_RIGHT,
	VIEW_9_FRONT,
	VIEW_11_FRONT,
	VIEW_12_LEFT,
	VIEW_12_FRONT,
	VIEW_12_RIGHT,
	VIEW_12_BACK,
	VIEW_13_FRONT,
};
#define VIEW_NR_WALLS (VIEW_13_FRONT+1)

/*
 * A map tile. A tile has 4 walls (4 bits each, packed into the `walls` member)
 * and an 8-bit ID.
 */
struct dungeon_tile {
	uint16_t walls;
	uint8_t id;
	uint8_t uk;
};

/*
 * Accessors for the `walls` member of `struct dungeon_tile`.
 */
#define WALL_FRONT(walls) (walls >> 12)
#define WALL_RIGHT(walls) ((walls & 0x0f00) >> 8)
#define WALL_BACK(walls)  ((walls & 0x00f0) >> 4)
#define WALL_LEFT(walls)  (walls & 0xf)

/*
 * Global dungeon data.
 */
static struct dungeon {
	unsigned tw, th;
	uint8_t *kabe[3];
	SDL_Color pal[16];
	struct {
		enum dungeon_direction dir;
		unsigned x, y;
	} player;
	struct dungeon_tile tile[MAX_TH][MAX_TW];
	uint8_t view[VIEW_NR_WALLS];
	uint8_t moved_through_wall;
} dungeon = {0};

/*
 * A 'logical' CG, which may consist of multiple actual CGs drawn side-by-side.
 * See 'Wall CGs' explanation below.
 */
struct dungeon_view_cg {
	unsigned x, y;
	uint8_t kabe[8];
};

/*
 * An entry in a draw order. If a wall exists at `dungeon.view[wall]`, then it
 * is drawn and the algorithm jumps to `next_sight_line` in the draw order.
 * See 'Draw Order' explanation below.
 */
struct dungeon_draw_order_entry {
	enum dungeon_view_wall wall;
	unsigned next_sight_line;
};

/*
 * Wall CGs
 * --------
 *
 * E.g.
 *     [2] = { 0, 52, { 7, 7|M, 6|M, E } },
 *
 * Explanation:
 *     The CG for the second wall in the current draw order (see 'Draw Order'
 *     explanation below) should be drawn at (0, 52) and consists of the
 *     following CGs:
 *         * CG 7
 *         * CG 7 mirrored on the Y-axis
 *         * CG 6 mirrored on the Y-axis
 *     Each CG is drawn in order, left to right.
 */

#define M VIEW_MIRRORED
#define E DRAW_ORDER_END

// CG numbers and position for VIEW_MODE_STANDARD. Index corresponds to draw order.
static struct dungeon_view_cg dungeon_walls_standard[] = {
	[0]  = {   0,  0, { 0, E } },
	[1]  = {   0,  0, { 3|M, E } },
	[2]  = {   0, 52, { 7, 7|M, 6|M, E } },
	[3]  = { 552,  0, { 0|M, E } },
	[4]  = { 552,  0, { 3, E } },
	[5]  = { 552, 52, { 6, 7, 7|M, E } },
	[6]  = {  88,  0, { 3, 4, 4|M, 3|M, E } },
	[7]  = {  88,  0, { 1, E } },
	[8]  = {  88, 52, { 5|M, E } },
	[9]  = { 468,  0, { 1|M, E } },
	[10] = { 468, 52, { 5, E } },
	[11] = { 172, 52, { 5, 6, 7, 7|M, 6|M, 5|M, E } },
	[12] = { 172, 52, { 2, E } },
	[13] = { 408, 52, { 2|M, E } }
};

// CG numbers and position for VIEW_MODE_FORWARD. Index corresponds to draw order.
static struct dungeon_view_cg dungeon_walls_forward[] = {
	[0]  = {   0,   0, { 8, E } },
	[1]  = {   0,   0, { 11|M, E } },
	[2]  = {   0,  24, { 14|M, E } },
	[3]  = { 612,   0, { 8|M, E } },
	[4]  = { 612,   0, { 11, E } },
	[5]  = { 612,  24, { 14, E } },
	[6]  = {  28,   0, { 11, 12, 12|M, 11|M, E } },
	[7]  = {  28,   0, { 9, 0xff } },
	[8]  = {  28,  24, { 13|M, E } },
	[9]  = {  48, 104, { 16, E } },
	[10] = { 500,   0, { 9|M, E } },
	[11] = { 500,  24, { 13, E } },
	[12] = { 500, 104, { 16|M, E } },
	[13] = { 140,  24, { 13, 14, 15, 15|M, 14|M, 13|M, E } },
	[14] = { 140,  24, { 10, E } },
	[15] = { 140, 104, { 16|M, E } },
	[16] = { 408,  24, { 10|M, E } },
	[17] = { 408, 104, { 16, E } },
	[18] = { 232, 104, { 16, 16|M, E } }
};

// CG numbers and position for VIEW_MODE_BACKWARD. Index corresponds to draw order.
static struct dungeon_view_cg dungeon_walls_backward[] = {
	[0]  = {   0,  0, { 17, E } },
	[1]  = {   0,  0, { 21|M, 20|M, E } },
	[2]  = {   0, 76, { 23|M, 24|M, E } },
	[3]  = { 524,  0, { 17|M, E } },
	[4]  = { 516,  0, { 20, 21, E } },
	[5]  = { 520, 76, { 24, 23, E } },
	[6]  = { 116,  0, { 20, 21, 22, 22|M, 21|M, 20|M, E } },
	[7]  = { 116,  0, { 18, E } },
	[8]  = { 116, 76, { 23|M, E } },
	[9]  = { 440,  0, { 18|M, E } },
	[10] = { 440, 76, { 23, E } },
	[11] = { 200, 76, { 23, 24, 24|M, 23|M, E } },
	[12] = { 200, 72, { 19, E } },
	[13] = { 408, 72, { 19|M, E } }
};

// CG numbers and position for VIEW_MODE_ROT_RIGHT. Index corresponds to draw order.
static struct dungeon_view_cg dungeon_walls_rot_right[] = {
	[0]  = {   0,  0, { 25, E } },
	[1]  = {   0, 64, { 30, E } },
	[2]  = { 176, 60, { 27, E } },
	[3]  = { 268,  0, { 28, E } },
	[4]  = { 268, 84, { 31, E } },
	[5]  = { 376, 96, { 34, E } },
	[6]  = { 412,  0, { 29, E } },
	[7]  = { 412,  4, { 26, E } },
	[8]  = { 424, 92, { 32, E } },
	[9]  = { 412, 88, { 35, E } },
	[10] = { 460, 64, { 36, E } },
	[11] = { 468, 96, { 33, E } }
};

// CG numbers and position for VIEW_MODE_ROT_LEFT. Index corresponds to draw order.
static struct dungeon_view_cg dungeon_walls_rot_left[] = {
	[0]  = { 228,  0, { 25|M, E } },
	[1]  = { 368, 64, { 30|M, E } },
	[2]  = { 372, 60, { 27|M, E } },
	[3]  = { 228,  0, { 28|M, E } },
	[4]  = { 212, 84, { 31|M, E } },
	[5]  = { 228, 96, { 34|M, E } },
	[6]  = {   0,  0, { 29|M, E } },
	[7]  = {   0,  4, { 26|M, E } },
	[8]  = { 168, 92, { 32|M, E } },
	[9]  = { 180, 88, { 35|M, E } },
	[10] = {   0, 64, { 36|M, E } },
	[11] = {  56, 96, { 33|M, E } }
};

// CG numbers and positions of walls for each view mode.
static struct dungeon_view_cg *dungeon_walls[] = {
	[VIEW_MODE_STANDARD]  = dungeon_walls_standard,
	[VIEW_MODE_FORWARD]   = dungeon_walls_forward,
	[VIEW_MODE_BACKWARD]  = dungeon_walls_backward,
	[VIEW_MODE_ROT_RIGHT] = dungeon_walls_rot_right,
	[VIEW_MODE_ROT_LEFT]  = dungeon_walls_rot_left
};

// CG numbers and position of ceiling for each view mode.
static struct dungeon_view_cg dungeon_ceilings[] = {
	[VIEW_MODE_STANDARD]  = { 0, 0, { 37, 37|M, E } },
	[VIEW_MODE_FORWARD]   = { 0, 0, { 39, 39|M, E } },
	[VIEW_MODE_BACKWARD]  = { 0, 0, { 41, 41|M, E } },
	[VIEW_MODE_ROT_RIGHT] = { 0, 0, { 43|M, E } },
	[VIEW_MODE_ROT_LEFT]  = { 0, 0, { 43, E } },
};

// CG numbers and position of floor for each view mode.
static struct dungeon_view_cg dungeon_floors[] = {
	[VIEW_MODE_STANDARD]  = { 0, 312, { 38, 38|M, E } },
	[VIEW_MODE_FORWARD]   = { 0, 312, { 40, 40|M, E } },
	[VIEW_MODE_BACKWARD]  = { 0, 312, { 42, 42|M, E } },
	[VIEW_MODE_ROT_RIGHT] = { 0, 312, { 44|M, E } },
	[VIEW_MODE_ROT_LEFT]  = { 0, 312, { 44, E } },
};

#undef E
#undef M

/*
 * Draw Order
 * ----------
 *
 * E.g.
 *     { VIEW_12_LEFT,  3 },
 *     { VIEW_11_FRONT, 3 },
 *
 * Explanation:
 *     If there's a wall at VIEW_12_LEFT, draw it, and then skip to index 3 in
 *     the draw order array (the next sight line).
 *     If there is not a wall at VIEW_12_LEFT, continue to the next entry in
 *     the draw order array (in this case, the entry for VIEW_11_FRONT).
 */

// Wall draw order for VIEW_MODE_STANDARD.
static struct dungeon_draw_order_entry dungeon_draw_order_standard[] = {
	[0]  = { VIEW_12_LEFT,   3 },
	[1]  = { VIEW_11_FRONT,  3 },
	[2]  = { VIEW_6_FRONT,   3 },
	[3]  = { VIEW_12_RIGHT,  6 },
	[4]  = { VIEW_13_FRONT,  6 },
	[5]  = { VIEW_8_FRONT,   6 },
	[6]  = { VIEW_12_FRONT,  DRAW_ORDER_END },
	[7]  = { VIEW_7_LEFT,    9 },
	[8]  = { VIEW_6_FRONT,   9 },
	[9]  = { VIEW_7_RIGHT,   11 },
	[10] = { VIEW_8_FRONT,   11 },
	[11] = { VIEW_7_FRONT,   DRAW_ORDER_END },
	[12] = { VIEW_2_LEFT,    13 },
	[13] = { VIEW_2_RIGHT,   DRAW_ORDER_END },
	[14] = { DRAW_ORDER_END, DRAW_ORDER_END },
};

// Wall draw order for VIEW_MODE_FORWARD.
static struct dungeon_draw_order_entry dungeon_draw_order_forward[] = {
	[0]  = { VIEW_12_LEFT,   3 },
	[1]  = { VIEW_11_FRONT,  3 },
	[2]  = { VIEW_6_FRONT,   3 },
	[3]  = { VIEW_12_RIGHT,  6 },
	[4]  = { VIEW_13_FRONT,  6 },
	[5]  = { VIEW_8_FRONT,   6 },
	[6]  = { VIEW_12_FRONT,  DRAW_ORDER_END },
	[7]  = { VIEW_7_LEFT,    10 },
	[8]  = { VIEW_6_FRONT,   10 },
	[9]  = { VIEW_1_FRONT,   10 },
	[10] = { VIEW_7_RIGHT,   13 },
	[11] = { VIEW_8_FRONT,   13 },
	[12] = { VIEW_3_FRONT,   13 },
	[13] = { VIEW_7_FRONT,   DRAW_ORDER_END },
	[14] = { VIEW_2_LEFT,    16 },
	[15] = { VIEW_1_FRONT,   16 },
	[16] = { VIEW_2_RIGHT,   18 },
	[17] = { VIEW_3_FRONT,   18 },
	[18] = { VIEW_2_FRONT,   DRAW_ORDER_END },
	[19] = { DRAW_ORDER_END, DRAW_ORDER_END },
};

// Wall draw order for VIEW_MODE_ROT_RIGHT.
static struct dungeon_draw_order_entry dungeon_draw_order_rot_right[] = {
	[0]  = { VIEW_12_FRONT,  6 },
	[1]  = { VIEW_7_FRONT,   3 },
	[2]  = { VIEW_2_RIGHT,   3 },
	[3]  = { VIEW_7_RIGHT,   6 },
	[4]  = { VIEW_8_FRONT,   6 },
	[5]  = { VIEW_3_RIGHT,   6 },
	[6]  = { VIEW_12_RIGHT,  DRAW_ORDER_END },
	[7]  = { VIEW_13_FRONT,  DRAW_ORDER_END },
	[8]  = { VIEW_8_FRONT,   10 },
	[9]  = { VIEW_3_RIGHT,   10 },
	[10] = { VIEW_8_RIGHT,   DRAW_ORDER_END },
	[11] = { VIEW_9_FRONT,   DRAW_ORDER_END },
	[12] = { DRAW_ORDER_END, DRAW_ORDER_END },
};

// Wall draw order for VIEW_MODE_ROT_LEFT.
static struct dungeon_draw_order_entry dungeon_draw_order_rot_left[] = {
	[0]  = { VIEW_12_FRONT,  6 },
	[1]  = { VIEW_7_FRONT,   3 },
	[2]  = { VIEW_2_LEFT,    3 },
	[3]  = { VIEW_7_LEFT,    6 },
	[4]  = { VIEW_6_FRONT,   6 },
	[5]  = { VIEW_1_LEFT,    6 },
	[6]  = { VIEW_12_LEFT,   DRAW_ORDER_END },
	[7]  = { VIEW_11_FRONT,  DRAW_ORDER_END },
	[8]  = { VIEW_6_FRONT,   10 },
	[9]  = { VIEW_1_LEFT,    10 },
	[10] = { VIEW_6_LEFT,    DRAW_ORDER_END },
	[11] = { VIEW_5_FRONT,   DRAW_ORDER_END },
	[12] = { DRAW_ORDER_END, DRAW_ORDER_END },
};

// Draw orders for each view mode.
static struct dungeon_draw_order_entry *dungeon_draw_order[] = {
	[VIEW_MODE_STANDARD]  = dungeon_draw_order_standard,
	[VIEW_MODE_FORWARD]   = dungeon_draw_order_forward,
	[VIEW_MODE_BACKWARD]  = dungeon_draw_order_standard,
	[VIEW_MODE_ROT_RIGHT] = dungeon_draw_order_rot_right,
	[VIEW_MODE_ROT_LEFT]  = dungeon_draw_order_rot_left,
};

/*
 * FLOOR[n].MP3 contains the map data for floor 'n'.
 */
static void dungeon_load_mp3(uint8_t *mp3)
{
	dungeon.tw = le_get16(mp3, 0);
	dungeon.th = le_get16(mp3, 2);
	if (dungeon.tw > MAX_TW || dungeon.th > MAX_TH)
		VM_ERROR("Dungeon size too large: %ux%u", dungeon.tw, dungeon.th);

	uint8_t *tile = mp3 + 4;
	for (int row = 0; row < dungeon.th; row++) {
		for (int col = 0; col < dungeon.tw; col++, tile += 4) {
			dungeon.tile[row][col] = (struct dungeon_tile) {
				.walls = le_get16(tile, 0),
				.id = tile[2],
				.uk = tile[3],
			};
		}
	}
}

/*
 * KABE.PAL contains a 4-bit indexed, BGR555 palette for dungeon CGs.
 */
static void dungeon_load_pal(uint8_t *pal)
{
	for (int i = 0; i < 16; i++) {
		uint16_t c = le_get16(pal, i * 2);
		dungeon.pal[i] = (SDL_Color) {
			.r = (c & 0x7c00) >> 7,
			.g = (c & 0x03e0) >> 2,
			.b = (c & 0x001f) << 3,
			.a = 255,
		};
	}
}

/*
 * Wall Rotations
 * --------------
 *
 * In canonical (North-facing) representation, a walls value of 0xABCD means:
 *     North wall = 0xA    ^
 *     East wall  = 0xB    A
 *     South wall = 0xC  D + B
 *     West wall  = 0xD    C
 *
 * These functions rotate a walls value so that 0xABCD would instead mean:
 *     Front wall = 0xA
 *     Right wall = 0xB    D
 *     Back wall  = 0xC  C + A >
 *     Left wall  = 0xD    B
 *
 * When we load the view, all walls are rotated according to the direction the
 * player is facing.
 */

static uint16_t rotate_walls_facing_east(uint16_t walls)
{
	return (walls >> 12) | (walls << 4);
}

static uint16_t rotate_walls_facing_west(uint16_t walls)
{
	return (walls << 12) | (walls >> 4);
}

static uint16_t rotate_walls_facing_south(uint16_t walls)
{
	return (walls << 8) | (walls >> 8);
}

attr_unused static void dungeon_print_view(void)
{
#define V(v) dungeon.view[VIEW_##v]
	NOTICE("+---+---+---+---+---+");
	NOTICE("|   | %1x | %1x | %1x |   |", V(1_FRONT), V(2_FRONT), V(3_FRONT));
	NOTICE("|   |%1x  |%1x %1x|  %1x|   |", V(1_LEFT), V(2_LEFT), V(2_RIGHT), V(3_RIGHT));
	NOTICE("+---+---+---+---+---+");
	NOTICE("| %1x | %1x | %1x | %1x | %1x |", V(5_FRONT), V(6_FRONT), V(7_FRONT), V(8_FRONT), V(9_FRONT));
	NOTICE("|   |%1x  |%1x %1x|  %1x|   |", V(6_LEFT), V(7_LEFT), V(7_RIGHT), V(8_RIGHT));
	NOTICE("+---+---+---+---+---+");
	NOTICE("|   | %1x | %1x | %1x |   |", V(11_FRONT), V(12_FRONT), V(13_FRONT));
	NOTICE("|   |   |%1x %1x|   |   |", V(12_LEFT), V(12_RIGHT));
	NOTICE("+---+---+=^=+---+---+");
#undef V
}

attr_unused static const char *dir2str(enum dungeon_direction dir)
{
	switch (dir) {
	case DUNGEON_NORTH: return "North";
	case DUNGEON_EAST: return "East";
	case DUNGEON_SOUTH: return "South";
	case DUNGEON_WEST: return "West";
	}
	return "Invalid direction";
}

/*             FORWARD
 *
 *   +----+====+====+====+----+
 *   |    =    =    =    =    |
 *   |  0 =  1 =  2 =  3 =  4 |
 *   |    =    =    =    =    |
 * L +====+====+====+====+====+ R
 * E |    =    =    =    =    | I
 * F |  5 =  6 =  7 =  8 =  9 | G
 * T |    =    =    =    =    | H
 *   +----+====+====+====+----+ T
 *   |    |    =::::=    |    |
 *   | 10 | 11 =:12:= 13 | 14 |
 *   |    |    =::::=    |    |
 *   +----+----+====+----+----+
 *               ^^
 *             PLAYER
 *
 * This function loads a 5x3 grid of tiles around the player, in the direction
 * the player is facing. The walls within this grid that are potentially
 * visible are stored into the `dungeon.view` array (in the diagram above,
 * these walls are marked with '=' characters).
 *
 */
static void dungeon_load_view(int x, int y, enum dungeon_direction dir)
{
	DUNGEON_LOG("dungeon_load_view(%u, %u, %s)", x, y, dir2str(dir));
	uint16_t view[15] = {0};
	switch (dir) {
	case DUNGEON_EAST:
		for (int v_x = x + 2, i = 0; v_x >= x; v_x--) {
			for (int v_y = y - 2; v_y <= y + 2; v_y++, i++) {
				if (v_x >= dungeon.tw || v_y < 0 || v_y >= dungeon.th)
					continue;
				uint16_t walls = dungeon.tile[v_y][v_x].walls;
				view[i] = rotate_walls_facing_east(walls);
			}
		}
		break;
	case DUNGEON_NORTH:
		for (int v_y = y - 2, i = 0; v_y <= y; v_y++) {
			for (int v_x = x - 2; v_x <= x + 2; v_x++, i++) {
				if (v_y < 0 || v_x < 0 || v_x >= dungeon.tw)
					continue;
				view[i] = dungeon.tile[v_y][v_x].walls;
			}
		}
		break;
	case DUNGEON_WEST:
		for (int v_x = x - 2, i = 0; v_x <= x; v_x++) {
			for (int v_y = y + 2; v_y >= y - 2; v_y--, i++) {
				if (v_x < 0 || v_y < 0 || v_y >= dungeon.th)
					continue;
				uint16_t walls = dungeon.tile[v_y][v_x].walls;
				view[i] = rotate_walls_facing_west(walls);
			}
		}
		break;
	case DUNGEON_SOUTH:
		for (int v_y = y + 2, i = 0; v_y >= y; v_y--) {
			for (int v_x = x + 2; v_x >= x - 2; v_x--, i++) {
				if (v_y >= dungeon.th || v_x < 0 || v_x >= dungeon.tw)
					continue;
				uint16_t walls = dungeon.tile[v_y][v_x].walls;
				view[i] = rotate_walls_facing_south(walls);
			}
		}
		break;
	}

	// load relevant walls into dungeon.view
	dungeon.view[VIEW_1_LEFT]   = WALL_LEFT(view[1]);
	dungeon.view[VIEW_1_FRONT]  = WALL_FRONT(view[1]);
	dungeon.view[VIEW_2_LEFT]   = WALL_LEFT(view[2]);
	dungeon.view[VIEW_2_FRONT]  = WALL_FRONT(view[2]);
	dungeon.view[VIEW_2_RIGHT]  = WALL_RIGHT(view[2]);
	dungeon.view[VIEW_3_FRONT]  = WALL_FRONT(view[3]);
	dungeon.view[VIEW_3_RIGHT]  = WALL_RIGHT(view[3]);
	dungeon.view[VIEW_5_FRONT]  = WALL_FRONT(view[5]);
	dungeon.view[VIEW_6_LEFT]   = WALL_LEFT(view[6]);
	dungeon.view[VIEW_6_FRONT]  = WALL_FRONT(view[6]);
	dungeon.view[VIEW_7_LEFT]   = WALL_LEFT(view[7]);
	dungeon.view[VIEW_7_FRONT]  = WALL_FRONT(view[7]);
	dungeon.view[VIEW_7_RIGHT]  = WALL_RIGHT(view[7]);
	dungeon.view[VIEW_8_FRONT]  = WALL_FRONT(view[8]);
	dungeon.view[VIEW_8_RIGHT]  = WALL_RIGHT(view[8]);
	dungeon.view[VIEW_9_FRONT]  = WALL_FRONT(view[9]);
	dungeon.view[VIEW_11_FRONT] = WALL_FRONT(view[11]);
	dungeon.view[VIEW_12_LEFT]  = WALL_LEFT(view[12]);
	dungeon.view[VIEW_12_FRONT] = WALL_FRONT(view[12]);
	dungeon.view[VIEW_12_RIGHT] = WALL_RIGHT(view[12]);
	dungeon.view[VIEW_12_BACK]  = WALL_BACK(view[12]);
	dungeon.view[VIEW_13_FRONT] = WALL_FRONT(view[13]);
	//dungeon_print_view();
}

static void _dungeon_set_pos(unsigned x, unsigned y, enum dungeon_direction dir)
{
	dungeon.player.x = x;
	dungeon.player.y = y;
	dungeon.player.dir = dir;
	dungeon_load_view(x, y, dir);
}

void dungeon_load(uint8_t *mp3, uint8_t *kabe1, uint8_t *kabe2, uint8_t *kabe3,
		uint8_t *kabe_pal, uint8_t *dun_a6)
{
	DUNGEON_LOG("dungeon_load");
	dungeon_load_mp3(mp3);
	dungeon_load_pal(kabe_pal);
	dungeon.kabe[0] = kabe1;
	dungeon.kabe[1] = kabe2;
	dungeon.kabe[2] = kabe3;
	_dungeon_set_pos(0, 0, DUNGEON_EAST);
}

void dungeon_set_pos(unsigned x, unsigned y, enum dungeon_direction dir)
{
	DUNGEON_LOG("dungeon_set_pos(%u, %u, %d)", x, y, dir);
	if (x >= dungeon.tw || y >= dungeon.th)
		VM_ERROR("Invalid position: %u,%u", x, y);
	if (dir >= 4)
		VM_ERROR("Invalid direction: %d", dir);
	_dungeon_set_pos(x, y, dir);
}

void dungeon_get_pos(uint16_t *x, uint16_t *y, uint16_t *dir)
{
	*x = dungeon.player.x;
	*y = dungeon.player.y;
	*dir = dungeon.player.dir;
}

static void dungeon_draw_cg(SDL_Surface *dst, unsigned x, unsigned y, uint8_t wall_id,
		unsigned wall_type)
{
	DUNGEON_LOG("dungeon_draw_cg(%u, %u, %u, %u)", x, y, (unsigned)wall_id, wall_type);

	bool mirrored = wall_id & VIEW_MIRRORED;
	wall_id &= ~VIEW_MIRRORED;
	if (wall_id >= ARRAY_SIZE(kabe_entry))
		VM_ERROR("Invalid wall ID: %u", (unsigned)wall_id);

	// get CG archive
	uint8_t *kabe_dat;
	if (wall_type == 0) {
		kabe_dat = dungeon.kabe[0];
	} else if (wall_type == 1) {
		kabe_dat = dungeon.kabe[1];
	} else if (wall_type == 2) {
		kabe_dat = dungeon.kabe[2];
		wall_id += 45;
	} else {
		VM_ERROR("Invalid wall type: %u", wall_type);
	}

	struct kabe_entry *kabe = &kabe_entry[wall_id];
	uint8_t *src = kabe_dat + kabe->offset;

	// XXX: CG data is 4-bit indexed bitmap
	if (!mirrored) {
		for (int row = 0; row < kabe->h; row++) {
			uint8_t *p = dst->pixels + (y + row) * dst->pitch + x * 3;
			for (int col = 0; col < kabe->w; col += 2, p += 6, src++) {
				SDL_Color *c1 = &dungeon.pal[*src >> 4];
				SDL_Color *c2 = &dungeon.pal[*src & 0xf];
				p[0] = c1->r;
				p[1] = c1->g;
				p[2] = c1->b;
				p[3] = c2->r;
				p[4] = c2->g;
				p[5] = c2->b;
			}
		}
	} else {
		for (int row = 0; row < kabe->h; row++) {
			uint8_t *p = dst->pixels + (y + row) * dst->pitch + x * 3;
			p += (kabe->w - 2) * 3;
			for (int col = kabe->w - 2; col >= 0; col -= 2, p -= 6, src++) {
				SDL_Color *c1 = &dungeon.pal[*src >> 4];
				SDL_Color *c2 = &dungeon.pal[*src & 0xf];
				p[0] = c2->r;
				p[1] = c2->g;
				p[2] = c2->b;
				p[3] = c1->r;
				p[4] = c1->g;
				p[5] = c1->b;
			}
		}
	}
}

/*
 * Draw a (logical) CG. Many walls consist of multiple CGs, drawn from left to
 * right.
 */
static void dungeon_draw_view_cg(SDL_Surface *dst, struct dungeon_view_cg *cg,
		unsigned wall_type)
{
	unsigned x = cg->x;
	for (int i = 0; cg->kabe[i] != DRAW_ORDER_END; i++) {
		dungeon_draw_cg(dst, x, cg->y, cg->kabe[i], wall_type);
		x += kabe_entry[cg->kabe[i] & ~VIEW_MIRRORED].w;
	}
}

static enum dungeon_speed dungeon_speed = DUNGEON_NORMAL;
static int move_frame_time[] = {
	[DUNGEON_FAST] = 5,
	[DUNGEON_NORMAL] = 25,
	[DUNGEON_SLOW] = 50
};

void dungeon_set_move_speed(enum dungeon_speed speed)
{
	dungeon_speed = speed;
}

enum dungeon_speed dungeon_get_move_speed(void)
{
	return dungeon_speed;
}

static void dungeon_draw_view(enum dungeon_view_mode mode)
{
	//DUNGEON_LOG("dungeon_draw_view(%d)", mode);
	vm_timer_t timer = vm_timer_create();

	uint16_t dst_i = mem_get_sysvar16(mes_sysvar16_dst_surface);
	SDL_Surface *dst = gfx_get_surface(dst_i);
	SDL_CALL(SDL_FillRect, dst, NULL, SDL_MapRGB(dst->format, 0, 0, 0));

	if (SDL_MUSTLOCK(dst))
		SDL_CALL(SDL_LockSurface, dst);

	dungeon_draw_view_cg(dst, &dungeon_ceilings[mode], 0);
	dungeon_draw_view_cg(dst, &dungeon_floors[mode], 0);

	struct dungeon_draw_order_entry *draw_order = dungeon_draw_order[mode];
	struct dungeon_view_cg *wall_cg = dungeon_walls[mode];
	for (int i = 0; draw_order[i].wall != DRAW_ORDER_END;) {
		uint8_t wall = dungeon.view[draw_order[i].wall] & 7;
		if (wall) {
			// draw
			dungeon_draw_view_cg(dst, &wall_cg[i], wall - 1);
			if (draw_order[i].next_sight_line == DRAW_ORDER_END)
				break;
			// skip to next sight line
			i = draw_order[i].next_sight_line;
		} else {
			// check next wall in sight line
			i++;
		}
	}

	if (SDL_MUSTLOCK(dst))
		SDL_UnlockSurface(dst);

	gfx_whole_surface_dirty(dst_i);
	vm_timer_tick(&timer, move_frame_time[dungeon_speed]);
	gfx_update();
}

void dungeon_draw(void)
{
	dungeon_draw_view(VIEW_MODE_STANDARD);
}

static void dungeon_move_forward(void)
{
	dungeon.moved_through_wall = dungeon.view[VIEW_12_FRONT];
	dungeon_draw_view(VIEW_MODE_FORWARD);
	switch (dungeon.player.dir) {
	case DUNGEON_NORTH: dungeon.player.y -= 1; break;
	case DUNGEON_SOUTH: dungeon.player.y += 1; break;
	case DUNGEON_WEST:  dungeon.player.x -= 1; break;
	case DUNGEON_EAST:  dungeon.player.x += 1; break;
	}
	dungeon_load_view(dungeon.player.x, dungeon.player.y, dungeon.player.dir);
	dungeon_draw_view(VIEW_MODE_BACKWARD);
	dungeon_draw_view(VIEW_MODE_STANDARD);
}

static void dungeon_move_backward(void)
{
	dungeon.moved_through_wall = dungeon.view[VIEW_12_BACK];
	dungeon_draw_view(VIEW_MODE_BACKWARD);
	switch (dungeon.player.dir) {
	case DUNGEON_NORTH: dungeon.player.y += 1; break;
	case DUNGEON_SOUTH: dungeon.player.y -= 1; break;
	case DUNGEON_WEST:  dungeon.player.x += 1; break;
	case DUNGEON_EAST:  dungeon.player.x -= 1; break;
	}
	dungeon_load_view(dungeon.player.x, dungeon.player.y, dungeon.player.dir);
	dungeon_draw_view(VIEW_MODE_FORWARD);
	dungeon_draw_view(VIEW_MODE_STANDARD);
}

static void dungeon_rotate_left(void)
{
	dungeon_draw_view(VIEW_MODE_ROT_LEFT);
	dungeon.player.dir = (dungeon.player.dir + 1) % 4;
	dungeon_load_view(dungeon.player.x, dungeon.player.y, dungeon.player.dir);
	dungeon_draw_view(VIEW_MODE_ROT_RIGHT);
	dungeon_draw_view(VIEW_MODE_STANDARD);
}

static void dungeon_rotate_right(void)
{
	dungeon_draw_view(VIEW_MODE_ROT_RIGHT);
	dungeon.player.dir = (dungeon.player.dir - 1) % 4;
	dungeon_load_view(dungeon.player.x, dungeon.player.y, dungeon.player.dir);
	dungeon_draw_view(VIEW_MODE_ROT_LEFT);
	dungeon_draw_view(VIEW_MODE_STANDARD);
}

uint16_t dungeon_move(enum dungeon_move_command cmd)
{
	switch (cmd) {
	case DUNGEON_MOVE_FORWARD:
		if (dungeon.view[VIEW_12_FRONT] & 0x8) {
			DUNGEON_LOG("dungeon_move_forward (blocked)");
			dungeon_draw_view(VIEW_MODE_FORWARD);
			dungeon_draw_view(VIEW_MODE_STANDARD);
			return 0xff01;
		}
		DUNGEON_LOG("dungeon_move_forward");
		dungeon_move_forward();
		break;
	case DUNGEON_MOVE_BACKWARD:
		// XXX: backing into doorway is not allowed; must be null wall behind
		if (dungeon.view[VIEW_12_BACK]) {
			DUNGEON_LOG("dungeon_move_backward (blocked)");
			dungeon_draw_view(VIEW_MODE_BACKWARD);
			dungeon_draw_view(VIEW_MODE_STANDARD);
			return 0xff01;
		}
		DUNGEON_LOG("dungeon_move_backward");
		dungeon_move_backward();
		break;
	case DUNGEON_ROTATE_LEFT:
		DUNGEON_LOG("dungeon_rotate_left");
		dungeon_rotate_left();
		break;
	case DUNGEON_ROTATE_RIGHT:
		DUNGEON_LOG("dungeon_rotate_right");
		dungeon_rotate_right();
		break;
	default:
		VM_ERROR("Invalid movement command: %d", cmd);
	}

	uint8_t id = dungeon.tile[dungeon.player.y][dungeon.player.x].id;
	if (id)
		return ((uint16_t)id << 8) | 0xff;
	if ((dungeon.moved_through_wall & 0x7) == 3) {
		// XXX: moved through a door, but no ID set on destination tile
		return 0xff02;
	}
	if ((dungeon.moved_through_wall & 0x7) == 1) {
		// XXX: moved through a window but no ID set on destination tile
		return 0xff03;
	}
	return 0;
}
