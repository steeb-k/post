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

#include "stamp-folder-index.h"

/* A folder that changed is re-read whole rather than followed change by
 * change: a removed uid takes its Message-ID with it, so there would be
 * nothing left to look the entry up by. Waiting a moment first folds a
 * sync's worth of arrivals into one pass. */
#define REINDEX_DELAY_SECONDS 2

struct _StampFolderIndex {
  GObject parent_instance;

  CamelStore *store;
  GCancellable *cancellable;

  /* guint64 Message-ID hash -> GPtrArray of interned folder names. The
   * names are interned so the arrays can hold them without copying and
   * a lookup result can be handed straight to a widget. */
  GHashTable *by_message;

  /* interned folder name -> GArray of the guint64s it contributed, so a
   * folder can be taken back out again without walking every entry. */
  GHashTable *by_folder;

  /* interned folder name -> CamelFolder we listen to. */
  GHashTable *watched;

  /* interned folder name -> pending re-read source id. */
  GHashTable *pending;

  /* Folder full names the account resolved as its own Sent, Drafts or
   * Trash, whatever they happen to be called on this server. */
  GHashTable *excluded;
};

enum {
  CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

G_DEFINE_FINAL_TYPE (StampFolderIndex, stamp_folder_index, G_TYPE_OBJECT);

static void on_folder_changed (CamelFolder           *folder,
                               CamelFolderChangeInfo *changes,
                               gpointer               user_data);

static void
stamp_folder_index_dispose (GObject *object)
{
  StampFolderIndex *self = STAMP_FOLDER_INDEX (object);
  GHashTableIter iter;
  gpointer key;
  gpointer value;

  if (self->cancellable)
    g_cancellable_cancel (self->cancellable);

  if (self->pending) {
    g_hash_table_iter_init (&iter, self->pending);
    while (g_hash_table_iter_next (&iter, &key, &value))
      g_source_remove (GPOINTER_TO_UINT (value));
    g_hash_table_remove_all (self->pending);
  }

  if (self->watched) {
    g_hash_table_iter_init (&iter, self->watched);
    while (g_hash_table_iter_next (&iter, &key, &value))
      g_signal_handlers_disconnect_by_func (value, on_folder_changed, self);
    g_hash_table_remove_all (self->watched);
  }

  g_clear_object (&self->cancellable);
  g_clear_object (&self->store);

  G_OBJECT_CLASS (stamp_folder_index_parent_class)->dispose (object);
}

static void
stamp_folder_index_finalize (GObject *object)
{
  StampFolderIndex *self = STAMP_FOLDER_INDEX (object);

  g_clear_pointer (&self->by_message, g_hash_table_unref);
  g_clear_pointer (&self->by_folder, g_hash_table_unref);
  g_clear_pointer (&self->watched, g_hash_table_unref);
  g_clear_pointer (&self->pending, g_hash_table_unref);
  g_clear_pointer (&self->excluded, g_hash_table_unref);

  G_OBJECT_CLASS (stamp_folder_index_parent_class)->finalize (object);
}

static void
stamp_folder_index_class_init (StampFolderIndexClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = stamp_folder_index_dispose;
  object_class->finalize = stamp_folder_index_finalize;

  /* Said once a folder has been read in, so rows drawn before the index
   * knew anything can ask again. */
  signals[CHANGED] = g_signal_new ("changed",
                                   G_TYPE_FROM_CLASS (klass),
                                   G_SIGNAL_RUN_LAST,
                                   0, NULL, NULL, NULL,
                                   G_TYPE_NONE, 0);
}

static void
stamp_folder_index_init (StampFolderIndex *self)
{
  self->by_message = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, (GDestroyNotify)g_ptr_array_unref);
  self->by_folder = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)g_array_unref);
  self->watched = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_object_unref);
  self->pending = g_hash_table_new (g_direct_hash, g_direct_equal);
  self->excluded = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  self->cancellable = g_cancellable_new ();
}

StampFolderIndex *
stamp_folder_index_new (CamelStore *store)
{
  StampFolderIndex *self;

  g_return_val_if_fail (CAMEL_IS_STORE (store), NULL);

  self = g_object_new (STAMP_TYPE_FOLDER_INDEX, NULL);
  self->store = g_object_ref (store);

  return self;
}

/*
 * Auxiliary methods
 */

/* Gmail's own labels -- Important, Starred, All Mail -- live under
 * [Gmail] and are no news to anybody: every read mail is in All Mail,
 * and Starred is already drawn as a star on the row. */
static gboolean
is_gmail_system_folder (const gchar *full_name)
{
  return g_str_has_prefix (full_name, "[Gmail]") ||
         g_str_has_prefix (full_name, "[Google Mail]");
}

/*
 * The same folders again for a server that does not say so.
 *
 * Camel types a folder from what the server advertises, and not every
 * server advertises: Proton's bridge hands over All Mail, Starred,
 * Archive, Sent and Spam looking exactly like folders the user made,
 * which put an "All Mail" chip on every mail in the inbox. Matched only
 * at the top level, so a real folder called Folders/Archive is still a
 * folder the user made and still counts.
 */
static gboolean
is_aggregate_name (const gchar *full_name)
{
  static const gchar * const names[] = {
    "All Mail", "All", "Archive", "Bin", "Deleted Items", "Deleted Messages",
    "Drafts", "Important", "Junk", "Junk E-mail", "Outbox", "Scheduled",
    "Sent", "Sent Items", "Sent Mail", "Sent Messages", "Snoozed", "Spam",
    "Starred", "Trash",
  };

  if (strchr (full_name, '/'))
    return FALSE;

  for (guint i = 0; i < G_N_ELEMENTS (names); i++) {
    if (g_ascii_strcasecmp (full_name, names[i]) == 0)
      return TRUE;
  }

  return FALSE;
}

static gboolean
folder_qualifies (StampFolderIndex *self,
                  CamelFolderInfo  *info)
{
  if (!info->full_name)
    return FALSE;

  if ((info->flags & CAMEL_FOLDER_TYPE_MASK) != CAMEL_FOLDER_TYPE_NORMAL)
    return FALSE;

  if (info->flags & (CAMEL_FOLDER_NOSELECT | CAMEL_FOLDER_VIRTUAL | CAMEL_FOLDER_VTRASH))
    return FALSE;

  if (g_hash_table_contains (self->excluded, info->full_name))
    return FALSE;

  return !is_gmail_system_folder (info->full_name) && !is_aggregate_name (info->full_name);
}

static void
collect_folders (StampFolderIndex *self,
                 CamelFolderInfo  *info,
                 GPtrArray        *names)
{
  for (CamelFolderInfo *iter = info; iter; iter = iter->next) {
    if (folder_qualifies (self, iter))
      g_ptr_array_add (names, (gpointer)g_intern_string (iter->full_name));

    if (iter->child)
      collect_folders (self, iter->child, names);
  }
}

/* Takes every mail @name contributed back out, so the folder can be
 * read in again from scratch. */
static void
forget_folder (StampFolderIndex *self,
               const gchar      *name)
{
  GArray *ids = g_hash_table_lookup (self->by_folder, name);

  if (!ids)
    return;

  for (guint i = 0; i < ids->len; i++) {
    guint64 message_id = g_array_index (ids, guint64, i);
    GPtrArray *folders = g_hash_table_lookup (self->by_message, &message_id);

    if (!folders)
      continue;

    g_ptr_array_remove_fast (folders, (gpointer)name);

    if (folders->len == 0)
      g_hash_table_remove (self->by_message, &message_id);
  }

  g_hash_table_remove (self->by_folder, name);
}

static void
index_folder (StampFolderIndex *self,
              CamelFolder      *folder,
              const gchar      *name)
{
  CamelFolderSummary *summary = camel_folder_get_folder_summary (folder);
  g_autoptr (GPtrArray) uids = NULL;
  GArray *ids;

  if (!summary)
    return;

  forget_folder (self, name);

  uids = camel_folder_summary_dup_uids (summary);
  if (!uids)
    return;

  ids = g_array_new (FALSE, FALSE, sizeof (guint64));

  for (guint i = 0; i < uids->len; i++) {
    g_autoptr (CamelMessageInfo) info = camel_folder_summary_get (summary, uids->pdata[i]);
    GPtrArray *folders;
    guint64 message_id;

    if (!info)
      continue;

    message_id = camel_message_info_get_message_id (info);

    /* A mail camel could not hash -- no Message-ID header, or one it
     * could not parse -- would otherwise all pile onto the same entry
     * and label each other. */
    if (message_id == 0)
      continue;

    folders = g_hash_table_lookup (self->by_message, &message_id);
    if (!folders) {
      guint64 *key = g_new (guint64, 1);

      *key = message_id;
      folders = g_ptr_array_new ();
      g_hash_table_insert (self->by_message, key, folders);
    }

    if (!g_ptr_array_find (folders, name, NULL))
      g_ptr_array_add (folders, (gpointer)name);

    g_array_append_val (ids, message_id);
  }

  g_hash_table_insert (self->by_folder, (gpointer)name, ids);
}

typedef struct {
  StampFolderIndex *self;
  CamelFolder *folder;
} ReindexData;

static void
reindex_data_free (gpointer data)
{
  ReindexData *reindex = data;

  g_clear_object (&reindex->folder);
  g_free (reindex);
}

static gboolean
on_reindex_timeout (gpointer user_data)
{
  ReindexData *reindex = user_data;
  StampFolderIndex *self = reindex->self;
  const gchar *name = g_intern_string (camel_folder_get_full_name (reindex->folder));

  g_hash_table_remove (self->pending, name);

  index_folder (self, reindex->folder, name);
  g_signal_emit (self, signals[CHANGED], 0);

  return G_SOURCE_REMOVE;
}

static void
on_folder_changed (CamelFolder           *folder,
                   CamelFolderChangeInfo *changes,
                   gpointer               user_data)
{
  StampFolderIndex *self = user_data;
  const gchar *name = g_intern_string (camel_folder_get_full_name (folder));
  ReindexData *reindex;

  if (g_hash_table_contains (self->pending, name))
    return;

  reindex = g_new0 (ReindexData, 1);
  reindex->self = self;
  reindex->folder = g_object_ref (folder);

  g_hash_table_insert (self->pending, (gpointer)name,
                       GUINT_TO_POINTER (g_timeout_add_seconds_full (G_PRIORITY_DEFAULT_IDLE,
                                                                     REINDEX_DELAY_SECONDS,
                                                                     on_reindex_timeout,
                                                                     reindex,
                                                                     reindex_data_free)));
}

static void
on_folder_ready (GObject      *source,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  g_autoptr (StampFolderIndex) self = user_data;
  g_autoptr (GError) error = NULL;
  g_autoptr (CamelFolder) folder = camel_store_get_folder_finish (CAMEL_STORE (source), result, &error);
  const gchar *name;

  if (!folder) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_debug ("%s: %s", G_STRFUNC, error ? error->message : "no folder");
    return;
  }

  name = g_intern_string (camel_folder_get_full_name (folder));

  index_folder (self, folder, name);

  if (!g_hash_table_contains (self->watched, name)) {
    g_signal_connect (folder, "changed", G_CALLBACK (on_folder_changed), self);
    g_hash_table_insert (self->watched, (gpointer)name, g_object_ref (folder));
  }

  g_signal_emit (self, signals[CHANGED], 0);
}

/*
 * Public methods
 */

void
stamp_folder_index_exclude (StampFolderIndex *self,
                            const gchar      *full_name)
{
  g_return_if_fail (STAMP_IS_FOLDER_INDEX (self));

  if (full_name && *full_name)
    g_hash_table_add (self->excluded, g_strdup (full_name));
}

void
stamp_folder_index_build (StampFolderIndex *self,
                          CamelFolderInfo  *info)
{
  g_autoptr (GPtrArray) names = g_ptr_array_new ();

  g_return_if_fail (STAMP_IS_FOLDER_INDEX (self));

  if (!info)
    return;

  collect_folders (self, info, names);

  for (guint i = 0; i < names->len; i++) {
    const gchar *name = names->pdata[i];

    if (g_hash_table_contains (self->watched, name))
      continue;

    /* Asked for one at a time and asynchronously: the store answers
     * from the summary already on disk, but a folder it has never seen
     * makes it go and look, and that must not be waited on here. */
    camel_store_get_folder (self->store, name, CAMEL_STORE_FOLDER_NONE,
                            G_PRIORITY_LOW, self->cancellable,
                            on_folder_ready, g_object_ref (self));
  }
}

GPtrArray *
stamp_folder_index_lookup (StampFolderIndex *self,
                           guint64           message_id,
                           const gchar      *exclude_full_name)
{
  GPtrArray *folders;
  GPtrArray *result;

  g_return_val_if_fail (STAMP_IS_FOLDER_INDEX (self), NULL);

  if (message_id == 0)
    return NULL;

  folders = g_hash_table_lookup (self->by_message, &message_id);
  if (!folders || folders->len == 0)
    return NULL;

  /* The common case by far: the mail is only in the folder being read,
   * and there is nothing to draw. */
  if (folders->len == 1 && g_strcmp0 (folders->pdata[0], exclude_full_name) == 0)
    return NULL;

  result = g_ptr_array_new ();

  for (guint i = 0; i < folders->len; i++) {
    if (g_strcmp0 (folders->pdata[i], exclude_full_name) != 0)
      g_ptr_array_add (result, folders->pdata[i]);
  }

  if (result->len == 0) {
    g_ptr_array_unref (result);
    return NULL;
  }

  return result;
}
