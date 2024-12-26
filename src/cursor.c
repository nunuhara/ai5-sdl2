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
#include <stdatomic.h>
#include <string.h>
#include <SDL.h>

#include "nulib.h"
#include "nulib/buffer.h"
#include "nulib/file.h"
#include "nulib/little_endian.h"

#include "ai5.h"
#include "ai5/cg.h"
#include "cursor.h"
#include "gfx_private.h"
#include "input.h"

#pragma pack(1)

#define NR_DIRECTORY_ENTRIES 16
#define SECTION_NAME_SIZE 8
#define DIRECTORY_ENTRY_RESOURCE 2
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x80

struct dos_header {
	uint16_t magic;
	uint16_t cblp;
	uint16_t cp;
	uint16_t crlc;
	uint16_t cparhdr;
	uint16_t minalloc;
	uint16_t maxalloc;
	uint16_t ss;
	uint16_t sp;
	uint16_t csum;
	uint16_t ip;
	uint16_t cs;
	uint16_t lfarlc;
	uint16_t ovno;
	uint16_t res[4];
	uint16_t oemid;
	uint16_t oeminfo;
	uint16_t res2[10];
	uint32_t lfanew;
};

// COFF header
struct coff_header {
	uint16_t machine;
	uint16_t number_of_sections;
	uint32_t time_date_stamp;
	uint32_t pointer_to_symbol_table;
	uint32_t number_of_symbols;
	uint16_t size_of_optional_header;
	uint16_t characteristics;
};

struct data_directory {
	uint32_t virtual_address;
	uint32_t size;
};

// PE optional header
struct optional_header {
	uint16_t magic;
	uint8_t major_linker_version;
	uint8_t minor_linker_version;
	uint32_t size_of_code;
	uint32_t size_of_initialized_data;
	uint32_t size_of_uninitialized_data;
	uint32_t address_of_entry_point;
	uint32_t base_of_code;
	uint32_t base_of_data;
	uint32_t image_base;
	uint32_t section_alignment;
	uint32_t file_alignment;
	uint16_t major_operating_system_version;
	uint16_t minor_operating_system_version;
	uint16_t major_image_version;
	uint16_t minor_image_version;
	uint16_t major_subsystem_version;
	uint16_t minor_subsystem_version;
	uint32_t win32_version_value;
	uint32_t size_of_image;
	uint32_t size_of_headers;
	uint32_t checksum;
	uint16_t subsystem;
	uint16_t dll_characteristics;
	uint32_t size_of_stack_reserve;
	uint32_t size_of_stack_commit;
	uint32_t size_of_heap_reserve;
	uint32_t size_of_heap_commit;
	uint32_t loader_flags;
	uint32_t number_of_rva_and_sizes;
	struct data_directory data_directory[NR_DIRECTORY_ENTRIES];
};

struct pe_header {
	uint32_t signature;
	struct coff_header coff;
	struct optional_header opt;
};

// header describing section in executable image
struct section_header {
	uint8_t name[SECTION_NAME_SIZE];
	union {
		uint32_t physical_address;
		uint32_t virtual_size;
	} misc;
	uint32_t virtual_address;
	uint32_t size_of_raw_data;
	uint32_t pointer_to_raw_data;
	uint32_t pointer_to_relocations;
	uint32_t pointer_to_linenumbers;
	uint16_t number_of_relocations;
	uint16_t number_of_linenumbers;
	uint32_t characteristics;
};

// resource directory table
struct res_dir_table {
	uint32_t characteristics;
	uint32_t time_date_stamp;
	uint16_t major_version;
	uint16_t minor_version;
	uint16_t number_of_named_entries;
	uint16_t number_of_id_entries;
};

// entry in resource directory table
struct res_dir_entry {
	uint32_t id;
	uint32_t off;
};

// leaf data of resource directory table
struct res_data_entry {
	uint32_t offset_to_data;
	uint32_t size;
	uint32_t code_page;
	uint32_t resource_handle;
};

// group_cursor resource data (table header)
struct cursor_dir {
	uint16_t reserved;
	uint16_t type;
	uint16_t count;
};

// entry in group_cursor table
struct cursor_dir_entry {
	uint16_t width;
	uint16_t height;
	uint16_t plane_count;
	uint16_t bit_count;
	uint32_t bytes_in_res;
	uint16_t res_id;
};

// group_icon resource data (table header)
struct icon_dir {
	uint16_t reserved;
	uint16_t type;
	uint16_t count;
};

// entry in group_icon table
struct icon_dir_entry {
	uint8_t width;
	uint8_t height;
	uint8_t color_count;
	uint8_t reserved;
	uint16_t plane_count;
	uint16_t bit_count;
	uint32_t bytes_in_res;
	uint16_t res_id;
};

// BITMAPINFOHEADER
struct bitmap_info {
	uint32_t size;
	uint32_t width;
	uint32_t height;
	uint16_t planes;
	uint16_t bpp;
	uint32_t compression;
	uint32_t size_image;
	uint32_t ppm_x;
	uint32_t ppm_y;
	uint32_t colors_used;
	uint32_t colors_important;
};

struct color_map {
	struct { uint8_t b, g, r, u; } colors[256];
};

// cursor resource data
struct cursor_data {
	uint16_t hotspot_x;
	uint16_t hotspot_y;
	struct bitmap_info bm_info;
	struct color_map colors;
};

// icon resource data
struct icon_data {
	struct bitmap_info bm_info;
	struct color_map colors;
};

struct resource {
	uint32_t id;
	struct resource *children;
	unsigned nr_children;
	struct {
		uint32_t addr;
		uint32_t size;
	} leaf;
};

struct vma {
	uint32_t virt_addr;
	uint32_t size;
	uint32_t raw_addr;
	bool initialized;
};

// TODO: use this abstraction for memory reads on executable image
struct vmm {
	struct vma *vmas;
	unsigned nr_vmas;
	struct buffer exe;
};

uint8_t *vmm_lookup(uint32_t ptr);

#pragma pack()

static bool buffer_expect_string(struct buffer *buf, const char *magic, size_t magic_size)
{
	if (buffer_remaining(buf) < magic_size)
		return false;
	return !strncmp(buffer_strdata(buf), magic, magic_size);
}

static uint16_t buffer_read_u16_at(struct buffer *buf, size_t off)
{
	size_t tmp = buf->index;
	buf->index = off;
	uint32_t v = buffer_read_u16(buf);
	buf->index = tmp;
	return v;
}

static uint32_t buffer_read_u32_at(struct buffer *buf, size_t off)
{
	size_t tmp = buf->index;
	buf->index = off;
	uint32_t v = buffer_read_u32(buf);
	buf->index = tmp;
	return v;
}

#define read_member16(b, s, m) \
	buffer_read_u16_at(b, b->index + offsetof(s, m));
#define read_member32(b, s, m) \
	buffer_read_u32_at(b, b->index + offsetof(s, m));

static bool read_pe_header(struct buffer *buf, uint32_t *addr_out, uint32_t *size_out,
		struct vma **vma_out, uint16_t *nr_vmas_out)
{
	attr_unused size_t pe_loc = buf->index;
	if (buffer_remaining(buf) < sizeof(struct pe_header))
		return false;

	if (buffer_read_u32(buf) != 0x4550)
		return false;

	uint16_t nr_sections = read_member16(buf, struct coff_header, number_of_sections);
	uint16_t opt_size = read_member16(buf, struct coff_header, size_of_optional_header);

	buffer_skip(buf, sizeof(struct coff_header));
	assert(buf->index == pe_loc + offsetof(struct pe_header, opt));
	size_t opt_pos = buf->index;
	buffer_skip(buf, offsetof(struct optional_header, data_directory));
	buffer_skip(buf, sizeof(struct data_directory) * DIRECTORY_ENTRY_RESOURCE);

	*addr_out = read_member32(buf, struct data_directory, virtual_address);
	*size_out = read_member32(buf, struct data_directory, size);

	buffer_seek(buf, opt_pos + opt_size);
	if (buffer_remaining(buf) < nr_sections * sizeof(struct section_header))
		return false;

	struct vma *vma = xmalloc(nr_sections * sizeof(struct vma));
	for (int i = 0; i < nr_sections; i++) {
		vma[i].virt_addr = read_member32(buf, struct section_header, virtual_address);
		vma[i].size = read_member32(buf, struct section_header, size_of_raw_data);
		vma[i].raw_addr = read_member32(buf, struct section_header, pointer_to_raw_data);
		uint32_t characteristics = read_member32(buf, struct section_header, characteristics);
		vma[i].initialized = !(characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA);
		buffer_skip(buf, sizeof(struct section_header));
	}

	*vma_out = vma;
	*nr_vmas_out = nr_sections;
	return true;
}

static bool read_res_dir_table(struct buffer *b, struct res_dir_entry **entry_out,
		uint16_t *nr_named_out, uint16_t *nr_id_out)
{
	if (buffer_remaining(b) < sizeof(struct res_dir_table))
		return false;

	uint16_t nr_named = read_member16(b, struct res_dir_table, number_of_named_entries);
	uint16_t nr_id = read_member16(b, struct res_dir_table, number_of_id_entries);
	unsigned nr_entries = nr_named + nr_id;
	buffer_skip(b, sizeof(struct res_dir_table));
	if (buffer_remaining(b) < nr_entries * sizeof(struct res_dir_entry))
		return false;

	struct res_dir_entry *entry = xmalloc(nr_entries * sizeof(struct res_dir_entry));
	for (unsigned i = 0; i < nr_entries; i++) {
		entry[i].id = buffer_read_u32(b);
		entry[i].off = buffer_read_u32(b);
	}

	*entry_out = entry;
	*nr_named_out = nr_named;
	*nr_id_out = nr_id;
	return true;
}

bool read_resource_data(struct buffer *b, struct resource *node)
{
	if (buffer_remaining(b) < sizeof(struct res_data_entry))
		return false;
	node->leaf.addr = buffer_read_u32(b);
	node->leaf.size = buffer_read_u32(b);
	return true;
}

bool read_resource_table(struct buffer *b, struct resource *node, uint32_t res_base)
{
	uint16_t nr_named, nr_id;
	struct res_dir_entry *entry = NULL;
	if (!read_res_dir_table(b, &entry, &nr_named, &nr_id))
		goto error;

	node->nr_children = nr_named + nr_id;
	node->children = xcalloc(node->nr_children, sizeof(struct resource));

	for (unsigned i = 0; i < node->nr_children; i++) {
		node->children[i].id = entry[i].id;
		buffer_seek(b, res_base + (entry[i].off & 0x7fffffff));
		if (entry[i].off & 0x80000000) {
			if (!read_resource_table(b, &node->children[i], res_base))
				goto error;
		} else {
			if (!read_resource_data(b, &node->children[i]))
				goto error;
		}
	}

	free(entry);
	return true;
error:
	free(entry);
	free(node->children);
	return false;
}

struct resource *read_resources(struct buffer *b)
{
	struct resource *root = xcalloc(1, sizeof(struct resource));
	if (read_resource_table(b, root, b->index))
		return root;

	free(root);
	return NULL;
}

void _free_resources(struct resource *res)
{
	for (unsigned i = 0; i < res->nr_children; i++) {
		_free_resources(&res->children[i]);
	}
	free(res->children);
}

void free_resources(struct resource *root)
{
	_free_resources(root);
	free(root);
}

void read_bitmap_info(struct buffer *b, struct bitmap_info *bm_info)
{
	bm_info->size = buffer_read_u32(b);
	bm_info->width = buffer_read_u32(b);
	bm_info->height = buffer_read_u32(b);
	bm_info->planes = buffer_read_u16(b);
	bm_info->bpp = buffer_read_u16(b);
	bm_info->compression = buffer_read_u32(b);
	bm_info->size_image = buffer_read_u32(b);
	bm_info->ppm_x = buffer_read_u32(b);
	bm_info->ppm_y = buffer_read_u32(b);
	bm_info->colors_used = buffer_read_u32(b);
	bm_info->colors_important = buffer_read_u32(b);
}

bool read_cursor_data(struct buffer *b, struct cursor_data *cur, uint8_t **xor_bitmap,
		uint8_t **and_bitmap)
{
	if (buffer_remaining(b) < sizeof(struct cursor_data))
		return false;
	cur->hotspot_x = buffer_read_u16(b);
	cur->hotspot_y = buffer_read_u16(b);
	read_bitmap_info(b, &cur->bm_info);
	// only simple monochrome bitmaps are supported
	if (cur->bm_info.planes != 1)
		return false;
	if (cur->bm_info.bpp != 1 && cur->bm_info.bpp != 8)
		return false;
	if (cur->bm_info.compression != 0)
		return false;
	if (cur->bm_info.colors_used != 0 && cur->bm_info.colors_used != 2
			&& cur->bm_info.colors_used != 256)
		return false;
	unsigned nr_colors = max(2, cur->bm_info.colors_used);
	for (unsigned i = 0; i < nr_colors; i++) {
		cur->colors.colors[i].b = buffer_read_u8(b);
		cur->colors.colors[i].g = buffer_read_u8(b);
		cur->colors.colors[i].r = buffer_read_u8(b);
		cur->colors.colors[i].u = buffer_read_u8(b);
	}
	*xor_bitmap = (uint8_t*)buffer_strdata(b);
	if (cur->bm_info.bpp == 8) {
		buffer_skip(b, cur->bm_info.width * (cur->bm_info.height / 2));
	} else {
		buffer_skip(b, cur->bm_info.size_image / 2);
	}
	*and_bitmap = (uint8_t*)buffer_strdata(b);
	return true;
}

static struct cg *load_bmp_8bpp(struct bitmap_info *bm_info, struct color_map *colors,
		uint8_t *pixels, uint8_t *bitmask)
{
	unsigned w = bm_info->width;
	unsigned h = bm_info->height / 2;

	// expand mask
	uint8_t *mask = xmalloc(w * h);
	for (int row = 0; row < h; row++) {
		uint8_t *dst = mask + row * w;
		uint8_t *src = bitmask + row * (w/8);
		for (int bit = 0x80, col = 0; col < w; bit >>= 1, col++, dst++) {
			if (bit == 0) {
				bit = 0x80;
				src++;
			}
			*dst = (*src & bit) ? 0 : 255;
		}
	}

	// convert pixels/mask to RGBA CG
	struct cg *cg = cg_alloc_direct(w, h);
	for (unsigned row = 0; row < h; row++) {
		uint8_t *dst = cg->pixels + row * w * 4;
		uint8_t *p_src = pixels + (h - (row + 1)) * w;
		uint8_t *m_src = mask + (h - (row + 1)) * w;
		for (unsigned col = 0; col < w; col++, p_src++, m_src++, dst += 4) {
			uint8_t c = *p_src;
			dst[0] = colors->colors[c].r;
			dst[1] = colors->colors[c].g;
			dst[2] = colors->colors[c].b;
			dst[3] = *m_src;
		}
	}

	free(mask);
	return cg;
}

static SDL_Cursor *load_color_cursor(struct cursor_data *data, uint8_t *pixels,
		uint8_t *bitmask)
{
	struct cg *cg = load_bmp_8bpp(&data->bm_info, &data->colors, pixels, bitmask);
	SDL_Surface *s;
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormatFrom, s, cg->pixels, cg->metrics.w,
			cg->metrics.h, 32, cg->metrics.w * 4, SDL_PIXELFORMAT_RGBA32);

	SDL_Cursor *c = SDL_CreateColorCursor(s, data->hotspot_x, data->hotspot_y);
	if (!c)
		WARNING("SDL_CreateColorCursor: %s", SDL_GetError());
	SDL_FreeSurface(s);
	cg_free(cg);
	return c;
}

static SDL_Cursor *load_monochrome_cursor(struct cursor_data *cur, uint8_t *xor_bitmap,
		uint8_t *and_bitmap)
{
	int stride = cur->bm_info.width / 8 + (cur->bm_info.width % 8 ? 1 : 0);
	stride = (stride + 3) & ~3;

	// convert masks to format expected by SDL
	uint8_t *data = xcalloc(cur->bm_info.height / 2, cur->bm_info.width / 8);
	uint8_t *mask = xcalloc(cur->bm_info.height / 2, cur->bm_info.width / 8);
	for (int row = 0; row < cur->bm_info.height / 2; row++) {
		int dst_byte = (cur->bm_info.height / 2 - (row + 1)) * stride;
		int src_byte = row * stride;
		for (int col = 0; col < cur->bm_info.width / 8; col++) {
			for (int bit = 7; bit >= 0; bit--) {
				uint8_t xor = (xor_bitmap[src_byte] >> bit) & 1;
				uint8_t and = (and_bitmap[src_byte] >> bit) & 1;

				if (and) {
					// nothing (transparent)
				} else if (!xor) {
					// black
					data[dst_byte] |= 1 << bit;
					mask[dst_byte] |= 1 << bit;
				} else {
					// white
					mask[dst_byte] |= 1 << bit;
				}
			}
			dst_byte++;
			src_byte++;
		}
	}

	SDL_Cursor *c = SDL_CreateCursor(data, mask,
			cur->bm_info.width, cur->bm_info.height / 2,
			cur->hotspot_x, cur->hotspot_y);
	free(data);
	free(mask);
	if (!c)
		WARNING("SDL_CreateCursor: %s", SDL_GetError());
	return c;
}

SDL_Cursor *read_cursor(struct buffer *b, struct resource *res)
{
	struct cursor_data cur;
	uint8_t *xor_bitmap = NULL;
	uint8_t *and_bitmap = NULL;
	buffer_seek(b, res->leaf.addr);
	if (!read_cursor_data(b, &cur, &xor_bitmap, &and_bitmap))
		return NULL;

	if (cur.bm_info.bpp == 8)
		return load_color_cursor(&cur, xor_bitmap, and_bitmap);
	return load_monochrome_cursor(&cur, xor_bitmap, and_bitmap);
}

int read_group_cursor(struct buffer *b, struct resource *res)
{
	buffer_seek(b, res->leaf.addr);
	if (buffer_remaining(b) < sizeof(struct cursor_dir))
		return -1;

	uint16_t count = read_member16(b, struct cursor_dir, count);
	if (count < 1) {
		WARNING("group_cursor directory contains no entries");
		return -1;
	}
	if (count > 1)
		WARNING("Ignoring additional entries in group_cursor directory");

	buffer_skip(b, sizeof(struct cursor_dir));
	buffer_skip(b, offsetof(struct cursor_dir_entry, res_id));
	return buffer_read_u16(b);
}

bool read_icon_data(struct buffer *b, struct icon_data *icon, uint8_t **image, uint8_t **bitmask)
{
	if (buffer_remaining(b) < sizeof(struct icon_data))
		return false;
	read_bitmap_info(b, &icon->bm_info);
	if (icon->bm_info.planes != 1)
		return false;
	if (icon->bm_info.bpp != 8)
		return false;
	if (icon->bm_info.compression != 0)
		return false;
	for (unsigned i = 0; i < 256; i++) {
		icon->colors.colors[i].b = buffer_read_u8(b);
		icon->colors.colors[i].g = buffer_read_u8(b);
		icon->colors.colors[i].r = buffer_read_u8(b);
		icon->colors.colors[i].u = buffer_read_u8(b);
	}
	*image = (uint8_t*)buffer_strdata(b);
	buffer_skip(b, icon->bm_info.width * (icon->bm_info.height / 2));
	*bitmask = (uint8_t*)buffer_strdata(b);
	return true;
}

SDL_Surface *read_icon(struct buffer *b, struct resource *res, struct cg **cg_out)
{
	struct icon_data icon;
	uint8_t *image;
	uint8_t *bitmask;
	buffer_seek(b, res->leaf.addr);
	if (!read_icon_data(b, &icon, &image, &bitmask)) {
		//WARNING("failed to read icon data");
		return NULL;
	}

	if (icon.bm_info.bpp != 8) {
		WARNING("icon bpp is not 8: %d", icon.bm_info.bpp);
		return NULL;
	}

	struct cg *cg = load_bmp_8bpp(&icon.bm_info, &icon.colors, image, bitmask);
	SDL_Surface *s;
	SDL_CTOR(SDL_CreateRGBSurfaceWithFormatFrom, s, cg->pixels, cg->metrics.w,
		cg->metrics.h, 32, cg->metrics.w * 4, SDL_PIXELFORMAT_RGBA32);
	*cg_out = cg;
	return s;
}

int read_group_icon(struct buffer *b, struct resource *res)
{
	buffer_seek(b, res->leaf.addr);
	if (buffer_remaining(b) < sizeof(struct icon_dir))
		return -1;

	uint16_t count = read_member16(b, struct icon_dir, count);
	if (count < 1) {
		WARNING("group_icon directory contains no entries");
		return -1;
	}
	//if (count > 1)
	//	WARNING("Ignoring additional entries in group_icon directory");

	buffer_skip(b, sizeof(struct icon_dir));
	buffer_skip(b, offsetof(struct icon_dir_entry, res_id));
	return buffer_read_u16(b);
}

#define CURSOR_MAX_FRAMES 4
static SDL_Cursor *system_cursor = NULL;
static SDL_Cursor **cursors = NULL;
static int nr_cursors = 0;
static int current_cursor = 0;
static bool cursor_animating = false;
static bool cursor_loaded = false;
static atomic_uint cursor_nr_frames = 2;
static atomic_uint cursor_frame = 0;
static atomic_uint cursor_frame_time[CURSOR_MAX_FRAMES] = { 500, 500 };

static int nr_icons = 0;
static SDL_Surface **icons = NULL;
static struct cg **icon_cgs = NULL;

void read_cursors(struct buffer *b, struct resource *root)
{
	struct resource *cursor = NULL;
	struct resource *group_cursor = NULL;
	struct resource *icon = NULL;
	struct resource *group_icon = NULL;
	for (unsigned i = 0; i < root->nr_children; i++) {
		if (root->children[i].id == 1) {
			cursor = &root->children[i];
		} else if (root->children[i].id == 12) {
			group_cursor = &root->children[i];
		} else if (root->children[i].id == 3) {
			icon = &root->children[i];
		} else if (root->children[i].id == 14) {
			group_icon = &root->children[i];
		}
	}
	if (!cursor || !group_cursor) {
		WARNING("No cursors found in executable");
		return;
	}

	nr_cursors = group_cursor->nr_children;
	cursors = xcalloc(nr_cursors, sizeof(struct SDL_Cursor*));
	for (unsigned i = 0; i < group_cursor->nr_children; i++) {
		struct resource *child = &group_cursor->children[i];
		if (child->nr_children != 1 || child->children[0].nr_children != 0) {
			WARNING("Unexpected resource layout (cursor %d)", i);
			continue;
		}
		int res_id = read_group_cursor(b, &child->children[0]);
		if (res_id < 0)
			continue;

		// get the corresponding cursor resource
		struct resource *cur_res = NULL;
		for (unsigned j = 0; j < cursor->nr_children; j++) {
			struct resource *child = &cursor->children[j];
			if (child->id == res_id) {
				if (child->nr_children != 1 || child->children[0].nr_children != 0) {
					WARNING("Unexpected resource layout (cursor %d)", i);
					continue;
				}
				cur_res = &child->children[0];
			}
		}
		if (!cur_res) {
			WARNING("Couldn't find cursor for group_cursor %d", i);
			continue;
		}
		cursors[i] = read_cursor(b, cur_res);
	}

	nr_icons = group_icon->nr_children;
	icons = xcalloc(nr_icons, sizeof(struct SDL_Surface*));
	icon_cgs = xcalloc(nr_icons, sizeof(struct cg*));
	for (unsigned i = 0; i < group_icon->nr_children; i++) {
		struct resource *child = &group_icon->children[i];
		if (child->nr_children != 1 || child->children[0].nr_children != 0) {
			WARNING("Unexpected resource layout (icon %d)", i);
			continue;
		}
		int res_id = read_group_icon(b, &child->children[0]);
		if (res_id < 0)
			continue;

		// get the corresponding icon resource
		struct resource *icon_res = NULL;
		for (unsigned j = 0; j < icon->nr_children; j++) {
			struct resource *child = &icon->children[j];
			if (child->id == res_id) {
				if (child->nr_children != 1 || child->children[0].nr_children != 0) {
					WARNING("Unexpected resource layout (icon %d)", i);
					continue;
				}
				icon_res = &child->children[0];
			}
		}
		if (!icon_res) {
			WARNING("Couldn't find icon for group_icon %d", i);
			continue;
		}
		icons[i] = read_icon(b, icon_res, &icon_cgs[i]);
	}
}

SDL_Surface *icon_get(unsigned no)
{
	return no < nr_icons ? icons[no] : NULL;
}

static void cursor_fini(void)
{
	for (int i = 0; i < nr_cursors; i++) {
		SDL_FreeCursor(cursors[i]);
	}
	free(cursors);
}

static uint32_t anim_cb(uint32_t interval, void *_)
{
	SDL_Event event = {0};
	event.type = cursor_swap_event;
	SDL_PushEvent(&event);
	return cursor_frame_time[(cursor_frame + 1) % cursor_nr_frames];
}

void cursor_init(const char *exe_path)
{
	system_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);

	struct buffer buf;
	struct vma *vma = NULL;
	struct res_dir_entry *entry = NULL;

	buf.buf = file_read(exe_path, &buf.size);
	buf.index = 0;
	if (!buf.buf) {
		WARNING("Failed to read executable");
		return;
	}

	// expect DOS header
	if (!buffer_expect_string(&buf, "MZ", 2))
		goto error;
	if (buffer_remaining(&buf) < sizeof(struct dos_header))
		goto error;

	// seek to PE header
	buffer_skip(&buf, offsetof(struct dos_header, lfanew));
	buffer_seek(&buf, buffer_read_u32(&buf));
	if (!buffer_expect_string(&buf, "PE\0\0", 4))
		goto error;

	// read relevant data from PE header
	uint32_t res_virt_addr;
	uint32_t res_virt_size;
	uint16_t nr_vmas;
	if (!read_pe_header(&buf, &res_virt_addr, &res_virt_size, &vma, &nr_vmas))
		goto error;

	// calculate size of expanded process image
	size_t vma_size = 0;
	for (int i = 0; i < nr_vmas; i++) {
		vma_size = max(vma_size, vma[i].virt_addr + vma[i].size);
	}
	if (vma_size < buf.size)
		goto error;

	// load sections into memory at correct offsets
	// TODO: use an abstraction for virtual memory lookups that doesn't involve
	//       copying memory
	buf.buf = realloc(buf.buf, vma_size);
	buf.size = vma_size;
	for (int i = nr_vmas - 1; i >= 0; i--) {
		if (!vma[i].initialized)
			continue;
		if (vma[i].virt_addr + vma[i].size > buf.size)
			goto error;
		if (vma[i].raw_addr + vma[i].size > buf.size)
			goto error;
		if (vma[i].virt_addr != vma[i].raw_addr) {
			memmove(buf.buf + vma[i].virt_addr, buf.buf + vma[i].raw_addr, vma[i].size);
		}
	}

	if (res_virt_addr + res_virt_size > buf.size)
		goto error;

	buffer_seek(&buf, res_virt_addr);
	struct resource *root = read_resources(&buf);
	if (!root)
		goto error;

	read_cursors(&buf, root);
	atexit(cursor_fini);

	SDL_AddTimer(cursor_frame_time[0], anim_cb, NULL);

	free_resources(root);
	free(buf.buf);
	free(vma);
	free(entry);
	return;
error:
	WARNING("Invalid/unexpected executable format: %s", exe_path);
	free(buf.buf);
	free(vma);
	free(entry);
}

#if 0
#define CURSOR_LOG(...) NOTICE(__VA_ARGS__)
#else
#define CURSOR_LOG(...)
#endif

void cursor_load(unsigned no, unsigned nr_frames, unsigned *frame_time)
{
	assert(nr_frames <= CURSOR_MAX_FRAMES);
	CURSOR_LOG("cursor_load(%u,%u)", no, nr_frames);
	if (no + nr_frames > nr_cursors) {
		WARNING("Invalid cursor number: %u+%u", no, nr_frames);
		return;
	}
	for (unsigned i = 0; i < nr_frames; i++) {
		if (!cursors[no+i]) {
			WARNING("Frame %u not loaded for cursor %u", i, no);
			return;
		}
	}
	current_cursor = no;
	if (frame_time) {
		for (unsigned i = 0; i < nr_frames; i++) {
			cursor_frame_time[i] = frame_time[i];
		}
	} else {
		cursor_frame_time[0] = 500;
		cursor_frame_time[1] = 500;
	}
	cursor_nr_frames = nr_frames;
	cursor_frame = nr_frames - 1;
	cursor_animating = true;
	cursor_loaded = true;
	cursor_swap();
}

void cursor_unload(void)
{
	CURSOR_LOG("cursor_unload()");
	cursor_animating = false;
	cursor_loaded = false;
	SDL_SetCursor(system_cursor);
}

void cursor_reload(void)
{
	CURSOR_LOG("cursor_reload()");
	unsigned frame_time[CURSOR_MAX_FRAMES];
	for (int i = 0; i < cursor_nr_frames; i++) {
		frame_time[i] = cursor_frame_time[i];
	}
	cursor_load(current_cursor, cursor_nr_frames, frame_time);
}

void cursor_show(void)
{
	CURSOR_LOG("cursor_show()");
	if (cursor_loaded)
		cursor_animating = true;
	SDL_ShowCursor(SDL_ENABLE);
}

void cursor_hide(void)
{
	CURSOR_LOG("cursor_hide()");
	cursor_animating = false;
	SDL_ShowCursor(SDL_DISABLE);
}

void cursor_set_pos(unsigned x, unsigned y)
{
	CURSOR_LOG("cursor_set_pos(%u,%u)", x, y);
	if (config.no_warp_mouse)
		return;
	int wx, wy;
	SDL_RenderLogicalToWindow(gfx.renderer, x, y, &wx, &wy);
	SDL_WarpMouseInWindow(gfx.window, wx, wy);
}

void cursor_get_pos(unsigned *x_out, unsigned *y_out)
{
	int x, y;
	SDL_GetMouseState(&x, &y);

	float fx, fy;
	SDL_RenderWindowToLogical(gfx.renderer, x, y, &fx, &fy);
	*x_out = fx < 0 ? 0 : (fx >= gfx_view.w ? gfx_view.w - 1 : (unsigned)fx);
	*y_out = fy < 0 ? 0 : (fy >= gfx_view.h ? gfx_view.h - 1 : (unsigned)fy);
}

void cursor_swap(void)
{
	if (!cursor_animating)
		return;

	cursor_frame = (cursor_frame + 1) % cursor_nr_frames;
	SDL_SetCursor(cursors[current_cursor + cursor_frame]);
}

static enum cursor_direction cursor_dir = CURSOR_DIR_NONE;

void cursor_set_direction(enum cursor_direction dir)
{
	cursor_dir = dir;
}

enum cursor_direction cursor_get_direction(void)
{
	return cursor_dir;
}
