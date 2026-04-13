/*
 * Copyright 2024-2026 Jan-Michael Brummer
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
#include <libedataserver/libedataserver.h>

G_BEGIN_DECLS

#define STAMP_TYPE_CATEGORY (stamp_category_get_type ())

G_DECLARE_FINAL_TYPE (StampCategory, stamp_category, STAMP, CATEGORY, GObject)

StampCategory *
stamp_category_new (const char *id,
                    const char *name,
                    const char *color,
                    const char *color_hex);

const char *
stamp_category_get_id (StampCategory *self);

const char *
stamp_category_get_name (StampCategory *self);

const char *
stamp_category_get_color (StampCategory *self);

const char *
stamp_category_get_hex (StampCategory *self);

/* Helper */
GList *
stamp_m365_get_categories_sync (ESource       *source,
                                GCancellable  *cancellable,
                                GError       **error);

void
stamp_m365_get_categories_async (ESource             *source,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data);

GList *
stamp_m365_get_categories_finish (ESource       *source,
                                  GAsyncResult  *result,
                                  GError       **error);

G_END_DECLS

