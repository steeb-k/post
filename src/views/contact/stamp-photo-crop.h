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
#include <gio/gio.h>

G_BEGIN_DECLS

#define STAMP_TYPE_PHOTO_CROP (stamp_photo_crop_get_type ())
G_DECLARE_FINAL_TYPE (StampPhotoCrop, stamp_photo_crop, STAMP, PHOTO_CROP, AdwDialog);

/* Handed the cropped square, or NULL if the picture could not be read. */
typedef void (*StampPhotoCropReady) (GdkTexture *texture,
                                     gpointer    user_data);

void
stamp_photo_crop_present (GtkWidget           *parent,
                          GFile               *file,
                          StampPhotoCropReady  callback,
                          gpointer             user_data);

G_END_DECLS
