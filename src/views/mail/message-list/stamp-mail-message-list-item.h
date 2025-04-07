/* stamp-mail-row.h
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

#include <adwaita.h>

#include <camel/camel.h>

#pragma once

G_BEGIN_DECLS

#define STAMP_TYPE_MAIL_MESSAGE_LIST_ITEM (stamp_mail_message_list_item_get_type ())

G_DECLARE_FINAL_TYPE (StampMailMessageListItem, stamp_mail_message_list_item, STAMP, MAIL_MESSAGE_LIST_ITEM, GtkListBoxRow);

GtkWidget *
stamp_mail_message_list_item_new (const CamelMessageInfo *message);

void
stamp_mail_message_list_item_set_expanded (StampMailMessageListItem *self,
                            gboolean      expanded);

G_END_DECLS

