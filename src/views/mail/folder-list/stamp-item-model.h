/* stamp-item-model.h
 *
 * Copyright 2024 Jan-Michael Brummer
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

#include <gtk/gtk.h>

// #include "stamp-folder-item-model.h"

#pragma once

G_BEGIN_DECLS

#define STAMP_TYPE_ITEM_MODEL (stamp_item_model_get_type ())

G_DECLARE_INTERFACE (StampItemModel, stamp_item_model, STAMP, ITEM_MODEL, GObject);

struct _StampItemModelInterface {
  GTypeInterface parent_iface;

  const char *(*get_name)(StampItemModel *self);
  void (*set_name)(StampItemModel *self, const char *name);
  const char *(*get_icon_name)(StampItemModel *self);
  void (*set_icon_name)(StampItemModel *self, const char *icon_name);
  const char *(*get_account_uid)(StampItemModel *self);
  void (*set_account_uid)(StampItemModel *self, const char *account_uid);
  GListStore *(*get_folder_list)(StampItemModel *self);
};

// StampItemModel *
// stamp_item_model_new (void);

void
stamp_item_model_set_icon_name (StampItemModel *self,
                                const char     *icon_name);

const char *
stamp_item_model_get_icon_name (StampItemModel *self);

void
stamp_item_model_set_name (StampItemModel *self,
                           const char     *name);

const char *
stamp_item_model_get_name (StampItemModel *self);

GListStore *
stamp_item_model_get_folder_list (StampItemModel *self);

const char *
stamp_item_model_get_account_uid (StampItemModel *self);

G_END_DECLS

