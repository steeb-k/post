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

#include "stamp-folder-index.h"

G_BEGIN_DECLS

/*
 * Set on a mail that turned up unread in a starred conversation, and
 * taken off again when the user opens it.
 *
 * A star means "keep me posted about this one", so a conversation that
 * gets a new mail has to stay marked as such until it has actually been
 * looked at -- not until the mark-read timeout quietly reads it while
 * the list is scrolled past. Camel has nothing that says "read but not
 * seen by a person", so this flag says it.
 *
 * It marks the new mail rather than the reading of it deliberately: a
 * mail that predates the flag carries nothing, and so a conversation
 * starred long ago does not turn bold the first time this runs. The
 * "X-" prefix keeps it out of the label chips; see
 * stamp_conversation_item_get_labels().
 */
#define STAMP_FLAG_NEW "X-Post-New"

#define STAMP_TYPE_CONVERSATION_ITEM (stamp_conversation_item_get_type())
G_DECLARE_FINAL_TYPE (StampConversationItem, stamp_conversation_item, STAMP, CONVERSATION_ITEM, GObject);

StampConversationItem *
stamp_conversation_item_new (CamelFolderThreadNode *thread_node,
                             const gchar            *service_uid);

const gchar *
stamp_conversation_item_get_subject (StampConversationItem *self);

CamelFolderThreadNode *
stamp_conversation_item_get_node (StampConversationItem *self);

const gchar *
stamp_conversation_item_get_from (StampConversationItem *self);

gint64
stamp_conversation_item_get_date (StampConversationItem *self);

guint
stamp_conversation_item_get_num_messages (StampConversationItem *self);

gboolean
stamp_conversation_item_get_unread (StampConversationItem *self);

/*
 * Note that the user has looked at this conversation, so that a star no
 * longer keeps it bold. See STAMP_FLAG_NEW.
 */
void
stamp_conversation_item_acknowledge (StampConversationItem *self);

gboolean
stamp_conversation_item_has_attachment (StampConversationItem *self);

gboolean
stamp_conversation_item_get_flagged (StampConversationItem *self);

void
stamp_conversation_item_set_flagged (StampConversationItem *self,
                                     gboolean               flagged);

/*
 * The star as the row draws it: the flag, unless a click of it is still
 * waiting out its delay. See stamp_conversation_item_toggle_star().
 */
gboolean
stamp_conversation_item_get_star_shown (StampConversationItem *self);

void
stamp_conversation_item_toggle_star (StampConversationItem *self);

void
stamp_conversation_item_flush_star (StampConversationItem *self);

const gchar *
stamp_conversation_item_get_preview (StampConversationItem *self);

guint
stamp_conversation_item_get_timestamp (StampConversationItem *self);

gchar *
stamp_conversation_item_get_mail (StampConversationItem *self);

void
stamp_conversation_item_notify_unread (StampConversationItem *self);

gboolean
stamp_conversation_item_get_answered (StampConversationItem *self);

gboolean
stamp_conversation_item_get_forwarded (StampConversationItem *self);

gboolean
stamp_conversation_item_get_calendar (StampConversationItem *self);

gchar *
stamp_conversation_item_get_service_uid (StampConversationItem *self);

void
stamp_conversation_item_set_hidden (StampConversationItem *self,
                                    gboolean               hidden);

gboolean
stamp_conversation_item_get_hidden (StampConversationItem *self);

const gchar *
stamp_conversation_item_get_uid (StampConversationItem *self);

void
stamp_conversation_item_update (StampConversationItem *self,
                                CamelMessageInfo      *info);

/*
 * The folders this conversation sits in besides @exclude_full_name --
 * on Gmail, its labels. See StampFolderIndex for why folders are the
 * answer to a question about labels.
 *
 * Returns: (transfer container) (nullable): interned folder full names.
 */
GPtrArray *
stamp_conversation_item_get_folders (StampConversationItem *self,
                                     StampFolderIndex      *index,
                                     const gchar           *exclude_full_name);

GPtrArray *
stamp_conversation_item_get_labels (StampConversationItem *self);

void
stamp_conversation_item_set_label (StampConversationItem *self,
                                   const gchar            *label,
                                   gboolean               state);

gboolean
stamp_conversation_item_is_important (StampConversationItem *self);

G_END_DECLS
