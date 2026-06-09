/*
 * Copyright 2025-2026 Jan-Michael Brummer
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

#include <glib.h>
#include <gtk/gtk.h>

#include "stamp-account.h"

#pragma once

G_BEGIN_DECLS

#define STAMP_TYPE_ITEM (stamp_item_get_type ())

G_DECLARE_DERIVABLE_TYPE (StampItem, stamp_item, STAMP, ITEM, GObject);

struct _StampItemClass
{
  GObjectClass parent_class;
};

void
stamp_item_set_icon_name (StampItem  *self,
                          const gchar *icon_name);

const gchar *
stamp_item_get_icon_name (StampItem *self);

void
stamp_item_set_name (StampItem  *self,
                     const gchar *name);

const gchar *
stamp_item_get_name (StampItem *self);

GListStore *
stamp_item_get_list_store (StampItem *self);

const gchar *
stamp_item_get_account_uid (StampItem *self);

void
stamp_item_set_list_store_type (StampItem *self,
                                GType      type);

StampAccount *
stamp_item_get_account (StampItem *self);

void
stamp_item_set_loading (StampItem *self,
                        gboolean   loading);

void
stamp_item_set_error (StampItem *self,
                      GError    *error);

GError *
stamp_item_get_error (StampItem *self);

G_END_DECLS

