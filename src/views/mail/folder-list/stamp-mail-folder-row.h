/* stamp-folder-row.h
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

#pragma once

#include <gtk/gtk.h>

// #include "stamp-mail-folder.h"
#include "stamp-item-model.h"

G_BEGIN_DECLS

#define STAMP_TYPE_MAIL_FOLDER_ROW (stamp_mail_folder_row_get_type ())
G_DECLARE_FINAL_TYPE (StampMailFolderRow, stamp_mail_folder_row, STAMP, MAIL_FOLDER_ROW, GtkBox);

struct _StampMailFolderRow {
  GtkBox parent_instance;
};

GtkWidget *
stamp_mail_folder_row_new (void);

// StampMailFolder *
// stamp_mail_folder_row_get_folder (StampMailFolderRow *self);

void
stamp_mail_folder_row_bind (StampMailFolderRow *self,
                            StampItemModel     *item_model);

G_END_DECLS

