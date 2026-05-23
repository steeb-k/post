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

#pragma once

#include <adwaita.h>
#include <camel/camel.h>

#include "stamp-account.h"

G_BEGIN_DECLS

#define STAMP_TYPE_MESSAGE_HEADER (stamp_message_header_get_type ())
G_DECLARE_FINAL_TYPE (StampMessageHeader, stamp_message_header, STAMP, MESSAGE_HEADER, GtkGrid);

GtkWidget *stamp_message_header_new (StampAccount *account);

void
stamp_message_header_set_account (StampMessageHeader *self,
                                  StampAccount       *account);

void stamp_message_header_set_mail (StampMessageHeader    *self,
                                    CamelFolderThreadNode *thread_node);
void
stamp_message_header_set_collapsed (StampMessageHeader *self,
                                    gboolean            collapsed);

void
stamp_message_header_set_extern (StampMessageHeader *self,
                                 gboolean            is_extern);

void
stamp_message_header_set_internal (StampMessageHeader *self,
                                   gboolean            is_internal);

void
stamp_message_header_set_sender (StampMessageHeader *self,
                                 const char         *sender);

G_END_DECLS

