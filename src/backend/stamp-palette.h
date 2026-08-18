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

#include <glib-object.h>

G_BEGIN_DECLS

/*
 * The one palette everything the user colours by hand draws from --
 * profiles and folders alike. Sharing it is the point: a folder tinted
 * blue and a profile tinted blue are the same blue, and a colour added
 * here shows up in both pickers at once.
 */
typedef struct {
  const gchar *id;
  const gchar *name;
  const gchar *hex;
} StampPaletteColor;

const StampPaletteColor *
stamp_palette_get (guint *n_colors);

/*
 * Falls back to the first entry for an unknown @color_id, so anything
 * holding a stale id still renders as something.
 */
const StampPaletteColor *
stamp_palette_find (const gchar *color_id);

/*
 * Unlike stamp_palette_find(), answers NULL for an id the palette does
 * not have -- for callers that must tell "no colour set" apart from
 * "coloured blue".
 */
const StampPaletteColor *
stamp_palette_lookup (const gchar *color_id);

G_END_DECLS
