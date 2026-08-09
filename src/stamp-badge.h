/*
 * Copyright 2026 steeb-k
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

/*
 * A badge a profile can wear in place of its letter. @id is what the
 * settings store and never changes; a badge is drawn either from
 * @icon_name or, for the two of our own, from @resource. The catalogue
 * is static, so every field is owned by it.
 */
typedef struct {
  const gchar *id;
  const gchar *icon_name;
  const gchar *resource;
  const gchar *label;
} StampBadge;

/*
 * Every badge on offer, in the order the picker shows them. The first
 * entry is the empty id, which is the letter rather than a badge.
 */
const StampBadge *
stamp_badge_get_all (guint *n_badges);

/*
 * The badge @id names, or NULL when this version does not carry it. An
 * empty or NULL @id is the no-badge entry, which does resolve.
 */
const StampBadge *
stamp_badge_find (const gchar *id);

/*
 * Draws @badge on @avatar, falling back to @initial when @badge is the
 * no-badge entry. Leaves the colour alone; see
 * stamp_profile_button_style_avatar().
 */
void
stamp_badge_apply (AdwAvatar        *avatar,
                   const StampBadge *badge,
                   const gchar      *initial);

G_END_DECLS
