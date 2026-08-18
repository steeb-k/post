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

#include <camel/camel.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define STAMP_TYPE_FOLDER_INDEX (stamp_folder_index_get_type ())

G_DECLARE_FINAL_TYPE (StampFolderIndex, stamp_folder_index, STAMP, FOLDER_INDEX, GObject);

/*
 * Which other folders a mail sits in.
 *
 * On Gmail a label is a folder and a labelled mail is filed in both the
 * Inbox and the label -- so the labels of a mail are readable without
 * asking Google anything, by noticing the same mail in more than one
 * place. IMAP gives no cross-folder identity to work from, but camel
 * already hashes each Message-ID into a guint64 for threading, and that
 * hash is the same in every folder the mail was filed in.
 *
 * Only ordinary folders are indexed: Sent, Trash, Junk, Archive, All
 * Mail and the rest carry a folder type, and saying that a mail is "in
 * All Mail" tells the reader nothing.
 */
StampFolderIndex *
stamp_folder_index_new (CamelStore *store);

/*
 * Leaves @full_name out of the index. For the folders only the account
 * can name -- the Sent and Drafts it resolved for this server -- since
 * a server that does not advertise them leaves camel nothing to type
 * them by. Call before stamp_folder_index_build().
 */
void
stamp_folder_index_exclude (StampFolderIndex *self,
                            const gchar      *full_name);

/*
 * Walks @info, indexes what qualifies, and keeps each folder it took in
 * up to date afterwards. Safe to call again with a newer tree; folders
 * already indexed are left alone.
 */
void
stamp_folder_index_build (StampFolderIndex *self,
                          CamelFolderInfo  *info);

/*
 * The folder full names holding the mail whose Message-ID hashes to
 * @message_id, minus @exclude_full_name -- the folder being read, which
 * has no news in it. NULL when there is nothing to say.
 *
 * Returns: (transfer container) (nullable): the names, which are
 *   interned and so outlive the array.
 */
GPtrArray *
stamp_folder_index_lookup (StampFolderIndex *self,
                           guint64           message_id,
                           const gchar      *exclude_full_name);

G_END_DECLS
