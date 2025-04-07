/*
 * Copyright 2025 Jan-Michael Brummer
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

#include "stamp-conversation-item.h"

G_BEGIN_DECLS

#define STAMP_TYPE_CONVERSATION_LIST_STORE (stamp_conversation_list_store_get_type())

G_DECLARE_FINAL_TYPE (StampConversationListStore, stamp_conversation_list_store, STAMP, CONVERSATION_LIST_STORE, GListStore)

void
stamp_conversation_list_store_add (StampConversationListStore *self,
                                   StampConversationItem      *item);

void
stamp_conversation_list_store_remove_all (StampConversationListStore *self);

StampConversationListStore *
stamp_conversation_list_store_new (void);

G_END_DECLS
