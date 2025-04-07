/* stamp-folder-item-model.h
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

#include "stamp-account.h"
// #include "stamp-mail-folder.h"
#include "stamp-item-model.h"

#pragma once

G_BEGIN_DECLS

#define STAMP_TYPE_FOLDER_ITEM_MODEL (stamp_folder_item_model_get_type ())

G_DECLARE_FINAL_TYPE (StampFolderItemModel, stamp_folder_item_model, STAMP, FOLDER_ITEM_MODEL, GObject);

StampFolderItemModel *
stamp_folder_item_model_new (StampAccount *account);

void
stamp_folder_item_set_folder_info (StampFolderItemModel *self,
                                   CamelFolderInfo      *folder_info);

CamelFolderInfo *
stamp_folder_item_model_get_folder (StampFolderItemModel *self);

StampAccount *
stamp_folder_item_model_get_account (StampFolderItemModel *self);

CamelFolderInfo *
stamp_folder_item_model_get_folder_info (StampFolderItemModel *self);

G_END_DECLS

