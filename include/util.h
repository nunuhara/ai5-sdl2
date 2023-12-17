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

#ifndef AI5_SDL2_UTIL_H
#define AI5_SDL2_UTIL_H

void util_warn_unimplemented(struct param_list *params);
void util_noop(struct param_list *params);
void util_get_text_colors(struct param_list *params);
void util_blink_fade(struct param_list *params);
void util_scale_h(struct param_list *params);
void util_invert_colors(struct param_list *params);
void util_fade(struct param_list *params);
void util_savedata_stash_name(struct param_list *params);
void util_pixelate(struct param_list *params);
void util_get_time(struct param_list *params);
void util_check_cursor(struct param_list *params);
void util_delay(struct param_list *params);
void util_save_animation(struct param_list *params);
void util_restore_animation(struct param_list *params);
void util_anim_save_running(struct param_list *params);
void util_copy_progressive(struct param_list *params);
void util_fade_progressive(struct param_list *params);
void util_anim_running(struct param_list *params);
void util_copy(struct param_list *params);
void util_bgm_play(struct param_list *params);
void util_bgm_is_playing(struct param_list *params);
void util_se_is_playing(struct param_list *params);
void util_get_ticks(struct param_list *params);
void util_wait_until(struct param_list *params);
void util_wait_until2(struct param_list *params);
void util_bgm_is_fading(struct param_list *params);

#endif // AI5_SDL2_UTIL_H
