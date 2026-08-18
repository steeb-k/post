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

#include "stamp-palette.h"

#include <glib/gi18n.h>

/* Named after the GNOME palette, so the circles sit well next to the
 * rest of the shell no matter which accent the user runs. */
static const StampPaletteColor palette[] = {
  { "blue",   N_("Blue"),   "#3584e4" },
  { "teal",   N_("Teal"),   "#2190a4" },
  { "green",  N_("Green"),  "#3a944a" },
  { "yellow", N_("Yellow"), "#c88800" },
  { "orange", N_("Orange"), "#ed5b00" },
  { "red",    N_("Red"),    "#e62d42" },
  { "pink",   N_("Pink"),   "#d56199" },
  { "purple", N_("Purple"), "#9141ac" },
  { "slate",  N_("Slate"),  "#6f8396" },
};

const StampPaletteColor *
stamp_palette_get (guint *n_colors)
{
  if (n_colors)
    *n_colors = G_N_ELEMENTS (palette);

  return palette;
}

const StampPaletteColor *
stamp_palette_lookup (const gchar *color_id)
{
  if (!color_id || !*color_id)
    return NULL;

  for (guint i = 0; i < G_N_ELEMENTS (palette); i++) {
    if (g_strcmp0 (palette[i].id, color_id) == 0)
      return &palette[i];
  }

  return NULL;
}

const StampPaletteColor *
stamp_palette_find (const gchar *color_id)
{
  const StampPaletteColor *color = stamp_palette_lookup (color_id);

  return color ? color : &palette[0];
}
