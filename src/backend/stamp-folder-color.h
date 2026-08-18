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

#define STAMP_TYPE_FOLDER_COLORS (stamp_folder_colors_get_type ())

G_DECLARE_FINAL_TYPE (StampFolderColors, stamp_folder_colors, STAMP, FOLDER_COLORS, GObject);

/*
 * The colours the user hands out to folders, kept in one place so no
 * widget has to know they live in GSettings. Accounts are addressed by
 * uid rather than by StampAccount: the dconf path is built from the uid
 * everywhere else too, and asking for a string keeps this module out of
 * the account's include graph.
 *
 * Emits "changed" -- with the account uid and folder full name, or NULL
 * for both when a whole account's colours were rewritten at once -- so
 * the sidebar and the conversation rows repaint together.
 */
StampFolderColors *
stamp_folder_colors_get_default (void);

/* NULL when the folder wears no colour, so "uncoloured" and "blue" stay
 * distinguishable. The returned id is owned by the palette. */
const gchar *
stamp_folder_color_lookup (const gchar *account_uid,
                           const gchar *full_name);

/* Passing NULL or "" for @color_id takes the colour away again. */
void
stamp_folder_color_set (const gchar *account_uid,
                        const gchar *full_name,
                        const gchar *color_id);

G_END_DECLS
